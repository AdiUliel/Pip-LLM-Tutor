/* PipDemo — cycle every face state every 2.6 s. Flash this to confirm the
 * LCD wiring, PSRAM, and TFT_eSPI User_Setup.h are all correct before
 * integrating PipFace into the main firmware. */

#include "PipFace.h"

void setup() {
  Pip::begin();
  Pip::setStrip("7 x 6", 3);
}

void loop() {
  static uint32_t last = 0;
  static int      i    = 0;
  static const char* cycle[] = {
    "idle", "speaking", "listening", "thinking",
    "happy", "proud", "celebrating", "encouraging",
    "concerned", "playful", "sleepy", "oops",
  };
  if (millis() - last > 2600) {
    last = millis();
    Pip::setEmotion(cycle[i++ % 12]);
  }
  Pip::tick();
}
