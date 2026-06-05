# Firebase Setup Guide

This guide gets your Firebase project wired up for the ESP32 homework assistant.

---

## Architecture Overview

```
ESP32 (mic + speaker)
  │
  ├─► Google Speech-to-Text  →  transcribed text (question)
  │
  └─► Firestore: POST /sessions/{id}/exchanges/{id}
                  { question, status:"pending", answer:null }
                        │
                        ▼
              Cloud Function (answerQuestion)
                        │
                        ├─► reads conversation history from Firestore
                        ├─► calls LLM API (Gemini / Claude / GPT)
                        └─► writes { answer, status:"done" } back
                                    │
                        ┌───────────┴────────────┐
                        ▼                        ▼
                  ESP32 polls              Flutter app
                  → reads answer           listens in real-time
                  → speaks it aloud        → shows chat history
```

---

## Step 1: Firebase Console Setup

Go to [console.firebase.google.com](https://console.firebase.google.com) and open your project.

### 1a. Enable Firestore -DONE
- Left sidebar → **Build → Firestore Database**
- Click **Create database**
- Choose **Production mode** (we'll deploy real rules)
- Pick a region close to you (e.g., `europe-west1` for Israel)

### 1b. Enable Authentication -DONE
- Left sidebar → **Build → Authentication**
- Click **Get started**
- Enable **Anonymous** sign-in (ESP32 will use this)
- Optionally enable **Email/Password** for the Flutter parent dashboard

### 1c. Enable Cloud Functions -DONE
- Left sidebar → **Build → Functions**
- Click **Get started** and follow the prompt
- ⚠️ Cloud Functions requires the **Blaze (pay-as-you-go)** plan.
  At homework-assistant scale the cost is essentially $0, but a billing account is required.

---

## Step 2: Install Firebase CLI -done

On your computer, run:

```bash
npm install -g firebase-tools
firebase login
```

---

## Step 3: Set Your Project ID -done

Edit `firebase/.firebaserc` and replace `YOUR_FIREBASE_PROJECT_ID` with your actual project ID.
You can find it in the Firebase console → Project Settings → General → Project ID.

```json
{
  "projects": {
    "default": "your-actual-project-id"
  }
}
```

---

## Step 4: Store Your LLM API Key as a Secret

Never hardcode API keys. Firebase Secrets Manager stores them securely.

```bash
cd firebase/
firebase functions:secrets:set LLM_API_KEY
# Paste your key when prompted (Gemini / OpenAI / Claude key)
```

---

## Step 5: Choose Your LLM Provider

Open `firebase/functions/index.js` and set the provider on line 28:

```js
const LLM_PROVIDER = "gemini"; // "gemini" | "openai" | "claude"
```

| Provider | Free tier | Quality | Notes |
|----------|-----------|---------|-------|
| **Gemini** | Yes (Gemini Flash) | Great | Best fit — same Google Cloud account |
| **Claude Haiku** | No (cheap ~$0.001/call) | Excellent for tutoring | Strong reasoning |
| **GPT-4o-mini** | No (cheap) | Good | Familiar API |

**Recommendation for this project:** Start with Gemini Flash — it has a generous free tier and integrates natively with Google Cloud.
Get a Gemini API key at [aistudio.google.com](https://aistudio.google.com).

---

## Step 6: Deploy to Firebase

```bash
cd firebase/

# Install function dependencies
npm install --prefix functions

# Deploy Firestore rules + indexes + Cloud Functions
firebase deploy
```

You should see output like:
```
✔  functions[answerQuestion]: Successful create operation.
✔  firestore: rules file firestore.rules compiled successfully
✔  Deploy complete!
```

---

## Step 7: Firestore Data Model

Your database will have this structure:

```
/devices/{deviceId}
    status:          "idle" | "listening" | "processing"
    lastSeen:        Timestamp
    name:            "Maor's ESP32"

/sessions/{sessionId}
    deviceId:        string  ← links to a device
    createdAt:       Timestamp
    lastActivity:    Timestamp

/sessions/{sessionId}/exchanges/{exchangeId}
    question:        "What is 7 times 8?"     ← written by ESP32
    answer:          "7 × 8 = 56! ..."        ← written by Cloud Function
    status:          "pending" | "processing" | "done" | "error"
    askedAt:         Timestamp
    answeredAt:      Timestamp | null
    error:           string | null
```

### How the ESP32 uses Firestore (REST API)

The ESP32 has no Firebase SDK, so it uses the **Firestore REST API** with an API key or anonymous auth token.

**Write a question:**
```
POST https://firestore.googleapis.com/v1/projects/{PROJECT_ID}/databases/(default)/documents/sessions/{SESSION_ID}/exchanges
Authorization: Bearer {FIREBASE_AUTH_TOKEN}
Content-Type: application/json

{
  "fields": {
    "question":  { "stringValue": "What is the capital of France?" },
    "answer":    { "nullValue": null },
    "status":    { "stringValue": "pending" },
    "askedAt":   { "timestampValue": "2026-06-05T10:00:00Z" }
  }
}
```

**Poll for the answer** (the response includes the auto-generated document ID):
```
GET https://firestore.googleapis.com/v1/projects/{PROJECT_ID}/databases/(default)/documents/sessions/{SESSION_ID}/exchanges/{EXCHANGE_ID}
Authorization: Bearer {FIREBASE_AUTH_TOKEN}
```
Loop every 2 seconds until `status == "done"`, then read `answer`.

---

## Step 8: Get an Auth Token for the ESP32

The ESP32 needs a Firebase Auth token to write to Firestore (due to security rules).

The easiest approach: use **Anonymous Authentication** via the Firebase Auth REST API.

```
POST https://identitytoolkit.googleapis.com/v1/accounts:signUp?key={WEB_API_KEY}
Content-Type: application/json

{ "returnSecureToken": true }
```

This returns an `idToken` (valid 1 hour) and a `refreshToken` (permanent).
Store the `refreshToken` in the ESP32's flash and refresh the `idToken` on each boot.

Your Firebase Web API Key is in: Firebase Console → Project Settings → General → Web API Key.

---

## What's Next

- [ ] Replace `YOUR_FIREBASE_PROJECT_ID` in `.firebaserc`
- [ ] Run `firebase deploy` from the `firebase/` folder
- [ ] Write ESP32 code to POST questions and poll for answers via Firestore REST API
- [ ] Wire Flutter app to listen to `/sessions/{id}/exchanges` in real-time
