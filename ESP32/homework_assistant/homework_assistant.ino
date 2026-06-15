/**
 * Homework Assistant — ESP32-S3 Firmware
 * Board: LCDWIKI 2.8" ESP32-S3 Display (ES3C28P / ES3N28P)
 *
 * Flow:
 *   1. Boot → connect WiFi → Firebase anonymous auth → create session
 *   2. IDLE: wait for button press (IO2, active LOW)
 *   3. RECORDING: hold button → I2S audio capture from ES8311 mic
 *   4. Release button → encode PCM → POST to Cloud Function STT proxy
 *   5. PROCESSING: post question to Firestore → Cloud Function calls Gemini
 *   6. Poll Firestore for answer
 *   7. SPEAKING: stream TTS MP3 answer via speaker
 *   8. Back to IDLE
 *
 * Required libraries (Arduino IDE → Tools → Manage Libraries):
 *   - ArduinoJson  by Benoit Blanchon  (v7.x)
 *   - ESP32-audioI2S  by schreibfaul1
 *
 * Board settings (Arduino IDE):
 *   - Board: "ESP32S3 Dev Module"
 *   - Flash Size: 16MB (128Mb)
 *   - PSRAM: "OPI PSRAM"   ← IMPORTANT for the audio buffer
 *   - Partition Scheme: "Huge APP"
 *   - Upload Speed: 921600
 */

#include <WiFi.h>
#include <driver/i2s.h>   // ESP32-S3 built-in I2S driver
#include "secrets.h"
#include "pins.h"
#include "es8311.h"
#include "firebase_client.h"
#include "stt_client.h"
#include "tts_player.h"

// ── State machine ─────────────────────────────────────────────────────────────
enum State { IDLE, RECORDING, PROCESSING, SPEAKING };
State state = IDLE;

// ── Audio record buffer (PSRAM) ───────────────────────────────────────────────
uint8_t* recordBuf  = nullptr;
size_t   recordBytes = 0;

// ─────────────────────────────────────────────────────────────────────────────
// I2S setup for RECORDING (raw driver, full-duplex)
// Call before reading from the mic.
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
    .use_apll         = true,           // accurate clock for STT
    .tx_desc_auto_clear = true,
    .mclk_multiple    = I2S_MCLK_MULTIPLE_384,  // MCLK = 384 * 16000 = 6.144 MHz (matches official LCD Wiki example)
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

void connectWiFi() {
  Serial.printf("[WiFi] Connecting to %s", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n[WiFi] Connected: " + WiFi.localIP().toString());
  delay(500); // let the stack settle before HTTPS
}

// ─────────────────────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== Homework Assistant Booting ===");

  // Button — IO3 reads press, IO2 acts as GND (OUTPUT LOW)
  pinMode(PIN_BTN_GND, OUTPUT);
  digitalWrite(PIN_BTN_GND, LOW);
  pinMode(PIN_BTN, INPUT_PULLUP);

  // Speaker amp — disabled until needed
  pinMode(PIN_AUDIO_EN, OUTPUT);
  digitalWrite(PIN_AUDIO_EN, HIGH);

  // PSRAM check
  if (!psramFound()) {
    Serial.println("⚠️  PSRAM not found! Check board settings (PSRAM: OPI PSRAM).");
  } else {
    recordBuf = (uint8_t*)ps_malloc(RECORD_BUF_SIZE);
    Serial.printf("[PSRAM] Record buffer: %u bytes\n", RECORD_BUF_SIZE);
  }

  // Init ES8311 codec via I2C
  if (!initES8311()) {
    Serial.println("⚠️  ES8311 init failed. Audio may not work.");
  }

  // WiFi
  connectWiFi();

  // Firebase anonymous auth
  g_idToken = firebaseSignIn();
  if (g_idToken.isEmpty()) {
    Serial.println("❌ Firebase auth failed. Check FIREBASE_WEB_API_KEY in secrets.h");
    while (true) delay(1000);
  }

  // Create Firestore session
  g_sessionId = firestoreCreateSession();
  if (g_sessionId.isEmpty()) {
    Serial.println("❌ Could not create Firestore session.");
    while (true) delay(1000);
  }

  Serial.println("\n✅ Ready! Hold the button to ask a question.");
}

// ─────────────────────────────────────────────────────────────────────────────

void loop() {
  bool btnDown = (digitalRead(PIN_BTN) == LOW);

  // ── DEBUG: print pin state every 500 ms (remove once button works) ─────────
  static uint32_t lastDebug = 0;
  if (millis() - lastDebug > 500) {
    Serial.printf("[BTN] IO%d = %s\n", PIN_BTN, btnDown ? "LOW (pressed)" : "HIGH (idle)");
    lastDebug = millis();
  }

  // ── IDLE → start recording when button pressed ────────────────────────────
  if (state == IDLE && btnDown) {
    Serial.println("[Main] Recording...");
    state = RECORDING;
    recordBytes = 0;
    i2s_start_recording();
  }

  // ── RECORDING → capture audio while button is held ───────────────────────
  if (state == RECORDING) {
    if (btnDown && recordBytes < RECORD_BUF_SIZE) {
      size_t bytesRead = 0;
      i2s_read(I2S_PORT,
               recordBuf + recordBytes,
               min((size_t)1024, RECORD_BUF_SIZE - recordBytes),
               &bytesRead,
               portMAX_DELAY);
      recordBytes += bytesRead;

      // Safety: stop if buffer full
      if (recordBytes >= RECORD_BUF_SIZE) {
        Serial.println("[Main] Buffer full — stopping recording.");
        btnDown = false;
      }
    }

    // Button released → process
    if (!btnDown) {
      i2s_stop_recording();
      state = PROCESSING;
      Serial.printf("[Main] Recorded %u bytes (%.1f sec)\n",
                    recordBytes, recordBytes / (float)(SAMPLE_RATE * 2));

      if (recordBytes < 1000) {
        Serial.println("[Main] Recording too short, ignoring.");
        state = IDLE;
        return;
      }

      // ── Identify which stereo channel carries mic data ───────────────────
      // The ES8311 is a mono codec; only one I2S slot carries the mic, the
      // other is zeros/noise. Measure both and keep the louder one — never
      // hard-code a channel (we previously stripped ch0 blindly, which sends
      // silence to STT if the mic actually lands on ch1).
      int micChannel = 0;  // 0 = even indices, 1 = odd indices
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
        Serial.printf("[Main] ch0(even) RMS: %.0f  ch1(odd) RMS: %.0f  -> using ch%d\n",
                      rms0, rms1, micChannel);
      }

      // ── Strip the unused channel — keep only the mic channel (mono) ──────
      {
        int16_t* src = (int16_t*)recordBuf;
        int16_t* dst = (int16_t*)recordBuf;
        size_t frames = recordBytes / 4;  // each stereo frame = 4 bytes
        for (size_t i = 0; i < frames; i++) dst[i] = src[i * 2 + micChannel];
        recordBytes = frames * 2;  // now mono
      }
      Serial.printf("[Main] Mono bytes after channel strip: %u (%.1f sec)\n",
                    recordBytes, recordBytes / (float)(SAMPLE_RATE * 2));

      // ── Amplitude check — tells us if mic is actually working ─────────────
      int16_t* samples = (int16_t*)recordBuf;
      int32_t  peak = 0;
      int32_t  clipped = 0;
      int64_t  sumSq = 0;
      size_t   nSamples = recordBytes / 2;
      for (size_t i = 0; i < nSamples; i++) {
        int32_t s = abs((int32_t)samples[i]);
        if (s > peak) peak = s;
        if (s >= 32700) clipped++;   // within ~200 of max → effectively clipped
        sumSq += (int64_t)samples[i] * samples[i];
      }
      float rms = sqrt((float)(sumSq / (int64_t)nSamples));
      Serial.printf("[Main] Audio peak: %d  RMS: %.0f  clipped: %d/%u (%.1f%%)\n",
                    peak, rms, clipped, (unsigned)nSamples,
                    100.0f * clipped / nSamples);
      // Reject near-silence BEFORE calling STT. Google STT will hallucinate a
      // plausible sentence from noise (we saw a full Hebrew sentence come back
      // from a RMS-4 recording). Real speech is RMS ~900+; silence is RMS ~10.
      // A threshold of 100 cleanly separates them.
      if (rms < 100) {
        Serial.println("[Main] ⚠️  Near-silence (RMS < 100) — no speech captured, "
                       "skipping STT. Speak clearly close to the mic while holding the button.");
        state = IDLE;
        return;
      }

      // Send to STT proxy Cloud Function
      String question = transcribeAudio(recordBuf, recordBytes, g_idToken);
      if (question.isEmpty()) {
        Serial.println("[Main] STT returned empty transcript.");
        state = IDLE;
        return;
      }
      Serial.println("[Main] Question: " + question);

      // Post to Firestore
      String exchangeId = firestorePostQuestion(g_sessionId, question);
      if (exchangeId.isEmpty()) {
        state = IDLE;
        return;
      }

      // Poll for LLM answer + audio URL (30 second timeout)
      String answer, audioUrl;
      if (!firestorePollForAnswer(g_sessionId, exchangeId, answer, audioUrl, 30000)) {
        state = IDLE;
        return;
      }
      Serial.println("[Main] Answer: " + answer);
      Serial.println("[Main] Audio:  " + audioUrl);

      // Stream WAV from Storage → I2S (no library needed)
      state = SPEAKING;
      speakFromUrl(audioUrl);   // releases I2S when done
      state = IDLE;
      Serial.println("\n✅ Ready! Hold the button to ask another question.");
    }
  }
}
