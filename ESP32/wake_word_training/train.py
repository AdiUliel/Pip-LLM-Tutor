import os,glob,numpy as np
os.environ['TF_CPP_MIN_LOG_LEVEL']='3'
os.environ.setdefault('TF_ENABLE_ONEDNN_OPTS','0')   # deterministic math -> clean C parity
import tensorflow as tf
from tensorflow.keras import layers, models

# ---- channel counts (export writes WW_C1/2/3_OUT; topology/kernel/stride unchanged) ----
C1,C2,C3 = 32,48,64

class SpecAugment(layers.Layer):
    """Training-only SpecAugment: one freq + one time mask (->0 == feature mean after
    normalization). Identity at inference and carries NO weights, so it never reaches
    the exported header. The saved model.keras strips it (see below), so export_c.py /
    parity.py can load_model() without any custom_objects. Kept light — the waveform
    augmentation already does the heavy lifting; heavier masking erased the fine
    'hey pip' vs near-miss distinction."""
    def __init__(self, n_freq=40, n_time=98, f_max=6, t_max=12, **kw):
        super().__init__(**kw)
        self.n_freq,self.n_time,self.f_max,self.t_max=n_freq,n_time,f_max,t_max
    def call(self, x, training=None):
        if training in (False, None):
            return x
        def fmask(z):
            f=tf.random.uniform([],1,self.f_max+1,tf.int32)
            f0=tf.random.uniform([],0,self.n_freq-f+1,tf.int32)
            r=tf.range(self.n_freq)
            m=tf.cast((r<f0)|(r>=f0+f),z.dtype)
            return z*tf.reshape(m,[1,1,self.n_freq,1])
        def tmask(z):
            t=tf.random.uniform([],1,self.t_max+1,tf.int32)
            t0=tf.random.uniform([],0,self.n_time-t+1,tf.int32)
            r=tf.range(self.n_time)
            m=tf.cast((r<t0)|(r>=t0+t),z.dtype)
            return z*tf.reshape(m,[1,self.n_time,1,1])
        x=fmask(x); x=tmask(x)
        return x

def build(spec_augment):
    inp=layers.Input((98,40,1))
    x=SpecAugment()(inp) if spec_augment else inp
    x=layers.Conv2D(C1,3,strides=2,padding='valid',activation='relu')(x)
    x=layers.Conv2D(C2,3,strides=2,padding='valid',activation='relu')(x)
    x=layers.Conv2D(C3,3,strides=2,padding='valid',activation='relu')(x)
    x=layers.GlobalAveragePooling2D()(x)
    if spec_augment: x=layers.Dropout(0.15)(x)
    out=layers.Dense(3,activation='softmax')(x)
    return models.Model(inp,out)

X=np.concatenate([np.load(f) for f in sorted(glob.glob('/tmp/ww/X_c*.npy'))])
Y=np.concatenate([np.load(f) for f in sorted(glob.glob('/tmp/ww/Y_c*.npy'))])
rng=np.random.default_rng(0); idx=rng.permutation(len(X)); X=X[idx]; Y=Y[idx]
nv=len(X)//6
Xv,Yv=X[:nv],Y[:nv]; Xt,Yt=X[nv:],Y[nv:]
MEAN=float(Xt.mean()); STD=float(Xt.std()+1e-6)
Xtn=((Xt-MEAN)/STD)[...,None]; Xvn=((Xv-MEAN)/STD)[...,None]
print("train",Xt.shape,"val",Xv.shape,"classes",np.bincount(Y).tolist(),"MEAN %.3f STD %.3f"%(MEAN,STD))

mt=build(spec_augment=True)   # training model (SpecAugment + Dropout)
mt.compile('adam','sparse_categorical_crossentropy',metrics=['accuracy'])
if os.path.exists('/tmp/ww/ckpt.weights.h5'):
    try: mt.load_weights('/tmp/ww/ckpt.weights.h5'); print('resumed')
    except Exception as e: print('no resume',e)
mt.fit(Xtn,Yt,validation_data=(Xvn,Yv),epochs=55,batch_size=128,class_weight={0:1.,1:2.,2:1.},verbose=2,
      callbacks=[tf.keras.callbacks.ReduceLROnPlateau(patience=4,factor=0.5,min_lr=1e-4)])
mt.save_weights('/tmp/ww/ckpt.weights.h5')

# Strip SpecAugment/Dropout into a clean inference model and copy the conv/dense weights,
# so the saved artifact has only standard layers (loads without custom_objects).
me=build(spec_augment=False)
src=[l for l in mt.layers if l.get_weights()]
dst=[l for l in me.layers if l.get_weights()]
for s,d in zip(src,dst): d.set_weights(s.get_weights())
me.save('/tmp/ww/model.keras')

p=me.predict(Xvn,verbose=0); pred=p.argmax(1)
print("val acc %.3f"%((pred==Yv).mean()))
import numpy as _n
cm=_n.zeros((3,3),int)
for a,b in zip(Yv,pred): cm[a,b]+=1
print("confusion (rows=true 0/1/2):\n",cm)
hp=(Yv==1)
for thr in (0.5,0.7,0.8,0.9):
    det=p[:,1]>=thr
    prec=(det&hp).sum()/max(1,det.sum()); rec=(det&hp).sum()/max(1,hp.sum())
    print("thr %.2f  hey_pip prec %.3f rec %.3f  (false-fires %d)"%(thr,prec,rec,(det&~hp).sum()))
np.savez('/tmp/ww/norm.npz',mean=MEAN,std=STD)
