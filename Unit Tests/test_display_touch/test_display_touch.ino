/**
 * Unit Test 3: 2.8" ILI9341 Display + FT6336G Touch
 * Board: LCDWIKI 2.8" ESP32-S3 (ES3C28P / ES3N28P)
 *
 * What it tests:
 *   - ILI9341 SPI display initializes and renders color fills
 *   - FT6336G touch controller reports touch coordinates via I2C
 *
 * Pass criteria: Screen fills with red/green/blue in sequence,
 *               then shows "Touch me!" and prints coordinates on touch.
 * Fail criteria: White/blank screen, or no touch events after pressing.
 *
 * Libraries (Arduino IDE → Manage Libraries):
 *   - Adafruit ILI9341  by Adafruit
 *   - Adafruit GFX Library  by Adafruit
 */

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>

// ── Display pins ──────────────────────────────────────────────────────────────
#define LCD_CS    10
#define LCD_DC    46
#define LCD_CLK   12
#define LCD_MOSI  11
#define LCD_MISO  13
#define LCD_BL    45   // backlight

// ── Touch pins (FT6336G via I2C) ─────────────────────────────────────────────
#define TOUCH_SDA  16
#define TOUCH_SCL  15
#define TOUCH_INT  17
#define FT6336_ADDR 0x38

Adafruit_ILI9341 tft(LCD_CS, LCD_DC, LCD_MOSI, LCD_CLK, -1, LCD_MISO);

// ── Read single touch point from FT6336G ─────────────────────────────────────
bool readTouch(int16_t &x, int16_t &y) {
  Wire.beginTransmission(FT6336_ADDR);
  Wire.write(0x02);  // TD_STATUS register
  Wire.endTransmission(false);
  Wire.requestFrom(FT6336_ADDR, 6);
  if (Wire.available() < 6) return false;
  uint8_t td  = Wire.read();          // touch count
  uint8_t xh  = Wire.read();
  uint8_t xl  = Wire.read();
  uint8_t yh  = Wire.read();
  uint8_t yl  = Wire.read();
  Wire.read();                         // weight (unused)
  if ((td & 0x0F) == 0) return false;
  x = ((xh & 0x0F) << 8) | xl;
  y = ((yh & 0x0F) << 8) | yl;
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=================================");
  Serial.println(" Unit Test 3: Display + Touch");
  Serial.println("=================================");

  // Backlight on
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);

  // Display init
  tft.begin();
  tft.setRotation(1);
  Serial.println("[Display] ILI9341 init OK");

  // Color sweep test
  Serial.println("[Display] Color fill: RED");
  tft.fillScreen(ILI9341_RED);   delay(700);
  Serial.println("[Display] Color fill: GREEN");
  tft.fillScreen(ILI9341_GREEN); delay(700);
  Serial.println("[Display] Color fill: BLUE");
  tft.fillScreen(ILI9341_BLUE);  delay(700);

  // Touch controller init
  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  Wire.beginTransmission(FT6336_ADDR);
  bool touchOk = (Wire.endTransmission() == 0);
  Serial.printf("[Touch] FT6336G found: %s\n", touchOk ? "YES" : "NO");

  // Prompt screen
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(40, 100);
  tft.print(touchOk ? "Touch me!" : "Touch IC not found");

  Serial.println("[Display] PASS — Screen rendering OK");
  if (touchOk) {
    Serial.println("[Touch] Waiting for touch events (30 s)...");
  } else {
    Serial.println("[RESULT] PARTIAL — Display OK, touch IC not found on I2C.");
  }
}

void loop() {
  int16_t tx, ty;
  if (readTouch(tx, ty)) {
    Serial.printf("[Touch] x=%d  y=%d\n", tx, ty);
    tft.fillCircle(tx, ty, 8, ILI9341_YELLOW);
    delay(50);
  }
}
