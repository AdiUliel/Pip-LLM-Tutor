import numpy as np, feat, sys, time
from tts import say
import sys as _s
np.random.seed(int(_s.argv[1]))
CLIP=feat.CLIP
BASES=['en-us','en','en-029','en-gb-scotland','en-gb-x-rp','en-gb-x-gbclan','en-gb-x-gbcwmd','en-us-nyc','gmw/en-US']
VARS=["","+m1","+m2","+m3","+m4","+m5","+m6","+m7","+f1","+f2","+f3","+f4"]
def rvoice(): return str(np.random.choice(BASES))+str(np.random.choice(VARS))
def rms(x): return float(np.sqrt(np.mean(x*x))+1e-12)
def pink(n):
    w=np.random.randn(n); X=np.fft.rfft(w); f=np.arange(len(X))+1
    return np.fft.irfft(X/np.sqrt(f),n).astype(np.float32)
def brown(n): return np.cumsum(np.random.randn(n)).astype(np.float32)
def hum(n):
    t=np.arange(n)/feat.SR; f=np.random.choice([50,60])
    return (np.sin(2*np.pi*f*t)+0.4*np.sin(2*np.pi*2*f*t)).astype(np.float32)
def rnoise(n):
    k=np.random.rand()
    x=np.random.randn(n).astype(np.float32) if k<0.4 else (pink(n) if k<0.7 else (brown(n) if k<0.85 else hum(n)))
    return x/(np.max(np.abs(x))+1e-9)
def reverb_ir():
    L=np.random.randint(200,1600); ir=np.exp(-np.arange(L)/np.random.uniform(40,300))
    ir*=(np.random.rand(L)<0.5); ir[0]=1.0; return ir.astype(np.float32)
def make(u,label):
    s=u.astype(np.float32).copy()
    if len(s)<10: return None
    if np.random.rand()<0.35:
        ir=reverb_ir(); s=np.convolve(s,ir)[:len(s)+len(ir)]
    s=s/(np.max(np.abs(s))+1e-9)*np.random.uniform(0.25,1.0)
    if len(s)>CLIP-100: s=s[:CLIP-100]
    buf=np.zeros(CLIP,np.float32)
    maxoff=max(1,CLIP-len(s)); off=np.random.randint(0,maxoff)
    buf[off:off+len(s)]+=s
    if label==0:
        buf=rnoise(CLIP)*np.random.uniform(0.05,0.6)
    else:
        snr=np.random.uniform(5,28); n=rnoise(CLIP); n=n/rms(n)
        buf=buf+n*(rms(buf)/(10**(snr/20)))
        if np.random.rand()<0.3: buf+=np.random.uniform(0.01,0.08)*rnoise(CLIP)
    m=np.max(np.abs(buf))
    if m>1: buf=buf/m
    return feat.logmel(buf)
POS=["hey pip","hey pip.","hey, pip","hey pip!","hey  pip","hey pip?","hey pip"]
NEG=["hey","pip","pip pip","hey there","hey pick","hey pin","hey peter","hey ship","hey big","hey kid",
 "pick","pin","peep","peter","pippa","pippi","philip","people","puppy","pizza","hippo","happy","hello","hi",
 "okay","stop","go","yes","no","please","again","help","what","why","how","good","ready","robot","listen",
 "math","english","number","answer","question","apple","banana","water","music","one","two","three","four",
 "five","six","seven","eight","nine","ten","name","play","next","door","phone","paper","purple","puzzle"]
NPOS=int(sys.argv[2]);NNEG=int(sys.argv[3]);NNOISE=int(sys.argv[4]);TAG=sys.argv[5]
X=[];Y=[];t0=time.time()
def log(m): open("/tmp/ww/gen_%s.log"%TAG,"a").write(m+"\n")
open("/tmp/ww/gen_%s.log"%TAG,"w").write("start\n")
for i in range(NPOS):
    u=say(np.random.choice(POS),rvoice(),np.random.randint(140,205),np.random.randint(28,78))
    f=make(u,1)
    if f is not None: X.append(f);Y.append(1)
    if i%400==0: log("pos %d/%d %.0fs"%(i,NPOS,time.time()-t0))
for i in range(NNEG):
    u=say(np.random.choice(NEG),rvoice(),np.random.randint(140,205),np.random.randint(28,78))
    f=make(u,2)
    if f is not None: X.append(f);Y.append(2)
    if i%400==0: log("neg %d/%d %.0fs"%(i,NNEG,time.time()-t0))
for i in range(NNOISE):
    f=make(np.zeros(8000,np.float32),0)
    if f is not None: X.append(f);Y.append(0)
X=np.array(X,np.float32);Y=np.array(Y,np.int64)
np.save("/tmp/ww/X_%s.npy"%TAG,X);np.save("/tmp/ww/Y_%s.npy"%TAG,Y)
np.savez("/tmp/ww/melfb.npz",fb=feat.MEL_FB,hann=feat.HANN)
log("DONE X=%s Y=%s %.0fs"%(X.shape,np.bincount(Y).tolist(),time.time()-t0))
