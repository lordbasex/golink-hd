# Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com>
# Writes one ACE-Step TOML per ANTIDOTO track and generates them one after another.
import subprocess, pathlib
BASE = "Instrumental 1930s jazz big band for a cartoon video game, vintage scratchy record feel, "
TRACKS = [
    ("title",     90, 150, "C major", "grand overture: triumphant brass fanfare, swinging full big band, stride piano, cymbal crashes, heroic and joyful, an adventure is starting"),
    ("map",       60, 120, "G major", "light walking ragtime: bouncy stride piano, clarinet melody, soft brushed drums, curious and cheerful, choosing the next stop, loopable"),
    ("colon",     90, 168, "F major", "hot swing and ragtime, fast stride piano, muted trumpets with wah-wah, clarinet runs, trombone slides, slap upright bass, brushed drums, playful and mischievous mood with a sneaky swampy groove, loopable"),
    ("intestine", 90, 160, "Bb major", "quirky and wobbly: oom-pah tuba bass, xylophone and woodblock runs, bouncy clarinets, twisting and turning melody, comic and springy, loopable"),
    ("stomach",   90, 172, "D minor", "hot jungle jazz: pounding tom-toms, growling plunger trombones, bubbling saxophones, sizzling hi-hat, dangerous and fiery, loopable"),
    ("heart",     90, 180, "E minor", "driving fast swing with a heartbeat kick drum pulse, pizzicato strings, urgent trumpets, racing rhythm like blood rushing, exciting, loopable"),
    ("lungs",     90, 140, "A major", "airy and breezy swing: flutes and clarinets floating, harp glissandos, soft muted brass, light cymbals like wind, hopeful and spacious, loopable"),
    ("brain",     90, 132, "C minor", "mysterious minor swing: eerie musical saw and theremin-like lead, celesta, walking bass, spooky muted trumpets, electric and strange, loopable"),
    ("boss",      90, 190, "G minor", "frantic hot jazz boss battle: frenetic big band, dramatic brass stabs, racing drums, wild clarinet and trumpet solos, intense and fun, loopable"),
    ("final_boss",120, 176, "D minor", "epic final boss battle: thunderous full big band with timpani, dark brass, tense strings, a heroic theme fighting back, grand and dramatic, loopable"),
    ("victory",   12, 150, "C major", "short victory jingle: bright brass fanfare and a happy cymbal ending, ta-da"),
    ("game_over", 10,  80, "C minor", "short game over jingle: sad wah-wah trombone descending, a slow tuba and one last cymbal, comic and melancholic"),
]
out = pathlib.Path.home() / "ai/antidoto/tracks"
for i, (name, dur, bpm, key, desc) in enumerate(TRACKS):
    toml = out / f"{name}.toml"
    toml.write_text(f"""caption = "{BASE}{desc}"
lyrics = "[Instrumental]"
instrumental = true
duration = {dur}
bpm = {bpm}
keyscale = "{key}"
timesignature = "4"
thinking = false
batch_size = 1
save_dir = "{out}/{name}"
audio_format = "wav"
seed = {1930 + i}
use_random_seed = false
offload_to_cpu = true
offload_dit_to_cpu = true
""")
    if list((out / name).glob("*.wav")):
        continue
    print("generating", name, flush=True)
    subprocess.run([".venv/bin/python", "cli.py", "--config", str(toml)], cwd=pathlib.Path.home() / "ai/ACE-Step-1.5",
                   stdout=open(out / f"{name}.log", "w"), stderr=subprocess.STDOUT)
print("done", flush=True)
