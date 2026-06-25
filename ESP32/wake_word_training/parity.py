import os,subprocess,numpy as np,feat,tensorflow as tf
from tts import say
os.environ['TF_CPP_MIN_LOG_LEVEL']='3'
m=tf.keras.models.load_model('/tmp/ww/model.keras')
nz=np.load('norm.npz'); MEAN=float(nz['mean']); STD=float(nz['std'])
np.random.seed(1)
def raw_clip(text,voice,rate=170,pitch=50,snr=20,off=2000):
    u=say(text,voice,rate,pitch).astype(np.float32)
    u=u/(np.max(np.abs(u))+1e-9)*0.7
    buf=np.zeros(feat.CLIP,np.float32); off=min(off,feat.CLIP-len(u)-1); buf[off:off+len(u)]+=u
    n=np.random.randn(feat.CLIP).astype(np.float32); n/= (np.sqrt(np.mean(n*n))+1e-9)
    buf=buf+n*(np.sqrt(np.mean(buf*buf))/(10**(snr/20)))
    buf=np.clip(buf,-1,1)
    return (buf*32767).astype(np.int16)
tests=[("hey pip","en-us+f3"),("hey pip","en-us+m4"),("hey pip","en-gb-x-rp+f2"),
       ("hey pip","en-029+m2"),("pip","en-us+m1"),("hey","en-us+f1"),
       ("pizza","en-us+m3"),("hello","en-gb-scotland"),("seven","en-us+f4"),("apple","en-us+m6")]
maxdiff=0.0
print("  text/voice            | py(noise,hp,unk)        | C(noise,hp,unk)         | dHP")
for i,(t,v) in enumerate(tests):
    pcm=raw_clip(t,v,snr=np.random.uniform(12,25),off=np.random.randint(500,5000))
    pcm.tofile("/tmp/ww/out/clip.raw")
    sig=pcm.astype(np.float32)/32768.0
    fe=feat.logmel(sig); fe=((fe-MEAN)/STD)[None,...,None]
    pp=m.predict(fe,verbose=0)[0]
    cout=subprocess.run(["./out/host_test","/tmp/ww/out/clip.raw"],capture_output=True,text=True).stdout.split()
    cp=[float(x) for x in cout]
    d=abs(pp[1]-cp[1]); maxdiff=max(maxdiff,max(abs(pp[j]-cp[j]) for j in range(3)))
    print(" %-20s | %.4f %.4f %.4f | %.4f %.4f %.4f | %.4f"%(t+"/"+v,pp[0],pp[1],pp[2],cp[0],cp[1],cp[2],d))
print("MAX ABS DIFF py-vs-C across all classes/clips: %.6f"%maxdiff)
print("PARITY:", "PASS (<1e-3)" if maxdiff<1e-3 else "CHECK")
