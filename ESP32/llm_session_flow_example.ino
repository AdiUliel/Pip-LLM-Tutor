/*
  LLM session flow example for the Emotional Tutor device.

  This is intentionally a small integration example, not a full firmware file.
  The real device code should:
    1. Create / reuse a session document.
    2. Read sessions/{sessionId}.currentQuestion.
    3. Speak the question.
    4. Convert child speech to text.
    5. Write a pending exchange with childAnswer.
    6. Poll the exchange until status == "done".
    7. Speak spokenFeedback and nextQuestion.

  Firestore write shape:
    sessions/{sessionId}/exchanges/{exchangeId}
    {
      "type": "learning_turn",
      "status": "pending",
      "childAnswer": "תשע",
      "askedAt": serverTimestamp
    }
*/

void startLearningLoopExample() {
  // PSEUDO CODE ONLY — adapt to the Firebase library used in your firmware.

  // String sessionId = createSession(childId, deviceId, "math");
  // String question = readString("sessions/" + sessionId + "/currentQuestion");
  // speak(question);

  // while (true) {
  //   String transcript = speechToText();
  //   String exchangeId = createPendingExchange(sessionId, transcript);
  //
  //   while (true) {
  //     ExchangeResult result = readExchange(sessionId, exchangeId);
  //     if (result.status == "done") {
  //       setRobotFace(result.emotion);
  //       speak(result.spokenFeedback);
  //       if (result.shouldTakeBreak) {
  //         speak("בוא ניקח הפסקה קצרה ונחזור עוד מעט.");
  //         break;
  //       }
  //       speak(result.nextQuestion);
  //       break;
  //     }
  //     if (result.status == "error") {
  //       speak("יש לי תקלה קטנה, ננסה שוב עוד רגע.");
  //       break;
  //     }
  //     delay(700);
  //   }
  // }
}
