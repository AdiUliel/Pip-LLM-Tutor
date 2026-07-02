import os,glob,numpy as np
os.environ['TF_CPP_MIN_LOG_LEVEL']='3'
import tensorflow as tf
from tensorflow.keras import layers, models
X=np.concatenate([np.load(f) for f in sorted(glob.glob('/tmp/ww/X_c*.npy'))])
Y=np.concatenate([np.load(f) for f in sorted(glob.glob('/tmp/ww/Y_c*.npy'))])
rng=np.random.default_rng(0); idx=rng.permutation(len(X)); X=X[idx]; Y=Y[idx]
nv=len(X)//6
Xv,Yv=X[:nv],Y[:nv]; Xt,Yt=X[nv:],Y[nv:]
MEAN=float(Xt.mean()); STD=float(Xt.std()+1e-6)
Xtn=((Xt-MEAN)/STD)[...,None]; Xvn=((Xv-MEAN)/STD)[...,None]
print("train",Xt.shape,"val",Xv.shape,"MEAN %.3f STD %.3f"%(MEAN,STD))
inp=layers.Input((98,40,1))
x=layers.Conv2D(16,3,strides=2,padding='valid',activation='relu')(inp)
x=layers.Conv2D(24,3,strides=2,padding='valid',activation='relu')(x)
x=layers.Conv2D(32,3,strides=2,padding='valid',activation='relu')(x)
x=layers.GlobalAveragePooling2D()(x)
out=layers.Dense(3,activation='softmax')(x)
m=models.Model(inp,out); m.compile('adam','sparse_categorical_crossentropy',metrics=['accuracy'])
if os.path.exists('/tmp/ww/ckpt.weights.h5'):
    try: m.load_weights('/tmp/ww/ckpt.weights.h5'); print('resumed')
    except Exception as e: print('no resume',e)
m.fit(Xtn,Yt,validation_data=(Xvn,Yv),epochs=45,batch_size=128,class_weight={0:1.,1:2.,2:1.},verbose=0,
      callbacks=[tf.keras.callbacks.ReduceLROnPlateau(patience=4,factor=0.5,min_lr=1e-4)])
m.save_weights('/tmp/ww/ckpt.weights.h5'); m.save('/tmp/ww/model.keras')
p=m.predict(Xvn,verbose=0); pred=p.argmax(1)
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
