const { FieldValue } = require("firebase-admin/firestore");
const { checkAnswer, clamp, generateQuestion } = require("./questionGenerator");

// Default model — kept in sync with index.js. gemini-2.0-flash-001 was
// discontinued (404); 2.5-flash is the current child-safe default.
const DEFAULT_MODEL = process.env.GEMINI_MODEL || "gemini-2.5-flash";

// Child-safe safety settings applied to every Gemini call in this module.
const CHILD_SAFETY_SETTINGS = [
  { category: "HARM_CATEGORY_HARASSMENT",        threshold: "BLOCK_LOW_AND_ABOVE" },
  { category: "HARM_CATEGORY_HATE_SPEECH",        threshold: "BLOCK_LOW_AND_ABOVE" },
  { category: "HARM_CATEGORY_SEXUALLY_EXPLICIT",  threshold: "BLOCK_LOW_AND_ABOVE" },
  { category: "HARM_CATEGORY_DANGEROUS_CONTENT",  threshold: "BLOCK_LOW_AND_ABOVE" },
];

const SYSTEM_PROMPT = `אתה פיפ — מורה רובוט חם ומעודד לילדים בגילאי 7–11.
אתה עוזר לילד להתאמן במתמטיקה ובאנגלית.

כללים חשובים:
- דבר תמיד בעברית, אלא אם הילד מתאמן על מילים באנגלית.
- פנה לילד בשמו אם אתה יודע אותו.
- שמור על תשובה קצרה מאוד — עד שלושה משפטים — כי הטקסט יישמע בקול על ידי רובוט.
- השתמש בשפה פשוטה, חמה ומעודדת שמתאימה לילד בן 7–10.
- אם התשובה נכונה: חגוג! השתמש במילים כמו "מדהים!", "וואו!", "כל הכבוד!", "אלוף/ה!".
- אם התשובה שגויה: אל תאמר רק "לא נכון". במקום זאת —
    1. הכר בניסיון של הילד בחמימות ("לא נורא, ניסית!")
    2. הסבר בקצרה איך לחשוב על זה (למשל: "כשמחברים 6 ועוד 3 סופרים שלוש קפיצות קדימה מ-6")
    3. תן את התשובה הנכונה: "ולכן התשובה היא X"
    4. סיים במשפט מעודד קצר.
- אם הילד נתן תשובה שלא קשורה לשאלה, הפנה אותו בחביבות בחזרה: "בוא נחזור לשאלה שלנו..."
- אם הילד מתסכל (הרבה טעויות ברצף), תהיה סבלני במיוחד, פשט את ההסבר ושקול להציע הפסקה קצרה.
- אל תזכיר בשום אופן שאתה AI, רובוט, מחשב, או תוכנה.
- אל תדון בנושאים לא מתאימים לגיל (אלימות, מבוגרים, מפחיד).
- בחר emotion שמשקף את הרגע: celebrating לרצף הצלחות, happy לתשובה נכונה, encouraging לטעות רגילה, concerned כשהילד מתקשה.

החזר JSON תקין בלבד עם המפתחות הבאים בדיוק:
{
  "spokenFeedback": string,
  "emotion": "happy" | "neutral" | "encouraging" | "concerned" | "celebrating",
  "shouldTakeBreak": boolean
}`;

// ── Exit-intent detection ─────────────────────────────────────────────────────
const EXIT_PHRASES = [
  "לסיים", "רוצה לסיים", "נגמרנו", "נגמר", "סיום", "להפסיק",
  "עייף", "עייפה", "לא רוצה", "stop", "finish", "bye", "done", "quit",
];

function detectExitIntent(text) {
  const t = String(text || "").toLowerCase().trim();
  return EXIT_PHRASES.some((p) => t.includes(p));
}

// ── Material-based question fetching ─────────────────────────────────────────
// If the child's profile has useUploadedMaterials:true, pull an unused question
// from the parent's enabled material docs. Falls back to the deterministic
// generator if no eligible material questions remain.
async function fetchMaterialQuestion(db, sessionId, session, child, subject) {
  if (!child?.useUploadedMaterials) return null;
  const parentId = session.parentId;
  if (!parentId) return null;

  const usedSnap = await db
    .collection("sessions").doc(sessionId)
    .collection("questions").get();
  const usedPrompts = new Set(usedSnap.docs.map((d) => d.data().prompt));

  const materialsSnap = await db.collection("materials")
    .where("parentId", "==", parentId)
    .where("subject", "==", subject)
    .where("enabled", "==", true)
    .get();

  const candidates = [];
  materialsSnap.forEach((doc) => {
    (doc.data().questions || []).forEach((q) => {
      if (q.prompt && q.expectedAnswer && !usedPrompts.has(q.prompt)) {
        candidates.push(q);
      }
    });
  });

  if (!candidates.length) return null;

  const q = candidates[Math.floor(Math.random() * candidates.length)];
  return {
    subject,
    prompt: q.prompt,
    expectedAnswer: String(q.expectedAnswer),
    topic: q.topic || subject,
    difficulty: session.currentDifficulty || 1,
    answerVariants: [String(q.expectedAnswer).toLowerCase().trim()],
    fromMaterial: true,
  };
}

function subjectTopicsFromChild(child, subject) {
  const raw = child.topicFocus || {};
  const topics = raw[subject];
  return Array.isArray(topics) ? topics.filter(Boolean) : [];
}

function ageLevelFloor(age, subject) {
  if (subject === "english") return 1;
  const a = Number(age) || 8;
  if (a <= 7) return 1;
  if (a <= 8) return 2;
  if (a <= 9) return 3;
  if (a <= 10) return 4;
  return 5;
}

function levelFromChild(child, subject) {
  const raw = (child || {}).level || {};
  const stored = raw[subject];
  if (stored != null) return clamp(stored, 1, 10);
  return clamp(ageLevelFloor((child || {}).age, subject), 1, 10);
}

function genderSuffix(child) {
  return child.gender === "boy" ? "" : "י";
}

function deterministicFeedback({ child, isCorrect, expectedAnswer, childAnswer, streakWrong, streakCorrect }) {
  const suffix = genderSuffix(child || {});
  if (isCorrect) {
    if (streakCorrect >= 2) {
      return {
        spokenFeedback: `מעולה! זאת תשובה נכונה. צברת רצף יפה, כל הכבוד${suffix}!`,
        emotion: "celebrating",
        shouldTakeBreak: false,
      };
    }
    return {
      spokenFeedback: `נכון מאוד! התשובה היא ${expectedAnswer}. כל הכבוד${suffix}!`,
      emotion: "happy",
      shouldTakeBreak: false,
    };
  }

  if (streakWrong >= 2) {
    return {
      spokenFeedback: `לא נורא, זה קצת מסובך. התשובה הנכונה היא ${expectedAnswer} — כדאי לחשוב על זה לאט לאט. נעשה שאלה קצת יותר קלה ונמשיך ביחד, אני כאן איתך!`,
      emotion: "concerned",
      shouldTakeBreak: streakWrong >= 3,
    };
  }

  return {
    spokenFeedback: `ניסית, וזה הכי חשוב! ענית ${childAnswer || "תשובה לא ברורה"}, אבל התשובה הנכונה היא ${expectedAnswer}. בפעם הבאה ננסה לחשב שלב שלב — אני בטוח שנצליח יחד!`,
    emotion: "encouraging",
    shouldTakeBreak: false,
  };
}

function computeNextDifficulty(currentDifficulty, isCorrect, streakWrong, streakCorrect) {
  if (streakWrong >= 2) return clamp(currentDifficulty - 1, 1, 10);
  if (isCorrect && streakCorrect >= 3) return clamp(currentDifficulty + 1, 1, 10);
  return clamp(currentDifficulty, 1, 10);
}

function moodFromEmotion(emotion) {
  switch (emotion) {
    case "celebrating":
      return 5;
    case "happy":
      return 4;
    case "concerned":
      return 2;
    case "encouraging":
      return 3;
    default:
      return 3;
  }
}

async function readChild(db, childId) {
  if (!childId) return null;
  const snap = await db.collection("children").doc(childId).get();
  return snap.exists ? { id: snap.id, ...snap.data() } : null;
}

async function readRecentQuestions(db, sessionId, limit = 5) {
  const snap = await db
    .collection("sessions")
    .doc(sessionId)
    .collection("questions")
    .orderBy("askedAt", "desc")
    .limit(limit)
    .get();
  return snap.docs.map((doc) => ({ id: doc.id, ...doc.data() }));
}

function safeJsonParse(text) {
  if (!text) return null;
  const trimmed = String(text).trim();
  try {
    return JSON.parse(trimmed);
  } catch (_) {
    const match = trimmed.match(/\{[\s\S]*\}/);
    if (!match) return null;
    try {
      return JSON.parse(match[0]);
    } catch (_) {
      return null;
    }
  }
}

async function llmFeedback(ai, payload, model = DEFAULT_MODEL, safetySettings = CHILD_SAFETY_SETTINGS) {
  if (!ai) return null;
  const result = await ai.models.generateContent({
    model,
    contents: [
      {
        role: "user",
        parts: [{ text: JSON.stringify(payload, null, 2) }],
      },
    ],
    config: {
      systemInstruction: SYSTEM_PROMPT,
      maxOutputTokens: 180,
      temperature: 0.45,
      responseMimeType: "application/json",
      safetySettings,
    },
  });
  return safeJsonParse(result.text);
}

function buildSessionPatch({ isCorrect, nextDifficulty, feedback, currentQuestion }) {
  return {
    status: feedback.shouldTakeBreak ? "break" : "active",
    currentDifficulty: nextDifficulty,
    currentQuestion: currentQuestion.prompt,
    currentExpectedAnswer: currentQuestion.expectedAnswer,
    currentTopic: currentQuestion.topic,
    currentAnswerVariants: currentQuestion.answerVariants || [],
    lastEmotion: feedback.emotion,
    lastFeedback: feedback.spokenFeedback,
    shouldTakeBreak: feedback.shouldTakeBreak,
    questionsAsked: FieldValue.increment(1),
    correctCount: FieldValue.increment(isCorrect ? 1 : 0),
    wrongCount: FieldValue.increment(isCorrect ? 0 : 1),
    starsEarned: FieldValue.increment(isCorrect ? 1 : 0),
    lastActivity: FieldValue.serverTimestamp(),
  };
}

async function createInitialQuestion(db, sessionRef, sessionData, child) {
  const subject = sessionData.subject || "math";
  const difficulty = clamp(sessionData.currentDifficulty || levelFromChild(child || {}, subject), 1, 10);
  const topics = subjectTopicsFromChild(child || {}, subject);
  const question = generateQuestion({
    subject,
    age: child?.age || 8,
    difficulty,
    topics,
  });
  await sessionRef.set(
    {
      status: "active",
      subject,
      currentDifficulty: difficulty,
      currentQuestion: question.prompt,
      currentExpectedAnswer: question.expectedAnswer,
      currentTopic: question.topic,
      currentAnswerVariants: question.answerVariants || [],
      questionsAsked: sessionData.questionsAsked || 0,
      correctCount: sessionData.correctCount || 0,
      wrongCount: sessionData.wrongCount || 0,
      starsEarned: sessionData.starsEarned || 0,
      longestStreak: sessionData.longestStreak || 0,
      moodSummary: sessionData.moodSummary || 3,
      lastEmotion: "happy",
      lastFeedback: "מתחילים! אני אשאל שאלה אחת בכל פעם.",
      lastActivity: FieldValue.serverTimestamp(),
    },
    { merge: true }
  );
  return question;
}

async function processLearningTurn({
  db,
  ai,
  model = DEFAULT_MODEL,
  safetySettings = CHILD_SAFETY_SETTINGS,
  synthesize = null,
  sessionId,
  exchangeId,
  exchangeData,
}) {
  const sessionRef = db.collection("sessions").doc(sessionId);
  const exchangeRef = sessionRef.collection("exchanges").doc(exchangeId);
  const sessionSnap = await sessionRef.get();
  if (!sessionSnap.exists) throw new Error(`Session ${sessionId} does not exist`);

  const session = sessionSnap.data();
  const child = await readChild(db, session.childId);
  let currentQuestion = session.currentQuestion;
  let expectedAnswer = session.currentExpectedAnswer;

  if (!currentQuestion || !expectedAnswer) {
    const generated = await createInitialQuestion(db, sessionRef, session, child);
    currentQuestion = generated.prompt;
    expectedAnswer = generated.expectedAnswer;
  }

  const childAnswer = exchangeData.childAnswer || exchangeData.childAnswerTranscript || exchangeData.question || "";

  // ── Exit intent ─────────────────────────────────────────────────────────────
  if (detectExitIntent(childAnswer)) {
    const childName = child?.name || "";
    const farewellText = childName
      ? `כל הכבוד ${childName}! עבדת מצוין היום. נתראה בפעם הבאה!`
      : `כל הכבוד! עבדת מצוין היום. נתראה בפעם הבאה!`;

    const audioUrl = synthesize ? await synthesize(farewellText, `${exchangeId}_farewell`) : "";
    const now = FieldValue.serverTimestamp();

    await db.runTransaction(async (tx) => {
      tx.update(exchangeRef, {
        status: "done",
        sessionEnded: true,
        spokenFeedback: farewellText,
        audioUrl,
        emotion: "happy",
        answeredAt: now,
      });
      tx.set(sessionRef, { status: "ended", endedAt: now, lastActivity: now }, { merge: true });
    });

    return { sessionEnded: true, spokenFeedback: farewellText, audioUrl };
  }

  const isCorrect = checkAnswer(expectedAnswer, childAnswer);
  const previousWrong = Number(session.consecutiveWrong || 0);
  const previousCorrect = Number(session.consecutiveCorrect || 0);
  const streakWrong = isCorrect ? 0 : previousWrong + 1;
  const streakCorrect = isCorrect ? previousCorrect + 1 : 0;
  const currentDifficulty = clamp(session.currentDifficulty || 1, 1, 10);
  const nextDifficulty = computeNextDifficulty(currentDifficulty, isCorrect, streakWrong, streakCorrect);

  const deterministic = deterministicFeedback({
    child,
    isCorrect,
    expectedAnswer,
    childAnswer,
    streakWrong,
    streakCorrect,
  });

  let feedback = deterministic;
  try {
    const recentQuestions = await readRecentQuestions(db, sessionId, 5);
    const llm = await llmFeedback(ai, {
      child: {
        age: child?.age || 8,
        gender: child?.gender || "girl",
        name: child?.name || "",
      },
      session: {
        subject: session.subject || "math",
        currentDifficulty,
        nextDifficulty,
        consecutiveCorrect: streakCorrect,
        consecutiveWrong: streakWrong,
      },
      turn: {
        currentQuestion,
        expectedAnswer,
        childAnswer,
        isCorrect,
      },
      recentQuestions: recentQuestions.map((q) => ({
        prompt: q.prompt,
        expectedAnswer: q.expectedAnswer,
        correct: q.correct,
      })),
    }, model, safetySettings);
    if (llm && typeof llm.spokenFeedback === "string") {
      feedback = {
        spokenFeedback: llm.spokenFeedback.slice(0, 500),
        emotion: ["happy", "neutral", "encouraging", "concerned", "celebrating"].includes(llm.emotion)
          ? llm.emotion
          : deterministic.emotion,
        shouldTakeBreak: Boolean(llm.shouldTakeBreak) || deterministic.shouldTakeBreak,
      };
    }
  } catch (err) {
    console.warn("LLM feedback failed, using deterministic fallback", err.message);
  }

  const subject = session.subject || "math";
  const topics = subjectTopicsFromChild(child || {}, subject);
  const nextQuestion =
    (await fetchMaterialQuestion(db, sessionId, session, child, subject)) ||
    generateQuestion({ subject, age: child?.age || 8, difficulty: nextDifficulty, topics });

  const mood = moodFromEmotion(feedback.emotion);
  const now = FieldValue.serverTimestamp();

  // Build the single utterance the robot will speak: the feedback on this answer
  // followed by the next question. Synthesize it to a WAV BEFORE we flip the
  // exchange to "done", so the polling device always reads a ready audioUrl.
  const spokenText = `${feedback.spokenFeedback} ${nextQuestion.prompt}`.trim();
  let audioUrl = "";
  if (synthesize) {
    audioUrl = await synthesize(spokenText, exchangeId);
  }

  const childLevelChanged = nextDifficulty !== currentDifficulty && session.childId;

  await db.runTransaction(async (tx) => {
    if (childLevelChanged) {
      tx.update(db.collection("children").doc(session.childId), {
        [`level.${subject}`]: nextDifficulty,
      });
    }

    tx.set(
      sessionRef.collection("questions").doc(exchangeId),
      {
        prompt: currentQuestion,
        expectedAnswer,
        childAnswerTranscript: childAnswer,
        correct: isCorrect,
        mood,
        difficulty: currentDifficulty,
        feedback: feedback.spokenFeedback,
        emotion: feedback.emotion,
        nextQuestion: nextQuestion.prompt,
        askedAt: exchangeData.askedAt || now,
        answeredAt: now,
      },
      { merge: true }
    );

    tx.update(exchangeRef, {
      status: "done",
      isCorrect,
      answer: feedback.spokenFeedback,
      spokenFeedback: feedback.spokenFeedback,
      audioUrl,
      emotion: feedback.emotion,
      shouldTakeBreak: feedback.shouldTakeBreak,
      nextQuestion: nextQuestion.prompt,
      expectedAnswer: nextQuestion.expectedAnswer,
      difficultyChange: nextDifficulty > currentDifficulty ? "up" : nextDifficulty < currentDifficulty ? "down" : "same",
      answeredAt: now,
    });

    tx.set(
      sessionRef,
      {
        ...buildSessionPatch({
          isCorrect,
          nextDifficulty,
          feedback,
          currentQuestion: nextQuestion,
        }),
        consecutiveCorrect: streakCorrect,
        consecutiveWrong: streakWrong,
        moodSummary: mood,
      },
      { merge: true }
    );
  });

  return {
    isCorrect,
    spokenFeedback: feedback.spokenFeedback,
    nextQuestion: nextQuestion.prompt,
    expectedAnswer: nextQuestion.expectedAnswer,
    emotion: feedback.emotion,
    shouldTakeBreak: feedback.shouldTakeBreak,
    difficulty: nextDifficulty,
  };
}

module.exports = {
  SYSTEM_PROMPT,
  createInitialQuestion,
  processLearningTurn,
};
