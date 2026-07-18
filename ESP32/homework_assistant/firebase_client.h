#pragma once
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <time.h>
#include "mbedtls/base64.h"   // decode base64 header fields from processTurn
#include "secrets.h"

// ── Read-only Stream over a PSRAM buffer ─────────────────────────────────────
// HTTPClient::sendRequest(method, stream, size) reads in small chunks instead of
// one big write(), which fails for large SSL payloads (HTTP -3 on ~150 KB+). Used
// by firestoreProcessTurnAudio() here (raw PCM upload) and by the STT client
// (stt_client.h, included AFTER this header — it relies on this definition).
class MemStream : public Stream {
  const uint8_t* _buf;
  size_t _len, _pos;
public:
  MemStream(const uint8_t* buf, size_t len) : _buf(buf), _len(len), _pos(0) {}
  int  available() override { return (int)min(_len - _pos, (size_t)1024); } // cap → reads in 1KB chunks
  int  read()      override { return _pos < _len ? _buf[_pos++] : -1; }
  int  peek()      override { return _pos < _len ? _buf[_pos]   : -1; }
  size_t write(uint8_t) override { return 0; }
};

// ─────────────────────────────────────────────────────────────────────────────
// Firebase REST client — connects the device to the team's tutor backend.
//
// Handles: anonymous sign-in, token refresh, child auto-detect, rules-compliant
// session creation, the learning-turn protocol, polling, and deviceState
// heartbeats so the Flutter app's device monitor shows the device live.
//
// Edge-to-edge protocol (matches firebase/functions):
//   1. firestoreCreateSession()  -> writes {childId,deviceId,subject,startedAt,
//                                    status:"starting", awaitingFirstQuestion:true}
//      onSessionCreated() generates the first question + audio.
//   2. firestoreWaitForCurrentQuestion() -> reads currentQuestion + audio url
//   3. firestorePostLearningTurn(answer) -> {type:"learning_turn",childAnswer,
//                                    status:"pending"}
//   4. firestorePollForTurnResult() -> spokenFeedback, emotion, nextQuestion,
//                                    expectedAnswer, audioUrl, shouldTakeBreak
// ─────────────────────────────────────────────────────────────────────────────

static String g_idToken      = "";
static String g_refreshToken = "";
static String g_sessionId    = "";
static String g_childId      = "";   // resolved child profile (may be empty)
static String g_firebaseUid  = "";   // Firebase anonymous UID == deviceId
// Sequence of the CURRENT question. Read from the session at start, then updated
// from each processTurn response (X-Turn-Seq). Echoed on every answer so the
// server can detect a stale answer (lost response) and resync instead of
// mis-grading it against the next question.
static long   g_turnSeq      = 0;

// Idle power policy, read from children/{id}.settings at boot (see
// firestoreResolveChildId). After g_screenOffMinutes of no interaction the
// device turns its screen off; after g_deviceSleepMinutes it deep-sleeps and
// wakes on the push-to-talk button. Defaults match AppConstants in the app.
static int g_screenOffMinutes   = 15;
static int g_deviceSleepMinutes = 50;

// Shared SSL client — setInsecure() skips cert verification (fine for IoT prototypes)
static WiFiClientSecure _sslClient;

static void _initSSL() {
  _sslClient.stop();         // close any previous connection (different host reuse bug)
  _sslClient.setInsecure();  // no CA bundle needed
}

// ── ISO-8601 UTC timestamp (needs NTP synced; see syncClock() in the sketch) ──
// Firestore timestampValue must be RFC-3339 / ISO-8601. The deviceState
// heartbeat freshness drives the app's online indicator, so this must be a real
// wall-clock time, not a placeholder.
static String isoNow() {
  time_t now = time(nullptr);
  if (now < 1000000000) return "2000-01-01T00:00:00Z"; // clock not synced yet
  struct tm tm_utc;
  gmtime_r(&now, &tm_utc);
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
  return String(buf);
}

// ── NVS-backed credential persistence ────────────────────────────────────────
// Firebase Auth anonymous accounts:signUp creates a brand-new user each call,
// so without persistence the UID would change every reboot — and any
// `children.deviceId` paired to the old UID would stop matching. We save
// {refreshToken, uid} after the first signUp and on every successful refresh,
// then on the next boot we try refresh-first to keep the same UID.
static Preferences g_authPrefs;

static void firebaseLoadCreds() {
  g_authPrefs.begin("firebase", true);
  g_refreshToken = g_authPrefs.getString("refresh_token", "");
  g_firebaseUid  = g_authPrefs.getString("uid", "");
  g_authPrefs.end();
}

static void firebaseSaveCreds() {
  g_authPrefs.begin("firebase", false);
  g_authPrefs.putString("refresh_token", g_refreshToken);
  g_authPrefs.putString("uid", g_firebaseUid);
  g_authPrefs.end();
}

static void firebaseClearCreds() {
  g_authPrefs.begin("firebase", false);
  g_authPrefs.clear();
  g_authPrefs.end();
  g_refreshToken = "";
  g_firebaseUid  = "";
}

// ── Anonymous sign-in via Firebase Auth REST API ──────────────────────────────
String firebaseSignIn() {
  _initSSL();
  HTTPClient http;
  String url = "https://identitytoolkit.googleapis.com/v1/accounts:signUp?key=";
  url += FIREBASE_WEB_API_KEY;

  http.begin(_sslClient, url);
  http.addHeader("Content-Type", "application/json");
  int code = http.POST("{\"returnSecureToken\":true}");

  if (code != 200) {
    Serial.printf("[Firebase] signUp failed: HTTP %d\n", code);
    http.end();
    return "";
  }

  JsonDocument doc;
  deserializeJson(doc, http.getString());
  http.end();

  g_idToken      = doc["idToken"].as<String>();
  g_refreshToken = doc["refreshToken"].as<String>();
  g_firebaseUid  = doc["localId"].as<String>();
  Serial.println("[Firebase] Signed in anonymously. UID: " + g_firebaseUid);
  return g_idToken;
}

// ── Pairing code (TUTOR-XXXXXX) ──────────────────────────────────────────────
// Derived deterministically from the chip's eFuse MAC, so it's stable across
// reboots without any flash storage. The Flutter pairing sheet reads the
// `pairingCodes/TUTOR-XXXXXX` doc to map the user-visible code to this device's
// (random, opaque) Firebase anonymous UID, which is what actually ends up on
// the child's profile as `deviceId`.
static String devicePairingDocId() {
  uint64_t mac = ESP.getEfuseMac();
  uint32_t code = (uint32_t)(mac % 1000000ULL);
  char buf[16];
  snprintf(buf, sizeof(buf), "TUTOR-%06u", (unsigned)code);
  return String(buf);
}

// Write pairingCodes/TUTOR-XXXXXX = { firebaseUid, updatedAt }.
// Call once after sign-in. Rules require firebaseUid == auth.uid so only the
// device can publish (or overwrite) its own mapping. The doc is what closes
// the loop between the user-visible 6-digit code and the device's auth uid;
// without it the Flutter pairing sheet has no way to translate the code into
// the value `children.deviceId` must hold.
void firestoreWritePairingCode() {
  if (g_idToken.isEmpty() || g_firebaseUid.isEmpty()) return;

  const String docId = devicePairingDocId();

  String url = "https://firestore.googleapis.com/v1/projects/";
  url += FIREBASE_PROJECT_ID;
  url += "/databases/(default)/documents/pairingCodes/" + docId;
  url += "?updateMask.fieldPaths=firebaseUid";
  url += "&updateMask.fieldPaths=updatedAt";

  JsonDocument body;
  body["fields"]["firebaseUid"]["stringValue"]   = g_firebaseUid;
  body["fields"]["updatedAt"]["timestampValue"]  = isoNow();

  String bodyStr;
  serializeJson(body, bodyStr);

  _initSSL();
  HTTPClient http;
  http.begin(_sslClient, url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + g_idToken);
  int code = http.sendRequest("PATCH", bodyStr);
  if (code != 200) {
    Serial.printf("[Firestore] pairingCodes PATCH HTTP %d — %s\n", code, http.getString().c_str());
  } else {
    Serial.println("[Firebase] Published pairing code: " + docId + " -> " + g_firebaseUid);
  }
  http.end();
}

// ── Refresh the idToken (call when you get a 401 from Firestore) ─────────────
String firebaseRefreshToken() {
  if (g_refreshToken.isEmpty()) return firebaseSignIn();

  _initSSL();
  HTTPClient http;
  String url = "https://securetoken.googleapis.com/v1/token?key=";
  url += FIREBASE_WEB_API_KEY;

  http.begin(_sslClient, url);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  String body = "grant_type=refresh_token&refresh_token=" + g_refreshToken;
  int code = http.POST(body);

  if (code != 200) {
    Serial.printf("[Firebase] refresh failed: HTTP %d — re-signing in\n", code);
    http.end();
    return firebaseSignIn();
  }

  JsonDocument doc;
  deserializeJson(doc, http.getString());
  http.end();

  g_idToken = doc["id_token"].as<String>();
  // Google rotates the refresh_token on every successful exchange; capture it
  // so the next refresh doesn't try to use a stale value.
  const String newRefresh = doc["refresh_token"].as<String>();
  if (!newRefresh.isEmpty()) g_refreshToken = newRefresh;
  Serial.println("[Firebase] Token refreshed.");
  return g_idToken;
}

// ── Boot-time auth ──────────────────────────────────────────────────────────
// Tries to refresh from NVS-saved creds first to keep the anonymous UID stable
// across reboots; falls back to a fresh signUp only when nothing's saved (or
// the saved token has been invalidated). Persists creds on success so the
// next boot picks up the same UID — required so that `children.deviceId`
// paired through the Flutter app keeps matching `session.deviceId` after the
// device reboots.
String firebaseBootAuth() {
  firebaseLoadCreds();

  String idToken;
  if (!g_refreshToken.isEmpty()) {
    Serial.println("[Firebase] Restoring saved session. UID: " + g_firebaseUid);
    idToken = firebaseRefreshToken();   // self-recovers via signUp on failure
  } else {
    Serial.println("[Firebase] No saved creds — signing up fresh.");
    idToken = firebaseSignIn();
  }

  if (!idToken.isEmpty()) {
    firebaseSaveCreds();
    Serial.println("[Firebase] Auth ready. UID: " + g_firebaseUid);
  }
  return idToken;
}

// ── Resolve which child profile this device serves ───────────────────────────
// Strategy (matches the "auto-detect, fallback to config" decision):
//   1. runQuery children where deviceId == our UID -> use the first match.
//   2. else, if CHILD_ID is set in secrets.h, use it.
//   3. else, "" (backend runs generic, non-personalised questions).
// Returns the child document id (possibly "").
String firestoreResolveChildId() {
  // 1. structured query by deviceId
  String url = "https://firestore.googleapis.com/v1/projects/";
  url += FIREBASE_PROJECT_ID;
  url += "/databases/(default)/documents:runQuery";

  JsonDocument q;
  q["structuredQuery"]["from"][0]["collectionId"] = "children";
  q["structuredQuery"]["where"]["fieldFilter"]["field"]["fieldPath"] = "deviceId";
  q["structuredQuery"]["where"]["fieldFilter"]["op"] = "EQUAL";
  q["structuredQuery"]["where"]["fieldFilter"]["value"]["stringValue"] = g_firebaseUid;
  q["structuredQuery"]["limit"] = 1;

  String body;
  serializeJson(q, body);

  _initSSL();
  HTTPClient http;
  http.begin(_sslClient, url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + g_idToken);
  int code = http.POST(body);

  if (code == 200) {
    JsonDocument resp;
    deserializeJson(resp, http.getString());
    http.end();
    // runQuery returns an array; the first element has .document.name if matched.
    if (resp.is<JsonArray>() && resp.size() > 0 && resp[0]["document"]["name"].is<const char*>()) {
      String name = resp[0]["document"]["name"].as<String>();
      String childId = name.substring(name.lastIndexOf('/') + 1);
      Serial.println("[Firestore] Auto-detected child: " + childId);

      // Cache the idle power policy (screen-off / deep-sleep) from settings.
      // Firestore REST returns integers as decimal strings under "integerValue".
      JsonVariant sf = resp[0]["document"]["fields"]["settings"]["mapValue"]["fields"];
      if (!sf.isNull()) {
        String so = sf["screenOffMinutes"]["integerValue"].as<String>();
        String ds = sf["deviceSleepMinutes"]["integerValue"].as<String>();
        if (so.length() && so.toInt() > 0) g_screenOffMinutes   = so.toInt();
        if (ds.length() && ds.toInt() > 0) g_deviceSleepMinutes = ds.toInt();
      }
      Serial.printf("[Firestore] idle policy: screen-off %d min, sleep %d min\n",
                    g_screenOffMinutes, g_deviceSleepMinutes);
      return childId;
    }
  } else {
    Serial.printf("[Firestore] child runQuery HTTP %d\n", code);
    http.end();
  }

  // 2. fallback to configured CHILD_ID
  String configured = String(CHILD_ID);
  if (configured.length() > 0) {
    Serial.println("[Firestore] Using configured CHILD_ID: " + configured);
    return configured;
  }

  // 3. none
  Serial.println("[Firestore] No child profile — generic session.");
  return "";
}

// ── Create a rules-compliant session document ────────────────────────────────
// Security rules require keys: childId, deviceId, subject, startedAt.
// When [awaitFirstQuestion] is true (default), sets awaitingFirstQuestion:true
// so onSessionCreated() seeds the first question right away. Set it to false
// when the device intends to run the voice identification flow first
// (identify_child → identify_subject); in that case the first question is
// created by handleIdentifySubject once the kid picks a subject by voice.
// Returns the auto-generated session document ID.
String firestoreCreateSession(const String& subject = SESSION_SUBJECT,
                              bool awaitFirstQuestion = true) {
  if (g_idToken.isEmpty()) return "";

  g_childId = firestoreResolveChildId();

  String url = "https://firestore.googleapis.com/v1/projects/";
  url += FIREBASE_PROJECT_ID;
  url += "/databases/(default)/documents/sessions";

  _initSSL();
  HTTPClient http;
  http.begin(_sslClient, url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + g_idToken);

  JsonDocument body;
  body["fields"]["childId"]["stringValue"]   = g_childId;          // "" allowed
  body["fields"]["deviceId"]["stringValue"]  = g_firebaseUid;      // == auth.uid (rules + ownership)
  body["fields"]["subject"]["stringValue"]   = subject;            // "math" | "english"
  body["fields"]["status"]["stringValue"]    = awaitFirstQuestion ? "starting" : "identifying_child";
  if (awaitFirstQuestion) {
    body["fields"]["awaitingFirstQuestion"]["booleanValue"] = true;
  }
  body["fields"]["startedAt"]["timestampValue"]    = isoNow();
  body["fields"]["lastActivity"]["timestampValue"] = isoNow();

  String bodyStr;
  serializeJson(body, bodyStr);

  int code = http.POST(bodyStr);
  if (code != 200) {
    Serial.printf("[Firestore] createSession failed: HTTP %d — %s\n", code, http.getString().c_str());
    http.end();
    return "";
  }

  JsonDocument resp;
  deserializeJson(resp, http.getString());
  http.end();

  String name = resp["name"].as<String>();
  String sessionId = name.substring(name.lastIndexOf('/') + 1);
  Serial.println("[Firestore] Session created: " + sessionId);
  return sessionId;
}

// ── Wait until onSessionCreated() has seeded the first question ───────────────
// Polls the session doc until status=="active" with a currentQuestion, then
// returns the question text + its audio URL (via out-params).
// subjectOut is optional — when non-null, the resolved subject ("math" |
// "english") from the session doc is written there. The cloud function
// updates this field after `identify_subject`, so the device knows which
// subject the kid actually chose by voice.
bool firestoreWaitForCurrentQuestion(const String& sessionId,
                                     String& questionOut,
                                     String& audioUrlOut,
                                     uint32_t timeoutMs = 30000,
                                     String* subjectOut = nullptr) {
  String url = "https://firestore.googleapis.com/v1/projects/";
  url += FIREBASE_PROJECT_ID;
  url += "/databases/(default)/documents/sessions/" + sessionId;

  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    _initSSL();
    HTTPClient http;
    http.begin(_sslClient, url);
    http.addHeader("Authorization", "Bearer " + g_idToken);
    int code = http.GET();
    if (code == 401) { firebaseRefreshToken(); http.end(); continue; }
    if (code != 200) { Serial.printf("[Firestore] session GET HTTP %d\n", code); http.end(); delay(1500); continue; }

    JsonDocument resp;
    deserializeJson(resp, http.getString());
    http.end();

    String status = resp["fields"]["status"]["stringValue"].as<String>();
    String q      = resp["fields"]["currentQuestion"]["stringValue"].as<String>();
    if (status == "error") {
      Serial.println("[Firestore] session init error: " +
                     resp["fields"]["error"]["stringValue"].as<String>());
      return false;
    }
    if ((status == "active" || status == "break") && q.length() > 0) {
      questionOut = q;
      // Sequence of this first question (integerValue arrives as a string).
      g_turnSeq = String(resp["fields"]["turnSeq"]["integerValue"].as<const char*>()).toInt();
      // Inline base64 WAV (preferred, no Storage round-trip). Falls back to
      // legacy URL if the doc was written by an older Cloud Function build.
      String b64 = resp["fields"]["currentQuestionAudioData"]["stringValue"].as<String>();
      audioUrlOut = b64.length() > 0
        ? b64
        : resp["fields"]["currentQuestionAudioUrl"]["stringValue"].as<String>();
      if (subjectOut) {
        String s = resp["fields"]["subject"]["stringValue"].as<String>();
        if (s.length() > 0) *subjectOut = s;
      }
      return true;
    }
    Serial.println("[Firestore] waiting for first question (status=" + status + ")");
    delay(1500);
  }
  Serial.println("[Firestore] Timed out waiting for first question.");
  return false;
}

// ── Post the child's spoken answer as a learning-turn exchange ────────────────
// Returns the auto-generated exchange document ID.
String firestorePostLearningTurn(const String& sessionId, const String& childAnswer) {
  String url = "https://firestore.googleapis.com/v1/projects/";
  url += FIREBASE_PROJECT_ID;
  url += "/databases/(default)/documents/sessions/" + sessionId + "/exchanges";

  _initSSL();
  HTTPClient http;
  http.begin(_sslClient, url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + g_idToken);

  JsonDocument body;
  body["fields"]["type"]["stringValue"]        = "learning_turn";
  body["fields"]["childAnswer"]["stringValue"] = childAnswer;
  body["fields"]["status"]["stringValue"]      = "pending";
  body["fields"]["askedAt"]["timestampValue"]  = isoNow();

  String bodyStr;
  serializeJson(body, bodyStr);

  int code = http.POST(bodyStr);
  if (code != 200) {
    Serial.printf("[Firestore] postLearningTurn failed: HTTP %d — %s\n", code, http.getString().c_str());
    http.end();
    return "";
  }

  JsonDocument resp;
  deserializeJson(resp, http.getString());
  http.end();

  String name = resp["name"].as<String>();
  String exchangeId = name.substring(name.lastIndexOf('/') + 1);
  Serial.println("[Firestore] Learning turn posted, exchange: " + exchangeId);
  return exchangeId;
}

// ── Result of a processed learning turn ──────────────────────────────────────
struct TurnResult {
  bool   ok            = false;
  String spokenFeedback;   // what to say first
  String nextQuestion;     // becomes the new current question
  String emotion;          // happy|neutral|encouraging|concerned|celebrating
  String audioUrl;         // WAV of (feedback + next question) — old post/poll path
  bool   shouldTakeBreak = false;
  bool   isCorrect       = false;
  bool   sessionEnded    = false;  // backend signals explicit session end (exit intent / continue declined)
  String endReason;        // "child_request" | "declined_continue" | "timeout" — set iff sessionEnded
  bool   sttEmpty        = false;  // Phase 2 audio path: server STT found no speech → caller reprompts
  // Phase 1 (processTurn): MP3 bytes returned inline in the HTTP body. Owned by
  // the caller — free(audioBuf) after playback. Empty on the old path.
  uint8_t* audioBuf    = nullptr;
  size_t   audioLen    = 0;
};

// ── Poll the exchange until the Cloud Function finishes the turn ──────────────
bool firestorePollForTurnResult(const String& sessionId,
                                const String& exchangeId,
                                TurnResult& out,
                                uint32_t timeoutMs = 30000) {
  String url = "https://firestore.googleapis.com/v1/projects/";
  url += FIREBASE_PROJECT_ID;
  url += "/databases/(default)/documents/sessions/" + sessionId + "/exchanges/" + exchangeId;

  uint32_t start = millis();

  // One TLS handshake + a single keep-alive HTTP connection for the WHOLE poll,
  // instead of a fresh handshake every iteration. The old loop called _initSSL()
  // (which stops + reopens the socket) on every pass, so each poll paid a full
  // ~0.3–0.6 s TLS handshake and the effective period was ~1 s, not 0.5 s. Now
  // we connect once, set reuse, and just re-GET — so the 250 ms interval is real.
  _initSSL();
  HTTPClient http;
  http.begin(_sslClient, url);
  http.setReuse(true);
  http.addHeader("Authorization", "Bearer " + g_idToken);

  while (millis() - start < timeoutMs) {
    int code = http.GET();
    if (code == 401) {
      // Token expired mid-poll: refresh and rebuild the connection with new auth.
      firebaseRefreshToken();
      http.end();
      _initSSL();
      http.begin(_sslClient, url);
      http.setReuse(true);
      http.addHeader("Authorization", "Bearer " + g_idToken);
      continue;
    }
    if (code != 200) {
      Serial.printf("[Firestore] poll HTTP %d\n", code);
      http.getString();   // drain body so the reused keep-alive socket stays clean
      delay(500);
      continue;
    }

    JsonDocument resp;
    deserializeJson(resp, http.getString());   // fully drains body → connection stays reusable

    String status = resp["fields"]["status"]["stringValue"].as<String>();
    Serial.println("[Firestore] turn status = " + status);

    if (status == "done") {
      out.ok             = true;
      out.spokenFeedback = resp["fields"]["spokenFeedback"]["stringValue"].as<String>();
      out.nextQuestion   = resp["fields"]["nextQuestion"]["stringValue"].as<String>();
      out.emotion        = resp["fields"]["emotion"]["stringValue"].as<String>();
      // Prefer inline base64 audio; fall back to URL for old Cloud builds.
      String b64         = resp["fields"]["audioData"]["stringValue"].as<String>();
      out.audioUrl       = b64.length() > 0
        ? b64
        : resp["fields"]["audioUrl"]["stringValue"].as<String>();
      out.shouldTakeBreak= resp["fields"]["shouldTakeBreak"]["booleanValue"].as<bool>();
      out.isCorrect      = resp["fields"]["isCorrect"]["booleanValue"].as<bool>();
      out.sessionEnded   = resp["fields"]["sessionEnded"]["booleanValue"].as<bool>();
      out.endReason      = resp["fields"]["endReason"]["stringValue"].as<String>();
      http.end();
      return true;
    }
    if (status == "error") {
      Serial.println("[Firestore] turn error: " +
                     resp["fields"]["error"]["stringValue"].as<String>());
      http.end();
      return false;
    }
    delay(250);
  }
  http.end();
  Serial.println("[Firestore] Timed out waiting for turn result.");
  return false;
}

// ── base64 (ASCII header) → String of raw bytes (UTF-8 Hebrew) ───────────────
static String _b64ToString(const String& b64) {
  if (b64.isEmpty()) return "";
  size_t inLen  = b64.length();
  size_t outCap = (inLen / 4) * 3 + 4;
  uint8_t* out  = (uint8_t*)malloc(outCap + 1);
  if (!out) return "";
  size_t outLen = 0;
  int rc = mbedtls_base64_decode(out, outCap, &outLen,
                                 (const uint8_t*)b64.c_str(), inLen);
  String s;
  if (rc == 0) { out[outLen] = 0; s = String((char*)out); }
  free(out);
  return s;
}

// ── Stream the HTTP response body (MP3 bytes) into a fresh PSRAM buffer ───────
// Sets out.audioBuf (caller frees) + out.audioLen. Shared by both inline-audio
// turn paths (firestoreProcessTurn / firestoreProcessTurnAudio) so the read loop
// lives in one place. Leaves out.audioBuf=nullptr / out.audioLen=0 on alloc fail.
static void _readMp3BodyToPSRAM(HTTPClient& http, TurnResult& out) {
  int contentLen = http.getSize();
  const size_t MAX_MP3 = 256 * 1024;
  size_t bufCap = (contentLen > 0 && (size_t)contentLen <= MAX_MP3) ? (size_t)contentLen : MAX_MP3;
  out.audioBuf = (uint8_t*)ps_malloc(bufCap);
  out.audioLen = 0;
  if (!out.audioBuf) {
    Serial.println("[Firestore] processTurn: ps_malloc failed for audio buffer.");
    return;
  }
  WiFiClient* stream = http.getStreamPtr();
  size_t filled = 0;
  uint32_t lastData = millis();
  while (filled < bufCap) {
    int avail = stream->available();
    if (avail > 0) {
      int n = stream->readBytes(out.audioBuf + filled, min((size_t)avail, bufCap - filled));
      filled += n;
      lastData = millis();
    } else if (!http.connected() || millis() - lastData > 3000) {
      break;
    }
    yield();
  }
  out.audioLen = filled;
}

// ─────────────────────────────────────────────────────────────────────────────
// firestoreProcessTurn — Phase 1: ONE synchronous call replaces post + poll +
// trigger + separate audio download. Sends {sessionId, childAnswer}; the function
// grades, (gated) calls Gemini, synthesises TTS, writes the same Firestore docs,
// and returns the result in response HEADERS + the MP3 in the response BODY.
// Fills `out` (incl. out.audioBuf/out.audioLen — caller must free(out.audioBuf)).
// Returns false on any failure so the caller can fall back / re-listen.
// ─────────────────────────────────────────────────────────────────────────────
bool firestoreProcessTurn(const String& sessionId, const String& childAnswer, TurnResult& out) {
  String url = "https://" CLOUD_FUNCTIONS_REGION "-" FIREBASE_PROJECT_ID
               ".cloudfunctions.net/processTurn?seq=" + String(g_turnSeq);

  JsonDocument body;
  body["sessionId"]   = sessionId;
  body["childAnswer"] = childAnswer;
  String bodyStr; serializeJson(body, bodyStr);

  const char* hdrKeys[] = {
    "X-Is-Correct", "X-Emotion", "X-Should-Break", "X-Session-Ended", "X-End-Reason",
    "X-Feedback-B64", "X-Next-Question-B64", "X-Exchange-Id", "X-Turn-Seq"
  };

  // Up to 2 attempts so an expired token (401) can be refreshed and retried once.
  for (int attempt = 0; attempt < 2; attempt++) {
    _initSSL();
    HTTPClient http;
    http.begin(_sslClient, url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Bearer " + g_idToken);
    http.setTimeout(60000);
    http.collectHeaders(hdrKeys, sizeof(hdrKeys) / sizeof(hdrKeys[0]));

    int code = http.POST(bodyStr);
    if (code == 401 && attempt == 0) { firebaseRefreshToken(); http.end(); continue; }
    if (code != 200) {
      Serial.printf("[Firestore] processTurn HTTP %d — %s\n", code, http.getString().c_str());
      http.end();
      return false;
    }

    // ── Metadata from headers ─────────────────────────────────────────────────
    out.isCorrect       = http.header("X-Is-Correct")    == "1";
    out.emotion         = http.header("X-Emotion");
    out.shouldTakeBreak = http.header("X-Should-Break")  == "1";
    out.sessionEnded    = http.header("X-Session-Ended") == "1";
    out.endReason       = http.header("X-End-Reason");
    out.spokenFeedback  = _b64ToString(http.header("X-Feedback-B64"));
    out.nextQuestion    = _b64ToString(http.header("X-Next-Question-B64"));
    { String ts = http.header("X-Turn-Seq"); if (ts.length()) g_turnSeq = ts.toInt(); }
    Serial.println("[Firestore] exchangeId: " + http.header("X-Exchange-Id"));

    // ── MP3 body → PSRAM (caller frees out.audioBuf) ──────────────────────────
    _readMp3BodyToPSRAM(http, out);
    http.end();
    out.ok = true;
    Serial.printf("[Firestore] processTurn done: correct=%d audio=%u bytes\n",
                  out.isCorrect, (unsigned)out.audioLen);
    return true;
  }
  return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// firestoreProcessTurnAudio — Phase 2: fold STT INTO processTurn.
//
// The device uploads the RAW mono PCM (LINEAR16 16 kHz) as the request BODY:
//   • no base64 — drops 33% vs the JSON STT upload (over the slow device link);
//   • no separate transcribeAudio round trip — one fewer ~2 s TLS handshake.
// The function transcribes, then grades + (gated) Gemini + TTS, and returns the
// result inline exactly like firestoreProcessTurn (metadata in headers, MP3 in the
// body). When the server's STT finds no speech it sets out.sttEmpty so the caller
// reprompts locally (instant SD-cached phrase) instead of wasting a turn.
// Fills out.audioBuf/out.audioLen — caller must free(out.audioBuf).
// ─────────────────────────────────────────────────────────────────────────────
bool firestoreProcessTurnAudio(const String& sessionId, const uint8_t* pcm, size_t pcmLen,
                               const char* langCode, TurnResult& out) {
  String url = "https://" CLOUD_FUNCTIONS_REGION "-" FIREBASE_PROJECT_ID
               ".cloudfunctions.net/processTurn?fmt=pcm16&sessionId=" + sessionId +
               "&lang=" + String(langCode) + "&seq=" + String(g_turnSeq);

  const char* hdrKeys[] = {
    "X-Is-Correct", "X-Emotion", "X-Should-Break", "X-Session-Ended", "X-End-Reason",
    "X-Feedback-B64", "X-Next-Question-B64", "X-Exchange-Id",
    "X-Stt-Empty", "X-Transcript-B64", "X-Turn-Seq"
  };
  const size_t hdrCount = sizeof(hdrKeys) / sizeof(hdrKeys[0]);

  // Up to 2 attempts so an expired token (401) can be refreshed and retried once.
  for (int attempt = 0; attempt < 2; attempt++) {
    _initSSL();
    HTTPClient http;
    http.begin(_sslClient, url);
    http.addHeader("Content-Type", "application/octet-stream");
    http.addHeader("Authorization", "Bearer " + g_idToken);
    http.setTimeout(60000);
    http.collectHeaders(hdrKeys, hdrCount);

    // Stream the raw PCM in 1 KB chunks (the single big SSL write() fails for large
    // payloads — same reason the STT path streams). A few-second answer is
    // ~60–120 KB of mono PCM.
    MemStream bodyStream(pcm, pcmLen);
    int code = http.sendRequest("POST", &bodyStream, pcmLen);
    if (code == 401 && attempt == 0) { firebaseRefreshToken(); http.end(); continue; }
    if (code != 200) {
      Serial.printf("[Firestore] processTurnAudio HTTP %d — %s\n", code, http.getString().c_str());
      http.end();
      return false;
    }

    // ── No speech recognised server-side → caller reprompts locally ───────────
    if (http.header("X-Stt-Empty") == "1") {
      http.end();
      out.ok = true;
      out.sttEmpty = true;
      Serial.println("[Firestore] processTurnAudio: server STT empty — will reprompt.");
      return true;
    }

    // ── Metadata from headers ─────────────────────────────────────────────────
    out.isCorrect       = http.header("X-Is-Correct")    == "1";
    out.emotion         = http.header("X-Emotion");
    out.shouldTakeBreak = http.header("X-Should-Break")  == "1";
    out.sessionEnded    = http.header("X-Session-Ended") == "1";
    out.endReason       = http.header("X-End-Reason");
    out.spokenFeedback  = _b64ToString(http.header("X-Feedback-B64"));
    out.nextQuestion    = _b64ToString(http.header("X-Next-Question-B64"));
    { String ts = http.header("X-Turn-Seq"); if (ts.length()) g_turnSeq = ts.toInt(); }
    Serial.println("[Firestore] exchangeId: " + http.header("X-Exchange-Id"));
    String transcript   = _b64ToString(http.header("X-Transcript-B64"));
    if (transcript.length()) Serial.println("[STT] (server) heard: " + transcript);

    // ── MP3 body → PSRAM (caller frees out.audioBuf) ──────────────────────────
    _readMp3BodyToPSRAM(http, out);
    http.end();
    out.ok = true;
    Serial.printf("[Firestore] processTurnAudio done: correct=%d audio=%u bytes\n",
                  out.isCorrect, (unsigned)out.audioLen);
    return true;
  }
  return false;
}

// ─────────────────────────────────────────────────────────────────────────────
//   Voice identification flow (boot-time): kid says name + picks subject
// ─────────────────────────────────────────────────────────────────────────────

// ── Result of an identify exchange ───────────────────────────────────────────
struct IdentifyResult {
  bool   ok           = false;
  bool   needsPairing = false;  // cloud sets when device.deviceId isn't paired
  String audioUrl;        // WAV of the cloud's spoken response
  String promptText;      // the text that was synthesized (for logging)
  String matchedChildName;
  String matchedChildId;
  String subject;         // only set by identify_subject ("math" | "english")
  String nextQuestion;    // only set by identify_subject — first question text
};

// ── Post an identify_child exchange ──────────────────────────────────────────
String firestorePostIdentifyChild(const String& sessionId, const String& nameTranscript) {
  String url = "https://firestore.googleapis.com/v1/projects/";
  url += FIREBASE_PROJECT_ID;
  url += "/databases/(default)/documents/sessions/" + sessionId + "/exchanges";

  _initSSL();
  HTTPClient http;
  http.begin(_sslClient, url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + g_idToken);

  JsonDocument body;
  body["fields"]["type"]["stringValue"]                  = "identify_child";
  body["fields"]["childNameTranscript"]["stringValue"]   = nameTranscript;
  body["fields"]["status"]["stringValue"]                = "pending";
  body["fields"]["askedAt"]["timestampValue"]            = isoNow();

  String bodyStr; serializeJson(body, bodyStr);
  int code = http.POST(bodyStr);
  if (code != 200) {
    Serial.printf("[Firestore] identify_child post HTTP %d — %s\n", code, http.getString().c_str());
    http.end(); return "";
  }
  JsonDocument resp; deserializeJson(resp, http.getString()); http.end();
  String name = resp["name"].as<String>();
  return name.substring(name.lastIndexOf('/') + 1);
}

// ── Post an identify_subject exchange ────────────────────────────────────────
String firestorePostIdentifySubject(const String& sessionId, const String& subjectTranscript) {
  String url = "https://firestore.googleapis.com/v1/projects/";
  url += FIREBASE_PROJECT_ID;
  url += "/databases/(default)/documents/sessions/" + sessionId + "/exchanges";

  _initSSL();
  HTTPClient http;
  http.begin(_sslClient, url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + g_idToken);

  JsonDocument body;
  body["fields"]["type"]["stringValue"]                  = "identify_subject";
  body["fields"]["subjectTranscript"]["stringValue"]     = subjectTranscript;
  body["fields"]["status"]["stringValue"]                = "pending";
  body["fields"]["askedAt"]["timestampValue"]            = isoNow();

  String bodyStr; serializeJson(body, bodyStr);
  int code = http.POST(bodyStr);
  if (code != 200) {
    Serial.printf("[Firestore] identify_subject post HTTP %d — %s\n", code, http.getString().c_str());
    http.end(); return "";
  }
  JsonDocument resp; deserializeJson(resp, http.getString()); http.end();
  String name = resp["name"].as<String>();
  return name.substring(name.lastIndexOf('/') + 1);
}

// ── Poll an identify exchange until status==done ─────────────────────────────
bool firestorePollForIdentifyResult(const String& sessionId,
                                    const String& exchangeId,
                                    IdentifyResult& out,
                                    uint32_t timeoutMs = 30000) {
  String url = "https://firestore.googleapis.com/v1/projects/";
  url += FIREBASE_PROJECT_ID;
  url += "/databases/(default)/documents/sessions/" + sessionId + "/exchanges/" + exchangeId;

  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    _initSSL();
    HTTPClient http;
    http.begin(_sslClient, url);
    http.addHeader("Authorization", "Bearer " + g_idToken);
    int code = http.GET();
    if (code == 401) { firebaseRefreshToken(); http.end(); continue; }
    if (code != 200) { Serial.printf("[Firestore] identify poll HTTP %d\n", code); http.end(); delay(1500); continue; }

    JsonDocument resp; deserializeJson(resp, http.getString()); http.end();
    String status = resp["fields"]["status"]["stringValue"].as<String>();
    if (status == "done") {
      out.ok                = true;
      // Prefer inline base64; legacy URL is the fallback.
      String b64            = resp["fields"]["audioData"]["stringValue"].as<String>();
      out.audioUrl          = b64.length() > 0
        ? b64
        : resp["fields"]["audioUrl"]["stringValue"].as<String>();
      out.promptText        = resp["fields"]["promptText"]["stringValue"].as<String>();
      out.matchedChildName  = resp["fields"]["matchedChildName"]["stringValue"].as<String>();
      out.matchedChildId    = resp["fields"]["matchedChildId"]["stringValue"].as<String>();
      out.subject           = resp["fields"]["subject"]["stringValue"].as<String>();
      out.nextQuestion      = resp["fields"]["nextQuestion"]["stringValue"].as<String>();
      out.needsPairing      = resp["fields"]["needsPairing"]["booleanValue"].as<bool>();
      return true;
    }
    if (status == "error") {
      Serial.println("[Firestore] identify error: " +
                     resp["fields"]["error"]["stringValue"].as<String>());
      return false;
    }
    delay(500);
  }
  Serial.println("[Firestore] Timed out waiting for identify result.");
  return false;
}

// ── Cloud Function /synthesizeSpeech: text → audio Storage URL ───────────────
// Used by the identify flow for the prompts ("מי כאן?" etc.) since there's no
// on-device TTS. The Cloud Function synthesizes the speech, uploads it to
// Firebase Storage, and returns a public https URL (kept in the legacy-named
// `audioBase64` field for firmware compatibility — it's a URL, not base64).
// speakAudio() streams it via the Audio library. Falls back to {audioUrl:"..."}.
String cloudSynthesizeSpeech(const String& text) {
  if (g_idToken.isEmpty()) return "";
  String url = "https://" CLOUD_FUNCTIONS_REGION "-" FIREBASE_PROJECT_ID
               ".cloudfunctions.net/synthesizeSpeech";

  _initSSL();
  HTTPClient http;
  http.begin(_sslClient, url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + g_idToken);
  http.setTimeout(30000);

  JsonDocument body;
  body["text"] = text;
  String bodyStr; serializeJson(body, bodyStr);

  int code = http.POST(bodyStr);
  if (code != 200) {
    Serial.printf("[TTS] HTTP %d — %s\n", code, http.getString().c_str());
    http.end(); return "";
  }
  JsonDocument resp; deserializeJson(resp, http.getString()); http.end();
  String b64 = resp["audioBase64"].as<String>();
  if (b64.length() > 0) return b64;
  return resp["audioUrl"].as<String>();
}

// ── Synthesise a phrase → MP3 bytes in ONE round trip (no Storage) ───────────
// Backed by the ttsBytes Cloud Function. Returns a fresh PSRAM buffer (caller
// frees) and sets *outLen; nullptr on failure. Used by the SD cache (misses +
// prefetch) so a phrase costs one round trip, not two (synth → Storage → download).
uint8_t* cloudSynthesizeBytes(const String& text, size_t* outLen) {
  *outLen = 0;
  if (g_idToken.isEmpty()) return nullptr;
  String url = "https://" CLOUD_FUNCTIONS_REGION "-" FIREBASE_PROJECT_ID
               ".cloudfunctions.net/ttsBytes";

  _initSSL();
  HTTPClient http;
  http.begin(_sslClient, url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + g_idToken);
  http.setTimeout(30000);

  JsonDocument body;
  body["text"] = text;
  String bodyStr; serializeJson(body, bodyStr);

  int code = http.POST(bodyStr);
  if (code != 200) {
    Serial.printf("[TTS] ttsBytes HTTP %d\n", code);
    http.end();
    return nullptr;
  }

  int contentLen = http.getSize();
  const size_t MAX_MP3 = 256 * 1024;
  size_t bufCap = (contentLen > 0 && (size_t)contentLen <= MAX_MP3) ? (size_t)contentLen : MAX_MP3;
  uint8_t* buf = (uint8_t*)ps_malloc(bufCap);
  if (!buf) { http.end(); return nullptr; }

  WiFiClient* stream = http.getStreamPtr();
  size_t filled = 0;
  uint32_t lastData = millis();
  while (filled < bufCap) {
    int avail = stream->available();
    if (avail > 0) {
      int n = stream->readBytes(buf + filled, min((size_t)avail, bufCap - filled));
      filled += n;
      lastData = millis();
    } else if (!http.connected() || millis() - lastData > 3000) {
      break;
    }
    yield();
  }
  http.end();
  *outLen = filled;
  return buf;
}

// ── deviceState/{deviceId}: live status for the Flutter app ───────────────────
// The app reads deviceState/{uid}.status + lastHeartbeat (freshness => online),
// currentQuestion and activeSubject. Rules allow any signed-in user to write.
// Call writeDeviceState() on every status change and periodically (heartbeat).
// Returns the HTTP status code of the PATCH (200 = the app can now see this
// device online). Returns -1 if we never even tried (no UID yet). Most callers
// ignore the return; setup() uses it to print an explicit app-link boot check.
// Device telemetry counters, defined elsewhere in the sketch: bootCount survives
// deep sleep (RTC memory, in the .ino); the TTS cache hit/miss counters live in
// tts_cache.h. Declared extern here (this header is included before those TUs).
extern uint32_t g_bootCount;
extern uint32_t g_ttsCacheHits;
extern uint32_t g_ttsCacheMisses;

int firestoreWriteDeviceState(const String& status,
                              const String& currentQuestion = "",
                              const String& subject = SESSION_SUBJECT) {
  if (g_firebaseUid.isEmpty()) return -1;

  // PATCH with an updateMask so we only touch the fields we own.
  String url = "https://firestore.googleapis.com/v1/projects/";
  url += FIREBASE_PROJECT_ID;
  url += "/databases/(default)/documents/deviceState/" + g_firebaseUid;
  url += "?updateMask.fieldPaths=status";
  url += "&updateMask.fieldPaths=lastHeartbeat";
  url += "&updateMask.fieldPaths=currentQuestion";
  url += "&updateMask.fieldPaths=activeSubject";
  // Device health telemetry (refreshed on every state transition).
  url += "&updateMask.fieldPaths=wifiRssi";
  url += "&updateMask.fieldPaths=freeHeap";
  url += "&updateMask.fieldPaths=uptimeSec";
  url += "&updateMask.fieldPaths=bootCount";
  url += "&updateMask.fieldPaths=ttsCacheHits";
  url += "&updateMask.fieldPaths=ttsCacheMisses";

  JsonDocument body;
  body["fields"]["status"]["stringValue"]        = status;       // idle|asking|listening|feedback|break|error
  body["fields"]["lastHeartbeat"]["timestampValue"] = isoNow();
  if (currentQuestion.length() > 0)
    body["fields"]["currentQuestion"]["stringValue"] = currentQuestion;
  else
    body["fields"]["currentQuestion"]["nullValue"]   = nullptr;
  body["fields"]["activeSubject"]["stringValue"]   = subject;
  // Firestore REST wants integerValue as a string. RSSI is dBm (negative), the
  // rest are unsigned. Cheap to read; useful for the reliability/coverage report.
  body["fields"]["wifiRssi"]["integerValue"]       = String((int)WiFi.RSSI());
  body["fields"]["freeHeap"]["integerValue"]       = String((uint32_t)ESP.getFreeHeap());
  body["fields"]["uptimeSec"]["integerValue"]      = String((uint32_t)(millis() / 1000UL));
  body["fields"]["bootCount"]["integerValue"]      = String(g_bootCount);
  body["fields"]["ttsCacheHits"]["integerValue"]   = String(g_ttsCacheHits);
  body["fields"]["ttsCacheMisses"]["integerValue"] = String(g_ttsCacheMisses);

  String bodyStr;
  serializeJson(body, bodyStr);

  _initSSL();
  HTTPClient http;
  http.begin(_sslClient, url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + g_idToken);
  int code = http.sendRequest("PATCH", bodyStr);
  if (code != 200) {
    Serial.printf("[Firestore] deviceState PATCH HTTP %d\n", code);
  }
  http.end();
  return code;
}

// ── Keep-alive heartbeat (loop() only) ───────────────────────────────────────
// firestoreWriteDeviceState() calls _initSSL() (stop + reopen socket), so the
// 10 s heartbeat paid a full ~2 s TLS handshake every time — and in wake-word mode
// that's ~2 s every 10 s where loop() can't poll "היי פיפ". This keeps ONE
// keep-alive HTTPClient across calls (the proven setReuse pattern from
// firestorePollForTurnResult): only the FIRST heartbeat after another Firestore
// call pays the handshake; the rest re-PATCH on the open socket (~0.1 s).
//
// It reuses the SHARED _sslClient — NOT a second concurrent TLS connection (that
// OOMs this board; see the note below). Any other call's _initSSL() simply stops
// the socket, and the next heartbeat's sendRequest() reconnects it transparently.
// Use this ONLY for the periodic loop heartbeat; meaningful state transitions
// still go through firestoreWriteDeviceState().
static HTTPClient _hbHttp;
static bool _hbInit = false;

void firestoreHeartbeat(const String& status,
                        const String& currentQuestion = "",
                        const String& subject = SESSION_SUBJECT) {
  if (g_firebaseUid.isEmpty()) return;

  String url = "https://firestore.googleapis.com/v1/projects/";
  url += FIREBASE_PROJECT_ID;
  url += "/databases/(default)/documents/deviceState/" + g_firebaseUid;
  url += "?updateMask.fieldPaths=status";
  url += "&updateMask.fieldPaths=lastHeartbeat";
  url += "&updateMask.fieldPaths=currentQuestion";
  url += "&updateMask.fieldPaths=activeSubject";

  if (!_hbInit) {
    _initSSL();                          // fresh socket for the first heartbeat
    _hbHttp.begin(_sslClient, url);
    _hbHttp.setReuse(true);
    _hbHttp.addHeader("Content-Type", "application/json");
    _hbHttp.addHeader("Authorization", "Bearer " + g_idToken);  // baked once; rebuilt on 401
    _hbInit = true;
  }

  JsonDocument body;
  body["fields"]["status"]["stringValue"]           = status;
  body["fields"]["lastHeartbeat"]["timestampValue"] = isoNow();
  if (currentQuestion.length() > 0)
    body["fields"]["currentQuestion"]["stringValue"] = currentQuestion;
  else
    body["fields"]["currentQuestion"]["nullValue"]   = nullptr;
  body["fields"]["activeSubject"]["stringValue"]     = subject;
  String bodyStr; serializeJson(body, bodyStr);

  int code = _hbHttp.sendRequest("PATCH", bodyStr);
  // Always drain the body — a reused keep-alive socket must be left clean.
  _hbHttp.getString();
  if (code == 401) {
    // Token rotated since we baked the Authorization header — refresh + rebuild.
    firebaseRefreshToken();
    _hbHttp.end();
    _hbInit = false;                     // next heartbeat re-inits with the new token
    return;
  }
  if (code != 200) {
    Serial.printf("[Firestore] heartbeat PATCH HTTP %d\n", code);
    if (code <= 0) { _hbHttp.end(); _hbInit = false; }  // socket dead — rebuild next time
  }
}

// NOTE: an earlier revision offloaded the two hot-path deviceState writes to a
// background FreeRTOS task with its OWN WiFiClientSecure. On this board that ran
// a second mbedTLS handshake concurrently with the main TLS connection and
// exhausted the internal heap — every following TLS call then failed with
// HTTP -1 and the device rebooted. Removed. The two writes were redundant anyway
// (the post-playback "listening" write already pushes the next question, and the
// 10 s heartbeat keeps the device "online"), so they're simply dropped from the
// turn. If live "feedback" status during grading is wanted back, set it
// server-side from answerQuestion (which already has admin access) — not with a
// concurrent device-side TLS connection.

// ── Read a pending remote command (start/stop) from the app, if any ──────────
// Returns "start" | "stop" | "none".
String firestoreReadCommand() {
  if (g_firebaseUid.isEmpty()) return "none";
  String url = "https://firestore.googleapis.com/v1/projects/";
  url += FIREBASE_PROJECT_ID;
  url += "/databases/(default)/documents/deviceState/" + g_firebaseUid;

  _initSSL();
  HTTPClient http;
  http.begin(_sslClient, url);
  http.addHeader("Authorization", "Bearer " + g_idToken);
  int code = http.GET();
  if (code != 200) { http.end(); return "none"; }
  JsonDocument resp;
  deserializeJson(resp, http.getString());
  http.end();
  String cmd = resp["fields"]["command"]["stringValue"].as<String>();
  return cmd.length() ? cmd : "none";
}
