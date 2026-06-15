#pragma once
// Board: LCDWIKI 2.8" ESP32-S3 Display (ES3C28P / ES3N28P)
// See ESP32/parameters.h for the full annotated pin table.

// ── Audio (ES8311 codec) ─────────────────────────────────────────────────────
#define PIN_AUDIO_EN    1    // LOW = speaker amp on
#define PIN_I2S_MCLK    4
#define PIN_I2S_BCLK    5
#define PIN_I2S_DOUT    8    // ESP32 → speaker  (confirmed from LCD Wiki echo example)
#define PIN_I2S_LRCK    7
#define PIN_I2S_DIN     6    // Microphone → ESP32 (confirmed from LCD Wiki echo example)

// ── ES8311 I2C ───────────────────────────────────────────────────────────────
#define PIN_I2C_SDA     16
#define PIN_I2C_SCL     15
#define ES8311_I2C_ADDR 0x18

// ── Button (push-to-talk) ────────────────────────────────────────────────────
// TEMPORARY: using the onboard BOOT button (GPIO0) because the external button's
// IO3 line is broken (shorting IO3→GND did not read LOW). GPIO0 is active-LOW with
// its own onboard pull-up, so it reads LOW when BOOT is pressed.
// NOTE: do NOT hold BOOT during reset/power-on — that enters USB download mode.
// To revert to the external button, restore: PIN_BTN 3, PIN_BTN_GND 2 (drive LOW).
#define PIN_BTN         0    // GPIO0 — onboard BOOT button, INPUT_PULLUP, active LOW
//#define PIN_BTN       3    // (was) IO3 external button — line is broken
#define PIN_BTN_GND     2    // IO2 — unused with BOOT button (BOOT has its own GND)
                             // IO43/IO44 (UART) can't be used: TX idles HIGH, RX is
                             // held by UART peripheral.

// ── RGB status LED (WS2812) ──────────────────────────────────────────────────
#define PIN_RGB_LED     42

// ── I2S port ─────────────────────────────────────────────────────────────────
#define I2S_PORT        I2S_NUM_0

// ── Audio config ─────────────────────────────────────────────────────────────
#define SAMPLE_RATE     16000   // Hz — matches Google STT requirement
#define SAMPLE_BITS     16
#define MAX_RECORD_MS   7000    // max recording length in ms
#define RECORD_BUF_SIZE ((SAMPLE_RATE * SAMPLE_BITS/8 * 2 * MAX_RECORD_MS) / 1000)
// = 16000 * 2 * 2 (stereo) * 7 = 448000 bytes — fits in ESP32-S3's 8 MB PSRAM
// Stereo is required to match ES8311's 32-bit I2S frame (left=mic, right=zeros).
// Left channel is extracted after recording, halving the effective byte count.
