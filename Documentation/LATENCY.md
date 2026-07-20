# Latency — the turn round-trip

How long it takes from *the child stops speaking* to *the robot starts speaking
the feedback*, and what the design does to keep that short. Setup: ESP32-S3 on
home Wi-Fi, a ~2–3 s spoken answer, Cloud Functions in `europe-west10`
(Berlin), device used from Israel. Full measured data and methodology:
[PERFORMANCE_EVALUATION.md](PERFORMANCE_EVALUATION.md).

## The turn — one synchronous call

The whole turn is a single HTTPS request. The device uploads the recorded
answer as raw PCM to `processTurn`, which runs every stage server-side and
returns everything in one response:

```text
device ──POST raw PCM (?fmt=pcm16)──▶ processTurn (warm instance)
                                        │ Speech-to-Text
                                        │ grade (deterministic)
                                        │ Gemini — only when feedback needs it
                                        │ Text-to-Speech
                                        │ write session/question/exchange docs
device ◀── feedback/emotion/next question in headers + MP3 in the body
```

The function still writes the same Firestore documents, so the parent app
follows the lesson live; the device itself never polls and never downloads
audio separately — the MP3 is decoded as the response streams in.

## What keeps it fast

- **One network hop per turn** — STT, grading, LLM and TTS run inside a single
  call; there is no device Firestore write, no trigger propagation, no poll
  loop, and no separate audio download.
- **Raw PCM upload** — no base64 (−33% bytes on the slow uplink).
- **Warm instances** (`minInstances: 1`) and the REST Firestore transport — no
  cold-start spike on the first answer.
- **The LLM is gated** — simple correct answers use deterministic feedback and
  skip Gemini entirely.
- **Gemini `gemini-2.5-flash-lite`, "thinking" disabled** — the reply is a
  short fixed-JSON (`maxOutputTokens: 180`), so the low-latency model variant
  is sufficient.
- **STT `command_and_search` model + phrase hints** — the short-utterance model
  fits one-word answers and names, and `speechContexts` bias recognition toward
  the current expected answer (boost 20).
- **Keep-alive TLS on the device** — connections are reused, avoiding a
  ~0.3–0.6 s handshake per request.
- **Feedback and next-question generation run in parallel** server-side, and
  question generation is fully local (no LLM call).

## Measured results

End-to-end turn (`RECORD → FIRST SOUND`, button release to first audio sample):

| Design | Correct turn | Wrong turn (Gemini) | Network hops |
|---|---|---|---|
| Polling design (write → trigger → poll → download) | 14.8 s | 17–19 s | 5 |
| **Single-call design (current)** | **4.0–4.3 s** | **5.8 s** | 1 |

Per-stage server breakdown, from the `[lat]` log inside `processTurn`
(15 turns, home Wi-Fi, warm):

| Stage | Measured |
|---|---|
| STT (upload + recognition, with hints) | 0.45–0.72 s |
| Grade + TTS, LLM skipped (correct answer) | 0.4–1.7 s |
| Grade + Gemini + TTS (wrong answer) | 2.0–2.9 s |
| PCM upload size per answer | 40–82 KB |

The dominant remaining cost on a wrong answer is the Gemini call; on top of
every stage sits the Israel↔EU round trip to `europe-west10`. The LLM row was
measured with `gemini-2.5-flash`; the deployed model is `gemini-2.5-flash-lite`
(set via the `GEMINI_MODEL` environment variable).

## How it is measured

- **Device:** each stage is wrapped in `millis()` deltas and printed as
  `[lat] …` serial lines; the key metric is `RECORD → FIRST SOUND`.
- **Server:** `processTurn` logs `[lat] session=… stt=…ms turn=…ms pcm=…B
  llm=…` per turn to Cloud Logging, separating upload+STT from
  grade+Gemini+TTS.
