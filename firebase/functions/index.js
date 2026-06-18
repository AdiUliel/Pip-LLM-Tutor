/**
 * Firebase Functions for the Emotional Tutor LLM interface.
 *
 * Edge-to-edge flow (ESP32 device + Flutter app share this backend):
 *
 *   1. Device signs in anonymously and creates a session document:
 *        /sessions/{sessionId}
 *        { childId, deviceId, subject, startedAt, status:"starting",
 *          awaitingFirstQuestion:true }
 *   2. `onSessionCreated` generates the first question (tutorEngine),
 *      synthesizes it to a WAV in Storage, and writes:
 *        { status:"active", currentQuestion, currentExpectedAnswer,
 *          currentQuestionAudioUrl }
 *   3. Device speaks `currentQuestionAudioUrl`, then records the child's
 *      spoken answer and writes a learning turn:
 *        /sessions/{sessionId}/exchanges/{exchangeId}
 *        { type:"learning_turn", status:"pending", childAnswer:"..." }
 *   4. `answerQuestion` (Firestore trigger) checks the answer, asks Gemini for
 *      emotional feedback, generates the next question, synthesizes
 *      feedback+next-question to a WAV, and writes back:
 *        { status:"done", isCorrect, spokenFeedback, emotion, nextQuestion,
 *          expectedAnswer, shouldTakeBreak, audioUrl }
 *   5. Device speaks `audioUrl`, shows the matching pip_face emotion, and
 *      loops back to step 3 for the next answer.
 *
 * Backward compatibility:
 *   If an exchange contains only { question, status:"pending" }, the function
 *   still answers it as a free-text homework-helper message (also with audio).
 *
 * Region: ALL functions run in europe-west10 (set via setGlobalOptions).
 */

const { setGlobalOptions } = require("firebase-functions/v2");
const { onDocumentCreated } = require("firebase-functions/v2/firestore");
const { onCall, onRequest, HttpsError } = require("firebase-functions/v2/https");
const { initializeApp } = require("firebase-admin/app");
const { getFirestore, FieldValue } = require("firebase-admin/firestore");
const { getStorage } = require("firebase-admin/storage");
const { getAuth } = require("firebase-admin/auth");
const { GoogleGenAI } = require("@google/genai");

const { createInitialQuestion, processLearningTurn } = require("./tutorEngine");

// FCM push notifications for the parent app — re-exported so they deploy
// alongside the existing tutor functions (`firebase deploy --only functions`).
const {
  notifyOnSessionStarted,
  monitorTutor,
} = require("./notifications");
exports.notifyOnSessionStarted = notifyOnSessionStarted;
exports.monitorTutor = monitorTutor;

// ── Every function in this codebase deploys to europe-west10 ─────────────────
const FUNCTIONS_REGION = process.env.FUNCTIONS_REGION || "europe-west10";
setGlobalOptions({ region: FUNCTIONS_REGION });

initializeApp();
const db = getFirestore();

// Vertex AI / Gemini.
//   - location "global" is the most reliable endpoint for Gemini 2.x model
//     availability (Vertex does not serve every model in every region).
//   - GEMINI_LOCATION can override it if you want to pin a specific region.
const ai = new GoogleGenAI({
  vertexai: true,
  project: process.env.GCLOUD_PROJECT,
  location: process.env.GEMINI_LOCATION || "global",
});

const GEMINI_MODEL = process.env.GEMINI_MODEL || "gemini-2.5-flash";

// Child-safe safety settings applied to every Gemini call in this file.
const CHILD_SAFETY_SETTINGS = [
  { category: "HARM_CATEGORY_HARASSMENT",        threshold: "BLOCK_LOW_AND_ABOVE" },
  { category: "HARM_CATEGORY_HATE_SPEECH",        threshold: "BLOCK_LOW_AND_ABOVE" },
  { category: "HARM_CATEGORY_SEXUALLY_EXPLICIT",  threshold: "BLOCK_LOW_AND_ABOVE" },
  { category: "HARM_CATEGORY_DANGEROUS_CONTENT",  threshold: "BLOCK_LOW_AND_ABOVE" },
];

const HOMEWORK_HELPER_PROMPT = `אתה פיפ — עוזר שיעורי בית חם ומעודד לילדים בגילאי 6–12.
תפקידך לעזור להם להבין את הנושא — לא לתת להם את התשובה ישירות.

כללים:
- דבר בעברית פשוטה ויפה שמתאימה לגיל הילד.
- השמור על תשובות קצרות (1–3 משפטים) — הן יישמעו בקול על ידי רובוט.
- השתמש במילים חיוביות ומעודדות.
- אם הילד שואל שאלה שאינה קשורה ללמידה, הפנה אותו בחביבות חזרה לשיעורים.
- אל תזכיר שאתה AI, רובוט, מחשב, או תוכנת מחשב.
- אל תדון בנושאים לא מתאימים לגיל.
- אם השאלה באנגלית, ענה באנגלית פשוטה ועודד.`;

// ── Child-identification helpers ──────────────────────────────────────────────

function matchChildByName(children, transcript) {
  const t = String(transcript || "").trim().toLowerCase();
  if (!t) return null;
  // Exact match first, then substring match
  return (
    children.find((c) => String(c.name || "").toLowerCase() === t) ||
    children.find((c) => {
      const n = String(c.name || "").toLowerCase();
      return n.includes(t) || t.includes(n);
    }) ||
    null
  );
}

function parseSubject(transcript) {
  const t = String(transcript || "").toLowerCase();
  if (t.includes("אנגלית") || t.includes("english") || t.includes("אנגל")) return "english";
  return "math";
}

// ─────────────────────────────────────────────────────────────────────────────
// Helper: synthesizeAndStore
//
// Calls Google Text-to-Speech (via ADC — no API key needed) and uploads the
// resulting WAV file to Firebase Storage. Returns the public download URL.
//
// ⚠️  Before this works you must:
//   1. Enable Firebase Storage in Firebase Console → Build → Storage
//   2. Enable the TTS API: console.cloud.google.com/apis/library/texttospeech.googleapis.com
//
// The device plays this WAV directly (it streams raw 16 kHz LINEAR16 PCM and
// skips the 44-byte WAV header), so the audioConfig below must stay in sync
// with tts_player.h on the ESP32.
// ─────────────────────────────────────────────────────────────────────────────
async function synthesizeAndStore(text, fileId) {
  const speech = String(text || "").trim();
  if (!speech) return "";

  const tokenRes = await fetch(
    "http://metadata.google.internal/computeMetadata/v1/instance/service-accounts/default/token",
    { headers: { "Metadata-Flavor": "Google" } }
  );
  const { access_token } = await tokenRes.json();

  const ttsRes = await fetch(
    "https://texttospeech.googleapis.com/v1/text:synthesize",
    {
      method: "POST",
      headers: {
        "Content-Type":  "application/json",
        "Authorization": `Bearer ${access_token}`,
      },
      body: JSON.stringify({
        input: { text: speech },
        voice: { languageCode: "he-IL", name: "he-IL-Standard-A" },
        audioConfig: {
          audioEncoding:   "LINEAR16",
          sampleRateHertz: 16000,
        },
      }),
    }
  );

  if (!ttsRes.ok) {
    const err = await ttsRes.text();
    throw new Error(`TTS HTTP ${ttsRes.status}: ${err}`);
  }

  const { audioContent } = await ttsRes.json();
  const wavBuffer = Buffer.from(audioContent, "base64");

  const bucket = getStorage().bucket();
  const file   = bucket.file(`tts/${fileId}.wav`);
  await file.save(wavBuffer, { contentType: "audio/wav", resumable: false });
  await file.makePublic();

  const publicUrl = `https://storage.googleapis.com/${bucket.name}/tts/${fileId}.wav`;
  console.log(`[TTS] Uploaded ${wavBuffer.length} bytes → ${publicUrl}`);
  return publicUrl;
}

// Wrapper passed into the tutor engine. Never throws — audio is best-effort so a
// TTS hiccup can't block the text answer from being written.
async function safeSynthesize(text, fileId) {
  try {
    return await synthesizeAndStore(text, fileId);
  } catch (err) {
    console.warn("[TTS] synthesis failed (continuing without audio):", err.message);
    return "";
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// identify_child exchange handler
//
// The device sends the child's spoken name. We fuzzy-match it against the
// parent's children profiles, update the session with the matched childId,
// and return a spoken prompt asking which subject to study.
// ─────────────────────────────────────────────────────────────────────────────
async function handleIdentifyChild(sessionId, exchangeId, data) {
  const sessionRef = db.collection("sessions").doc(sessionId);
  const exchangeRef = sessionRef.collection("exchanges").doc(exchangeId);

  const sessionSnap = await sessionRef.get();
  if (!sessionSnap.exists) throw new Error("Session not found");
  const session = sessionSnap.data();

  const parentId = session.parentId;
  if (!parentId) throw new Error("Session has no parentId");

  const childrenSnap = await db.collection("children")
    .where("parentId", "==", parentId)
    .get();
  const children = childrenSnap.docs.map((d) => ({ id: d.id, ...d.data() }));

  const transcript = String(data.childNameTranscript || data.childAnswer || "").trim();
  const matched = matchChildByName(children, transcript);

  let promptText;
  if (matched) {
    promptText = `שלום ${matched.name}! מה נלמד היום — חשבון או אנגלית?`;
    await sessionRef.set(
      { childId: matched.id, status: "selecting_subject", lastActivity: FieldValue.serverTimestamp() },
      { merge: true }
    );
  } else {
    promptText = `לא הבנתי את השם. תגיד לי שוב — מי אתה?`;
  }

  const audioUrl = await safeSynthesize(promptText, `${exchangeId}_identify`);
  await exchangeRef.update({
    status: "done",
    matchedChildId: matched?.id || null,
    matchedChildName: matched?.name || null,
    promptText,
    audioUrl,
    answeredAt: FieldValue.serverTimestamp(),
  });
}

// ─────────────────────────────────────────────────────────────────────────────
// identify_subject exchange handler
//
// The child says "חשבון" or "אנגלית". We parse the subject, seed the first
// question, and make the session active.
// ─────────────────────────────────────────────────────────────────────────────
async function handleIdentifySubject(sessionId, exchangeId, data) {
  const sessionRef = db.collection("sessions").doc(sessionId);
  const exchangeRef = sessionRef.collection("exchanges").doc(exchangeId);

  const sessionSnap = await sessionRef.get();
  if (!sessionSnap.exists) throw new Error("Session not found");
  const session = sessionSnap.data();

  const transcript = String(data.subjectTranscript || data.childAnswer || "").trim();
  const subject = parseSubject(transcript);

  let child = null;
  if (session.childId) {
    const childSnap = await db.collection("children").doc(session.childId).get();
    if (childSnap.exists) child = { id: childSnap.id, ...childSnap.data() };
  }

  await sessionRef.set({ subject, status: "starting" }, { merge: true });
  const question = await createInitialQuestion(db, sessionRef, { ...session, subject }, child);

  const subjectLabel = subject === "math" ? "חשבון" : "אנגלית";
  const greeting = child?.name
    ? `יופי ${child.name}! נתחיל ${subjectLabel}. הנה השאלה הראשונה:`
    : `יופי! נתחיל ${subjectLabel}. הנה השאלה הראשונה:`;
  const spokenText = `${greeting} ${question.prompt}`;
  const audioUrl = await safeSynthesize(spokenText, `${exchangeId}_subject`);

  await sessionRef.set(
    { currentQuestionAudioUrl: audioUrl, status: "active", lastActivity: FieldValue.serverTimestamp() },
    { merge: true }
  );
  await exchangeRef.update({
    status: "done",
    subject,
    promptText: spokenText,
    audioUrl,
    answeredAt: FieldValue.serverTimestamp(),
  });
}

// ─────────────────────────────────────────────────────────────────────────────
// Free-text homework-helper answer (legacy / fallback path).
// ─────────────────────────────────────────────────────────────────────────────
async function answerFreeTextQuestion(sessionId, exchangeId, data) {
  const exchangeRef = db
    .collection("sessions")
    .doc(sessionId)
    .collection("exchanges")
    .doc(exchangeId);

  const [sessionSnap, historySnap] = await Promise.all([
    db.collection("sessions").doc(sessionId).get(),
    db
      .collection("sessions")
      .doc(sessionId)
      .collection("exchanges")
      .where("status", "==", "done")
      .orderBy("askedAt", "asc")
      .limit(10)
      .get(),
  ]);

  const session = sessionSnap.exists ? sessionSnap.data() : {};
  let child = null;
  if (session.childId) {
    const childSnap = await db.collection("children").doc(session.childId).get();
    if (childSnap.exists) child = { id: childSnap.id, ...childSnap.data() };
  }

  const childContext = child
    ? `\nהילד שמולך: שם "${child.name || "חבר"}", גיל ${child.age || 8}.`
    : "";
  const systemInstruction = HOMEWORK_HELPER_PROMPT + childContext;

  const contents = [];
  historySnap.forEach((doc) => {
    const d = doc.data();
    if (d.question) contents.push({ role: "user", parts: [{ text: d.question }] });
    if (d.answer) contents.push({ role: "model", parts: [{ text: d.answer }] });
  });
  contents.push({ role: "user", parts: [{ text: data.question }] });

  const result = await ai.models.generateContent({
    model: GEMINI_MODEL,
    contents,
    config: {
      systemInstruction,
      maxOutputTokens: 180,
      temperature: 0.65,
      safetySettings: CHILD_SAFETY_SETTINGS,
    },
  });

  const answer = String(result.text || "").trim();

  // Synthesize BEFORE marking done so the device never polls a done doc with an
  // empty audioUrl.
  const audioUrl = await safeSynthesize(answer, exchangeId);

  await exchangeRef.update({
    answer,
    spokenFeedback: answer,
    audioUrl,
    status: "done",
    emotion: "encouraging",
    answeredAt: FieldValue.serverTimestamp(),
  });

  await db.collection("sessions").doc(sessionId).set(
    { lastActivity: FieldValue.serverTimestamp() },
    { merge: true }
  );
}

// ─────────────────────────────────────────────────────────────────────────────
// Trigger: a session was created. If the device flagged awaitingFirstQuestion,
// generate the opening question and synthesize its audio onto the session doc.
// (The callable startLearningSession path seeds its own question and does NOT
// set this flag, so it is skipped here — no double generation.)
// ─────────────────────────────────────────────────────────────────────────────
exports.onSessionCreated = onDocumentCreated(
  "sessions/{sessionId}",
  async (event) => {
    const snap = event.data;
    if (!snap) return;
    const data = snap.data();
    const { sessionId } = event.params;
    const sessionRef = snap.ref;

    // New identification flow: greet the child and ask for their name.
    if (data.status === "identifying") {
      const greetingText = "שלום! אני פיפ, המורה הרובוט שלך. מי אתה? תגיד לי את שמך!";
      const audioUrl = await safeSynthesize(greetingText, `${sessionId}_greeting`);
      await sessionRef.set(
        { greetingAudioUrl: audioUrl, lastActivity: FieldValue.serverTimestamp() },
        { merge: true }
      );
      console.log(`[${sessionId}] identifying session — greeting ready`);
      return;
    }

    if (data.awaitingFirstQuestion !== true) return;

    try {
      let child = null;
      if (data.childId) {
        const childSnap = await db.collection("children").doc(data.childId).get();
        if (childSnap.exists) child = { id: childSnap.id, ...childSnap.data() };
      }

      const question = await createInitialQuestion(db, sessionRef, data, child);

      const childName = child?.name ? child.name : null;
      const subjectLabel = (data.subject || "math") === "math" ? "מתמטיקה" : "אנגלית";
      const greeting = childName
        ? `שלום ${childName}! אני פיפ, המורה הרובוט שלך. היום נתרגל יחד ${subjectLabel}. מוכן${child?.gender === "girl" ? "ה" : ""}? הנה השאלה הראשונה:`
        : `שלום! אני פיפ, המורה הרובוט שלך. היום נתרגל יחד ${subjectLabel}. מוכנים? הנה השאלה הראשונה:`;
      const audioUrl = await safeSynthesize(`${greeting} ${question.prompt}`, `${sessionId}_q0`);

      await sessionRef.set(
        {
          status: "active",
          currentQuestionAudioUrl: audioUrl,
          awaitingFirstQuestion: false,
          lastActivity: FieldValue.serverTimestamp(),
        },
        { merge: true }
      );
      console.log(`[${sessionId}] first question ready: "${question.prompt}"`);
    } catch (err) {
      console.error(`[${sessionId}] onSessionCreated failed:`, err);
      await sessionRef.set(
        { status: "error", error: err.message || "init failed", awaitingFirstQuestion: false },
        { merge: true }
      );
    }
  }
);

// ─────────────────────────────────────────────────────────────────────────────
// Trigger: a new exchange was created → process it.
// ─────────────────────────────────────────────────────────────────────────────
exports.answerQuestion = onDocumentCreated(
  "sessions/{sessionId}/exchanges/{exchangeId}",
  async (event) => {
    const { sessionId, exchangeId } = event.params;
    const snap = event.data;
    if (!snap) return;

    const data = snap.data();
    if (data.status !== "pending") return;

    const exchangeRef = db
      .collection("sessions")
      .doc(sessionId)
      .collection("exchanges")
      .doc(exchangeId);

    await exchangeRef.update({ status: "processing" });

    try {
      // ── Identification exchanges ──────────────────────────────────────────
      if (data.type === "identify_child") {
        await handleIdentifyChild(sessionId, exchangeId, data);
        console.log(`[${sessionId}/${exchangeId}] identify_child done`);
        return;
      }
      if (data.type === "identify_subject") {
        await handleIdentifySubject(sessionId, exchangeId, data);
        console.log(`[${sessionId}/${exchangeId}] identify_subject done`);
        return;
      }

      const isLearningTurn =
        data.type === "learning_turn" ||
        data.childAnswer !== undefined ||
        data.childAnswerTranscript !== undefined;

      if (isLearningTurn) {
        const result = await processLearningTurn({
          db,
          ai,
          model: GEMINI_MODEL,
          safetySettings: CHILD_SAFETY_SETTINGS,
          synthesize: safeSynthesize,
          sessionId,
          exchangeId,
          exchangeData: data,
        });
        console.log(`[${sessionId}/${exchangeId}] learning turn done`, result);
      } else if (data.question) {
        await answerFreeTextQuestion(sessionId, exchangeId, data);
        console.log(`[${sessionId}/${exchangeId}] free-text helper answer done`);
      } else {
        await exchangeRef.update({
          status: "error",
          error: "Missing childAnswer or question",
          answeredAt: FieldValue.serverTimestamp(),
        });
      }
    } catch (err) {
      console.error("LLM tutor function failed:", err);
      await exchangeRef.update({
        status: "error",
        error: err.message || "Unknown error",
        answeredAt: FieldValue.serverTimestamp(),
      });
    }
  }
);

/**
 * Callable helper for Flutter/testing: create a session and generate the first
 * question immediately. (ESP32 creates sessions directly via REST instead.)
 */
exports.startLearningSession = onCall(async (request) => {
  if (!request.auth) {
    throw new HttpsError("unauthenticated", "Must be signed in.");
  }

  const { childId, deviceId, subject = "math" } = request.data || {};
  if (!childId) throw new HttpsError("invalid-argument", "childId is required.");

  const childSnap = await db.collection("children").doc(childId).get();
  if (!childSnap.exists) throw new HttpsError("not-found", "Child was not found.");

  const child = { id: childSnap.id, ...childSnap.data() };
  const sessionRef = db.collection("sessions").doc();
  await sessionRef.set({
    childId,
    parentId: child.parentId || request.auth.uid,
    deviceId: deviceId || child.deviceId || request.auth.uid,
    subject,
    status: "starting",
    startedAt: FieldValue.serverTimestamp(),
    lastActivity: FieldValue.serverTimestamp(),
    questionsAsked: 0,
    correctCount: 0,
    wrongCount: 0,
    starsEarned: 0,
    longestStreak: 0,
    consecutiveCorrect: 0,
    consecutiveWrong: 0,
    moodSummary: 3,
  });

  const question = await createInitialQuestion(db, sessionRef, { childId, subject }, child);
  const audioUrl = await safeSynthesize(question.prompt, `${sessionRef.id}_q0`);
  await sessionRef.set({ currentQuestionAudioUrl: audioUrl }, { merge: true });

  return {
    sessionId: sessionRef.id,
    currentQuestion: question.prompt,
    expectedAnswer: question.expectedAnswer,
    currentQuestionAudioUrl: audioUrl,
    difficulty: question.difficulty,
  };
});

/**
 * Callable helper for Flutter/testing: submit a child answer. The trigger above
 * will process the created exchange document.
 */
exports.submitChildAnswer = onCall(async (request) => {
  if (!request.auth) {
    throw new HttpsError("unauthenticated", "Must be signed in.");
  }

  const { sessionId, childAnswer } = request.data || {};
  if (!sessionId || !childAnswer) {
    throw new HttpsError("invalid-argument", "sessionId and childAnswer are required.");
  }

  const exchangeRef = db
    .collection("sessions")
    .doc(sessionId)
    .collection("exchanges")
    .doc();

  await exchangeRef.set({
    type: "learning_turn",
    status: "pending",
    childAnswer,
    askedAt: FieldValue.serverTimestamp(),
    createdBy: request.auth.uid,
  });

  return { exchangeId: exchangeRef.id, status: "pending" };
});

// ─────────────────────────────────────────────────────────────────────────────
// Cloud Function: transcribeAudio
//
// HTTP endpoint used by the ESP32 to transcribe audio without needing a
// Google STT API key on the device. The ESP32 sends raw PCM audio (base64),
// this function forwards it to Google Speech-to-Text using ADC.
//
// Request:  POST  { audio: "<base64 LINEAR16 PCM>", languageCode: "he-IL" }
// Headers:  Authorization: Bearer <Firebase idToken>
// Response: { transcript: "the transcribed text" }
// ─────────────────────────────────────────────────────────────────────────────
exports.transcribeAudio = onRequest(
  { cors: false, timeoutSeconds: 30 },
  async (req, res) => {
    if (req.method !== "POST") {
      return res.status(405).send("Method Not Allowed");
    }

    const authHeader = req.headers.authorization || "";
    if (!authHeader.startsWith("Bearer ")) {
      return res.status(401).json({ error: "Missing auth token" });
    }
    try {
      await getAuth().verifyIdToken(authHeader.split("Bearer ")[1]);
    } catch {
      return res.status(401).json({ error: "Invalid auth token" });
    }

    const { audio, languageCode = "he-IL" } = req.body;
    if (!audio) return res.status(400).json({ error: "No audio provided" });

    let accessToken;
    try {
      const tokenRes = await fetch(
        "http://metadata.google.internal/computeMetadata/v1/instance/service-accounts/default/token",
        { headers: { "Metadata-Flavor": "Google" } }
      );
      const tokenData = await tokenRes.json();
      accessToken = tokenData.access_token;
    } catch (err) {
      console.error("Failed to get access token:", err);
      return res.status(500).json({ error: "Auth error" });
    }

    try {
      const sttRes = await fetch(
        "https://speech.googleapis.com/v1/speech:recognize",
        {
          method: "POST",
          headers: {
            "Content-Type":  "application/json",
            "Authorization": `Bearer ${accessToken}`,
          },
          body: JSON.stringify({
            audio: { content: audio },
            config: {
              encoding:        "LINEAR16",
              sampleRateHertz: 16000,
              languageCode,
              model:           "default",
            },
          }),
        }
      );

      if (!sttRes.ok) {
        const errText = await sttRes.text();
        console.error("STT error:", errText);
        return res.status(502).json({ error: "STT failed", detail: errText });
      }

      const sttData = await sttRes.json();
      const transcript =
        sttData.results?.[0]?.alternatives?.[0]?.transcript ?? "";

      console.log(`Transcribed: "${transcript}"`);
      return res.json({ transcript });
    } catch (err) {
      console.error("STT call failed:", err);
      return res.status(500).json({ error: err.message });
    }
  }
);
