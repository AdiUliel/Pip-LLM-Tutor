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
const { initializeApp }      = require("firebase-admin/app");
const { getFirestore }       = require("firebase-admin/firestore");
const { GoogleGenAI }        = require("@google/genai");

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

      // ── Write the answer back ───────────────────────────────────────────
      await exchangeRef.update({
        answer,
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
