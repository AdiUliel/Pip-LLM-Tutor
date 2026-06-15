#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// TTS Player — no external library required
//
// The Cloud Function already called Google TTS and uploaded a WAV file to
// Firebase Storage. This module downloads the WAV over HTTP and streams the
// raw PCM audio directly to I2S (skipping the 44-byte WAV header).
//
// Works with any ESP32 core version — uses only driver/i2s.h (built-in).
// ─────────────────────────────────────────────────────────────────────────────

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <driver/i2s.h>
#include "pins.h"

// WAV standard header size (RIFF PCM mono = always 44 bytes)
#define WAV_HEADER_SIZE 44

// ── Stream WAV from URL, play via I2S ────────────────────────────────────────
// audioUrl: public Firebase Storage URL to the WAV file
// Call AFTER i2s_stop_recording(). The I2S driver is reused (same port).
void speakFromUrl(const String& audioUrl) {
  if (audioUrl.isEmpty()) {
    Serial.println("[TTS] No audio URL — skipping playback.");
    return;
  }

  Serial.println("[TTS] Connecting to: " + audioUrl);
  digitalWrite(PIN_AUDIO_EN, LOW);  // Enable speaker amp

  // ── Reconfigure I2S for PLAYBACK ─────────────────────────────────────────
  // Same pins and sample rate as recording, but TX only.
  i2s_config_t cfg = {
    .mode             = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate      = SAMPLE_RATE,          // 16000 Hz (must match TTS output)
    .bits_per_sample  = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format   = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count    = 8,
    .dma_buf_len      = 512,
    .use_apll         = true,
    .tx_desc_auto_clear = true,
    .mclk_multiple    = I2S_MCLK_MULTIPLE_256,
  };
  i2s_pin_config_t pins = {
    .mck_io_num   = PIN_I2S_MCLK,
    .bck_io_num   = PIN_I2S_BCLK,
    .ws_io_num    = PIN_I2S_LRCK,
    .data_out_num = PIN_I2S_DOUT,
    .data_in_num  = I2S_PIN_NO_CHANGE,  // TX only — no mic input during playback
  };
  i2s_driver_install(I2S_PORT, &cfg, 0, NULL);
  i2s_set_pin(I2S_PORT, &pins);
  i2s_zero_dma_buffer(I2S_PORT);

  // ── Download and stream ───────────────────────────────────────────────────
  WiFiClientSecure sslClient;
  sslClient.setInsecure();
  HTTPClient http;
  http.begin(sslClient, audioUrl);
  http.setTimeout(15000);

  int code = http.GET();
  if (code != 200) {
    Serial.printf("[TTS] HTTP %d — skipping.\n", code);
    http.end();
    i2s_driver_uninstall(I2S_PORT);
    digitalWrite(PIN_AUDIO_EN, HIGH);
    return;
  }

  WiFiClient* stream = http.getStreamPtr();
  int         remaining = http.getSize();   // -1 if chunked

  // Skip WAV header (44 bytes)
  uint8_t header[WAV_HEADER_SIZE];
  int headerRead = 0;
  while (headerRead < WAV_HEADER_SIZE) {
    if (stream->available()) {
      headerRead += stream->readBytes(header + headerRead,
                                      WAV_HEADER_SIZE - headerRead);
    }
    yield();
  }
  if (remaining > 0) remaining -= WAV_HEADER_SIZE;

  // Stream PCM chunks → I2S
  uint8_t buf[512];
  while (http.connected() || stream->available()) {
    int avail = stream->available();
    if (avail > 0) {
      int toRead = min(avail, (int)sizeof(buf));
      int n = stream->readBytes(buf, toRead);
      size_t written = 0;
      i2s_write(I2S_PORT, buf, n, &written, pdMS_TO_TICKS(200));
      if (remaining > 0) {
        remaining -= n;
        if (remaining <= 0) break;
      }
    } else {
      delay(1);
    }
    yield();
  }

  // Drain DMA buffer
  delay(200);

  http.end();
  i2s_driver_uninstall(I2S_PORT);
  digitalWrite(PIN_AUDIO_EN, HIGH);
  Serial.println("[TTS] Playback done.");
}
