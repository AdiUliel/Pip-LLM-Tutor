const { FieldValue } = require("firebase-admin/firestore");
const { answerVariants, checkAnswer, clamp, generateQuestion } = require("./questionGenerator");

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
- בחר emotion שמשקף את הרגע: happy לתשובה נכונה בודדת, proud לרצף הצלחות,
  celebrating לרצף ארוך במיוחד או אבן דרך, encouraging לרמז ולטעות רגילה,
  concerned כשהילד מתקשה הרבה זמן.

החזר JSON תקין בלבד עם המפתחות הבאים בדיוק:
{
  "spokenFeedback": string,
  "emotion": "happy" | "neutral" | "encouraging" | "concerned" | "celebrating" | "proud",
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
// Pull an unused question from the CHILD's enabled uploaded material docs for
// this subject. Returns null (→ caller falls back to the deterministic
// generator) when no eligible material question remains. The parent app and
// extractQuestionsFromMaterial both write materials as items[] of
// {question, answer}; we map those to the engine's {prompt, expectedAnswer}.
// Scoped by childId (which materials carry and the session knows) — there's no
// per-child opt-in flag; an uploaded material that's toggled `enabled` IS the
// opt-in.
async function fetchMaterialQuestion(db, sessionId, session, child, subject) {
  const childId = session.childId;
  if (!childId) return null;

  const usedSnap = await db
    .collection("sessions").doc(sessionId)
    .collection("questions").get();
  const usedPrompts = new Set(usedSnap.docs.map((d) => d.data().prompt));

  const materialsSnap = await db.collection("materials")
    .where("childId", "==", childId)
    .where("subject", "==", subject)
    .where("enabled", "==", true)
    .get();

  const candidates = [];
  materialsSnap.forEach((doc) => {
    (doc.data().items || []).forEach((it) => {
      const prompt = it && it.question ? String(it.question).trim() : "";
      const answer = it && it.answer ? String(it.answer).trim() : "";
      if (prompt && answer && !usedPrompts.has(prompt)) {
        // difficulty is absent on older materials / typed Q&A → keep null so the
        // item is usable at any level (never excluded by the window below).
        const d = Number(it.difficulty);
        const difficulty = Number.isFinite(d) ? clamp(d, 1, 10) : null;
        candidates.push({ prompt, answer, topic: it.topic || subject, difficulty });
      }
    });
  });

  if (!candidates.length) return null;

  // Prefer uploaded questions near the child's current level, but NEVER starve the
  // material: try ±1, then ±2, then any uploaded question — so a small homework set
  // is still used rather than dropping through to generated questions. Items with
  // no difficulty (null) are eligible at every level.
  const level = clamp(session.currentDifficulty || 1, 1, 10);
  const near = (w) =>
    candidates.filter((c) => c.difficulty == null || Math.abs(c.difficulty - level) <= w);
  const pool = near(1).length ? near(1) : near(2).length ? near(2) : candidates;

  const q = pool[Math.floor(Math.random() * pool.length)];
  return {
    subject,
    prompt: q.prompt,
    expectedAnswer: q.answer,
    // Use the question's real topic/difficulty (feeds accuracy-by-topic analytics);
    // fall back to the session values for older, untagged material.
    topic: q.topic || subject,
    difficulty: q.difficulty ?? (session.currentDifficulty || 1),
    // Same smart grading as generated questions: normalization + Hebrew
    // number-words, so a child answering "תשע" to a material "9" still counts.
    answerVariants: answerVariants(q.answer),
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
    // Rare milestone (long streak) → celebrating; a solid streak → proud;
    // a single correct answer → happy. Keeps celebrating special.
    if (streakCorrect >= 5) {
      return {
        spokenFeedback: `וואו! רצף מדהים, כל הכבוד${suffix}! ממשיכים חזק!`,
        emotion: "celebrating",
        shouldTakeBreak: false,
      };
    }
    if (streakCorrect >= 2) {
      return {
        spokenFeedback: `מעולה! זאת תשובה נכונה. צברת רצף יפה, כל הכבוד${suffix}!`,
        emotion: "proud",
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
    case "proud":
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
      // Disable 2.5-flash "thinking": the output is a tiny fixed JSON, so thinking
      // only adds latency here (~0.5–1.3 s) with no quality gain.
      thinkingConfig: { thinkingBudget: 0 },
      safetySettings,
    },
  });
  const parsed = safeJsonParse(result.text);
  // Attach Gemini token usage so the caller can log per-turn cost telemetry.
  if (parsed && typeof parsed === "object") {
    const u = result.usageMetadata || {};
    parsed._usage = {
      inputTokens: u.promptTokenCount || 0,
      outputTokens: u.candidatesTokenCount || 0,
      totalTokens: u.totalTokenCount || 0,
    };
  }
  return parsed;
}

function buildSessionPatch({ isCorrect, nextDifficulty, feedback, currentQuestion, streakCorrect, previousLongest }) {
  return {
    status: feedback.shouldTakeBreak ? "break" : "active",
    currentDifficulty: nextDifficulty,
    currentQuestion: currentQuestion.prompt,
    currentExpectedAnswer: currentQuestion.expectedAnswer,
    currentTopic: currentQuestion.topic,
    // Remember whether the NEXT question came from uploaded material, so when it
    // is answered next turn its questions-log entry can record fromMaterial.
    currentFromMaterial: Boolean(currentQuestion.fromMaterial),
    currentAnswerVariants: currentQuestion.answerVariants || [],
    lastEmotion: feedback.emotion,
    lastFeedback: feedback.spokenFeedback,
    shouldTakeBreak: feedback.shouldTakeBreak,
    questionsAsked: FieldValue.increment(1),
    correctCount: FieldValue.increment(isCorrect ? 1 : 0),
    wrongCount: FieldValue.increment(isCorrect ? 0 : 1),
    starsEarned: FieldValue.increment(isCorrect ? 1 : 0),
    // Running best correct-answer streak (max of prior best and the current run).
    // streakCorrect is 0 on a wrong answer, so this never regresses.
    longestStreak: Math.max(Number(previousLongest || 0), Number(streakCorrect || 0)),
    lastActivity: FieldValue.serverTimestamp(),
  };
}

async function createInitialQuestion(db, sessionRef, sessionData, child) {
  const subject = sessionData.subject || "math";
  const difficulty = clamp(sessionData.currentDifficulty || levelFromChild(child || {}, subject), 1, 10);
  const topics = subjectTopicsFromChild(child || {}, subject);
  // Prefer an uploaded-material question for the very first question too; fall
  // back to the deterministic generator when the child has no material questions.
  const question =
    (await fetchMaterialQuestion(db, sessionRef.id, sessionData, child, subject)) ||
    generateQuestion({
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
      currentFromMaterial: Boolean(question.fromMaterial),
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
  const t0 = Date.now();   // turn-processing stopwatch (for exchange.processingMs)
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
  // `reason` is one of "child_request" | "declined_continue" | "timeout" — stored
  // on both the exchange and the session doc so the parent app's session summary
  // can show *why* it ended, not just that it ended. farewellText already states
  // the reason out loud (it's synthesized to speech and played on the device).
  async function endSession(farewellText, reason) {
    const audioUrl = synthesize ? await synthesize(farewellText, `${exchangeId}_${reason}`) : "";
    const now = FieldValue.serverTimestamp();
    await db.runTransaction(async (tx) => {
      tx.update(exchangeRef, {
        status: "done",
        sessionEnded: true,
        endReason: reason,
        spokenFeedback: farewellText,
        audioData: audioUrl,
        emotion: "happy",
        answeredAt: now,
      });
      tx.set(sessionRef, { status: "ended", endedAt: now, endReason: reason, lastActivity: now }, { merge: true });
    });
    return { sessionEnded: true, endReason: reason, spokenFeedback: farewellText, audioData: audioUrl };
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
        ? `בסדר, מסיימים כאן. כל הכבוד ${childName}! עבדת מצוין היום. נתראה בפעם הבאה!`
        : `בסדר, מסיימים כאן. כל הכבוד! עבדת מצוין היום. נתראה בפעם הבאה!`;
      return endSession(farewellText, "declined_continue");
    }
    // Child said "כן" — clear the flag and fall through to normal processing
    // (we'll re-generate the next question as if it were a normal correct turn).
    await sessionRef.set({ askToContinue: false }, { merge: true });
  }

  // ── Exit intent ─────────────────────────────────────────────────────────────
  if (detectExitIntent(childAnswer)) {
    const childName = child?.name || "";
    const farewellText = childName
      ? `בסדר, מסיימים כי ביקשת לעצור. כל הכבוד ${childName}! עבדת מצוין היום. נתראה בפעם הבאה!`
      : `בסדר, מסיימים כי ביקשת לעצור. כל הכבוד! עבדת מצוין היום. נתראה בפעם הבאה!`;
    return endSession(farewellText, "child_request");
  }

  // ── 50-minute session auto-end ────────────────────────────────────────────────
  // Never cut a turn mid-flight; instead flag the end so it happens after this
  // answer's feedback is played and the child hears a natural farewell.
  const sessionStartMs = session.startedAt?.toMillis?.() ?? 0;
  const sessionAgeMin = sessionStartMs > 0 ? (Date.now() - sessionStartMs) / 60000 : 0;
  // End at the parent-configured session length (clamped 5–60 min). Was a
  // hardcoded 50 that ignored the child's setting.
  const sessionCapMin = clamp(Number(child?.settings?.sessionMinutes) || 60, 5, 60);
  const shouldAutoEnd = sessionAgeMin >= sessionCapMin;

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
  let llmUsage = null;   // Gemini token counts for this turn (null when LLM skipped)
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
          emotion: ["happy", "neutral", "encouraging", "concerned", "celebrating", "proud"].includes(llm.emotion)
            ? llm.emotion
            : deterministic.emotion,
          shouldTakeBreak: Boolean(llm.shouldTakeBreak) || deterministic.shouldTakeBreak,
        };
      }
      if (llm && llm._usage) llmUsage = llm._usage;
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
        ? `עברו ${sessionCapMin} דקות, וזה הזמן שקבענו לשיעור. כל הכבוד ${childName}! עשינו שיעור ארוך ומצוין היום. נתראה בפעם הבאה!`
        : `עברו ${sessionCapMin} דקות, וזה הזמן שקבענו לשיעור. כל הכבוד! עשינו שיעור ארוך ומצוין היום. נתראה בפעם הבאה!`);
    return endSession(farewellText, "timeout");
  }

  // ── Continue-check: ask the child if they want a break, per this child's
  // configured break policy — after the first `breakFirstQuestions` questions,
  // then every `breakEveryQuestions`, OR after `breakAfterMinutes` of session,
  // whichever comes first. Parents set these in the app (children/{id}.settings).
  // Triggered AFTER the advancing answer so the child hears their feedback first.
  const settings = child?.settings || {};
  const breakFirstQ = Number(settings.breakFirstQuestions) > 0 ? Number(settings.breakFirstQuestions) : 7;
  const breakEveryQ = Number(settings.breakEveryQuestions) > 0 ? Number(settings.breakEveryQuestions) : 4;
  const breakAfterMin = Number(settings.breakAfterMinutes) > 0 ? Number(settings.breakAfterMinutes) : 15;

  const newQuestionsAsked = (session.questionsAsked || 0) + (advancesToNextQuestion ? 1 : 0);
  const byQuestionCount = newQuestionsAsked >= breakFirstQ &&
    (newQuestionsAsked - breakFirstQ) % breakEveryQ === 0;

  // Time trigger — measured since the last break prompt (or session start).
  const lastBreakMs = typeof session.lastBreakAskedAtMs === "number" ? session.lastBreakAskedAtMs : sessionStartMs;
  const minutesSinceBreak = lastBreakMs > 0 ? (Date.now() - lastBreakMs) / 60000 : 0;
  const byTime = breakAfterMin > 0 && minutesSinceBreak >= breakAfterMin;

  const shouldAskToContinue = advancesToNextQuestion && (byQuestionCount || byTime);

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
        // Append a snapshot so the app can chart level progression over time
        // ("level 2 → 4 in three weeks"). Date.now() keeps each entry unique so
        // arrayUnion always appends (serverTimestamp isn't allowed in arrays).
        levelHistory: FieldValue.arrayUnion({ subject, level: nextDifficulty, at: Date.now() }),
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
        // Topic + source of the question just answered — enables the parent
        // app's "accuracy by topic" and "material vs generated" analytics.
        topic: session.currentTopic || subject,
        fromMaterial: Boolean(session.currentFromMaterial),
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
      // Telemetry: whether the Gemini call ran (the needsLLM gate), the total
      // server-side turn time, TTS characters synthesized, and Gemini token
      // usage — powers LLM-skip-rate, live latency, and per-lesson cost stats.
      llmUsed: needsLLM,
      processingMs: Date.now() - t0,
      ttsChars: spokenText.length,
      ...(llmUsage && {
        llmInputTokens: llmUsage.inputTokens,
        llmOutputTokens: llmUsage.outputTokens,
      }),
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
          streakCorrect,
          previousLongest: session.longestStreak,
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
        // Running per-session cost/telemetry totals (for the "a lesson costs ~X"
        // stat and average turn latency).
        ttsCharsTotal: FieldValue.increment(spokenText.length),
        processingMsTotal: FieldValue.increment(Date.now() - t0),
        ...(llmUsage && {
          llmInputTokensTotal: FieldValue.increment(llmUsage.inputTokens),
          llmOutputTokensTotal: FieldValue.increment(llmUsage.outputTokens),
        }),
        // Set flag so the next turn knows to interpret the child's response as yes/no.
        askToContinue: shouldAskToContinue ? true : false,
        // When asking to continue, the "current question" stays as the continue prompt
        // so the ESP32 face strip shows something sensible, and stamp the time so the
        // next time-based break is measured from here.
        ...(shouldAskToContinue && {
          currentQuestion: continuePromptText,
          lastBreakAskedAtMs: Date.now(),
        }),
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
