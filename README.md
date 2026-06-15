## XXXXX Project by :  
  
## Details about the project
 
## Folder description :
* ESP32: source code for the esp side (firmware) — `homework_assistant/` is the main sketch.
* pip_face: Arduino library for the animated on-device face (ESP32-S3 + ILI9341).
* firebase: Cloud Functions (the tutor AI / LLM, STT & TTS proxies) + Firestore/Storage rules.
* flutter_app: dart code for our Flutter parent app.
* Documentation: wiring diagram, operating instructions, `INTEGRATION.md` (edge-to-edge wiring) and `LLM_INTERFACE.md`.
* Unit Tests: tests for individual hardware components (input / output devices)
* Parameters: contains description of parameters and settings that can be modified IN YOUR CODE
* Assets: link to 3D printed parts, Audio files used in this project, Fritzing file for connection diagram (FZZ format) etc

## ESP32 SDK version used in this project: 
* ESP32 by Espressif - version 2.0.17

## Arduino/ESP32 libraries used in this project (firmware):
* ArduinoJson by Benoit Blanchon - version 7.x (Firestore REST + STT JSON)
* TFT_eSPI by Bodmer - version 2.5.43 — **MODIFIED, see note below** (drives the ILI9341 display via pip_face)
* pip_face (this repo) — animated procedural face; depends on TFT_eSPI
* Built into the ESP32 core (no separate install): WiFi, WiFiClientSecure, HTTPClient, Wire (I2C → ES8311 codec), driver/i2s.h, time.h

Speech-to-Text and Text-to-Speech run server-side in Cloud Functions (Google STT/TTS), so no on-device STT/TTS library is required.

### ⚠️ Modified TFT_eSPI (required for this board)
The stock TFT_eSPI from the Library Manager **crashes at `init()`** on the LCDWIKI
2.8" ESP32-S3 board (Guru Meditation: StoreProhibited) because of its ESP32-S3 SPI
processor file. Use **LCDWIKI's pre-patched TFT_eSPI 2.5.43** instead:
1. Replace `Arduino/libraries/TFT_eSPI` with the bundled copy at
   `Dont copy/1-示例程序_Demo/Arduino/Install libraries/TFT_eSPI`.
2. Apply the patches from `Dont copy/1-示例程序_Demo/Arduino/Replaced files/`:
   * `User_Setup.h` → `TFT_eSPI/User_Setup.h` (defines `USE_HSPI_PORT`, the board pins, ILI9341 driver)
   * `TFT_eSPI_ESP32_S3.c` → `TFT_eSPI/Processors/TFT_eSPI_ESP32_S3.c` (the actual fix)
   * `ILI9341_Init.h` → `TFT_eSPI/TFT_Drivers/ILI9341_Init.h`
   * (skip `lv_conf.h` — that's for LVGL, not used by pip_face)

LCDWIKI's `User_Setup.h` is authoritative — do not also apply `pip_face/User_Setup_LCDWIKI.h`
(kept only as a pin reference). The firmware also has a `USE_PIP_FACE` switch (top of
`homework_assistant.ino`) to build/run audio-only without the display. See
`Documentation/INTEGRATION.md` for full deploy/flash steps.

## Flutter app packages (flutter_app/pubspec.yaml):
* firebase_core ^4.10.0, firebase_auth ^6.5.2, cloud_firestore ^6.5.0, firebase_storage ^13.4.2, firebase_messaging ^16.3.0
* provider ^6.1.5, fl_chart ^1.2.0, file_picker ^11.0.2, http ^1.6.0, intl ^0.20.2, shared_preferences ^2.5.5, google_fonts ^8.1.0, google_sign_in ^6.2.2, cupertino_icons ^1.0.8

## Cloud Functions dependencies (firebase/functions/package.json):
* firebase-admin ^12, firebase-functions ^6, @google/genai ^1 (Vertex AI / Gemini) — Node 22, region europe-west10

## Connection diagram:
* [View the ESP32-S3 Hardware Documentation Here](Documentation/Hardware%20Documentation/index.html)
## Project Poster:
 
This project is part of ICST - The Interdisciplinary Center for Smart Technologies, Taub Faculty of Computer Science, Technion
https://icst.cs.technion.ac.il/
