# Edge-to-Edge Integration

How the three parts — **ESP32 device**, **Cloud Functions / Firebase**, and the
**Flutter parent app** — are wired together for an end-to-end test, and what you
have to do to deploy and run it.

## The connected flow

```text
   ESP32 device                Firebase / Cloud Functions              Flutter app
 ┌───────────────┐            (region: europe-west10)               ┌───────────────┐
 │ button + mic  │                                                  │ device monitor │
 │ ES8311 codec  │   1. create session {childId, deviceId,          │ reports/trends │
 │ ILI9341 +     │      subject, startedAt} ───────────────▶        │ child config   │
 │ pip_face      │      onSessionCreated seeds the first            └───────┬───────┘
 └──────┬────────┘      question + its TTS audio                            │
        │                                                                   │
        │ 2. speak question, record answer (push-to-talk)                   │
        │ 3. POST raw PCM ──▶ processTurn:                                  │
        │       STT → grade → Gemini (gated) → TTS,                         │
        │       writes sessions/questions/exchanges docs ───────────────────┤
        │ 4. response: feedback + emotion + next question                   │ watches
        │       in headers, MP3 audio in the body                           │ sessions,
        │ 5. show emotion, speak audio, loop to 2                           │ questions,
        │                                                                   │ deviceState
        └── deviceState/{deviceId}: status + heartbeat every 10 s ──────────┘
```

The device, functions, and app all read/write the **same Firestore documents** —
that shared schema is what connects them. See `LLM_INTERFACE.md` for field-level
detail. (`answerQuestion`, a Firestore-trigger route, stays deployed as the
app-initiated / fallback path.)

## How each part is wired

**Cloud Functions (`firebase/functions/`)**
- `setGlobalOptions({ region: "europe-west10" })` so *all* functions run in one
  region. The functions must use the Firebase Functions v2 API for this global
  option to apply.
- Gemini runs through the Vertex **`global`** endpoint; the tutor feedback uses
  `gemini-2.5-flash-lite` and material extraction uses `gemini-2.5-flash`.
- **TTS audio** is synthesized for every spoken reply; on the trigger path the
  audio is uploaded *before* `status:"done"`, so a polling device never sees a
  done doc without an `audioUrl`.
- The `onSessionCreated` trigger seeds the first question and its audio when the
  device creates a session with `awaitingFirstQuestion:true`.

**ESP32 firmware (`ESP32/homework_assistant/`)**
- Drives the full adaptive tutor loop (speak question → listen → grade → speak
  feedback + next question → repeat).
- Session creation sends the full **rules-required** document (`childId,
  deviceId, subject, startedAt`) so the Firestore security rules accept it.
- **Child auto-detect:** queries `children` where `deviceId ==` this device's
  Firebase UID; falls back to `CHILD_ID` in `secrets.h` (the Firestore child
  document ID, not the device UID); else runs generic.
- **NTP clock** + ISO-8601 timestamps so Firestore timestamps and the app's
  online/heartbeat check work.
- Writes `deviceState/{deviceId}` on every state change and every 10 s so the
  app's device monitor shows the device live.
- **pip_face integrated:** shows the emotion returned by the backend, plus
  speaking/listening/thinking/break states, and the current question on the
  bottom strip.

**pip_face (`pip_face/`)**
- README wiring example matches *this* board (GPIO8 is the I2S speaker line,
  so the display uses different DC/RST pins). The configured `User_Setup.h`
  ships with the patched TFT_eSPI in `ESP32/modified libraries/TFT_eSPI/`.

**Rules**
- One `storage.rules` ruleset covers both the device's `tts/` public read and
  the app's `materials/` authenticated read/write; the `firebase/` and
  `flutter_app/` copies are identical.

## Deploy

```bash
# 1. Backend (functions + Firestore + Storage rules), from firebase/
cd firebase
npm install --prefix functions
firebase login
firebase use <project-id>
firebase deploy --only functions,firestore:rules,storage

# 2. Flutter app
cd ../flutter_app
flutter pub get
flutter run            # or: flutter build apk / flutter build web
```

> If `firebase deploy` rejects `europe-west10` for the Firestore-trigger
> functions, verify that the functions use the v2 API and that the selected
> region supports the required triggers and matches the Firestore location. If
> another supported region is used, update the backend region configuration,
> `CLOUD_FUNCTIONS_REGION` in `ESP32/homework_assistant/secrets.h`, and
> `AppConstants.functionsRegion` to match. `FUNCTIONS_REGION` only has an effect
> if the backend code reads that environment variable.

Also enable in the Google Cloud console: **Vertex AI**, **Cloud
Text-to-Speech**, **Cloud Speech-to-Text**, and **Firebase Storage**. Enable
**Anonymous Authentication** in Firebase Authentication, and make sure billing
and the runtime service-account permissions required by these services are in
place.

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
7. Button line: `pins.h` uses IO3 (with IO2 as GND); if a unit's IO3 expansion
   line is flaky, switch `PIN_BTN` to GPIO0 (the onboard BOOT button).

## Test checklist (edge-to-edge)

1. In the app, create a child and set its **deviceId** to the value the device
   prints at boot (`Signed in anonymously. UID: …`). If auto-detection is not
   used, set `CHILD_ID` to the Firestore child document ID — **not** to the
   device UID. Leave `CHILD_ID` blank only when intentionally running in generic
   mode.
2. Power the device → it should print "first question ready", speak it, and show
   the listening face.
3. App → Device monitor shows the device **online** with the current question.
4. Hold the button, answer out loud, release → device shows thinking, then the
   emotion, and speaks the feedback + next question.
5. App → Reports / Session detail shows the question log filling in.
