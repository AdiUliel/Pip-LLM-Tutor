# Pip — Face State Machine

_Behaviour reference for the animated face on the device screen · Pip LLM Tutor_

This document describes the state machine of **Pip's face** — the character shown
on the device's ILI9341 display. It reflects **what the firmware actually drives
today** (grounded in `ESP32/homework_assistant/homework_assistant.ino` and
`pip_face/PipFace.cpp`), not just the original design intent — see §5 for where
the two differ.

Pip is drawn in solid cyan on a black RGB565 panel; states are distinguished by
eye/mouth shape and motion, not colour. The render runs on its own ~30 fps
FreeRTOS task, so the face keeps animating even while the main loop is blocked on
WiFi, STT, or audio.

---

## 1. How the face is driven

The firmware sets the face two ways, both defined near the top of the `.ino`:

- **`faceStatus(status)`** → `Pip::setDeviceStatus()` — the functional loop
  states, also mirrored to `deviceState.status` in Firestore so the parent app's
  device monitor shows the same thing.
- **`faceEmotion(label)`** → `Pip::setEmotion()` — a direct emotion, used for the
  per-turn feedback face and a few boot/setup moments.

Per answer, the backend (`tutorEngine.js`) returns an `emotion`, which the device
maps to a face via `pipEmotionFor()` and shows with `faceEmotion()`.

---

## 2. The core session loop

One turn advances in one direction: **ask → listen → think → react**, repeat.

```
                 ┌─────────────────────────────────────────────┐
                 ▼                                             │
  IDLE ─► SPEAKING ─► LISTENING ─► THINKING ─► [react] ─► SPEAKING (next Q)
 (idle)  (asking)    (listening)  (thinking)     │
                                                 ├─ correct, single       → HAPPY
                                                 ├─ correct, streak ≥2     → PROUD
                                                 ├─ correct, streak ≥5     → CELEBRATING
                                                 ├─ wrong, 1st attempt     → ENCOURAGING (hint, retry)
                                                 └─ wrong, streak ≥2        → CONCERNED (easier next)
```

- **Correct answer** → `happy`; a solid streak (≥2) → `proud`; a rare long streak (≥5) → `celebrating` → next question.
- **Wrong answer** → `encouraging`, a hint, and the *same* question again; a second miss reveals the answer and moves on. A run of misses (streakWrong ≥2) → `concerned` and the engine drops the difficulty.
- Background **monitoring states** can interrupt at any time: SLEEPY for a break, OOPS for errors, and PLAYFUL as a brief re-engagement wink when the child goes quiet mid-question. Afterwards the device returns toward IDLE/LISTENING.

---

## 3. State reference (as implemented)

| State (pip) | Trigger in firmware | Set via |
|---|---|---|
| **IDLE** | Nothing happening / end of another state (`faceStatus("idle")`) | deviceState |
| **SPEAKING** | Device is reading a question or feedback aloud (`"asking"`, `faceEmotion("speaking")`) | both |
| **LISTENING** | Waiting for / capturing the child's answer (`"listening"`) | both |
| **THINKING** | Boot, WiFi/connect, identify flow, and while a turn is being processed (`faceEmotion("thinking")`) | emotion |
| **HAPPY** | A single correct answer (backend `emotion:"happy"`; also `neutral`) | emotion |
| **PROUD** | Correct answer on a solid streak — `streakCorrect ≥ 2` (backend `emotion:"proud"`) | emotion |
| **CELEBRATING** | Correct answer on a rare long streak — `streakCorrect ≥ 5` (backend `emotion:"celebrating"`); also the "connected ✓" moment right after successful pairing | emotion |
| **ENCOURAGING** | First wrong attempt / hint, and the default feedback face | emotion |
| **CONCERNED** | Child is struggling — `streakWrong ≥ 2` (backend `emotion:"concerned"`) | emotion |
| **PLAYFUL** | Boredom nudge — no interaction for `BOREDOM_NUDGE_SECONDS` (40 s) while waiting for an answer with the screen on; a brief wink, then back to LISTENING | emotion |
| **SLEEPY** | Break time (`faceStatus("break")`); also the calm "waiting to be set up" face on an **unpaired** device | both |
| **OOPS** | Technical error — WiFi lost, Firebase auth failed, or a cloud/turn error (`faceStatus("error")`) | deviceState |

Animation/duration details (blink cadence, cross-fades, per-state motion) are as
in the original design doc; the firmware keeps the ~30 fps partial-update render
and slow, non-strobing motion for child safety.

---

## 4. Emotion mapping (backend → face)

`pipEmotionFor()` in the firmware:

| Backend `emotion` | Face shown |
|---|---|
| `celebrating` | CELEBRATING |
| `proud` | PROUD |
| `happy` | HAPPY |
| `neutral` | HAPPY |
| `encouraging` | ENCOURAGING |
| `concerned` | CONCERNED |
| *(anything else)* | ENCOURAGING (safe default) |

The backend emits `happy`, `proud`, `celebrating`, `encouraging`, or `concerned`
for a turn (`deterministicFeedback` + the LLM emotion whitelist), so those five
are the reachable feedback faces.

---

## 5. Notes on the implementation

All 12 designed face states are now reachable. PROUD and PLAYFUL — previously
defined but unused — are wired as of this version:

- **PROUD** — the backend now emits `emotion:"proud"` for a solid correct streak
  (`streakCorrect ≥ 2`), and `pipEmotionFor()` maps it to the PROUD face.
  CELEBRATING is reserved for a rare long streak (`≥ 5`), keeping it special.
- **PLAYFUL** — the firmware shows a brief PLAYFUL wink when the child goes quiet
  mid-question (no interaction for `BOREDOM_NUDGE_SECONDS`, default 40 s, while
  the screen is on), then returns to LISTENING. It is visual only — Pip never
  interrupts with sound — and fires at most once per idle stretch.

A few states do double duty by design:

- **SLEEPY** — the scheduled break *and* the "waiting to be paired" screen on an
  unpaired device.
- **CELEBRATING** — an in-lesson milestone *and* the "connected ✓" moment right
  after successful pairing.
- **THINKING** — while checking an answer, and also at boot and during
  WiFi/connect.

---

_For a print-ready copy, open this file in any Markdown viewer and export to PDF._

_See also [`pip_face_states.html`](pip_face_states.html) — a standalone interactive
page showing all 12 face states animated as vectors._
