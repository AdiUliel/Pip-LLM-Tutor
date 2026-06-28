#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// TTS Player — streams MP3 from a URL via the ESP32-audioI2S `Audio` library.
// This mirrors the board's official "Example_30_ai_chat", which plays its TTS
// with audio.connecttohost(url), so it is known to work with this board's ES8311.
//
// The Cloud Function synthesizes speech (Google TTS, MP3 @ 16 kHz mono), uploads
// it to Firebase Storage, and returns a public https URL. We hand that URL to the
// Audio library, which downloads + decodes + drives the ES8311 over I2S.
//
// ┌─ REQUIRED LIBRARY ─────────────────────────────────────────────────────────┐
// │ ESP32-audioI2S **v2.0.6** (the last release on the LEGACY driver/i2s.h, so   │
// │ it compiles on Arduino-ESP32 core 2.0.x). v3.x needs core 3.0.0+ and will    │
// │ NOT compile here. Install from the upstream tag:                             │
// │   https://github.com/schreibfaul1/ESP32-audioI2S/releases/tag/2.0.6          │
// │                                                                              │
// │ REQUIRED ONE-TIME PATCH to that library (its constructor leaves the I2S      │
// │ MCLK multiple at the default 256×fs, but this board's ES8311 is clocked at   │
// │ 384×fs — 256 gives muffled/garbled audio). In  ...\ESP32-audioI2S\src\       │
// │ Audio.cpp, inside `Audio::Audio(...)`, where m_i2s_config.* is filled in,    │
// │ set/add these two lines:                                                     │
// │     m_i2s_config.use_apll       = true;                                      │
// │     m_i2s_config.mclk_multiple  = I2S_MCLK_MULTIPLE_384;                     │
// │ (re-apply if you ever reinstall the library.)                               │
// └─────────────────────────────────────────────────────────────────────────────┘
//
// I2S ownership: the Audio ctor calls i2s_driver_install and the dtor calls
// i2s_driver_uninstall (both LEGACY driver — same as the mic recorder). We create
// the object per playback and delete it when done, so the recorder can reclaim
// I2S_NUM_0 for the next recording. Only ONE owner is active at a time — every
// caller stops recording before speakAudio().
// ─────────────────────────────────────────────────────────────────────────────

#include <Audio.h>
#include "pins.h"

#ifndef TTS_VOLUME
#define TTS_VOLUME        18      // 0..21 (ESP32-audioI2S volume scale)
#endif
#ifndef TTS_MAX_PLAY_MS
#define TTS_MAX_PLAY_MS   30000   // safety net: never let a stalled stream wedge the loop
#endif

// ── Stream + play an audio URL (MP3) ─────────────────────────────────────────
void speakFromUrl(const String& audioUrl) {
  if (audioUrl.isEmpty()) {
    Serial.println("[TTS] No audio URL — skipping playback.");
    return;
  }
  Serial.println("[TTS] Streaming: " + audioUrl);

  digitalWrite(PIN_AUDIO_EN, LOW);            // speaker amp on

  // Grab the I2S port for this reply (default ctor → external DAC, I2S_NUM_0;
  // freed by the dtor on delete below). NOTE the v2.0.6 setPinout signature:
  //   setPinout(BCLK, LRC, DOUT, DIN=NO_CHANGE, MCK=NO_CHANGE)
  // MCLK is the 5th argument — passing it as the 4th would route it to DIN and
  // leave the ES8311 without a master clock (silent).
  Audio* audio = new Audio();
  audio->setPinout(PIN_I2S_BCLK, PIN_I2S_LRCK, PIN_I2S_DOUT,
                   I2S_PIN_NO_CHANGE, PIN_I2S_MCLK);
  audio->setVolume(TTS_VOLUME);
  audio->forceMono(true);                     // single speaker on this board

  if (!audio->connecttohost(audioUrl.c_str())) {
    Serial.println("[TTS] connecttohost failed — skipping.");
    delete audio;
    digitalWrite(PIN_AUDIO_EN, HIGH);
    return;
  }

  // Pump the decoder until the stream ends (isRunning() goes false at EOF) or the
  // safety timeout fires. v2.0.6 has no audio_eof_stream callback, so we rely on
  // isRunning().
  uint32_t start = millis();
  while (audio->isRunning() && (millis() - start) < TTS_MAX_PLAY_MS) {
    audio->loop();
    delay(1);                                 // yield to WiFi / idle
  }

  audio->stopSong();
  delete audio;                               // releases the I2S port (i2s_driver_uninstall)

  digitalWrite(PIN_AUDIO_EN, HIGH);           // amp off
  Serial.println("[TTS] Playback done.");
}

// ── Dispatch ─────────────────────────────────────────────────────────────────
// Everything the backend returns is an https Storage URL; connecttohost streams
// and decodes the MP3.
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
