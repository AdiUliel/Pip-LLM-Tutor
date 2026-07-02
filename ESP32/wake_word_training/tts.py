import ctypes, numpy as np, espeakng_loader
from scipy.signal import resample_poly

_lib = ctypes.CDLL(espeakng_loader.get_library_path())
_DATA = espeakng_loader.get_data_path().encode()
# enums
AUDIO_OUTPUT_RETRIEVAL = 1
espeakRATE, espeakVOLUME, espeakPITCH, espeakRANGE = 1,2,3,4
espeakCHARS_UTF8 = 1

_lib.espeak_Initialize.restype = ctypes.c_int
sr = _lib.espeak_Initialize(AUDIO_OUTPUT_RETRIEVAL, 0, _DATA, 0)
if sr <= 0:
    raise RuntimeError("espeak_Initialize failed: %d" % sr)
ESPEAK_SR = sr

_buf = []
CB = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.POINTER(ctypes.c_short), ctypes.c_int, ctypes.c_void_p)
def _cb(wav, n, events):
    if n > 0:
        _buf.extend(wav[i] for i in range(n))
    return 0
_cbref = CB(_cb)
_lib.espeak_SetSynthCallback(_cbref)
_lib.espeak_SetVoiceByName.argtypes=[ctypes.c_char_p]
_lib.espeak_SetParameter.argtypes=[ctypes.c_int,ctypes.c_int,ctypes.c_int]
_lib.espeak_Synth.argtypes=[ctypes.c_char_p,ctypes.c_size_t,ctypes.c_uint,ctypes.c_int,ctypes.c_uint,ctypes.c_uint,ctypes.POINTER(ctypes.c_uint),ctypes.c_void_p]

def say(text, voice="en-us", rate=175, pitch=50, vol=100, out_sr=16000):
    """Return mono float32 [-1,1] at out_sr for the spoken text."""
    global _buf
    _buf = []
    if _lib.espeak_SetVoiceByName(voice.encode()) != 0:
        raise RuntimeError("voice not found: "+voice)
    _lib.espeak_SetParameter(espeakRATE, int(rate), 0)
    _lib.espeak_SetParameter(espeakPITCH, int(pitch), 0)
    _lib.espeak_SetParameter(espeakVOLUME, int(vol), 0)
    t = text.encode("utf-8")
    _lib.espeak_Synth(t, len(t)+1, 0, 0, 0, espeakCHARS_UTF8, None, None)
    _lib.espeak_Synchronize()
    a = np.array(_buf, dtype=np.float32)/32768.0
    if len(a)==0: return a
    if ESPEAK_SR != out_sr:
        a = resample_poly(a, out_sr, ESPEAK_SR).astype(np.float32)
    return a

if __name__=="__main__":
    print("espeak SR:", ESPEAK_SR)
    import soundfile as sf
    voices=["en-us","en-us+f2","en-us+m3","en-gb","en-gb-x-rp+f4","en-us+m5"]
    for v in voices:
        a=say("hey pip", voice=v, rate=160, pitch=55)
        print(f"{v:16s} dur={len(a)/16000:.2f}s peak={np.max(np.abs(a)):.2f} n={len(a)}")
        sf.write(f"/tmp/ww/sample_{v.replace('+','_').replace('-','')}.wav", a, 16000)
    print("OK wrote samples")
