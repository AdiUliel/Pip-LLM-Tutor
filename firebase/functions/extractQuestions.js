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
    ? `המקצוע אנגלית. הילד דובר עברית ולומד אנגלית, לכן נסח כל שאלה בעברית והתשובה תהיה מילה או ביטוי באנגלית.
- לחומר של אוצר מילים (מילה מול תמונה, או תרגום): שאל "איך אומרים <המילה בעברית> באנגלית?" והתשובה היא המילה באנגלית. לדוגמה: תמונה של כלב עם המילה dog → שאלה: "איך אומרים כלב באנגלית?", תשובה: "dog".
- אל תבקש מהילד לאיית אות-אות, ואל תיצור תשובות של אותיות מופרדות.`
    : "השאלה והתשובה יהיו בעברית.";

  return `אתה עוזר ליצור שאלות תרגול מחומר לימוד שהורה העלה.

מצורף קובץ (PDF / תמונה / טקסט) שמכיל חומר לימוד.
קודם בדוק האם החומר מתאים לילד בגיל ${age} ולמקצוע ${subjectLabel}, ואז חלץ ממנו 5-12 שאלות תרגול.

דרישות:
- הילד לא רואה את הדף — השאלה נשמעת בקול בלבד. לכן אסור להתייחס לתמונות, למיקום בדף, לצבעים, לקווים או ל"התמונה של...". אם החומר מבוסס תמונות (כמו התאמת מילה לתמונה), המר כל פריט לשאלה מילולית עצמאית שמובנת רק מהשמע.
- נסח כל שאלה כמשפט שאלה טבעי, ברור וידידותי שאפשר להקריא בקול לילד — לא ביטוי גולמי. השאלה תוקרא בקול על ידי רובוט, אז היא חייבת להישמע כמו שאלה מדוברת. לדוגמה: "7+2" → "כמה זה 7 ועוד 2?"; "8×4" → "כמה זה 8 כפול 4?"; "12-5" → "כמה זה 12 פחות 5?".
- שאלה קצרה וברורה, ברמה מתאימה לגיל.
- התשובה מדויקת ותמציתית (מילה, מספר, או משפט קצר). למספרים — כתוב את התשובה כספרה (למשל 9), לא כמילה.
- אל תיצור שאלות עם תוכן לא מתאים לילדים.
- אם החומר ריק או לא ברור, החזר appropriate: true עם items: [] — אל תמציא תוכן.
- ${langInstruction}

התאמה (appropriate):
- אם החומר מתאים לגיל ${age} ולמקצוע ${subjectLabel} — החזר appropriate: true.
- אם החומר לא מתאים (תוכן מבוגר/מפחיד/אלים/לא בטיחותי, או שאינו קשור למקצוע ${subjectLabel}) — החזר appropriate: false, שדה reason עם משפט קצר בעברית שמסביר למה, ו-items ריק.

החזר JSON בלבד.`;
}

// Filter raw {question, answer} objects down to clean, non-empty, trimmed pairs.
function normalizeItems(arr) {
  if (!Array.isArray(arr)) return [];
  return arr
    .filter((i) => i && i.question && i.answer)
    .map((i) => ({
      question: String(i.question).trim(),
      answer:   String(i.answer).trim(),
    }))
    .filter((i) => i.question.length > 0 && i.answer.length > 0);
}

// Pull every COMPLETE object out of the "items" array by scanning brace depth
// with full string/escape awareness. A trailing object that was cut off mid-way
// (token limit, dropped stream) never closes its brace, so it is skipped — every
// complete pair that arrived before the cut is preserved. Each candidate is run
// through the real JSON parser so per-object escaping stays correct.
function salvageItems(text) {
  const open = text.match(/"items"\s*:\s*\[/);
  if (!open) return [];
  const objects = [];
  let depth = 0, inString = false, escaped = false, objStart = -1;
  for (let i = open.index + open[0].length; i < text.length; i++) {
    const ch = text[i];
    if (inString) {
      if (escaped) escaped = false;
      else if (ch === "\\") escaped = true;
      else if (ch === '"') inString = false;
      continue;
    }
    if (ch === '"') { inString = true; continue; }
    if (ch === "{") { if (depth === 0) objStart = i; depth++; }
    else if (ch === "}") {
      depth--;
      if (depth === 0 && objStart !== -1) { objects.push(text.slice(objStart, i + 1)); objStart = -1; }
    } else if (ch === "]" && depth === 0) {
      break;   // clean end of the items array
    }
  }
  const out = [];
  for (const obj of objects) {
    try { out.push(JSON.parse(obj)); } catch (_) { /* skip a malformed object */ }
  }
  return out;
}

// Parse Gemini's JSON, tolerating truncation. On a clean parse we read fields
// directly; when the whole-response parse fails (e.g. the response was cut off
// mid-array) we salvage the complete items and recover appropriate/reason from
// the partial text — those two fields precede `items` in the schema, so they are
// intact on a mid-array cut. `truncated` marks that fallback path for logging.
function parseExtraction(text) {
  const raw = String(text || "");
  try {
    const parsed = JSON.parse(raw);
    return {
      appropriate: parsed?.appropriate !== false,   // missing → treat as appropriate
      reason: typeof parsed?.reason === "string" ? parsed.reason.trim() : "",
      items: normalizeItems(parsed?.items),
      truncated: false,
    };
  } catch (_) {
    const reasonMatch = raw.match(/"reason"\s*:\s*"((?:[^"\\]|\\.)*)"/);
    return {
      appropriate: !/"appropriate"\s*:\s*false/.test(raw),
      reason: reasonMatch ? reasonMatch[1].trim() : "",
      items: normalizeItems(salvageItems(raw)),
      truncated: true,
    };
  }
}

// Turn a bare arithmetic expression a parent typed ("6+8", "12 - 5", "8×4",
// "9/3") into a natural spoken question — the same phrasing the LLM uses for
// extracted questions, so typed math reads aloud naturally instead of the raw
// "6+8". Anything that isn't a pure "number op number" (word problems, an
// already-natural question) is returned unchanged, which also makes this a safe
// idempotent no-op on its own output.
function naturalizeMathExpression(question) {
  const s = String(question == null ? "" : question).trim();
  const m = s.match(/^(\d+)\s*([+\-*x×÷/:])\s*(\d+)\s*=?\s*\??$/);
  if (!m) return question;
  const a = m[1], op = m[2], b = m[3];
  switch (op) {
    case "+":                     return `כמה זה ${a} ועוד ${b}?`;
    case "-":                     return `כמה זה ${a} פחות ${b}?`;
    case "*": case "x": case "×": return `כמה זה ${a} כפול ${b}?`;
    case "/": case ":": case "÷": return `כמה זה ${a} חלקי ${b}?`;
    default:                      return question;
  }
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

    const { materialId } = event.params;
    const ref = event.data.after.ref;

    // Manual Q&A (parent typed the items; no file to extract from). The Gemini
    // path is skipped, but for math we still normalize a bare arithmetic
    // expression ("6+8" → "כמה זה 6 ועוד 8?") so a typed question reads aloud
    // like the generated/extracted ones. Idempotent: naturalizeMathExpression is
    // a no-op on its own Hebrew output, so the resulting write re-triggers into
    // this same branch and changes nothing.
    if (!after.fileUrl) {
      const isMath = (after.subject || "math") !== "english";
      if (isMath && Array.isArray(after.items) && after.items.length > 0) {
        const normalized = after.items.map((it) => ({
          ...it,
          question: naturalizeMathExpression(it && it.question),
        }));
        if (normalized.some((n, i) => n.question !== after.items[i].question)) {
          await ref.update({ items: normalized });
          console.log(`[${materialId}] normalized typed math expression(s)`);
        }
      }
      return;                                    // no file → nothing to extract
    }

    if (after.itemsGeneratedAt) return;          // already processed once
    if (Array.isArray(after.items) && after.items.length > 0) return; // parent typed Q&A alongside a file

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

    // API call — a throw here is a genuine, unrecoverable failure (network /
    // auth / hard safety block). Parsing is handled separately below so a merely
    // truncated response never lands in this bucket and loses everything.
    let responseText;
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
          maxOutputTokens:   4000,
          temperature:       0.3,
          responseMimeType:  "application/json",
          responseSchema:    RESPONSE_SCHEMA,
          safetySettings:    CHILD_SAFETY_SETTINGS,
        },
      });
      responseText = result.text || "";
    } catch (e) {
      console.error(`[${materialId}] Gemini call failed:`, e.message);
      await ref.update({
        itemsGeneratedAt: FieldValue.serverTimestamp(),
        extractionError:   `gemini failed: ${e.message}`,
      });
      return;
    }

    // Tolerant parse: a response cut off mid-array still yields every complete
    // Q&A pair that arrived before the cut, instead of discarding all of them.
    const { appropriate, reason, items, truncated } = parseExtraction(responseText);
    if (truncated) {
      console.warn(`[${materialId}] response partial/truncated — salvaged ${items.length} complete item(s)`);
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

    // Safety net: the prompt already tells Gemini to phrase math naturally, but
    // if a raw "6+8" slips through, normalize it here too (no-op for English and
    // for already-natural questions).
    const finalItems = subject === "english"
      ? items
      : items.map((it) => ({ ...it, question: naturalizeMathExpression(it.question) }));

    console.log(`[${materialId}] extracted ${finalItems.length} Q&A pairs`);
    await ref.update({
      items: finalItems,
      itemsGeneratedAt: FieldValue.serverTimestamp(),
      extractionError:  FieldValue.delete(),
    });
  }
);
