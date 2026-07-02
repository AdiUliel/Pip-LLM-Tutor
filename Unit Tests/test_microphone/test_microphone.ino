/**
 * Unit Test 4: Microphone (ES8311 codec + I2S ADC input)
 * Board: LCDWIKI 2.8" ESP32-S3 (ES3C28P / ES3N28P)
 *
 * What it tests:
 *   - ES8311 codec reachable on I2C (SDA=16, SCL=15)
 *   - I2S ADC input captures audio from onboard mic
 *   - Peak amplitude reported to Serial — confirms mic is active
 *
 * Pass criteria: Peak amplitude > 500 when speaking near the mic.
 * Fail criteria: ES8311 not found, or peak stays near 0 (silent).
 *
 * Libraries: none beyond ESP32 built-in (Wire, driver/i2s)
 */

#include <Wire.h>
#include <driver/i2s.h>

// ── Pin definitions ───────────────────────────────────────────────────────────
#define PIN_I2C_SDA   16
#define PIN_I2C_SCL   15
#define ES8311_ADDR   0x18
#define PIN_I2S_MCLK   4
#define PIN_I2S_BCLK   5
#define PIN_I2S_DOUT   6
#define PIN_I2S_LRCK   7
#define PIN_I2S_DIN    8

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

  codec_write(0x00, 0x1F); delay(20);
  codec_write(0x00, 0x00);
  codec_write(0x01, 0x30);
  codec_write(0x02, 0x00);
  codec_write(0x03, 0x10);
  codec_write(0x04, 0x10);
  codec_write(0x06, 0x03);
  codec_write(0x09, 0x00);
  codec_write(0x0A, 0x00);
  codec_write(0x0D, 0x01);
  codec_write(0x0E, 0x02);
  codec_write(0x0F, 0xFF);
  codec_write(0x10, 0x1C);
  codec_write(0x11, 0x60);
  codec_write(0x15, 0x40);
  codec_write(0x17, 0xBF);  // high mic gain
  codec_write(0x1B, 0x19);  // PGA ~+24 dB
  return true;
}

// ── I2S init for recording ────────────────────────────────────────────────────
void i2s_init() {
  i2s_config_t cfg = {
    .mode             = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX),
    .sample_rate      = SAMPLE_RATE,
    .bits_per_sample  = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format   = I2S_CHANNEL_FMT_ONLY_LEFT,
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
    .data_in_num  = PIN_I2S_DIN,
  };
  i2s_driver_install(I2S_PORT, &cfg, 0, NULL);
  i2s_set_pin(I2S_PORT, &pins);
  i2s_zero_dma_buffer(I2S_PORT);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n============================");
  Serial.println(" Unit Test 4: Microphone");
  Serial.println("============================");

  if (!initCodec()) {
    Serial.println("[RESULT] FAIL — ES8311 not found on I2C.");
    return;
  }
  Serial.println("[ES8311] Codec init OK");

  i2s_init();
  Serial.println("[I2S] Started — speak into the mic...\n");
}

void loop() {
  static int16_t buf[256];
  size_t bytesRead = 0;
  i2s_read(I2S_PORT, buf, sizeof(buf), &bytesRead, portMAX_DELAY);

  int samples = bytesRead / 2;
  int16_t peak = 0;
  for (int i = 0; i < samples; i++) {
    int16_t v = abs(buf[i]);
    if (v > peak) peak = v;
  }

  // Simple bar graph
  int bars = peak / 500;
  char bar[21] = {0};
  for (int i = 0; i < min(bars, 20); i++) bar[i] = '#';
  Serial.printf("Peak: %5d  [%-20s] %s\n", peak, bar,
                peak > 500 ? "OK" : "quiet");
}
