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
 *      synthesizes it to base64-encoded WAV (no Storage), and writes:
 *        { status:"active", currentQuestion, currentExpectedAnswer,
 *          currentQuestionAudioData }
 *   3. Device decodes `currentQuestionAudioData`, speaks it, then records
 *      the child's spoken answer and writes a learning turn:
 *        /sessions/{sessionId}/exchanges/{exchangeId}
 *        { type:"learning_turn", status:"pending", childAnswer:"..." }
 *   4. `answerQuestion` (Firestore trigger) checks the answer, asks Gemini for
 *      emotional feedback, generates the next question, synthesizes
 *      feedback+next-question to a base64 WAV, and writes back:
 *        { status:"done", isCorrect, spokenFeedback, emotion, nextQuestion,
 *          expectedAnswer, shouldTakeBreak, audioData }
 *   5. Device speaks `audioData`, shows the matching pip_face emotion, and
 *      loops back to step 3 for the next answer.
 *
 * Backward compatibility:
 *   If an exchange contains only { question, status:"pending" }, the function
 *   still answers it as a free-text homework-helper message (also with audio).
 *
 * Region: ALL functions run in europe-west10 (set via setGlobalOptions).
 */

const { setGlobalOptions } = require("firebase-functions/v2");

// ── Every function in this codebase deploys to europe-west10 ─────────────────
// MUST be set before any require() that defines triggers — onSchedule/onCall
// pick up the region at definition time, not at deploy time.
const FUNCTIONS_REGION = process.env.FUNCTIONS_REGION || "europe-west10";
setGlobalOptions({ region: FUNCTIONS_REGION });

const { onDocumentCreated } = require("firebase-functions/v2/firestore");
const { onCall, onRequest, HttpsError } = require("firebase-functions/v2/https");
const { initializeApp } = require("firebase-admin/app");
const { initializeFirestore, FieldValue } = require("firebase-admin/firestore");
const { getStorage } = require("firebase-admin/storage");
const { getAuth } = require("firebase-admin/auth");
const { GoogleGenAI } = require("@google/genai");

const { createInitialQuestion, processLearningTurn } = require("./tutorEngine");

// FCM push notifications for the parent app — re-exported so they deploy
// alongside the existing tutor functions (`firebase deploy --only functions`).
const {
  notifyOnSessionStarted,
  monitorTutor,
  sendSessionEndedNow,
} = require("./notifications");
exports.notifyOnSessionStarted = notifyOnSessionStarted;
exports.monitorTutor = monitorTutor;

// PDF / image / text material → Q&A pairs via Gemini multimodal.
const { extractQuestionsFromMaterial } = require("./extractQuestions");
exports.extractQuestionsFromMaterial = extractQuestionsFromMaterial;

const app = initializeApp();
// preferRest: skip the Admin SDK's gRPC Firestore channel (whose lazy init adds
// ~2–5 s on a cold start) and use the REST transport, which matches this code's
// simple read/write pattern. MUST run before any getFirestore() in the codebase;
// since every function loads this module at cold start, this is that first call.
const db = initializeFirestore(app, { preferRest: true });

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
  return (
    // 1. Exact name.
    children.find((c) => String(c.name || "").toLowerCase() === t) ||
    // 2. Spoken phrase contains the FULL name ("אני נעמה" → "נעמה").
    children.find((c) => {
      const n = String(c.name || "").toLowerCase();
      return n.length >= 2 && t.includes(n);
    }) ||
    // 3. Near-complete capture: transcript is a prefix that's almost the whole
    //    name (STT dropped the last letter). NOT any short fragment — that's
    //    how "מה" wrongly matched "נעמה" (n.includes(t)).
    children.find((c) => {
      const n = String(c.name || "").toLowerCase();
      return n.length >= 3 && n.startsWith(t) && t.length >= n.length - 1;
    }) ||
    null
  );
}

// Returns "english" | "math" | null. null means neither subject was clearly
// said, so the caller should re-ask instead of silently defaulting to math
// (which made the device start a math lesson regardless of what the kid said).
function parseSubject(transcript) {
  const t = String(transcript || "").toLowerCase();
  if (t.includes("אנגלית") || t.includes("english") || t.includes("אנגל")) return "english";
  if (t.includes("חשבון") || t.includes("חשבן") || t.includes("מתמטיקה") ||
      t.includes("חשבונ") || t.includes("math") || t.includes("מתמט")) return "math";
  return null;
}

// ─────────────────────────────────────────────────────────────────────────────
// Helper: synthesizeAudio
//
// Calls Google Text-to-Speech (via ADC — no API key needed), uploads the WAV
// to Storage under `tts/<fileId>.wav`, and returns a public HTTPS URL the
// ESP32 streams chunk-by-chunk through speakFromUrl(). Inline base64 was
// tried (commit 5c1a252) but 200–550 KB stringValues overflow Arduino-String
// heap on the device and silently land as a too-short field, so the device
// would skip the audio entirely. URL playback is the proven path.
//
// ⚠️  Before this works you must:
//   1. Enable the TTS API: console.cloud.google.com/apis/library/texttospeech.googleapis.com
//   2. Bucket lifecycle: set 1-day TTL on the `tts/` prefix so files don't
//      accumulate. One-shot, outside this code:
//        gsutil lifecycle set tts-lifecycle.json gs://<bucket>
//
// The device plays this WAV directly (streams raw 16 kHz LINEAR16 PCM and
// skips the 44-byte WAV header), so the audioConfig below must stay in sync
// with tts_player.h on the ESP32.
// ─────────────────────────────────────────────────────────────────────────────
// Low-level Google TTS call → MP3 Buffer (no Storage). Shared by the URL-based
// path (synthesizeAudio) and the bytes-in-response path (processTurn).
async function ttsToMp3Buffer(text) {
  const speech = String(text || "").trim();
  if (!speech) return null;

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
          // MP3 @ 16 kHz. The ESP32 decodes it with the helix decoder in
          // tts_player.h. OPUS would be smaller but needs the v3.x library /
          // core 3.x; this project is on core 2.0.x, so MP3 it is.
          audioEncoding:   "MP3",
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
  return Buffer.from(audioContent, "base64");
}

// Upload an MP3 buffer to Storage and return a public URL. Best-effort caller
// decides; this throws on failure.
async function uploadMp3(buf, fileId) {
  const bucket = getStorage().bucket();
  const objectPath = `tts/${fileId}.mp3`;
  const file = bucket.file(objectPath);
  await file.save(buf, { contentType: "audio/mpeg", resumable: false });
  await file.makePublic();
  return `https://storage.googleapis.com/${bucket.name}/${objectPath}`;
}

async function synthesizeAudio(text, fileId) {
  const buf = await ttsToMp3Buffer(text);
  if (!buf) return "";
  const url = await uploadMp3(buf, fileId);
  console.log(`[TTS] Synthesised "${String(text).slice(0, 40)}..." → ${url} (${buf.length} bytes)`);
  return url;
}

// MP3 bytes for processTurn to send inline in the HTTP response. We deliberately
// SKIP the Storage upload + makePublic that the old URL path needed: the device
// gets these bytes in the response body, and the Flutter app reads no audio field
// (verified — no audioData/audioUrl reads, no audio player). Dropping the upload
// shaves ~100–300 ms off every turn's synchronous critical path. The exchange
// doc's audioData is left "" (nothing consumes it on this path).
async function synthesizeAudioCapture(text) {
  const buffer = await ttsToMp3Buffer(text);
  return { url: "", buffer };
}

// Wrapper passed into the tutor engine. Never throws — audio is best-effort so a
// TTS hiccup can't block the text answer from being written.
async function safeSynthesize(text, fileId) {
  try {
    return await synthesizeAudio(text, fileId);
  } catch (err) {
    const snippet = String(text || "").slice(0, 40);
    console.warn(`[TTS] synthesis failed for ${fileId} "${snippet}":`, err.message);
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

  // Derive parentId from the paired device when the session doesn't
  // carry it (the ESP32 signs in anonymously and doesn't know the parent
  // until the parent has paired it to at least one child in the app).
  let parentId = session.parentId;
  if (!parentId && session.deviceId) {
    const pairedSnap = await db.collection("children")
      .where("deviceId", "==", session.deviceId)
      .limit(1)
      .get();
    if (!pairedSnap.empty) {
      parentId = pairedSnap.docs[0].data().parentId;
      if (parentId) await sessionRef.set({ parentId }, { merge: true });
    }
  }
  if (!parentId) {
    // Device's anonymous UID isn't in any children.deviceId — the parent hasn't
    // paired the device yet. Don't throw (that bubbles up as exchange.error and
    // the kid hears silence); instead finish the exchange cleanly with a spoken
    // prompt explaining what's wrong. The device-side flow will see matchedChildId=""
    // and fall back to the legacy "awaitingFirstQuestion" path.
    console.warn(`[identify] device ${session.deviceId} not paired to any child`);
    const promptText = "ההתקן הזה לא מקושר עדיין. בקשי מההורה לפתוח את האפליקציה כדי לחבר אותי.";
    const audioData = await safeSynthesize(promptText, `${exchangeId}_unpaired`);
    await exchangeRef.set({
      status: "done",
      matchedChildId: "",
      matchedChildName: "",
      needsPairing: true,
      promptText,
      audioData,
    }, { merge: true });
    return;
  }

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
    audioData: audioUrl,
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

  // Didn't clearly hear "חשבון" or "אנגלית" — re-ask instead of defaulting to
  // math. The device's identify loop sees subject:"" and prompts the child
  // again; no question is created yet.
  if (!subject) {
    console.log(`[${sessionId}/${exchangeId}] identify_subject unrecognised: "${transcript}" — re-asking`);
    await exchangeRef.update({
      status: "done",
      subject: "",
      answeredAt: FieldValue.serverTimestamp(),
    });
    return;
  }

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
    { currentQuestionAudioData: audioUrl, status: "active", lastActivity: FieldValue.serverTimestamp() },
    { merge: true }
  );
  await exchangeRef.update({
    status: "done",
    subject,
    promptText: spokenText,
    audioData: audioUrl,
    nextQuestion: question.prompt,
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
      // Disable 2.5-flash "thinking" — short spoken reply, no benefit from it.
      thinkingConfig: { thinkingBudget: 0 },
      safetySettings: CHILD_SAFETY_SETTINGS,
    },
  });

  const answer = String(result.text || "").trim();

  // Synthesize BEFORE marking done so the device never polls a done doc with
  // empty audio.
  const audioUrl = await safeSynthesize(answer, exchangeId);

  await exchangeRef.update({
    answer,
    spokenFeedback: answer,
    audioData: audioUrl,
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
        { greetingAudioData: audioUrl, lastActivity: FieldValue.serverTimestamp() },
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
          currentQuestionAudioData: audioUrl,
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
  { document: "sessions/{sessionId}/exchanges/{exchangeId}", minInstances: 1 },
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
        // Immediately notify parent when the child explicitly ends the session
        // (exit intent, continue declined, or 50-min limit). Don't wait for
        // monitorTutor which runs once per minute.
        if (result?.sessionEnded) {
          const sessionSnap = await db.collection("sessions").doc(sessionId).get();
          if (sessionSnap.exists) {
            await sendSessionEndedNow(db, sessionId, sessionSnap.data());
          }
        }
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
  if (child.parentId && child.parentId !== request.auth.uid) {
    throw new HttpsError("permission-denied", "Not your child.");
  }
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
  await sessionRef.set({ currentQuestionAudioData: audioUrl }, { merge: true });

  return {
    sessionId: sessionRef.id,
    currentQuestion: question.prompt,
    expectedAnswer: question.expectedAnswer,
    currentQuestionAudioData: audioUrl,
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
// ─────────────────────────────────────────────────────────────────────────────
// Cloud Function: synthesizeSpeech
//
// HTTP endpoint used by the ESP32 to get a TTS audio URL on demand —
// needed by the identification flow ("מי כאן?", "מה תרצה ללמוד היום?")
// where the device has no pre-recorded prompts and no on-device TTS.
//
// Request:  POST  { text: "...", id?: "optional-storage-id" }
// Headers:  Authorization: Bearer <Firebase idToken>
// Response: { audioBase64: "<https url>" }
//   (Field name kept for ESP32 firmware compatibility — cloudSynthesizeSpeech
//    reads `audioBase64` and passes it to speakAudio() which dispatches on
//    `https://` prefix. The value is now a Storage URL, not base64.)
// ─────────────────────────────────────────────────────────────────────────────
// ─────────────────────────────────────────────────────────────────────────────
// processTurn — the synchronous device turn (Phase 1).
//
// The device POSTs {sessionId, childAnswer} and BLOCKS on one HTTPS call that:
//   grade (deterministic) → gated Gemini (thinking off) → Google TTS → writes the
//   same session/questions/exchanges docs → returns the result INLINE.
//
// Response: feedback/emotion/next-question/flags in headers (Hebrew base64-encoded
// since header values must be ASCII); the MP3 bytes as the response BODY. This
// removes, in one shot, the device's Firestore write, the Eventarc trigger, the
// poll loop, and the separate Storage download that the old post+poll path paid.
//
// The exchange doc is created with status "processing" (NOT "pending"), so the
// answerQuestion trigger — which only acts on "pending" — skips it. answerQuestion
// stays deployed as the app-initiated / fallback path.
// ─────────────────────────────────────────────────────────────────────────────
exports.processTurn = onRequest(
  { cors: false, timeoutSeconds: 60, minInstances: 1 },
  async (req, res) => {
    if (req.method !== "POST") return res.status(405).send("Method Not Allowed");

    const authHeader = req.headers.authorization || "";
    if (!authHeader.startsWith("Bearer ")) {
      return res.status(401).json({ error: "Missing auth token" });
    }
    try {
      await getAuth().verifyIdToken(authHeader.split("Bearer ")[1]);
    } catch {
      return res.status(401).json({ error: "Invalid auth token" });
    }

    const { sessionId, childAnswer } = req.body || {};
    if (!sessionId || typeof childAnswer !== "string") {
      return res.status(400).json({ error: "sessionId and childAnswer (string) required" });
    }

    try {
      const sessionRef = db.collection("sessions").doc(sessionId);
      const exchangeRef = sessionRef.collection("exchanges").doc();   // auto-id
      const exchangeId = exchangeRef.id;
      const exchangeData = {
        type: "learning_turn",
        childAnswer,
        status: "processing",   // NOT "pending" → answerQuestion trigger skips it
        handledBy: "processTurn",
        askedAt: FieldValue.serverTimestamp(),
      };
      await exchangeRef.set(exchangeData);

      // Capture the spoken-feedback MP3 bytes while still writing the Storage URL
      // into the docs (so the Flutter app's audioData keeps working).
      let audioBuffer = null;
      const synthesizeCapturing = async (text) => {
        const r = await synthesizeAudioCapture(text);
        if (r.buffer) audioBuffer = r.buffer;
        return r.url;   // "" — audioData isn't used on the processTurn path
      };

      const result = await processLearningTurn({
        db,
        ai,
        model: GEMINI_MODEL,
        safetySettings: CHILD_SAFETY_SETTINGS,
        synthesize: synthesizeCapturing,
        sessionId,
        exchangeId,
        exchangeData,
      });

      // Notify the parent immediately on explicit session end (same as the trigger).
      if (result?.sessionEnded) {
        const sessionSnap = await sessionRef.get();
        if (sessionSnap.exists) await sendSessionEndedNow(db, sessionId, sessionSnap.data());
      }

      // Metadata → headers. Hebrew is base64(utf8) since header values are ASCII.
      const b64 = (s) => Buffer.from(String(s || ""), "utf8").toString("base64");
      res.set("X-Exchange-Id",       exchangeId);
      res.set("X-Is-Correct",        result.isCorrect ? "1" : "0");
      res.set("X-Emotion",           result.emotion || "neutral");
      res.set("X-Should-Break",      result.shouldTakeBreak ? "1" : "0");
      res.set("X-Session-Ended",     result.sessionEnded ? "1" : "0");
      res.set("X-Feedback-B64",      b64(result.spokenFeedback));
      res.set("X-Next-Question-B64", b64(result.nextQuestion));

      if (audioBuffer && audioBuffer.length > 0) {
        res.set("Content-Type", "audio/mpeg");
        return res.status(200).send(audioBuffer);
      }
      // TTS produced nothing — return metadata only with an empty body.
      res.set("Content-Type", "application/octet-stream");
      return res.status(200).send(Buffer.alloc(0));
    } catch (err) {
      console.error("[processTurn] failed:", err);
      return res.status(500).json({ error: err.message || "processTurn failed" });
    }
  }
);

// ─────────────────────────────────────────────────────────────────────────────
// ttsBytes — {text} → MP3 bytes in the response BODY (no Storage upload).
// The device's SD cache uses this for misses AND for prefetch (sdCacheWarm), so a
// phrase costs ONE round trip instead of the two the URL path needs
// (synthesizeSpeech → Storage → makePublic → device download). No minInstances:
// prefetch is one-time and warm misses are rare once the cache is primed, so a
// cold start here is acceptable and saves the always-warm cost.
// ─────────────────────────────────────────────────────────────────────────────
exports.ttsBytes = onRequest(
  { cors: false, timeoutSeconds: 30 },
  async (req, res) => {
    if (req.method !== "POST") return res.status(405).send("Method Not Allowed");
    const authHeader = req.headers.authorization || "";
    if (!authHeader.startsWith("Bearer ")) {
      return res.status(401).json({ error: "Missing auth token" });
    }
    try {
      await getAuth().verifyIdToken(authHeader.split("Bearer ")[1]);
    } catch {
      return res.status(401).json({ error: "Invalid auth token" });
    }
    const { text } = req.body || {};
    if (!text || typeof text !== "string") {
      return res.status(400).json({ error: "text (string) required" });
    }
    try {
      const buf = await ttsToMp3Buffer(text);
      if (!buf || buf.length === 0) {
        res.set("Content-Type", "application/octet-stream");
        return res.status(200).send(Buffer.alloc(0));
      }
      res.set("Content-Type", "audio/mpeg");
      return res.status(200).send(buf);
    } catch (err) {
      console.error("[ttsBytes] failed:", err);
      return res.status(500).json({ error: err.message || "ttsBytes failed" });
    }
  }
);

exports.synthesizeSpeech = onRequest(
  // minInstances: 1 keeps one warm instance so the first call after idle skips the
  // Cloud Functions v2 cold start (the 3–12 s spike a child would otherwise feel).
  { cors: false, timeoutSeconds: 30, minInstances: 1 },
  async (req, res) => {
    if (req.method !== "POST") return res.status(405).send("Method Not Allowed");

    const authHeader = req.headers.authorization || "";
    if (!authHeader.startsWith("Bearer ")) {
      return res.status(401).json({ error: "Missing auth token" });
    }
    try {
      await getAuth().verifyIdToken(authHeader.split("Bearer ")[1]);
    } catch {
      return res.status(401).json({ error: "Invalid auth token" });
    }

    const { text, id } = req.body || {};
    if (!text || typeof text !== "string") {
      return res.status(400).json({ error: "No text provided" });
    }

    try {
      const audioBase64 = await synthesizeAudio(text, id || `tts_${Date.now()}`);
      return res.json({ audioBase64 });
    } catch (err) {
      console.error("[synthesizeSpeech] failed:", err);
      return res.status(500).json({ error: err.message });
    }
  }
);

exports.transcribeAudio = onRequest(
  // minInstances: 1 — STT is the first network hop of a turn, so its cold start is
  // fully additive. Keep one instance warm to kill the first-answer cold-start spike.
  { cors: false, timeoutSeconds: 30, minInstances: 1 },
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
