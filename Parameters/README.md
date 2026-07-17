# Parameters

All hard-coded (compile-time) parameters of the ESP32 firmware. Changing any of
them requires re-compiling and re-flashing the firmware.

They live in four files under `ESP32/homework_assistant/`:

| File | What it holds |
|---|---|
| `secrets.h` | Credentials + per-deployment config (copy from `secrets_template.h`) |
| `pins.h` | GPIO pin mapping + audio capture config for the LCDWIKI ESP32-S3 board |
| `homework_assistant.ino` (top of file) | Feature flags, bench-testing/demo switches, timing |
| `es8311.h` | Audio codec (volume / gain) registers |

`ESP32/parameters.h` is a fully-annotated reference copy of the board pin table.

---

## 1. Credentials & deployment config — `secrets.h`

> Gitignored. Copy `secrets_template.h` → `secrets.h` and fill in.

| Parameter | Default | Description |
|---|---|---|
| `WIFI_SSID` / `WIFI_PASSWORD` | — | WiFi network the device connects to |
| `FIREBASE_WEB_API_KEY` | — | Firebase Console → Project Settings → Web API Key |
| `FIREBASE_PROJECT_ID` | — | Firebase project id (from `.firebaserc`) |
| `CLOUD_FUNCTIONS_REGION` | `"europe-west10"` | Region the Cloud Functions are deployed to |
| `STT_LANGUAGE_CODE` | `"he-IL"` | BCP-47 language for Speech-to-Text (`"en-US"`, `"ar-IL"`, …) |
| `SESSION_SUBJECT` | `"math"` | Subject a session starts with: `"math"` or `"english"` |
| `CHILD_ID` | `""` | Fallback child profile id if no child has this device's UID; `""` = generic session |
| `NTP_SERVER` | `"pool.ntp.org"` | Time server (needed for Firestore timestamps + heartbeat freshness) |

## 2. Feature flags & testing switches — `homework_assistant.ino`

| Parameter | Default | Description |
|---|---|---|
| `USE_PROCESS_TURN` | `1` | `1` = one synchronous `processTurn` cloud call per turn (grade+LLM+TTS inline). `0` = legacy post-and-poll path (A/B fallback) |
| `USE_PROCESS_TURN_AUDIO` | `1` | `1` = upload raw PCM to `processTurn`; server runs STT (one round-trip, −33% bytes). `0` = transcribe on-device first. Requires `USE_PROCESS_TURN` |
| `SPEAKER_LOW_VOLUME` | `0` | `1` = quiet bench-testing volume (digital DAC gain only). `0` = normal loudness |
| `ES8311_DAC_VOL_LOW` | `0x8F` | DAC volume register used when `SPEAKER_LOW_VOLUME=1` (~−24 dB vs unity) |
| `SCREEN_OFF_TEST_SECONDS` | `0` | >0 forces backlight-off after this many **seconds** (bench testing); `0` = use the child's minutes policy from the app |
| `DEEP_SLEEP_TEST_SECONDS` | `0` | >0 forces deep-sleep after this many **seconds** (bench testing); `0` = minutes policy. Keep ≥ screen-off |
| `SHOW_PAIRING_CODE_SECONDS` | `0` | >0 holds the `TUTOR-XXXXXX` pairing code on screen this long at **every** boot; `0` = only when unpaired |
| `DEMO_FRESH_DEVICE` | `0` | `1` = wipe saved identity each boot → device behaves factory-new (pairing demo). Turn off after demos |
| `BOREDOM_NUDGE_SECONDS` | `40` | Seconds of no interaction (screen on, waiting for an answer) before Pip shows a brief PLAYFUL wink to re-engage the child; `0` disables. Visual only, once per idle stretch |
| `HEARTBEAT_MS` | `10000` | Device→app heartbeat interval (ms); the app's online timeout is 30 s |
| `MIC_TARGET_PEAK` | `22000` | Auto-gain target peak for mic capture (~−3.4 dBFS) |
| `MIC_MAX_GAIN` | `12.0f` | Auto-gain cap (~+21.6 dB) |

## 3. Pins & audio capture — `pins.h`

Board: **LCDWIKI 2.8" ESP32-S3 Display (ES3C28P / ES3N28P)** — display, touch,
codec, mic, amp and SD are all onboard; only the speaker and the push-to-talk
button are external.

| Parameter | Default | Description |
|---|---|---|
| `PIN_AUDIO_EN` | `1` | FM8002E speaker-amp enable (LOW = on) |
| `PIN_I2S_MCLK` / `BCLK` / `LRCK` | `4` / `5` / `7` | I2S clocks to the ES8311 codec |
| `PIN_I2S_DOUT` / `PIN_I2S_DIN` | `8` / `6` | I2S data: ESP32→speaker / mic→ESP32 |
| `PIN_I2C_SDA` / `PIN_I2C_SCL` | `16` / `15` | I2C bus (ES8311 codec + FT6336G touch, shared) |
| `ES8311_I2C_ADDR` | `0x18` | Codec I2C address |
| `PIN_BTN` / `PIN_BTN_GND` | `3` / `2` | Push-to-talk button between IO2 (driven LOW as GND) and IO3 (`INPUT_PULLUP`, active LOW) |
| `PIN_RGB_LED` | `42` | Onboard WS2812 status LED |
| `PIN_LCD_BL` | `45` | LCD backlight enable (HIGH = on); other LCD pins are fixed in the TFT_eSPI `User_Setup` |
| `I2S_PORT` | `I2S_NUM_0` | I2S peripheral used |
| `SAMPLE_RATE` | `16000` | Capture sample rate (Hz) — Google STT requirement |
| `SAMPLE_BITS` | `16` | Bits per sample |
| `MAX_RECORD_MS` | `7000` | Max push-to-talk recording length (ms); sizes the PSRAM record buffer (448 KB) |

SD-card (SDIO) pins for the TTS cache are in `tts_cache.h` / `parameters.h`
(`SD_CLK 38`, `SD_CMD 40`, `SD_DATA0–3 39/41/48/47`).

## 4. Audio codec — `es8311.h`

| Parameter | Default | Description |
|---|---|---|
| DAC volume register (`0x32`) | `0xC5` | Normal speaker loudness (~+3 dB; `0xBF` = 0 dB unity). Higher hex = louder |
