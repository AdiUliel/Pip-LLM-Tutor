# Statistics — Pip LLM Tutor

The statistics the system measures and can present: what the parent app shows,
the measured engineering numbers, the per-turn data captured for analytics, and
forward-looking additions. Everything is grounded in the actual data model
(field names below exist in Firestore).

---

## 1. Shown in the parent app

| Statistic | Where | Source |
|---|---|---|
| Lifetime totals: sessions, questions, stars | Trends → "סיכום כללי" card | `StatsProvider` aggregates over `sessions` |
| Active-days streak ("practised N days in a row") | Trends summary card | `StatsProvider.activeDayStreak` |
| Sessions this week + avg minutes/session | Trends summary card | `StatsProvider.sessionsThisWeek` / `avgSessionMinutes` |
| Minutes practised today | Dashboard "today ring" | `StatsProvider.minutesToday` |
| Per-session accuracy % and correct/total | Reports list | `session.correctCount / questionsAsked` |
| Stars earned + longest streak per session | Reports / session detail | `session.starsEarned`, `session.longestStreak` |
| Session duration + date + end reason | Reports list | `startedAt`, `endedAt`, `endReason` |
| Accuracy over time (chart) | Trends | `StatsProvider.accuracySeries` |
| Mood over time (chart) | Trends | `moodSummary` per session |
| Time spent per session, by subject (bar chart) | Trends | `session.durationMinutes` + `subject` |
| Subject split (math vs English) | Trends | `StatsProvider.sessionsBySubject` |
| Per-question log: prompt, transcript, correct/wrong, feedback | Session detail | `sessions/{id}/questions` subcollection |
| Device online/offline + current question live | Device monitor | `deviceState.lastHeartbeat` freshness |
| Average accuracy across sessions | Dashboard | `StatsProvider.averageAccuracy` |

## 2. Measured engineering statistics (poster-ready)

Real numbers, measured on hardware (see `latency-results.pdf` / `LATENCY.md`):

| Metric | Value |
|---|---|
| Turn latency (record-end → robot speaks), original | **14.8–19 s** |
| Turn latency after optimisation (Phase 2) | **4.0–5.8 s** |
| Improvement | **~3.5× faster** (10–13 s cut per turn) |
| Serial network hops per turn | **5 → 1** |
| Audio delivery: URL download → inline decode | **2.6–3.8 s → 2–4 ms** |
| Cold-start spike eliminated | was up to **+12 s** on the first answer |
| Gemini hint call (only on wrong answers) | adds **~1.5 s** |
| SD-cached prompt playback vs cloud synth | **tens of ms vs ~2–3 s** |
| Raw-PCM upload vs base64 | **−33% bytes** on the uplink |

Codebase (counted from the repo, 2026-07-17):

| Metric | Value |
|---|---|
| Total code | **~18,400 LOC**: firmware 3,671 · Cloud Functions 2,985 · Flutter app 10,496 · pip_face 733 · HW unit tests 512 |
| Deployed Cloud Functions | **12** (4 HTTP, 2 callable, 5 Firestore triggers, 1 scheduled) |
| Firestore collections | **8** (children, sessions, exchanges, questions, materials, parents, deviceState, pairingCodes) |
| Question bank | **105 English vocab words** across 12 topics + **20 grammar questions** + procedurally-generated math (4 operations × 10 difficulty levels) |
| pip_face emotions/states | **12** (idle, speaking, listening, thinking, happy, proud, celebrating, encouraging, concerned, playful, sleepy, oops) — see `FACE_STATE_MACHINE.md` |
| Pre-cached spoken phrases | **8** (SD cache, warmed at boot) |
| Languages | Hebrew UI + speech, English content; he-IL / en-US STT |

## 3. Per-turn data now captured (ready to chart)

Each answered question writes these fields (in `tutorEngine.js`); the data
accumulates from deploy onward and can be charted in the app with a query only —
no further backend work:

| Field (where) | Unlocks |
|---|---|
| `questions.topic` | **Accuracy by topic** ("strong in colors, weak in division") — the most actionable insight for a parent |
| `questions.difficulty` | **Accuracy by difficulty level** — shows the adaptive engine working on the 1–10 ladder |
| `questions.fromMaterial` | **Material vs generated**: share and accuracy on parent-uploaded homework |
| `questions.mood` + `questions.correct` | **Mood ↔ accuracy correlation** — the emotional-tutor premise, with data |
| `session.longestStreak` (now maintained) | **Longest correct streak** — the app already renders it; previously always 0, now real |
| `children.levelHistory[]` (`{subject, level, at}`) | **Level progression over time** — the clearest "is my child improving" curve |
| `exchange.llmUsed` | **LLM skip rate**: % of turns answered deterministically (the `needsLLM` gate) — quantifies the cost/latency win |
| `exchange.processingMs` + `session.processingMsTotal` | **Live turn-latency distribution / averages** in production, continuing the LATENCY.md story |
| `exchange.llmInputTokens`/`llmOutputTokens` + `session.llmInputTokensTotal`/`llmOutputTokensTotal` | **Gemini token usage & cost per lesson** — "a full lesson costs ~X agora" |
| `exchange.ttsChars` + `session.ttsCharsTotal` | **TTS characters synthesized** — the other half of the per-lesson cost model |
| `session.sttEmptyCount` | **STT-empty / reprompt rate** — real-world audio-robustness signal (how often no speech was recognised) |
| `material.itemsCount` + `material.extractionTruncated` | **Materials funnel** — questions extracted per uploaded file, and whether extraction was cut off |

## 4. Computable now from existing session data

Available app-side from the `sessions` stream, no backend change (some already
surfaced in §1; the rest are a small `StatsProvider` addition away):

- Hint effectiveness: % solved on the 2nd try after a hint (consecutive
  `questions` with the same prompt).
- Most-missed questions/words ("top 5 hardest") — group `questions` by prompt.
- Child thinking time per question — gap between `answeredAt` and the next `askedAt`.
- Time-of-day usage histogram — from `sessions.startedAt`.
- Session end-reason breakdown (child asked / declined / 50-min cap / inactivity)
  — `StatsProvider.endReasonBreakdown`.
- Materials funnel: files uploaded → questions extracted → blocked by validation
  (`materials` docs: `items.length`, `status`).

## 5. Future work (needs device firmware / instrumentation)

Lower priority — these are device-side (need a reflash) or need a monitoring hook:

- **TTS cache hit rate** — the device logs SD cache HIT/MISS to serial only; would
  need a counter sent up via `deviceState`.
- **Device telemetry: Wi-Fi RSSI, free heap, wake count, recording duration** —
  all measurable on-device (`WiFi.RSSI()`, `ESP.getFreeHeap()`, RTC-memory wake
  counter) but only serial-logged today; would ship in a `deviceState.telemetry`
  field.
- **Device uptime % / heartbeat gaps** — needs a log or Cloud Monitoring, since
  `deviceState.lastHeartbeat` is overwritten each beat.
- **Answers-per-hour cloud cost** — the token, STT-seconds and TTS-character
  counters (now captured) can be combined into a per-device economics figure.
