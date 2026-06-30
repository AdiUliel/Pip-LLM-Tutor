# tts_edge.py — Microsoft edge-tts neural voices (EN + HE). Far closer to real
# speech than espeak; needs internet. Decodes the returned mp3 to 16 kHz mono via
# ffmpeg. build_dataset.py falls back to espeak (tts.py) if available() is False.
import edge_tts
import asyncio
import subprocess
import tempfile
import os
import numpy as np
import soundfile as sf

EN = ["en-US-AriaNeural", "en-US-GuyNeural", "en-US-JennyNeural", "en-US-AnaNeural",  # Ana = child-ish
      "en-GB-RyanNeural", "en-GB-SoniaNeural", "en-AU-NatashaNeural", "en-IN-NeerjaNeural"]
HE = ["he-IL-AvriNeural", "he-IL-HilaNeural"]   # Hebrew-accented — matches the real kids
VOICES = EN + HE


async def _save(text, voice, rate, pitch, path):
    await edge_tts.Communicate(text, voice, rate=rate, pitch=pitch).save(path)


def say_edge(text, voice, rate="+0%", pitch="+0Hz", out_sr=16000):
    """Return mono float32 [-1,1] at out_sr for the spoken text (via edge-tts + ffmpeg)."""
    mp3 = tempfile.mktemp(suffix=".mp3")
    wav = tempfile.mktemp(suffix=".wav")
    try:
        asyncio.run(_save(text, voice, rate, pitch, mp3))
        subprocess.run(["ffmpeg", "-y", "-i", mp3, "-ar", str(out_sr), "-ac", "1", wav],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
        a, _ = sf.read(wav, dtype="float32")
    finally:
        for f in (mp3, wav):
            try:
                os.remove(f)
            except OSError:
                pass
    return a if a.ndim == 1 else a[:, 0]


def available():
    """One quick synth to confirm internet + the service are reachable."""
    try:
        a = say_edge("hi", "en-US-AriaNeural")
        return len(a) > 0
    except Exception as e:
        print("[tts_edge] unavailable, will fall back to espeak:", repr(e)[:160])
        return False
