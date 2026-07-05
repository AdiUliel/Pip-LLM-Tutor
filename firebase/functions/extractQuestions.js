/**
 * extractQuestionsFromMaterial — turns an uploaded homework file
 * (PDF / image / text) into Q&A pairs stored on the same MaterialDoc.
 *
 * Trigger: onWrite on materials/{materialId}
 *   - Skip if no fileUrl
 *   - Skip if already processed (itemsGeneratedAt is set)
 *   - Skip if items array is already populated (parent typed Q&A manually)
 *
 * Pipeline:
 *   1. Read the child for age / subject context (Hebrew or English prompt)
 *   2. Download the file via its public download URL
 *   3. Send the bytes inline to Gemini multimodal with a strict JSON schema
 *      so the response is always parseable
 *   4. Write items[] + itemsGeneratedAt back to the same MaterialDoc
 *
 * The ESP32 tutorEngine can then pull questions from
 * materials/{id}.items during a session (that wiring lives in
 * tutorEngine.js — partner's domain — and is not changed here).
 */

const { onDocumentWritten } = require("firebase-functions/v2/firestore");
const { getFirestore, FieldValue } = require("firebase-admin/firestore");
const { GoogleGenAI } = require("@google/genai");

const GEMINI_MODEL = process.env.GEMINI_MODEL || "gemini-2.5-flash";

const CHILD_SAFETY_SETTINGS = [
  { category: "HARM_CATEGORY_HARASSMENT",       threshold: "BLOCK_LOW_AND_ABOVE" },
  { category: "HARM_CATEGORY_HATE_SPEECH",      threshold: "BLOCK_LOW_AND_ABOVE" },
  { category: "HARM_CATEGORY_SEXUALLY_EXPLICIT", threshold: "BLOCK_LOW_AND_ABOVE" },
  { category: "HARM_CATEGORY_DANGEROUS_CONTENT", threshold: "BLOCK_LOW_AND_ABOVE" },
];

// JSON schema enforced on Gemini's response — eliminates parse failures.
// `appropriate` gates whether the material is suitable for this child's age and
// subject; `reason` is a short Hebrew explanation when it is not.
const RESPONSE_SCHEMA = {
  type: "object",
  properties: {
    appropriate: { type: "boolean" },
    reason:      { type: "string" },
    items: {
      type: "array",
      items: {
        type: "object",
        properties: {
          question: { type: "string" },
          answer:   { type: "string" },
        },
        required: ["question", "answer"],
      },
    },
  },
  required: ["appropriate", "items"],
};

function mimeFromUrl(url) {
  const lower = url.toLowerCase().split("?")[0];
  if (lower.endsWith(".pdf"))  return "application/pdf";
  if (lower.endsWith(".png"))  return "image/png";
  if (lower.endsWith(".jpg") || lower.endsWith(".jpeg")) return "image/jpeg";
  if (lower.endsWith(".csv"))  return "text/csv";
  if (lower.endsWith(".txt"))  return "text/plain";
  return "application/octet-stream";
}

function buildPrompt({ subject, age }) {
  const subjectLabel = subject === "english" ? "אנגלית" : "חשבון";
  const langInstruction = subject === "english"
    ? "המקצוע אנגלית, ולכן השאלה והתשובה יהיו באנגלית."
    : "השאלה והתשובה יהיו בעברית.";

  return `אתה עוזר ליצור שאלות תרגול מחומר לימוד שהורה העלה.

מצורף קובץ (PDF / תמונה / טקסט) שמכיל חומר לימוד.
קודם בדוק האם החומר מתאים לילד בגיל ${age} ולמקצוע ${subjectLabel}, ואז חלץ ממנו 5-12 שאלות תרגול.

דרישות:
- שאלות קצרות וברורות, ברמה מתאימה לגיל.
- התשובה מדויקת ותמציתית (מילה, מספר, או משפט קצר).
- אל תיצור שאלות עם תוכן לא מתאים לילדים.
- אם החומר ריק או לא ברור, החזר appropriate: true עם items: [] — אל תמציא תוכן.
- ${langInstruction}

התאמה (appropriate):
- אם החומר מתאים לגיל ${age} ולמקצוע ${subjectLabel} — החזר appropriate: true.
- אם החומר לא מתאים (תוכן מבוגר/מפחיד/אלים/לא בטיחותי, או שאינו קשור למקצוע ${subjectLabel}) — החזר appropriate: false, שדה reason עם משפט קצר בעברית שמסביר למה, ו-items ריק.

החזר JSON בלבד.`;
}

// Lazy-init the GenAI client + Firestore so cold-start cost only hits this
// function when it actually fires.
let _ai;
function ai() {
  if (!_ai) {
    _ai = new GoogleGenAI({
      vertexai: true,
      project: process.env.GCLOUD_PROJECT,
      location: process.env.GEMINI_LOCATION || "global",
    });
  }
  return _ai;
}

exports.extractQuestionsFromMaterial = onDocumentWritten(
  { document: "materials/{materialId}", timeoutSeconds: 120, memory: "1GiB" },
  async (event) => {
    const after = event.data?.after?.data();
    if (!after) return;                          // deletion
    if (!after.fileUrl) return;                  // text-only material; nothing to extract
    if (after.itemsGeneratedAt) return;          // already processed once
    if (Array.isArray(after.items) && after.items.length > 0) return; // parent typed Q&A

    const { materialId } = event.params;
    const ref = event.data.after.ref;

    // Mark this doc as in-flight so concurrent re-triggers don't double-bill
    // Gemini. We write the timestamp at the end too — that "completed" marker
    // is what gates future skips.
    try {
      await ref.update({ extractionStartedAt: FieldValue.serverTimestamp() });
    } catch (e) {
      console.error(`[${materialId}] failed to mark in-flight:`, e.message);
      return;
    }

    const db = getFirestore();
    let childAge = 8;
    if (after.childId) {
      const childSnap = await db.collection("children").doc(after.childId).get();
      if (childSnap.exists) childAge = childSnap.data().age || childAge;
    }
    const subject = after.subject || "math";
    const prompt  = buildPrompt({ subject, age: childAge });
    const mime    = mimeFromUrl(after.fileUrl);

    let base64;
    try {
      const res = await fetch(after.fileUrl);
      if (!res.ok) throw new Error(`fetch ${res.status}`);
      const buf = Buffer.from(await res.arrayBuffer());
      base64 = buf.toString("base64");
    } catch (e) {
      console.error(`[${materialId}] download failed:`, e.message);
      await ref.update({
        itemsGeneratedAt: FieldValue.serverTimestamp(),
        extractionError:   `download failed: ${e.message}`,
      });
      return;
    }

    let items = [];
    let appropriate = true;
    let reason = "";
    try {
      const result = await ai().models.generateContent({
        model: GEMINI_MODEL,
        contents: [{
          role: "user",
          parts: [
            { text: prompt },
            { inlineData: { mimeType: mime, data: base64 } },
          ],
        }],
        config: {
          maxOutputTokens:   2000,
          temperature:       0.3,
          responseMimeType:  "application/json",
          responseSchema:    RESPONSE_SCHEMA,
          safetySettings:    CHILD_SAFETY_SETTINGS,
        },
      });

      const parsed = JSON.parse(result.text);
      // Default to appropriate:true when the field is missing so a malformed
      // response never blocks a legitimate upload.
      appropriate = parsed?.appropriate !== false;
      reason = typeof parsed?.reason === "string" ? parsed.reason.trim() : "";
      if (Array.isArray(parsed?.items)) {
        items = parsed.items
          .filter((i) => i && i.question && i.answer)
          .map((i) => ({
            question: String(i.question).trim(),
            answer:   String(i.answer).trim(),
          }))
          .filter((i) => i.question.length > 0 && i.answer.length > 0);
      }
    } catch (e) {
      console.error(`[${materialId}] Gemini extraction failed:`, e.message);
      await ref.update({
        itemsGeneratedAt: FieldValue.serverTimestamp(),
        extractionError:   `gemini failed: ${e.message}`,
      });
      return;
    }

    // ── Content deemed unsuitable for this child → don't store questions, and
    // disable the material so the device never draws from it. The parent sees a
    // clear reason in the app (extractionError starts with "inappropriate:").
    if (!appropriate) {
      console.log(`[${materialId}] marked inappropriate: ${reason || "(no reason)"}`);
      await ref.update({
        items: [],
        enabled: false,
        itemsGeneratedAt: FieldValue.serverTimestamp(),
        extractionError:  `inappropriate: ${reason || "תוכן לא מתאים לילד"}`,
      });
      return;
    }

    // ── No usable question came out of the file → flag it so the parent gets a
    // "no questions found" warning instead of a silent "0 שאלות".
    if (items.length === 0) {
      console.log(`[${materialId}] extraction produced 0 questions`);
      await ref.update({
        items: [],
        itemsGeneratedAt: FieldValue.serverTimestamp(),
        extractionError:  "no_questions",
      });
      return;
    }

    console.log(`[${materialId}] extracted ${items.length} Q&A pairs`);
    await ref.update({
      items,
      itemsGeneratedAt: FieldValue.serverTimestamp(),
      extractionError:  FieldValue.delete(),
    });
  }
);
