# augment.py — shared augmentation for real + TTS training clips.
# Materialized (and smoke-tested) from RETRAIN_RUNBOOK.md §5b. Every speech clip
# is level-jittered to peak U[0.25,1.0] — bracketing the device's runtime
# peak-normalization to ~0.60 (wake_word.h WW_TARGET_PEAK), per §1.5.
import numpy as np
from scipy.signal import fftconvolve, butter, lfilter
import feat

SR, CLIP = feat.SR, feat.CLIP

def rms(x): return float(np.sqrt(np.mean(x*x))+1e-12)

# ── Synthetic room noises — same family gen_data.py trains class 0 on ─────────
def _pink(n):
    w=np.random.randn(n); X=np.fft.rfft(w); f=np.arange(len(X))+1
    return np.fft.irfft(X/np.sqrt(f),n).astype(np.float32)
def _brown(n): return np.cumsum(np.random.randn(n)).astype(np.float32)
def _hum(n):
    t=np.arange(n)/SR; f=np.random.choice([50,60])
    return (np.sin(2*np.pi*f*t)+0.4*np.sin(2*np.pi*2*f*t)).astype(np.float32)
def rnoise(n):
    k=np.random.rand()
    x=np.random.randn(n).astype(np.float32) if k<0.4 else (_pink(n) if k<0.7 else (_brown(n) if k<0.85 else _hum(n)))
    return x/(np.max(np.abs(x))+1e-9)
def make_noise_pool(count):
    """Synthetic 1 s noises; extend with real ambient windows from real_data.collect()."""
    return [rnoise(CLIP) for _ in range(count)]

# ── Speech augmentation ────────────────────────────────────────────────────────
def _rir():
    L=np.random.randint(200,1600); ir=np.exp(-np.arange(L)/np.random.uniform(40,300))
    ir*=(np.random.rand(L)<0.5); ir[0]=1.0; return ir.astype(np.float32)

def _bandlimit(x):  # mimic the covered mic / small-speaker frequency response
    lo=np.random.uniform(3000,7000); b,a=butter(4,lo/(SR/2),btype="low"); x=lfilter(b,a,x)
    if np.random.rand()<0.5: b,a=butter(2,120/(SR/2),btype="high"); x=lfilter(b,a,x)
    return x.astype(np.float32)

def _varispeed(u):  # speed+pitch together (cheap; simulates kids/adults)
    f=np.random.uniform(0.9,1.1); n=max(8,int(len(u)/f))
    return np.interp(np.linspace(0,len(u)-1,n),np.arange(len(u)),u).astype(np.float32)

def place_in_clip(u):  # random offset into the 1 s window
    u=u[:CLIP-100] if len(u)>CLIP-100 else u
    buf=np.zeros(CLIP,np.float32); off=np.random.randint(0,max(1,CLIP-len(u))); buf[off:off+len(u)]+=u
    return buf

def augment_speech(u, noise_pool):
    """One clean utterance (float32 @16 kHz) → one augmented 1 s clip."""
    s=u.astype(np.float32).copy()
    if np.random.rand()<0.5:  s=_varispeed(s)
    if np.random.rand()<0.35: s=fftconvolve(s,_rir())[:len(s)+200]
    s=_bandlimit(s)
    s=s/(np.max(np.abs(s))+1e-9)*np.random.uniform(0.25,1.0)   # level — matches device (§1.5)
    buf=place_in_clip(s)
    n=noise_pool[np.random.randint(len(noise_pool))]           # real OR synthetic 1 s noise
    snr=np.random.uniform(3,28)
    buf=buf+n*(rms(buf)/(10**(snr/20))/(rms(n)+1e-9))
    m=np.max(np.abs(buf))
    return buf/m if m>1 else buf
