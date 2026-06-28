# Latency — where the round-trip time goes, and how to cut it

How long it takes from *the child stops speaking* to *the robot starts
speaking the feedback*, broken down hop-by-hop against the actual code, plus a
prioritised plan to shorten it. This is an analysis/plan document — no code has
been changed yet.

All times below are **engineering estimates**, not measured numbers, for an
ESP32‑S3 on home Wi‑Fi, a ~2–3 s spoken answer, functions in `europe‑west10`
(Berlin), and a device used from Israel. Treat the ranges as "what to expect and
what to attack", and measure on real hardware before/after each change (see
[How to measure](#how-to-measure)).

---

## TL;DR

The backend compute is already well optimised. The remaining latency is
**structural** — it comes from the *number of serial network hops* in one turn,
not from slow code. A single answer currently crosses the network **five times
in series**: STT upload → Firestore write → Eventarc trigger → poll loop →
audio download.

The highest-value change (the recommended middle ground) is to **collapse the
device's turn into one synchronous HTTPS call** that grades, calls Gemini,
synthesises the audio, and returns everything in the HTTP response body — while
still writing the same Firestore docs server-side so the Flutter app is
unaffected. That removes the Eventarc trigger delay, the entire poll loop, the
intermediate Firestore writes, and the separate audio download.

Before touching the architecture, four **zero-risk config changes** are worth
deploying on their own:

1. Keep STT (and the new turn endpoint) warm with `minInstances: 1`.
2. Add `preferRest: true` to the Admin SDK Firestore init (kills 2–5 s of gRPC cold-start).
3. Disable Gemini "thinking" with `thinkingBudget: 0`.
4. Stop re-doing the TLS handshake on every poll on the device.

Estimated effect: the warm-path turn drops from roughly **5.5–10 s** to
**~2–5 s**, and the cold-start cliff (today up to **+12 s** on the first STT call
after idle) effectively disappears.

---

## The turn, hop by hop (today)

The hot path lives in `ESP32/homework_assistant/homework_assistant.ino` →
`processCapturedAnswer()` (around line 641). One answer does this:

```text
child stops talking
  │
  │  (A) endpoint on silence            recordUntilSilence(), trailSilenceMs=1200
  ▼
  │  (B) STT round trip                 transcribeAudio()  → POST transcribeAudio CF → Google STT
  ▼                                      stt_client.h: fresh TLS, base64 PCM, 60 s timeout
  │  (C) write child answer             firestorePostLearningTurn() → REST write to Firestore
  ▼                                      firebase_client.h:396
  │  (D) Eventarc fires the trigger     answerQuestion (onDocumentCreated)  index.js:500
  ▼                                      Firestore → Eventarc → function (propagation + maybe cold start)
  │  (E) function does the work         grade → (maybe) Gemini → Google TTS → Storage upload → makePublic
  ▼                                      index.js / tutorEngine.js
  │  (F) device polls for "done"        firestorePollForTurnResult(), delay(500), 30 s cap
  ▼                                      firebase_client.h:446 — re-handshakes TLS every poll
  │  (G) download + play the MP3        speakAudio() → speakFromUrl()  tts_player.h:70
  ▼                                      fresh TLS to storage.googleapis.com, whole file before sound
robot speaks
```

Stages **B, C, D, F, G** are each a separate network round trip, and they run
**one after another**. That serialisation is the core problem.

### Estimated budget — warm path, *correct* answer (LLM skipped)

| Stage | Est. (warm) | Why |
|---|---|---|
| A — silence endpoint | ~1.2 s | `trailSilenceMs=1200`; perceived delay but not network |
| B — STT round trip | 1.5–3.0 s | TLS handshake + base64 upload (~128 KB for 3 s audio) + Google STT sync recognise |
| C — post answer (Firestore write) | 0.4–0.8 s | fresh TLS + REST write |
| D — Eventarc trigger propagation | 0.3–1.5 s | Firestore→Eventarc→function indirection; not tunable |
| E — function compute (no LLM) | 0.9–1.7 s | `processing` write + deterministic grade + TTS synth + Storage save + `makePublic` + `done` write |
| F — poll detection lag | 0.6–1.6 s | `delay(500)` + a **full TLS handshake every poll** + GET |
| G — audio download before playback | 0.7–1.2 s | fresh TLS + whole MP3 buffered into PSRAM before the first sample |
| **Total (excluding A)** | **~5.4–9.8 s** | |

A *wrong* answer adds the Gemini call (stage E), which the engine correctly runs
only when it adds value: **+1.0–2.5 s** today (with thinking on), or ~0.5–1.2 s
with thinking off.

**Cold start penalty:** `transcribeAudio` and `synthesizeSpeech` have no
`minInstances`, so after the device has been idle the *first* STT call pays a
Cloud Functions v2 cold start. With `firebase-admin` + Firestore that is
commonly **3–12 s** because the Admin SDK's Firestore client initialises over
gRPC. This is the single worst spike a child will feel.

---

## Where the latency actually is (ranked)

### 1. Five serial round trips instead of one — the write→trigger→poll loop
**Evidence:** `index.js:500` (`answerQuestion = onDocumentCreated(...)`),
`firebase_client.h:446` (`firestorePollForTurnResult`), `:492` (`delay(500)`).

The device writes a document, an **Eventarc trigger** picks the change up
asynchronously, the function processes it, and the device **polls** Firestore
until it sees `status:"done"`. That design is great for the Flutter app (it just
watches the doc), but for the device it adds three avoidable costs: the trigger
propagation delay (D), the polling detection lag (F), and an intermediate
`status:"processing"` write. None of these exist if the device instead makes one
synchronous request and the function answers in the HTTP response.

`submitChildAnswer` (`index.js:633`) already shows the seed of a direct path — it
just doesn't do the work inline or return the result; it only creates the pending
doc for the trigger.

### 2. The device re-opens a TLS connection on every poll
**Evidence:** `firebase_client.h:36` (`_initSSL()` calls `_sslClient.stop()`),
called at the top of every poll iteration (`:456`), loop sleeps `delay(500)`
(`:492`).

Each poll is therefore a *fresh* TLS 1.2 handshake to `firestore.googleapis.com`
(~0.3–0.6 s on an ESP32‑S3) **plus** the GET — so the real poll period is closer
to ~1 s than the intended 0.5 s, and a result that's ready right after a poll
waits most of a cycle. The same fresh-handshake-per-call pattern is in STT
(`stt_client.h:104`) and TTS (`tts_player.h:78`). Even if the trigger/poll model
is kept, reusing one keep-alive connection removes most of stage F.

### 3. STT is its own serial round trip, and it can cold-start
**Evidence:** `transcribeAudio` onRequest with no `minInstances`
(`index.js:715`), synchronous `speech:recognize` (`:749`), device uploads
base64 PCM (`stt_client.h:78–115`).

STT happens *before* the turn is even posted, so its latency is fully additive.
Two issues compound it: (a) no warm instance → cold-start risk on the first
answer; (b) base64 inflates the upload by 33% (a 3 s answer ≈ 96 KB PCM → ~128 KB
on the wire; the 7 s `MAX_RECORD_MS` cap is ~300 KB). Google's sync `recognize`
also waits for the whole clip before returning.

### 4. Audio takes the scenic route: TTS → Storage → re-download
**Evidence:** `synthesizeAudio` uploads to Storage and calls `makePublic`
(`index.js:196–204`); the device then downloads the whole MP3 over a new TLS
connection before playing a single sample (`tts_player.h:70–113`).

The function writes the audio to Cloud Storage, makes it public, returns a URL;
the device opens a *second* connection to `storage.googleapis.com` and buffers
the entire file into PSRAM before playback. If the audio came back in the turn
response body and the device decoded it as bytes arrived, stages D→G collapse and
playback starts ~1 s sooner.

> Note: returning audio **inline as a base64 Firestore field** was already tried
> and reverted — 200–550 KB string values overflow the Arduino‑String heap
> (`index.js:142–143`, and `tts_player.h:182` now refuses non-URL payloads). So
> the fix is **not** "put base64 back in Firestore"; it's "stream the MP3 bytes in
> the HTTP response of the synchronous turn call." Different transport, no giant
> Firestore string.

### 5. Gemini runs with "thinking" on, on the `global` endpoint
**Evidence:** model `gemini-2.5-flash` (`index.js:78`), `location: "global"`
(`:72–76`), `llmFeedback` sets no `thinkingConfig` (`tutorEngine.js:222–241`).

On the turns where Gemini *is* called, 2.5‑flash defaults to "thinking" enabled,
which adds latency for no benefit here — the output is a tiny fixed JSON
(`maxOutputTokens: 180`). Setting `thinkingBudget: 0` keeps 2.0‑flash‑class speed.
The `global` endpoint is chosen for model availability but can route far from
Berlin; a regional Vertex endpoint co-located with the function shaves the
model's network RTT (verify the model is served in the region first).

### 6. Everything pays Berlin↔Israel RTT, several times
**Evidence:** `setGlobalOptions({ region: "europe-west10" })` (`index.js:38`),
device URLs built from `CLOUD_FUNCTIONS_REGION` (`secrets.h:16`).

~60–80 ms each way doesn't sound like much, but the current turn pays it on five
separate connections, several with a fresh TLS handshake (3–4 round trips each).
There is no GCP region inside Israel, and Berlin is a reasonable EU choice — so
the win here is **making fewer connections**, not moving the region.

---

## What's already optimised (don't redo these)

The backend is not where the time is going. Prior work already did the right
things, and the plan below preserves all of it:

- **The LLM is gated.** Simple correct answers use deterministic feedback and
  skip Gemini entirely (`tutorEngine.js:414`, `needsLLM`). Only hints, answer
  reveals, and streaks call the model.
- **Feedback and next-question run in parallel**, not in series
  (`tutorEngine.js:419–478`).
- **Question generation is fully local** — no LLM call in
  `questionGenerator.js`.
- **The trigger function is kept warm** (`answerQuestion` has `minInstances: 1`,
  `index.js:501`).
- **Inline base64 audio was tried and correctly reverted** (heap overflow); URL
  playback is the proven path. The plan streams bytes over HTTP instead of
  re-introducing a giant Firestore string.

---

## The plan (recommended: the middle ground)

Two phases. **Phase 0** is pure configuration — deploy it first, independently,
and measure. **Phase 1** is the structural change that removes the serial hops.
**Phase 2** is optional and listed for completeness.

### Phase 0 — zero-risk config wins (do this first)

No architecture change; each is a few lines and individually deployable.

| # | Change | File / location | Est. saving |
|---|---|---|---|
| 0.1 | `minInstances: 1` on `transcribeAudio` (and `synthesizeSpeech`) | `index.js:715`, `:685` | removes the **3–12 s** cold-start spike on the first answer |
| 0.2 | `preferRest: true` on Firestore init | `index.js:66` (`initializeFirestore(app,{preferRest:true})`) | ~2–5 s off any remaining cold start |
| 0.3 | `thinkingConfig: { thinkingBudget: 0 }` in the Gemini config | `tutorEngine.js:232`, `index.js:403` | ~0.5–1.3 s on wrong-answer turns |
| 0.4 | Reuse one keep-alive TLS connection across polls; stop calling `_initSSL()` per iteration; drop poll interval to ~250 ms | `firebase_client.h:446–492`, `:36` | ~0.5–1.2 s off stage F |
| 0.5 | (Optional) trim `trailSilenceMs` 1200 → ~700–800 ms | `homework_assistant.ino:266` | ~0.4 s of perceived delay — tune against false endpoints |

Phase 0 alone should take a warm turn from ~5.5–10 s down to ~4–7 s and, more
importantly, kill the cold-start spike — without restructuring anything.

### Phase 1 — collapse the turn into one synchronous call (the core win)

Add a synchronous HTTPS function — call it `processTurn` — that the device calls
**once** and blocks on:

```text
device  ──POST {sessionId, childAnswer}──▶  processTurn (onRequest/onCall, warm)
                                              │  grade (deterministic)
                                              │  Gemini only if needsLLM (thinking off)
                                              │  Google TTS  → MP3 bytes (no Storage)
                                              │  write session/question/exchange docs
                                              │     (same schema, for the Flutter app)
device  ◀──── JSON {feedback, emotion, nextQuestion, isCorrect, …}
device  ◀──── + MP3 audio (response body of a paired /turnAudio call, or signed-URL)
```

What this removes from the budget, all at once:

- **D — Eventarc propagation** (0.3–1.5 s): gone; the device calls the function directly.
- **F — the entire poll loop** (0.6–1.6 s + handshakes): gone; the answer is the HTTP response.
- The intermediate `status:"processing"` write: gone.
- **G — separate Storage download** (0.7–1.2 s): folded in; audio returns with the result and decodes as it arrives.
- **C — the device's own Firestore write** (0.4–0.8 s): gone; the function writes the docs server-side.

Keep `answerQuestion` (the trigger) deployed as a **fallback / app-initiated
path** so nothing else breaks; the device simply stops depending on it. The
Flutter app keeps watching the same `sessions/exchanges` docs because
`processTurn` still writes them — it just writes them itself instead of via the
trigger.

**Audio transport detail.** The cleanest option that avoids both the Storage
round trip *and* the Arduino‑String heap problem: have the device decode the MP3
**streamed from the HTTP response** (the helix decoder in `tts_player.h` already
works frame-by-frame — feed it from `http.getStreamPtr()` as bytes arrive instead
of buffering the whole file first). If keeping the request/response strictly JSON
is preferred, return a short-lived signed Storage URL but start the TTS synth
earlier; the streamed-body approach is the bigger win.

After Phase 0 + Phase 1, the warm turn is essentially: **STT round trip + one
combined turn call**, with audio playback starting mid-response.

### Estimated budget — after Phase 0 + 1 (warm)

| Stage | Est. | Notes |
|---|---|---|
| A — silence endpoint | 0.8–1.2 s | optional trim (0.5) |
| B — STT round trip | 1.2–2.2 s | warmed (0.1/0.2); optionally raw PCM upload to drop the 33% base64 inflation |
| B→E — single `processTurn` call | 1.0–2.5 s | warm TLS + grade + Gemini (only if needed, thinking off) + TTS; **audio streams back** |
| D, F, G, C | **eliminated** | trigger, poll, separate download, device write all removed |
| **Total (excluding A)** | **~2.2–4.7 s** | playback begins before the response fully arrives |

Net: roughly **3–5 s** off the warm path versus today, plus the cold-start cliff
removed. The child hears feedback in ~2–4 s after speaking instead of ~6–10 s.

### Phase 2 — optional, only if you want to push further

- **Fold STT into `processTurn`:** send the audio to the same endpoint, so STT +
  grade + Gemini + TTS are one round trip (collapses B into the turn call). Saves
  another ~0.5–1 s of connection overhead; bigger change, and couples STT to the
  turn.
- **Streaming STT** (`streaming Recognize`) so transcription overlaps with the
  child finishing the last word. Marginal for short answers.
- **Pipeline Gemini → TTS:** start synthesising the first sentence while the model
  is still emitting. Complex; only worth it on the LLM turns.

---

## Risks / things to verify on hardware

- **`processTurn` runtime vs. timeout.** A synchronous call now holds the
  connection through Gemini + TTS. Set the function `timeoutSeconds` generously
  (e.g. 30) and the device HTTP timeout to match; keep the function warm so the
  client isn't holding a connection open through a cold start.
- **Streamed MP3 decode underrun.** If bytes arrive slower than real-time
  playback, the helix path can starve. Keep a small PSRAM pre-buffer (e.g. start
  playback after the first ~8–16 KB) rather than zero buffering.
- **`thinkingBudget: 0` output quality.** Verify the Hebrew feedback JSON stays
  well-formed and warm without thinking; `safeJsonParse` already guards parsing,
  but spot-check hint/answer-reveal turns.
- **`preferRest: true` semantics.** REST-mode Firestore is fine for this
  read/write pattern; just confirm no code depends on gRPC-only behaviour
  (none observed).
- **`minInstances` cost.** One always-warm instance per function bills idle
  memory + ~10% CPU. With STT + turn warmed that's a small, predictable cost —
  acceptable for a responsive demo; scale to 0 when the project is dormant.
- **Region pinning for Gemini.** If you move off the `global` endpoint, confirm
  `gemini-2.5-flash` is actually served in the chosen region or calls will 404.

---

## How to measure

Numbers above are estimates — instrument before committing to them:

- **Device side:** wrap each stage in `millis()` deltas and print them
  (`[lat] stt=… post=… poll=… play=…`). The serial log already has the hooks;
  add timing around `transcribeAudio`, `firestorePostLearningTurn`,
  `firestorePollForTurnResult`, and `speakAudio` in `processCapturedAnswer()`.
- **Function side:** Cloud Functions logs show invocation + execution time;
  enable `console.time`/`timeEnd` around the Gemini and TTS calls. Watch for the
  cold-start log line on the first call after idle.
- **One metric that matters:** wall-clock from "device finished recording" to
  "first audio sample out". Capture it for (a) a correct answer and (b) a wrong
  answer, warm and cold, before and after each phase.

---

## One-screen change checklist

```text
Phase 0 (config, deploy first, no architecture change)
  [ ] index.js:715   transcribeAudio  → add { minInstances: 1 }
  [ ] index.js:685   synthesizeSpeech → add { minInstances: 1 }
  [ ] index.js:66    initializeFirestore(app, { preferRest: true })
  [ ] tutorEngine.js:232 / index.js:403  config.thinkingConfig = { thinkingBudget: 0 }
  [ ] firebase_client.h:446  reuse one TLS connection across polls (drop per-poll _initSSL)
  [ ] firebase_client.h:492  poll delay 500 → ~250 ms
  [ ] homework_assistant.ino:266  trailSilenceMs 1200 → ~750 ms (tune)

Phase 1 (structural — the recommended middle ground)
  [ ] new CF processTurn: grade + (gated) Gemini + TTS, returns result inline,
        writes session/question/exchange docs server-side
  [ ] new CF processTurn keeps { minInstances: 1 }
  [ ] tts_player.h: decode MP3 from the streamed HTTP response (don't buffer whole file)
  [ ] firebase_client.h: replace post+poll with one processTurn call
  [ ] keep answerQuestion trigger as fallback / app path (don't delete)

Phase 2 (optional)
  [ ] fold STT into processTurn (send audio, one round trip)
  [ ] streaming STT / Gemini→TTS pipelining
```
