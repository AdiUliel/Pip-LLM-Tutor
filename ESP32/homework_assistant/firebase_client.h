#pragma once
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "secrets.h"

// ─────────────────────────────────────────────────────────────────────────────
// Firebase REST client
// Handles: anonymous sign-in, token refresh, Firestore read/write
// ─────────────────────────────────────────────────────────────────────────────

static String g_idToken      = "";
static String g_refreshToken = "";
static String g_sessionId    = "";
static String g_firebaseUid  = "";  // Firebase anonymous UID (from sign-in response)

// Shared SSL client — setInsecure() skips cert verification (fine for IoT prototypes)
static WiFiClientSecure _sslClient;

static void _initSSL() {
  _sslClient.stop();         // close any previous connection (different host reuse bug)
  _sslClient.setInsecure();  // no CA bundle needed
}

// ── Anonymous sign-in via Firebase Auth REST API ──────────────────────────────
// Returns the idToken, stores refreshToken globally.
// The idToken expires after 1 hour — call firebaseRefreshToken() to renew.
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

// ── Create a session document in Firestore ────────────────────────────────────
// Returns the auto-generated session document ID.
String firestoreCreateSession() {
  if (g_idToken.isEmpty()) return "";

  String url = "https://firestore.googleapis.com/v1/projects/";
  url += FIREBASE_PROJECT_ID;
  url += "/databases/(default)/documents/sessions";

  _initSSL();
  HTTPClient http;
  http.begin(_sslClient, url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + g_idToken);

  // Build session document
  JsonDocument body;
  body["fields"]["deviceId"]["stringValue"] = g_firebaseUid;  // Firebase UID (matches auth.uid)
  body["fields"]["createdAt"]["timestampValue"] = "2000-01-01T00:00:00Z"; // placeholder
  body["fields"]["lastActivity"]["timestampValue"] = "2000-01-01T00:00:00Z";

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

  // Document name format: "projects/.../databases/.../documents/sessions/SESSION_ID"
  String name = resp["name"].as<String>();
  String sessionId = name.substring(name.lastIndexOf('/') + 1);
  Serial.println("[Firestore] Session created: " + sessionId);
  return sessionId;
}

// ── Post a question as a new exchange document ────────────────────────────────
// Returns the auto-generated exchange document ID.
String firestorePostQuestion(const String& sessionId, const String& question) {
  String url = "https://firestore.googleapis.com/v1/projects/";
  url += FIREBASE_PROJECT_ID;
  url += "/databases/(default)/documents/sessions/";
  url += sessionId + "/exchanges";

  _initSSL();
  HTTPClient http;
  http.begin(_sslClient, url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + g_idToken);

  JsonDocument body;
  body["fields"]["question"]["stringValue"] = question;
  body["fields"]["answer"]["nullValue"]     = nullptr;
  body["fields"]["status"]["stringValue"]   = "pending";
  // Use server timestamp via REST field transform (simplified: use fixed string)
  body["fields"]["askedAt"]["timestampValue"] = "2000-01-01T00:00:00Z";

  String bodyStr;
  serializeJson(body, bodyStr);

  int code = http.POST(bodyStr);
  if (code != 200) {
    Serial.printf("[Firestore] postQuestion failed: HTTP %d\n", code);
    http.end();
    return "";
  }

  JsonDocument resp;
  deserializeJson(resp, http.getString());
  http.end();

  String name = resp["name"].as<String>();
  String exchangeId = name.substring(name.lastIndexOf('/') + 1);
  Serial.println("[Firestore] Question posted, exchange: " + exchangeId);
  return exchangeId;
}

// ── Poll until status == "done", then return answer text + audioUrl ───────────
// answer   : output — the LLM's text answer
// audioUrl : output — public Firebase Storage URL to the WAV file (may be "")
// Returns true if an answer was received, false on timeout/error.
bool firestorePollForAnswer(const String& sessionId,
                             const String& exchangeId,
                             String&       answer,
                             String&       audioUrl,
                             uint32_t      timeoutMs = 30000) {
  String url = "https://firestore.googleapis.com/v1/projects/";
  url += FIREBASE_PROJECT_ID;
  url += "/databases/(default)/documents/sessions/";
  url += sessionId + "/exchanges/" + exchangeId;

  uint32_t start = millis();

  while (millis() - start < timeoutMs) {
    _initSSL();
    HTTPClient http;
    http.begin(_sslClient, url);
    http.addHeader("Authorization", "Bearer " + g_idToken);

    int code = http.GET();
    if (code == 401) {
      // Token expired — refresh and retry
      Serial.println("[Firestore] Token expired, refreshing...");
      firebaseRefreshToken();
      http.end();
      continue;
    }
    if (code != 200) {
      Serial.printf("[Firestore] poll failed: HTTP %d\n", code);
      http.end();
      delay(2000);
      continue;
    }

    JsonDocument resp;
    deserializeJson(resp, http.getString());
    http.end();

    String status = resp["fields"]["status"]["stringValue"].as<String>();
    Serial.println("[Firestore] status = " + status);

    if (status == "done") {
      answer   = resp["fields"]["answer"]["stringValue"].as<String>();
      audioUrl = resp["fields"]["audioUrl"]["stringValue"].as<String>();
      return true;
    }
    if (status == "error") {
      String errMsg = resp["fields"]["error"]["stringValue"].as<String>();
      Serial.println("[Firestore] Cloud Function reported an error: " + errMsg);
      return false;
    }

    delay(2000); // poll every 2 seconds
  }

  Serial.println("[Firestore] Timed out waiting for answer.");
  return false;
}
