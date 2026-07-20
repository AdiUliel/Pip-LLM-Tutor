const { FieldValue } = require("firebase-admin/firestore");
const { answerVariants, checkAnswer, clamp, generateQuestion } = require("./questionGenerator");

// Default model — kept in sync with index.js. index.js always passes an
// explicit model, so this fallback only matters if llmFeedback is called
// directly; keep it aligned with index.js (flash-lite for latency).
const DEFAULT_MODEL = process.env.GEMINI_MODEL || "gemini-2.5-flash-lite";

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
- מין הילד נתון בשדה \`gender\` (\"boy\"=זכר, \"girl\"=נקבה). התאם תמיד את הפנייה למין הילד — פעלים, שמות תואר וכינויי גוף בלשון זכר לבן ובלשון נקבה לבת (למשל \"נסה/נסי\", \"מוכן/מוכנה\", \"כל הכבוד, אלוף/אלופה\"). לעולם אל תשתמש בלשון זכר כשמדובר בבת.
- **חשוב מאוד:** הדוגמאות בהוראות שבהמשך כתובות בלשון זכר כברירת מחדל (\"תחשוב\", \"נסה\", \"רוצה לנסות\") — כשמדובר בבת המר אותן ללשון נקבה (\"תחשבי\", \"נסי\", \"את רוצה לנסות\") והוסף את כינוי הגוף \"את\" כשצריך, כך שגם הקול (TTS) יישמע בלשון נקבה. אל תעתיק את הדוגמאות מילה-במילה — התאם אותן למין הילד.
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
  // "סיים" (final ם) covers לסיים / נסיים / "בוא נסיים"; "סיימ" (regular מ)
  // covers the mid-word forms סיימתי / מסיימים. "עצור" covers עצור / לעצור.
  "סיים", "סיימ", "רוצה לסיים", "נגמרנו", "נגמר", "סיום", "להפסיק", "עצור",
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

  // Questions already asked in THIS session (+ the one being answered right now,
  // so we never immediately re-pick it as the next question).
  const usedSnap = await db
    .collection("sessions").doc(sessionId)
    .collection("questions").get();
  const usedInSession = new Set(usedSnap.docs.map((d) => d.data().prompt));
  if (session.currentQuestion) usedInSession.add(session.currentQuestion);

  // Questions asked in PAST sessions too — the per-child material history, so the
  // same uploaded question isn't repeated across sessions until the file is done.
  const usedHistory = new Set(
    (child && child.usedMaterialPrompts && child.usedMaterialPrompts[subject]) || []
  );

  const materialsSnap = await db.collection("materials")
    .where("childId", "==", childId)
    .where("subject", "==", subject)
    .where("enabled", "==", true)
    .get();

  // Every usable material item (difficulty null ⇒ usable at any level).
  const all = [];
  materialsSnap.forEach((doc) => {
    (doc.data().items || []).forEach((it) => {
      const prompt = it && it.question ? String(it.question).trim() : "";
      const answer = it && it.answer ? String(it.answer).trim() : "";
      if (prompt && answer) {
        const d = Number(it.difficulty);
        const difficulty = Number.isFinite(d) ? clamp(d, 1, 10) : null;
        all.push({ prompt, answer, topic: it.topic || subject, difficulty });
      }
    });
  });
  if (!all.length) return null;

  // Exclude anything already asked (this session AND past sessions). Once every
  // uploaded question has been asked there are no candidates → return null, so the
  // caller falls back to GENERATED questions. We do NOT recycle the file; if the
  // parent later uploads MORE material, those new questions aren't in the history
  // yet and get asked before we fall back to generated again.
  const candidates = all.filter((c) => !usedInSession.has(c.prompt) && !usedHistory.has(c.prompt));
  if (!candidates.length) return null;

  // Prefer uploaded questions near the child's current level, but NEVER starve the
  // material: try ±1, then ±2, then any candidate.
  const level = clamp(session.currentDifficulty || 1, 1, 10);
  const near = (w) =>
    candidates.filter((c) => c.difficulty == null || Math.abs(c.difficulty - level) <= w);
  const pool = near(1).length ? near(1) : near(2).length ? near(2) : candidates;

  const q = pool[Math.floor(Math.random() * pool.length)];

  // Record this question in the per-child history so it's never asked again;
  // once the whole file is exhausted the child moves to generated questions.
  // Best-effort — a failed history write must not break the turn.
  await db.collection("children").doc(childId).update({
    [`usedMaterialPrompts.${subject}`]: FieldValue.arrayUnion(q.prompt),
  }).catch((e) => console.warn("[material] history write failed:", e.message));

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

function deterministicFeedback({ child, isCorrect, expectedAnswer, childAnswer, streakWrong, streakCorrect }) {
  // Gender-correct praise word: "אלוף" (boy) / "אלופה" (girl). The old code
  // appended genderSuffix ("י") to "כל הכבוד", producing the non-word
  // "כל הכבודי" for girls — "כל הכבוד" is invariant, so gender a real word instead.
  const champ = (child && child.gender === "boy") ? "אלוף" : "אלופה";
  if (isCorrect) {
    // Rare milestone (long streak) → celebrating; a solid streak → proud;
    // a single correct answer → happy. Keeps celebrating special.
    if (streakCorrect >= 5) {
      return {
        spokenFeedback: `וואו! רצף מדהים, כל הכבוד, ${champ}! ממשיכים חזק!`,
        emotion: "celebrating",
        shouldTakeBreak: false,
      };
    }
    if (streakCorrect >= 2) {
      return {
        spokenFeedback: `מעולה! זאת תשובה נכונה. צברת רצף יפה, כל הכבוד, ${champ}!`,
        emotion: "proud",
        shouldTakeBreak: false,
      };
    }
    return {
      spokenFeedback: `נכון מאוד! התשובה היא ${expectedAnswer}. כל הכבוד, ${champ}!`,
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
      // turnSeq identifies the CURRENT question. The device echoes it when
      // answering so a stale answer (lost response → device still on the old
      // question) is detected and resynced instead of graded against the new one.
      turnSeq: 1,
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
  deviceSeq,
}) {
  const sessionRef = db.collection("sessions").doc(sessionId);
  const exchangeRef = sessionRef.collection("exchanges").doc(exchangeId);
  const sessionSnap = await sessionRef.get();
  if (!sessionSnap.exists) throw new Error(`Session ${sessionId} does not exist`);

  const session = sessionSnap.data();

  // ── Stale-answer guard ────────────────────────────────────────────────────
  // If the device echoes a turnSeq that no longer matches the session's, its last
  // response was lost and it's still on the OLD question. Don't grade this answer
  // against the (already advanced) current question — re-send the current question
  // so the device resyncs. Skipped when either side has no seq (older firmware).
  {
    const sessionSeq = Number(session.turnSeq);
    const clientSeq = Number(deviceSeq);
    if (Number.isFinite(clientSeq) && clientSeq > 0 &&
        Number.isFinite(sessionSeq) && sessionSeq > 0 &&
        clientSeq !== sessionSeq) {
      console.log(`[turn] stale seq ${clientSeq} != ${sessionSeq} — resyncing device`);
      const q = session.currentQuestion || "";
      const audioUrl = synthesize ? await synthesize(q, `${exchangeId}_resync`) : "";
      await exchangeRef.set(
        { status: "resynced", staleSeq: clientSeq, answeredAt: FieldValue.serverTimestamp() },
        { merge: true }
      );
      return {
        isCorrect: false,
        spokenFeedback: "",
        nextQuestion: q,
        expectedAnswer: session.currentExpectedAnswer || "",
        emotion: "encouraging",
        shouldTakeBreak: false,
        difficulty: session.currentDifficulty || 1,
        turnSeq: sessionSeq,
        resynced: true,
        audioData: audioUrl,
      };
    }
  }

  const t0 = Date.now();   // turn-processing stopwatch (for exchange.processingMs)
  const child = await readChild(db, session.childId);
  // Gender-correct 2nd-person verb for the fixed (non-LLM) prompts: masc "רוצה
  // לנסות/להמשיך", fem "רוצָה" needs the pronoun to force the feminine reading,
  // so we say "את" + verb for girls. boy → "", girl → "את ".
  const youWant = (child && child.gender === "boy") ? "רוצה" : "את רוצה";
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
  // askToContinue:true, the child's response arrives here. Three-way:
  //   explicit NO / exit intent → end the session;
  //   explicit YES             → clear the flag, continue normally;
  //   anything else            → RE-ASK and KEEP the flag.
  // The old code treated any non-"לא" as yes and CLEARED the flag — so a
  // mis-heard reply ("בוא נסיים") silently armed normal grading, and the
  // child's follow-up "לא" was graded as a (wrong) answer instead of ending.
  if (session.askToContinue === true) {
    const t = String(childAnswer || "").toLowerCase().replace(/[.,!?;:]/g, " ").trim();
    const saidNo = detectExitIntent(t) ||
      /(^|\s)לא(\s|$)/.test(t) || /(^|\s)no(\s|$)/.test(t);
    const saidYes =
      /(^|\s)(כן|בטח|כמובן|יאללה|אוקיי|אוקי|טוב|בסדר|עוד|ממשיכים|להמשיך|נמשיך|yes|ok|okay|sure)(\s|$)/.test(t);
    if (saidNo) {
      const childName = child?.name || "";
      const farewellText = childName
        ? `בסדר, מסיימים כאן. כל הכבוד ${childName}! עבדת מצוין היום. נתראה בפעם הבאה!`
        : `בסדר, מסיימים כאן. כל הכבוד! עבדת מצוין היום. נתראה בפעם הבאה!`;
      return endSession(farewellText, "declined_continue");
    }
    if (!saidYes) {
      // Unclear reply — repeat the yes/no question, flag stays armed so the
      // next "לא" actually ends the session. turnSeq unchanged (no advance).
      const reSayYesNo = (child && child.gender === "boy") ? "תגיד" : "תגידי";
      const q = `לא הבנתי. ${youWant} להמשיך לתרגל עוד קצת? ${reSayYesNo} כן או לא.`;
      const audioUrl = synthesize ? await synthesize(q, `${exchangeId}_reask`) : "";
      await exchangeRef.set(
        { status: "done", spokenFeedback: q, audioData: audioUrl, emotion: "encouraging", answeredAt: FieldValue.serverTimestamp() },
        { merge: true }
      );
      return {
        isCorrect: false,
        spokenFeedback: q,
        nextQuestion: session.currentQuestion || q,
        expectedAnswer: session.currentExpectedAnswer || "",
        emotion: "encouraging",
        shouldTakeBreak: false,
        difficulty: session.currentDifficulty || 1,
        turnSeq: Number(session.turnSeq) || 1,
        audioData: audioUrl,
      };
    }
    // Explicit yes — serve the PARKED next question. Never fall through to
    // grading: the old fall-through graded "כן" against the pending question's
    // answer ("purple") → "wrong, want to try again?" nonsense.
    const pendingPrompt = String(session.pendingQuestionPrompt || "").trim() ||
      String(session.currentQuestion || "").trim();
    const resume = `יופי, ממשיכים! ${pendingPrompt}`;
    const resumeAudio = synthesize ? await synthesize(resume, `${exchangeId}_resume`) : "";
    await Promise.all([
      sessionRef.set(
        {
          askToContinue: false,
          currentQuestion: pendingPrompt,           // restore the real question
          pendingQuestionPrompt: FieldValue.delete(),
          lastActivity: FieldValue.serverTimestamp(),
        },
        { merge: true }
      ),
      exchangeRef.set(
        { status: "done", spokenFeedback: resume, audioData: resumeAudio, emotion: "happy", answeredAt: FieldValue.serverTimestamp() },
        { merge: true }
      ),
    ]);
    return {
      isCorrect: true,
      spokenFeedback: resume,
      nextQuestion: pendingPrompt,
      expectedAnswer: session.currentExpectedAnswer || "",
      emotion: "happy",
      shouldTakeBreak: false,
      difficulty: session.currentDifficulty || 1,
      turnSeq: Number(session.turnSeq) || 1,        // unchanged — same pending question
      audioData: resumeAudio,
    };
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
  // New sequence for the question the device will hold after this turn: +1 when we
  // advance to a new question, unchanged on a hint (same question).
  const newTurnSeq = (Number(session.turnSeq) || 1) + (advancesToNextQuestion ? 1 : 0);

  // ── Session-cap auto-end: replace next question with farewell ────────────────
  if (shouldAutoEnd && advancesToNextQuestion) {
    // Record the question that was JUST answered before ending — endSession
    // returns before the normal transaction below, so without this the final
    // turn vanished from the report: the child's last answer ("green") wasn't
    // counted or listed, and questionsAsked came up short ("0/1 of 2").
    await Promise.all([
      sessionRef.set({
        questionsAsked: FieldValue.increment(1),
        correctCount: FieldValue.increment(isCorrect ? 1 : 0),
        wrongCount: FieldValue.increment(isCorrect ? 0 : 1),
        starsEarned: FieldValue.increment(isCorrect ? 1 : 0),
        longestStreak: Math.max(Number(session.longestStreak || 0), Number(streakCorrect || 0)),
      }, { merge: true }),
      sessionRef.collection("questions").doc(exchangeId).set({
        prompt: currentQuestion,
        expectedAnswer,
        childAnswerTranscript: childAnswer,
        correct: isCorrect,
        mood,
        difficulty: currentDifficulty,
        topic: session.currentTopic || subject,
        fromMaterial: Boolean(session.currentFromMaterial),
        feedback: feedback.spokenFeedback,
        emotion: feedback.emotion,
        askedAt: exchangeData.askedAt || now,
        answeredAt: now,
      }, { merge: true }),
    ]);
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
  const sayYesNo = (child && child.gender === "boy") ? "תגיד" : "תגידי";
  const continuePromptText = `${youWant} להמשיך לתרגל עוד קצת? ${sayYesNo} כן או לא.`;
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
        // Advance turnSeq only when we move to a new question (a hint keeps the
        // same question and the same seq). Matches the returned turnSeq below.
        ...(advancesToNextQuestion && { turnSeq: newTurnSeq }),
        // Stamp the start of actual learning on the FIRST turn — the app measures
        // session duration from here (excludes boot / WiFi / identify overhead),
        // not from session creation. Set once; never overwritten.
        ...(session.learningStartedAt == null && { learningStartedAt: now }),
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
          // Park the NEXT question's prompt so the yes-handler can serve it —
          // currentQuestion was just overwritten with the continue prompt, and
          // without this the pending question's text was simply LOST (the
          // yes-path then graded "כן" against its answer → nonsense wrongs).
          pendingQuestionPrompt: nextQuestion.prompt,
          lastBreakAskedAtMs: Date.now(),
        }),
      },
      { merge: true }
    );
  });

  return {
    isCorrect,
    spokenFeedback: feedback.spokenFeedback,
    // On a continue-prompt turn the device must DISPLAY the same thing the
    // audio asks ("רוצה להמשיך?") — not the pending next question, which made
    // the strip show a color question while the voice asked yes/no.
    nextQuestion: shouldAskToContinue ? continuePromptText : nextQuestion.prompt,
    expectedAnswer: nextQuestion.expectedAnswer,
    emotion: feedback.emotion,
    shouldTakeBreak: feedback.shouldTakeBreak,
    difficulty: nextDifficulty,
    turnSeq: newTurnSeq,
    llmUsed: needsLLM,   // for processTurn's [lat] breakdown log
  };
}

module.exports = {
  SYSTEM_PROMPT,
  createInitialQuestion,
  processLearningTurn,
};
