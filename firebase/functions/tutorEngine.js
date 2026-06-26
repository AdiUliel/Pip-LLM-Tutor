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
- אל תזכיר בשום אופן שאתה AI, רובוט, מחשב, או תוכנה.
- אל תדון בנושאים לא מתאימים לגיל (אלימות, מבוגרים, מפחיד).

הקלט יכלול שדה \`feedbackMode\` שקובע איך להגיב:

- "celebrate" — התשובה נכונה. חגוג! ("מדהים!", "וואו!", "כל הכבוד!", "אלוף/ה!").

- "hint" — הילד טעה בפעם הראשונה בשאלה הזו. **אסור לתת את התשובה.**
  במקום זאת:
    1. עודד קלות ("כמעט!", "ניסיון יפה", "אל תוותר").
    2. תן רמז שמכוון לחשיבה (למשל: "תחשוב — כמה פחות 3 מ-13? נסה לספור אחורה: 13, 12, 11...").
    3. סיים בשאלה עדינה שמזמינה לנסות שוב ("רוצה לנסות שוב?").

- "answer" — הילד טעה פעמיים ברצף באותה שאלה. עכשיו:
    1. הכר בניסיון בחמימות ("לא נורא, השאלה הזו קצת קשה!").
    2. תן את התשובה + הסבר קצר איך מגיעים אליה (1-2 משפטים, למשל:
       "התשובה היא 10! כשמחסירים 3 מ-13, סופרים שלוש קפיצות אחורה: 13, 12, 11, 10.").
    3. סיים במשפט מעודד שמכין לשאלה הבאה.

- אם הילד נתן תשובה לא קשורה לשאלה, הפנה אותו בחזרה ("בוא נחזור לשאלה שלנו…").
- אם הילד מתסכל (הרבה טעויות ברצף), שקול שהפסקה קצרה תעזור.
- בחר emotion שמשקף את הרגע: celebrating לרצף הצלחות, happy לתשובה נכונה,
  encouraging לרמז ולטעות רגילה, concerned כשהילד מתקשה הרבה זמן.

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

  // ── Shared session-end helper ─────────────────────────────────────────────────
  async function endSession(farewellText, label) {
    const audioUrl = synthesize ? await synthesize(farewellText, `${exchangeId}_${label}`) : "";
    const now = FieldValue.serverTimestamp();
    await db.runTransaction(async (tx) => {
      tx.update(exchangeRef, {
        status: "done",
        sessionEnded: true,
        spokenFeedback: farewellText,
        audioData: audioUrl,
        emotion: "happy",
        answeredAt: now,
      });
      tx.set(sessionRef, { status: "ended", endedAt: now, lastActivity: now }, { merge: true });
    });
    return { sessionEnded: true, spokenFeedback: farewellText, audioData: audioUrl };
  }

  // ── Continue-prompt response ──────────────────────────────────────────────────
  // When the previous turn injected "רוצה להמשיך?" as the next question and set
  // askToContinue:true, the child's response arrives here. "לא" / exit phrases →
  // end session; anything else (including "כן") → clear flag and continue normally.
  if (session.askToContinue === true) {
    const wantsToStop = detectExitIntent(childAnswer) ||
      /(^|\s)לא(\s|$)/.test(childAnswer) || /\bno\b/i.test(childAnswer);
    if (wantsToStop) {
      const childName = child?.name || "";
      const farewellText = childName
        ? `כל הכבוד ${childName}! עבדת מצוין היום. נתראה בפעם הבאה!`
        : `כל הכבוד! עבדת מצוין היום. נתראה בפעם הבאה!`;
      return endSession(farewellText, "continue_no");
    }
    // Child said "כן" — clear the flag and fall through to normal processing
    // (we'll re-generate the next question as if it were a normal correct turn).
    await sessionRef.set({ askToContinue: false }, { merge: true });
  }

  // ── Exit intent ─────────────────────────────────────────────────────────────
  if (detectExitIntent(childAnswer)) {
    const childName = child?.name || "";
    const farewellText = childName
      ? `כל הכבוד ${childName}! עבדת מצוין היום. נתראה בפעם הבאה!`
      : `כל הכבוד! עבדת מצוין היום. נתראה בפעם הבאה!`;
    return endSession(farewellText, "farewell");
  }

  // ── 50-minute session auto-end ────────────────────────────────────────────────
  // Never cut a turn mid-flight; instead flag the end so it happens after this
  // answer's feedback is played and the child hears a natural farewell.
  const sessionStartMs = session.startedAt?.toMillis?.() ?? 0;
  const sessionAgeMin = sessionStartMs > 0 ? (Date.now() - sessionStartMs) / 60000 : 0;
  const shouldAutoEnd = sessionAgeMin >= 50;

  const isCorrect = checkAnswer(expectedAnswer, childAnswer);
  const previousWrong = Number(session.consecutiveWrong || 0);
  const previousCorrect = Number(session.consecutiveCorrect || 0);
  const streakWrong = isCorrect ? 0 : previousWrong + 1;
  const streakCorrect = isCorrect ? previousCorrect + 1 : 0;
  const currentDifficulty = clamp(session.currentDifficulty || 1, 1, 10);
  const nextDifficulty = computeNextDifficulty(currentDifficulty, isCorrect, streakWrong, streakCorrect);

  // ── Hint-then-answer logic ────────────────────────────────────────────────────
  // Track wrong attempts on the CURRENT question (separate from the
  // session-wide consecutiveWrong, which can span multiple questions).
  // First wrong  → give a hint, keep the same question for one more try.
  // Second wrong → give the answer + 1-sentence explanation, then advance.
  // Correct      → reset, advance.
  const wrongOnCurrent = Number(session.wrongAttemptsOnCurrent || 0);
  const isHintMode   = !isCorrect && wrongOnCurrent === 0;
  const isAnswerMode = !isCorrect && wrongOnCurrent >= 1;
  const feedbackMode = isCorrect ? "celebrate" : (isHintMode ? "hint" : "answer");
  const nextWrongOnCurrent = isHintMode ? 1 : 0;   // reset on correct OR after answer reveal
  const advancesToNextQuestion = !isHintMode;

  const deterministic = deterministicFeedback({
    child,
    isCorrect,
    expectedAnswer,
    childAnswer,
    streakWrong,
    streakCorrect,
  });

  // Skip LLM when deterministic feedback is sufficient:
  //   • Simple correct answer (no streak) → "כל הכבוד" is enough, no LLM needed
  //   • Streak ≥3 correct → celebrate mode, deterministic already handles it well
  // LLM IS called when it adds real value:
  //   • First wrong attempt (hint mode) — needs a personalised, question-specific hint
  //   • Second wrong attempt (answer reveal) — needs a clear explanation
  //   • Child is struggling (streakWrong ≥ 2) — needs an encouraging, adaptive response
  const needsLLM = !isCorrect || streakCorrect >= 3;

  const subject = session.subject || "math";
  const topics = subjectTopicsFromChild(child || {}, subject);

  // Run LLM feedback and next-question fetch IN PARALLEL — they are independent.
  // LLM is only called when it adds real value (needsLLM gate above).
  const nextQuestionPromise = advancesToNextQuestion
    ? fetchMaterialQuestion(db, sessionId, session, child, subject)
        .then((mat) => mat || generateQuestion({ subject, age: child?.age || 8, difficulty: nextDifficulty, topics }))
    : Promise.resolve({
        subject,
        prompt: currentQuestion,
        expectedAnswer,
        topic: session.currentTopic || subject,
        difficulty: currentDifficulty,
        answerVariants: session.currentAnswerVariants || [],
      });

  let feedback = deterministic;
  if (needsLLM) {
    const llmPromise = readRecentQuestions(db, sessionId, 5).then((recentQuestions) =>
      llmFeedback(ai, {
        child: {
          age: child?.age || 8,
          gender: child?.gender || "girl",
          name: child?.name || "",
        },
        session: {
          subject,
          currentDifficulty,
          nextDifficulty,
          consecutiveCorrect: streakCorrect,
          consecutiveWrong: streakWrong,
        },
        turn: { currentQuestion, expectedAnswer, childAnswer, isCorrect },
        feedbackMode,
        wrongAttemptsOnCurrent: wrongOnCurrent,
        recentQuestions: recentQuestions.map((q) => ({
          prompt: q.prompt,
          expectedAnswer: q.expectedAnswer,
          correct: q.correct,
        })),
      }, model, safetySettings)
    );

    try {
      const llm = await llmPromise;
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
  } else {
    console.log("[LLM] skipped — simple correct answer, deterministic feedback used");
  }

  const nextQuestion = await nextQuestionPromise;

  const mood = moodFromEmotion(feedback.emotion);
  const now = FieldValue.serverTimestamp();

  // ── 50-min auto-end: replace next question with farewell ─────────────────────
  if (shouldAutoEnd && advancesToNextQuestion) {
    const childName = child?.name || "";
    const farewellText = feedback.spokenFeedback.trim() + " " +
      (childName
        ? `כל הכבוד ${childName}! עשינו שיעור ארוך ומצוין היום. נתראה בפעם הבאה!`
        : `כל הכבוד! עשינו שיעור ארוך ומצוין היום. נתראה בפעם הבאה!`);
    return endSession(farewellText, "auto_end");
  }

  // ── Continue-check every 4 questions starting from Q7 ────────────────────────
  // Trigger AFTER the Nth advancing answer so the child always hears the feedback
  // for their last answer before being asked to continue.
  const newQuestionsAsked = (session.questionsAsked || 0) + (advancesToNextQuestion ? 1 : 0);
  const shouldAskToContinue = advancesToNextQuestion &&
    newQuestionsAsked >= 7 &&
    (newQuestionsAsked - 7) % 4 === 0;

  // Build the spoken utterance. In hint mode the feedback already contains
  // a "try again?" prompt — don't append the question again to avoid the
  // robot saying it twice in a row.
  const continuePromptText = "רוצה להמשיך לתרגל עוד קצת? תגיד כן או לא.";
  const spokenText = isHintMode
    ? feedback.spokenFeedback.trim()
    : shouldAskToContinue
      ? `${feedback.spokenFeedback} ${continuePromptText}`.trim()
      : `${feedback.spokenFeedback} ${nextQuestion.prompt}`.trim();
  let audioUrl = "";
  if (synthesize) {
    audioUrl = await synthesize(spokenText, exchangeId);
  }

  // Only adjust the child's stored level when we actually moved on from
  // this question — a hint-pause shouldn't trigger a level shuffle.
  const childLevelChanged = advancesToNextQuestion && nextDifficulty !== currentDifficulty && session.childId;

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
      audioData: audioUrl,
      emotion: feedback.emotion,
      shouldTakeBreak: feedback.shouldTakeBreak,
      nextQuestion: nextQuestion.prompt,
      expectedAnswer: nextQuestion.expectedAnswer,
      difficultyChange: nextDifficulty > currentDifficulty ? "up" : nextDifficulty < currentDifficulty ? "down" : "same",
      answeredAt: now,
    });

    // In hint mode keep questionsAsked / *Count the same (we're not done
    // with this question yet) — buildSessionPatch increments those, so
    // pick a slimmer patch instead.
    const sessionPatch = advancesToNextQuestion
      ? buildSessionPatch({
          isCorrect,
          nextDifficulty,
          feedback,
          currentQuestion: nextQuestion,
        })
      : {
          status: feedback.shouldTakeBreak ? "break" : "active",
          lastEmotion: feedback.emotion,
          lastFeedback: feedback.spokenFeedback,
          shouldTakeBreak: feedback.shouldTakeBreak,
          lastActivity: FieldValue.serverTimestamp(),
        };

    tx.set(
      sessionRef,
      {
        ...sessionPatch,
        consecutiveCorrect: streakCorrect,
        consecutiveWrong: streakWrong,
        wrongAttemptsOnCurrent: nextWrongOnCurrent,
        moodSummary: mood,
        // Set flag so the next turn knows to interpret the child's response as yes/no.
        askToContinue: shouldAskToContinue ? true : false,
        // When asking to continue, the "current question" stays as the continue prompt
        // so the ESP32 face strip shows something sensible.
        ...(shouldAskToContinue && { currentQuestion: continuePromptText }),
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
