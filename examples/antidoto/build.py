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
OUT = os.path.join(HERE, "game")

# animation -> (strip it comes from, frames, fps); the engine's states
ANIMS = {
    "idle": ("idle", 6, 8),
    "run": ("run", 8, 14),
    "jump": ("jump", 6, 10),
    "hurt": ("hurt", 4, 12),
    "bored": ("bored", 8, 6),
    "win": ("win", 6, 8),
}
SKINS = ["red", "blue"]


def png_size(path):
    with open(path, "rb") as f:
        head = f.read(24)
    return struct.unpack(">II", head[16:24])


# zone -> its layers: (picture, height in the game, speed % of the camera, y)
LAYERS = {
    "colon": [("bg_colon_far", 400, 20, -20), ("bg_colon_mid", 300, 55, 200)],
}


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
                        src, dst, str(height)], check=True)
        out.append({"file": name + ".png", "speed": speed, "y": y})
    return out


def textures(zone):
    """The zone's floor textures (made by textures.py into textures/)."""
    out = {}
    for kind in ("ground_top", "ground", "platform", "brick"):
        name = f"tex_{zone}_{kind}.png"
        src = os.path.join(HERE, "textures", name)
        if os.path.exists(src):
            shutil.copy(src, os.path.join(OUT, name))
            out[kind] = name
    return out or None


def thing(name, total, fps, part=None):
    """A level thing's animation from sprites/ (cut.sh), or None."""
    src = os.path.join(HERE, "sprites", name + ".png")
    if not os.path.exists(src):
        return None
    w, h = png_size(src)
    if not os.path.exists(os.path.join(OUT, name + ".png")):
        shutil.copy(src, os.path.join(OUT, name + ".png"))
    return dict({"file": name + ".png", "frame": [w // total, h], "fps": fps, "feet": 2}, **(part or {}))


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
            "effects": {"outline": "#120c0c", "shadows": True, "zoom": "auto", "grade": "sepia", "grade_amount": 40}}


def main():
    if os.path.isdir(OUT):
        shutil.rmtree(OUT)
    os.makedirs(OUT)
    skins = {}
    for skin in SKINS:
        anims = {}
        for name, (strip, frames, fps) in ANIMS.items():
            src = os.path.join(HERE, "sprites", f"{skin}_{strip}.png")
            if not os.path.exists(src):
                continue
            w, h = png_size(src)
            shutil.copy(src, os.path.join(OUT, f"{skin}_{strip}.png"))
            anims[name] = {"file": f"{skin}_{strip}.png", "frame": [w // frames, h], "fps": fps, "feet": 2}
        skins[skin] = anims
    manifest = {
        "format": 3,
        "title": "ANTÍDOTO",
        "version": "0.1.0",
        "genre": "platformer",
        "players": 2,
        "screen": "16:9",
        "level": "level.json",
        "sky": ["#3a1420", "#7a3a3a"],
        # a hero about 80 px tall: hitbox, and a jump of about 12 cells
        "physics": {"hitbox": [28, 66], "enemy_hitbox": [34, 30], "walk": 300, "run": 460, "jump": 1050, "gravity": 50, "gravity_hold": 30, "fall_max": 1200},
        "sprites": {k: v for k, v in {
            "hero": {"players": SKINS, "skins": skins},
            "enemy": {k: v for k, v in {"walk": thing("enemy_germ", 6, 8, {"from": 0, "frames": 4}),
                                         "squashed": thing("enemy_germ", 6, 8, {"from": 5, "frames": 1})}.items() if v} or None,
            "coin": thing("obj_vitamin", 4, 8),
            "checkpoint": {k: v for k, v in {"off": thing("obj_leukocyte", 4, 2, {"from": 0, "frames": 2}),
                                              "on": thing("obj_leukocyte", 4, 4, {"from": 2, "frames": 2})}.items() if v} or None,
            "goal": thing("obj_portal", 4, 6),
        }.items() if v},
        "textures": textures("colon"),
        "layers": layers("colon"),
        "music": music("colon"),
    }
    manifest = {k: v for k, v in manifest.items() if v is not None}
    with open(os.path.join(OUT, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2, ensure_ascii=False)
        f.write("\n")
    with open(os.path.join(OUT, "level.json"), "w", encoding="utf-8") as f:
        json.dump(level(), f, indent=1)
        f.write("\n")
    print(OUT)


if __name__ == "__main__":
    main()
