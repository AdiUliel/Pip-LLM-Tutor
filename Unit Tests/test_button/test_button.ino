/**
 * Unit Test 5: Push-to-Talk Button (IO3 input, IO2 as software GND)
 * Board: LCDWIKI 2.8" ESP32-S3 (ES3C28P / ES3N28P)
 *
 * What it tests:
 *   - External button on IO2/IO3/IO14/IO21 expansion header is detected
 *   - IO2 held LOW (OUTPUT) acts as the GND reference for the button
 *   - IO3 INPUT_PULLUP reads HIGH at rest, LOW when button pressed
 *
 * Pass criteria: Serial prints PRESSED / RELEASED with correct timing.
 * Fail criteria: Line reads LOW permanently (short), or never changes
 *               (button not connected / wrong pins).
 *
 * Wiring: button between IO2 and IO3 on the expansion header.
 *         IO43/IO44 (UART) can't be used — UART TX idles HIGH.
 *
 * Libraries: none (ESP32 built-in only)
 */

#define PIN_BTN     3   // IO3 — INPUT_PULLUP, reads button (active LOW)
#define PIN_BTN_GND 2   // IO2 — OUTPUT LOW, software GND for the button

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=========================");
  Serial.println(" Unit Test 5: Button");
  Serial.println("=========================");

  pinMode(PIN_BTN_GND, OUTPUT);
  digitalWrite(PIN_BTN_GND, LOW);  // IO2 acts as GND
  pinMode(PIN_BTN, INPUT_PULLUP);

  bool idle = (digitalRead(PIN_BTN) == HIGH);
  Serial.printf("[Button] Resting state: %s\n", idle ? "HIGH (correct)" : "LOW (check wiring — possible short)");

  if (!idle) {
    Serial.println("[RESULT] FAIL — IO3 reads LOW at rest. Check wiring.");
    return;
  }

  Serial.println("[Button] Waiting for press events...\n");
}

void loop() {
  static bool lastState = HIGH;
  static uint32_t pressTime = 0;

  bool current = digitalRead(PIN_BTN);

  if (current == LOW && lastState == HIGH) {
    pressTime = millis();
    Serial.println("[Button] PRESSED");
  }

  if (current == HIGH && lastState == LOW) {
    uint32_t held = millis() - pressTime;
    Serial.printf("[Button] RELEASED — held for %u ms\n", held);
    if (held < 50) {
      Serial.println("         (possible bounce — held < 50 ms)");
    } else {
      Serial.println("[RESULT] PASS");
    }
  }

  lastState = current;
  delay(10);  // simple debounce polling interval
}
