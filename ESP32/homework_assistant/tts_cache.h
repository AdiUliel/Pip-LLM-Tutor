#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// TTS cache — store synthesised MP3s on the SD card, keyed by a hash of the text.
//
// WHY: every spoken phrase otherwise costs a fresh-TLS synthesizeSpeech call +
// a fresh-TLS Storage download (~2–4 s total on this board, handshake-dominated).
// For STATIC / repeated phrases — the boot prompts ("מי כאן?", "לא הכרתי את השם",
// "חשבון או אנגלית?") and reprompts — that audio is byte-for-byte identical every
// time (Google TTS is deterministic for a given text+voice). So we cache it once
// and replay from SD in tens of ms. The per-turn FEEDBACK is unique each time and
// is NOT cacheable — that path is handled by processTurn (inline audio) instead.
//
// SD is on a DEDICATED SD_MMC (SDIO) bus on this board — pins below, taken from
// the official Example_05_show_SD_jpg_picture demo. They do NOT collide with the
// audio I2S pins, the codec I2C, the TFT SPI, the button, or the RGB LED, so the
// card coexists with everything.
//
// FAILS SAFE: if the card is missing / unreadable / not FAT32, caching is simply
// disabled and every phrase falls back to the cloud path. Nothing breaks.
// NOTE: format the card as **FAT32** — ESP-IDF's FATFS does not mount exFAT
// (Windows defaults 32 GB+ cards to exFAT; reformat with a FAT32 tool).
//
// Requires firebase_client.h (cloudSynthesizeSpeech) and tts_player.h
// (_ttsDownloadToPSRAM / speakFromBuffer / g_ttsFirstSampleMs) to be included first.
// ─────────────────────────────────────────────────────────────────────────────

#include "SD_MMC.h"
#include <FS.h>
#include <MD5Builder.h>

// SD_MMC (SDIO) pins for this board — from the official SD JPG demo.
#define SDCACHE_CLK 38
#define SDCACHE_CMD 40
#define SDCACHE_D0  39
#define SDCACHE_D1  41
#define SDCACHE_D2  48
#define SDCACHE_D3  47

static bool g_sdReady = false;

// Mount the card and ensure /tts exists. Safe to call once from setup(); returns
// false (and disables caching) on any failure.
bool sdCacheBegin() {
  if (g_sdReady) return true;
  if (!SD_MMC.setPins(SDCACHE_CLK, SDCACHE_CMD, SDCACHE_D0, SDCACHE_D1, SDCACHE_D2, SDCACHE_D3)) {
    Serial.println("[SD] setPins failed — TTS cache disabled.");
    return false;
  }
  if (!SD_MMC.begin()) {
    Serial.println("[SD] mount failed (missing card or not FAT32) — TTS cache disabled.");
    return false;
  }
  if (!SD_MMC.exists("/tts")) SD_MMC.mkdir("/tts");
  g_sdReady = true;
  Serial.printf("[SD] mounted, %llu MB — TTS cache ready at /tts\n",
                SD_MMC.cardSize() / (1024ULL * 1024ULL));
  return true;
}

// Cache path = /tts/<md5(text)>.mp3.
static String _ttsKeyPath(const String& text) {
  MD5Builder md5;
  md5.begin();
  md5.add(text);
  md5.calculate();
  return "/tts/" + md5.toString() + ".mp3";
}

// Read a cached MP3 into a fresh PSRAM buffer (caller frees). nullptr if absent.
static uint8_t* _sdReadMp3(const String& path, size_t* outLen) {
  *outLen = 0;
  if (!g_sdReady || !SD_MMC.exists(path)) return nullptr;
  File f = SD_MMC.open(path, FILE_READ);
  if (!f) return nullptr;
  size_t sz = f.size();
  if (sz < 4 || sz > 256 * 1024) { f.close(); return nullptr; }
  uint8_t* buf = (uint8_t*)ps_malloc(sz);
  if (!buf) { f.close(); return nullptr; }
  size_t rd = f.read(buf, sz);
  f.close();
  if (rd != sz) { free(buf); return nullptr; }
  *outLen = sz;
  return buf;
}

static void _sdWriteMp3(const String& path, const uint8_t* buf, size_t len) {
  if (!g_sdReady || !buf || len < 4) return;
  File f = SD_MMC.open(path, FILE_WRITE);
  if (!f) { Serial.println("[SD] open-for-write failed: " + path); return; }
  size_t wr = f.write(buf, len);
  f.close();
  if (wr != len) Serial.println("[SD] short write: " + path);
}

// Speak a fixed/repeated phrase, caching its MP3 on SD keyed by the text hash.
//   HIT  → read from SD (~tens of ms), skipping BOTH the synth call and download.
//   MISS → synthesise via cloud, play, and save for next time.
// Returns true if something was played. Falls back to the pure cloud path when
// the SD card isn't available, so it's always safe to use in place of
// cloudSynthesizeSpeech()+speakAudio().
bool speakTextCached(const String& text) {
  if (text.isEmpty()) return false;

  if (g_sdReady) {
    const String path = _ttsKeyPath(text);
    size_t len = 0;
    uint8_t* cached = _sdReadMp3(path, &len);
    if (cached) {
      Serial.println("[SD] cache HIT: " + text);
      g_ttsFirstSampleMs = 0;
      uint32_t t0 = millis();
      speakFromBuffer(cached, len);
      if (g_ttsFirstSampleMs)
        Serial.printf("[lat]   audio(SD): decode-to-first-sample=%lu ms\n",
                      (unsigned long)(g_ttsFirstSampleMs - t0));
      free(cached);
      return true;
    }
    Serial.println("[SD] cache MISS: " + text);
  }

  // Miss (or no SD): synthesise via cloud, download bytes, play, and cache.
  String url = cloudSynthesizeSpeech(text);
  if (url.isEmpty()) return false;
  size_t len = 0;
  uint8_t* buf = _ttsDownloadToPSRAM(url, &len);
  if (!buf) return false;
  bool played = false;
  if (len >= 4) {
    if (g_sdReady) _sdWriteMp3(_ttsKeyPath(text), buf, len);
    g_ttsFirstSampleMs = 0;
    speakFromBuffer(buf, len);
    played = true;
  }
  free(buf);
  return played;
}
