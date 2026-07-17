# Firebase Setup Guide

How to wire up a Firebase project for Pip from scratch. (For day-to-day deploy
commands see `Documentation/INTEGRATION.md`; for the full Firestore schema see
`Documentation/LLM_INTERFACE.md`.)

---

## Architecture Overview

```
ESP32 (mic + speaker + display)
  │
  │  ONE HTTPS call per turn (raw PCM16 audio upload)
  ▼
Cloud Function processTurn  ──►  Google STT (transcribe)
  │                         ──►  Gemini 2.5 Flash via Vertex AI (grade + feedback + next question)
  │                         ──►  Google TTS (he-IL audio, returned inline as the HTTP body)
  │
  ├─► writes the turn + session stats to Firestore
  ▼
Firestore  ◄── realtime streams ──►  Flutter parent app (reports, materials, pairing)
```

Supporting functions: `onSessionCreated` (first question + TTS when a session
doc appears), `extractQuestionsFromMaterial` (turns uploaded homework files
into validated Q&A via Gemini), `enforceDeviceUniqueness` (one device ↔ one
child, newest pairing wins), `notifyOnSessionStarted`/`monitorTutor` (FCM push
+ monitoring), `transcribeAudio`/`synthesizeSpeech`/`ttsBytes` (STT/TTS
proxies), and a legacy post-and-poll path (`answerQuestion`,
`startLearningSession`, `submitChildAnswer`) kept as an A/B fallback
(`USE_PROCESS_TURN 0` in the firmware).

---

## Step 1: Firebase Console Setup

Go to [console.firebase.google.com](https://console.firebase.google.com) and create/open a project.

### 1a. Firestore
- **Build → Firestore Database → Create database**, Production mode.
- Region close to you (this project: `eur3`/European region).

### 1b. Authentication
- **Build → Authentication → Get started**.
- Enable **Anonymous** (the ESP32 signs in anonymously and persists its
  refresh token in flash, so its UID is stable across reboots).
- Enable **Email/Password** and **Google** (the Flutter parent app supports both).

### 1c. Cloud Functions
- **Build → Functions → Get started**.
- ⚠️ Requires the **Blaze (pay-as-you-go)** plan. At this project's scale the
  cost is essentially $0, but a billing account is required.
- **Region:** all functions deploy to **`europe-west10`** (set via
  `setGlobalOptions` in `functions/index.js`). The ESP32
  (`CLOUD_FUNCTIONS_REGION` in `secrets.h`) and the Flutter app
  (`AppConstants.functionsRegion`) must match. If `firebase deploy` rejects the
  region for a Firestore-trigger function (Eventarc availability varies), set
  `FUNCTIONS_REGION` to a nearby supported region and update both client
  constants.

### 1d. Enable Google Cloud APIs
In the [Google Cloud console](https://console.cloud.google.com) for the same
project, enable:
- **Vertex AI API** (Gemini — no API key needed; functions use the project's
  service account, so there is no LLM key/secret to manage)
- **Cloud Speech-to-Text API**
- **Cloud Text-to-Speech API**
- **Firebase Storage** (uploaded homework files + generated TTS audio)

---

## Step 2: Install the Firebase CLI

```bash
npm install -g firebase-tools
firebase login
```

---

## Step 3: Set Your Project ID

Edit `firebase/.firebaserc` and set your project ID (Firebase console →
Project Settings → General):

```json
{
  "projects": { "default": "your-actual-project-id" }
}
```

---

## Step 4: Deploy

```bash
cd firebase/
npm install --prefix functions

# Functions discovery is heavy — without these flags deploy can OOM/time out:
NODE_OPTIONS="--max-old-space-size=8192" FUNCTIONS_DISCOVERY_TIMEOUT=540 \
  firebase deploy    # functions + firestore rules/indexes + storage rules
```

Optional env overrides (Functions → configuration): `GEMINI_MODEL`
(default `gemini-2.5-flash`), `GEMINI_LOCATION` (default `global`),
`FUNCTIONS_REGION`.

---

## Step 5: Firestore Data Model (summary)

```
parents/{parentId}                    ← app writes (account profile)
children/{childId}                    ← app writes (profile, settings, deviceId pairing)
materials/{materialId}                ← app writes; extractQuestionsFromMaterial fills items[]
pairingCodes/{TUTOR-XXXXXX}           ← device registers its code → firebaseUid
deviceState/{deviceUid}               ← device writes status + heartbeat; app reads
sessions/{sessionId}                  ← device creates; functions + device write turns/stats
  questions/ (subcollection)          ← one doc per Q&A turn
```

Full field-by-field schema: `Documentation/LLM_INTERFACE.md`.

---

## Step 6: Device Auth (already implemented in the firmware)

The ESP32 has no Firebase SDK — it uses the REST APIs:

1. First boot: `accounts:signUp` (anonymous) with the **Web API Key** from
   `secrets.h` → receives `idToken` + `refreshToken`.
2. The `refreshToken` + UID are persisted in NVS flash, so the device keeps a
   **stable UID** across reboots (this UID is what a child's `deviceId` links
   to when pairing).
3. Every boot: the token is refreshed via `securetoken.googleapis.com`.

No manual token work is needed — `ESP32/homework_assistant/firebase_client.h`
does all of this.

---

## Setup checklist

- [ ] Firestore + Auth (Anonymous, Email/Password, Google) + Functions (Blaze) enabled
- [ ] Vertex AI, STT, TTS, Storage APIs enabled in Google Cloud
- [ ] `.firebaserc` project ID set
- [ ] `firebase deploy` succeeded (with the NODE_OPTIONS flags)
- [ ] `ESP32/homework_assistant/secrets.h` filled in (Web API Key, project ID, region)
- [ ] Flutter app: `flutterfire configure` run against the same project
