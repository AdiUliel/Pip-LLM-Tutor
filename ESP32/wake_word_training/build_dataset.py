# build_dataset.py — assemble the robust "hey pip" training set (runbook Approach B).
#
# Sources (class order 0=noise, 1=hey-pip, 2=other-speech):
#   1) REAL  ww/pos        -> class 1  (anchor; trimmed + heavily augmented)
#   2) REAL  ww/neg/n*     -> class 2  (other speech)
#   3) REAL  ww/neg/c*     -> class 0  (ambient, sliced) + real-noise pool
#   4) NEURAL-TTS hey pip  -> class 1  (edge-tts EN+HE; espeak fallback)
#   5) TTS hard negatives  -> class 2  (edge-tts + espeak: hey/pip/pizza/near-misses/words)
#   6) SYNTHETIC noise     -> class 0  (white/pink/brown/hum) + augmented real ambient
#
# Output: /tmp/ww/X_c{0,1,2}.npy + Y_c{0,1,2}.npy  (train.py globs X_c*.npy) and melfb.npz.
import os
import sys
import time
import glob
import numpy as np

import feat
import augment
import real_data
import tts_edge

OUT = "/tmp/ww"
WW_DIR = os.environ.get("WW_DIR", "/home/user/Desktop/iot/ww")
SR, CLIP = feat.SR, feat.CLIP
np.random.seed(1234)
os.makedirs(OUT, exist_ok=True)

# ---- tunable counts (balanced; negatives kept rich to kill false-fires) ----
AUG_PER_POS      = 50     # real positives  -> ~7.5k class 1
AUG_PER_NEG      = 30     # real n* speech  -> ~6.0k class 2
TTS_POS_PER_VOICE = 9     # edge positives per voice (x10 voices) -> ~90 clips
AUG_TTS_POS      = 45     # -> ~4.0k class 1
N_EDGE_NEG       = 160    # unique edge negatives  -> x AUG_TTS_NEG (near-miss heavy)
AUG_TTS_NEG      = 22     # -> ~3.5k class 2
ESPEAK_NEG_VOICES = 4     # espeak neg voices per word
AUG_ESPEAK_NEG   = 10     # -> ~2.7k class 2
N_SYNTH_NOISE    = 4000   # synthetic noise clips  (class 0)
N_AUG_REAL_NOISE = 3500   # augmented real-ambient clips (class 0)

POS_TXT = ["hey pip", "hey, pip", "hey pip!", "hey  pip", "hey pip."]
POS_HE  = ["היי פיפ", "היי פיפ."]
# Hard negatives: near-misses first (sampled with priority), then a broad word list.
NEG_HARD = ["hey", "pip", "pip pip", "hey kid", "hey pick", "hey pin", "hey peter",
            "hey ship", "hey big", "peep", "pippa", "pippi", "philip", "pizza", "hippo"]
NEG_TXT = NEG_HARD + ["pick", "pin", "peter", "people", "puppy", "happy", "hello", "hi",
            "okay", "stop", "go", "yes", "no", "please", "again", "help", "what", "why",
            "how", "good", "ready", "robot", "listen", "math", "english", "number",
            "answer", "question", "apple", "banana", "water", "music", "one", "two",
            "three", "four", "five", "six", "seven", "eight", "nine", "ten", "name",
            "play", "next", "door", "phone", "paper", "purple", "puzzle"]

# espeak fallback / extra diversity voices (same families as gen_data.py)
ESPK_BASES = ['en-us', 'en', 'en-029', 'en-gb-scotland', 'en-gb-x-rp',
              'en-gb-x-gbclan', 'en-gb-x-gbcwmd', 'en-us-nyc']
ESPK_VARS = ["", "+m1", "+m2", "+m3", "+m4", "+m5", "+m6", "+m7", "+f1", "+f2", "+f3", "+f4"]


def log(m):
    print("[build] %s" % m, flush=True)


# ---- synthetic noise (reimplemented from gen_data; importing it would run it) ----
def _pink(n):
    w = np.random.randn(n)
    X = np.fft.rfft(w)
    f = np.arange(len(X)) + 1
    return np.fft.irfft(X / np.sqrt(f), n).astype(np.float32)


def _brown(n):
    return np.cumsum(np.random.randn(n)).astype(np.float32)


def _hum(n):
    t = np.arange(n) / SR
    f = np.random.choice([50, 60])
    return (np.sin(2 * np.pi * f * t) + 0.4 * np.sin(2 * np.pi * 2 * f * t)).astype(np.float32)


def rnoise(n):
    k = np.random.rand()
    x = (np.random.randn(n).astype(np.float32) if k < 0.4 else
         _pink(n) if k < 0.7 else _brown(n) if k < 0.85 else _hum(n))
    return x / (np.max(np.abs(x)) + 1e-9)


# ---- pitch shifter (librosa) to simulate kids/women over adult recordings ----
try:
    import librosa

    def pitch_shifter(u):
        steps = np.random.uniform(-3, 5)   # biased upward -> kids/women
        return librosa.effects.pitch_shift(u, sr=SR, n_steps=steps).astype(np.float32)
except Exception as e:
    log("librosa pitch_shift unavailable: %r" % e)
    pitch_shifter = None


def _erate():
    return "%+d%%" % np.random.randint(-15, 21)


def _epitch():
    return "%+dHz" % np.random.randint(-10, 31)


def main():
    t0 = time.time()
    for f in glob.glob(OUT + "/X_*.npy") + glob.glob(OUT + "/Y_*.npy"):
        os.remove(f)

    # ===== synthetic noise pool (mixed into speech) =====
    synth_pool = [rnoise(CLIP) * np.random.uniform(0.05, 0.7) for _ in range(400)]

    # ===== 1-3) REAL DATA (the anchor) =====
    log("collecting real data from %s ..." % WW_DIR)
    Xr, Yr, real_noise = real_data.collect(WW_DIR, synth_pool, pitch_shifter,
                                            aug_per_pos=AUG_PER_POS, aug_per_neg=AUG_PER_NEG)
    log("real: X=%s classes=%s  %.0fs" % (Xr.shape, np.bincount(Yr).tolist(), time.time() - t0))
    noise_pool = synth_pool + real_noise   # real room noise now mixes into TTS speech too

    Xp, Yp = [list(Xr[Yr == c]) for c in (0, 1, 2)], None
    Xc0, Xc1, Xc2 = Xp[0], Xp[1], Xp[2]

    # ===== TTS setup =====
    use_edge = tts_edge.available()
    log("edge-tts available: %s" % use_edge)
    from tts import say as espeak_say   # local espeak (also used as fallback)

    def edge_or_espeak_pos():
        """One clean 'hey pip' utterance (trimmed, peak-normalized)."""
        if use_edge:
            v = np.random.choice(tts_edge.VOICES)
            txt = np.random.choice(POS_HE if v.startswith("he-") and np.random.rand() < 0.5 else POS_TXT)
            try:
                a = tts_edge.say_edge(str(txt), v, rate=_erate(), pitch=_epitch())
            except Exception:
                a = espeak_say(np.random.choice(POS_TXT), str(np.random.choice(ESPK_BASES)) + str(np.random.choice(ESPK_VARS)))
        else:
            a = espeak_say(np.random.choice(POS_TXT), str(np.random.choice(ESPK_BASES)) + str(np.random.choice(ESPK_VARS)),
                           rate=np.random.randint(140, 205), pitch=np.random.randint(28, 78))
        a = real_data.extract_speech(a.astype(np.float32))
        return a / (np.max(np.abs(a)) + 1e-9) if len(a) else None

    # ===== 4) NEURAL-TTS positives -> class 1 =====
    n_pos_clips = (len(tts_edge.VOICES) * TTS_POS_PER_VOICE) if use_edge else 80
    ok = 0
    for i in range(n_pos_clips):
        u = edge_or_espeak_pos()
        if u is None:
            continue
        ok += 1
        for _ in range(AUG_TTS_POS):
            Xc1.append(feat.logmel(augment.augment_speech(u, noise_pool, pitch_shifter)))
        if i % 20 == 0:
            log("tts pos %d/%d  %.0fs" % (i, n_pos_clips, time.time() - t0))
    log("tts positives: %d clips synthesized" % ok)

    # ===== 5a) edge-tts hard negatives -> class 2 =====
    if use_edge:
        pairs = [(w, np.random.choice(tts_edge.VOICES)) for w in NEG_HARD] * 3
        while len(pairs) < N_EDGE_NEG:
            pairs.append((np.random.choice(NEG_TXT), np.random.choice(tts_edge.VOICES)))
        for i, (w, v) in enumerate(pairs[:N_EDGE_NEG]):
            try:
                a = tts_edge.say_edge(str(w), str(v), rate=_erate(), pitch=_epitch())
            except Exception:
                continue
            a = real_data.extract_speech(a.astype(np.float32))
            if not len(a):
                continue
            u = a / (np.max(np.abs(a)) + 1e-9)
            for _ in range(AUG_TTS_NEG):
                Xc2.append(feat.logmel(augment.augment_speech(u, noise_pool, pitch_shifter)))
            if i % 30 == 0:
                log("edge neg %d/%d  %.0fs" % (i, N_EDGE_NEG, time.time() - t0))

    # ===== 5b) espeak hard negatives -> class 2 (cheap, local, extra diversity) =====
    for wi, w in enumerate(NEG_TXT):
        for _ in range(ESPEAK_NEG_VOICES):
            v = str(np.random.choice(ESPK_BASES)) + str(np.random.choice(ESPK_VARS))
            try:
                a = espeak_say(w, v, rate=np.random.randint(140, 205), pitch=np.random.randint(28, 78))
            except Exception:
                continue
            a = real_data.extract_speech(a.astype(np.float32))
            if not len(a):
                continue
            u = a / (np.max(np.abs(a)) + 1e-9)
            for _ in range(AUG_ESPEAK_NEG):
                Xc2.append(feat.logmel(augment.augment_speech(u, noise_pool, pitch_shifter)))
    log("class2 after TTS negatives: %d  %.0fs" % (len(Xc2), time.time() - t0))

    # ===== 6) extra noise -> class 0 =====
    for _ in range(N_SYNTH_NOISE):
        Xc0.append(feat.logmel(rnoise(CLIP) * np.random.uniform(0.05, 0.7)))
    # augmented variants of REAL ambient (level + mixed with another noise)
    if real_noise:
        for _ in range(N_AUG_REAL_NOISE):
            base = real_noise[np.random.randint(len(real_noise))].copy()
            base = base / (np.max(np.abs(base)) + 1e-9) * np.random.uniform(0.15, 0.95)
            if np.random.rand() < 0.5:
                base = base + rnoise(CLIP) * np.random.uniform(0.02, 0.3)
            m = np.max(np.abs(base))
            if m > 1:
                base = base / m
            Xc0.append(feat.logmel(base.astype(np.float32)))
    log("class0 noise total: %d  %.0fs" % (len(Xc0), time.time() - t0))

    # ===== assemble + save per-class chunks =====
    for tag, lst, lab in [("c0", Xc0, 0), ("c1", Xc1, 1), ("c2", Xc2, 2)]:
        X = np.asarray(lst, np.float32)
        Y = np.full(len(lst), lab, np.int64)
        np.save("%s/X_%s.npy" % (OUT, tag), X)
        np.save("%s/Y_%s.npy" % (OUT, tag), Y)
        log("saved %s: %s" % (tag, X.shape))
    np.savez(OUT + "/melfb.npz", fb=feat.MEL_FB, hann=feat.HANN)

    total = len(Xc0) + len(Xc1) + len(Xc2)
    log("DONE total=%d  class[0/1/2]=%d/%d/%d  %.0fs"
        % (total, len(Xc0), len(Xc1), len(Xc2), time.time() - t0))


if __name__ == "__main__":
    main()
