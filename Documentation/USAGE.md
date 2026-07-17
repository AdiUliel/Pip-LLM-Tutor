# Usage Instructions

How to operate Pip day-to-day. For building/flashing the firmware and deploying
the cloud side, see [INTEGRATION.md](INTEGRATION.md).

## First-time setup (pairing)

1. Power the device over USB-C. Pip's face appears and the device connects to
   WiFi (credentials from `secrets.h`).
2. An **unpaired** device shows its pairing code on screen: `TUTOR-XXXXXX`
   (also printed to the Serial Monitor at every boot).
3. In the parent app (https://llm-tutor-d721e.web.app or the Android APK):
   sign in → add/select a child → enter the pairing code under the device
   section. Pairing is "newest wins" — re-pairing to another child automatically
   unlinks the previous one; no unpair step is needed.
4. Reboot the device (or wait for the session to end). It picks up the paired
   child and greets them by name.

## Daily use

1. Power on. The device auto-starts a tutoring session for the paired child and
   **speaks the first question** (face: speaking).
2. The child **holds the push-to-talk button** (the physical button on the
   device), answers out loud, and **releases** (max recording ~7 s).
3. Pip grades the answer, speaks feedback + the next question, and its face
   reacts to how the child is doing. Correct answers add stars on the strip.
4. Progress, mood and accuracy reports appear in the parent app; homework
   materials uploaded in the app are turned into questions automatically.

**Auto power-off (idle policy):** after a period with no interaction the screen
turns off, and later the device enters deep sleep ("off"). Both thresholds are
set per-child in the app (Settings → auto power-off). **A button press wakes the
device.**

## Main error indications

| What you see | Meaning | What to do |
|---|---|---|
| Face shows **error** status shortly after boot | WiFi could not connect (~20 s without a link; device keeps retrying by itself) | Check the router / hotspot and the `WIFI_SSID`/`WIFI_PASSWORD` in `secrets.h` |
| Serial: `❌ Firebase auth failed` + error face | Wrong/expired `FIREBASE_WEB_API_KEY` in `secrets.h` | Copy the Web API Key from Firebase Console → Project Settings |
| Serial: `Not ready to start (unpaired or a transient error) — retrying in 15s` | Device is not paired to any child, or a transient cloud error | Pair it in the app (see above); transient errors resolve on their own |
| Pip re-asks the same question after a long pause | The answer upload failed mid-turn (WiFi hiccup / backend timeout) | Nothing — the device re-prompts automatically; just answer again |
| Serial: `⚠️ pip_face init failed` (audio still works) | Display init problem (TFT_eSPI `User_Setup` not copied, or PSRAM not enabled in the IDE board settings) | See [INTEGRATION.md](INTEGRATION.md) — copy `pip_face/User_Setup_LCDWIKI.h` and set PSRAM to "OPI PSRAM" |
| Serial: `⚠️ ES8311 init failed. Audio may not work.` | Audio codec did not answer on I2C | Power-cycle the board; verify it's the LCDWIKI ES3C28P/ES3N28P board |
| App shows the device as offline | No heartbeat for >30 s | Device is asleep/unpowered or lost WiFi — press the button / check power and network |

The device is deliberately self-healing: WiFi retries forever, session start
retries every 15 s, and failed turns re-prompt — power-cycling is the universal
fallback.

## Parent-app error messages

All app errors are shown in Hebrew, in-context:

| Message | Meaning | What to do |
|---|---|---|
| «אימייל או סיסמה שגויים» / «כתובת אימייל לא תקינה» | Wrong sign-in credentials / malformed email | Fix and retry; use "forgot password" for a reset email |
| «האימייל כבר רשום במערכת» | Sign-up with an email that already has an account | Sign in instead |
| «האימייל כבר רשום עם סיסמה. התחברו עם אימייל וסיסמה» | Tried Google Sign-In for an account created with email+password | Sign in with the original method |
| «הסיסמה חלשה מדי (לפחות 6 תווים)» | Password under 6 characters at sign-up | Choose a longer password |
| «אין חיבור לאינטרנט» + offline banner | The app itself lost connectivity | Restore network; queued writes sync automatically |
| «לא נמצא התקן עם הקוד הזה. ודאו שהמכשיר דולק ומחובר ל-Wi-Fi.» | Pairing code doesn't match any registered device | Check the `TUTOR-XXXXXX` code on the device screen; make sure it booted and reached WiFi at least once |
| «ההתקן נמצא, אבל לא דיווח עדכני. ודאו שהוא דולק ונסו שוב בעוד רגע.» | Device exists but its heartbeat is stale (>60 s) — probably off or offline right now | Power the device, wait a few seconds for a heartbeat, retry pairing |
| «לא נמצאו שאלות במסמך. נסה קובץ ברור יותר.» | Uploaded homework file yielded no extractable questions | Upload a clearer photo/scan of the material |
| «עיבוד השאלות נכשל. נסה שוב.» / «העלאת הקובץ נכשלה» | Cloud extraction or the upload itself failed (transient) | Retry; check connectivity |
| «נחסם — תוכן לא מתאים, לא ניתן לכלול בתרגול» | Material failed validation (not age/subject-appropriate) — its toggle is locked | Upload material matching the child's age and the selected subject |
| Device badge shows «לא מחובר» | No device heartbeat for >30 s | Device asleep/unpowered or lost WiFi — press its button / check power and network |

## Calibration

**No manual calibration is required.**

- **Microphone** — input gain is automatic (auto-gain up to +21.6 dB targeting
  ~−3.4 dBFS; see `MIC_TARGET_PEAK` / `MIC_MAX_GAIN` in
  [Parameters](../Parameters/README.md)).
- **Speaker volume** — fixed at compile time (`es8311.h` DAC volume register,
  default ~+3 dB). For quiet bench testing set `SPEAKER_LOW_VOLUME 1` and
  re-flash.
- **Touch screen** — the FT6336G is capacitive and needs no calibration.
