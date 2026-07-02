# Wake Word — "hey pip" (self-contained, no library)

Replaces the push-to-talk **button** with an on-device **wake word**: the device
listens continuously on the ES8311 mic, and when the child says **"hey pip"** it
records the answer (ended automatically on silence) instead of waiting for a
button hold.

The model was **trained offline for you** and is bundled in the sketch — there is
**nothing to install** (no Edge Impulse, no TensorFlow, no Arduino ML library).
The button is **not deleted**: it is compiled out behind `USE_WAKE_WORD` and comes
back by flipping one flag (see [Reverting](#reverting-to-the-button)).

---

## Files

| File | What it is |
|------|------------|
| `hey_pip_model.h` | Auto-generated weights: 40-bin mel filterbank + the tiny CNN (~21k floats, ~83 KB flash). |
| `ww_infer.h` | Pure-C inference: log-mel front-end (FFT + mel + log) and the CNN forward pass. No dependencies. |
| `wake_word.h` | Arduino glue: streams the ES8311 mic into a rolling 1 s window, runs inference, exposes `wakeWordBegin/StartListening/Poll/StopListening`. |
| `ESP32/wake_word_training/` | The full training pipeline used to make the model, so you can retrain/improve it later. |

---

## How it works

1. The mic feeds a **rolling 1-second window** (16 kHz mono).
2. Every poll (~200 ms) the firmware computes a **log-mel spectrogram** (98×40)
   and runs a **small 3-layer CNN** → 3 class probabilities: `noise`,
   `hey pip`, `other speech`.
3. If `P(hey pip) ≥ WAKE_WORD_THRESHOLD` it fires, hands I2S to the recorder, and
   the existing answer→STT→tutor pipeline runs unchanged.

I2S is installed only while listening and released the instant the wake word
fires, so it never clashes with the recorder or the TTS player (which install
their own I2S). The wake-word path reuses `i2s_start_recording()`, so the ES8311
clocking you tuned is identical for listening and recording.

---

## Build & flash (nothing to install)

1. Make sure `USE_WAKE_WORD` is `1` in `homework_assistant.ino` (it is by default).
2. Arduino IDE board settings (same as before):
   - Board **ESP32S3 Dev Module**, Flash **16MB**, **PSRAM: OPI PSRAM** (required —
     the model scratch lives in PSRAM), Partition **Huge APP**.
3. Upload. On boot the serial log prints:
   ```
   [WakeWord] 'hey pip' model ready: 98-frame log-mel, CNN 16/24/32, threshold 0.80
   ```
4. Say **"hey pip"**, then answer.

---

## Tuning (in `wake_word.h`)

| Macro | Default | Effect |
|-------|---------|--------|
| `WAKE_WORD_THRESHOLD` | `0.80f` | Confidence to fire. **False-firing?** raise to 0.85–0.92. **Missing it?** lower to ~0.70. |
| `WW_HOP` | `3200` (200 ms) | Audio pulled per poll = detection granularity vs. CPU. |

The serial line `[WakeWord] 'hey pip' 0.xx ✓` prints the score on each detection,
so you can watch real values and pick a threshold.

---

## Accuracy & honest caveats

- **Verified:** the on-device C inference matches the Python trainer **bit-for-bit
  (max diff 1e-6)** on held-out clips, and held-out **validation accuracy is 0.92**.
  In evaluation, **noise never false-fired**; "pip", "hey", "pizza", "hello", etc.
  scored low.
- At threshold 0.80, single-window "hey pip" recall is ~0.82 / precision ~0.90.
  In practice recall is higher because the word spans several overlapping 1 s
  windows as it's spoken.
- **Biggest caveat:** the model was trained on **synthetic voices** (espeak-ng) —
  no real-mic, no real children. A real child's (Hebrew-accented) "hey pip" may
  differ, so **expect to tune `WAKE_WORD_THRESHOLD` once on the device**. If it
  underperforms, retrain with real recordings (below) — that closes most of the gap.

---

## Footprint

- Flash: ~83 KB of weights (mostly the mel filterbank).
- PSRAM scratch: ~160 KB (1 s window + activations), allocated once at boot.
- Compute: ~30–40 ms per inference, run every ~200 ms while idle (a few % CPU).

---

## Retrain / improve the model

Everything used to build the model is in `ESP32/wake_word_training/`. To regenerate
`hey_pip_model.h` (e.g. after adding your own recordings or changing the phrase):

```bash
pip install --break-system-packages tensorflow-cpu numpy scipy soundfile \
    espeakng_loader py-espeak-ng
python3 gen_data.py 11 600 800 300 c0   # repeat with seeds 22/33/44 for more data
python3 train.py                        # trains + saves model.keras (val acc ~0.92)
python3 export_c.py                     # writes hey_pip_model.h
python3 parity.py                       # proves the C inference == the trained model
```

To make it robust to your real users, record a few dozen clips of the actual
children saying "hey pip" (1 s, 16 kHz mono), drop them in as extra positives, and
retrain — synthetic data gets you started, real data makes it solid.

---

## Reverting to the button

Set one flag in `homework_assistant.ino` and re-flash:

```cpp
#define USE_WAKE_WORD 0
```

That recompiles the original push-to-talk path (button GPIO + button-held capture)
and ignores the wake-word model entirely.
