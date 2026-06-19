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
 *   5. Child holds the button and answers → I2S capture → STT proxy
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
// ── pip_face on/off switch ────────────────────────────────────────────────────
// Set to 0 to build & boot WITHOUT the display. The audio tutor loop runs fine
// without it — use this to isolate a display/TFT_eSPI problem from the rest of
// the device. With USE_PIP_FACE=1 you MUST have TFT_eSPI installed AND its
// User_Setup configured for this board (copy pip_face/User_Setup_LCDWIKI.h over
// libraries/TFT_eSPI/User_Setup.h), or TFT_eSPI will crash at init.
#define USE_PIP_FACE 1
#if USE_PIP_FACE
  #include "PipFace.h"     // animated face (TFT_eSPI). See INTEGRATION.md.
#endif
#include "firebase_client.h"
#include "stt_client.h"
#include "tts_player.h"

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

// These are no-ops when USE_PIP_FACE==0 (or when begin() failed) so the rest of
// the firmware never has to care whether the display is present.
void faceEmotion(const char* label) {
#if USE_PIP_FACE
  if (g_haveFace) Pip::setEmotion(label);
#else
  (void)label;
#endif
}
void faceStatus(const char* status, int mood = -1) {
#if USE_PIP_FACE
  if (g_haveFace) Pip::setDeviceStatus(status, mood);
#else
  (void)status; (void)mood;
#endif
}
void faceStrip(const String& text) {
#if USE_PIP_FACE
  if (g_haveFace) Pip::setStrip(text.c_str(), g_stars);
#else
  (void)text;
#endif
}
void faceTick() {
#if USE_PIP_FACE
  if (g_haveFace) Pip::tick();
#endif
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
  // Silence reject.
  {
    int16_t* samples = (int16_t*)recordBuf;
    int64_t sumSq = 0; size_t n = recordBytes / 2;
    for (size_t i = 0; i < n; i++) sumSq += (int64_t)samples[i] * samples[i];
    float rms = sqrt((float)(sumSq / (int64_t)n));
    Serial.printf("[Audio] mono RMS: %.0f\n", rms);
    if (rms < 100) {
      Serial.println("[Audio] near-silence; speak closer.");
      return false;
    }
  }
  return true;
}

// Blocking record: face listens, waits for button press, captures while
// held, stops on release. Used by the boot-time identify flow before the
// main state-machine loop takes over button handling.
bool recordOneAnswerBlocking(uint32_t maxWaitMs = 20000) {
  Serial.println("[Identify] Press the button and answer...");
  faceEmotion("listening");
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

// Boot-time voice identification flow:
//   1. Robot asks "מי כאן?" → kid speaks name → identify_child exchange
//   2. Robot replies "שלום X, מה נלמד היום?" → kid says "חשבון" or "אנגלית"
//      → identify_subject exchange
//   3. The identify_subject reply also contains the FIRST question — so
//      after this returns, the main loop is ready to start practising.
// Returns true on success; on failure the caller can fall back to the
// legacy "awaitingFirstQuestion" flow.
bool runIdentifyFlow(String& firstQuestionOut, String& firstAudioUrlOut) {
  // ── Step 1 — ask "who's here?" ────────────────────────────────────────────
  String welcomeUrl = cloudSynthesizeSpeech("היי! מי כאן? תגיד לי את שמך אחרי שתלחץ על הכפתור.");
  if (welcomeUrl.isEmpty()) {
    Serial.println("[Identify] TTS welcome failed.");
    return false;
  }
  faceEmotion("speaking");
  speakAudio(welcomeUrl);

  if (!recordOneAnswerBlocking()) return false;
  if (!processRecordedAudio()) {
    // Speak a gentle retry prompt then try once more.
    String retryUrl = cloudSynthesizeSpeech("לא שמעתי. תלחץ על הכפתור ותגיד שוב את שמך.");
    if (!retryUrl.isEmpty()) { faceEmotion("encouraging"); speakAudio(retryUrl); }
    if (!recordOneAnswerBlocking() || !processRecordedAudio()) return false;
  }
  faceEmotion("thinking");
  String nameTranscript = transcribeAudio(recordBuf, recordBytes, g_idToken, "he-IL");
  if (nameTranscript.isEmpty()) {
    Serial.println("[Identify] empty name transcript.");
    return false;
  }

  String childExchange = firestorePostIdentifyChild(g_sessionId, nameTranscript);
  if (childExchange.isEmpty()) return false;

  IdentifyResult child;
  if (!firestorePollForIdentifyResult(g_sessionId, childExchange, child)) return false;
  Serial.printf("[Identify] matched: %s (%s)\n",
                child.matchedChildName.c_str(), child.matchedChildId.c_str());
  if (child.matchedChildId.length() > 0) g_childId = child.matchedChildId;

  // ── Step 2 — speak greeting + ask for subject, record answer ──────────────
  if (!child.audioUrl.isEmpty()) {
    faceEmotion("speaking");
    speakAudio(child.audioUrl);
  }

  if (!recordOneAnswerBlocking()) return false;
  if (!processRecordedAudio()) {
    String retryUrl = cloudSynthesizeSpeech("לא שמעתי. חשבון או אנגלית?");
    if (!retryUrl.isEmpty()) { faceEmotion("encouraging"); speakAudio(retryUrl); }
    if (!recordOneAnswerBlocking() || !processRecordedAudio()) return false;
  }
  faceEmotion("thinking");
  String subjectTranscript = transcribeAudio(recordBuf, recordBytes, g_idToken, "he-IL");
  if (subjectTranscript.isEmpty()) {
    Serial.println("[Identify] empty subject transcript.");
    return false;
  }

  String subjectExchange = firestorePostIdentifySubject(g_sessionId, subjectTranscript);
  if (subjectExchange.isEmpty()) return false;

  IdentifyResult subj;
  if (!firestorePollForIdentifyResult(g_sessionId, subjectExchange, subj)) return false;
  Serial.printf("[Identify] subject: %s\n", subj.subject.c_str());
  if (subj.subject.length() > 0) g_currentSubject = subj.subject;
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

  // Button — IO3 reads press, IO2 acts as GND (OUTPUT LOW).
  // (If the IO3 expansion line is flaky on your unit, switch PIN_BTN to GPIO0 /
  //  the onboard BOOT button in pins.h — see INTEGRATION.md.)
  pinMode(PIN_BTN_GND, OUTPUT);
  digitalWrite(PIN_BTN_GND, LOW);
  pinMode(PIN_BTN, INPUT_PULLUP);

  // Speaker amp — disabled until needed.
  pinMode(PIN_AUDIO_EN, OUTPUT);
  digitalWrite(PIN_AUDIO_EN, HIGH);

  // Display backlight (pip_face / TFT_eSPI does not manage it).
  pinMode(PIN_LCD_BL, OUTPUT);
  digitalWrite(PIN_LCD_BL, HIGH);

  // PSRAM check + record buffer.
  if (!psramFound()) {
    Serial.println("⚠️  PSRAM not found! Check board settings (PSRAM: OPI PSRAM).");
  } else {
    recordBuf = (uint8_t*)ps_malloc(RECORD_BUF_SIZE);
    Serial.printf("[PSRAM] Record buffer: %u bytes\n", RECORD_BUF_SIZE);
  }

  // pip_face (needs PSRAM for its 240x240 sprite, and a correct TFT_eSPI
  // User_Setup for this board).
#if USE_PIP_FACE
  g_haveFace = Pip::begin();
  if (!g_haveFace) Serial.println("⚠️  pip_face init failed (PSRAM/TFT_eSPI). Continuing audio-only.");
  faceEmotion("thinking");
  faceTick();
#else
  Serial.println("[pip_face] disabled at build time (USE_PIP_FACE=0).");
#endif

  // ES8311 codec via I2C.
  if (!initES8311()) Serial.println("⚠️  ES8311 init failed. Audio may not work.");

  // Network + time + auth.
  connectWiFi();
  syncClock();

  g_idToken = firebaseSignIn();
  if (g_idToken.isEmpty()) {
    Serial.println("❌ Firebase auth failed. Check FIREBASE_WEB_API_KEY in secrets.h");
    faceStatus("error");
    while (true) { faceTick(); delay(200); }
  }

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
    if (!ready) {
      Serial.println("⚠️  Identify flow failed — falling back to legacy generic-session path.");
      // Recreate the session in legacy mode so onSessionCreated kicks in.
      g_sessionId = firestoreCreateSession(SESSION_SUBJECT, true);
    }
  }
  if (!ready) {
    Serial.println("[Tutor] Waiting for the first question from the backend...");
    faceEmotion("thinking");
    ready = firestoreWaitForCurrentQuestion(g_sessionId, g_currentQuestion, firstAudio, 30000, &g_currentSubject);
  }
  if (!ready) {
    Serial.println("❌ No first question. Check Cloud Functions / Vertex AI setup.");
    faceStatus("error");
    firestoreWriteDeviceState("error");
    while (true) { faceTick(); delay(200); }
  }

  askCurrentQuestion(firstAudio);
}

// ─────────────────────────────────────────────────────────────────────────────
void loop() {
  faceTick();  // ~30fps internally; cheap to call every iteration

  // Heartbeat so the app keeps showing the device online.
  if (millis() - g_lastHeartbeat > HEARTBEAT_MS) {
    g_lastHeartbeat = millis();
    const char* hb = (state == IDLE) ? "listening" : "asking";
    firestoreWriteDeviceState(hb, g_currentQuestion);
  }

  bool btnDown = (digitalRead(PIN_BTN) == LOW);

  // ── IDLE → start recording the child's answer when button pressed ──────────
  if (state == IDLE && btnDown) {
    Serial.println("[Main] Recording answer...");
    state = RECORDING;
    recordBytes = 0;
    faceEmotion("listening");
    i2s_start_recording();
  }

  // ── RECORDING → capture while button held ─────────────────────────────────
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

      if (recordBytes < 1000) {
        Serial.println("[Main] Recording too short, ignoring.");
        state = IDLE;
        faceStatus("listening");
        return;
      }

      // ── Pick the stereo slot that actually carries the mic (mono codec) ────
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

      // Reject near-silence before paying for STT (STT hallucinates on noise).
      {
        int16_t* samples = (int16_t*)recordBuf;
        int64_t sumSq = 0; size_t n = recordBytes / 2;
        for (size_t i = 0; i < n; i++) sumSq += (int64_t)samples[i] * samples[i];
        float rms = sqrt((float)(sumSq / (int64_t)n));
        Serial.printf("[Main] Answer RMS: %.0f\n", rms);
        if (rms < 100) {
          Serial.println("[Main] ⚠️  Near-silence — skipping. Speak close to the mic.");
          state = IDLE;
          faceStatus("listening");
          return;
        }
      }

      // STT — pick language by the SESSION's subject (read from Firestore
      // after identify_subject), not the compile-time SESSION_SUBJECT.
      // So an English lesson is recognised as English even when the device
      // was flashed with SESSION_SUBJECT="math".
      faceEmotion("thinking");
      faceTick();
      const char* sttLang = g_currentSubject.equalsIgnoreCase("english") ? "en-US" : "he-IL";
      String answer = transcribeAudio(recordBuf, recordBytes, g_idToken, sttLang);
      if (answer.isEmpty()) {
        Serial.println("[Main] STT returned empty transcript.");
        state = IDLE;
        faceStatus("listening");
        return;
      }
      Serial.println("[Main] Child answered: " + answer);

      // Post learning turn → backend grades + feedback + next question + audio.
      firestoreWriteDeviceState("feedback", g_currentQuestion);
      String exchangeId = firestorePostLearningTurn(g_sessionId, answer);
      if (exchangeId.isEmpty()) { state = IDLE; faceStatus("listening"); return; }

      TurnResult turn;
      if (!firestorePollForTurnResult(g_sessionId, exchangeId, turn, 30000)) {
        state = IDLE;
        faceStatus("listening");
        return;
      }

      if (turn.isCorrect) g_stars++;
      Serial.println("[Main] Feedback: " + turn.spokenFeedback);
      Serial.println("[Main] Next:     " + turn.nextQuestion);
      Serial.println("[Main] Emotion:  " + turn.emotion);

      // Show emotion + speak feedback (audio already includes the next question).
      faceEmotion(pipEmotionFor(turn.emotion));
      firestoreWriteDeviceState("feedback", turn.nextQuestion);
      faceStrip(turn.nextQuestion);

      state = SPEAKING;
      speakAudio(turn.audioUrl);

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
      state = IDLE;
      faceStatus("listening");
      firestoreWriteDeviceState("listening", g_currentQuestion);
      Serial.println("\n🎤 Hold the button and say your answer.");
    }
  }
}
