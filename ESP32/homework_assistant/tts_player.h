#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// TTS Player — no external library required (uses minimp3 for decoding)
//
// The Cloud Function calls Google TTS (MP3 @ 16kHz mono) and uploads the file
// to Firebase Storage. This module downloads the MP3 over HTTPS, decodes it
// to raw PCM using minimp3 (a single-header C decoder), and streams the PCM
// directly to I2S.
//
// MP3 vs WAV: a 5-second clip is ~20 KB MP3 vs ~160 KB WAV — 8x smaller,
// saving ~1-1.5 s of download latency on every robot reply.
//
// Requires minimp3_ex.h in this directory.
// Get it from: https://github.com/lieff/minimp3 (public domain)
// Download minimp3.h and minimp3_ex.h and place them next to this file.
// ─────────────────────────────────────────────────────────────────────────────

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#include "minimp3_ex.h"

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <driver/i2s.h>
#include "pins.h"

// ── Configure I2S for playback ────────────────────────────────────────────────
static void _tts_i2s_install(int sampleRate) {
  i2s_config_t cfg = {
    .mode             = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate      = (uint32_t)sampleRate,
    .bits_per_sample  = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format   = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count    = 8,
    .dma_buf_len      = 512,
    .use_apll         = true,
    .tx_desc_auto_clear = true,
    .mclk_multiple    = I2S_MCLK_MULTIPLE_384,
  };
  i2s_pin_config_t pins = {
    .mck_io_num   = PIN_I2S_MCLK,
    .bck_io_num   = PIN_I2S_BCLK,
    .ws_io_num    = PIN_I2S_LRCK,
    .data_out_num = PIN_I2S_DOUT,
    .data_in_num  = I2S_PIN_NO_CHANGE,
  };
  i2s_driver_install(I2S_PORT, &cfg, 0, NULL);
  i2s_set_pin(I2S_PORT, &pins);
  i2s_zero_dma_buffer(I2S_PORT);
}

// ── Play raw PCM (int16_t mono) via I2S ──────────────────────────────────────
static void _tts_play_pcm(const mp3d_sample_t* pcm, size_t samples, int channels) {
  const size_t WRITE_CHUNK = 512;  // samples per I2S write
  size_t i = 0;
  while (i < samples) {
    size_t chunk = min(WRITE_CHUNK, samples - i);
    size_t written = 0;
    if (channels == 1) {
      i2s_write(I2S_PORT, pcm + i, chunk * sizeof(mp3d_sample_t), &written, portMAX_DELAY);
    } else {
      // Stereo → mono: take left channel only
      int16_t mono[WRITE_CHUNK];
      for (size_t j = 0; j < chunk; j++) mono[j] = pcm[(i + j) * 2];
      i2s_write(I2S_PORT, mono, chunk * sizeof(int16_t), &written, portMAX_DELAY);
    }
    i += chunk;
  }
}

// ── Decode MP3 bytes (in PSRAM) and play ─────────────────────────────────────
void speakFromMp3(const uint8_t* mp3Data, size_t mp3Len) {
  if (!mp3Data || mp3Len < 4) {
    Serial.println("[TTS] MP3 buffer too small — skipping.");
    return;
  }

  mp3dec_ex_t dec;
  if (mp3dec_ex_open_buf(&dec, mp3Data, mp3Len, MP3D_SEEK_TO_SAMPLE) != 0) {
    Serial.println("[TTS] minimp3: failed to open MP3 buffer.");
    return;
  }

  int sampleRate = dec.info.hz > 0 ? dec.info.hz : SAMPLE_RATE;
  int channels   = dec.info.channels > 0 ? dec.info.channels : 1;
  Serial.printf("[TTS] MP3: %d Hz, %d ch, ~%llu samples\n",
                sampleRate, channels, (unsigned long long)dec.samples);

  // Allocate PCM output in PSRAM
  size_t pcmCap = dec.samples > 0 ? (size_t)dec.samples : (mp3Len * 8);
  mp3d_sample_t* pcm = (mp3d_sample_t*)ps_malloc(pcmCap * sizeof(mp3d_sample_t));
  if (!pcm) {
    Serial.println("[TTS] ps_malloc failed for PCM buffer.");
    mp3dec_ex_close(&dec);
    return;
  }

  size_t decoded = mp3dec_ex_read(&dec, pcm, pcmCap);
  mp3dec_ex_close(&dec);
  Serial.printf("[TTS] MP3 decoded: %u samples (%u bytes PCM)\n",
                (unsigned)decoded, (unsigned)(decoded * sizeof(mp3d_sample_t)));

  digitalWrite(PIN_AUDIO_EN, LOW);
  _tts_i2s_install(sampleRate);
  _tts_play_pcm(pcm, decoded, channels);
  free(pcm);

  delay(200);  // drain DMA
  i2s_driver_uninstall(I2S_PORT);
  digitalWrite(PIN_AUDIO_EN, HIGH);
  Serial.println("[TTS] Playback done.");
}

// ── Stream MP3 from URL: download to PSRAM, then decode + play ───────────────
void speakFromUrl(const String& audioUrl) {
  if (audioUrl.isEmpty()) {
    Serial.println("[TTS] No audio URL — skipping playback.");
    return;
  }

  Serial.println("[TTS] Downloading: " + audioUrl);

  WiFiClientSecure sslClient;
  sslClient.setInsecure();
  HTTPClient http;
  http.begin(sslClient, audioUrl);
  http.setTimeout(15000);

  int code = http.GET();
  if (code != 200) {
    Serial.printf("[TTS] HTTP %d — skipping.\n", code);
    http.end();
    return;
  }

  int contentLen = http.getSize();
  const size_t MAX_MP3 = 256 * 1024;  // 256 KB cap (~60 s @ 32 kbps MP3)
  size_t bufCap = (contentLen > 0 && (size_t)contentLen <= MAX_MP3)
                    ? (size_t)contentLen
                    : MAX_MP3;

  uint8_t* mp3Buf = (uint8_t*)ps_malloc(bufCap);
  if (!mp3Buf) {
    Serial.println("[TTS] ps_malloc failed for MP3 download buffer.");
    http.end();
    return;
  }

  WiFiClient* stream = http.getStreamPtr();
  size_t filled = 0;
  uint32_t lastData = millis();
  while (filled < bufCap) {
    int avail = stream->available();
    if (avail > 0) {
      int n = stream->readBytes(mp3Buf + filled,
                                min((size_t)avail, bufCap - filled));
      filled += n;
      lastData = millis();
    } else if (!http.connected() || millis() - lastData > 3000) {
      break;
    }
    yield();
  }
  http.end();
  Serial.printf("[TTS] Downloaded %u bytes of MP3.\n", (unsigned)filled);

  speakFromMp3(mp3Buf, filled);
  free(mp3Buf);
}

// ── Decode base64 MP3 from Firestore inline field and play ───────────────────
void speakFromBase64(const String& b64) {
  if (b64.length() < 60) {
    Serial.println("[TTS] base64 too short — skipping.");
    return;
  }

  // Decode base64 → MP3 bytes in PSRAM
  size_t b64Len = b64.length();
  size_t outCap = (b64Len / 4) * 3 + 4;
  uint8_t* mp3Buf = (uint8_t*)ps_malloc(outCap);
  if (!mp3Buf) {
    Serial.println("[TTS] base64 decode: ps_malloc failed.");
    return;
  }

  const char* p = b64.c_str();
  size_t outLen = 0;
  uint32_t v = 0;
  int bits = 0;
  for (size_t i = 0; i < b64Len; i++) {
    uint8_t c = (uint8_t)p[i];
    if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
    int8_t d;
    if      (c >= 'A' && c <= 'Z') d = c - 'A';
    else if (c >= 'a' && c <= 'z') d = c - 'a' + 26;
    else if (c >= '0' && c <= '9') d = c - '0' + 52;
    else if (c == '+')             d = 62;
    else if (c == '/')             d = 63;
    else                           continue;
    v = (v << 6) | (uint32_t)d;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      mp3Buf[outLen++] = (uint8_t)((v >> bits) & 0xFF);
    }
  }
  Serial.printf("[TTS] base64 decoded %u → %u bytes (MP3).\n",
                (unsigned)b64Len, (unsigned)outLen);

  speakFromMp3(mp3Buf, outLen);
  free(mp3Buf);
}

// ── Auto-dispatch: URL → HTTPS download, anything else → base64 decode ───────
void speakAudio(const String& s) {
  if (s.isEmpty()) {
    Serial.println("[TTS] empty audio — skipping.");
    return;
  }
  if (s.startsWith("http://") || s.startsWith("https://")) {
    speakFromUrl(s);
  } else {
    speakFromBase64(s);
  }
}
