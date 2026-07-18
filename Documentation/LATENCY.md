# Latency — where the round-trip time went, and how it was cut

How long it takes from *the child stops speaking* to *the robot starts speaking
the feedback*, broken down hop-by-hop against the code — and the optimizations
that were applied to cut it from **~15–19 s to ~4–6 s**.

This is the engineering analysis behind the optimization. For the **measured
before/after results, the per-turn hardware numbers, and the test conditions**,
see [PERFORMANCE_EVALUATION.md](PERFORMANCE_EVALUATION.md). Setup for the analysis
below: ESP32-S3 on home Wi-Fi, a ~2–3 s spoken answer, functions in
`europe-west10` (Berlin), device used from Israel.

---

## TL;DR

The backend compute was already well optimised. The latency that remained was
**structural** — it came from the *number of serial network hops* in one turn,
not from slow code. The original design crossed the network **five times in
series**: STT upload → Firestore write → Eventarc trigger → poll loop → audio
download.

The fix **collapsed the device's turn into one synchronous HTTPS call**
(`processTurn`) that grades, calls Gemini (only when needed), synthesises the
audio, and returns everything in the HTTP response body — while still writing the
same Firestore docs server-side so the Flutter app is unaffected. That removed the
Eventarc trigger delay, the entire poll loop, the intermediate Firestore writes,
and the separate audio download. A round of zero-risk config changes (warm
instances, REST Firestore, no Gemini "thinking", keep-alive TLS) came first, and a
final phase folded STT into the same call over raw PCM.

**Result (measured):** the turn dropped from **14.8–19 s** to **4.0–5.8 s**
(~3.5× faster), and the cold-start cliff (up to **+12 s** on the first STT call
after idle) was eliminated.

---

## The original turn, hop by hop (the baseline this started from)

The hot path lived in `ESP32/homework_assistant/homework_assistant.ino` →
`processCapturedAnswer()`. In the original MP3-URL design, one answer did this:

```text
child stops talking
  │
  │  (A) endpoint on silence            recordUntilSilence(), trailSilenceMs=1200
  ▼
  │  (B) STT round trip                 transcribeAudio()  → POST transcribeAudio CF → Google STT
  ▼                                      stt_client.h: fresh TLS, base64 PCM, 60 s timeout
  │  (C) write child answer             firestorePostLearningTurn() → REST write to Firestore
  ▼
  │  (D) Eventarc fires the trigger     answerQuestion (onDocumentCreated)
  ▼                                      Firestore → Eventarc → function (propagation + maybe cold start)
  │  (E) function does the work         grade → (maybe) Gemini → Google TTS → Storage upload → makePublic
  ▼
  │  (F) device polls for "done"        firestorePollForTurnResult(), delay(500), 30 s cap
  ▼                                      re-handshakes TLS every poll
  │  (G) download + play the MP3        speakAudio() → speakFromUrl()
  ▼                                      fresh TLS to storage.googleapis.com, whole file before sound
robot speaks
```

Stages **B, C, D, F, G** were each a separate network round trip, running **one
after another** — that serialisation was the core problem.

### Baseline budget — warm path, *correct* answer (LLM skipped)

| Stage | Baseline (warm) | Why |
|---|---|---|
| A — silence endpoint | ~1.2 s | `trailSilenceMs=1200`; perceived delay, not network |
| B — STT round trip | 1.5–3.0 s | TLS handshake + base64 upload (~128 KB for 3 s) + Google STT |
| C — post answer (Firestore write) | 0.4–0.8 s | fresh TLS + REST write |
| D — Eventarc trigger propagation | 0.3–1.5 s | Firestore→Eventarc→function indirection |
| E — function compute (no LLM) | 0.9–1.7 s | `processing` write + grade + TTS + Storage save + `makePublic` + `done` write |
| F — poll detection lag | 0.6–1.6 s | `delay(500)` + a full TLS handshake **every poll** + GET |
| G — audio download before playback | 0.7–1.2 s | fresh TLS + whole MP3 buffered before the first sample |
| **Total (excluding A)** | **~5.4–9.8 s** | plus the cold-start spike below |

A *wrong* answer added the Gemini call (stage E) — correctly run only when it adds
value. **Cold-start penalty:** `transcribeAudio`/`synthesizeSpeech` had no warm
instance, so the first STT call after idle paid a Cloud Functions v2 cold start —
commonly **3–12 s** with the Admin SDK's gRPC Firestore init. That was the single
worst spike a child would feel.

---

## Where the latency was (ranked)

### 1. Five serial round trips instead of one — the write→trigger→poll loop
The device wrote a document, an **Eventarc trigger** picked the change up
asynchronously, the function processed it, and the device **polled** Firestore
until it saw `status:"done"`. Great for the Flutter app (it just watches the doc),
but for the device it added three avoidable costs: trigger propagation (D), poll
detection lag (F), and an intermediate `status:"processing"` write.

### 2. The device re-opened a TLS connection on every poll
`_initSSL()` (`_sslClient.stop()`) ran at the top of every poll iteration, so each
poll was a *fresh* TLS 1.2 handshake to `firestore.googleapis.com` (~0.3–0.6 s on
an ESP32-S3) plus the GET — making the real poll period closer to ~1 s than the
intended 0.5 s. The same fresh-handshake-per-call pattern was in STT and TTS.

### 3. STT was its own serial round trip, and it could cold-start
STT happened *before* the turn was even posted, so its latency was fully additive.
Two issues compounded it: no warm instance (cold-start risk on the first answer),
and base64 inflating the upload by 33% (a 3 s answer ≈ 96 KB PCM → ~128 KB on the
wire). Google's sync `recognize` also waited for the whole clip.

### 4. Audio took the scenic route: TTS → Storage → re-download
The function wrote the audio to Cloud Storage, made it public, returned a URL; the
device then opened a *second* connection to `storage.googleapis.com` and buffered
the entire file into PSRAM before playing a single sample.

> Note: returning audio **inline as a base64 Firestore field** had already been
> tried and reverted — 200–550 KB string values overflow the Arduino-String heap.
> So the fix was **not** "put base64 back in Firestore"; it was "stream the MP3
> bytes in the HTTP response of the synchronous turn call." Different transport, no
> giant Firestore string.

### 5. Gemini ran with "thinking" on
On the turns where Gemini *was* called, `gemini-2.5-flash` defaulted to "thinking"
enabled, which added latency for no benefit here — the output is a tiny fixed JSON
(`maxOutputTokens: 180`).

### 6. Everything paid Berlin↔Israel RTT, several times
~60–80 ms each way isn't much, but the original turn paid it on five separate
connections, several with a fresh TLS handshake. There is no GCP region inside
Israel and Berlin is a reasonable EU choice, so the win was **making fewer
connections**, not moving the region.

---

## What was already optimised before this work

The backend was not where the time was going. Prior work had already done the
right things, and the optimization preserved all of it:

- **The LLM is gated** — simple correct answers use deterministic feedback and
  skip Gemini entirely (`tutorEngine.js`, `needsLLM`).
- **Feedback and next-question run in parallel**, not in series.
- **Question generation is fully local** — no LLM call in `questionGenerator.js`.
- **The trigger function is kept warm** (`answerQuestion`, `minInstances: 1`).

---

## The optimizations applied

Delivered in three phases, each measured before moving on (see the phase-by-phase
numbers in [PERFORMANCE_EVALUATION.md](PERFORMANCE_EVALUATION.md)).

### Phase 0 — zero-risk config wins

No architecture change; each a few lines, individually deployable:

| # | Change | Effect |
|---|---|---|
| 0.1 | `minInstances: 1` on the STT/turn/TTS endpoints | removed the **3–12 s** cold-start spike on the first answer |
| 0.2 | `preferRest: true` on the Admin SDK Firestore init | ~2–5 s off any remaining cold start (no gRPC warm-up) |
| 0.3 | `thinkingConfig: { thinkingBudget: 0 }` on the Gemini call | ~0.5–1.3 s on wrong-answer turns |
| 0.4 | reuse one keep-alive TLS connection across polls; drop the poll interval | ~0.5–1.2 s off stage F |
| 0.5 | trimmed `trailSilenceMs` 1200 → ~750 ms | ~0.4 s of perceived delay |

Phase 0 alone took a warm turn from ~14.8 s to ~10.8 s and killed the cold-start
spike — without restructuring anything.

### Phase 1 — collapse the turn into one synchronous call (the core win)

A synchronous HTTPS function, `processTurn`, that the device calls **once** and
blocks on:

```text
device  ──POST {sessionId, childAnswer}──▶  processTurn (onRequest, warm)
                                              │  grade (deterministic)
                                              │  Gemini only if needsLLM (thinking off)
                                              │  Google TTS → MP3 bytes (no Storage)
                                              │  write session/question/exchange docs (same schema)
device  ◀──── JSON {feedback, emotion, nextQuestion, isCorrect} + MP3 in the response body
```

This removed, all at once: the Eventarc propagation (D), the entire poll loop (F),
the intermediate `processing` write, the separate Storage download (G, now folded
in and decoded as bytes arrive), and the device's own Firestore write (C). The
`answerQuestion` trigger was kept deployed as a **fallback / app-initiated path**
(toggled by `USE_PROCESS_TURN`), so nothing else broke — the Flutter app keeps
watching the same `sessions/exchanges` docs because `processTurn` still writes
them. The device decodes the MP3 **streamed from the HTTP response** (the helix
decoder is fed from the stream as bytes arrive instead of buffering the whole
file). Phase 1 brought the warm turn to ~6.3–6.6 s and cut network hops 5 → 2.

### Phase 2 — fold STT into the turn call

The device uploads the raw mono PCM straight to `processTurn` (`?fmt=pcm16`), which
runs STT server-side, then grade + Gemini + TTS — **one round trip** for the whole
turn, with no separate `transcribeAudio` handshake and a raw-PCM upload (no base64,
−33% bytes over the slow uplink). Toggled by `USE_PROCESS_TURN_AUDIO`. Phase 2
brought the warm turn to **4.0–4.3 s** (correct) / 5.8 s (wrong, with the gated
Gemini hint) and cut network hops 2 → 1.

---

## Result

| | Correct turn | Wrong turn (Gemini) | Net hops |
|---|---|---|---|
| MP3-URL baseline | 14.8 s | 17–19 s | 5 |
| After Phase 0 | 10.8 s | 12.5 s | 5 (warm) |
| After Phase 1 | 6.3–6.6 s | 7–8.3 s | 2 |
| **After Phase 2** | **4.0–4.3 s** | **5.8 s** | 1 |

~3.5× faster, 10–13 s cut per turn, cold-start cliff removed, inline-audio decode
down from a 2.6–3.8 s URL download to 2–4 ms. The remaining ~4 s on a correct turn
is essentially the single STT + compute + TTS round trip Israel↔Berlin — close to
the practical floor without changing region. Full measured per-turn hardware data
and the test methodology are in
[PERFORMANCE_EVALUATION.md](PERFORMANCE_EVALUATION.md).

## How it was measured

- **Device side:** each stage is wrapped in `millis()` deltas and printed as
  `[lat] …` serial lines; the key metric is `RECORD → FIRST SOUND` (button release
  → first audio sample out), captured for correct and wrong answers, warm and cold.
- **Function side:** Cloud Functions logs show per-invocation execution time and
  the cold-start line on the first call after idle.
