# tts_edge.py — neural TTS (Microsoft edge-tts) for training data; far closer to
# real speech than espeak. Materialized from RETRAIN_RUNBOOK.md §5a.
# Needs internet + `pip install edge-tts` + ffmpeg on PATH. Import stays lazy so
# build_dataset.py --no-tts runs without either.
import asyncio, os, subprocess, tempfile
import numpy as np

EN = ["en-US-AriaNeural","en-US-GuyNeural","en-US-JennyNeural","en-US-AnaNeural",  # AnaNeural = child-ish
      "en-GB-RyanNeural","en-GB-SoniaNeural","en-AU-NatashaNeural","en-IN-NeerjaNeural"]
HE = ["he-IL-AvriNeural","he-IL-HilaNeural"]   # Hebrew accent — matches the real kids
ALL = EN + HE

def say_edge(text, voice, rate="+0%", pitch="+0Hz", out_sr=16000):
    """Synthesize → float32 mono @out_sr. Raises on missing edge-tts/ffmpeg/net."""
    import edge_tts, wave                            # lazy: only when TTS is used
    mp3=tempfile.mktemp(suffix=".mp3"); wav=tempfile.mktemp(suffix=".wav")
    async def _save():
        await edge_tts.Communicate(text, voice, rate=rate, pitch=pitch).save(mp3)
    asyncio.run(_save())
    subprocess.run(["ffmpeg","-y","-i",mp3,"-ar",str(out_sr),"-ac","1",
                    "-sample_fmt","s16",wav],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
    with wave.open(wav,"rb") as w:
        a=np.frombuffer(w.readframes(w.getnframes()),dtype=np.int16
                        ).astype(np.float32)/32768.0
    os.remove(mp3); os.remove(wav)
    return a

def rand_style(rng):
    """Random (voice, rate, pitch) — spread across speakers, speeds, pitches."""
    return (str(rng.choice(ALL)),
            "%+d%%" % rng.integers(-15, 21),
            "%+dHz" % rng.integers(-10, 31))

# Positive phrasings + hard negatives (near-misses first — they matter most).
POS_TEXTS = ["hey pip", "hey pip.", "hey, pip", "hey pip!", "היי פיפ"]
NEG_TEXTS = ["hey","pip","pip pip","hey there","hey pick","hey pin","hey kid",
             "hey ship","hey big","pick","pin","peep","pippa","philip","people",
             "puppy","pizza","hippo","happy","hello","hi","okay","stop","yes","no",
             "please","help","robot","listen","seven","באמת","נכון","שלום","תודה",
             "כן","לא","שמונה","ארבע"]
