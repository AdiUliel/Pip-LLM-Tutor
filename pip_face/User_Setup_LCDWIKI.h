// ============================================================================
//  TFT_eSPI User_Setup for the LCDWIKI 2.8" ESP32-S3 Display (ES3C28P/ES3N28P)
//  Used by pip_face on the Homework Assistant tutor device.
// ----------------------------------------------------------------------------
//  ⚠️ IMPORTANT: this pin file ALONE is not enough on this board. Stock
//  TFT_eSPI's ESP32-S3 SPI processor crashes at init() here (StoreProhibited).
//  The board needs LCDWIKI's PATCHED TFT_eSPI: use their bundled library and
//  apply the files in "Dont copy/1-示例程序_Demo/Arduino/Replaced files/"
//  (User_Setup.h + Processors/TFT_eSPI_ESP32_S3.c + TFT_Drivers/ILI9341_Init.h).
//  See Documentation/INTEGRATION.md. Their User_Setup.h is authoritative — this
//  file is kept only as a pin reference. Their setup also defines USE_HSPI_PORT.
// ----------------------------------------------------------------------------
//  HOW TO USE (one of two ways):
//   A) Replace the contents of  <Arduino>/libraries/TFT_eSPI/User_Setup.h
//      with this file, OR
//   B) Copy this file into the TFT_eSPI library folder and add this line to
//      TFT_eSPI/User_Setup_Select.h (and comment out the default include):
//          #include <User_Setup_LCDWIKI.h>
//
//  Pin source: ESP32/parameters.h (board pinout). These display pins do NOT
//  collide with the I2S audio bus (4,5,6,7,8), the I2C codec/touch bus (15,16)
//  or the push-to-talk button (2,3).
// ============================================================================

#define USER_SETUP_INFO "LCDWIKI_28_ESP32S3"

#define ILI9341_DRIVER          // ILI9341V uses the same init sequence

#define TFT_WIDTH  240
#define TFT_HEIGHT 320

// ── SPI pins (per ESP32/parameters.h) ───────────────────────────────────────
#define TFT_MISO 13
#define TFT_MOSI 11
#define TFT_SCLK 12
#define TFT_CS   10
#define TFT_DC   46
#define TFT_RST  -1             // no dedicated reset GPIO; tied to board reset

// Backlight (GPIO45) is driven by the main firmware (digitalWrite HIGH), not
// by TFT_eSPI — so TFT_BL is intentionally not defined here.

// ── Fonts ───────────────────────────────────────────────────────────────────
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT

// ── SPI speed ────────────────────────────────────────────────────────────────
#define SPI_FREQUENCY        40000000
#define SPI_READ_FREQUENCY   20000000

// IPS panel: if black appears white, uncomment the next line.
// #define TFT_INVERSION_ON
