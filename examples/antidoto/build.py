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
    "dash": ("dash", 6, 30),          # played once over the dash
}
SKINS = ["red", "blue"]
# pixels walked in one whole walk cycle (12 frames, two steps): the steps follow the ground; at the
# walking speed (2.6 px a frame) a cycle lasts about two thirds of a second
STRIDE = 110
# the rubber-hose puppet (src/rig.c): parts cut by parts.py, arms and legs drawn by the engine;
# a skin with a rig is posed on every frame instead of playing its sheets
RIG = {"limb": 4, "leg": 24, "arm": 15, "stride": 84, "lift": 8, "bob": 3}
# the finger pistol: X fires antibodies (about 7 a second) from the fingertip of the puppet's aiming
# pose; a germ takes 3. Y, once 6 antibodies have hit (the first two germs), opens the capsule: a fan of 10 granules
# three hits; with the last one left the capsule looks worn out; at none it dissolves
# two seconds unhurt after a hit; 3 lives, then 10 seconds to continue (each player its own countdown)
HEALTH = {"hits": 3, "worn": 1, "knockout": 100, "invulnerable": 120, "lives": 3, "continue": 10}
WEAPON = {"button": "x", "rate": 8, "speed": 900, "range": 420, "muzzle": [30, -33], "enemy_health": 3,
          "super": {"button": "y", "charge": 6, "granules": 10, "spread": 70, "speed": 650, "range": 260,
                    "damage": 2, "frames": 48, "release": 22}}
# A dashes: a quick rush forward (about 5 cells), through enemies, once in the air until landing
DASH = {"button": "a", "speed": 750, "frames": 14, "cooldown": 30}
# the zones' other enemies: a spore flies after the heroes, bobbing; a spitter stands and lobs green globs
ENEMIES = {"spore": {"hitbox": [30, 28], "health": 2, "speed": 80, "bob": 10, "range": 260},
           "spitter": {"hitbox": [40, 40], "health": 4, "rate": 100, "shot_speed": 380, "range": 340},
           # a bacteria curls up and rolls at the heroes; a parasite leaps at them; a fungus puffs spores up
           # that fall on them; a cancer cell crawls at them and splits in two when beaten
           "roller": {"hitbox": [30, 30], "health": 3, "speed": 330, "range": 380},
           "hopper": {"hitbox": [32, 30], "health": 2, "speed": 260, "jump": 720, "rate": 80, "range": 320},
           "puffer": {"hitbox": [36, 44], "health": 4, "rate": 140, "shot_speed": 300, "spores": 3, "range": 340},
           "splitter": {"hitbox": [40, 38], "health": 6, "speed": 70, "range": 420}}
# zone -> its boss: name, health, attacks in turn, rest between them, spit fan, brood size, speed; harder up the body.
# FAGO REX creeps like a spider: forward at the heroes and back, then lets its brood out, and again
BOSSES = {
    "colon": ("FAGO REX", 180, ["advance", "brood", "advance", "jump", "brood"], 50, 3, 6, 220),
    "intestine": ("LOMBRIZ VÍRICA", 230, ["charge", "brood", "spit", "advance", "brood"], 50, 3, 6, 260),
    "stomach": ("ÁCIDO BARÓN", 280, ["spit", "advance", "brood", "spit", "jump", "brood"], 45, 4, 7, 250),
    "lungs": ("GRIPÓN", 330, ["spit", "brood", "spit", "advance", "brood"], 45, 5, 7, 240),
    "heart": ("REY CÁPSIDE", 380, ["jump", "brood", "charge", "spit", "brood"], 40, 4, 8, 280),
    "brain": ("NEUROVIRUS", 500, ["spit", "brood", "jump", "charge", "brood", "advance"], 35, 5, 9, 300),
}
# zone -> its mid-boss, at the end of the first act (the same fields as BOSSES), a little easier than the zone's boss
MINIBOSSES = {
    "colon": ("CAPITÁN COLI", 110, ["charge", "brood", "advance", "spit"], 55, 3, 4, 240),
    "intestine": ("LA TENIA", 140, ["advance", "brood", "charge", "spit"], 55, 3, 5, 260),
    "stomach": ("HONGÓN", 170, ["spit", "brood", "jump", "spit"], 50, 4, 5, 220),
    "lungs": ("MOHO NEGRO", 200, ["spit", "brood", "spit", "charge"], 50, 5, 5, 230),
    "heart": ("TUMORÓN", 230, ["jump", "brood", "advance", "brood"], 45, 3, 6, 240),
    "brain": ("EL PRIÓN", 260, ["charge", "spit", "brood", "jump"], 45, 4, 6, 280),
}
# NEUROVIRUS, beaten once, evolves: bigger, stronger, faster, angrier
OMEGA = ("NEUROVIRUS OMEGA", 650, ["spit", "charge", "brood", "jump", "spit", "brood"], 30, 7, 10, 320)
# the credits after the ending ("# " starts a heading)
CREDITS = [
    "# ANTÍDOTO", "",
    "# IDEA, DISEÑO Y PROGRAMACIÓN", "Federico Pereira", "",
    "# MOTOR", "go-link HD", "motor propio en C, sin librerías externas", "",
    "# SE JUEGA CON", "go-link", "Pion WebRTC", "libvpx (VP8)", "Opus", "Fyne", "",
    "# GRACIAS", "a quienes probaron el juego", "y a ti por jugarlo", "",
    "# FIN", "",
]
# the enemies of each zone past the germs, per stretch (0 to 1): S spores (in the air), P spitters, R bacteria
# (rollers), H parasites (hoppers), U fungi (puffers), K cancer cells (splitters)
ZONE_ENEMIES = {"colon": {"R": 0.35, "H": 0.2},
                "intestine": {"P": 0.3, "H": 0.35, "R": 0.2},
                "stomach": {"S": 0.25, "P": 0.25, "U": 0.35, "R": 0.2},
                "lungs": {"S": 0.5, "U": 0.4, "H": 0.15},
                "heart": {"S": 0.15, "P": 0.3, "K": 0.35, "H": 0.25},
                "brain": {"S": 0.3, "P": 0.25, "K": 0.3, "U": 0.25, "R": 0.2, "H": 0.2}}
ARENA = 52  # the boss's arena: flat cells at the level's end, a checkpoint at its start
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
    """The zone's layer pictures made to repeat and sized for the game (layers.py through uv, see cut.sh), listed."""
    import subprocess
    out = []
    for name, height, speed, y in LAYERS[zone]:
        src = os.path.join(HERE, "source", name + ".png")
        if not os.path.exists(src):
            continue
        dst = os.path.join(OUT, name + ".png")
        # made to repeat with no seam (the engine lays it again and again across the level)
        subprocess.run([os.environ.get("UV", "uv"), "run", "-q", "--with", "pillow", "--with", "numpy", "python",
                        os.path.join(HERE, "layers.py"), src, dst, str(height * RES)], check=True, cwd=HERE)
        out.append({"file": name + ".png", "speed": speed, "y": y})
    return out


def textures(zone):
    """The zone's floor textures (made by textures.py into textures/)."""
    out = {}
    for kind in [k + side for k in ("ground_top", "ground", "platform", "brick", "brick_top", "brick_bottom") for side in ("", "_left", "_right")]:
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
          "shoot": "shoot", "hit": "virus_pop", "knockout": "knockout", "super": "super", "yawn": "yawn",
          "spit": "spit", "dash": "dash", "roar": "boss_roar", "boss_hit": "boss_hit", "boss_down": "boss_defeat"}
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
    """The zone's music from source/music (made by make_tracks.py on the machine with the model), made IMA
    ADPCM by tools/glhd (a quarter of the size; WAV PCM made the package about 190 MB)."""
    import subprocess
    src = os.path.join(HERE, "source", "music", track + ".wav")
    if not os.path.exists(src):
        return None
    dst = os.path.join(OUT, track + ".wav")
    if not os.path.exists(dst):
        subprocess.run([os.path.join(HERE, "..", "..", "tools", "glhd"), "adpcm", src, dst], check=True, stdout=subprocess.DEVNULL)
    return {"file": track + ".wav", "volume": 180}


def boss_sprites(sheet_name):
    """A boss sheet cut by cut.sh (idle, idle, windup, attack, hurt, down): its hitbox and its animations."""
    sheet = thing(sheet_name, 6, 4)
    fw, fh = sheet["frame"]
    # the body, not the frame (a sneeze or a bolt widens some frames): about half its width, most of its height
    w, h = min(fw * 55 // 100, 130 * RES) // RES, fh * 82 // 100 // RES
    part = lambda first, n, fps=4: dict(sheet, **{"from": first, "frames": n, "fps": fps})
    return [w, h], {"idle": part(0, 2, 3), "windup": part(2, 1), "attack": part(3, 1), "hurt": part(4, 1),
                    "down": part(5, 1)}


def boss(zone, mid=False):
    """The zone's boss (or, `mid`, its mid-boss): its sheet cut by cut.sh, its minions the same sheet small,
    its music; NEUROVIRUS evolves into its OMEGA form."""
    prefix, table = ("miniboss", MINIBOSSES) if mid else ("boss", BOSSES)
    if zone not in table or not os.path.exists(os.path.join(HERE, SPRITES, f"{prefix}_{zone}.png")):
        return None
    name, health, attacks, rest, spit, brood, speed = table[zone]
    hitbox, sprites = boss_sprites(f"{prefix}_{zone}")
    small = thing(f"{prefix}_{zone}_minion", 6, 8)
    sprites["minion"] = dict(small, **{"from": 0, "frames": 2, "fps": 8})
    out = {"name": name, "hitbox": hitbox, "health": health, "attacks": attacks, "rest": rest, "spit": spit,
           "brood": brood, "speed": speed, "shot_speed": 420,
           "minion": {"hitbox": [28, 28], "health": 1, "speed": 170}, "sprites": sprites}
    spit_pic = thing("obj_spit", 1, 1)
    if spit_pic:
        out["sprites"]["shot"] = spit_pic
    if zone == "brain" and not mid and os.path.exists(os.path.join(HERE, SPRITES, "boss_brain_omega.png")):
        name, health, attacks, rest, spit, brood, speed = OMEGA
        hitbox, sprites = boss_sprites("boss_brain_omega")
        out["evolve"] = {"name": name, "hitbox": hitbox, "health": health, "attacks": attacks, "rest": rest,
                         "spit": spit, "brood": brood, "speed": speed, "shot_speed": 520, "sprites": sprites}
    m = music("final_boss" if zone == "brain" and not mid else "boss")
    if m:
        out["music"] = m
    return out


def arena(lv):
    """The level's end made the boss's arena: ARENA flat cells, a checkpoint where it starts, the boss ('X')
    near its end and the goal (shown when the boss is beaten) after it."""
    w, h = lv["width"], lv["height"]
    ground = min(r for r in range(h) if lv["rows"][r].count("#") > w // 2)
    rows = [list(r.replace("F", ".")) + ["#" if r_i >= ground else "." for _ in range(ARENA)] for r_i, r in enumerate(lv["rows"])]
    w += ARENA
    for c in range(w - ARENA - 8, w):  # the last stretch before it is flat too: no gap at the arena's edge
        for r in range(h):
            rows[r][c] = "#" if r >= ground else "."
    rows[ground - 1][w - ARENA - 4] = "C"
    rows[ground - 1][w - 14] = "X"
    rows[ground - 1][w - 3] = "F"
    return dict(lv, width=w, rows=["".join(r) for r in rows])


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


# each zone is two acts: the first one long, the second longer and ending in the boss's arena; cells wide
# (the engine takes up to 1792), so the whole game takes 40 to 60 minutes
def act_width(k, act):
    return 1200 + 60 * k + 100 * act


def made_level(k, grade, act, base=None):
    """Zone k's act (0 or 1), made from a fixed seed so every build is the same: stretches of ground with
    germs and the zone's own enemies, broken by gaps a hero clears and by sections that change the pace
    (platform bridges over long pits, brick stairs, towers with vitamins on top, enemy gauntlets, quiet
    stretches of vitamins), a checkpoint every 180 cells, the goal; harder as the body goes up and in
    the second act. `base`, a hand-made start (the colon's first act), is kept and the rest made after it."""
    import random
    rnd = random.Random(1930 + k * 10 + act)
    w, h, ground = act_width(k, act), 30, 26
    hard = k + act  # 0 to 6
    rows = [["."] * w for _ in range(h)]
    for r in range(ground, h):
        for c in range(w):
            rows[r][c] = "#"
    c = 14
    if base:
        for r in range(h):
            for c2, ch in enumerate(base["rows"][r][:base["width"] - 10]):
                rows[r][c2] = ch
        c = base["width"] - 10

    def put(col, row, text):
        for i, ch in enumerate(text):
            if 0 <= col + i < w:
                rows[row][col + i] = ch

    def gap(c0, n):
        for r in range(ground, h):
            for x in range(c0, min(w, c0 + n)):
                rows[r][x] = "."

    # enemies spread over the whole act: no more than its share so far (the engine takes 224 a level)
    budget, placed = 90 + 18 * hard, [0]

    def foe(col, row, ch):
        if placed[0] < budget * col / w + 6 and 0 <= col < w:
            rows[row][col] = ch
            placed[0] += 1

    mix = ZONE_ENEMIES[ZONES[k][0]]
    spores = mix.get("S", 0)
    ground_kinds = [ch for ch in "PRHUK" if mix.get(ch)]

    def zone_foe(col):
        """One of the zone's own ground enemies, picked by their weights (None: a germ)."""
        roll = rnd.random() * max(1, sum(mix.get(ch, 0) for ch in ground_kinds))
        for ch in ground_kinds:
            roll -= mix[ch]
            if roll < 0:
                return ch
        return None

    checks = list(range(180, w - 40, 180))
    while c < w - 30:
        before = c
        kind = rnd.random()
        if kind < 0.12:
            # a bridge: a long pit crossed on short platforms, vitamins over the gaps between them
            n = rnd.randint(9, 12 + min(hard, 4))
            gap(c, n)
            x = c
            while x < c + n - 1:
                put(x, ground - 3 - rnd.randint(0, 2), "===")
                if x + 4 < c + n:
                    put(x + 3, ground - 7, "o")
                x += 5
            c += n
        elif kind < 0.22:
            # brick stairs up and a jump off the top
            steps = rnd.randint(3, 5)
            for i in range(steps):
                for r in range(ground - 1 - i, ground):
                    put(c + i * 2, r, "BB")
            put(c + steps * 2 - 2, ground - 2 - steps, "oo")
            c += steps * 2
            n = rnd.randint(2, 3)
            gap(c, n)
            c += n
        elif kind < 0.30:
            # a tower: a brick column, vitamins on top and a spore around it from the stomach up
            tall = rnd.randint(4, 7)
            for r in range(ground - tall, ground):
                put(c + 3, r, "BB")
            put(c + 3, ground - tall - 1, "oo")
            if spores and rnd.random() < spores:
                foe(c + 8, ground - tall - 2, "S")
            c += 10
        elif kind < 0.38 + 0.02 * hard:
            # a gauntlet: germs and the zone's own enemies in a row
            n = 3 + min(hard, 4)
            for i in range(n):
                foe(c + 3 + i * 3, ground - 1, (zone_foe(c) if rnd.random() < 0.5 else None) or "E")
            if ground_kinds:
                foe(c + 4 + n * 3, ground - 1, ground_kinds[rnd.randrange(len(ground_kinds))])
            c += 7 + n * 3
        elif kind < 0.46:
            # a quiet stretch: vitamins in an arc, a breath before what comes next
            for i in range(8):
                put(c + 2 + i, ground - 2 - (3 if 2 <= i <= 5 else 1), "o")
            c += 12
        else:
            # the plain stretch: germs on the flat, the zone's enemies, a platform or a step, then a gap
            run = rnd.randint(9, 16) - min(hard, 4)
            stretch = max(6, run)
            for _ in range(rnd.randint(0, 1 + hard // 2) if stretch >= 8 else 0):
                foe(c + rnd.randint(3, stretch - 2), ground - 1, "E")
            if stretch >= 8 and rnd.random() < spores:
                foe(c + rnd.randint(2, stretch - 2), ground - 7 - rnd.randint(0, 3), "S")
            for ch in ground_kinds:
                if stretch >= 8 and rnd.random() < mix[ch]:
                    foe(c + rnd.randint(3, stretch - 3), ground - 1, ch)
            pick = rnd.random()
            if pick < 0.35:
                put(c + 2, ground - 5, "=" * 5)
                put(c + 2, ground - 6, "ooo")
            elif pick < 0.55:
                put(c + 3, ground - 1, "BB")
                put(c + 4, ground - 2, "B")
                put(c + 4, ground - 3, "o")
            c += stretch
            n = rnd.randint(2, 3 + (hard >= 3))
            gap(c, n)
            if rnd.random() < 0.5:
                put(c - 1, ground - 5, "=" * (n + 2))
                put(c, ground - 6, "o" * n)
            c += n
        # a checkpoint on solid ground once the section passes one: a flat landing of 6 cells
        if checks and c >= checks[0]:
            checks.pop(0)
            for r in range(ground, h):
                for x in range(c, min(w, c + 6)):
                    rows[r][x] = "#"
            for r in range(ground - 8, ground):
                for x in range(c, min(w, c + 6)):
                    rows[r][x] = "."
            put(c + 2, ground - 1, "C")
            c += 6
        if c == before:
            c += 1
    for r in range(ground, h):  # solid ground to the goal
        for x in range(w - 30, w):
            rows[r][x] = "#"
    put(w - 8, ground - 1, "F")
    return {"width": w, "height": h, "start": [4, ground - 1], "rows": ["".join(r) for r in rows],
            "effects": {"shadows": True, "zoom": "auto", "grade": grade, "grade_amount": 40}}


def levels():
    """Every zone's two acts, in the order the body is climbed: the second one shows the first one's art and ends with the boss."""
    out = []
    for k, (zone, sky, grade) in enumerate(ZONES):
        for act in (0, 1):
            lv = made_level(k, grade, act, level() if k == 0 and act == 0 else None)
            fight = boss(zone, mid=act == 0)
            if fight:
                lv = arena(lv)  # the first act ends with the mid-boss, the second with the zone's boss
            name = f"level_{k + 1}_{zone}_{act + 1}.json"
            with open(os.path.join(OUT, name), "w", encoding="utf-8") as f:
                json.dump(lv, f, indent=1)
                f.write("\n")
            if act == 0:
                entry = {"level": name, "sky": sky, "layers": layers(zone), "textures": textures(zone),
                         "intro": intro(zone), "music": music(zone), "boss": fight}
            else:
                entry = {"level": name, "same_art": True, "boss": fight}
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
        "dash": DASH,
        "enemies": ENEMIES,
        "physics": {"hitbox": [28, 66], "enemy_hitbox": [34, 30], "walk": 260, "run": 340, "accel": 40, "jump": 1050, "gravity": 50, "gravity_hold": 30, "fall_max": 1200},
        "sprites": {k: v for k, v in {
            "hero": {"players": SKINS, "skins": skins},
            "enemy": {k: v for k, v in {"walk": dict(thing("enemy_germ_walk", 8, 10) or {}, stride=70) or None,
                                         "squashed": thing("enemy_germ", 6, 8, {"from": 5, "frames": 1})}.items() if v} or None,
            "spore": {k: v for k, v in {"fly": thing("enemy_spore", 6, 10, {"from": 0, "frames": 5}),
                                         "pop": thing("enemy_spore", 6, 10, {"from": 5, "frames": 1})}.items() if v} or None,
            "spitter": {k: v for k, v in {"idle": thing("enemy_spitter", 6, 3, {"from": 0, "frames": 2}),
                                           "spit": thing("enemy_spitter", 6, 10, {"from": 2, "frames": 3}),
                                           "squashed": thing("enemy_spitter", 6, 8, {"from": 5, "frames": 1})}.items() if v} or None,
            "spit": thing("obj_spit", 1, 1),
            "roller": {k: v for k, v in {"roll": thing("enemy_bacteria", 6, 12, {"from": 0, "frames": 5}),
                                          "squashed": thing("enemy_bacteria", 6, 8, {"from": 5, "frames": 1})}.items() if v} or None,
            "hopper": {k: v for k, v in {"idle": thing("enemy_parasite", 6, 4, {"from": 4, "frames": 1}),
                                          "jump": thing("enemy_parasite", 6, 8, {"from": 1, "frames": 2}),
                                          "squashed": thing("enemy_parasite", 6, 8, {"from": 5, "frames": 1})}.items() if v} or None,
            "puffer": {k: v for k, v in {"idle": thing("enemy_fungus", 6, 3, {"from": 0, "frames": 2}),
                                          "puff": thing("enemy_fungus", 6, 10, {"from": 2, "frames": 3}),
                                          "squashed": thing("enemy_fungus", 6, 8, {"from": 5, "frames": 1})}.items() if v} or None,
            "splitter": {k: v for k, v in {"crawl": thing("enemy_cancer", 6, 6, {"from": 0, "frames": 4}),
                                            "split": thing("enemy_cancer", 6, 8, {"from": 4, "frames": 1}),
                                            "squashed": thing("enemy_cancer", 6, 8, {"from": 5, "frames": 1})}.items() if v} or None,
            "coin": thing("obj_vitamin", 4, 8),
            "checkpoint": {k: v for k, v in {"off": thing("obj_leukocyte", 4, 2, {"from": 0, "frames": 2}),
                                              "on": thing("obj_leukocyte", 4, 4, {"from": 2, "frames": 2})}.items() if v} or None,
            "goal": thing("obj_portal", 4, 6),
        }.items() if v},
        "screens": screens(),
        "credits": CREDITS,
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
