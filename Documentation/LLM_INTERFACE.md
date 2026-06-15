# LLM Interface — Emotional Tutor

This project now includes a real LLM integration layer for the emotional tutor flow.
The main LLM interface is implemented in Firebase Cloud Functions so the ESP32 / Raspberry Pi device does **not** hold an API key and does **not** call Gemini directly.

## High-level flow

```text
Child speaks
  ↓
Device performs / receives Speech-to-Text transcript
  ↓
Device writes child answer to Firestore
  ↓
Cloud Function reads the session state
  ↓
Cloud Function checks correctness + calls Gemini for emotional feedback
  ↓
Cloud Function generates next math/English question
  ↓
Device reads Firestore and speaks/displays the response
```

## Main Firestore collections

```text
children/{childId}
  name
  age
  gender
  subjectsEnabled
  topicFocus
  level
  deviceId

sessions/{sessionId}
  childId
  deviceId
  subject: "math" | "english"
  status: "starting" | "active" | "break" | "ended"
  currentDifficulty
  currentQuestion
  currentExpectedAnswer
  currentTopic
  currentAnswerVariants
  lastFeedback
  lastEmotion
  shouldTakeBreak
  questionsAsked
  correctCount
  wrongCount
  starsEarned
  consecutiveCorrect
  consecutiveWrong

sessions/{sessionId}/exchanges/{exchangeId}
  type: "learning_turn"
  status: "pending" | "processing" | "done" | "error"
  childAnswer
  spokenFeedback
  emotion
  nextQuestion
  expectedAnswer
  isCorrect
  shouldTakeBreak

sessions/{sessionId}/questions/{questionId}
  prompt
  expectedAnswer
  childAnswerTranscript
  correct
  mood
  difficulty
  feedback
  emotion
```

## ESP32 / device write format

When the child answers the current question, write a new document:

```json
{
  "type": "learning_turn",
  "status": "pending",
  "childAnswer": "תשע",
  "askedAt": "server timestamp"
}
```

Path:

```text
sessions/{sessionId}/exchanges/{exchangeId}
```

The Cloud Function will update the same document with:

```json
{
  "status": "done",
  "isCorrect": true,
  "spokenFeedback": "נכון מאוד! כל הכבוד!",
  "emotion": "happy",
  "nextQuestion": "כמה זה 6 ועוד 3?",
  "expectedAnswer": "9",
  "shouldTakeBreak": false
}
```

The device should then:

1. Speak `spokenFeedback`.
2. Display face according to `emotion`.
3. Speak/display `nextQuestion`.
4. Wait for the next child answer.

## Callable Functions for testing / Flutter

### `startLearningSession`

Input:

```json
{
  "childId": "CHILD_ID",
  "deviceId": "DEVICE_ID",
  "subject": "math"
}
```

Output:

```json
{
  "sessionId": "...",
  "currentQuestion": "כמה זה 4 ועוד 5?",
  "expectedAnswer": "9",
  "difficulty": 1
}
```

### `submitChildAnswer`

Input:

```json
{
  "sessionId": "SESSION_ID",
  "childAnswer": "תשע"
}
```

Output:

```json
{
  "exchangeId": "...",
  "status": "pending"
}
```

The Firestore trigger `answerQuestion` processes this pending exchange.

## Files added / changed

```text
firebase/functions/index.js
firebase/functions/tutorEngine.js
firebase/functions/questionGenerator.js
firebase/firestore.rules
flutter_app/firestore.rules
Documentation/LLM_INTERFACE.md
```

## Deploy

From the `firebase/` folder:

```bash
cd firebase/functions
npm install
cd ..
firebase deploy --only functions,firestore:rules
```

## Gemini / Vertex AI notes

The code uses `@google/genai` with Vertex AI mode:

```js
new GoogleGenAI({
  vertexai: true,
  project: process.env.GCLOUD_PROJECT,
  location: process.env.GEMINI_LOCATION || "us-central1"
});
```

That means the function uses the Cloud Function service account instead of a public API key.
Make sure Vertex AI / Gemini is enabled in the Firebase Google Cloud project.

Optional environment variables:

```text
GEMINI_MODEL=gemini-2.0-flash-001
GEMINI_LOCATION=us-central1
```

## Why the LLM is behind Cloud Functions

- Keeps API keys / service account credentials away from the device.
- Lets the app and device share the same tutor logic.
- Keeps session history and reports consistent.
- Allows fallback logic if Gemini fails.
- Makes it easier to add safety filters and parent controls later.
