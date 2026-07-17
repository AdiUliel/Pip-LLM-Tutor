# Edge-to-Edge Integration

How the three parts — **ESP32 device**, **Cloud Functions / Firebase**, and the
**Flutter parent app** — are wired together for an end-to-end test, and what you
have to do to deploy and run it.

## The connected flow

```text
                         ┌──────────────────────────────────────────┐
                         │            Firebase / Cloud Functions       │
                         │              (region: europe-west10)        │
   ESP32 device          │                                            │     Flutter app
 ┌───────────────┐       │  onSessionCreated ─ first question + TTS    │   ┌──────────────┐
 │ button + mic  │       │  answerQuestion   ─ grade + Gemini + TTS    │   │ device monitor│
 │ ES8311 codec  │       │  transcribeAudio  ─ STT proxy               │   │ reports/trends│
 │ ILI9341 +     │       │  Gemini (Vertex, global endpoint)           │   │ child config  │
 │ pip_face      │       └──────────────────────────────────────────┘   └──────────────┘
 └──────┬────────┘                     ▲   │                                   ▲   │
        │ 1. create session            │   │ 2. seed question+audio            │   │
        │  {childId,deviceId,subject,  │   ▼                                   │   ▼
        │   startedAt,status:starting, ├─ sessions/{id} (currentQuestion,      │  watch sessions,
        │   awaitingFirstQuestion}     │   currentQuestionAudioUrl)            │  questions, deviceState
        │                              │                                       │
        │ 3. speak question, listen    │                                       │
        │ 4. POST learning_turn ───────┼─ sessions/{id}/exchanges/{id}         │
        │      {childAnswer,pending}   │   (childAnswer)                        │
        │                              │ 5. answerQuestion grades, calls       │
        │                              │    Gemini, generates next question,   │
        │ 6. poll exchange ◄───────────┼─ writes {spokenFeedback, emotion,     │
        │      until status:done       │   nextQuestion, audioUrl, isCorrect}  │
        │ 7. show emotion + speak audio│                                       │
        │ 8. loop to step 3            │                                       │
        │                              │                                       │
        └─ deviceState/{deviceId} ─────┴──────── status + heartbeat ───────────┘
```

The device, functions, and app all read/write the **same Firestore documents** —
that shared schema is what connects them. See `LLM_INTERFACE.md` for field-level
detail.

## What was changed to connect everything

**Cloud Functions (`firebase/functions/`)**
- `setGlobalOptions({ region: "europe-west10" })` so *all* functions run in one
  region (previously only `transcribeAudio` was pinned; the rest defaulted to
  `us-central1`).
- Gemini moved to the Vertex **`global`** endpoint and the model bumped to
  `gemini-2.5-flash` (the old `gemini-2.0-flash-001` returns 404 — discontinued).
- **TTS audio is now actually produced.** `synthesizeAndStore` is wired into both
  the learning-turn path (feedback + next question) and the free-text path, and
  the audio is uploaded *before* `status:"done"` so the polling device never sees
  a done doc without an `audioUrl`.
- New `onSessionCreated` trigger seeds the first question and its audio when the
  device creates a session with `awaitingFirstQuestion:true`.

**ESP32 firmware (`ESP32/homework_assistant/`)**
- Drives the full adaptive tutor loop (speak question → listen → grade → speak
  feedback + next question → repeat) instead of one-shot free-text Q&A.
- Session creation is now **rules-compliant** (`childId, deviceId, subject,
  startedAt`) — the old version sent only `deviceId` and was rejected by the
  security rules.
- **Child auto-detect:** queries `children` where `deviceId == ` this device's
  Firebase UID; falls back to `CHILD_ID` in `secrets.h`; else runs generic.
- **NTP clock** + ISO-8601 timestamps so Firestore timestamps and the app's
  online/heartbeat check work.
- Writes `deviceState/{deviceId}` on every state change and every 10 s so the
  app's device monitor shows the device live.
- **pip_face integrated:** shows the emotion returned by the backend, plus
  speaking/listening/thinking/break states, and the current question on the
  bottom strip.

**pip_face (`pip_face/`)**
- README wiring example corrected for *this* board (the old example used
  `TFT_DC 9` / `TFT_RST 8`, but GPIO8 is the I2S speaker line here). The
  configured `User_Setup.h` ships with the patched TFT_eSPI in
  `ESP32/modified libraries/TFT_eSPI/`.

**Rules**
- `storage.rules` merged: the device's `tts/` public read **and** the app's
  `materials/` auth read/write now live in one ruleset (they were split across
  two divergent files). `firebase/` and `flutter_app/` copies are identical.

## Deploy

```bash
# 1. Backend (functions + Firestore + Storage rules), from firebase/
cd firebase
npm install --prefix functions
firebase deploy --only functions,firestore:rules,storage

# 2. Flutter app
cd ../flutter_app
flutter pub get
flutter run            # or: flutter build apk / flutter build web
```

> If `firebase deploy` rejects `europe-west10` for the Firestore-trigger
> functions (Eventarc availability varies by project), set `FUNCTIONS_REGION`
> to a nearby supported region, then update `CLOUD_FUNCTIONS_REGION` in
> `ESP32/homework_assistant/secrets.h` and `AppConstants.functionsRegion` to
> match.

Also enable in the Google Cloud console: **Vertex AI**, **Cloud
Text-to-Speech**, **Cloud Speech-to-Text**, and **Firebase Storage**.

## Flash the device

1. Install Arduino libraries: **ArduinoJson** (v7).
2. Install **pip_face** as a library: copy the `pip_face/` folder into your
   Arduino `libraries/` directory.
3. **TFT_eSPI for this board — use the patched copy in this repo, NOT a stock
   install.** The stock TFT_eSPI ESP32-S3 SPI processor file crashes at
   `init()` on this board (`StoreProhibited`, write to `0x10`). This repo ships
   LCDWIKI's working, pre-patched TFT_eSPI 2.5.43 — processor file, ILI9341
   init, and a `User_Setup.h` (with `USE_HSPI_PORT`) already configured for
   this board:
   - Replace `Arduino/libraries/TFT_eSPI` with this repo's
     `ESP32/modified libraries/TFT_eSPI/`.
4. Fill in `ESP32/homework_assistant/secrets.h` (WiFi, project, optional
   `CHILD_ID`, `SESSION_SUBJECT`).
5. Board settings: ESP32S3 Dev Module, **PSRAM: OPI PSRAM**, Flash 16 MB,
   Partition "Huge APP".
6. Flash `homework_assistant.ino`.

## Test checklist (edge-to-edge)

1. In the app, create a child and set its **deviceId** to the value the device
   prints at boot (`Signed in anonymously. UID: …`). (Or paste that UID into
   `CHILD_ID`/leave blank for generic.)
2. Power the device → it should print "first question ready", speak it, and show
   the listening face.
3. App → Device monitor shows the device **online** with the current question.
4. Hold the button, answer out loud, release → device shows thinking, then the
   emotion, and speaks the feedback + next question.
5. App → Reports / Session detail shows the question log filling in.

## Known things to verify on real hardware

- **Button line:** `pins.h` uses IO3 (with IO2 as GND). If your unit's IO3
  expansion line is flaky, switch `PIN_BTN` to GPIO0 (onboard BOOT button).
- **`europe-west10` trigger support** — see the deploy note above.
- **Vertex model availability** — we use the `global` endpoint to avoid
  region-specific gaps; if you pin `GEMINI_LOCATION`, confirm the model exists
  there.
