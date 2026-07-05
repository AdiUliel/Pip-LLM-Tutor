# build_dataset.py — ONE command that assembles the retraining dataset from
# (a) real device recordings, (b) neural-TTS voices, (c) synthetic noise, per
# RETRAIN_RUNBOOK.md §5 (Approach B). Writes /tmp/ww/X_c9*.npy shards in the
# exact format the existing train.py globs ('/tmp/ww/X_c*.npy').
#
#   python3 build_dataset.py --ww ~/ww              # full: real + edge-TTS + synth
#   python3 build_dataset.py --ww ~/ww --no-tts     # offline: real + synth only
#
# Then: python3 train.py && python3 export_c.py && python3 parity.py
import argparse, glob, os, sys
import numpy as np
import feat, augment, real_data

ap = argparse.ArgumentParser()
ap.add_argument("--ww", required=True, help="path to the /ww folder copied off the SD card")
ap.add_argument("--out", default="/tmp/ww")
ap.add_argument("--seed", type=int, default=7)
ap.add_argument("--aug-per-pos", type=int, default=40, help="augmented copies per real 'hey pip'")
ap.add_argument("--aug-per-neg", type=int, default=20, help="augmented copies per real short negative")
ap.add_argument("--no-tts", action="store_true", help="skip edge-tts (no internet/ffmpeg needed)")
ap.add_argument("--tts-pos-utts", type=int, default=250, help="unique TTS 'hey pip' syntheses")
ap.add_argument("--tts-neg-utts", type=int, default=350, help="unique TTS hard-negative syntheses")
ap.add_argument("--tts-aug", type=int, default=12, help="augmented copies per TTS utterance")
ap.add_argument("--synth-noise", type=int, default=3000, help="synthetic class-0 clips")
args = ap.parse_args()

np.random.seed(args.seed)
rng = np.random.default_rng(args.seed)
os.makedirs(args.out, exist_ok=True)

# Warn about shards already present — train.py will pick those up too.
old = sorted(glob.glob(os.path.join(args.out, "X_c*.npy")))
if old:
    print("NOTE: existing shards in %s will ALSO be used by train.py:" % args.out)
    for f in old: print("  ", f)
    print("Delete them first if you want a real-data-only / fresh dataset.\n")

def save_shard(tag, X, Y):
    np.save(os.path.join(args.out, "X_%s.npy" % tag), X)
    np.save(os.path.join(args.out, "Y_%s.npy" % tag), Y)
    print("shard %s: X=%s  classes=%s" % (tag, X.shape, np.bincount(Y, minlength=3).tolist()))

# ── 1. Real recordings (ambient sliced first → real noise feeds every mix) ────
pool = augment.make_noise_pool(400)                      # synthetic base pool
Xr, Yr, real_noise = real_data.collect(args.ww, pool,
                                       aug_per_pos=args.aug_per_pos,
                                       aug_per_neg=args.aug_per_neg)
if len(Xr) == 0:
    sys.exit("No real recordings found under %s — check the folder layout (ww/pos, ww/neg)." % args.ww)
save_shard("c90", Xr, Yr)
pool += real_noise                                       # TTS gets real room noise too

# ── 2. Neural-TTS positives + hard negatives (optional) ───────────────────────
if not args.no_tts:
    import tts_edge
    X=[]; Y=[]; fails=0
    for texts, label, n_utts in ((tts_edge.POS_TEXTS, 1, args.tts_pos_utts),
                                 (tts_edge.NEG_TEXTS, 2, args.tts_neg_utts)):
        for i in range(n_utts):
            voice, rate, pitch = tts_edge.rand_style(rng)
            text = str(rng.choice(texts))
            # Hebrew texts need a Hebrew voice to synthesize correctly.
            if any("֐" <= ch <= "ת" for ch in text):
                voice = str(rng.choice(tts_edge.HE))
            try:
                u = tts_edge.say_edge(text, voice, rate, pitch)
            except Exception as e:
                fails += 1
                if fails <= 3: print("tts fail (%s): %s" % (text, e))
                continue
            for _ in range(args.tts_aug):
                X.append(feat.logmel(augment.augment_speech(u, pool))); Y.append(label)
            if i % 50 == 0: print("tts label %d: %d/%d" % (label, i, n_utts))
    if fails: print("tts: %d syntheses failed (skipped)" % fails)
    if X: save_shard("c91", np.array(X, np.float32), np.array(Y, np.int64))
else:
    print("--no-tts: skipping neural-TTS shard (lean on real + synthetic)")

# ── 3. Synthetic noise floor (class 0, same recipe as gen_data.py) ────────────
X=[]; Y=[]
for _ in range(args.synth_noise):
    X.append(feat.logmel(augment.rnoise(feat.CLIP)*np.random.uniform(0.05,0.6))); Y.append(0)
save_shard("c92", np.array(X, np.float32), np.array(Y, np.int64))

np.savez(os.path.join(args.out, "melfb.npz"), fb=feat.MEL_FB, hann=feat.HANN)
print("\nDone. Next:  python3 train.py  &&  python3 export_c.py  &&  python3 parity.py")
print("Target (runbook §6): val acc >= 0.97, ~0 false-fires at threshold 0.7-0.8.")
