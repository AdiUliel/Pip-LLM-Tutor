/* ============================================================================
 *  PipFace — animated tutor-device face for ESP32-S3 + ILI9341 (240x320, RGB565)
 * ----------------------------------------------------------------------------
 *  Public API for the procedural face renderer. Twelve emotion states drawn
 *  with simple shapes — no image assets, no SD card, ~30 fps from a 240x240
 *  off-screen sprite (~115 KB, requires PSRAM).
 *
 *  TFT_eSPI (Bodmer) is the only hard dependency. Configure
 *  TFT_eSPI/User_Setup.h for YOUR wiring. For the LCDWIKI 2.8" ESP32-S3 Display
 *  (this project's board) use the values below — a ready-to-copy file ships as
 *  pip_face/User_Setup_LCDWIKI.h. NOTE: do NOT use DC=9 / RST=8 (the old
 *  generic example): pin 8 is the I2S speaker line on this board.
 *
 *      #define ILI9341_DRIVER
 *      #define TFT_WIDTH  240
 *      #define TFT_HEIGHT 320
 *      #define TFT_MISO 13
 *      #define TFT_MOSI 11
 *      #define TFT_SCLK 12
 *      #define TFT_CS   10
 *      #define TFT_DC   46
 *      #define TFT_RST  -1     // tied to board reset; no dedicated GPIO
 *      // Backlight (GPIO45) is driven by the main firmware, not TFT_eSPI.
 *      #define SPI_FREQUENCY 40000000
 *
 *  Integration (5 lines in your main sketch):
 *
 *      #include "PipFace.h"
 *      void setup() { Pip::begin(); }
 *      void loop()  {
 *        pollFirebase();                          // your code
 *        Pip::setDeviceStatus(status, mood);      // when status changes
 *        Pip::tick();                             // drives the animation
 *      }
 * ========================================================================== */
#pragma once

#include <Arduino.h>

namespace Pip {

  // Call once from setup(). Initializes TFT_eSPI, allocates the 240x240
  // sprite (PSRAM), paints an idle frame. Returns false if the sprite can't
  // be allocated — usually means PSRAM is disabled in the board config.
  bool begin();

  // Call every loop iteration. Internally rate-limits to ~30 fps; cheap to
  // call at high frequency. Re-renders the bottom status strip only when
  // setStrip() has changed it since the last frame.
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
