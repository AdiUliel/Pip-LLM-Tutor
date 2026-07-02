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
  "shouldTakeBreak": false,
  "audioUrl": "https://storage.googleapis.com/<bucket>/tts/<exchangeId>.wav"
}
```

`audioUrl` is a 16 kHz LINEAR16 WAV containing **spokenFeedback + nextQuestion**
spoken together, synthesized by the function *before* it flips `status` to
`done` (so the polling device never reads a done doc with an empty URL).

The device should then:

1. Display the pip_face emotion from `emotion`.
2. Play `audioUrl` (feedback then the next question).
3. Treat `nextQuestion` as the new current question.
4. Wait for the next child answer.

### First question

The device does **not** speak the very first question from an exchange — it is
seeded on the session itself by the `onSessionCreated` trigger:

```text
sessions/{sessionId}
  status: "active"
  currentQuestion: "כמה זה 4 ועוד 5?"
  currentExpectedAnswer: "9"
  currentQuestionAudioUrl: "https://storage.googleapis.com/<bucket>/tts/<sessionId>_q0.wav"
```

### Live device status for the app

The device also writes `deviceState/{deviceId}` (deviceId == its anon UID) on
every state change and every ~10s as a heartbeat, so the Flutter device monitor
shows it live:

```text
deviceState/{deviceId}
  status: "idle" | "asking" | "listening" | "feedback" | "break" | "error"
  lastHeartbeat: <server/NTP timestamp>   // freshness drives "online"
  currentQuestion: string
  activeSubject: "math" | "english"
```

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

## Region

All Cloud Functions deploy to **europe-west10**, set once at the top of
`index.js`:

```js
const { setGlobalOptions } = require("firebase-functions/v2");
setGlobalOptions({ region: "europe-west10" });
```

This covers `answerQuestion`, `onSessionCreated`, `startLearningSession`,
`submitChildAnswer` and `transcribeAudio`. The ESP32 builds the STT URL from
`CLOUD_FUNCTIONS_REGION` in `secrets.h` (already `europe-west10`), and the
Flutter app uses `AppConstants.functionsRegion`.

## Gemini / Vertex AI notes

The code uses `@google/genai` with Vertex AI mode:

```js
new GoogleGenAI({
  vertexai: true,
  project: process.env.GCLOUD_PROJECT,
  location: process.env.GEMINI_LOCATION || "global"
});
```

The function uses the Cloud Function service account instead of a public API key.
Make sure Vertex AI / Gemini is enabled in the Firebase Google Cloud project.

We use the Vertex **`global`** endpoint for Gemini because model availability is
not uniform across regions (Berlin/europe-west10 does not serve every Gemini
model). The *functions* still run in europe-west10; only the model endpoint is
global. To pin a specific region instead, set `GEMINI_LOCATION`.

The previous model `gemini-2.0-flash-001` was discontinued (404). Default is now
`gemini-2.5-flash`.

Optional environment variables:

```text
GEMINI_MODEL=gemini-2.5-flash
GEMINI_LOCATION=global
FUNCTIONS_REGION=europe-west10
```

## Why the LLM is behind Cloud Functions

- Keeps API keys / service account credentials away from the device.
- Lets the app and device share the same tutor logic.
- Keeps session history and reports consistent.
- Allows fallback logic if Gemini fails.
- Makes it easier to add safety filters and parent controls later.
