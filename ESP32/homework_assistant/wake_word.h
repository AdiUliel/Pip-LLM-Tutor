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
// TUNING: if it false-fires, raise WAKE_WORD_THRESHOLD (e.g. 0.85–0.92); if it
// misses, lower it (≈0.70). The model was trained on synthetic voices, so expect
// to tune this once on the real device. See Documentation/WAKE_WORD.md.
// ─────────────────────────────────────────────────────────────────────────────

#include <Arduino.h>
#include <driver/i2s.h>
#include <string.h>
#include "pins.h"
#include "ww_infer.h"     // pure-C log-mel + CNN (includes hey_pip_model.h)

// Confidence (0..1) on the "hey pip" class needed to trigger.
#ifndef WAKE_WORD_THRESHOLD
#define WAKE_WORD_THRESHOLD 0.80f
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
#define WW_MAX_GAIN 20.0f          // was 40 — high gain on a silent room amplified noise into false fires
#endif

// ── False-fire suppression ────────────────────────────────────────────────────
// NOISE GATE: ignore any window whose RAW peak is below this. A quiet room sits
// around peak ~0.015; a real "hey pip" puts the window at ~0.07+. Gating below
// WW_MIN_PEAK means silence is never amplified-then-scored, which is what caused
// the 0.4–0.8 "ghost" scores. Raise it if you still get false fires; lower it if
// a softly-spoken wake word gets ignored (watch the 'win' column in test mode).
#ifndef WW_MIN_PEAK
#define WW_MIN_PEAK 0.030f
#endif
// DEBOUNCE: a real wake word stays in the 1 s window for several polls, so it
// scores high on WW_CONSEC polls in a row; stray noise blips don't. Require this
// many consecutive over-threshold polls before firing.
#ifndef WW_CONSEC
#define WW_CONSEC 2
#endif

// New audio pulled in per poll (samples @16 kHz). 3200 = 200 ms → ~200 ms detect
// granularity; the model still sees the full 1 s window each time.
#ifndef WW_HOP
#define WW_HOP 3200
#endif

// ── Reused from the main sketch (identical ES8311 I2S clocking for listen+record)
void i2s_start_recording();
void i2s_stop_recording();
void faceTick();

// ── State / PSRAM scratch ─────────────────────────────────────────────────────
static bool   ww_active = false;
static int    ww_mic_ch = 0;
static int    ww_consec = 0;       // consecutive over-threshold polls (debounce)
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
// can't compound across polls. Returns the gain applied (handy for logging).
static float ww_normalize(const float* in, float* out, float* gainOut){
  float peak = 1e-6f;
  for(int i=0;i<WW_CLIP;i++){ float a = fabsf(in[i]); if(a>peak) peak=a; }
  float g = WW_TARGET_PEAK / peak;
  if(g > WW_MAX_GAIN) g = WW_MAX_GAIN;
  if(g < 1.0f)        g = 1.0f;     // already loud enough — leave as-is, never attenuate
  for(int i=0;i<WW_CLIP;i++) out[i] = in[i]*g;
  if(gainOut) *gainOut = g;
  return peak;                      // RAW window peak (pre-gain) — input to the noise gate
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

  float winpeak = ww_normalize(ww_ring, ww_sig, nullptr);   // normalize + get raw level
  if(winpeak < WW_MIN_PEAK){ ww_consec = 0; return 0; }     // squelch: room too quiet to be speech
  float prob[WW_NCLASS];
  ww_infer(ww_sig, ww_feat, ww_b1, ww_b2, ww_b3, prob);     // prob[1] = "hey pip"
  if(prob[1] >= WAKE_WORD_THRESHOLD){
    if(++ww_consec >= WW_CONSEC){                            // sustained over several polls → real
      ww_consec = 0;
      Serial.printf("[WakeWord] 'hey pip' %.2f  ✓\n", (double)prob[1]);
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
  Serial.printf ("Threshold = %.2f. Say \"hey pip\" and watch 'score'/'max'.\n",
                 (double)WAKE_WORD_THRESHOLD);
  Serial.println("  win  = window peak BEFORE gain; quiet room ~0.015, real 'hey pip' ~0.07+");
  Serial.println("  gain = auto-boost to the model's training level (caps at WW_MAX_GAIN)");
  Serial.println("  [gated] = below WW_MIN_PEAK, ignored;  (arming)=1 hit;  FIRE=WW_CONSEC in a row");
  Serial.println("  too many false fires  -> raise WW_MIN_PEAK / WAKE_WORD_THRESHOLD / WW_CONSEC");
  Serial.println("  misses your wake word -> lower WAKE_WORD_THRESHOLD (watch 'max') or WW_MIN_PEAK");
  Serial.println("This loop never exits; reflash with WW_TEST_MODE 0 for normal use.\n");

  if(!ww_ring){
    Serial.println("[WWTEST] scratch not allocated — wakeWordBegin() failed (PSRAM?). Halting.");
    while(true){ faceTick(); delay(200); }
  }

  wakeWordStartListening();
  float maxScore = 0.0f;
  int   tmConsec = 0;
  while(true){
    // Same window slide + mic read as wakeWordPoll(), but we also measure the
    // level and ALWAYS print (never gated on the threshold).
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
    float gain; float winpeak = ww_normalize(ww_ring, ww_sig, &gain);   // same as live detection
    float prob[WW_NCLASS];
    ww_infer(ww_sig, ww_feat, ww_b1, ww_b2, ww_b3, prob);
    float score = prob[1];
    if(score > maxScore) maxScore = score;
    // Mirror the live gate + debounce so the display matches real behavior.
    const char* tag = "";
    if(winpeak < WW_MIN_PEAK){ tag = "  [gated: too quiet]"; tmConsec = 0; }
    else if(score >= WAKE_WORD_THRESHOLD){
      if(++tmConsec >= WW_CONSEC) tag = "  <<< FIRE";
      else                        tag = "  (arming)";
    } else { tmConsec = 0; }
    Serial.printf("[WWTEST] ch=%d  rms=%.4f  win=%.3f  gain=%4.1fx  score=%.3f  max=%.3f%s\n",
                  ww_mic_ch, (double)rms, (double)winpeak, (double)gain,
                  (double)score, (double)maxScore, tag);
  }
}
