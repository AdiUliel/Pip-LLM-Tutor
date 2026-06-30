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
// on the real device. See Documentation/WAKE_WORD.md.
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
#define WW_MIN_PEAK 0.008f         // 0.030→0.015→0.008 — on-device measurement showed a real,
                                   // normal-volume "hey pip" raw-peaks at ~0.012 through this
                                   // board's built-in mic; 0.015 was squelching it before scoring.
                                   // 0.008 sits between the ~0.004 quiet-room floor and ~0.012 speech.
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

// Stereo slot carrying the mic. The ES8311 is mono and always lands on the SAME
// I2S slot on this board (slot 1 / RIGHT); the other slot carries constant
// clock/common-mode crosstalk at a steady ~0.007 (crest ~1.0). Pinned, not
// auto-detected: the old energy-based boot calibration compared the two slots
// over a 64 ms window and, whenever the room was quiet at boot, the crosstalk
// slot out-energized the silent mic and the device locked onto the WRONG (deaf)
// channel for the whole session — the root cause of "hey pip" intermittently
// never firing. (The STT recorder uses the same energy pick but runs it AFTER
// capturing loud speech, so it stays reliable there.) Flip to 0 only if a board
// revision wires the mic on the other slot.
#ifndef WW_MIC_CHANNEL
#define WW_MIC_CHANNEL 1
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

// Pin the mic to its fixed I2S slot (see WW_MIC_CHANNEL). We still read+discard
// one DMA buffer so any codec-startup garbage doesn't enter the rolling window,
// but we no longer let boot-time silence decide the channel (that locked onto the
// deaf crosstalk slot — see WW_MIC_CHANNEL).
static void ww_calibrate_channel(){
  const int FR=1024; static int16_t tmp[FR*2]; size_t br=0;
  i2s_read(I2S_PORT,(char*)tmp,sizeof(tmp),&br,portMAX_DELAY);
  ww_mic_ch = WW_MIC_CHANNEL;
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
