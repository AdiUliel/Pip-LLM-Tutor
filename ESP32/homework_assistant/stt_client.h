#pragma once
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "secrets.h"

// Minimal read-only Stream over a PSRAM buffer.
// HTTPClient::sendRequest(method, stream, size) reads in 4KB chunks,
// avoiding the single large write() that fails for 150KB+ SSL payloads.
class MemStream : public Stream {
  const uint8_t* _buf;
  size_t _len, _pos;
public:
  MemStream(const uint8_t* buf, size_t len) : _buf(buf), _len(len), _pos(0) {}
  int  available() override { return (int)min(_len - _pos, (size_t)1024); } // cap → HTTPClient reads in 1KB chunks
  int  read()      override { return _pos < _len ? _buf[_pos++] : -1; }
  int  peek()      override { return _pos < _len ? _buf[_pos]   : -1; }
  size_t write(uint8_t) override { return 0; }
};

// ─────────────────────────────────────────────────────────────────────────────
// Speech-to-Text client
// Sends raw PCM audio to our Cloud Function STT proxy (transcribeAudio).
// The Cloud Function forwards to Google STT using ADC — no API key needed here.
// ─────────────────────────────────────────────────────────────────────────────

// Base64 encode helper (ESP32 has no built-in, use a simple table impl)
static const char b64chars[] =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// Encode directly into a PSRAM-allocated buffer.
// Caller must free() the returned pointer.
// Returns nullptr on allocation failure.
char* base64EncodePSRAM(const uint8_t* data, size_t len, size_t& outLen) {
  outLen = (len / 3 + 1) * 4 + 1;
  char* out = (char*)ps_malloc(outLen);
  if (!out) return nullptr;
  size_t pos = 0;
  for (size_t i = 0; i < len; i += 3) {
    uint32_t b = ((uint32_t)data[i] << 16)
               | (i+1 < len ? (uint32_t)data[i+1] << 8 : 0)
               | (i+2 < len ? (uint32_t)data[i+2]      : 0);
    out[pos++] = b64chars[(b >> 18) & 0x3F];
    out[pos++] = b64chars[(b >> 12) & 0x3F];
    out[pos++] = (i+1 < len) ? b64chars[(b >>  6) & 0x3F] : '=';
    out[pos++] = (i+2 < len) ? b64chars[(b      ) & 0x3F] : '=';
  }
  out[pos] = '\0';
  outLen = pos;
  return out;
}

// ── Send audio to the Cloud Function STT proxy ────────────────────────────────
// audio     : raw PCM bytes (16-bit, 16kHz, mono)
// audioLen  : number of bytes
// idToken   : Firebase anonymous auth token
// Returns the transcribed text, or "" on failure.
String transcribeAudio(const uint8_t* audio, size_t audioLen, const String& idToken) {
  // Build the Cloud Function URL
  // Format: https://{region}-{projectId}.cloudfunctions.net/transcribeAudio
  // OR (Gen 2): https://transcribeaudio-{hash}-{region}.a.run.app
  // Find the exact URL in Firebase Console → Functions → transcribeAudio → URL
  String url = "https://" CLOUD_FUNCTIONS_REGION "-" FIREBASE_PROJECT_ID
               ".cloudfunctions.net/transcribeAudio";

  Serial.printf("[STT] Encoding %u bytes of audio...\n", audioLen);

  // Encode into PSRAM — keeps the 150KB+ base64 out of the regular heap
  size_t b64Len = 0;
  char* b64buf = base64EncodePSRAM(audio, audioLen, b64Len);
  if (!b64buf) {
    Serial.println("[STT] PSRAM alloc failed for base64");
    return "";
  }
  Serial.printf("[STT] Base64 size: %u chars\n", b64Len);

  // Build JSON body in PSRAM — prefix + b64 + suffix
  const char* prefix = "{\"audio\":\"";
  const char* suffix = "\",\"languageCode\":\"" STT_LANGUAGE_CODE "\"}";
  size_t bodyLen = strlen(prefix) + b64Len + strlen(suffix);
  char* bodyBuf = (char*)ps_malloc(bodyLen + 1);
  if (!bodyBuf) {
    free(b64buf);
    Serial.println("[STT] PSRAM alloc failed for body");
    return "";
  }
  memcpy(bodyBuf,                          prefix, strlen(prefix));
  memcpy(bodyBuf + strlen(prefix),         b64buf, b64Len);
  memcpy(bodyBuf + strlen(prefix) + b64Len, suffix, strlen(suffix) + 1);
  free(b64buf);  // b64 copy now lives inside bodyBuf

  WiFiClientSecure sslClient;
  sslClient.setInsecure();
  HTTPClient http;
  http.begin(sslClient, url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + idToken);
  http.setTimeout(60000); // large payload needs extra time

  // sendRequest with a Stream reads in 4KB chunks — avoids the single
  // large SSL write() that causes HTTP -3 for 150KB+ payloads.
  MemStream bodyStream((uint8_t*)bodyBuf, bodyLen);
  int code = http.sendRequest("POST", &bodyStream, bodyLen);
  free(bodyBuf);
  if (code != 200) {
    Serial.printf("[STT] HTTP %d: %s\n", code, http.getString().c_str());
    http.end();
    return "";
  }

  JsonDocument resp;
  deserializeJson(resp, http.getString());
  http.end();

  String transcript = resp["transcript"].as<String>();
  Serial.println("[STT] Transcript: " + transcript);
  return transcript;
}
