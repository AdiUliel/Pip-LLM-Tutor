# "hey pip" wake-word — training pipeline

> 🤖 **Retraining for robustness? START WITH [`RETRAIN_RUNBOOK.md`](RETRAIN_RUNBOOK.md).**
> It's a self-contained, step-by-step handoff (real device recordings + neural-TTS +
> heavy augmentation) meant to be executed by Claude on an Ubuntu machine. The notes
> below describe the original v1 pipeline.


This is exactly how the bundled model (`ESP32/homework_assistant/hey_pip_model.h`)
was produced. It runs entirely offline — synthetic speech (espeak-ng) + a small
CNN trained in TensorFlow, exported to plain C. No accounts, no cloud.

## Setup
```bash
pip install --break-system-packages tensorflow-cpu numpy scipy soundfile \
    espeakng_loader py-espeak-ng
```

## Pipeline (scripts use /tmp/ww as the working dir — adjust paths if you like)
| Script | Does |
|--------|------|
| `feat.py` | Shared log-mel front-end (must stay identical to `ww_infer.h`). |
| `tts.py` | espeak-ng synthesis via ctypes (many voices/pitches/rates). |
| `gen_data.py` | `python3 gen_data.py <seed> <npos> <nneg> <nnoise> <tag>` → `X_<tag>.npy`. Synthetic "hey pip" positives, hard negatives ("pip","hey","pizza"…), noise; augmented (gain, offset, noise@SNR, reverb). |
| `train.py` | Concatenates chunks, trains the 3-layer CNN, saves `model.keras`, reports val accuracy + threshold sweep. |
| `export_c.py` | Writes `hey_pip_model.h` (weights + mel filterbank + norm constants). |
| `parity.py` | Builds `host_test` and proves the C inference == the Keras model (max diff was 1e-6). |
| `ww_infer.h`, `host_test.c` | Pure-C inference + a host harness, for the parity check / debugging on a PC. |

## Make it production-grade
Synthetic voices get you a working v1 (val acc ~0.92). For best results on the real
device, record a few dozen clips of the actual children saying "hey pip"
(1 s, 16 kHz mono int16), add them as extra positives in `gen_data.py`, and retrain.
Then copy the new `hey_pip_model.h` into `ESP32/homework_assistant/`.
