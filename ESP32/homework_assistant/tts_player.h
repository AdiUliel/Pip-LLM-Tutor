#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// TTS Player — download MP3 → decode with the ESP32-audioI2S **helix** MP3
// decoder → play PCM through OUR OWN I2S in the exact format that makes sound on
// this board (mono ONLY_LEFT frame, MCLK ×384, APLL on — the same config the
// old, audible WAV playback used).
//
// Why not just use audio.connecttohost()? On this board the Audio class outputs
// a STEREO (RIGHT_LEFT) frame, which our ES8311 init reproduces as silence (only
// the ONLY_LEFT mono frame is audible here). So we borrow only the library's
// MP3 *decoder* and keep our proven I2S output path.
//
// REQUIRES: ESP32-audioI2S **v2.0.6** installed (legacy driver, core 2.0.x). We
// include <Audio.h> only so the Arduino builder compiles+links the library's
// helix decoder (mp3_decoder/*.cpp); we never instantiate the Audio class, so
// the library never touches I2S. The earlier "mclk_multiple" library patch is
// NOT needed for this path (we set the I2S clock ourselves below) — but leaving
// it in place is harmless.
// ─────────────────────────────────────────────────────────────────────────────

#include <Audio.h>                     // pulls the library in so helix gets linked
#include "mp3_decoder/mp3_decoder.h"   // MP3Decode / MP3FindSyncWord / MP3Get*
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <driver/i2s.h>
#include "pins.h"

// [lat] Wall-clock (millis) at which the FIRST audio sample is pushed to I2S,
// i.e. the instant the robot actually starts speaking. 0 until set per call.
// processCapturedAnswer() reads this to compute end-to-end "record → first sound".
volatile uint32_t g_ttsFirstSampleMs = 0;

// ── I2S playback config: the proven-audible WAV config (ONLY_LEFT, MCLK ×384) ──
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

// Play one decoded frame (int16, interleaved) to the mono ONLY_LEFT I2S.
// `totalSamps` = samples across all channels (MP3GetOutputSamps()).
static void _tts_play_frame(const int16_t* pcm, int totalSamps, int channels) {
  size_t w = 0;
  if (channels == 1) {
    i2s_write(I2S_PORT, pcm, (size_t)totalSamps * sizeof(int16_t), &w, portMAX_DELAY);
  } else {
    static int16_t mono[1152];                 // 1 MP3 frame = 1152 samples/ch max
    int frames = totalSamps / 2;
    for (int i = 0; i < frames; i++) mono[i] = pcm[i * 2];   // keep left channel
    i2s_write(I2S_PORT, mono, (size_t)frames * sizeof(int16_t), &w, portMAX_DELAY);
  }
}

// ── Download MP3 (to PSRAM), decode with helix, play ─────────────────────────
void speakFromUrl(const String& audioUrl) {
  if (audioUrl.isEmpty()) {
    Serial.println("[TTS] No audio URL — skipping playback.");
    return;
  }
  Serial.println("[TTS] Downloading: " + audioUrl);
  g_ttsFirstSampleMs = 0;              // [lat] reset; set when first frame plays
  uint32_t _ttsStart = millis();       // [lat] entry to playback (URL in hand)

  // 1) Download the whole MP3 into PSRAM (proven HTTPS path).
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
  const size_t MAX_MP3 = 256 * 1024;
  size_t bufCap = (contentLen > 0 && (size_t)contentLen <= MAX_MP3) ? (size_t)contentLen : MAX_MP3;
  uint8_t* mp3Buf = (uint8_t*)ps_malloc(bufCap);
  if (!mp3Buf) {
    Serial.println("[TTS] ps_malloc failed for MP3 buffer.");
    http.end();
    return;
  }
  WiFiClient* stream = http.getStreamPtr();
  size_t filled = 0;
  uint32_t lastData = millis();
  while (filled < bufCap) {
    int avail = stream->available();
    if (avail > 0) {
      int n = stream->readBytes(mp3Buf + filled, min((size_t)avail, bufCap - filled));
      filled += n;
      lastData = millis();
    } else if (!http.connected() || millis() - lastData > 3000) {
      break;
    }
    yield();
  }
  http.end();
  Serial.printf("[TTS] Downloaded %u bytes of MP3.\n", (unsigned)filled);
  if (filled < 4) { free(mp3Buf); return; }

  // 2) Decode with helix, frame by frame, and stream PCM to I2S.
  if (!MP3Decoder_AllocateBuffers()) {
    Serial.println("[TTS] MP3Decoder_AllocateBuffers failed.");
    free(mp3Buf);
    return;
  }

  static int16_t pcm[2304];          // 1152 samples/ch * 2 ch (max per frame)
  uint8_t* p = mp3Buf;
  int remaining = (int)filled;
  bool i2sUp = false;
  int32_t peak = 0;
  uint32_t framesPlayed = 0;

  digitalWrite(PIN_AUDIO_EN, LOW);   // amp on

  while (remaining > 0) {
    int sync = MP3FindSyncWord(p, remaining);
    if (sync < 0) break;             // no further frames
    p += sync; remaining -= sync;

    int bytesLeft = remaining;
    int ret = MP3Decode(p, &bytesLeft, pcm, 0);
    int consumed = remaining - bytesLeft;
    if (consumed <= 0) consumed = 2; // guard against stall
    p += consumed; remaining -= consumed;
    if (ret < 0) continue;           // skip a bad frame, keep going

    int ch    = MP3GetChannels();
    int rate  = MP3GetSampRate();
    int samps = MP3GetOutputSamps(); // total across channels
    if (samps <= 0) continue;

    if (!i2sUp) {
      _tts_i2s_install(rate > 0 ? rate : SAMPLE_RATE);
      i2sUp = true;
      g_ttsFirstSampleMs = millis();   // [lat] first sample out = robot starts speaking
      Serial.printf("[TTS] MP3: %d Hz, %d ch\n", rate, ch);
      Serial.printf("[lat]   audio: download+decode-to-first-sample=%lu ms\n",
                    (unsigned long)(g_ttsFirstSampleMs - _ttsStart));
    }
    for (int i = 0; i < samps; i++) { int32_t a = pcm[i] < 0 ? -pcm[i] : pcm[i]; if (a > peak) peak = a; }
    _tts_play_frame(pcm, samps, ch);
    framesPlayed++;
    yield();
  }

  MP3Decoder_FreeBuffers();
  free(mp3Buf);

  Serial.printf("[TTS] Decoded %u frames, peak=%ld/32767\n", (unsigned)framesPlayed, (long)peak);

  if (i2sUp) {
    delay(200);                      // drain DMA
    i2s_driver_uninstall(I2S_PORT);
  }
  digitalWrite(PIN_AUDIO_EN, HIGH);  // amp off
  Serial.println("[TTS] Playback done.");
}

// ── Dispatch ─────────────────────────────────────────────────────────────────
void speakAudio(const String& s) {
  if (s.isEmpty()) {
    Serial.println("[TTS] empty audio — skipping.");
    return;
  }
  if (s.startsWith("http://") || s.startsWith("https://")) {
    speakFromUrl(s);
  } else {
    Serial.println("[TTS] non-URL audio payload is no longer supported — skipping.");
  }
}
