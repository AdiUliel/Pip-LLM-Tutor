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
its ESP32-S3 SPI processor file — independent of the pin config. Use **LCDWIKI's
pre-patched TFT_eSPI 2.5.43** instead:

1. Replace `Arduino/libraries/TFT_eSPI` with the bundled copy at
   `Dont copy/1-示例程序_Demo/Arduino/Install libraries/TFT_eSPI`.
2. Apply the patches from `Dont copy/1-示例程序_Demo/Arduino/Replaced files/`:
   - `User_Setup.h` → `TFT_eSPI/User_Setup.h` (defines `USE_HSPI_PORT`, the board pins, ILI9341 driver)
   - `TFT_eSPI_ESP32_S3.c` → `TFT_eSPI/Processors/TFT_eSPI_ESP32_S3.c` (the actual fix)
   - `ILI9341_Init.h` → `TFT_eSPI/TFT_Drivers/ILI9341_Init.h`
   - (skip `lv_conf.h` — that's for LVGL, not used by pip_face)

LCDWIKI's `User_Setup.h` is authoritative — do **not** also apply
`pip_face/User_Setup_LCDWIKI.h` (kept only as a pin reference). The firmware has a
`USE_PIP_FACE` switch (top of `homework_assistant.ino`) to build/run audio-only
without the display. Verify the display with LCDWIKI's `Example_03_display_graphics`
demo before flashing the firmware.

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
