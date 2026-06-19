#pragma once
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include "secrets.h"

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
  Serial.println("[Firebase] Token refreshed.");
  return g_idToken;
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
  String audioUrl;         // WAV of (feedback + next question)
  bool   shouldTakeBreak = false;
  bool   isCorrect       = false;
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
  while (millis() - start < timeoutMs) {
    _initSSL();
    HTTPClient http;
    http.begin(_sslClient, url);
    http.addHeader("Authorization", "Bearer " + g_idToken);

    int code = http.GET();
    if (code == 401) { firebaseRefreshToken(); http.end(); continue; }
    if (code != 200) { Serial.printf("[Firestore] poll HTTP %d\n", code); http.end(); delay(2000); continue; }

    JsonDocument resp;
    deserializeJson(resp, http.getString());
    http.end();

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
      return true;
    }
    if (status == "error") {
      Serial.println("[Firestore] turn error: " +
                     resp["fields"]["error"]["stringValue"].as<String>());
      return false;
    }
    delay(2000);
  }
  Serial.println("[Firestore] Timed out waiting for turn result.");
  return false;
}

// ─────────────────────────────────────────────────────────────────────────────
//   Voice identification flow (boot-time): kid says name + picks subject
// ─────────────────────────────────────────────────────────────────────────────

// ── Result of an identify exchange ───────────────────────────────────────────
struct IdentifyResult {
  bool   ok        = false;
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
      return true;
    }
    if (status == "error") {
      Serial.println("[Firestore] identify error: " +
                     resp["fields"]["error"]["stringValue"].as<String>());
      return false;
    }
    delay(1500);
  }
  Serial.println("[Firestore] Timed out waiting for identify result.");
  return false;
}

// ── Cloud Function /synthesizeSpeech: text → base64 WAV audio ────────────────
// Used by the identify flow for the very first prompt ("מי כאן?") since
// there's no on-device TTS and no pre-recorded audio files on the ESP32.
// Returns the base64-encoded WAV ready for speakFromBase64() — no Storage
// upload involved, saving ~1.5-3 s per round.
// Falls back to the legacy {audioUrl: "..."} shape for older Cloud builds.
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

// ── deviceState/{deviceId}: live status for the Flutter app ───────────────────
// The app reads deviceState/{uid}.status + lastHeartbeat (freshness => online),
// currentQuestion and activeSubject. Rules allow any signed-in user to write.
// Call writeDeviceState() on every status change and periodically (heartbeat).
void firestoreWriteDeviceState(const String& status,
                               const String& currentQuestion = "",
                               const String& subject = SESSION_SUBJECT) {
  if (g_firebaseUid.isEmpty()) return;

  // PATCH with an updateMask so we only touch the fields we own.
  String url = "https://firestore.googleapis.com/v1/projects/";
  url += FIREBASE_PROJECT_ID;
  url += "/databases/(default)/documents/deviceState/" + g_firebaseUid;
  url += "?updateMask.fieldPaths=status";
  url += "&updateMask.fieldPaths=lastHeartbeat";
  url += "&updateMask.fieldPaths=currentQuestion";
  url += "&updateMask.fieldPaths=activeSubject";

  JsonDocument body;
  body["fields"]["status"]["stringValue"]        = status;       // idle|asking|listening|feedback|break|error
  body["fields"]["lastHeartbeat"]["timestampValue"] = isoNow();
  if (currentQuestion.length() > 0)
    body["fields"]["currentQuestion"]["stringValue"] = currentQuestion;
  else
    body["fields"]["currentQuestion"]["nullValue"]   = nullptr;
  body["fields"]["activeSubject"]["stringValue"]   = subject;

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
}

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
