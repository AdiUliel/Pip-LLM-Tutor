// Unit tests for the answer-grading pipeline (questionGenerator.js).
// Plain Node — no test framework needed. Run with:  npm test
//
// Covers the three grading layers:
//   1. Exact / variant matching + filler stripping ("התשובה היא 24")
//   2. Spoken Hebrew number phrases ("עשרים וארבע" → 24), 0–999, both genders
//   3. Cross-script phonetic matching for English lessons ("רד" ↔ "red")
// plus sanity checks on the local question generator.

const assert = require("assert");
const { checkAnswer, answerVariants, clamp, generateQuestion } = require("../questionGenerator");

let passed = 0, failed = 0;
function check(name, actual, expected) {
  if (actual === expected) { passed++; return; }
  failed++;
  console.error(`✗ ${name} — got ${JSON.stringify(actual)}, expected ${JSON.stringify(expected)}`);
}

// ── 1. Exact / variant / filler ──────────────────────────────────────────────
const exactCases = [
  ["9", "9", true],
  ["9", "תשע", true],                 // Hebrew number word
  ["24", "התשובה היא 24", true],      // filler stripped
  ["dog", "the answer is dog", true],
  ["dog", "a dog", true],             // loose text match
  ["9", "תשע עשרה", false],           // 9 must not match inside 19
  ["9", "19", false],
  ["45", "45", true],
];
for (const [exp, ans, want] of exactCases) {
  check(`exact  (${exp} | ${ans})`, checkAnswer(exp, ans), want);
}

// ── 2. Spoken Hebrew numbers (both genders, tens+units, teens, hundreds) ─────
const numberCases = [
  ["24", "עשרים וארבע", true], ["24", "עשרים וארבעה", true],
  ["24", "התשובה היא עשרים וארבע", true],
  ["30", "שלושים", true], ["45", "ארבעים וחמש", true], ["45", "ארבעים וחמישה", true],
  ["72", "שבעים ושתיים", true], ["72", "שבעים ושניים", true],
  ["100", "מאה", true], ["108", "מאה ושמונה", true], ["300", "שלוש מאות", true],
  ["10", "עשר", true], ["10", "עשרה", true],
  ["14", "ארבע עשרה", true], ["14", "ארבעה עשר", true], ["14", "ארבע-עשרה", true],
  ["19", "תשע עשרה", true], ["11", "אחת עשרה", true], ["11", "אחד עשר", true],
  ["0", "אפס", true], ["8", "שמונה", true], ["3", "שלושה", true],
  // must NOT match
  ["24", "עשרים", false], ["24", "ארבע", false], ["24", "עשרים וחמש", false],
  ["240", "עשרים וארבע", false],
  ["24", "עשרים וארבע ילדים", false], // trailing non-number word — not a number phrase
  ["19", "תשע", false],
];
for (const [exp, ans, want] of numberCases) {
  check(`number (${exp} | ${ans})`, checkAnswer(exp, ans), want);
}

// ── 3. Cross-script phonetic (English answered in Hebrew letters) ────────────
const phoneticCases = [
  // colors
  ["red", "רד", true], ["red", "ראד", true], ["blue", "בלו", true],
  ["green", "גרין", true], ["yellow", "ילו", true], ["yellow", "יילו", true],
  ["black", "בלאק", true], ["white", "וייט", true], ["white", "ווייט", true],
  ["orange", "אורנג'", true], ["purple", "פרפל", true], ["pink", "פינק", true],
  ["brown", "בראון", true],
  // animals / school / food
  ["dog", "דוג", true], ["cat", "קט", true], ["cat", "קאט", true],
  ["turtle", "טרטל", true], ["book", "בוק", true], ["pencil", "פנסיל", true],
  ["chair", "צ'ר", true], ["chair", "צ'אר", true], ["table", "טייבל", true],
  ["computer", "קומפיוטר", true], ["bag", "באג", true],
  ["apple", "אפל", true], ["banana", "בננה", true], ["bread", "ברד", true],
  ["milk", "מילק", true], ["egg", "אג", true], ["cake", "קייק", true],
  ["rice", "רייס", true],
  // family (th sounds)
  ["mother", "מאדר", true], ["father", "פאדר", true],
  ["brother", "בראדר", true], ["sister", "סיסטר", true],
  // numbers in English lessons
  ["one", "ואן", true], ["one", "וואן", true], ["two", "טו", true],
  ["three", "תרי", true], ["three", "טרי", true], ["four", "פור", true],
  ["five", "פייב", true], ["six", "סיקס", true], ["seven", "סבן", true],
  ["eight", "אייט", true], ["nine", "ניין", true], ["ten", "טן", true],
  // with filler
  ["red", "זה רד", true], ["dog", "התשובה היא דוג", true],
  // must NOT falsely match
  ["red", "בלו", false], ["red", "ירוק", false], ["cat", "דוג", false],
  ["blue", "כחול", false],          // Hebrew translation is not the English word
  ["pink", "פרפל", false], ["book", "באג", false], ["ten", "טו", false],
  ["9", "רד", false],
  // English answered in English still passes
  ["red", "red", true], ["red", "the answer is red", true],
];
for (const [exp, ans, want] of phoneticCases) {
  check(`phonet (${exp} | ${ans})`, checkAnswer(exp, ans), want);
}

// ── 4. answerVariants ────────────────────────────────────────────────────────
check("variants include the raw answer", answerVariants("Red").includes("red"), true);
check("variants include hebrew word for 9", answerVariants("9").includes("תשע"), true);
check("clamp low", clamp(0, 1, 10), 1);
check("clamp high", clamp(99, 1, 10), 10);

// ── 5. Question generator sanity (all subjects, all difficulty levels) ───────
for (const subject of ["math", "english"]) {
  for (let level = 1; level <= 10; level++) {
    for (let i = 0; i < 20; i++) {
      const q = generateQuestion({ subject, age: 8, difficulty: level });
      const label = `gen ${subject} L${level}`;
      check(`${label} has prompt`, typeof q.prompt === "string" && q.prompt.length > 0, true);
      check(`${label} has answer`, typeof q.expectedAnswer === "string" && q.expectedAnswer.length > 0, true);
      // The expected answer must grade as correct against its own question.
      check(`${label} self-consistent`, checkAnswer(q.expectedAnswer, q.expectedAnswer), true);
      // Math: parse "כמה זה A <op> B?" and verify the arithmetic.
      if (subject === "math") {
        const m = q.prompt.match(/(\d+)\s+(ועוד|פחות|כפול|חלקי)\s+(\d+)/);
        if (m) {
          const a = Number(m[1]), b = Number(m[3]);
          const ops = { "ועוד": a + b, "פחות": a - b, "כפול": a * b, "חלקי": a / b };
          check(`${label} arithmetic ${a} ${m[2]} ${b}`,
            Number(q.expectedAnswer), ops[m[2]]);
        }
      }
    }
  }
}

// ── Summary ──────────────────────────────────────────────────────────────────
console.log(`\n${passed} passed, ${failed} failed`);
if (failed > 0) process.exit(1);
console.log("ALL TESTS PASSED");
