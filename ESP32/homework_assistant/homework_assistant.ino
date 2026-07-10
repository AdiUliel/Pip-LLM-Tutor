/**
 * Homework Assistant — ESP32-S3 Firmware (Emotional Tutor, edge-to-edge)
 * Board: LCDWIKI 2.8" ESP32-S3 Display (ES3C28P / ES3N28P)
 *
 * This firmware drives the team's adaptive tutor loop end-to-end and shows the
 * pip_face animated emotions on the ILI9341 display. It also publishes
 * deviceState so the Flutter parent app sees the device live.
 *
 * Flow:
 *   1. Boot → ES8311 codec → pip_face → WiFi → NTP → Firebase anon auth
 *   2. Create a session (auto-detects the child profile by deviceId)
 *   3. Backend (onSessionCreated) generates the first question + its TTS audio
 *   4. Device SPEAKS the question (face: speaking) then LISTENS (face: listening)
 *   5. Child holds the push-to-talk button and answers → I2S capture (ended on
 *      button release) → STT proxy.
 *   6. Device posts a learning_turn → backend grades + Gemini feedback +
 *      next question + combined TTS audio
 *   7. Device shows the returned EMOTION on pip_face, SPEAKS feedback+next
 *      question, then loops back to step 4 with the next question
 *
 * Required libraries (Arduino IDE → Manage Libraries):
 *   - ArduinoJson      by Benoit Blanchon (v7.x)
 *   - TFT_eSPI         by Bodmer            (for pip_face; configure User_Setup)
 *   See INTEGRATION.md for the TFT_eSPI User_Setup for this exact board.
 *
 * Board settings (Arduino IDE):
 *   - Board: "ESP32S3 Dev Module"
 *   - Flash Size: 16MB (128Mb)
 *   - PSRAM: "OPI PSRAM"   ← IMPORTANT (audio buffer + pip_face sprite)
 *   - Partition Scheme: "Huge APP"
 *   - Upload Speed: 921600
 */

#include <WiFi.h>
#include <time.h>
#include <driver/i2s.h>   // ESP32-S3 built-in I2S driver
#include "secrets.h"
#include "pins.h"
#include "es8311.h"
// pip_face animated face (TFT_eSPI). You MUST have TFT_eSPI installed AND its
// User_Setup configured for this board (copy pip_face/User_Setup_LCDWIKI.h over
// libraries/TFT_eSPI/User_Setup.h), or TFT_eSPI will crash at init. See
// INTEGRATION.md.
#include "PipFace.h"
#include "firebase_client.h"
#include "stt_client.h"
#include "tts_player.h"
#include "tts_cache.h"     // SD-backed cache for static/repeated phrases (after the 3 above)
#include "esp_sleep.h"     // deep sleep + ext1 button wake (idle power policy)
#include "driver/rtc_io.h" // hold the button pins through deep sleep
#include "driver/gpio.h"   // hold the (non-RTC) backlight pin off through deep sleep

// ── Phase 1: collapse the turn into ONE synchronous processTurn call ──────────
// 1 = device calls processTurn (grade + Gemini + TTS + writes done server-side,
//     result returned inline; audio is the HTTP body). Removes the device's own
//     Firestore write, the Eventarc trigger, the poll loop, and the separate
//     audio download. 0 = legacy post+poll path (firestorePostLearningTurn +
//     firestorePollForTurnResult). Flip to 0 to A/B or fall back.
#ifndef USE_PROCESS_TURN
#define USE_PROCESS_TURN 1
#endif

// ── Phase 2: fold STT into processTurn (upload audio; server transcribes) ─────
// 1 = the device uploads the raw mono PCM straight to processTurn (?fmt=pcm16),
//     which runs STT itself, then grade + Gemini + TTS — ONE round trip for the
//     whole turn, with NO separate transcribeAudio handshake and a RAW-PCM upload
//     (no base64, −33% bytes over the slow uplink). Requires USE_PROCESS_TURN.
// 0 = transcribe on-device first (transcribeAudio), then send the TEXT to
//     processTurn (or to post+poll when USE_PROCESS_TURN=0). Flip to 0 to A/B or
//     fall back. The boot identify flow always uses on-device STT regardless.
#ifndef USE_PROCESS_TURN_AUDIO
#define USE_PROCESS_TURN_AUDIO 1
#endif

// ── Low speaker volume (bench-testing) ────────────────────────────────────────
// 1 = quiet the TTS at boot so you're not blasted while testing at your desk.
//     Purely a digital-gain change on the ES8311 DAC (reg 0x32) — the audio path
//     is otherwise identical, just softer. 0 = normal shipping loudness.
// Tune ES8311_DAC_VOL_LOW to taste: higher hex = louder (0xBF = 0 dB unity;
// 0xC5 ≈ +3 dB is the normal level in es8311.h).
#ifndef SPEAKER_LOW_VOLUME
#define SPEAKER_LOW_VOLUME 0
#endif
#define ES8311_DAC_VOL_LOW 0x8F   // ~-24 dB vs unity — clearly quiet but audible

// ── State machine ─────────────────────────────────────────────────────────────
enum State { IDLE, RECORDING, PROCESSING, SPEAKING };
State state = IDLE;

// ── Audio record buffer (PSRAM) ───────────────────────────────────────────────
uint8_t* recordBuf  = nullptr;
size_t   recordBytes = 0;

// ── Tutor loop state ──────────────────────────────────────────────────────────
String g_currentQuestion = "";
// Subject of the active session. Seeded from SESSION_SUBJECT, but the
// kid's voice-pick during identify_subject overwrites the session doc in
// Firestore, and firestoreWaitForCurrentQuestion reads that value back
// into this global so STT/UI logic uses the actual chosen subject.
String g_currentSubject = SESSION_SUBJECT;
int    g_stars           = 0;     // running correct-answer count (shown on strip)
bool   g_haveFace        = false; // pip_face initialised OK

// ── Heartbeat ─────────────────────────────────────────────────────────────────
uint32_t g_lastHeartbeat = 0;
const uint32_t HEARTBEAT_MS = 10000;  // app online-timeout is 30s

// ── Idle power policy ─────────────────────────────────────────────────────────
// Timestamp of the child's last interaction (button press / finished answer).
// The loop turns the screen off after g_screenOffMinutes and deep-sleeps after
// g_deviceSleepMinutes of no interaction (both from the child's settings; see
// firebase_client.h). g_screenOff tracks whether the backlight is currently off.
uint32_t g_lastInteractionMs = 0;
bool     g_screenOff         = false;

// ─────────────────────────────────────────────────────────────────────────────
// I2S setup for RECORDING (raw driver, full-duplex)
// ─────────────────────────────────────────────────────────────────────────────
void i2s_start_recording() {
  i2s_config_t cfg = {
    .mode             = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX),
    .sample_rate      = SAMPLE_RATE,
    .bits_per_sample  = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format   = I2S_CHANNEL_FMT_RIGHT_LEFT,  // stereo — matches ES8311's 32-bit frame
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
    .data_in_num  = PIN_I2S_DIN,
  };
  i2s_driver_install(I2S_PORT, &cfg, 0, NULL);
  i2s_set_pin(I2S_PORT, &pins);
  i2s_zero_dma_buffer(I2S_PORT);
}

void i2s_stop_recording() {
  i2s_driver_uninstall(I2S_PORT);
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────
void connectWiFi() {
  Serial.printf("[WiFi] Connecting to %s", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n[WiFi] Connected: " + WiFi.localIP().toString());
  delay(500);
}

void syncClock() {
  // Real time is needed for Firestore timestamps + the app heartbeat freshness.
  configTime(0, 0, NTP_SERVER);     // UTC; gmtime_r() is used downstream
  Serial.print("[Time] Syncing NTP");
  time_t now = time(nullptr);
  uint32_t start = millis();
  while (now < 1000000000 && millis() - start < 10000) {
    delay(300);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println(now < 1000000000 ? "  (failed — using placeholder time)"
                                  : "  ok: " + isoNow());
}

// Map the backend emotion vocabulary onto a pip_face emotion label.
const char* pipEmotionFor(const String& e) {
  if (e == "celebrating") return "celebrating";
  if (e == "happy")       return "happy";
  if (e == "encouraging") return "encouraging";
  if (e == "concerned")   return "concerned";
  if (e == "neutral")     return "happy";
  return "encouraging";
}

// These are no-ops when pip_face begin() failed, so the rest of the firmware
// never has to care whether the display actually came up.
void faceEmotion(const char* label) {
  if (g_haveFace) Pip::setEmotion(label);
}
void faceStatus(const char* status, int mood = -1) {
  if (g_haveFace) Pip::setDeviceStatus(status, mood);
}
void faceStrip(const String& text) {
  if (g_haveFace) Pip::setStrip(text.c_str(), g_stars);
}
void faceTick() {
  if (g_haveFace) Pip::tick();
}

// Show the device's pairing code ON THE SCREEN (not just the Serial Monitor) so
// a parent can read it and link the device in the app. Shown whenever the device
// is unpaired — the code is stable (MAC-derived, devicePairingDocId in
// firebase_client.h), so it's the same value the parent types under "TUTOR-".
void showPairingCode() {
  const String code = devicePairingDocId();     // "TUTOR-XXXXXX"
  Serial.println("[Pairing] Show on screen — enter this code in the app: " + code);
  faceEmotion("sleepy");                         // calm "waiting to be set up" face
  faceStrip("להגדרה באפליקציה: " + code);
}

// Post-process the audio in recordBuf in-place: pick the channel that
// actually carries the mic, strip to mono, and reject near-silence.
// Returns true if the audio is usable for STT; false if too short / silent.
// recordBytes is updated to the mono-stripped length on success.
bool processRecordedAudio() {
  if (recordBytes < 1000) {
    Serial.println("[Audio] too short, ignoring.");
    return false;
  }
  // Pick the louder stereo channel (the codec is mono on one side).
  int micChannel = 0;
  {
    int16_t* raw = (int16_t*)recordBuf;
    size_t frames = recordBytes / 4;
    int64_t sumSq0 = 0, sumSq1 = 0;
    for (size_t i = 0; i < frames; i++) {
      sumSq0 += (int64_t)raw[i*2]   * raw[i*2];
      sumSq1 += (int64_t)raw[i*2+1] * raw[i*2+1];
    }
    float rms0 = sqrt((float)(sumSq0 / (int64_t)frames));
    float rms1 = sqrt((float)(sumSq1 / (int64_t)frames));
    micChannel = (rms1 > rms0) ? 1 : 0;
    Serial.printf("[Audio] ch0 RMS: %.0f  ch1 RMS: %.0f  -> ch%d\n", rms0, rms1, micChannel);
  }
  // Strip to mono.
  {
    int16_t* src = (int16_t*)recordBuf;
    int16_t* dst = (int16_t*)recordBuf;
    size_t frames = recordBytes / 4;
    for (size_t i = 0; i < frames; i++) dst[i] = src[i * 2 + micChannel];
    recordBytes = frames * 2;
  }
  // Boost quiet / far-field speech to a healthy STT level, then reject only true
  // silence (see micAutoGainMono). Floor lowered 250→130 because the auto-gain now
  // rescues quiet-but-real speech that used to be discarded.
  {
    float rms = micAutoGainMono();
    Serial.printf("[Audio] mono RMS: %.0f\n", rms);
    if (rms < 130) {
      Serial.println("[Audio] near-silence; speak closer.");
      return false;
    }
  }
  return true;
}

// ── Software mic auto-gain (far-field boost) ─────────────────────────────────
// The ES8311 analog PGA is already maxed (es8311.h reg14=0x1A), so a child who
// isn't right on top of the mic records quietly and the near-silence rejects
// below used to discard the turn ("speak closer"). This scales the captured mono
// utterance up toward a healthy full-scale peak, so quiet/distant
// speech reaches STT at a usable level. It's adaptive and peak-based: a loud close
// answer gets ~1x (never amplified into clipping), a quiet distant one up to
// MIC_MAX_GAIN. Operates in place on recordBuf (already stripped to mono); returns
// the PRE-gain RMS so the caller can still reject true silence.
#define MIC_TARGET_PEAK  22000   // ~-3.4 dBFS: loud, with headroom (full scale 32767)
#define MIC_MAX_GAIN     8.0f    // cap (+18 dB) so a silent room's noise floor isn't blown up
static float micAutoGainMono() {
  int16_t* s = (int16_t*)recordBuf;
  size_t   n = recordBytes / 2;
  if (n == 0) return 0.0f;
  int32_t peak = 1; int64_t sumSq = 0;
  for (size_t i = 0; i < n; i++) {
    int32_t a = s[i] < 0 ? -s[i] : s[i];
    if (a > peak) peak = a;
    sumSq += (int64_t)s[i] * s[i];
  }
  float rms = sqrtf((float)(sumSq / (int64_t)n));   // pre-gain (for the silence reject)
  float g   = (float)MIC_TARGET_PEAK / (float)peak;
  if (g > MIC_MAX_GAIN) g = MIC_MAX_GAIN;
  if (g > 1.0f) {                                   // never attenuate a healthy signal
    for (size_t i = 0; i < n; i++) {
      int32_t v = (int32_t)lroundf(s[i] * g);
      if (v >  32767) v =  32767;                   // saturate (rare: g is derived from peak)
      if (v < -32768) v = -32768;
      s[i] = (int16_t)v;
    }
    Serial.printf("[Mic] auto-gain x%.1f (RMS %.0f -> %.0f)\n",
                  (double)g, (double)rms, (double)(rms * g));
  }
  return rms;
}

// Blocking record: face listens, waits for the push-to-talk button, captures the
// answer while it's held. Used by the boot-time identify flow before the main
// state-machine loop takes over.
bool recordOneAnswerBlocking(uint32_t maxWaitMs = 20000) {
  faceEmotion("listening");
  Serial.println("[Identify] Press the button and answer...");
  uint32_t waitStart = millis();
  while (digitalRead(PIN_BTN) != LOW) {
    faceTick();
    if (millis() - waitStart > maxWaitMs) {
      Serial.println("[Identify] No button press within timeout.");
      return false;
    }
    delay(20);
  }
  Serial.println("[Identify] Recording...");
  recordBytes = 0;
  i2s_start_recording();
  while (digitalRead(PIN_BTN) == LOW && recordBytes < RECORD_BUF_SIZE) {
    size_t bytesRead = 0;
    i2s_read(I2S_PORT,
             recordBuf + recordBytes,
             min((size_t)1024, RECORD_BUF_SIZE - recordBytes),
             &bytesRead, portMAX_DELAY);
    recordBytes += bytesRead;
  }
  i2s_stop_recording();
  Serial.printf("[Identify] Recorded %u bytes (%.1f s)\n",
                recordBytes, recordBytes / (float)(SAMPLE_RATE * 2));
  return true;
}

// Capture one spoken answer during the boot identify flow, RE-ASKING until the
// child actually says something the STT can transcribe. Never returns empty: it
// keeps re-prompting (with `retryPrompt`) and listening until it has a
// transcript, so the caller never has to fall back to the generic session just
// because the child was slow, silent, or didn't trigger the wake word.
String identifyCaptureAnswer(const char* retryPrompt) {
  while (true) {
    if (recordOneAnswerBlocking() && processRecordedAudio()) {
      faceEmotion("thinking");
      String t = transcribeAudio(recordBuf, recordBytes, g_idToken, "he-IL");
      if (!t.isEmpty()) return t;
    }
    // No trigger, silence, or unintelligible speech — re-ask and listen again.
    // retryPrompt is a fixed string → SD cache makes the re-ask instant.
    faceEmotion("encouraging");
    speakTextCached(retryPrompt);
  }
}

// Boot-time voice identification flow:
//   1. Robot asks "מי כאן?" → kid speaks name → identify_child exchange
//   2. Robot replies "שלום X, מה נלמד היום?" → kid says "חשבון" or "אנגלית"
//      → identify_subject exchange
//   3. The identify_subject reply also contains the FIRST question — so
//      after this returns, the main loop is ready to start practising.
// Returns true on success. Returns false when the device isn't paired to any
// parent/child (needsPairing) or the welcome TTS itself failed to play — in
// BOTH cases the caller must NOT fall back to a generic/anonymous session; it
// should wait and retry instead (see setup()).
bool runIdentifyFlow(String& firstQuestionOut, String& firstAudioUrlOut) {
  // ── Step 1 — ask "who's here?" ────────────────────────────────────────────
  const char* welcomeText = "היי! מי כאן? תגיד לי את שמך אחרי שתלחץ על הכפתור.";
  faceEmotion("speaking");
  if (!speakTextCached(welcomeText)) {     // SD cache → instant after first boot
    Serial.println("[Identify] TTS welcome failed.");
    return false;
  }

  // Repeat "who's here?" until the cloud matches a real child — never fall back
  // to a generic session just because the child was slow, silent, or
  // mis-recognised. The only non-retry exit is needsPairing: the device's UID
  // isn't linked to any parent, so no spoken name can ever match and looping
  // would trap the child forever.
  const char* nameRetry = "לא שמעתי. תלחץ על הכפתור ותגיד שוב את שמך.";
  IdentifyResult child;
  while (true) {
    String nameTranscript = identifyCaptureAnswer(nameRetry);

    String childExchange = firestorePostIdentifyChild(g_sessionId, nameTranscript);
    if (childExchange.isEmpty() ||
        !firestorePollForIdentifyResult(g_sessionId, childExchange, child)) {
      delay(1000);   // transient cloud error — pause, then ask again
      continue;
    }
    Serial.printf("[Identify] matched: %s (%s)\n",
                  child.matchedChildName.c_str(), child.matchedChildId.c_str());
    if (child.matchedChildId.length() > 0) break;            // success
    if (child.needsPairing) {
      // Local cached phrase, not child.audioUrl — this can retry every ~15s
      // indefinitely (see setup()), so we don't want a fresh cloud TTS
      // synthesis on every retry. Text must match STATIC_TTS_PHRASES verbatim.
      faceEmotion("encouraging");
      speakTextCached("אין משתמש משויך למכשיר הזה, ולא הוגדר תלמיד. יש להגדיר אותי דרך האפליקציה.");
      Serial.println("[Identify] device not paired to any parent/child.");
      showPairingCode();   // put the code on the SCREEN so the parent can pair
      return false;
    }
    // Name not recognised — re-ask and keep looping (cached static phrase).
    faceEmotion("encouraging");
    speakTextCached("לא הכרתי את השם הזה. תגיד אותו שוב, בבקשה.");
  }
  g_childId = child.matchedChildId;

  // ── Step 2 — speak greeting + ask for subject, record answer ──────────────
  if (!child.audioUrl.isEmpty()) {
    faceEmotion("speaking");
    speakAudio(child.audioUrl);
  }

  // Repeat "math or english?" until the cloud returns a recognised subject
  // (which also carries the first question). Same rule as the name step: keep
  // asking rather than dropping into a generic session on a missed answer.
  IdentifyResult subj;
  while (true) {
    String subjectTranscript = identifyCaptureAnswer("לא שמעתי. חשבון או אנגלית?");

    String subjectExchange = firestorePostIdentifySubject(g_sessionId, subjectTranscript);
    if (subjectExchange.isEmpty() ||
        !firestorePollForIdentifyResult(g_sessionId, subjectExchange, subj)) {
      delay(1000);   // transient cloud error — pause, then ask again
      continue;
    }
    if (subj.subject.length() > 0) break;                    // success
    // Subject not understood — re-ask and keep looping (cached static phrase).
    faceEmotion("encouraging");
    speakTextCached("לא הבנתי. תגיד חשבון או אנגלית?");
  }
  Serial.printf("[Identify] subject: %s\n", subj.subject.c_str());
  g_currentSubject  = subj.subject;
  firstQuestionOut  = subj.nextQuestion;
  firstAudioUrlOut  = subj.audioUrl;
  return true;
}

// Speak the current question and move into the "waiting for the child" state.
void askCurrentQuestion(const String& audioUrl) {
  Serial.println("[Tutor] Question: " + g_currentQuestion);
  faceStatus("asking");
  faceStrip(g_currentQuestion);
  firestoreWriteDeviceState("asking", g_currentQuestion);
  faceTick();

  state = SPEAKING;
  speakAudio(audioUrl);            // releases I2S when done

  // Now listen.
  state = IDLE;
  faceStatus("listening");
  firestoreWriteDeviceState("listening", g_currentQuestion);
  Serial.println("\n🎤 Hold the button and say your answer.");
}

// ─────────────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== Homework Assistant (Emotional Tutor) Booting ===");

  // If we woke from deep sleep (idle power policy), the button pins were held
  // through the RTC domain — release them and return them to normal GPIO so the
  // pinMode/digitalRead below work. Harmless on a cold boot.
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1) {
    Serial.println("[Boot] Woke from deep sleep (button press).");
  }
  rtc_gpio_hold_dis((gpio_num_t)PIN_BTN_GND);
  rtc_gpio_hold_dis((gpio_num_t)PIN_BTN);
  rtc_gpio_deinit((gpio_num_t)PIN_BTN_GND);
  rtc_gpio_deinit((gpio_num_t)PIN_BTN);

  // Push-to-talk button. IO3 reads press, IO2 acts as GND (OUTPUT LOW).
  // (If the IO3 expansion line is flaky on your unit, switch PIN_BTN to GPIO0 /
  //  the onboard BOOT button in pins.h — see INTEGRATION.md.)
  pinMode(PIN_BTN_GND, OUTPUT);
  digitalWrite(PIN_BTN_GND, LOW);
  pinMode(PIN_BTN, INPUT_PULLUP);

  // Speaker amp — disabled until needed.
  pinMode(PIN_AUDIO_EN, OUTPUT);
  digitalWrite(PIN_AUDIO_EN, HIGH);

  // Display backlight (pip_face / TFT_eSPI does not manage it). Release any hold
  // left on the pin by enterDeepSleep() first (no-op on a cold boot) so it can be
  // driven again.
  gpio_hold_dis((gpio_num_t)PIN_LCD_BL);
  pinMode(PIN_LCD_BL, OUTPUT);
  digitalWrite(PIN_LCD_BL, HIGH);

  // PSRAM check + record buffer.
  if (!psramFound()) {
    Serial.println("⚠️  PSRAM not found! Check board settings (PSRAM: OPI PSRAM).");
  } else {
    recordBuf = (uint8_t*)ps_malloc(RECORD_BUF_SIZE);
    Serial.printf("[PSRAM] Record buffer: %u bytes\n", RECORD_BUF_SIZE);
  }

  // SD card: enables the TTS cache for static/repeated phrases. Independent of
  // Wi-Fi; fails safe (caching off, cloud fallback) if no FAT32 card is present.
  sdCacheBegin();

  // pip_face (needs PSRAM for its 240x240 sprite, and a correct TFT_eSPI
  // User_Setup for this board).
  g_haveFace = Pip::begin();
  if (!g_haveFace) Serial.println("⚠️  pip_face init failed (PSRAM/TFT_eSPI). Continuing audio-only.");
  faceEmotion("thinking");
  faceTick();

  // ES8311 codec via I2C.
  if (!initES8311()) Serial.println("⚠️  ES8311 init failed. Audio may not work.");

  // Low-volume bench-testing mode: quiet the TTS so you're not blasted at your
  // desk. Compile-time toggle (SPEAKER_LOW_VOLUME) — see the top of this file.
#if SPEAKER_LOW_VOLUME
  es8311SetDacVolume(ES8311_DAC_VOL_LOW);
  Serial.printf("[Audio] LOW-VOLUME test mode: DAC vol = 0x%02X (normal 0x%02X)\n",
                ES8311_DAC_VOL_LOW, ES8311_DAC_VOL_DEFAULT);
#endif

  // Network + time + auth.
  connectWiFi();
  syncClock();

  // Boot-time auth: restores the saved anonymous UID from NVS when possible so
  // a `children.deviceId` paired via the Flutter app keeps matching across
  // device reboots. Falls back to a fresh signUp on the very first boot or if
  // the saved refresh token has been invalidated.
  g_idToken = firebaseBootAuth();
  if (g_idToken.isEmpty()) {
    Serial.println("❌ Firebase auth failed. Check FIREBASE_WEB_API_KEY in secrets.h");
    faceStatus("error");
    while (true) { faceTick(); delay(200); }
  }

  // Publish the user-visible pairing code → this device's UID so the parent app
  // can pair against the right Firebase identity. Cheap (one PATCH) and idempotent.
  firestoreWritePairingCode();

  // Prefetch the FIXED phrases (boot + reprompts) into the SD cache so their first
  // use is an instant hit, not a ~2–3 s synth. Strings here MUST match the ones
  // passed to speakTextCached() below verbatim (same bytes → same cache key). Runs
  // once; on later boots every phrase is already on the card and is skipped.
  static const char* const STATIC_TTS_PHRASES[] = {
    "היי! מי כאן? תגיד לי את שמך אחרי שתלחץ על הכפתור.",
    "לא שמעתי. תלחץ על הכפתור ותגיד שוב את שמך.",
    "לא שמעתי אותך. נסה לענות שוב.",
    "לא הכרתי את השם הזה. תגיד אותו שוב, בבקשה.",
    "לא שמעתי. חשבון או אנגלית?",
    "לא הבנתי. תגיד חשבון או אנגלית?",
    "אופס, הייתה בעיית תקשורת. בוא ננסה שוב.",
    "אין משתמש משויך למכשיר הזה, ולא הוגדר תלמיד. יש להגדיר אותי דרך האפליקציה.",
  };
  sdCacheWarm(STATIC_TTS_PHRASES,
              sizeof(STATIC_TTS_PHRASES) / sizeof(STATIC_TTS_PHRASES[0]));

  // Two boot paths:
  //   A) CHILD_ID hardcoded → legacy fast path: cloud auto-creates the first
  //      question via onSessionCreated(awaitingFirstQuestion:true).
  //   B) CHILD_ID empty     → voice identification flow:
  //      "מי כאן?" → identify_child → "חשבון או אנגלית?" → identify_subject
  //      → first question is created by handleIdentifySubject.
  const bool useIdentifyFlow = (String(CHILD_ID).length() == 0);

  g_sessionId = firestoreCreateSession(SESSION_SUBJECT, !useIdentifyFlow);
  if (g_sessionId.isEmpty()) {
    Serial.println("❌ Could not create Firestore session.");
    faceStatus("error");
    firestoreWriteDeviceState("error");
    while (true) { faceTick(); delay(200); }
  }
  firestoreWriteDeviceState("idle");

  String firstAudio;
  bool ready = false;

  if (useIdentifyFlow) {
    Serial.println("[Tutor] Running voice identification flow...");
    ready = runIdentifyFlow(g_currentQuestion, firstAudio);
    // runIdentifyFlow() returns false when the device isn't paired to any
    // parent/child yet, or on a transient local TTS failure. NO session may
    // start in either case (no generic/anonymous fallback): keep waiting and
    // retrying until the parent pairs the device and configures a child.
    while (!ready) {
      Serial.println("[Tutor] Not ready to start (unpaired or a transient error) — retrying in 15s...");
      firestoreWriteDeviceState("error");
      delay(15000);
      ready = runIdentifyFlow(g_currentQuestion, firstAudio);
    }
  } else {
    Serial.println("[Tutor] Waiting for the first question from the backend...");
    faceEmotion("thinking");
    ready = firestoreWaitForCurrentQuestion(g_sessionId, g_currentQuestion, firstAudio, 30000, &g_currentSubject);
    if (!ready) {
      Serial.println("❌ No first question. Check Cloud Functions / Vertex AI setup.");
      faceStatus("error");
      firestoreWriteDeviceState("error");
      while (true) { faceTick(); delay(200); }
    }
  }

  askCurrentQuestion(firstAudio);
  noteInteraction();  // start the idle timers from the first question
}

// ─────────────────────────────────────────────────────────────────────────────
// Return to the IDLE "waiting for the child" state, so the device is ready for
// the next button press after a turn or an early-out (empty STT, silence,
// backend hiccup).
void backToListening() {
  state = IDLE;
  faceStatus("listening");
}

// A capture didn't yield a usable answer (empty STT or near-silence). Re-speak
// the CURRENT question so the child knows what to answer — not just a generic
// "try again" — then re-arm listening so the device never goes silently "stuck".
// The full question is also already shown on the strip via askCurrentQuestion().
void repromptAfterMiss() {
  String q = g_currentQuestion;
  q.trim();
  const char* tail = "נסה לענות שוב.";
  // STATIC reprompt (no embedded question) so it's one fixed, prefetchable phrase
  // — an instant SD hit instead of a unique per-question synth every miss. The
  // question itself is already shown on the strip via faceStrip(), so the child
  // still sees what to answer.
  (void)q;   // kept for context/logging; intentionally not spoken
  String msg = String("לא שמעתי אותך. ") + tail;
  faceEmotion("encouraging");
  speakTextCached(msg);
  backToListening();
}

// The upload to processTurn/processTurnAudio failed (WiFi hiccup, backend
// down, etc). Previously this was a silent backToListening() — the child got
// no feedback at all and could easily think the device just didn't hear them.
// Speak a short fixed phrase (SD-cached, so it's instant) so at least the
// child knows something went wrong and to try again.
void repromptAfterNetworkError() {
  faceEmotion("encouraging");
  speakTextCached("אופס, הייתה בעיית תקשורת. בוא ננסה שוב.");
  backToListening();
}

// Process whatever is in recordBuf (stereo PCM, recordBytes long): strip to the
// mic channel, reject silence, run STT, post the learning turn, speak feedback +
// next question, then go back to listening. Shared by the wake-word path and the
// legacy button path so the heavy logic lives in exactly one place.
void processCapturedAnswer() {
  if (recordBytes < 1000) {
    Serial.println("[Main] Recording too short, ignoring.");
    backToListening();
    return;
  }

  // ── Pick the stereo slot that actually carries the mic (mono codec) ────────
  int micChannel = 0;
  {
    int16_t* raw = (int16_t*)recordBuf;
    size_t frames = recordBytes / 4;
    int64_t sumSq0 = 0, sumSq1 = 0;
    for (size_t i = 0; i < frames; i++) {
      sumSq0 += (int64_t)raw[i*2]   * raw[i*2];
      sumSq1 += (int64_t)raw[i*2+1] * raw[i*2+1];
    }
    float rms0 = sqrt((float)(sumSq0 / (int64_t)frames));
    float rms1 = sqrt((float)(sumSq1 / (int64_t)frames));
    micChannel = (rms1 > rms0) ? 1 : 0;
    Serial.printf("[Main] ch0 RMS: %.0f  ch1 RMS: %.0f  -> ch%d\n", rms0, rms1, micChannel);
  }
  // Strip to mono (keep the mic channel).
  {
    int16_t* src = (int16_t*)recordBuf;
    int16_t* dst = (int16_t*)recordBuf;
    size_t frames = recordBytes / 4;
    for (size_t i = 0; i < frames; i++) dst[i] = src[i * 2 + micChannel];
    recordBytes = frames * 2;
  }

  // Boost quiet / far-field speech to a healthy STT level, THEN reject only true
  // silence (see micAutoGainMono). The server STT-empty path (turn.sttEmpty) is the
  // backstop for noise that slips through, so this floor can sit low (250→130)
  // without wasting real turns.
  {
    float rms = micAutoGainMono();
    Serial.printf("[Main] Answer RMS: %.0f\n", rms);
    if (rms < 130) {
      Serial.println("[Main] ⚠️  Near-silence — skipping. Speak close to the mic.");
      repromptAfterMiss();   // re-speak the question so the child knows what to answer
      return;
    }
  }

  // STT — pick language by the SESSION's subject (read from Firestore after
  // identify_subject), not the compile-time SESSION_SUBJECT. So an English
  // lesson is recognised as English even when flashed with SESSION_SUBJECT="math".
  // [lat] t0 = child finished recording (entry to processing). All deltas below
  // are measured against this and against each preceding stage.
  uint32_t _latT0 = millis();

  faceEmotion("thinking");
  faceTick();
  const char* sttLang = g_currentSubject.equalsIgnoreCase("english") ? "en-US" : "he-IL";

  TurnResult turn;

#if USE_PROCESS_TURN && USE_PROCESS_TURN_AUDIO
  // Phase 2: upload the raw mono PCM straight to processTurn. ONE round trip does
  // STT + grade + (gated) Gemini + TTS + the doc writes, returned inline (metadata
  // in headers, MP3 in the body). Removes the separate transcribeAudio handshake
  // and the base64 upload overhead entirely.
  uint32_t _latPreTurn = millis();
  if (!firestoreProcessTurnAudio(g_sessionId, recordBuf, recordBytes, sttLang, turn)) {
    repromptAfterNetworkError();
    return;
  }
  uint32_t _latPostTurn = millis();
  if (turn.sttEmpty) {
    Serial.println("[Main] Server STT found no speech — asking child to repeat.");
    repromptAfterMiss();   // re-speak the question so the child knows what to answer
    return;
  }
  Serial.printf("[lat]   A->E processTurnAudio(stt+turn)=%lu ms\n",
                (unsigned long)(_latPostTurn - _latPreTurn));
#else
  // On-device STT first, then send the TEXT onward. (Phase 1, or legacy post+poll
  // when USE_PROCESS_TURN=0.)
  uint32_t _latPreStt = millis();   // after local pre-processing (mono strip, RMS gate)
  String answer = transcribeAudio(recordBuf, recordBytes, g_idToken, sttLang);
  uint32_t _latPostStt = millis();
  Serial.printf("[lat]   B stt=%lu ms\n", (unsigned long)(_latPostStt - _latPreStt));
  if (answer.isEmpty()) {
    Serial.println("[Main] STT returned empty transcript — asking child to repeat.");
    repromptAfterMiss();   // re-speak the question so the child knows what to answer
    return;
  }
  Serial.println("[Main] Child answered: " + answer);

  #if USE_PROCESS_TURN
  // Phase 1: ONE synchronous call does grade + (gated) Gemini + TTS + the doc
  // writes, and returns the result inline (metadata in headers, MP3 in the body).
  // Replaces the device write + Eventarc trigger + poll loop + separate download.
  uint32_t _latPreTurn = millis();
  if (!firestoreProcessTurn(g_sessionId, answer, turn)) {
    repromptAfterNetworkError();
    return;
  }
  uint32_t _latPostTurn = millis();
  Serial.printf("[lat]   B->E processTurn=%lu ms\n", (unsigned long)(_latPostTurn - _latPreTurn));
  #else
  // Legacy post + poll path (kept as fallback; flip USE_PROCESS_TURN to 0).
  uint32_t _latPrePost = millis();
  String exchangeId = firestorePostLearningTurn(g_sessionId, answer);
  uint32_t _latPostPost = millis();
  Serial.printf("[lat]   C post=%lu ms\n", (unsigned long)(_latPostPost - _latPrePost));
  if (exchangeId.isEmpty()) { backToListening(); return; }

  uint32_t _latPrePoll = millis();
  if (!firestorePollForTurnResult(g_sessionId, exchangeId, turn, 30000)) {
    backToListening();
    return;
  }
  uint32_t _latPostPoll = millis();
  Serial.printf("[lat]   D+E+F trigger+compute+poll=%lu ms\n",
                (unsigned long)(_latPostPoll - _latPrePoll));
  #endif
#endif

  if (turn.isCorrect) g_stars++;
  Serial.println("[Main] Feedback: " + turn.spokenFeedback);
  Serial.println("[Main] Next:     " + turn.nextQuestion);
  Serial.println("[Main] Emotion:  " + turn.emotion);

  // Show emotion + speak feedback (audio already includes the next question).
  faceEmotion(pipEmotionFor(turn.emotion));
  // (No deviceState write here — the post-playback "listening" write below pushes
  // the next question; keeping a write on this line only delayed playback ~2 s.)
  faceStrip(turn.nextQuestion);

  state = SPEAKING;
  uint32_t _latPrePlay = millis();
#if USE_PROCESS_TURN
  speakFromBuffer(turn.audioBuf, turn.audioLen);   // inline MP3 from the turn response
  if (turn.audioBuf) { free(turn.audioBuf); turn.audioBuf = nullptr; }
#else
  speakAudio(turn.audioUrl);          // installs its own I2S; releases it when done
#endif

  // [lat] One-line round-trip summary. g_ttsFirstSampleMs was stamped the instant
  // the first audio sample hit I2S (robot starts speaking).
  uint32_t _latFirstSound = g_ttsFirstSampleMs ? g_ttsFirstSampleMs : millis();
#if USE_PROCESS_TURN && USE_PROCESS_TURN_AUDIO
  // Phase 2: STT is folded into processTurnAudio, so there's no separate stt stage.
  Serial.printf(
    "[lat] === turn: processTurnAudio(stt+turn)=%lu decode=%lu "
    "| RECORD->FIRST_SOUND=%lu ms ===\n",
    (unsigned long)(_latPostTurn - _latPreTurn),
    (unsigned long)(_latFirstSound - _latPrePlay),
    (unsigned long)(_latFirstSound - _latT0));
#elif USE_PROCESS_TURN
  Serial.printf(
    "[lat] === turn: stt=%lu processTurn=%lu decode=%lu "
    "| RECORD->FIRST_SOUND=%lu ms ===\n",
    (unsigned long)(_latPostStt  - _latPreStt),
    (unsigned long)(_latPostTurn - _latPreTurn),
    (unsigned long)(_latFirstSound - _latPrePlay),
    (unsigned long)(_latFirstSound - _latT0));
#else
  Serial.printf(
    "[lat] === turn: stt=%lu post=%lu trig+compute+poll=%lu audio(G)=%lu "
    "| RECORD->FIRST_SOUND=%lu ms ===\n",
    (unsigned long)(_latPostStt  - _latPreStt),
    (unsigned long)(_latPostPost - _latPrePost),
    (unsigned long)(_latPostPoll - _latPrePoll),
    (unsigned long)(_latFirstSound - _latPrePlay),
    (unsigned long)(_latFirstSound - _latT0));
#endif

  // Session ended explicitly (exit intent / continue declined / 50-min limit).
  // Power down rather than reboot: on this board there is no power-latch, so deep
  // sleep IS "off" (screen dark, CPU halted). A button press wakes it, and setup()
  // re-runs the identify flow into a fresh session — same end state as the old
  // ESP.restart(), minus the device springing back to life fully powered.
  if (turn.sessionEnded) {
    Serial.println("[Main] Session ended, reason=" + turn.endReason + ". Powering down — press the button to start again.");
    delay(1500);         // brief beat after the spoken goodbye before the screen goes dark
    enterDeepSleep();    // does not return; wakes on button into a fresh session
    return;
  }

  // The next question is now current.
  g_currentQuestion = turn.nextQuestion;

  if (turn.shouldTakeBreak) {
    Serial.println("[Main] Taking a short break.");
    faceStatus("break");
    firestoreWriteDeviceState("break", g_currentQuestion);
    faceTick();
    delay(4000);
  }

  // Ready for the next answer.
  firestoreWriteDeviceState("listening", g_currentQuestion);
  Serial.println("\n🎤 Hold the button and say your answer.");
  noteInteraction();                  // answer finished — restart the idle timers
  backToListening();
}

// ─────────────────────────────────────────────────────────────────────────────
// Idle power policy — screen off, then deep sleep, after periods of no
// interaction. Both thresholds come from the child's settings (g_screenOffMinutes
// / g_deviceSleepMinutes) and replace the old fixed 5-min cloud inactivity cut.
// ─────────────────────────────────────────────────────────────────────────────
void screenOff() {
  if (g_screenOff) return;
  Serial.println("[Idle] No interaction — turning the screen off.");
  digitalWrite(PIN_LCD_BL, LOW);
  g_screenOff = true;
}

void wakeScreen() {
  if (!g_screenOff) return;
  Serial.println("[Idle] Waking the screen.");
  digitalWrite(PIN_LCD_BL, HIGH);
  g_screenOff = false;
}

// "The child just did something" — reset the idle timers and wake the screen.
void noteInteraction() {
  g_lastInteractionMs = millis();
  wakeScreen();
}

// Deep sleep until the push-to-talk button is pressed. Waking is a full reset, so
// setup() re-runs the identify flow into a fresh session. Called both by the idle
// timeout and at explicit session end — on this board (no power latch) this deep
// sleep is the device's "off": screen dark, CPU halted, wakes on the button.
void enterDeepSleep() {
  Serial.println("[Power] Entering deep sleep — press the button to wake.");
  faceStatus("idle");
  firestoreWriteDeviceState("idle");
  digitalWrite(PIN_LCD_BL, LOW);
  // IO45 is not an RTC pad, so rtc_gpio_hold can't keep it low. Latch it with the
  // digital IO hold + deep-sleep hold so the backlight stays fully off (not
  // floating) for the whole sleep; setup() releases the hold on wake.
  gpio_hold_en((gpio_num_t)PIN_LCD_BL);
  gpio_deep_sleep_hold_en();
  delay(200);  // let the Firestore write flush before the CPU halts

  // The button is wired between IO2 (driven LOW as its GND) and IO3
  // (INPUT_PULLUP). In deep sleep the normal GPIO drivers are off, so hold IO2
  // LOW through the RTC domain and enable the RTC pull-up on IO3, then wake when
  // IO3 goes LOW (button pressed). setup() releases these holds on boot.
  rtc_gpio_init((gpio_num_t)PIN_BTN_GND);
  rtc_gpio_set_direction((gpio_num_t)PIN_BTN_GND, RTC_GPIO_MODE_OUTPUT_ONLY);
  rtc_gpio_set_level((gpio_num_t)PIN_BTN_GND, 0);
  rtc_gpio_hold_en((gpio_num_t)PIN_BTN_GND);

  rtc_gpio_init((gpio_num_t)PIN_BTN);
  rtc_gpio_set_direction((gpio_num_t)PIN_BTN, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pulldown_dis((gpio_num_t)PIN_BTN);
  rtc_gpio_pullup_en((gpio_num_t)PIN_BTN);

  esp_sleep_enable_ext1_wakeup(1ULL << PIN_BTN, ESP_EXT1_WAKEUP_ANY_LOW);
  esp_deep_sleep_start();  // does not return
}

// Called each idle loop: enforce the screen-off, then the deep-sleep, threshold.
void checkIdlePolicy() {
  if (state != IDLE) return;
  uint32_t idleMs = millis() - g_lastInteractionMs;
  if (g_deviceSleepMinutes > 0 &&
      idleMs >= (uint32_t)g_deviceSleepMinutes * 60000UL) {
    enterDeepSleep();  // does not return
  } else if (g_screenOffMinutes > 0 && !g_screenOff &&
             idleMs >= (uint32_t)g_screenOffMinutes * 60000UL) {
    screenOff();
  }
}

// ─────────────────────────────────────────────────────────────────────────────
void loop() {
  faceTick();  // ~30fps internally; cheap to call every iteration
  checkIdlePolicy();  // screen-off / deep-sleep after inactivity

  // Heartbeat so the app keeps showing the device online. Uses the keep-alive
  // path (firestoreHeartbeat) so repeats cost ~0.1 s instead of a ~2 s handshake —
  // the device stays responsive to the button between heartbeats. Meaningful
  // state transitions still use firestoreWriteDeviceState().
  if (millis() - g_lastHeartbeat > HEARTBEAT_MS) {
    g_lastHeartbeat = millis();
    const char* hb = (state == IDLE) ? "listening" : "asking";
    firestoreHeartbeat(hb, g_currentQuestion);
  }

  // ── IDLE → start recording the child's answer when the button is pressed ────
  bool btnDown = (digitalRead(PIN_BTN) == LOW);

  if (state == IDLE && btnDown) {
    Serial.println("[Main] Recording answer...");
    noteInteraction();                // wake the screen + reset the idle timers
    state = RECORDING;
    recordBytes = 0;
    faceEmotion("listening");
    i2s_start_recording();
  }

  // RECORDING → capture while the button is held.
  if (state == RECORDING) {
    if (btnDown && recordBytes < RECORD_BUF_SIZE) {
      size_t bytesRead = 0;
      i2s_read(I2S_PORT,
               recordBuf + recordBytes,
               min((size_t)1024, RECORD_BUF_SIZE - recordBytes),
               &bytesRead, portMAX_DELAY);
      recordBytes += bytesRead;
      if (recordBytes >= RECORD_BUF_SIZE) {
        Serial.println("[Main] Buffer full — stopping recording.");
        btnDown = false;
      }
    }

    if (!btnDown) {  // button released → process this answer
      i2s_stop_recording();
      state = PROCESSING;
      Serial.printf("[Main] Recorded %u bytes (%.1f sec)\n",
                    recordBytes, recordBytes / (float)(SAMPLE_RATE * 2));
      processCapturedAnswer();          // STT → grade → speak pipeline
    }
  }
}
