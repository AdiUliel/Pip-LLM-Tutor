/**
 * Unit Test 2: Speaker (ES8311 codec + I2S DAC output)
 * Board: LCDWIKI 2.8" ESP32-S3 (ES3C28P / ES3N28P)
 *
 * What it tests:
 *   - ES8311 codec reachable on I2C bus (SDA=16, SCL=15)
 *   - I2S DAC output reaches the speaker via FM8002E amp (IO1)
 *   - Audible 440 Hz sine wave plays for ~3 seconds
 *
 * Pass criteria: Audible tone from speaker.
 * Fail criteria: ES8311 not found on I2C, or silence.
 *
 * Libraries: none beyond ESP32 built-in (Wire, driver/i2s)
 */

#include <Wire.h>
#include <driver/i2s.h>
#include <math.h>

// ── Pin definitions ───────────────────────────────────────────────────────────
#define PIN_AUDIO_EN   1   // LOW = speaker amp on
#define PIN_I2C_SDA   16
#define PIN_I2C_SCL   15
#define ES8311_ADDR   0x18
#define PIN_I2S_MCLK   4
#define PIN_I2S_BCLK   5
#define PIN_I2S_DOUT   6
#define PIN_I2S_LRCK   7
#define PIN_I2S_DIN    8   // not used in playback, still wired

#define SAMPLE_RATE   16000
#define I2S_PORT      I2S_NUM_0

// ── ES8311 helpers ────────────────────────────────────────────────────────────
static void codec_write(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(ES8311_ADDR);
  Wire.write(reg); Wire.write(val);
  Wire.endTransmission();
}

bool initCodec() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.beginTransmission(ES8311_ADDR);
  if (Wire.endTransmission() != 0) return false;

  codec_write(0x00, 0x1F); delay(20);  // reset
  codec_write(0x00, 0x00);
  codec_write(0x01, 0x30);  // MCLK from pin
  codec_write(0x02, 0x00);
  codec_write(0x03, 0x10);
  codec_write(0x04, 0x10);
  codec_write(0x06, 0x03);  // BCLK = MCLK/8
  codec_write(0x09, 0x00);  // ADC I2S 16-bit
  codec_write(0x0A, 0x00);  // DAC I2S 16-bit
  codec_write(0x0D, 0x01);
  codec_write(0x31, 0x60);  // DAC power up
  codec_write(0x37, 0x08);  // DAC volume 0 dB
  codec_write(0x44, 0x08);  // Route DAC to output
  return true;
}

// ── I2S init for playback ─────────────────────────────────────────────────────
void i2s_init() {
  i2s_config_t cfg = {
    .mode             = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate      = SAMPLE_RATE,
    .bits_per_sample  = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format   = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count    = 8,
    .dma_buf_len      = 512,
    .use_apll         = true,
    .tx_desc_auto_clear = true,
    .mclk_multiple    = I2S_MCLK_MULTIPLE_256,
  };
  i2s_pin_config_t pins = {
    .mck_io_num   = PIN_I2S_MCLK,
    .bck_io_num   = PIN_I2S_BCLK,
    .ws_io_num    = PIN_I2S_LRCK,
    .data_out_num = PIN_I2S_DOUT,
    .data_in_num  = I2S_PIN_NO_CHANGE,
  };
  i2s_driver_install(I2S_PORT, &cfg, 0, NULL);
  i2s_set_pin(I2S_PORT, &pins);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n===========================");
  Serial.println(" Unit Test 2: Speaker");
  Serial.println("===========================");

  // Enable speaker amp
  pinMode(PIN_AUDIO_EN, OUTPUT);
  digitalWrite(PIN_AUDIO_EN, LOW);  // LOW = amp on

  // Init codec
  if (!initCodec()) {
    Serial.println("[RESULT] FAIL — ES8311 not found on I2C. Check wiring.");
    return;
  }
  Serial.println("[ES8311] Codec init OK");

  // Init I2S
  i2s_init();
  Serial.println("[I2S] Started");

  // Generate 440 Hz sine wave and play for 3 seconds
  const int freq = 440;
  const int amplitude = 8000;
  const int bufSamples = 256;
  int16_t buf[bufSamples * 2]; // stereo
  size_t written;

  Serial.println("[Speaker] Playing 440 Hz tone for 3 seconds...");
  uint32_t start = millis();
  int phase = 0;

  while (millis() - start < 3000) {
    for (int i = 0; i < bufSamples; i++) {
      int16_t sample = (int16_t)(amplitude * sinf(2.0f * M_PI * freq * phase / SAMPLE_RATE));
      buf[i * 2]     = sample; // L
      buf[i * 2 + 1] = sample; // R
      phase = (phase + 1) % SAMPLE_RATE;
    }
    i2s_write(I2S_PORT, buf, sizeof(buf), &written, portMAX_DELAY);
  }

  i2s_driver_uninstall(I2S_PORT);
  digitalWrite(PIN_AUDIO_EN, HIGH); // amp off

  Serial.println("[RESULT] PASS — Tone played. Confirm audible output.");
}

void loop() {}
