# real_data.py — load the on-device /ww recordings (the anchor of the dataset) and
# turn them into (log-mel, label) pairs. Class order: 0=noise, 1=hey-pip, 2=other.
#
# Recordings are quiet (device mic peak ~0.05) and the positives are ~1.8 s while the
# model window is 1 s, so each utterance is peak-normalized and silence-trimmed to the
# actual speech before being placed at a random offset in the 1 s clip. The long
# ambient files are sliced into overlapping 1 s windows (class 0) and ALSO returned as
# a real-noise pool so synthetic/real speech can be mixed against true room noise.
import glob
import numpy as np
import soundfile as sf
import feat
import augment


def _load(p):
    a, sr = sf.read(p, dtype="float32")
    a = a if a.ndim == 1 else a[:, 0]
    assert sr == feat.SR, "%s: sr=%d (expected %d)" % (p, sr, feat.SR)
    return a


def _frame_rms(a, win=feat.FRAME_LEN, hop=feat.FRAME_STEP):
    if len(a) < win:
        return np.array([np.sqrt(np.mean(a * a) + 1e-12)], np.float32)
    fr = np.lib.stride_tricks.sliding_window_view(a, win)[::hop]
    return np.sqrt(np.mean(fr * fr, axis=1) + 1e-12)


def extract_speech(a, lo=0.02, hi=0.96, max_len=15200, pad=320):
    """Isolate the actual spoken phrase via a cumulative-energy span (drops the long
    quiet tail the capture leaves behind). The capped length (~0.95 s) leaves room for
    random time-shifting inside the 1 s window — important for the WW_CONSEC debounce."""
    a = a.astype(np.float32)
    if len(a) < 1600:
        return a
    e = _frame_rms(a) ** 2
    if e.sum() < 1e-9:
        return a
    c = np.cumsum(e) / e.sum()
    hop = feat.FRAME_STEP
    s = int(np.searchsorted(c, lo))
    t = int(np.searchsorted(c, hi))
    start = max(0, s * hop - pad)
    end = min(len(a), t * hop + feat.FRAME_LEN + pad)
    seg = a[start:end]
    if len(seg) > max_len:
        seg = seg[:max_len]
    return seg if len(seg) >= 1600 else a


def windows_1s(a, hop=8000):
    """Slice a long clip into overlapping 1 s windows (hop 0.5 s by default)."""
    out = []
    for s in range(0, max(1, len(a) - feat.CLIP + 1), hop):
        out.append(a[s:s + feat.CLIP])
    if len(a) < feat.CLIP:
        out.append(np.pad(a, (0, feat.CLIP - len(a))))
    return out


def collect(ww_dir, noise_pool, pitch_shifter=None, aug_per_pos=45, aug_per_neg=30):
    """Returns (X[N,98,40] float32, Y[N] int64, real_noise[list of 1 s float32])."""
    X, Y, real_noise = [], [], []

    # class 0: long ambient -> sliced 1 s windows; also feeds the real-noise pool.
    for p in sorted(glob.glob("%s/neg/c*.wav" % ww_dir)):
        for w in windows_1s(_load(p)):
            w = w.astype(np.float32)
            wn = w / (np.max(np.abs(w)) + 1e-9) * np.random.uniform(0.2, 0.9)
            real_noise.append(wn)
            X.append(feat.logmel(wn))
            Y.append(0)

    # Mix synthetic + real room noise into every augmented speech clip.
    pool = list(noise_pool) + real_noise

    # class 1: real "hey pip", phrase-isolated + heavily augmented (the anchor).
    for p in sorted(glob.glob("%s/pos/p*.wav" % ww_dir)):
        u = extract_speech(_load(p))
        u = u / (np.max(np.abs(u)) + 1e-9)
        for _ in range(aug_per_pos):
            X.append(feat.logmel(augment.augment_speech(u, pool, pitch_shifter)))
            Y.append(1)

    # class 2: real other / non-wake speech.
    for p in sorted(glob.glob("%s/neg/n*.wav" % ww_dir)):
        u = extract_speech(_load(p))
        u = u / (np.max(np.abs(u)) + 1e-9)
        for _ in range(aug_per_neg):
            X.append(feat.logmel(augment.augment_speech(u, pool, pitch_shifter)))
            Y.append(2)

    return np.array(X, np.float32), np.array(Y, np.int64), real_noise
