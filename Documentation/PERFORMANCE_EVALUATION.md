# Performance Evaluation — Pip LLM Tutor

Quantitative evaluation of the project's main algorithm — the **turn-processing
pipeline** — with the measured results and the method used to obtain them. The
hop-by-hop design analysis lives in [LATENCY.md](LATENCY.md); the source chart is
[latency-results.pdf](latency-results.pdf).

---

## 1. What was evaluated

A tutoring "turn" runs this pipeline on every answer:

```
record answer → STT → grade (deterministic + answer variants) → adaptive difficulty
             → gated Gemini feedback (only when it adds value) → TTS → play
```

The key algorithmic decisions evaluated:

1. **Latency of the whole pipeline** — the user-perceived metric that matters for
   a child: *record-end → robot starts speaking the feedback.*
2. **The LLM gating algorithm** — Gemini is called only on wrong answers or long
   streaks (`needsLLM` gate); simple correct answers use deterministic feedback.
   This trades a small quality cost for a large latency/cost saving.

## 2. Experimental setup (test conditions)

| Parameter | Value |
|---|---|
| Device | ESP32-S3 (LCDWIKI 2.8" board), firmware built for this project |
| Network | Home Wi-Fi, Israel |
| Cloud | Firebase Cloud Functions in `europe-west10` (Berlin), warm (`minInstances: 1`) |
| Metric | Wall-clock `RECORD → FIRST_SOUND` (button release → first audio sample out) |
| Instrumentation | Device-side `millis()` deltas printed as `[lat]` serial logs (see `homework_assistant.ino`, and §"How to measure" in LATENCY.md) |
| Turn types | Correct answer (no LLM) and wrong answer (Gemini hint), warm path |
| Date | Measured 2026-06-29 |

The same metric was captured for each configuration in the table below, so the
effect of each change is isolated.

## 3. Results — latency by configuration

| Configuration | Key characteristics | Correct | Wrong (Gemini) | Net hops |
|---|---|---|---|---|
| WAV over URL | full uncompressed WAV downloaded before playback | ~20 s † | — | 5 |
| MP3 over URL | write→trigger→poll→download | **14.8 s** | 17–19 s | 5 |
| + warm config | `minInstances`, `preferRest`, `thinkingBudget:0`, keep-alive poll | 10.8 s | 12.5 s | 5 (warm) |
| + single-call turn | one synchronous `processTurn` (grade + gated Gemini + TTS inline); SD-cached prompts | 6.3–6.6 s | 7–8.3 s | 2 |
| **+ server-side STT (current)** | raw PCM uploaded to `processTurn`; keep-alive heartbeat | **4.0–4.3 s** | 5.8 s | 1 |

† Not instrumented (timers were added at the MP3 baseline).

**Headline:** 14.8–19 s → **4.0–5.8 s**, a **~3.5× speed-up** (10–13 s cut per
turn). Network round-trips per turn dropped **5 → 1**; inline-audio decode dropped
from a **2.6–3.8 s** URL download to **2–4 ms**.

### Where the baseline 14.8 s went (measured, per stage)

The original MP3-URL baseline, broken down by stage on three real turns (device
`[lat]` timers) — this is what the optimisation had to remove:

| Stage | Turn 1 "milk" (correct, no LLM) | Turn 2 "SCH" (wrong, LLM) | Turn 3 "Joshua" (wrong, LLM) | Doc estimate |
|---|---|---|---|---|
| B — STT | 4.02 s | 2.83 s | 3.29 s | 1.5–3.0 s |
| C — post write | 2.27 s | 2.25 s | 2.08 s | 0.4–0.8 s |
| D+E+F — trigger + compute + poll | 2.27 s | 4.64 s | 4.40 s | 1.8–4.8 s |
| G — audio download + decode | 2.60 s | 3.04 s | 3.75 s | 0.7–1.2 s |
| Gap (2 unmeasured device-state writes) | ~3.65 s | ~4.20 s | ~5.50 s | not in doc |
| **RECORD → FIRST SOUND** | **14.8 s** | **17.0 s** | **19.0 s** | 5.4–9.8 s |

Gap = total − the four measured stages; it's the two `firestoreWriteDeviceState`
writes (local preprocessing is sub-100 ms). The single-call design collapses
B+C+D+E+F+the writes into one `processTurn` request — which is why the
current-configuration figures below land near 4 s.

## 4. Results — current configuration measured live on hardware

Three consecutive real turns (the raw `[lat]` output):

| Turn | Answer | Path | processTurnAudio | Decode | **RECORD→SOUND** |
|---|---|---|---|---|---|
| 1 | "sister" ✓ | grade only | 4341 ms | 2 ms | **4343 ms** |
| 2 | "milk" ✓ (streak) | grade only | 4066 ms | 4 ms | **4073 ms** |
| 3 | "no idea" ✗ | grade + Gemini hint | 5841 ms | 2 ms | **5843 ms** |

**Interpretation:** the remaining ~4 s on a correct turn is essentially the single
STT + compute + TTS round trip Israel↔Berlin — close to the practical floor
without changing region. Turn 3's extra ~1.5 s is the (correctly gated) Gemini
hint call — i.e. the gating algorithm keeps the common correct-answer path off the
LLM, saving ~1.5 s and one API call on the majority of turns.

## 5. Correctness of the grading algorithm

Answers are graded by a deterministic check (normalisation + accepted answer
variants) before any LLM call. During development, grading false-positives (e.g.
partial/near-miss transcripts scored as correct) were found and fixed
(commit `f9febcc`), and English answers spoken in Hebrew are handled via an STT
alternative-language path. Grading is validated qualitatively per turn against the
child's transcript, which is logged in `sessions/{id}/questions` (prompt, expected
answer, transcript, correct/wrong) for review.

The per-turn telemetry now recorded (`correct`, `topic`, `difficulty`, `llmUsed`,
`processingMs`, token counts — see [STATISTICS.md](STATISTICS.md)) makes the next
step a quantitative one: accuracy-by-topic, LLM-skip-rate, and live latency
percentiles can be measured directly from production data.

## 6. Reproducing the measurement

1. Flash the firmware with the `[lat]` logging enabled (already in
   `processCapturedAnswer()` / the main turn path).
2. Open the Arduino Serial Monitor; answer several questions (mix correct and
   wrong) on a warm session.
3. Read the `[lat] === turn: … RECORD->FIRST_SOUND=… ms ===` summary line per
   turn; average correct-answer and wrong-answer turns separately.
4. For cloud-side timing, the Functions logs show per-invocation execution time.
