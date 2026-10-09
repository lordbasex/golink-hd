# Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com>
# Generates ANTÍDOTO's sound effects with MOSS-SoundEffect v2 (Apache 2.0),
# one WAV per effect, skipping the ones already made, so it can run again
# after a stop (run_sfx.sh starts it again until every effect is there). Run it with the model's own environment, on Apple silicon
# (torch.compile off, and the model's float64 made float32, see
# docs/howto/ai-art-and-audio.md):
#   TORCHDYNAMO_DISABLE=1 ~/ai/MOSS-TTS/moss_soundeffect_v2/.venv/bin/python make_sfx.py [OUT_DIR]
import gc
import pathlib
import sys
import wave

import torch
from moss_soundeffect_v2 import MossSoundEffectPipeline

STEPS = 30     # 50 is the model's default; on an M1 each step takes about 10 s
RATE = 48000   # the model's output (its DAC VAE runs at 48 kHz)
STYLE = "1930s cartoon animation sound effect, clean, no music, no speech, "

# name, seconds, what it sounds like
SFX = [
    ("jump", 1, "a short springy boing as a small character jumps"),
    ("land", 1, "a soft thud of small rubber shoes landing on the ground"),
    ("shoot", 1, "a short pop like a cork shot from a toy gun, with a tiny whistle"),
    ("super", 2, "a lid popping open followed by a fizzing burst of many little bubbles"),
    ("dash", 1, "a fast whoosh with a quick slide whistle"),
    ("hurt", 1, "a bonk on a hollow head followed by a short squeaky ouch"),
    ("knockout", 2, "a descending slide whistle ending in the fizz of a tablet dissolving in water"),
    ("revive", 2, "a magical twinkling chime rising up, sparkles"),
    ("yawn", 3, "a big long yawn of a small sleepy creature, ending with a sigh"),
    ("victory", 2, "a cheerful whistle and a happy little jingle of bells"),
    ("germ_squash", 1, "a wet squishy splat of a small slimy blob being stomped"),
    ("virus_pop", 1, "a gooey bubble popping"),
    ("spit", 1, "a quick wet spit of a slime ball"),
    ("buzz", 2, "a small buzzing of tiny flapping wings"),
    ("boss_roar", 2, "a deep comic monster roar"),
    ("stomp", 2, "a heavy stomp that shakes the ground, with a rumble"),
    ("sneeze", 3, "a huge exaggerated sneeze, ah ah achoo, with a gust of wind"),
    ("boss_hit", 1, "a metallic clang with a comic grunt"),
    ("boss_defeat", 3, "a big poof explosion followed by twinkling stars"),
    ("vitamin", 1, "a bright two note chime of picking up a shiny pill"),
    ("checkpoint", 1, "a friendly bicycle bell ringing twice"),
    ("menu", 1, "a short crisp click of a typewriter key"),
    ("ready_go", 2, "an old-fashioned boxing bell ringing once"),
    ("knockout_bell", 2, "a boxing bell ringing three times quickly"),
]


def save(audio, path, rate):
    """A (batch, channels, samples) float tensor as a 16-bit WAV (torchaudio's save needs torchcodec)."""
    a = audio[0].detach().float().cpu().clamp(-1, 1)
    if a.dim() == 1:
        a = a.unsqueeze(0)
    pcm = (a.t().contiguous() * 32767).round().to(torch.int16).numpy().tobytes()
    with wave.open(str(path), "wb") as w:
        w.setnchannels(a.shape[0])
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm)


def main():
    out = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else pathlib.Path.home() / "ai/antidoto/sfx")
    out.mkdir(parents=True, exist_ok=True)
    device = "mps" if torch.backends.mps.is_available() else "cpu"
    pipe = MossSoundEffectPipeline.from_pretrained(
        "OpenMOSS-Team/MOSS-SoundEffect-v2.0", torch_dtype=torch.float32, device=device)
    for i, (name, seconds, what) in enumerate(SFX):
        dst = out / f"{name}.wav"
        if dst.exists():
            continue
        print("generating", name, flush=True)
        torch.manual_seed(1930 + i)
        audio = pipe(prompt=STYLE + what, seconds=seconds, num_inference_steps=STEPS, cfg_scale=4.0)
        save(audio, dst, RATE)
        # the M1's memory fills up from one effect to the next without this
        del audio
        gc.collect()
        if device == "mps":
            torch.mps.empty_cache()
    print("done", flush=True)


if __name__ == "__main__":
    main()
