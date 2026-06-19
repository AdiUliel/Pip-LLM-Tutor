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
    .mclk_multiple    = I2S_MCLK_MULTIPLE_384,  // MUST match es8311 clock coeffs (6.144 MHz = 384×16k) and recording config — 256 here made MCLK 4.096 MHz, wrong DAC clock → muffled playback
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
  int contentLen = http.getSize();          // total bytes, or -1 if chunked

  // ── Pipelined download + playback (PSRAM buffer) ──────────────────────────
  // Two failure modes to avoid: (1) playing straight off the TLS socket stutters
  // because SSL arrives in bursts and any gap starves the I2S DMA; (2) waiting
  // for the WHOLE clip to download before playing adds big startup latency.
  // Solution: buffer into PSRAM, but start the DAC after only a small PREBUFFER
  // cushion, then keep pulling from the socket *between* I2S writes. Low latency
  // to first sound AND a cushion that absorbs network jitter.
  const size_t MAX_CLIP    = 1024 * 1024;   // 1 MB safety cap (~32 s @ 16k mono)
  const size_t PREBUFFER   = 32 * 1024;     // ~1 s of audio before first sound (bigger cushion vs. jitter)
  const size_t WRITE_CHUNK = 1024;          // bytes pushed to I2S per write

  size_t capacity = (contentLen > 0) ? (size_t)contentLen : MAX_CLIP;
  if (capacity > MAX_CLIP) capacity = MAX_CLIP;

  uint8_t* clip = (uint8_t*)ps_malloc(capacity);
  if (!clip) {
    Serial.println("[TTS] ps_malloc failed — out of PSRAM.");
    http.end();
    i2s_driver_uninstall(I2S_PORT);
    digitalWrite(PIN_AUDIO_EN, HIGH);
    return;
  }

  size_t   filled   = 0;                 // bytes downloaded into clip
  size_t   played   = WAV_HEADER_SIZE;   // next byte to send (skip 44B WAV header)
  uint32_t lastData = millis();

  while (true) {
    // 1) Drain whatever the socket currently has into the PSRAM buffer
    int avail = stream->available();
    if (avail > 0 && filled < capacity) {
      int n = stream->readBytes(clip + filled, min(avail, (int)(capacity - filled)));
      filled += n;
      lastData = millis();
    }

    bool netDone = (contentLen > 0 && filled >= (size_t)contentLen)
                || (filled >= capacity)
                || (!http.connected() && stream->available() == 0)
                || (millis() - lastData > 3000);   // network idle 3 s → done

    // 2) Once we have a cushion (or the whole short clip arrived), feed the DAC.
    //    Keep reading the socket between writes so we never fall behind.
    if (filled >= PREBUFFER + WAV_HEADER_SIZE || netDone) {
      while (played < filled && (played + WRITE_CHUNK <= filled || netDone)) {
        size_t chunk   = min(WRITE_CHUNK, filled - played);
        size_t written = 0;
        i2s_write(I2S_PORT, clip + played, chunk, &written, portMAX_DELAY);
        played += written;
        int a2 = stream->available();
        if (a2 > 0 && filled < capacity) {
          int n2 = stream->readBytes(clip + filled, min(a2, (int)(capacity - filled)));
          filled += n2;
          lastData = millis();
        }
      }
    }

    if (netDone && played >= filled) break;
    yield();
  }
  http.end();
  free(clip);
  Serial.printf("[TTS] Played %u bytes.\n", (unsigned)filled);

  // Drain DMA buffer
  delay(200);

  i2s_driver_uninstall(I2S_PORT);
  digitalWrite(PIN_AUDIO_EN, HIGH);
  Serial.println("[TTS] Playback done.");
}

// ── Play an in-memory WAV directly (no HTTP) ─────────────────────────────────
// wavBytes must point to a valid RIFF mono LINEAR16 16kHz file (WAV_HEADER_SIZE
// header followed by raw PCM samples). Same I2S setup as speakFromUrl().
// Caller owns the buffer's lifetime; this function does NOT free it.
void speakFromBytes(const uint8_t* wavBytes, size_t len) {
  if (!wavBytes || len <= WAV_HEADER_SIZE) {
    Serial.println("[TTS] in-memory clip too short — skipping playback.");
    return;
  }

  Serial.printf("[TTS] Playing %u bytes from memory.\n", (unsigned)len);
  digitalWrite(PIN_AUDIO_EN, LOW);

  i2s_config_t cfg = {
    .mode             = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate      = SAMPLE_RATE,
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

  size_t played = WAV_HEADER_SIZE;
  const size_t WRITE_CHUNK = 1024;
  while (played < len) {
    size_t chunk   = min(WRITE_CHUNK, len - played);
    size_t written = 0;
    i2s_write(I2S_PORT, wavBytes + played, chunk, &written, portMAX_DELAY);
    played += written;
  }
  delay(200);  // drain DMA
  i2s_driver_uninstall(I2S_PORT);
  digitalWrite(PIN_AUDIO_EN, HIGH);
  Serial.println("[TTS] Playback done (in-memory).");
}

// ── Auto-dispatch: URL → HTTPS stream, anything else → base64 decode ────────
// The Cloud Function now returns base64 audio inline (no Storage round-trip),
// but the device still understands legacy URL responses too. This helper
// picks the right path so call sites don't have to care.
void speakAudio(const String& s); // forward; defined after speakFromBase64

// ── Decode base64 in PSRAM and play ──────────────────────────────────────────
// Convenience wrapper used by the new inline-audio path (the Cloud Function
// returns base64 WAV directly in the exchange doc, eliminating the Storage
// upload + makePublic + HTTPS GET round-trip on every robot reply).
void speakFromBase64(const String& b64) {
  if (b64.length() < 60) {
    Serial.println("[TTS] base64 too short — skipping.");
    return;
  }
  // Decode size = ceil(b64Len * 3 / 4); ignore padding for the alloc estimate.
  size_t b64Len = b64.length();
  size_t outCap = (b64Len / 4) * 3 + 4;
  uint8_t* out = (uint8_t*)ps_malloc(outCap);
  if (!out) {
    Serial.println("[TTS] base64 decode: ps_malloc failed.");
    return;
  }

  // Standard base64 alphabet decode table.
  static const int8_t dec[256] = {
    [0 ... 255] = -1,
    ['A']= 0,['B']= 1,['C']= 2,['D']= 3,['E']= 4,['F']= 5,['G']= 6,['H']= 7,
    ['I']= 8,['J']= 9,['K']=10,['L']=11,['M']=12,['N']=13,['O']=14,['P']=15,
    ['Q']=16,['R']=17,['S']=18,['T']=19,['U']=20,['V']=21,['W']=22,['X']=23,
    ['Y']=24,['Z']=25,
    ['a']=26,['b']=27,['c']=28,['d']=29,['e']=30,['f']=31,['g']=32,['h']=33,
    ['i']=34,['j']=35,['k']=36,['l']=37,['m']=38,['n']=39,['o']=40,['p']=41,
    ['q']=42,['r']=43,['s']=44,['t']=45,['u']=46,['v']=47,['w']=48,['x']=49,
    ['y']=50,['z']=51,
    ['0']=52,['1']=53,['2']=54,['3']=55,['4']=56,['5']=57,['6']=58,['7']=59,
    ['8']=60,['9']=61,
    ['+']=62,['/']=63,
  };

  const char* p = b64.c_str();
  size_t outLen = 0;
  uint32_t v = 0;
  int bits = 0;
  for (size_t i = 0; i < b64Len; i++) {
    uint8_t c = (uint8_t)p[i];
    if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
    int8_t d = dec[c];
    if (d < 0) continue;        // skip stray chars
    v = (v << 6) | (uint32_t)d;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out[outLen++] = (uint8_t)((v >> bits) & 0xFF);
    }
  }
  Serial.printf("[TTS] base64 decoded %u → %u bytes.\n",
                (unsigned)b64Len, (unsigned)outLen);
  speakFromBytes(out, outLen);
  free(out);
}

// Definition of the auto-dispatch forward-declared above.
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
