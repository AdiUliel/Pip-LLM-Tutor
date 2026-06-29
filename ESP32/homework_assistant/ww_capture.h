#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Wake-word DATA CAPTURE mode — record real "hey pip" + noise to the SD card as
// 16 kHz mono WAV, to RETRAIN the on-device model on YOUR mic / room / voice.
//
// This is a SEPARATE build mode (exactly like WW_TEST_MODE): when
// WW_CAPTURE_MODE==1 the device boots straight into this recorder and never runs
// the tutor loop / WiFi / Firebase. Flash it, collect your clips, copy the /ww
// folder off the SD card, then reflash the normal firmware (WW_CAPTURE_MODE 0).
//
// WHY WAV (not MP3): these files are TRAINING DATA, read offline on the PC — the
// trainer wants lossless raw PCM (MP3 is lossy and would add compression
// artifacts the real mic never produced). It has ZERO effect on real-time
// "hey pip" detection: that path reads live I2S samples from RAM and never
// touches a file or this module. (The board also has no MP3 *encoder* — only the
// TTS *decoder* — so on-device MP3 here isn't even possible.)
//
// Files written (all 16 kHz / mono / 16-bit PCM WAV):
//   /ww/pos/pNNNN.wav  — one "hey pip" each            (key 'p' or the button)
//   /ww/neg/nNNNN.wav  — one short non-wake clip       (key 'n')
//   /ww/neg/cNNNN.wav  — one LONG continuous ambient   (key 'c', ~20 s)
//
// Serial commands (Serial Monitor @115200; send a single key):
//   p = record ONE positive  "hey pip"      (WWCAP_POS_MS)
//   n = record ONE negative  short clip      (WWCAP_NEG_MS)
//   c = record ONE negative  LONG ambient     (WWCAP_CONT_MS) ← room/bed/other-speech
//   l = list how many clips captured + SD free space
// The hardware button (if wired on PIN_BTN) also records a POSITIVE per press.
//
// Reuses initES8311() + i2s_start_recording()/i2s_stop_recording() + SD_MMC, so
// the mic path is byte-for-byte what the runtime detector hears.
// ─────────────────────────────────────────────────────────────────────────────

#include <Arduino.h>
#include <driver/i2s.h>
#include "SD_MMC.h"
#include <FS.h>
#include "pins.h"

// Reused from the main sketch (same ES8311 clocking the detector/recorder use).
void i2s_start_recording();
void i2s_stop_recording();
void faceTick();

// Clip lengths (ms). "hey pip" is ~0.6 s; 1.8 s leaves margin to start speaking.
#ifndef WWCAP_POS_MS
#define WWCAP_POS_MS  1800
#endif
#ifndef WWCAP_NEG_MS
#define WWCAP_NEG_MS  1500
#endif
#ifndef WWCAP_CONT_MS
#define WWCAP_CONT_MS 20000     // 20 s continuous ambient/noise per 'c'
#endif

static int wwcap_mic_ch = 0;    // stereo slot carrying the mic (ES8311 is mono)

static String _wwcap4(int n) { char b[8]; snprintf(b, sizeof(b), "%04d", n); return String(b); }

// ── Write a 44-byte canonical PCM WAV header (mono/16-bit @ SAMPLE_RATE) ──────
// ESP32-S3 is little-endian, so the raw uint writes are already correct on disk.
static void _wwcapWriteWavHeader(File& f, uint32_t dataBytes) {
  uint32_t sr = SAMPLE_RATE; uint16_t ch = 1, bits = 16;
  uint32_t byteRate = sr * ch * bits / 8; uint16_t blockAlign = ch * bits / 8;
  uint32_t riffLen = 36 + dataBytes, fmtLen = 16; uint16_t fmtTag = 1;
  f.write((const uint8_t*)"RIFF", 4); f.write((uint8_t*)&riffLen, 4);
  f.write((const uint8_t*)"WAVE", 4);
  f.write((const uint8_t*)"fmt ", 4); f.write((uint8_t*)&fmtLen, 4);
  f.write((uint8_t*)&fmtTag, 2);   f.write((uint8_t*)&ch, 2);
  f.write((uint8_t*)&sr, 4);       f.write((uint8_t*)&byteRate, 4);
  f.write((uint8_t*)&blockAlign, 2); f.write((uint8_t*)&bits, 2);
  f.write((const uint8_t*)"data", 4); f.write((uint8_t*)&dataBytes, 4);
}

// Pick the stereo slot that carries the mic (same logic as the recorder/detector).
static void _wwcapCalibrateChannel() {
  const int FR = 1024; static int16_t tmp[FR * 2]; size_t br = 0;
  i2s_read(I2S_PORT, (char*)tmp, sizeof(tmp), &br, portMAX_DELAY);
  size_t fr = br / 4; int64_t s0 = 0, s1 = 0;
  for (size_t i = 0; i < fr; i++) { s0 += (int64_t)tmp[i*2]*tmp[i*2]; s1 += (int64_t)tmp[i*2+1]*tmp[i*2+1]; }
  wwcap_mic_ch = (s1 > s0) ? 1 : 0;
}

// Next free index in `dir` for files starting with `prefix` (e.g. 'p' → p0007.wav).
static int _wwcapNextIndex(const char* dir, char prefix) {
  int maxIdx = -1;
  File d = SD_MMC.open(dir);
  if (d && d.isDirectory()) {
    for (File e = d.openNextFile(); e; e = d.openNextFile()) {
      String name = e.name();
      int slash = name.lastIndexOf('/'); if (slash >= 0) name = name.substring(slash + 1);
      if (name.length() > 1 && name[0] == prefix) {
        int dot = name.indexOf('.');
        int v = name.substring(1, dot > 0 ? dot : name.length()).toInt();
        if (v > maxIdx) maxIdx = v;
      }
      e.close();
    }
  }
  if (d) d.close();
  return maxIdx + 1;
}

// Record `ms` of mono mic audio to `path` as WAV. Streams to SD; prints level.
static bool _wwcapRecord(const String& path, uint32_t ms) {
  uint32_t totalSamples = (uint32_t)SAMPLE_RATE * ms / 1000;
  File f = SD_MMC.open(path, FILE_WRITE);
  if (!f) { Serial.println("[WWCAP] open-for-write FAILED: " + path); return false; }
  _wwcapWriteWavHeader(f, totalSamples * 2);

  i2s_start_recording();
  static int16_t st[512 * 2];   // stereo read buffer
  static int16_t mono[512];     // mono out

  // Discard ~120 ms so codec settling + stale DMA don't taint the clip.
  { uint32_t p0 = millis(); size_t br = 0;
    while (millis() - p0 < 120) i2s_read(I2S_PORT, (char*)st, sizeof(st), &br, portMAX_DELAY); }

  uint32_t got = 0; int64_t sumSq = 0; int32_t peak = 0; uint32_t t0 = millis();
  while (got < totalSamples) {
    size_t want = totalSamples - got; if (want > 512) want = 512;
    size_t br = 0;
    i2s_read(I2S_PORT, (char*)st, want * 4, &br, portMAX_DELAY);
    int frames = br / 4;
    for (int i = 0; i < frames; i++) {
      int16_t s = st[i * 2 + wwcap_mic_ch];
      mono[i] = s;
      int32_t a = s < 0 ? -s : s; if (a > peak) peak = a;
      sumSq += (int64_t)s * s;
    }
    f.write((uint8_t*)mono, frames * 2);
    got += frames;
    faceTick();
  }
  i2s_stop_recording();
  f.close();

  float rms = (got > 0) ? sqrtf((float)(sumSq / (int64_t)got)) : 0;
  const char* tag = (peak >= 32000) ? "  ! CLIPPING (move back)"
                  : (rms < 150)     ? "  ! very quiet (move closer / check mic)"
                                    : "  ok";
  Serial.printf("[WWCAP] saved %s  (%.1fs)  RMS=%.0f  peak=%ld/32767%s\n",
                path.c_str(), (millis() - t0) / 1000.0f, (double)rms, (long)peak, tag);
  return true;
}

// ── Capture mode entry point (never returns) ─────────────────────────────────
inline void wakeWordRunCaptureMode() {
  Serial.println("\n========== WAKE-WORD DATA CAPTURE ==========");

  // Amp OFF so the speaker can't bleed into the recordings.
  pinMode(PIN_AUDIO_EN, OUTPUT); digitalWrite(PIN_AUDIO_EN, HIGH);

  if (SD_MMC.cardType() == CARD_NONE) {
    Serial.println("[WWCAP] No SD card mounted (need a FAT32 card). Halting.");
    while (true) { faceTick(); delay(300); }
  }
  if (!SD_MMC.exists("/ww"))     SD_MMC.mkdir("/ww");
  if (!SD_MMC.exists("/ww/pos")) SD_MMC.mkdir("/ww/pos");
  if (!SD_MMC.exists("/ww/neg")) SD_MMC.mkdir("/ww/neg");

  // Button (optional): IO3 reads press, IO2 acts as GND.
  pinMode(PIN_BTN_GND, OUTPUT); digitalWrite(PIN_BTN_GND, LOW);
  pinMode(PIN_BTN, INPUT_PULLUP);

  // Find the mic channel once.
  i2s_start_recording(); _wwcapCalibrateChannel(); i2s_stop_recording();
  Serial.printf("[WWCAP] mic channel = %d\n", wwcap_mic_ch);

  int posIdx = _wwcapNextIndex("/ww/pos", 'p');
  int negSIdx = _wwcapNextIndex("/ww/neg", 'n');
  int negLIdx = _wwcapNextIndex("/ww/neg", 'c');

  Serial.println("\nGoal: ~100-150 'hey pip' (vary TONE / SPEED / DISTANCE), and");
  Serial.println("~8-10 min of 'c' ambient incl. the noises that false-trigger it.");
  Serial.println("Commands (send a single key @115200):");
  Serial.println("  p = positive \"hey pip\"      n = short negative");
  Serial.println("  c = LONG ambient (~20 s)      l = list counts");
  Serial.println("  (hardware button also records a positive)");
  Serial.printf ("Next indices: pos=%d  neg(n)=%d  neg(c)=%d\n", posIdx, negSIdx, negLIdx);

  bool btnPrev = (digitalRead(PIN_BTN) == LOW);
  while (true) {
    faceTick();

    if (Serial.available()) {
      char ch = Serial.read();
      if (ch == 'p' || ch == 'P') {
        Serial.println("\n* Say \"hey pip\" now...");  delay(200);
        if (_wwcapRecord("/ww/pos/p" + _wwcap4(posIdx) + ".wav", WWCAP_POS_MS)) posIdx++;
      } else if (ch == 'n' || ch == 'N') {
        Serial.println("\n* Short NEGATIVE (say anything that's NOT \"hey pip\")..."); delay(200);
        if (_wwcapRecord("/ww/neg/n" + _wwcap4(negSIdx) + ".wav", WWCAP_NEG_MS)) negSIdx++;
      } else if (ch == 'c' || ch == 'C') {
        Serial.printf("\n* %d s CONTINUOUS ambient — move around, make the false-trigger noises...\n", WWCAP_CONT_MS / 1000);
        delay(200);
        if (_wwcapRecord("/ww/neg/c" + _wwcap4(negLIdx) + ".wav", WWCAP_CONT_MS)) negLIdx++;
      } else if (ch == 'l' || ch == 'L') {
        uint64_t freeMB = (SD_MMC.totalBytes() - SD_MMC.usedBytes()) / (1024ULL * 1024ULL);
        Serial.printf("[WWCAP] captured: pos=%d  neg-short=%d  neg-long=%d   SD free=%llu MB\n",
                      posIdx, negSIdx, negLIdx, (unsigned long long)freeMB);
      } else if (ch != '\n' && ch != '\r' && ch != ' ') {
        Serial.println("keys: p / n / c / l");
      }
    }

    bool btnNow = (digitalRead(PIN_BTN) == LOW);
    if (btnNow && !btnPrev) {
      Serial.println("\n* [button] Say \"hey pip\" now..."); delay(200);
      if (_wwcapRecord("/ww/pos/p" + _wwcap4(posIdx) + ".wav", WWCAP_POS_MS)) posIdx++;
    }
    btnPrev = btnNow;
    delay(5);
  }
}
