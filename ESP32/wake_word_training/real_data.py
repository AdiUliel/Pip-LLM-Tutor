# real_data.py — load the device-captured SD recordings and turn them into
# training features. Materialized (and smoke-tested) from RETRAIN_RUNBOOK.md §5c.
#
# Expects the /ww folder copied off the SD card (see ww_capture.h):
#   ww/pos/pNNNN.wav   "hey pip" utterances          → label 1 (augmented hard)
#   ww/neg/nNNNN.wav   short non-wake speech         → label 2
#   ww/neg/cNNNN.wav   long continuous ambient/noise → label 0 (sliced to 1 s)
# All 16 kHz mono 16-bit WAV. The ambient windows are ALSO returned as a noise
# pool so synthetic speech gets mixed with the REAL room's noise.
import glob, numpy as np, feat, augment

def _load(path):
    """Read a 16 kHz mono WAV → float32 [-1,1]. Uses soundfile when available,
    otherwise falls back to the stdlib wave module (the SD captures are plain
    16-bit PCM, so both paths are exact)."""
    try:
        import soundfile as sf
        a, sr = sf.read(path, dtype="float32")
        if a.ndim > 1: a = a[:, 0]
    except ImportError:
        import wave
        with wave.open(path, "rb") as w:
            assert w.getsampwidth() == 2 and w.getnchannels() == 1, path
            sr = w.getframerate()
            a = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16
                              ).astype(np.float32) / 32768.0
    assert sr == feat.SR, "%s: %d Hz (expected %d)" % (path, sr, feat.SR)
    return a

def windows_1s(a, hop=8000):
    """Slice a long clip into overlapping 1 s windows (hop 0.5 s)."""
    out=[]
    for s in range(0, max(1, len(a)-feat.CLIP+1), hop):
        out.append(a[s:s+feat.CLIP])
    if len(a) < feat.CLIP:
        out.append(np.pad(a, (0, feat.CLIP-len(a))))
    return out

def collect(ww_dir, noise_pool, aug_per_pos=40, aug_per_neg=20):
    """→ (X features, Y labels, real_noise windows). The ambient recordings are
    sliced FIRST so the real room's noise is already in the mixing pool when the
    real positives/negatives get augmented; extend your own pool with the
    returned real_noise before augmenting TTS clips too."""
    X=[]; Y=[]
    pos = sorted(glob.glob("%s/pos/p*.wav" % ww_dir))
    neg = sorted(glob.glob("%s/neg/n*.wav" % ww_dir))
    amb = sorted(glob.glob("%s/neg/c*.wav" % ww_dir))
    print("real: %d pos, %d neg, %d ambient files" % (len(pos), len(neg), len(amb)))

    real_noise=[]
    for p in amb:                                    # label 0 — real room ambient
        for w in windows_1s(_load(p)):
            real_noise.append(w/(np.max(np.abs(w))+1e-9)*np.random.uniform(0.2,0.9))
            X.append(feat.logmel(w)); Y.append(0)

    pool = list(noise_pool) + real_noise             # augment speech with REAL room noise too

    for p in pos:                                    # label 1 — the anchor; augment hard
        u=_load(p); u=u/(np.max(np.abs(u))+1e-9)
        for _ in range(aug_per_pos):
            X.append(feat.logmel(augment.augment_speech(u, pool))); Y.append(1)

    for p in neg:                                    # label 2 — other speech
        u=_load(p); u=u/(np.max(np.abs(u))+1e-9)
        for _ in range(aug_per_neg):
            X.append(feat.logmel(augment.augment_speech(u, pool))); Y.append(2)

    return np.array(X,np.float32), np.array(Y,np.int64), real_noise
