#pragma once

// ── WiFi ─────────────────────────────────────────────────────────────────────
#define WIFI_SSID       "POCO F4"
#define WIFI_PASSWORD   "11111111"

// ── Firebase ─────────────────────────────────────────────────────────────────
// Firebase Console → Project Settings → General → Web API Key
#define FIREBASE_WEB_API_KEY  "AIzaSyABxr5DlWydIXW7EBHk7nYh2r3qlf4df_I"

// Firebase project ID (from .firebaserc)
#define FIREBASE_PROJECT_ID   "llm-tutor-d721e"

// Cloud Function region (must match where your functions are deployed)
// Check Firebase Console → Functions → your function URL to confirm region
#define CLOUD_FUNCTIONS_REGION  "europe-west10"

// ── Language ─────────────────────────────────────────────────────────────────
// BCP-47 language code for Speech-to-Text.
// Common codes: "he-IL" (Hebrew), "en-US" (English), "ar-IL" (Arabic)
#define STT_LANGUAGE_CODE  "he-IL"

// ── Tutor session ────────────────────────────────────────────────────────────
// Subject the device starts a session with: "math" or "english".
#define SESSION_SUBJECT  "math"

// Child profile binding.
// The device first auto-detects the child whose `deviceId` equals this device's
// Firebase UID (set that in the Flutter app's child config). If none is found it
// falls back to CHILD_ID below. Leave "" to run generic (non-personalised)
// sessions. Find a child's id in Firestore → children/{id}.
#define CHILD_ID  ""

// ── Clock (NTP) ──────────────────────────────────────────────────────────────
// Real wall-clock time is required for Firestore timestamps and for the app's
// "device online" heartbeat freshness check.
#define NTP_SERVER  "pool.ntp.org"
