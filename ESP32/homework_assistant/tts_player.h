#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// TTS Player — streams OPUS (or MP3/WAV) from a URL via the ESP32-audioI2S
// `Audio` library. This is the board-blessed playback path: the LCDWIKI 2.8"
// ESP32-S3 official "Example_30_ai_chat" plays its TTS exactly this way
// (audio.connecttohost(url)), so it is known to work with this board's ES8311.
//
// The Cloud Function synthesizes speech (Google TTS, OGG_OPUS @ 16 kHz mono),
// uploads it to Firebase Storage, and returns a public https URL. We hand that
// URL to the Audio library, which downloads + decodes + drives the ES8311 over
// I2S. No manual download/decode (the old minimp3 path) is needed.
//
// REQUIRES the ESP32-audioI2S library (v3.x) installed in the Arduino IDE:
//   bundled with the board at:  .../Install libraries/ESP32-audioI2S
//   or upstream:                https://github.com/schreibfaul1/ESP32-audioI2S
//
// I2S ownership: the Audio object grabs the I2S port in its constructor
// (i2s_new_channel, MCLK ×384 — matching our ES8311 clock config) and releases
// it in its destructor (i2s_del_channel). We create it per playback and delete
// it when done, so the legacy mic recorder (driver/i2s.h, in the .ino) can
// reclaim the same port (I2S_NUM_0) for the next recording. Only ONE owner is
// active at a time — every caller stops recording before calling speakAudio().
// ─────────────────────────────────────────────────────────────────────────────

#include <Audio.h>
#include "pins.h"

#ifndef TTS_VOLUME
#define TTS_VOLUME        18      // 0..21 (ESP32-audioI2S volume scale)
#endif
#ifndef TTS_MAX_PLAY_MS
#define TTS_MAX_PLAY_MS   30000   // safety net: never let a stalled stream wedge the loop
#endif

// The Audio library calls this (weak) hook when a stream finishes.
static volatile bool _tts_eof = false;
void audio_eof_stream(const char* info) { _tts_eof = true; }

// ── Stream + play an audio URL (OPUS / MP3 / WAV — auto-detected) ─────────────
void speakFromUrl(const String& audioUrl) {
  if (audioUrl.isEmpty()) {
    Serial.println("[TTS] No audio URL — skipping playback.");
    return;
  }
  Serial.println("[TTS] Streaming: " + audioUrl);

  digitalWrite(PIN_AUDIO_EN, LOW);            // speaker amp on

  // Grab the I2S port for the duration of this reply (freed on delete below).
  Audio* audio = new Audio(I2S_NUM_0);
  audio->setPinout(PIN_I2S_BCLK, PIN_I2S_LRCK, PIN_I2S_DOUT, PIN_I2S_MCLK);
  audio->setVolume(TTS_VOLUME);
  audio->forceMono(true);                     // single speaker on this board

  _tts_eof = false;
  if (!audio->connecttohost(audioUrl.c_str())) {
    Serial.println("[TTS] connecttohost failed — skipping.");
    delete audio;
    digitalWrite(PIN_AUDIO_EN, HIGH);
    return;
  }

  // Pump the decoder until the stream ends (or the safety timeout fires).
  uint32_t start = millis();
  while (!_tts_eof && audio->isRunning() && (millis() - start) < TTS_MAX_PLAY_MS) {
    audio->loop();
    delay(1);                                 // yield to WiFi / idle
  }

  audio->stopSong();
  delete audio;                               // releases the I2S port (i2s_del_channel)

  digitalWrite(PIN_AUDIO_EN, HIGH);           // amp off
  Serial.println("[TTS] Playback done.");
}

// ── Dispatch ─────────────────────────────────────────────────────────────────
// Everything the backend returns is now an https Storage URL; connecttohost
// auto-detects the codec (opus/mp3/wav) from the HTTP response, so a single path
// covers them all.
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
