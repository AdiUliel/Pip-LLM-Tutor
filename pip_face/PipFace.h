/* ============================================================================
 *  PipFace — animated tutor-device face for ESP32-S3 + ILI9341 (240x320, RGB565)
 * ----------------------------------------------------------------------------
 *  Public API for the procedural face renderer. Twelve emotion states drawn
 *  with simple shapes — no image assets, no SD card, ~30 fps from a 240x240
 *  off-screen sprite (~115 KB, requires PSRAM).
 *
 *  TFT_eSPI (Bodmer) is the only hard dependency. Configure
 *  TFT_eSPI/User_Setup.h for YOUR wiring — example for ESP32-S3:
 *
 *      #define ILI9341_DRIVER
 *      #define TFT_WIDTH  240
 *      #define TFT_HEIGHT 320
 *      #define TFT_MOSI 11
 *      #define TFT_SCLK 12
 *      #define TFT_CS   10
 *      #define TFT_DC    9
 *      #define TFT_RST   8
 *      #define SPI_FREQUENCY 40000000
 *
 *  Integration:
 *
 *      #include "PipFace.h"
 *      void setup() { Pip::begin(); }              // spawns a render task
 *      void loop()  {
 *        pollFirebase();                           // your code
 *        Pip::setDeviceStatus(status, mood);       // when status changes
 *      }
 *
 *  The face animates on its own FreeRTOS task — your main loop can be
 *  blocked on HTTPS, STT, or audio for seconds at a time and the face
 *  will stay smooth.
 * ========================================================================== */
#pragma once

#include <Arduino.h>

namespace Pip {

  // Call once from setup(). Initializes TFT_eSPI + the Hebrew text engine,
  // allocates the 240x240 sprite (PSRAM), and spawns a low-priority
  // FreeRTOS task that draws the face at ~30 fps for the lifetime of the
  // program. Returns false if the sprite can't be allocated — usually
  // means PSRAM is disabled in the board config; in that case the task is
  // not started and the rest of the API becomes a no-op.
  bool begin();

  // No-op. The face renders on its own task spawned in begin(), so the
  // main loop doesn't need to drive frames — it stays responsive even
  // when the main loop is blocked on HTTPS, STT, audio recording, or
  // playback. Kept in the API so existing call-sites still compile.
  void tick();

  // Direct setter — pick any of the 12 emotions by name when your tutoring
  // logic has finer-grained signals than deviceState.status alone.
  // Case-sensitive labels:
  //   idle, speaking, listening, thinking, happy, proud, celebrating,
  //   encouraging, concerned, playful, sleepy, oops
  // Unknown labels are ignored (current state preserved).
  void setEmotion(const char* label);

  // High-level setter — speaks the project's deviceState.status vocabulary.
  // Optional mood (1..5) refines the "feedback" state; pass -1 if unknown.
  //   idle      -> idle
  //   asking    -> speaking
  //   listening -> listening
  //   feedback  -> proud      (mood >= 4)
  //              -> concerned  (mood <= 2)
  //              -> encouraging (mood == 3 or unknown)
  //   break     -> sleepy
  //   error     -> oops
  // Unknown statuses are ignored.
  void setDeviceStatus(const char* status, int mood = -1);

  // Optional bottom status strip — re-rendered only when text or stars
  // change between calls. Safe to call every loop iteration.
  // Passing nullptr clears the text.
  void setStrip(const char* questionOrLabel, int stars);

}  // namespace Pip
