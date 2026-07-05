#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Wake-word module — English "hey pip" (self-contained, no external library)
//
// Replaces the push-to-talk button: the device listens continuously on the
// ES8311 mic and fires when it hears "hey pip"; the main loop then records the
// child's answer (ended automatically by trailing silence).
//
// HOW IT WORKS — fully on-device, NOTHING to install:
//   • A small CNN (trained offline on synthetic "hey pip" speech) runs on a
//     log-mel spectrogram of the last 1 second of audio. Both the front-end and
//     the network are plain C in ww_infer.h + hey_pip_model.h — no TensorFlow,
//     no Edge Impulse, no Arduino ML library. The exact same math was verified
//     bit-for-bit against the trainer on a host PC.
//   • Streaming: each poll reads ~200 ms of new mic audio into a rolling 1 s
//     window and runs one inference; prob["hey pip"] ≥ WAKE_WORD_THRESHOLD fires.
//   • Audio comes from the SAME I2S/ES8311 path used for recording: we reuse the
//     sketch's i2s_start_recording()/i2s_stop_recording() and strip to the mic
//     channel. I2S is installed only while listening and released the moment the
//     wake word fires, so it never collides with the recorder or the TTS player.
//
// TUNING: the defaults below are tuned for SENSITIVITY — the reported failure was
// "rarely detects", made worse by the partly-covered mic hole (= a quieter mic).
// If it now FALSE-fires, raise WAKE_WORD_THRESHOLD (toward 0.80–0.90), raise
// WW_MIN_PEAK, or raise WW_CONSEC. If it still MISSES, build with WW_TEST_MODE 1,
// say "hey pip", read the 'max' score column, and set WAKE_WORD_THRESHOLD a touch
// below it. The model was trained on synthetic voices, so expect to tune this once
// on the real device. See Documentation/WAKE_WORD.md (per-home calibration recipe).
//
// ROBUSTNESS LAYERS (see the knob blocks below, in gate order):
//   warm-up after re-arm (skips zero-padded windows) → refractory after a fire →
//   absolute peak floor (WW_MIN_PEAK) → adaptive ambient RMS gate (WW_GATE_K) →
//   CNN → threshold+margin (WW_MARGIN) → WW_CONSEC debounce. The main sketch can
//   also nudge the threshold up temporarily after probable false fires
//   (wakeWordNudgeThreshold).
// ─────────────────────────────────────────────────────────────────────────────

#include <Arduino.h>
#include <driver/i2s.h>
#include <string.h>
#include "pins.h"
#include "ww_infer.h"     // pure-C log-mel + CNN (includes hey_pip_model.h)

// Confidence (0..1) on the "hey pip" class needed to trigger. Lowered 0.80→0.70:
// once the input is leveled correctly (noise gate + auto-gain below), a real
// "hey pip" scores well above this — 0.80 was rejecting borderline-but-valid
// utterances, especially when the wake word is spoken at arm's length or farther.
#ifndef WAKE_WORD_THRESHOLD
#define WAKE_WORD_THRESHOLD 0.70f
#endif

// ── Input auto-gain ───────────────────────────────────────────────────────────
// The model was trained on clips normalized to peak ≈0.25–1.0, but this ES8311
// mic delivers a much quieter signal (peak ~0.01–0.03). Log-mel features are
// level-sensitive, so without scaling the input the model sees out-of-distribution
// audio and never fires. Before each inference we scale the 1 s window so its peak
// reaches WW_TARGET_PEAK — matching training — capped at WW_MAX_GAIN so a silent
// room's noise floor isn't blown up to speech level (which could false-fire).
// This is wake-word-only; it does NOT touch the shared codec gain or the STT path.
#ifndef WW_TARGET_PEAK
#define WW_TARGET_PEAK 0.60f
#endif
#ifndef WW_MAX_GAIN
#define WW_MAX_GAIN 50.0f          // 20→50: with the mic hole partly covered the window
                                   // peak can sit ~0.012–0.02, which needs 30–50× to reach
                                   // WW_TARGET_PEAK. The noise gate (WW_MIN_PEAK) still stops
                                   // a silent room from being amplified into a false fire.
#endif

// ── False-fire suppression ────────────────────────────────────────────────────
// NOISE GATE: ignore any window whose RAW peak is below this — pure silence must
// never be amplified-then-scored (that produced the 0.4–0.8 "ghost" scores). But
// the gate MUST sit below a real, possibly-distant "hey pip": with the mic hole
// partly covered a valid utterance can peak as low as ~0.015, so the gate lives
// just under that and we lean on the model's dedicated noise class + the WW_CONSEC
// debounce to reject the occasional room-noise window that slips through. Raise it
// if a quiet room false-fires; lower it if a soft wake word is ignored (watch the
// 'win' column in test mode).
#ifndef WW_MIN_PEAK
#define WW_MIN_PEAK 0.015f         // 0.030→0.015 — 0.030 was gating real (distant/quiet) wake words
#endif
// DEBOUNCE: a real wake word stays in the 1 s window for several polls, so it
// scores high on WW_CONSEC polls in a row; stray noise blips don't. Require this
// many consecutive over-threshold polls before firing.
#ifndef WW_CONSEC
#define WW_CONSEC 2   // 3→2: 3-in-a-row over a high threshold was hard to satisfy for real
                      // (quieter) utterances. 2 consecutive polls still rejects single-poll
                      // score jitter. Raise back to 3 if you start getting false fires.
#endif

// New audio pulled in per poll (samples @16 kHz). 1600 = 100 ms → ~100 ms detect
// granularity; the model still sees the full 1 s window each time. Smaller hop =
// the phrase is scored at more alignments inside the window (a streaming-KWS recall
// win) and detection reacts ~100 ms sooner, at the cost of one extra inference/100 ms.
#ifndef WW_HOP
#define WW_HOP 1600
#endif

// ── Margin rule ───────────────────────────────────────────────────────────────
// The model is a 3-way softmax: prob[0]=noise, prob[1]="hey pip", prob[2]=other
// speech. Besides clearing the threshold, prob[1] must BEAT the best competitor
// by this margin. At threshold 0.70 this is implied (2*0.70-1 = 0.40), so the
// rule costs nothing today — its job is making a LOWER per-home threshold safe:
// at 0.60–0.65 it still enforces p1 ≥ 0.60 absolute and rejects windows where
// the "other speech" class concentrates probability (the confusable case).
#ifndef WW_MARGIN
#define WW_MARGIN 0.40f
#endif

// ── Refractory ────────────────────────────────────────────────────────────────
// After a fire, suppress re-triggers for this long. In the current call graph a
// fire tears down I2S immediately, so this is 2-line insurance for any future
// faster re-arm path rather than an active suppressor.
#ifndef WW_REFRACTORY_MS
#define WW_REFRACTORY_MS 2500
#endif

// ── Adaptive ambient gate ─────────────────────────────────────────────────────
// A rolling estimate of the room's noise floor (EMA of window RMS on polls the
// model considers non-speech). A window must exceed ambient*WW_GATE_K to be worth
// scoring — this kills steady-noise false fires (fan / AC / TV murmur) that pass
// the fixed peak gate, and skips the CNN on those polls (net CPU saving).
// Asymmetric alphas: SLOW rise / FAST fall — an inflated ambient costs recall
// (the primary complaint), a deflated one only costs inferences that margin +
// consec still reject. WW_AMBIENT_MAX caps the estimate so the gate can never
// climb above 0.02 RMS (below close-range speech) — no lockout after transients.
#ifndef WW_GATE_K
#define WW_GATE_K 2.5f             // fallback to 2.0 if a distant quiet-room "hey pip" gets gated
#endif
#ifndef WW_AMBIENT_ALPHA_UP
#define WW_AMBIENT_ALPHA_UP 0.02f  // ~5 s time constant at 10 polls/s
#endif
#ifndef WW_AMBIENT_ALPHA_DOWN
#define WW_AMBIENT_ALPHA_DOWN 0.25f // ~0.4 s — recover recall quickly when the room goes quiet
#endif
#ifndef WW_AMBIENT_MAX
#define WW_AMBIENT_MAX 0.008f      // gate ceiling = 0.008*2.5 = 0.02 RMS
#endif
#ifndef WW_AMBIENT_SCORE_MAX
#define WW_AMBIENT_SCORE_MAX 0.30f // only polls scoring below this feed the ambient EMA
#endif

// ── Reused from the main sketch (identical ES8311 I2S clocking for listen+record)
void i2s_start_recording();
void i2s_stop_recording();
void faceTick();

// ── State / PSRAM scratch ─────────────────────────────────────────────────────
static bool   ww_active = false;
static int    ww_mic_ch = 0;
static int    ww_consec = 0;       // consecutive over-threshold polls (debounce)
static int    ww_warmupPolls   = 0;     // polls until the ring holds 1 s of REAL audio
static uint32_t ww_suppressUntil = 0;   // refractory deadline (millis) after a fire
static float  ww_ambientRms    = -1.0f; // EMA of window RMS on non-speech polls; <0 = unseeded
static float  ww_thrBump       = 0.0f;  // temporary threshold add (caller-driven, see nudge below)
static uint32_t ww_thrBumpUntil = 0;
static float* ww_ring = nullptr;   // rolling 1 s window  (WW_CLIP)
static float* ww_sig  = nullptr;   // gain-normalized copy fed to inference (WW_CLIP)
static float* ww_feat = nullptr;   // log-mel             (WW_FRAMES*WW_MELS)
static float* ww_b1 = nullptr;     // conv1 activations
static float* ww_b2 = nullptr;     // conv2 activations
static float* ww_b3 = nullptr;     // conv3 activations

static inline int ww_oh(int ih){ return (ih-WW_K)/2+1; }

// Allocate scratch (PSRAM) and print model info. Call once at boot.
inline bool wakeWordBegin(){
  int H1=ww_oh(WW_FRAMES), W1=ww_oh(WW_MELS);
  int H2=ww_oh(H1),        W2=ww_oh(W1);
  int H3=ww_oh(H2),        W3=ww_oh(W2);
  ww_ring = (float*)ps_malloc(sizeof(float)*WW_CLIP);
  ww_sig  = (float*)ps_malloc(sizeof(float)*WW_CLIP);
  ww_feat = (float*)ps_malloc(sizeof(float)*WW_FRAMES*WW_MELS);
  ww_b1   = (float*)ps_malloc(sizeof(float)*H1*W1*WW_C1_OUT);
  ww_b2   = (float*)ps_malloc(sizeof(float)*H2*W2*WW_C2_OUT);
  ww_b3   = (float*)ps_malloc(sizeof(float)*H3*W3*WW_C3_OUT);
  if(!ww_ring||!ww_sig||!ww_feat||!ww_b1||!ww_b2||!ww_b3){
    Serial.println("[WakeWord] ❌ PSRAM alloc failed.");
    return false;
  }
  Serial.printf("[WakeWord] 'hey pip' model ready: %d-frame log-mel, CNN %d/%d/%d, threshold %.2f\n",
                WW_FRAMES, WW_C1_OUT, WW_C2_OUT, WW_C3_OUT, (double)WAKE_WORD_THRESHOLD);
  return true;
}

// Pick the stereo slot that carries the mic (ES8311 is mono on one side).
static void ww_calibrate_channel(){
  const int FR=1024; static int16_t tmp[FR*2]; size_t br=0;
  i2s_read(I2S_PORT,(char*)tmp,sizeof(tmp),&br,portMAX_DELAY);
  size_t fr=br/4; int64_t s0=0,s1=0;
  for(size_t i=0;i<fr;i++){ s0+=(int64_t)tmp[i*2]*tmp[i*2]; s1+=(int64_t)tmp[i*2+1]*tmp[i*2+1]; }
  ww_mic_ch=(s1>s0)?1:0;
}

// Begin listening: install I2S, find mic channel, clear the rolling window.
inline void wakeWordStartListening(){
  if(ww_active) return;
  i2s_start_recording();
  ww_calibrate_channel();
  memset(ww_ring,0,sizeof(float)*WW_CLIP);
  // WARM-UP: the ring was just zeroed, so for the next WW_CLIP/WW_HOP polls the
  // window is partly zeros — out-of-distribution input the model was never
  // trained on, which can score arbitrarily (this was a real source of
  // "fires by itself right after re-arm"). Skip inference until the window
  // holds a full second of real audio. Not added latency: the child physically
  // cannot have said "hey pip" into a window that didn't exist yet.
  ww_warmupPolls = WW_CLIP / WW_HOP;
  ww_consec = 0;
  // Deliberately NOT reset here: ww_suppressUntil (refractory must survive the
  // re-arm) and ww_ambientRms (a TTS interlude doesn't change the room).
  ww_active=true;
}

// Stop listening and release I2S for the recorder / TTS player.
inline void wakeWordStopListening(){
  if(!ww_active) return;
  i2s_stop_recording();
  ww_active=false;
}

// Scale a 1 s window to the training peak level (WW_TARGET_PEAK), capped at
// WW_MAX_GAIN. Writes to `out` and never touches the rolling window, so the gain
// can't compound across polls. Also reports the RAW window RMS (same pass, no
// extra loop) — the adaptive ambient gate keys on it. Returns the raw peak.
static float ww_normalize(const float* in, float* out, float* gainOut,
                          float* rmsOut = nullptr){
  float peak = 1e-6f, sumSq = 0.0f;
  for(int i=0;i<WW_CLIP;i++){
    float a = fabsf(in[i]); if(a>peak) peak=a;
    sumSq += in[i]*in[i];
  }
  float g = WW_TARGET_PEAK / peak;
  if(g > WW_MAX_GAIN) g = WW_MAX_GAIN;
  if(g < 1.0f)        g = 1.0f;     // already loud enough — leave as-is, never attenuate
  for(int i=0;i<WW_CLIP;i++) out[i] = in[i]*g;
  if(gainOut) *gainOut = g;
  if(rmsOut)  *rmsOut  = sqrtf(sumSq/(float)WW_CLIP);   // RAW window RMS (pre-gain)
  return peak;                      // RAW window peak (pre-gain) — input to the noise gate
}

// Feed one non-speech window RMS into the rolling ambient estimate.
// Seed-on-first; then EMA with slow rise / fast fall (see the knob block above);
// clamped so the gate can never lock out close-range speech.
static void ww_ambientUpdate(float rms){
  if(ww_ambientRms < 0.0f) ww_ambientRms = rms;
  else {
    float a = (rms > ww_ambientRms) ? WW_AMBIENT_ALPHA_UP : WW_AMBIENT_ALPHA_DOWN;
    ww_ambientRms += a * (rms - ww_ambientRms);
  }
  if(ww_ambientRms > WW_AMBIENT_MAX) ww_ambientRms = WW_AMBIENT_MAX;
}

// Effective threshold = compile-time base + a temporary caller-driven bump.
static inline float ww_effThreshold(){
  return WAKE_WORD_THRESHOLD +
         (((int32_t)(ww_thrBumpUntil - millis()) > 0) ? ww_thrBump : 0.0f);
}

// Temporarily raise the effective threshold (e.g. +0.05 for 30 s). Called by the
// main sketch after repeated triggers that were followed by silence — a probable
// false-fire storm quenches itself without permanently hurting recall.
inline void wakeWordNudgeThreshold(float delta, uint32_t durationMs){
  ww_thrBump      = delta;
  ww_thrBumpUntil = millis() + durationMs;
  Serial.printf("[WakeWord] threshold %.2f -> %.2f for %lus (false-fire quench)\n",
                (double)WAKE_WORD_THRESHOLD,
                (double)(WAKE_WORD_THRESHOLD + delta),
                (unsigned long)(durationMs/1000UL));
}

// Pull ~WW_HOP ms of new audio, slide the window, run one inference.
// Returns 1 if "hey pip" detected this poll, 0 if not, -1 if not listening.
inline int wakeWordPoll(){
  if(!ww_active) return -1;

  // Slide the rolling window left by WW_HOP and read fresh mono mic samples in.
  memmove(ww_ring, ww_ring+WW_HOP, (WW_CLIP-WW_HOP)*sizeof(float));
  float* tail = ww_ring+(WW_CLIP-WW_HOP);
  int got=0; int16_t st[256*2];
  while(got<WW_HOP){
    int want=WW_HOP-got, frames=want<256?want:256; size_t br=0;
    i2s_read(I2S_PORT,(char*)st,frames*4,&br,portMAX_DELAY);
    int f=br/4;
    for(int i=0;i<f && got<WW_HOP;i++) tail[got++]=st[i*2+ww_mic_ch]/32768.0f;
    faceTick();
  }

  // Gate chain, cheap → expensive. Audio above was already read regardless, so
  // I2S never backs up while a gate is suppressing detection.
  float winRms;
  float winpeak = ww_normalize(ww_ring, ww_sig, nullptr, &winRms);

  // G0 — warm-up: window still padded with re-arm zeros; scores are meaningless.
  // (Also skip the ambient EMA — a zero-diluted RMS would drag it down.)
  if(ww_warmupPolls > 0){ ww_warmupPolls--; ww_consec = 0; return 0; }

  // G1 — refractory after a fire (rollover-safe). No ambient update: this close
  // to a trigger the window likely still holds the wake word itself, not room.
  if((int32_t)(ww_suppressUntil - millis()) > 0){ ww_consec = 0; return 0; }

  // G2 — absolute peak floor: pure silence must never be amplified-then-scored.
  if(winpeak < WW_MIN_PEAK){ ww_consec = 0; ww_ambientUpdate(winRms); return 0; }

  // G3 — adaptive ambient gate: steady room noise (fan/AC/TV murmur) tracks the
  // EMA, so it never reaches ambient*WW_GATE_K and the CNN is skipped entirely.
  // Unseeded (<0) = gate off → graceful degradation to the old behavior.
  if(ww_ambientRms >= 0.0f && winRms < ww_ambientRms * WW_GATE_K){
    ww_consec = 0; ww_ambientUpdate(winRms); return 0;
  }

  float prob[WW_NCLASS];
  ww_infer(ww_sig, ww_feat, ww_b1, ww_b2, ww_b3, prob);     // prob[1] = "hey pip"
  if(prob[1] < WW_AMBIENT_SCORE_MAX) ww_ambientUpdate(winRms); // model says "not us" → it's room

  // G4 — threshold (with any temporary bump) + class margin: "hey pip" must both
  // clear the bar AND beat the best competitor class decisively.
  float margin = prob[1] - fmaxf(prob[0], prob[2]);
  if(prob[1] >= ww_effThreshold() && margin >= WW_MARGIN){
    if(++ww_consec >= WW_CONSEC){                            // G5 — sustained over several polls → real
      ww_consec = 0;
      ww_suppressUntil = millis() + WW_REFRACTORY_MS;
      Serial.printf("[WakeWord] 'hey pip' p=%.2f m=%.2f  ✓\n",
                    (double)prob[1], (double)margin);
      return 1;
    }
  } else {
    ww_consec = 0;
  }
  return 0;
}

// ── On-device TEST MODE — live score printer for tuning / debugging ───────────
// Never returns. Streams one line per ~WW_HOP of audio with everything you need
// to diagnose an unresponsive wake word WITHOUT any cloud setup:
//   ch    = which stereo slot the mic was found on (0 or 1)
//   rms   = average level of this slice (0..1). Should jump up when you talk.
//   peak  = loudest sample this slice (0..1). ~0 while speaking ⇒ no mic audio.
//   score = model confidence for "hey pip" (0..1). Compare against the threshold.
//   max   = highest score seen so far this run (the peak of your "hey pip").
// Tuning recipe: say "hey pip" a few times, note the 'max', then set
// WAKE_WORD_THRESHOLD a touch below it (e.g. max 0.74 → threshold 0.65).
// Requires wakeWordBegin() to have run first (scratch buffers + codec up).
inline void wakeWordRunTestMode(){
  Serial.println("\n========== WAKE-WORD TEST MODE ==========");
  Serial.printf ("Threshold = %.2f, margin = %.2f. Say \"hey pip\" and watch 'p1'/'max'.\n",
                 (double)WAKE_WORD_THRESHOLD, (double)WW_MARGIN);
  Serial.println("  win = window peak BEFORE gain;  amb = rolling ambient RMS estimate");
  Serial.println("  p0/p1/p2 = noise / 'hey pip' / other-speech;  mrg = p1 - max(p0,p2)");
  Serial.println("  tags: [warmup] [refract] [gated:peak] [gated:amb] [margin] (arming) FIRE");
  Serial.println("  too many false fires  -> raise WAKE_WORD_THRESHOLD / WW_CONSEC / WW_GATE_K");
  Serial.println("  misses your wake word -> lower WAKE_WORD_THRESHOLD (watch 'max'); if the");
  Serial.println("  utterance shows [gated:amb], lower WW_GATE_K to 2.0");
  Serial.println("This loop never exits; reflash with WW_TEST_MODE 0 for normal use.\n");

  if(!ww_ring){
    Serial.println("[WWTEST] scratch not allocated — wakeWordBegin() failed (PSRAM?). Halting.");
    while(true){ faceTick(); delay(200); }
  }

  wakeWordStartListening();
  float maxScore = 0.0f;
  int   tmConsec = 0;
  while(true){
    // Same window slide + mic read as wakeWordPoll(), but inference ALWAYS runs
    // so scores stay visible even on polls the live gates would skip.
    memmove(ww_ring, ww_ring+WW_HOP, (WW_CLIP-WW_HOP)*sizeof(float));
    float* tail = ww_ring+(WW_CLIP-WW_HOP);
    int got=0; int16_t st[256*2]; float sumSq=0.0f, peak=0.0f;
    while(got<WW_HOP){
      int want=WW_HOP-got, frames=want<256?want:256; size_t br=0;
      i2s_read(I2S_PORT,(char*)st,frames*4,&br,portMAX_DELAY);
      int f=br/4;
      for(int i=0;i<f && got<WW_HOP;i++){
        float s = st[i*2+ww_mic_ch]/32768.0f;
        tail[got++]=s; sumSq+=s*s; if(fabsf(s)>peak) peak=fabsf(s);
      }
      faceTick();
    }
    float rms = sqrtf(sumSq/(float)WW_HOP);
    float gain, winRms;
    float winpeak = ww_normalize(ww_ring, ww_sig, &gain, &winRms);  // same as live detection
    float prob[WW_NCLASS];
    ww_infer(ww_sig, ww_feat, ww_b1, ww_b2, ww_b3, prob);
    float score  = prob[1];
    float margin = score - fmaxf(prob[0], prob[2]);
    if(score > maxScore) maxScore = score;

    // Mirror the live G0–G5 chain (including ambient updates) so the display
    // matches real behavior exactly.
    const char* tag = "";
    bool warmup  = (ww_warmupPolls > 0);
    bool refract = ((int32_t)(ww_suppressUntil - millis()) > 0);
    if(warmup){ ww_warmupPolls--; tmConsec = 0; tag = "  [warmup]"; }
    else if(refract){ tmConsec = 0; tag = "  [refract]"; }
    else if(winpeak < WW_MIN_PEAK){
      tmConsec = 0; ww_ambientUpdate(winRms); tag = "  [gated:peak]";
    }
    else if(ww_ambientRms >= 0.0f && winRms < ww_ambientRms * WW_GATE_K){
      tmConsec = 0; ww_ambientUpdate(winRms); tag = "  [gated:amb]";
    }
    else {
      if(score < WW_AMBIENT_SCORE_MAX) ww_ambientUpdate(winRms);
      if(score >= ww_effThreshold()){
        if(margin < WW_MARGIN){ tmConsec = 0; tag = "  [margin]"; }
        else if(++tmConsec >= WW_CONSEC){
          tmConsec = 0;
          ww_suppressUntil = millis() + WW_REFRACTORY_MS;   // mirror live refractory
          tag = "  <<< FIRE";
        } else tag = "  (arming)";
      } else tmConsec = 0;
    }
    Serial.printf("[WWTEST] ch=%d rms=%.4f win=%.3f amb=%.4f gain=%4.1fx "
                  "p0=%.2f p1=%.3f p2=%.2f mrg=%+.2f max=%.3f%s\n",
                  ww_mic_ch, (double)rms, (double)winpeak,
                  (double)(ww_ambientRms < 0.0f ? 0.0f : ww_ambientRms), (double)gain,
                  (double)prob[0], (double)score, (double)prob[2],
                  (double)margin, (double)maxScore, tag);
  }
}
