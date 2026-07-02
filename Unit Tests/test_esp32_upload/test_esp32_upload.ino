/**
 * Unit Test 1: ESP32-S3 Board Upload & Basic Connectivity
 * Board: LCDWIKI 2.8" ESP32-S3 (ES3C28P / ES3N28P)
 *
 * What it tests:
 *   - Sketch uploads successfully via USB Type-C
 *   - Serial output works at 115200 baud
 *   - PSRAM is detected (required for audio buffer)
 *   - RGB LED blinks to confirm execution
 *
 * Pass criteria: Serial prints PASS and RGB LED blinks green.
 * Fail criteria: No serial output, or PSRAM not found.
 *
 * Board settings:
 *   - Board: "ESP32S3 Dev Module"
 *   - Flash Size: 16MB (128Mb)
 *   - PSRAM: "OPI PSRAM"
 *   - Partition Scheme: "Huge APP"
 */

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

#define PIN_RGB_LED  42
#define NUM_LEDS     1

Adafruit_NeoPixel led(NUM_LEDS, PIN_RGB_LED, NEO_GRB + NEO_KHZ800);

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=============================");
  Serial.println(" Unit Test 1: ESP32-S3 Upload");
  Serial.println("=============================");

  // Chip info
  Serial.printf("Chip model  : %s\n", ESP.getChipModel());
  Serial.printf("Chip cores  : %d\n", ESP.getChipCores());
  Serial.printf("CPU freq    : %d MHz\n", ESP.getCpuFreqMHz());
  Serial.printf("Flash size  : %d MB\n", ESP.getFlashChipSize() / (1024 * 1024));

  // PSRAM check
  bool psramOk = psramFound();
  Serial.printf("PSRAM found : %s\n", psramOk ? "YES" : "NO");
  if (psramOk) {
    Serial.printf("PSRAM size  : %d KB\n", ESP.getPsramSize() / 1024);
  }

  // RGB LED
  led.begin();
  led.setBrightness(50);

  if (psramOk) {
    Serial.println("\n[RESULT] PASS");
    led.setPixelColor(0, led.Color(0, 255, 0)); // green
  } else {
    Serial.println("\n[RESULT] FAIL — PSRAM not detected. Check board settings.");
    led.setPixelColor(0, led.Color(255, 0, 0)); // red
  }
  led.show();
}

void loop() {
  // Blink to confirm execution
  led.setBrightness(50);
  led.show();
  delay(500);
  led.setBrightness(0);
  led.show();
  delay(500);
}
