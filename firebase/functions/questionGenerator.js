const HEBREW_NUMBER_WORDS = {
  0: "אפס",
  1: "אחת",
  2: "שתיים",
  3: "שלוש",
  4: "ארבע",
  5: "חמש",
  6: "שש",
  7: "שבע",
  8: "שמונה",
  9: "תשע",
  10: "עשר",
  11: "אחת עשרה",
  12: "שתים עשרה",
  13: "שלוש עשרה",
  14: "ארבע עשרה",
  15: "חמש עשרה",
  16: "שש עשרה",
  17: "שבע עשרה",
  18: "שמונה עשרה",
  19: "תשע עשרה",
  20: "עשרים",
};

const ENGLISH_VOCAB = [
  { he: "כלב", en: "dog" },
  { he: "חתול", en: "cat" },
  { he: "בית", en: "house" },
  { he: "ספר", en: "book" },
  { he: "מים", en: "water" },
  { he: "שמש", en: "sun" },
  { he: "ירח", en: "moon" },
  { he: "אדום", en: "red" },
  { he: "כחול", en: "blue" },
  { he: "שמחה", en: "happy" },
  { he: "בית ספר", en: "school" },
  { he: "חבר", en: "friend" },
  { he: "תפוח", en: "apple" },
  { he: "כדור", en: "ball" },
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

function checkAnswer(expectedAnswer, childAnswer) {
  const expected = answerVariants(expectedAnswer);
  const actual = normalizeText(childAnswer);
  if (!actual) return false;
  if (expected.includes(actual)) return true;
  // Lightweight tolerance for speech-to-text that adds extra words.
  return expected.some((v) => v.length > 1 && actual.includes(v));
}

function generateMathQuestion({ age = 8, difficulty = 1, topics = [] } = {}) {
  const level = clamp(difficulty, 1, 10);
  const topic = pick(topics, Math.floor(Math.random() * 1000)) || inferMathTopic(age, level);
  let a;
  let b;
  let prompt;
  let expectedAnswer;

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
  return "division"; // level 6+
}

function generateEnglishQuestion({ difficulty = 1, topics = [] } = {}) {
  const level = clamp(difficulty, 1, 10);
  const item = pick(ENGLISH_VOCAB, randomInt(0, 9999));
  const topic = pick(topics, randomInt(0, 9999)) || "vocabulary";

  if (topic === "spelling" || level >= 5) {
    return {
      subject: "english",
      prompt: `איך מאייתים באנגלית את המילה ${item.en}? תגיד/י את האותיות לפי הסדר.`,
      expectedAnswer: item.en.split("").join(" "),
      topic: "spelling",
      difficulty: level,
      answerVariants: [item.en, item.en.split("").join(" ")],
    };
  }

  return {
    subject: "english",
    prompt: `איך אומרים ${item.he} באנגלית?`,
    expectedAnswer: item.en,
    topic,
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
  checkAnswer,
  generateQuestion,
  generateMathQuestion,
  generateEnglishQuestion,
};
