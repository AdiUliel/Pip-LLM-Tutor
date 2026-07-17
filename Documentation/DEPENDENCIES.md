# Dependencies & Libraries

Full library/dependency list for the three parts of the project, plus the
board-specific modified TFT_eSPI setup. (The main `README.md` keeps only a short
summary and links here.) See also `INTEGRATION.md` for deploy/flash steps.

## ESP32 firmware (`ESP32/homework_assistant/`)

- **ArduinoJson** by Benoit Blanchon — v7.x (Firestore REST + STT JSON parsing)
- **TFT_eSPI** by Bodmer — v2.5.43, **modified for this board** (see below); drives the ILI9341 display via pip_face
- **pip_face** (this repo) — animated procedural face; depends on TFT_eSPI
- Built into the ESP32 core (no separate install): WiFi, WiFiClientSecure, HTTPClient, Wire (I2C → ES8311 codec), driver/i2s.h, time.h

Speech-to-Text and Text-to-Speech run server-side in Cloud Functions (Google
STT/TTS), so no on-device STT/TTS library is required.

### ⚠️ Modified TFT_eSPI (required for this board)

The stock TFT_eSPI from the Library Manager **crashes at `init()`** on the LCDWIKI
2.8" ESP32-S3 board (Guru Meditation: StoreProhibited, write to `0x10`) because of
its ESP32-S3 SPI processor file — independent of the pin config. **This repo ships
LCDWIKI's pre-patched TFT_eSPI 2.5.43** with the patched processor file, ILI9341
init, and a `User_Setup.h` (with `USE_HSPI_PORT` and the board pins) already
configured:

- Replace `Arduino/libraries/TFT_eSPI` with this repo's
  `ESP32/modified libraries/TFT_eSPI/`.

## Flutter app (`flutter_app/pubspec.yaml`)

- firebase_core ^4.10.0, firebase_auth ^6.5.2, cloud_firestore ^6.5.0, firebase_storage ^13.4.2, firebase_messaging ^16.3.0
- provider ^6.1.5, fl_chart ^1.2.0, file_picker ^11.0.2, http ^1.6.0, intl ^0.20.2
- shared_preferences ^2.5.5, google_fonts ^8.1.0, google_sign_in ^6.2.2, cupertino_icons ^1.0.8
- Dart SDK ^3.12.1

## Cloud Functions (`firebase/functions/package.json`)

- firebase-admin ^12, firebase-functions ^6, @google/genai ^1 (Vertex AI / Gemini)
- Node 22, deployed to region **europe-west10**

## Additional folders (not in the main README)

- **pip_face**: Arduino library for the animated on-device face (ESP32-S3 + ILI9341).
- **firebase**: Cloud Functions (tutor AI / LLM, STT & TTS proxies) + Firestore/Storage rules.
- **Documentation/INTEGRATION.md**: edge-to-edge wiring + deploy/flash steps.
- **Documentation/LLM_INTERFACE.md**: Firestore schema + LLM flow.
