/**
 * Cloud Function: answerQuestion
 *
 * Triggered when a new exchange document is created inside a session.
 * Path: /sessions/{sessionId}/exchanges/{exchangeId}
 *
 * Flow:
 *   1. ESP32 writes { question, status:"pending", answer:null, askedAt }
 *   2. This function fires, fetches conversation history for context
 *   3. Calls Gemini via Vertex AI (no API key — uses the function's service account / ADC)
 *   4. Writes the answer back → status:"done"
 *   5. ESP32 polls the same document and reads the answer
 */

const { onDocumentCreated } = require("firebase-functions/v2/firestore");
const { onRequest }         = require("firebase-functions/v2/https");
const { initializeApp }     = require("firebase-admin/app");
const { getFirestore }      = require("firebase-admin/firestore");
const { getStorage }        = require("firebase-admin/storage");
const { getAuth }           = require("firebase-admin/auth");
const { GoogleGenAI }       = require("@google/genai");

initializeApp();
const db = getFirestore();

// ── Gemini client via Vertex AI (ADC — no API key needed) ─────────────────────
// The Cloud Function's service account is used automatically.
const ai = new GoogleGenAI({
  vertexai: true,
  project:  process.env.GCLOUD_PROJECT, // auto-set in Cloud Functions
  location: "us-central1",              // change if your project is in another region
});

// ── System prompt ─────────────────────────────────────────────────────────────
const SYSTEM_PROMPT = `You are a friendly homework helper for young children (ages 6-12).
Your job is to help them understand their schoolwork — not just give answers.
- Use simple, encouraging language.
- Break problems into small steps.
- If the child seems stuck, give a hint rather than the full answer.
- Keep responses short (2-4 sentences) since they will be read aloud.
- Respond in the same language the child uses.`;

// ─────────────────────────────────────────────────────────────────────────────

exports.answerQuestion = onDocumentCreated(
  "sessions/{sessionId}/exchanges/{exchangeId}",
  async (event) => {
    const { sessionId, exchangeId } = event.params;
    const exchangeRef = db
      .collection("sessions")
      .doc(sessionId)
      .collection("exchanges")
      .doc(exchangeId);

    const snap = event.data;
    if (!snap) return;

    const data = snap.data();

    // Skip if not a pending question (safety guard)
    if (data.status !== "pending" || !data.question) {
      console.log("Skipping — not a pending question.", { status: data.status });
      return;
    }

    // Mark as processing so ESP32 knows we're working on it
    await exchangeRef.update({ status: "processing" });

    try {
      // ── Fetch conversation history for LLM context ──────────────────────
      const historySnap = await db
        .collection("sessions")
        .doc(sessionId)
        .collection("exchanges")
        .where("status", "==", "done")
        .orderBy("askedAt", "asc")
        .limit(10)
        .get();

      // Build content array: previous Q&A pairs + current question
      const contents = [];
      historySnap.forEach((doc) => {
        const d = doc.data();
        if (d.question) contents.push({ role: "user",  parts: [{ text: d.question }] });
        if (d.answer)   contents.push({ role: "model", parts: [{ text: d.answer   }] });
      });
      contents.push({ role: "user", parts: [{ text: data.question }] });

      // ── Call Gemini ─────────────────────────────────────────────────────
      const result = await ai.models.generateContent({
        model: "gemini-2.0-flash-001",
        contents,
        config: {
          systemInstruction: SYSTEM_PROMPT,
          maxOutputTokens: 200,
          temperature: 0.7,
        },
      });

      const answer = result.text.trim();

      // ── Call Google TTS → upload WAV to Storage → get public URL ────────
      let audioUrl = "";
      try {
        audioUrl = await synthesizeAndStore(answer, exchangeId);
      } catch (ttsErr) {
        // TTS failure is non-fatal — the text answer is still useful
        console.warn("TTS failed (non-fatal):", ttsErr.message);
      }

      // ── Write the answer back ───────────────────────────────────────────
      await exchangeRef.update({
        answer,
        audioUrl,           // public URL to raw WAV file (empty string if TTS failed)
        status:     "done",
        answeredAt: new Date(),
      });

      // Bump session's lastActivity
      await db.collection("sessions").doc(sessionId).update({
        lastActivity: new Date(),
      });

      console.log(`[${sessionId}/${exchangeId}] Done: "${answer.slice(0, 80)}..."`);
    } catch (err) {
      console.error("Gemini call failed:", err);
      await exchangeRef.update({
        status:     "error",
        error:      err.message || "Unknown error",
        answeredAt: new Date(),
      });
    }
  }
);

// ─────────────────────────────────────────────────────────────────────────────
// Helper: synthesizeAndStore
//
// Calls Google Text-to-Speech (via ADC — no API key needed) and uploads the
// resulting WAV file to Firebase Storage. Returns the public download URL.
//
// ⚠️  Before this works you must:
//   1. Enable Firebase Storage in Firebase Console → Build → Storage
//   2. Enable the TTS API: console.cloud.google.com/apis/library/texttospeech.googleapis.com
// ─────────────────────────────────────────────────────────────────────────────
async function synthesizeAndStore(text, exchangeId) {
  // Get ADC token from the GCP metadata server (same approach as STT proxy)
  const tokenRes = await fetch(
    "http://metadata.google.internal/computeMetadata/v1/instance/service-accounts/default/token",
    { headers: { "Metadata-Flavor": "Google" } }
  );
  const { access_token } = await tokenRes.json();

  // Call Google Cloud Text-to-Speech
  // Language/voice: change languageCode + name to match your children's language.
  // Hebrew: "he-IL" / "he-IL-Standard-A"
  // English: "en-US" / "en-US-Standard-C"
  const ttsRes = await fetch(
    "https://texttospeech.googleapis.com/v1/text:synthesize",
    {
      method: "POST",
      headers: {
        "Content-Type":  "application/json",
        "Authorization": `Bearer ${access_token}`,
      },
      body: JSON.stringify({
        input: { text },
        voice: { languageCode: "he-IL", name: "he-IL-Standard-A" },
        audioConfig: {
          audioEncoding:   "LINEAR16",  // raw PCM in WAV container
          sampleRateHertz: 16000,       // must match ESP32 I2S sample rate
        },
      }),
    }
  );

  if (!ttsRes.ok) {
    const err = await ttsRes.text();
    throw new Error(`TTS HTTP ${ttsRes.status}: ${err}`);
  }

  const { audioContent } = await ttsRes.json();
  // audioContent is base64-encoded WAV (with 44-byte RIFF header)
  const wavBuffer = Buffer.from(audioContent, "base64");

  // Upload to Firebase Storage — path: tts/{exchangeId}.wav
  const bucket = getStorage().bucket();
  const file   = bucket.file(`tts/${exchangeId}.wav`);
  await file.save(wavBuffer, { contentType: "audio/wav", resumable: false });
  await file.makePublic();

  const publicUrl = `https://storage.googleapis.com/${bucket.name}/tts/${exchangeId}.wav`;
  console.log(`[TTS] Uploaded ${wavBuffer.length} bytes → ${publicUrl}`);
  return publicUrl;
}

// ─────────────────────────────────────────────────────────────────────────────
// Cloud Function: transcribeAudio
//
// HTTP endpoint used by the ESP32 to transcribe audio without needing a
// Google STT API key on the device. The ESP32 sends raw PCM audio (base64),
// this function forwards it to Google Speech-to-Text using ADC (service account).
//
// Request:  POST  { audio: "<base64 LINEAR16 PCM>", languageCode: "he-IL" }
// Headers:  Authorization: Bearer <Firebase idToken>
// Response: { transcript: "the transcribed text" }
// ─────────────────────────────────────────────────────────────────────────────
exports.transcribeAudio = onRequest(
  { cors: false, timeoutSeconds: 30, region: "europe-west10" },
  async (req, res) => {
    if (req.method !== "POST") {
      return res.status(405).send("Method Not Allowed");
    }

    // ── Verify Firebase auth token ──────────────────────────────────────────
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

    // ── Get ADC access token from metadata server (no API key needed) ───────
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

    // ── Call Google Speech-to-Text ──────────────────────────────────────────
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
