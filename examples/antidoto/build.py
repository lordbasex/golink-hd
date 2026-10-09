#!/usr/bin/env python3
# Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com>
"""Builds ANTÍDOTO's package folder (examples/antidoto/game) from the cut
sprites (sprites/, made by cut.sh): the manifest with the heroes' physics
and skins, and the level. Pack it with tools/glhd pack.

  python3 examples/antidoto/build.py
"""
import json
import os
import shutil
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
# RES=2 (720p) or RES=3 (1080p): the pictures cut that many times bigger (cut.sh, parts.py and
# textures.py with the same RES) and the package in game_x2/ or game_x3/; the game itself is the same
RES = int(os.environ.get("RES", "1"))
SPRITES = "sprites" if RES == 1 else f"sprites_x{RES}"
TEXTURES = "textures" if RES == 1 else f"textures_x{RES}"
OUT = os.path.join(HERE, "game" if RES == 1 else f"game_x{RES}")

# animation -> (strip it comes from, frames, fps); the engine's states
ANIMS = {
    "idle": ("idle", 6, 8),
    "run": ("walk12", 12, 14),
    "jump": ("jump", 6, 10),
    "hurt": ("hurt", 4, 12),
    "bored": ("bored", 8, 6),
    "win": ("win", 6, 8),
    "knockout": ("dissolve", 8, 10),  # played once over the knockout (its fps does not count)
    "super": ("super", 8, 10),        # played once over the super attack
}
SKINS = ["red", "blue"]
# pixels walked in one whole walk cycle (12 frames, two steps): the steps follow the ground; at the
# walking speed (1.8 px a frame) a cycle lasts about a second, 12 drawings a second like an animated cartoon
STRIDE = 110
# the rubber-hose puppet (src/rig.c): parts cut by parts.py, arms and legs drawn by the engine;
# a skin with a rig is posed on every frame instead of playing its sheets
RIG = {"limb": 4, "leg": 24, "arm": 15, "stride": 84, "lift": 8, "bob": 3}
# the finger pistol: X fires antibodies (about 7 a second) from the fingertip of the puppet's aiming
# pose; a germ takes 3. Y, once 6 antibodies have hit (the first two germs), opens the capsule: a fan of 10 granules
# three hits; with the last one left the capsule looks worn out; at none it dissolves
HEALTH = {"hits": 3, "worn": 1, "knockout": 100}
WEAPON = {"button": "x", "rate": 8, "speed": 900, "range": 420, "muzzle": [30, -33], "enemy_health": 3,
          "super": {"button": "y", "charge": 6, "granules": 10, "spread": 70, "speed": 650, "range": 260,
                    "damage": 2, "frames": 48, "release": 22}}
# a skin's shot and its burst (cut.sh): strip, frames, fps
SHOTS = {"shot": ("shot", 4, 12), "shot_hit": ("shot_hit", 6, 20), "granule": ("granule", 4, 12)}


def rig(skin):
    """The skin's puppet from sprites/ (parts.py), or None."""
    files = {"body": f"{skin}_body.png", "hand": f"{skin}_hands.png", "foot": f"{skin}_feet.png"}
    worn = f"{skin}_body_worn.png"
    if os.environ.get("NO_RIG") or not all(os.path.exists(os.path.join(HERE, SPRITES, f)) for f in files.values()):
        return None
    # the hoses' sizes are drawn at the pictures' scale; the stride is walked in the game's pixels
    out = {k: v * RES if k != "stride" else v for k, v in RIG.items()}
    for part, name in files.items():
        shutil.copy(os.path.join(HERE, SPRITES, name), os.path.join(OUT, name))
        w, h = png_size(os.path.join(OUT, name))
        cells = 4 if part == "foot" else 6
        out[part] = {"file": name, "frame": [w // cells, h]}
        if part != "hand":
            out[part]["feet"] = 2
    if os.path.exists(os.path.join(HERE, SPRITES, worn)):
        shutil.copy(os.path.join(HERE, SPRITES, worn), os.path.join(OUT, worn))
        w, h = png_size(os.path.join(OUT, worn))
        out["worn"] = {"file": worn, "frame": [w // 6, h], "feet": 2}
    return out


def png_size(path):
    with open(path, "rb") as f:
        head = f.read(24)
    return struct.unpack(">II", head[16:24])


# zone -> its layers: (picture, height in the game, speed % of the camera, y)
# the body from the bottom up: each zone's level, sky, grade and difficulty (0 = the hand-made first level)
ZONES = [
    ("colon", ["#3a1420", "#7a3a3a"], "sepia"),
    ("intestine", ["#3a1a12", "#8a4a30"], "sepia"),
    ("stomach", ["#2a1008", "#9a4a1a"], "sunset"),
    ("lungs", ["#2a3040", "#a07080"], "sepia"),
    ("heart", ["#300810", "#902030"], "sunset"),
    ("brain", ["#1a1030", "#6a4a8a"], "night"),
]
# zone -> its layers: (picture, height in the game, speed % of the camera, y)
LAYERS = {z: [(f"bg_{z}_far", 400, 20, -20), (f"bg_{z}_mid", 300, 55, 200)] for z, _, _ in ZONES}


def layers(zone):
    """Resizes the zone's layer pictures to the game's size (Pillow through uv, see cut.sh) and lists them."""
    import subprocess
    out = []
    for name, height, speed, y in LAYERS[zone]:
        src = os.path.join(HERE, "source", name + ".png")
        if not os.path.exists(src):
            continue
        dst = os.path.join(OUT, name + ".png")
        subprocess.run([os.environ.get("UV", "uv"), "run", "-q", "--with", "pillow", "python", "-c",
                        "import sys; from PIL import Image; im = Image.open(sys.argv[1]);"
                        " im.resize((round(im.width * int(sys.argv[3]) / im.height), int(sys.argv[3])), Image.LANCZOS).save(sys.argv[2])",
                        src, dst, str(height * RES)], check=True)
        out.append({"file": name + ".png", "speed": speed, "y": y})
    return out


def textures(zone):
    """The zone's floor textures (made by textures.py into textures/)."""
    out = {}
    for kind in ("ground_top", "ground", "platform", "brick"):
        name = f"tex_{zone}_{kind}.png"
        src = os.path.join(HERE, TEXTURES, name)
        if os.path.exists(src):
            shutil.copy(src, os.path.join(OUT, name))
            out[kind] = name
    return out or None


def thing(name, total, fps, part=None):
    """A level thing's animation from sprites/ (cut.sh), or None."""
    src = os.path.join(HERE, SPRITES, name + ".png")
    if not os.path.exists(src):
        return None
    w, h = png_size(src)
    if not os.path.exists(os.path.join(OUT, name + ".png")):
        shutil.copy(src, os.path.join(OUT, name + ".png"))
    return dict({"file": name + ".png", "frame": [w // total, h], "fps": fps, "feet": 2}, **(part or {}))


def screens():
    """The title and the ending (resized by the engine to the screen); each level has its own intro card."""
    out = {}
    for key, name in (("title", "ui_start"), ("ending", "ui_end")):
        src = os.path.join(HERE, "source", name + ".png")
        if os.path.exists(src):
            shutil.copy(src, os.path.join(OUT, name + ".png"))
            out[key] = name + ".png"
    if out:
        out["intro_seconds"] = 6
    return out or None


# the engine's effects -> ANTÍDOTO's (made by make_sfx.py on the machine with the model, in source/sfx)
SOUNDS = {"jump": "jump", "coin": "vitamin", "stomp": "germ_squash", "hurt": "hurt",
          "join": "ready_go", "check": "checkpoint", "clear": "victory", "pause": "menu",
          "shoot": "shoot", "hit": "virus_pop", "knockout": "knockout", "super": "super", "yawn": "yawn"}
SFX_PEAK = 13000       # every effect at the same loudness, well under the music's
SFX_QUIET = 600        # quieter than this at the start or the end is silence


def sfx(name):
    """An effect from source/sfx made ready for the game: the silence before and after cut, so it sounds
    the moment the button is pressed, the same peak as the others, a short fade out."""
    import array
    import wave
    src = os.path.join(HERE, "source", "sfx", name + ".wav")
    if not os.path.exists(src):
        return None
    with wave.open(src) as w:
        ch, rate = w.getnchannels(), w.getframerate()
        pcm = array.array("h", w.readframes(w.getnframes()))
    loud = [i for i, v in enumerate(pcm) if abs(v) > SFX_QUIET]
    if not loud:
        return None
    pcm = pcm[loud[0] // ch * ch:(loud[-1] // ch + 1) * ch]
    peak = max(abs(v) for v in pcm)
    fade = rate * 15 // 1000 * ch
    out = array.array("h", (int(v * SFX_PEAK / peak * min(1, (len(pcm) - i) / fade)) for i, v in enumerate(pcm)))
    with wave.open(os.path.join(OUT, "sfx_" + name + ".wav"), "wb") as w:
        w.setnchannels(ch)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(out.tobytes())
    return "sfx_" + name + ".wav"


def sounds():
    out = {k: f for k, f in ((k, sfx(v)) for k, v in SOUNDS.items()) if f}
    return out or None


def music(track):
    """The zone's music from source/music (made by make_tracks.py on the machine with the model)."""
    src = os.path.join(HERE, "source", "music", track + ".wav")
    if not os.path.exists(src):
        return None
    shutil.copy(src, os.path.join(OUT, track + ".wav"))
    return {"file": track + ".wav", "volume": 180}


def level():
    """A first stretch of the colon: ground, gaps, platforms at a big hero's reach, enemies, a checkpoint, the goal."""
    w, h = 220, 30
    rows = [["."] * w for _ in range(h)]
    for r in range(26, 30):
        for c in range(w):
            rows[r][c] = "#"

    def put(col, row, text):
        for i, ch in enumerate(text):
            rows[row][col + i] = ch

    for c0, c1 in ((40, 44), (96, 101), (150, 156)):
        for r in range(26, 30):
            for c in range(c0, c1):
                rows[r][c] = "."
    put(20, 20, "======")
    put(21, 19, "oooo")
    put(30, 16, "BBBB")
    put(30, 15, "oooo")
    put(28, 25, "E")  # the first germs, on flat ground before the first gap: learn to shoot
    put(35, 25, "E")
    put(55, 25, "E")
    put(66, 25, "E")
    put(74, 21, "=======")
    put(75, 20, "ooooo")
    put(86, 25, "C")
    put(96, 21, "======")
    put(110, 25, "E")
    put(118, 19, "BBBBB")
    put(119, 18, "ooo")
    put(130, 23, "=====")
    put(138, 19, "=====")
    put(146, 15, "=====")
    put(147, 14, "ooo")
    put(165, 25, "E")
    put(172, 25, "E")
    put(180, 25, "C")
    put(190, 21, "=====")
    put(212, 25, "F")
    return {"width": w, "height": h, "start": [4, 25], "rows": ["".join(r) for r in rows],
            "effects": {"shadows": True, "zoom": "auto", "grade": "sepia", "grade_amount": 40}}


def intro(zone):
    """The zone's intro card."""
    name = f"ui_intro_{zone}"
    src = os.path.join(HERE, "source", name + ".png")
    if not os.path.exists(src):
        return None
    shutil.copy(src, os.path.join(OUT, name + ".png"))
    return name + ".png"


def made_level(k, grade):
    """Level k + 1 (k >= 1), made from a fixed seed so every build is the same: the ground broken by
    gaps a hero clears, platforms with vitamins over some of them, brick steps, germs on the flat
    stretches, two checkpoints and the goal; longer, with more gaps and germs, as the body goes up."""
    import random
    rnd = random.Random(1930 + k)
    w, h, ground = 200 + 20 * k, 30, 26
    rows = [["."] * w for _ in range(h)]
    for r in range(ground, h):
        for c in range(w):
            rows[r][c] = "#"

    def put(col, row, text):
        for i, ch in enumerate(text):
            if 0 <= col + i < w:
                rows[row][col + i] = ch

    def gap(c0, n):
        for r in range(ground, h):
            for c in range(c0, c0 + n):
                rows[r][c] = "."

    c, enemies, checks = 14, 0, [w // 3, 2 * w // 3]
    while c < w - 24:
        run = rnd.randint(9, 16) - min(k, 4)
        stretch = max(6, run)
        # germs on the flat, more of them higher up the body
        for _ in range(rnd.randint(0, 1 + k // 2) if stretch >= 8 else 0):
            put(c + rnd.randint(3, stretch - 2), ground - 1, "E")
            enemies += 1
        kind = rnd.random()
        if kind < 0.35:
            put(c + 2, ground - 5, "=" * 5)
            put(c + 2, ground - 6, "ooo")
        elif kind < 0.55:
            put(c + 3, ground - 1, "BB")
            put(c + 4, ground - 2, "B")
            put(c + 4, ground - 3, "o")
        c += stretch
        if any(c - stretch < x <= c for x in checks):
            put(c - 2, ground - 1, "C")
            continue
        n = rnd.randint(2, 3 + (k >= 3))
        gap(c, n)
        if rnd.random() < 0.5:
            put(c - 1, ground - 5, "=" * (n + 2))
            put(c, ground - 6, "o" * n)
        c += n
    put(w - 8, ground - 1, "F")
    return {"width": w, "height": h, "start": [4, ground - 1], "rows": ["".join(r) for r in rows],
            "effects": {"shadows": True, "zoom": "auto", "grade": grade, "grade_amount": 40}}


def levels():
    """Every zone's level, in the order the body is climbed."""
    out = []
    for k, (zone, sky, grade) in enumerate(ZONES):
        lv = level() if k == 0 else made_level(k, grade)
        name = f"level_{k + 1}_{zone}.json"
        with open(os.path.join(OUT, name), "w", encoding="utf-8") as f:
            json.dump(lv, f, indent=1)
            f.write("\n")
        entry = {"level": name, "sky": sky, "layers": layers(zone), "textures": textures(zone),
                 "intro": intro(zone), "music": music(zone)}
        out.append({k2: v for k2, v in entry.items() if v})
    return out


def main():
    if os.path.isdir(OUT):
        shutil.rmtree(OUT)
    os.makedirs(OUT)
    skins = {}
    for skin in SKINS:
        anims = {}
        for name, (strip, frames, fps) in ANIMS.items():
            src = os.path.join(HERE, SPRITES, f"{skin}_{strip}.png")
            if not os.path.exists(src):
                continue
            w, h = png_size(src)
            shutil.copy(src, os.path.join(OUT, f"{skin}_{strip}.png"))
            anims[name] = {"file": f"{skin}_{strip}.png", "frame": [w // frames, h], "fps": fps, "feet": 2}
            if name == "run":
                anims[name]["stride"] = STRIDE
        for name, (strip, frames, fps) in SHOTS.items():
            src = os.path.join(HERE, SPRITES, f"{skin}_{strip}.png")
            if os.path.exists(src):
                w, h = png_size(src)
                shutil.copy(src, os.path.join(OUT, f"{skin}_{strip}.png"))
                anims[name] = {"file": f"{skin}_{strip}.png", "frame": [w // frames, h], "fps": fps}
        skins[skin] = anims
        puppet = rig(skin)
        if puppet:
            skins[skin]["rig"] = puppet
    manifest = {
        "format": 3,
        "title": "ANTÍDOTO",
        "version": "0.1.0",
        "genre": "platformer",
        "players": 2,
        "screen": "16:9",
        "resolution": {1: "360p", 2: "720p", 3: "1080p"}[RES],
        "sky": ["#3a1420", "#7a3a3a"],
        # a hero about 80 px tall: hitbox, and a jump of about 12 cells
        "weapon": WEAPON,
        "health": HEALTH,
        "physics": {"hitbox": [28, 66], "enemy_hitbox": [34, 30], "walk": 180, "run": 280, "jump": 1050, "gravity": 50, "gravity_hold": 30, "fall_max": 1200},
        "sprites": {k: v for k, v in {
            "hero": {"players": SKINS, "skins": skins},
            "enemy": {k: v for k, v in {"walk": dict(thing("enemy_germ_walk", 8, 10) or {}, stride=70) or None,
                                         "squashed": thing("enemy_germ", 6, 8, {"from": 5, "frames": 1})}.items() if v} or None,
            "coin": thing("obj_vitamin", 4, 8),
            "checkpoint": {k: v for k, v in {"off": thing("obj_leukocyte", 4, 2, {"from": 0, "frames": 2}),
                                              "on": thing("obj_leukocyte", 4, 4, {"from": 2, "frames": 2})}.items() if v} or None,
            "goal": thing("obj_portal", 4, 6),
        }.items() if v},
        "screens": screens(),
        "sounds": sounds(),
        "levels": levels(),
    }
    manifest = {k: v for k, v in manifest.items() if v is not None}
    with open(os.path.join(OUT, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2, ensure_ascii=False)
        f.write("\n")
    print(OUT)


if __name__ == "__main__":
    main()
