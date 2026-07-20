// ─────────────────────────────────────────────────────────────────────────────
// Question generator — math + English for Israeli elementary school (grades 3–6)
// English curriculum follows the Israeli Ministry of Education syllabus:
//   Grade 3 (age 8):  Basic vocab — animals, colors, family, food, numbers
//   Grade 4 (age 9):  Body parts, school items, classroom objects, greetings
//   Grade 5 (age 10): Actions/verbs, clothing, weather, nature, places
//   Grade 6 (age 11): Simple sentences, plurals, to-be/to-have/can forms
// Grammar IS taught from grade 5 (present simple, plurals, I am/is/are/have/can).
// ─────────────────────────────────────────────────────────────────────────────

const HEBREW_NUMBER_WORDS = {
  0: "אפס", 1: "אחת", 2: "שתיים", 3: "שלוש", 4: "ארבע", 5: "חמש",
  6: "שש", 7: "שבע", 8: "שמונה", 9: "תשע", 10: "עשר",
  11: "אחת עשרה", 12: "שתים עשרה", 13: "שלוש עשרה", 14: "ארבע עשרה",
  15: "חמש עשרה", 16: "שש עשרה", 17: "שבע עשרה", 18: "שמונה עשרה",
  19: "תשע עשרה", 20: "עשרים",
};

// ── English word bank ──────────────────────────────────────────────────────────
// Each entry: { he: "עברית", en: "english", topic, minDiff }
// minDiff: minimum difficulty level at which this word appears (1–10)
const ENGLISH_VOCAB = [
  // Animals (difficulty 1–2)
  { he: "כלב",     en: "dog",      topic: "animals",  minDiff: 1 },
  { he: "חתול",    en: "cat",      topic: "animals",  minDiff: 1 },
  { he: "פרה",     en: "cow",      topic: "animals",  minDiff: 1 },
  { he: "סוס",     en: "horse",    topic: "animals",  minDiff: 1 },
  { he: "עוף",     en: "chicken",  topic: "animals",  minDiff: 1 },
  { he: "דג",      en: "fish",     topic: "animals",  minDiff: 1 },
  { he: "ציפור",   en: "bird",     topic: "animals",  minDiff: 1 },
  { he: "ארנב",    en: "rabbit",   topic: "animals",  minDiff: 2 },
  { he: "כבשה",    en: "sheep",    topic: "animals",  minDiff: 2 },
  { he: "חזיר",    en: "pig",      topic: "animals",  minDiff: 2 },
  { he: "אריה",    en: "lion",     topic: "animals",  minDiff: 2 },
  { he: "פיל",     en: "elephant", topic: "animals",  minDiff: 2 },
  { he: "קוף",     en: "monkey",   topic: "animals",  minDiff: 2 },
  { he: "פרפר",    en: "butterfly",topic: "animals",  minDiff: 3 },
  { he: "צב",      en: "turtle",   topic: "animals",  minDiff: 3 },

  // Colors (difficulty 1)
  { he: "אדום",    en: "red",      topic: "colors",   minDiff: 1 },
  { he: "כחול",    en: "blue",     topic: "colors",   minDiff: 1 },
  { he: "ירוק",    en: "green",    topic: "colors",   minDiff: 1 },
  { he: "צהוב",    en: "yellow",   topic: "colors",   minDiff: 1 },
  { he: "שחור",    en: "black",    topic: "colors",   minDiff: 1 },
  { he: "לבן",     en: "white",    topic: "colors",   minDiff: 1 },
  { he: "כתום",    en: "orange",   topic: "colors",   minDiff: 2 },
  { he: "סגול",    en: "purple",   topic: "colors",   minDiff: 2 },
  { he: "ורוד",    en: "pink",     topic: "colors",   minDiff: 2 },
  { he: "חום",     en: "brown",    topic: "colors",   minDiff: 2 },

  // Family (difficulty 1–2)
  { he: "אמא",     en: "mother",   topic: "family",   minDiff: 1 },
  { he: "אבא",     en: "father",   topic: "family",   minDiff: 1 },
  { he: "אח",      en: "brother",  topic: "family",   minDiff: 1 },
  { he: "אחות",    en: "sister",   topic: "family",   minDiff: 1 },
  { he: "סבתא",    en: "grandmother", topic: "family", minDiff: 2 },
  { he: "סבא",     en: "grandfather", topic: "family", minDiff: 2 },
  { he: "דוד",     en: "uncle",    topic: "family",   minDiff: 2 },
  { he: "דודה",    en: "aunt",     topic: "family",   minDiff: 2 },

  // Food (difficulty 2–3)
  { he: "תפוח",    en: "apple",    topic: "food",     minDiff: 1 },
  { he: "בננה",    en: "banana",   topic: "food",     minDiff: 1 },
  { he: "לחם",     en: "bread",    topic: "food",     minDiff: 2 },
  { he: "חלב",     en: "milk",     topic: "food",     minDiff: 2 },
  { he: "ביצה",    en: "egg",      topic: "food",     minDiff: 2 },
  { he: "עוגה",    en: "cake",     topic: "food",     minDiff: 2 },
  { he: "גזר",     en: "carrot",   topic: "food",     minDiff: 2 },
  { he: "תפוז",    en: "orange",   topic: "food",     minDiff: 2 },
  { he: "תות",     en: "strawberry", topic: "food",   minDiff: 3 },
  { he: "אבטיח",   en: "watermelon", topic: "food",   minDiff: 3 },
  { he: "ענב",     en: "grape",    topic: "food",     minDiff: 3 },
  { he: "אורז",    en: "rice",     topic: "food",     minDiff: 3 },

  // School & classroom (difficulty 2)
  { he: "ספר",     en: "book",     topic: "school",   minDiff: 1 },
  { he: "עיפרון",  en: "pencil",   topic: "school",   minDiff: 2 },
  { he: "שולחן",   en: "table",    topic: "school",   minDiff: 2 },
  { he: "כיסא",    en: "chair",    topic: "school",   minDiff: 2 },
  { he: "מחברת",   en: "notebook", topic: "school",   minDiff: 2 },
  { he: "מחק",     en: "eraser",   topic: "school",   minDiff: 2 },
  { he: "סרגל",    en: "ruler",    topic: "school",   minDiff: 3 },
  { he: "תיק",     en: "bag",      topic: "school",   minDiff: 2 },
  { he: "לוח",     en: "board",    topic: "school",   minDiff: 3 },
  { he: "מחשב",    en: "computer", topic: "school",   minDiff: 3 },

  // Body parts (difficulty 2–3)
  { he: "ראש",     en: "head",     topic: "body",     minDiff: 2 },
  { he: "עיניים",  en: "eyes",     topic: "body",     minDiff: 2 },
  { he: "אוזניים", en: "ears",     topic: "body",     minDiff: 2 },
  { he: "אף",      en: "nose",     topic: "body",     minDiff: 2 },
  { he: "פה",      en: "mouth",    topic: "body",     minDiff: 2 },
  { he: "יד",      en: "hand",     topic: "body",     minDiff: 2 },
  { he: "רגל",     en: "leg",      topic: "body",     minDiff: 2 },
  { he: "כתף",     en: "shoulder", topic: "body",     minDiff: 3 },
  { he: "בטן",     en: "stomach",  topic: "body",     minDiff: 3 },

  // Home (difficulty 3)
  { he: "בית",     en: "house",    topic: "home",     minDiff: 1 },
  { he: "דלת",     en: "door",     topic: "home",     minDiff: 2 },
  { he: "חלון",    en: "window",   topic: "home",     minDiff: 3 },
  { he: "מטבח",    en: "kitchen",  topic: "home",     minDiff: 3 },
  { he: "חדר שינה", en: "bedroom", topic: "home",     minDiff: 3 },
  { he: "אמבטיה",  en: "bathroom", topic: "home",     minDiff: 3 },
  { he: "גינה",    en: "garden",   topic: "home",     minDiff: 3 },

  // Nature & weather (difficulty 3)
  { he: "שמש",     en: "sun",      topic: "nature",   minDiff: 1 },
  { he: "ירח",     en: "moon",     topic: "nature",   minDiff: 1 },
  { he: "כוכב",    en: "star",     topic: "nature",   minDiff: 2 },
  { he: "עץ",      en: "tree",     topic: "nature",   minDiff: 2 },
  { he: "פרח",     en: "flower",   topic: "nature",   minDiff: 2 },
  { he: "מים",     en: "water",    topic: "nature",   minDiff: 1 },
  { he: "שמים",    en: "sky",      topic: "nature",   minDiff: 2 },
  { he: "גשם",     en: "rain",     topic: "nature",   minDiff: 3 },
  { he: "שלג",     en: "snow",     topic: "nature",   minDiff: 3 },
  { he: "ים",      en: "sea",      topic: "nature",   minDiff: 2 },
  { he: "הר",      en: "mountain", topic: "nature",   minDiff: 4 },

  // Clothing (difficulty 3–4)
  { he: "חולצה",   en: "shirt",    topic: "clothing", minDiff: 3 },
  { he: "מכנסיים", en: "pants",    topic: "clothing", minDiff: 3 },
  { he: "נעליים",  en: "shoes",    topic: "clothing", minDiff: 3 },
  { he: "גרביים",  en: "socks",    topic: "clothing", minDiff: 3 },
  { he: "כובע",    en: "hat",      topic: "clothing", minDiff: 3 },
  { he: "מעיל",    en: "coat",     topic: "clothing", minDiff: 4 },
  { he: "שמלה",    en: "dress",    topic: "clothing", minDiff: 4 },

  // Places (difficulty 3–4)
  { he: "בית ספר", en: "school",   topic: "places",   minDiff: 2 },
  { he: "חנות",    en: "store",    topic: "places",   minDiff: 3 },
  { he: "בית חולים", en: "hospital", topic: "places", minDiff: 4 },
  { he: "פארק",    en: "park",     topic: "places",   minDiff: 3 },
  { he: "מסעדה",   en: "restaurant", topic: "places", minDiff: 4 },
  { he: "ספרייה",  en: "library",  topic: "places",   minDiff: 4 },

  // Actions/verbs (difficulty 4–5)
  { he: "לרוץ",    en: "to run",   topic: "actions",  minDiff: 4 },
  { he: "ללכת",    en: "to walk",  topic: "actions",  minDiff: 4 },
  { he: "לאכול",   en: "to eat",   topic: "actions",  minDiff: 4 },
  { he: "לשתות",   en: "to drink", topic: "actions",  minDiff: 4 },
  { he: "לישון",   en: "to sleep", topic: "actions",  minDiff: 4 },
  { he: "לשחק",    en: "to play",  topic: "actions",  minDiff: 4 },
  { he: "לקרוא",   en: "to read",  topic: "actions",  minDiff: 5 },
  { he: "לכתוב",   en: "to write", topic: "actions",  minDiff: 5 },
  { he: "לצייר",   en: "to draw",  topic: "actions",  minDiff: 5 },
  { he: "לשמוע",   en: "to hear",  topic: "actions",  minDiff: 5 },
];

// ── Grammar questions (difficulty 5–8, Israeli grades 5–6 curriculum) ─────────
// Present simple: I/you/we/they + verb | he/she/it + verb+s
// to be: am/is/are | to have: have/has | can
const GRAMMAR_QUESTIONS = [
  // to be
  { prompt: "השלם: I ___ a student.", expectedAnswer: "am", topic: "grammar", minDiff: 5 },
  { prompt: "השלם: She ___ happy.", expectedAnswer: "is", topic: "grammar", minDiff: 5 },
  { prompt: "השלם: They ___ good friends.", expectedAnswer: "are", topic: "grammar", minDiff: 5 },
  { prompt: "השלם: He ___ a teacher.", expectedAnswer: "is", topic: "grammar", minDiff: 5 },
  { prompt: "השלם: We ___ at school.", expectedAnswer: "are", topic: "grammar", minDiff: 5 },
  // to have
  { prompt: "השלם: I ___ a dog.", expectedAnswer: "have", topic: "grammar", minDiff: 5 },
  { prompt: "השלם: She ___ a blue bag.", expectedAnswer: "has", topic: "grammar", minDiff: 5 },
  { prompt: "השלם: They ___ two cats.", expectedAnswer: "have", topic: "grammar", minDiff: 5 },
  { prompt: "השלם: He ___ a big house.", expectedAnswer: "has", topic: "grammar", minDiff: 6 },
  // can
  { prompt: "השלם: I ___ swim.", expectedAnswer: "can", topic: "grammar", minDiff: 5 },
  { prompt: "השלם: She ___ run fast.", expectedAnswer: "can", topic: "grammar", minDiff: 5 },
  // plurals
  { prompt: "מה הרבים של dog?", expectedAnswer: "dogs", topic: "grammar", minDiff: 6 },
  { prompt: "מה הרבים של cat?", expectedAnswer: "cats", topic: "grammar", minDiff: 6 },
  { prompt: "מה הרבים של book?", expectedAnswer: "books", topic: "grammar", minDiff: 6 },
  { prompt: "מה הרבים של house?", expectedAnswer: "houses", topic: "grammar", minDiff: 7 },
  { prompt: "מה הרבים של box?", expectedAnswer: "boxes", topic: "grammar", minDiff: 7 },
  // sentence translation (difficulty 6+)
  { prompt: "תרגם לאנגלית: לכלב שלי יש ארבע רגליים.", expectedAnswer: "my dog has four legs", topic: "grammar", minDiff: 7 },
  { prompt: "תרגם לאנגלית: אני אוהב ספרים.", expectedAnswer: "i love books", topic: "grammar", minDiff: 7 },
  { prompt: "תרגם לאנגלית: היא רצה מהר.", expectedAnswer: "she runs fast", topic: "grammar", minDiff: 8 },
  { prompt: "תרגם לאנגלית: אנחנו בבית הספר.", expectedAnswer: "we are at school", topic: "grammar", minDiff: 8 },
];

function clamp(value, min, max) {
  const n = Number(value);
  if (Number.isNaN(n)) return min;
  return Math.max(min, Math.min(max, n));
}

function pick(list, seed = Date.now()) {
  if (!list.length) return null;
  return list[Math.abs(seed) % list.length];
}

function randomInt(min, max) {
  return Math.floor(Math.random() * (max - min + 1)) + min;
}

function normalizeText(value) {
  return String(value || "")
    .trim()
    .toLowerCase()
    .replace(/[.,!?;:()\[\]{}"'׳״]/g, "")
    .replace(/\s+/g, " ");
}

function answerVariants(answer) {
  const raw = String(answer);
  const variants = new Set([normalizeText(raw)]);
  const asNumber = Number(raw);
  if (!Number.isNaN(asNumber)) {
    variants.add(String(asNumber));
    if (HEBREW_NUMBER_WORDS[asNumber]) variants.add(normalizeText(HEBREW_NUMBER_WORDS[asNumber]));
  }
  return Array.from(variants).filter(Boolean);
}

// ── Hebrew number-phrase parser ──────────────────────────────────────────────
// STT often writes a spoken number as WORDS ("עשרים וארבע"), not digits. The
// old grading only knew a 0–20 feminine lookup, so "3 כפול 8" answered
// "עשרים וארבע" was marked wrong. Parse the whole phrase to an integer instead:
// 0–999, both genders (ארבע/ארבעה), teens (ארבע עשרה), tens+units with
// ו' החיבור (עשרים וארבע), hundreds (מאה/מאתיים/שלוש מאות). Returns null when
// any word isn't a number word — so ordinary text answers are unaffected.
const HEB_NUM_TOKEN = {
  "אפס": 0,
  "אחת": 1, "אחד": 1,
  "שתיים": 2, "שניים": 2, "שתים": 2, "שנים": 2,
  "שלוש": 3, "שלושה": 3,
  "ארבע": 4, "ארבעה": 4,
  "חמש": 5, "חמישה": 5,
  "שש": 6, "שישה": 6,
  "שבע": 7, "שבעה": 7,
  "שמונה": 8,
  "תשע": 9, "תשעה": 9,
  "עשר": 10, "עשרה": 10,
  "עשרים": 20, "שלושים": 30, "ארבעים": 40, "חמישים": 50,
  "שישים": 60, "שבעים": 70, "שמונים": 80, "תשעים": 90,
  "מאה": 100, "מאתיים": 200,
};

function hebrewWordsToNumber(text) {
  const words = String(text || "").replace(/-/g, " ").trim().split(/\s+/).filter(Boolean);
  if (words.length === 0) return null;
  let hundreds = 0, tens = 0, unit = null, teen = false, sawAny = false;
  for (let raw of words) {
    // ו' החיבור: "וארבע" → "ארבע" (only when the rest is a number word).
    if (raw.length > 1 && raw[0] === "ו" && (HEB_NUM_TOKEN[raw.slice(1)] != null || raw.slice(1) === "מאות")) {
      raw = raw.slice(1);
    }
    if (raw === "מאות") {                       // "שלוש מאות" — unit becomes hundreds
      if (unit == null || teen || hundreds) return null;
      hundreds = unit * 100; unit = null; sawAny = true; continue;
    }
    const val = HEB_NUM_TOKEN[raw];
    if (val == null) return null;                // not a number phrase after all
    sawAny = true;
    if (val === 100 || val === 200) {
      if (hundreds || tens || unit != null) return null;
      hundreds = val;
    } else if (val >= 20) {                      // עשרים..תשעים
      if (tens || teen || unit != null) return null;
      tens = val;
    } else if (val === 10) {                     // teen marker or standalone 10
      if (unit != null && !tens && !teen) { unit += 10; teen = true; } // ארבע עשרה = 14
      else if (unit == null && !tens && !teen) tens = 10;             // עשר = 10
      else return null;
    } else {                                     // 0–9
      if (unit != null || teen) return null;
      unit = val;
    }
  }
  if (!sawAny) return null;
  return hundreds + tens + (unit == null ? 0 : unit);
}

// Common lead-in / filler words a child may say around the real answer
// ("התשובה היא תשע", "it is dog"). Stripping these before an exact match keeps a
// number embedded in a phrase counting as correct — WITHOUT the loose substring
// match below wrongly accepting "תשע" inside "תשע עשרה" (9 vs 19) or "9" in "19".
const ANSWER_FILLER = new Set([
  "התשובה", "היא", "הוא", "זה", "זהו", "שווה", "הפתרון", "אולי", "נראה", "לי", "אה", "אמ",
  "the", "answer", "is", "it", "um", "uh", "like", "maybe",
]);

function stripAnswerFiller(text) {
  return text.split(" ").filter((t) => t && !ANSWER_FILLER.has(t)).join(" ");
}

// ── Cross-script phonetic matching (Hebrew STT ↔ English answer) ─────────────
// In English practice the child answers by VOICE, and the Hebrew-primary STT
// often writes the English word in Hebrew letters — "רד" for red, "בלו" for
// blue. The exact/variant compare above can never match those, so correct
// answers were graded wrong. Fix: reduce both sides to a shared consonant
// "skeleton" — vowels dropped, letters Hebrew spelling can't distinguish merged
// (b/v/w→b, p/f→p, s/sh/z/צ→s, d/t/th→t, k/c/q/ck→k, ch/ג'→j, silent gh) —
// and accept when the skeletons are equal. Only ever applied CROSS-script
// (Hebrew transcript vs Latin expected), so Hebrew↔Hebrew and math grading are
// untouched.
const HEBREW_RE = /[֐-׿]/;

// Hebrew letter → skeleton token. Vowel-ish letters (אהעיו) drop — Hebrew
// spellings of English words use them for vowel sounds ("בלו", "ואן").
const HE_SKEL = {
  "א": "", "ב": "b", "ג": "g", "ד": "t", "ה": "", "ו": "", "ז": "s",
  "ח": "h", "ט": "t", "י": "", "כ": "k", "ך": "k", "ל": "l", "מ": "m",
  "ם": "m", "נ": "n", "ן": "n", "ס": "s", "ע": "", "פ": "p", "ף": "p",
  "צ": "s", "ץ": "s", "ק": "k", "ר": "r", "ש": "s", "ת": "t",
};
// NOTE on geresh (ג'/צ'/ז'): normalizeText strips apostrophes BEFORE we get
// here, so "צ'ר" arrives as "צר". The Latin side therefore maps to the BASE
// letters' sounds: ch→s (what stripped צ' becomes) and j→g (stripped ג').

// Double-vav is ambiguous — the w SOUND as a consonant ("ווטר" water) or just
// a long vowel ("וואן" one, where the spelling has no w). Emit both readings.
function hebrewSkeletons(word) {
  const build = (vavAsConsonant) => {
    let out = "";
    for (let i = 0; i < word.length; i++) {
      const ch = word[i];
      if (ch === "ו" && word[i + 1] === "ו") {
        if (vavAsConsonant) out += "b";
        i++; continue;
      }
      if (HE_SKEL[ch] != null) out += HE_SKEL[ch];
    }
    return out.replace(/(.)\1+/g, "$1");             // collapse doubles
  };
  return Array.from(new Set([build(true), build(false)]));
}

// English word → the same skeleton alphabet. Returns two variants because a
// `w` may surface in Hebrew as a consonant ("ווטר") or vanish into a vowel
// ("one" → "ואן"): try w→v-like and w→dropped.
function latinSkeletons(word) {
  const base = word.toLowerCase()
    .replace(/wh/g, "w")                             // white — silent h
    .replace(/gh/g, "")                              // eight, night — silent
    .replace(/c(?=[eiy])/g, "s")                     // soft c: pencil, rice
    .replace(/ph/g, "p").replace(/sh/g, "s").replace(/ch/g, "s")
    .replace(/th/g, "t").replace(/ck/g, "k")
    .replace(/x/g, "ks").replace(/q/g, "k").replace(/c/g, "k")
    .replace(/j/g, "g");
  return [base.replace(/w/g, "v"), base.replace(/w/g, "")].map((v) =>
    v.replace(/v/g, "b").replace(/f/g, "p").replace(/z/g, "s").replace(/d/g, "t")
      .replace(/[aeiouy]/g, "")
      .replace(/[^a-z]/g, "")
      .replace(/(.)\1+/g, "$1"));
}

// True when one side is Hebrew script, the other Latin, and their phonetic
// skeletons agree ("רד" ↔ "red"). Same-script pairs return false — the exact
// paths in checkAnswer already own those.
function phoneticMatch(expectedWord, childWord) {
  const expHeb = HEBREW_RE.test(expectedWord);
  const ansHeb = HEBREW_RE.test(childWord);
  if (expHeb === ansHeb) return false;
  const hebs = hebrewSkeletons(ansHeb ? childWord : expectedWord).filter(Boolean);
  if (hebs.length === 0) return false;
  const latin = ansHeb ? expectedWord : childWord;
  return latinSkeletons(latin).some((s) => s.length > 0 && hebs.includes(s));
}

function checkAnswer(expectedAnswer, childAnswer) {
  const expected = answerVariants(expectedAnswer);
  const actual = normalizeText(childAnswer);
  if (!actual) return false;
  if (expected.includes(actual)) return true;                  // exact / variant match
  const stripped = stripAnswerFiller(actual);                  // "זה תשע" → "תשע"
  if (stripped && expected.includes(stripped)) return true;
  // Numeric expected + spoken-words answer: parse the WHOLE phrase as a Hebrew
  // number ("עשרים וארבע" → 24) and compare values. Covers 0–999 in both
  // genders — the old 0–20 lookup missed e.g. every multiplication result >20.
  // Whole-phrase parsing keeps 9 from matching inside "תשע עשרה" (19).
  const expNum = Number(String(expectedAnswer).trim());
  if (!Number.isNaN(expNum)) {
    const spoken = hebrewWordsToNumber(stripped || actual);
    if (spoken != null && spoken === expNum) return true;
  }
  // Cross-script phonetic pass: Hebrew-lettered STT of an English answer
  // ("רד" for red). Checked word-by-word (child may pad with filler) and as a
  // spaces-stripped whole for multi-word answers ("ice cream" ↔ "אייס קרים").
  const expRaw = normalizeText(expectedAnswer);
  const answerWords = (stripped || actual).split(" ");
  if (answerWords.some((w) => phoneticMatch(expRaw, w))) return true;
  if (phoneticMatch(expRaw.replace(/\s+/g, ""), (stripped || actual).replace(/\s+/g, ""))) {
    return true;
  }
  // Loose substring match lets a child pad a TEXT answer ("a dog", "dog!").
  // Skip it for numbers: "9" must not match inside "19"/"90", nor "תשע" (9)
  // inside "תשע עשרה" (19).
  if (/^\d+$/.test(String(expectedAnswer).trim())) return false;
  return expected.some((v) => v.length > 1 && actual.includes(v));
}

function generateMathQuestion({ age = 8, difficulty = 1, topics = [] } = {}) {
  const level = clamp(difficulty, 1, 10);
  const topic = pick(topics, Math.floor(Math.random() * 1000)) || inferMathTopic(age, level);
  let a, b, prompt, expectedAnswer;

  if (topic === "division" || level >= 6) {
    b = randomInt(2, Math.min(9, 2 + level));
    expectedAnswer = String(randomInt(2, Math.min(12, 3 + level)));
    a = Number(expectedAnswer) * b;
    prompt = `כמה זה ${a} חלקי ${b}?`;
  } else if (topic === "multiplication" || level >= 4) {
    a = randomInt(2, Math.min(10, 3 + level));
    b = randomInt(2, Math.min(10, 3 + level));
    prompt = `כמה זה ${a} כפול ${b}?`;
    expectedAnswer = String(a * b);
  } else if (topic === "subtraction" || level >= 2) {
    a = randomInt(6, Math.min(40, 10 + level * 4));
    b = randomInt(1, Math.min(a, 5 + level * 2));
    prompt = `כמה זה ${a} פחות ${b}?`;
    expectedAnswer = String(a - b);
  } else {
    a = randomInt(1, Math.min(20, 6 + level * 3));
    b = randomInt(1, Math.min(20, 6 + level * 3));
    prompt = `כמה זה ${a} ועוד ${b}?`;
    expectedAnswer = String(a + b);
  }

  return {
    subject: "math",
    prompt,
    expectedAnswer,
    topic,
    difficulty: level,
    answerVariants: answerVariants(expectedAnswer),
  };
}

function inferMathTopic(age, level) {
  if (age <= 7) return level <= 2 ? "addition" : "subtraction";
  if (level <= 1) return "addition";
  if (level <= 3) return "subtraction";
  if (level <= 5) return "multiplication";
  return "division";
}

function generateEnglishQuestion({ difficulty = 1, topics = [] } = {}) {
  const level = clamp(difficulty, 1, 10);

  // Grammar questions at difficulty 5+
  if (level >= 5) {
    const eligible = GRAMMAR_QUESTIONS.filter((q) => q.minDiff <= level);
    if (eligible.length > 0) {
      // Mix grammar and vocab — pick grammar ~40% of the time at level 5+
      if (Math.random() < 0.4) {
        const q = eligible[randomInt(0, eligible.length - 1)];
        return {
          subject: "english",
          prompt: q.prompt,
          expectedAnswer: q.expectedAnswer,
          topic: q.topic,
          difficulty: level,
          answerVariants: answerVariants(q.expectedAnswer),
        };
      }
    }
  }

  // Spelling at difficulty 5+ — exclude multi-word entries (verbs like "to walk")
  if (level >= 5 && Math.random() < 0.3) {
    const eligibleVocab = ENGLISH_VOCAB.filter((w) => w.minDiff <= level && w.en.length >= 4 && !w.en.includes(" "));
    if (eligibleVocab.length > 0) {
      const item = eligibleVocab[randomInt(0, eligibleVocab.length - 1)];
      return {
        subject: "english",
        prompt: `איך מאייתים את המילה ${item.en} באנגלית? תגיד/י את האותיות לפי הסדר.`,
        expectedAnswer: item.en.split("").join(" "),
        topic: "spelling",
        difficulty: level,
        answerVariants: [item.en, item.en.split("").join(" "), normalizeText(item.en)],
      };
    }
  }

  // Standard translation question — filter by topic preference and difficulty
  const preferred = topics.length > 0
    ? ENGLISH_VOCAB.filter((w) => w.minDiff <= level && topics.includes(w.topic))
    : [];
  const pool = preferred.length > 0
    ? preferred
    : ENGLISH_VOCAB.filter((w) => w.minDiff <= level);

  if (pool.length === 0) {
    // Fallback: easiest words
    const item = ENGLISH_VOCAB[randomInt(0, Math.min(10, ENGLISH_VOCAB.length - 1))];
    return {
      subject: "english",
      prompt: `איך אומרים ${item.he} באנגלית?`,
      expectedAnswer: item.en,
      topic: item.topic,
      difficulty: level,
      answerVariants: answerVariants(item.en),
    };
  }

  const item = pool[randomInt(0, pool.length - 1)];
  return {
    subject: "english",
    prompt: `איך אומרים ${item.he} באנגלית?`,
    expectedAnswer: item.en,
    topic: item.topic,
    difficulty: level,
    answerVariants: answerVariants(item.en),
  };
}

function generateQuestion({ subject = "math", age = 8, difficulty = 1, topics = [] } = {}) {
  if (subject === "english") return generateEnglishQuestion({ difficulty, topics });
  return generateMathQuestion({ age, difficulty, topics });
}

module.exports = {
  clamp,
  normalizeText,
  answerVariants,
  checkAnswer,
  generateQuestion,
  generateMathQuestion,
  generateEnglishQuestion,
};
