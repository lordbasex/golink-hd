#!/usr/bin/env python3
# Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com>
"""Writes the API test games: package folders that use every part of the
package format, so one play checks them all, on the engine and in a host.

  examples/api-test      16:9, 4 players: every picture, every letter of the
                         level and every effect, with a dialog at each stop
                         saying what should be seen there
  examples/api-test-43   4:3, 2 players, a short level
  examples/api-test-916  9:16, 1 player, a level that climbs

The pictures are drawn here, in colors no built-in picture has, so a picture
the engine failed to load shows as the demo's. Only the standard library is
used. Run it from the repository: python3 examples/make-api-test.py
"""
import json
import os
import struct
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))


def png(path, w, h, px):
    """px[y][x] = (r, g, b, a)"""
    raw = b"".join(b"\0" + bytes(c for p in row for c in p) for row in px)

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


def canvas(w, h):
    return [[(0, 0, 0, 0) for _ in range(w)] for _ in range(h)]


def rect(px, x0, y0, w, h, c):
    for y in range(y0, y0 + h):
        for x in range(x0, x0 + w):
            if 0 <= y < len(px) and 0 <= x < len(px[0]):
                px[y][x] = c + (255,) if len(c) == 3 else c


def hexc(s):
    return tuple(int(s[i:i + 2], 16) for i in (1, 3, 5))


def shade(c, k):
    return tuple(max(0, min(255, int(v * k))) for v in c)


# One color per player: P1 teal, P2 orange, P3 violet, P4 lime, P5 pink, P6 sky, P7 gold, P8 red.
PLAYERS = ["#16b3a3", "#ff8a1f", "#8a5cf6", "#7ed321", "#ff4fa3", "#36a9ff", "#f5c400", "#e5383b"]


def hero():
    """16 x 24 frames (idle, walk, walk, jump), a row per player."""
    px = canvas(64, 24 * 8)
    for row, col in enumerate(PLAYERS):
        body, dark, skin = hexc(col), shade(hexc(col), 0.55), (255, 214, 170)
        for f in range(4):
            x, y = f * 16, row * 24
            rect(px, x + 4, y + 2, 8, 7, skin)               # head
            rect(px, x + 4, y + 2, 8, 2, dark)               # hair
            rect(px, x + 9, y + 5, 2, 2, (20, 20, 30))       # eye, facing right
            rect(px, x + 3, y + 9, 10, 8, body)              # shirt
            rect(px, x + 6, y + 11, 4, 4, (255, 255, 255))   # a white square on the chest
            arm = -3 if f == 3 else 0
            rect(px, x + 1, y + 10 + arm, 2, 6, body)
            rect(px, x + 13, y + 10 + arm, 2, 6, body)
            if f == 1:
                rect(px, x + 3, y + 17, 3, 7, dark), rect(px, x + 9, y + 17, 3, 5, dark)
            elif f == 2:
                rect(px, x + 4, y + 17, 3, 5, dark), rect(px, x + 10, y + 17, 3, 7, dark)
            elif f == 3:
                rect(px, x + 3, y + 17, 3, 5, dark), rect(px, x + 10, y + 17, 3, 5, dark)
            else:
                rect(px, x + 4, y + 17, 3, 7, dark), rect(px, x + 9, y + 17, 3, 7, dark)
    return 64, 24 * 8, px


def enemy():
    """48 x 16: walk, walk, squashed. A violet box robot."""
    px = canvas(48, 16)
    body, dark, eye = (150, 60, 200), (80, 30, 110), (255, 240, 80)
    for f in range(2):
        x = f * 16
        rect(px, x + 2, 3, 12, 10, body)
        rect(px, x + 4, 5, 3, 3, eye), rect(px, x + 9, 5, 3, 3, eye)
        rect(px, x + 7, 0, 2, 3, dark)
        rect(px, x + (3 if f == 0 else 5), 13, 3, 3, dark)
        rect(px, x + (10 if f == 0 else 8), 13, 3, 3, dark)
    rect(px, 32 + 1, 11, 14, 5, body)
    rect(px, 32 + 4, 12, 8, 1, eye)
    return 48, 16, px


def tiles():
    """64 x 16: ground top, ground, brick, one-way platform. Blue stone, gold bricks."""
    px = canvas(64, 16)
    stone, line = (60, 96, 170), (40, 64, 120)
    for t in (0, 1):
        x = t * 16
        rect(px, x, 0, 16, 16, stone)
        for yy in (0, 8):
            rect(px, x, yy + 7, 16, 1, line)
        rect(px, x + 7, 0, 1, 8, line), rect(px, x + 3, 8, 1, 8, line), rect(px, x + 11, 8, 1, 8, line)
    rect(px, 0, 0, 16, 4, (0, 220, 200))                 # the top: a teal strip
    rect(px, 0, 4, 16, 1, (0, 150, 140))
    brick, mortar = (230, 170, 30), (140, 90, 10)
    rect(px, 32, 0, 16, 16, brick)
    for yy in (3, 7, 11, 15):
        rect(px, 32, yy, 16, 1, mortar)
    for yy, xs in ((0, (40,)), (4, (36, 44)), (8, (40,)), (12, (36, 44))):
        for xx in xs:
            rect(px, xx, yy, 1, 3, mortar)
    rect(px, 48, 0, 16, 5, (255, 255, 255))              # one-way: white and red stripes
    for xx in range(48, 64, 4):
        rect(px, xx, 0, 2, 5, (220, 40, 60))
    rect(px, 48, 5, 16, 1, (90, 90, 100))
    return 64, 16, px


def coin():
    """64 x 16: a green gem spinning in four frames."""
    px = canvas(64, 16)
    widths = (10, 6, 2, 6)
    for f, w in enumerate(widths):
        x = f * 16 + 8 - w // 2
        for y in range(2, 14):
            k = 1 - abs(y - 8) / 7
            ww = max(1, int(w * k + 0.5))
            rect(px, f * 16 + 8 - ww // 2, y, ww, 1, (40, 220, 110) if y < 8 else (20, 150, 80))
        rect(px, x, 6, 1, 2, (220, 255, 230))
    return 64, 16, px


def checkpoint():
    """32 x 32: a flag not reached (grey), reached (green)."""
    px = canvas(32, 32)
    for f, col in enumerate(((140, 140, 150), (40, 220, 90))):
        x = f * 16
        rect(px, x + 3, 2, 2, 30, (230, 230, 240))
        rect(px, x + 5, 3, 9, 7, col)
    return 32, 32, px


def goal():
    """32 x 64: a portal of violet and teal stripes."""
    px = canvas(32, 64)
    for y in range(64):
        for x in range(32):
            inside = 4 <= x < 28 and 4 <= y
            edge = not inside and (x < 32 and y < 64)
            if edge:
                px[y][x] = (230, 230, 240, 255)
            elif inside:
                px[y][x] = ((120, 60, 220, 255) if ((x + y) // 4) % 2 else (0, 200, 190, 255))
    return 32, 64, px


def portrait():
    """48 x 48: Testy, a robot's face."""
    px = canvas(48, 48)
    rect(px, 0, 0, 48, 48, (30, 34, 50))
    rect(px, 8, 10, 32, 30, (170, 180, 200))
    rect(px, 22, 3, 4, 7, (170, 180, 200)), rect(px, 20, 0, 8, 4, (255, 80, 80))
    rect(px, 13, 17, 8, 8, (20, 20, 30)), rect(px, 27, 17, 8, 8, (20, 20, 30))
    rect(px, 15, 19, 3, 3, (0, 230, 210)), rect(px, 29, 19, 3, 3, (0, 230, 210))
    rect(px, 15, 31, 18, 3, (20, 20, 30))
    return 48, 48, px


def lut():
    """256 x 16: a teal and orange look (16 slices of 16 x 16; slice = blue, x = red, y = green)."""
    px = canvas(256, 16)
    for b in range(16):
        for g in range(16):
            for r in range(16):
                R, G, B = r * 17, g * 17, b * 17
                lum = (R * 3 + G * 6 + B) / 10
                k = lum / 255
                # shadows toward teal, lights toward orange
                R2 = R * 0.85 + 255 * 0.15 * k
                G2 = G * 0.92 + 20 * (1 - k)
                B2 = B * 0.85 + 60 * (1 - k)
                px[g][b * 16 + r] = (int(min(255, R2)), int(min(255, G2)), int(min(255, B2)), 255)
    return 256, 16, px


PICTURES = {"hero": hero, "enemy": enemy, "tiles": tiles, "coin": coin, "checkpoint": checkpoint,
            "goal": goal, "portrait": portrait, "lut": lut}


def put(rows, col, row, text):
    r = list(rows[row])
    for i, ch in enumerate(text):
        if 0 <= col + i < len(r):
            r[col + i] = ch
    rows[row] = "".join(r)


def write(name, manifest, level, pictures):
    out = os.path.join(HERE, name)
    os.makedirs(out, exist_ok=True)
    for key in pictures:
        w, h, px = PICTURES[key]()
        png(os.path.join(out, key + ".png"), w, h, px)
    manifest["pictures"] = {key: key + ".png" for key in pictures}
    with open(os.path.join(out, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2, ensure_ascii=False)
        f.write("\n")
    with open(os.path.join(out, "level.json"), "w", encoding="utf-8") as f:
        json.dump(level, f, indent=1, ensure_ascii=False)
        f.write("\n")


def dialog(col, en, es, pt):
    return {"column": col, "name": "Testy", "text": {"en": en, "es": es, "pt": pt}}


def main_game():
    # 46 rows: tall enough for the camera to zoom out to 0.5x (720 px); the
    # stops are laid out on the bottom 24 rows, shifted down by D.
    w, h = 240, 46
    D = h - 24
    rows = ["." * w for _ in range(h)]

    def put(rows, col, row, text):
        globals()["put"](rows, col, row + D, text)

    for r in range(20, 24):
        rows[r + D] = "#" * w
    # a gap to jump over and one with a one-way bridge
    for r in range(20, 24):
        put(rows, 44, r, "...")
        put(rows, 128, r, "....")
    put(rows, 127, 17, "======")
    # stop 1 (columns 12-40): gems, bricks, one-way platforms
    put(rows, 14, 16, "ooooo")
    put(rows, 14, 17, "=====")
    put(rows, 22, 15, "BBBB")
    put(rows, 22, 14, "oooo")
    put(rows, 30, 13, "====")
    put(rows, 30, 12, "oooo")
    # stop 2 (52-80): enemies, outlines and shadows
    for c in (58, 64, 70, 76):
        put(rows, c, 19, "E")
    put(rows, 66, 16, "ooo")
    # stop 3 (84-96): the checkpoint
    put(rows, 90, 19, "C")
    # stop 4 (100-126): lights in the dark
    put(rows, 104, 17, "BB")
    put(rows, 112, 15, "BB")
    put(rows, 120, 17, "BB")
    put(rows, 108, 18, "oo")
    put(rows, 116, 16, "oo")
    # stop 5 (134-170): waves, enemies to squash in the water's ripple
    put(rows, 150, 19, "E")
    put(rows, 160, 19, "E")
    put(rows, 154, 16, "oooo")
    # stop 6 (172-210): far apart for the zoom, a high ledge to climb
    put(rows, 186, 16, "====")
    put(rows, 192, 13, "====")
    put(rows, 198, 10, "====")
    put(rows, 198, 9, "oooo")
    # stop 7: the goal
    put(rows, 230, 19, "F")
    dialogs = [
        dialog(6, "API test! Your heroes wear this package's colors: P1 teal, P2 orange, P3 violet, P4 lime.",
               "¡Prueba de la API! Tus héroes usan los colores de este paquete: P1 turquesa, P2 naranja, P3 violeta, P4 lima.",
               "Teste da API! Seus heróis usam as cores deste pacote: P1 turquesa, P2 laranja, P3 violeta, P4 lima."),
        dialog(12, "Green gems spin in 4 frames. Gold bricks are solid; white and red planks let you jump up through them.",
               "Las gemas verdes giran en 4 cuadros. Los ladrillos dorados son sólidos; por las tablas blancas y rojas se pasa saltando.",
               "As gemas verdes giram em 4 quadros. Os tijolos dourados são sólidos; as tábuas brancas e vermelhas deixam passar pulando."),
        dialog(52, "Violet robots walk in 2 frames and flatten when you land on them. Every character has a dark outline and a shadow.",
               "Los robots violetas caminan en 2 cuadros y se aplastan si les caes encima. Cada personaje tiene contorno oscuro y sombra.",
               "Os robôs violeta andam em 2 quadros e achatam quando você cai em cima. Cada personagem tem contorno escuro e sombra."),
        dialog(84, "The grey flag turns green when you touch it: a checkpoint.",
               "La bandera gris se pone verde cuando la tocas: un punto de control.",
               "A bandeira cinza fica verde quando você a toca: um ponto de controle."),
        dialog(100, "It is dark here: you carry a warm light, and colored lights flicker. Bright spots glow (bloom).",
               "Aquí está oscuro: llevas una luz cálida y hay luces de colores que parpadean. Lo brillante resplandece (bloom).",
               "Aqui está escuro: você leva uma luz quente e luzes coloridas piscam. O que brilha resplandece (bloom)."),
        dialog(134, "From the ground down everything ripples, like heat or water (waves). Squash two more robots.",
               "Del suelo para abajo todo ondula, como calor o agua (olas). Aplasta dos robots más.",
               "Do chão para baixo tudo ondula, como calor ou água (ondas). Esmague mais dois robôs."),
        dialog(172, "Spread out: the camera zooms out when players are far apart. Sound is muffled, with an echo.",
               "Sepárense: la cámara se aleja cuando los jugadores están lejos. El sonido suena apagado y con eco.",
               "Separem-se: a câmera se afasta quando os jogadores estão longe. O som fica abafado e com eco."),
        dialog(214, "Every color passes through this package's table (teal shadows, orange lights). The portal ends the test.",
               "Todos los colores pasan por la tabla de este paquete (sombras turquesa, luces naranjas). El portal termina la prueba.",
               "Todas as cores passam pela tabela deste pacote (sombras turquesa, luzes laranja). O portal termina o teste."),
    ]
    lights = [
        {"x": 104, "y": 14 + D, "radius": 70, "color": "#ff4040", "flicker": 0},
        {"x": 110, "y": 12 + D, "radius": 80, "color": "#40ff70", "flicker": 24},
        {"x": 116, "y": 12 + D, "radius": 80, "color": "#4080ff", "flicker": 48},
        {"x": 122, "y": 14 + D, "radius": 70, "color": "#ffd040", "flicker": 12},
        {"x": 30, "y": 10 + D, "radius": 120, "color": "#ffffff", "flicker": 0},
        {"x": 154, "y": 13 + D, "radius": 110, "color": "#60e0ff", "flicker": 8},
        {"x": 199, "y": 7 + D, "radius": 120, "color": "#ff80ff", "flicker": 0},
        {"x": 230, "y": 15 + D, "radius": 140, "color": "#b080ff", "flicker": 16},
    ]
    level = {
        "width": w, "height": h, "start": [3, 19 + D], "rows": rows,
        "effects": {
            "darkness": 150, "player_light": 90, "player_light_color": "#ffe2b0", "lights": lights,
            "grade": "lut", "grade_amount": 200, "bloom": 140, "bloom_threshold": 200,
            "waves": {"row": 20 + D, "amplitude": 2, "wavelength": 40},
            "zoom": "auto", "outline": "#101018", "shadows": True,
            "lowpass": 150, "echo": 160, "dialogs": dialogs,
        },
    }
    manifest = {"format": 2, "title": "go-link HD: API test", "version": "1.0.0", "genre": "platformer",
                "players": 4, "screen": "16:9", "level": "level.json", "sky": ["#0b1030", "#3a2a60"]}
    write("api-test", manifest, level, list(PICTURES))


def small_43():
    w, h = 64, 24
    rows = ["." * w for _ in range(h)]
    for r in range(20, 24):
        rows[r] = "#" * w
    put(rows, 12, 17, "=====")
    put(rows, 12, 16, "ooooo")
    put(rows, 24, 19, "E")
    put(rows, 32, 19, "C")
    put(rows, 40, 16, "BBB")
    put(rows, 58, 19, "F")
    level = {"width": w, "height": h, "start": [3, 19], "rows": rows, "effects": {
        "grade": "sepia", "grade_amount": 180, "outline": "#202020", "shadows": True,
        "dialogs": [dialog(4, "A 4:3 screen (480 x 360) for 2 players.",
                           "Una pantalla 4:3 (480 x 360) para 2 jugadores.",
                           "Uma tela 4:3 (480 x 360) para 2 jogadores.")]}}
    manifest = {"format": 2, "title": "go-link HD: API test 4:3", "version": "1.0.0", "genre": "platformer",
                "players": 2, "screen": "4:3", "level": "level.json", "sky": ["#f0d080", "#f8f0d0"]}
    write("api-test-43", manifest, level, ["hero", "enemy", "tiles", "coin", "checkpoint", "goal", "portrait"])


def tall_916():
    w, h = 24, 64
    rows = ["." * w for _ in range(h)]
    for r in range(60, 64):
        rows[r] = "#" * w
    # platforms that climb, left and right
    for i, r in enumerate(range(56, 8, -4)):
        c = 3 if i % 2 == 0 else 13
        put(rows, c, r, "=======")
        put(rows, c + 2, r - 1, "oo")
    put(rows, 9, 7, "F")
    put(rows, 9, 8, "BBBB")
    level = {"width": w, "height": h, "start": [2, 59], "rows": rows, "effects": {
        "grade": "underwater", "grade_amount": 160,
        "waves": {"row": 40, "amplitude": 3, "wavelength": 30},
        "dialogs": [dialog(5, "A vertical 9:16 screen (360 x 640): climb to the portal.",
                           "Una pantalla vertical 9:16 (360 x 640): sube hasta el portal.",
                           "Uma tela vertical 9:16 (360 x 640): suba até o portal.")]}}
    manifest = {"format": 2, "title": "go-link HD: API test 9:16", "version": "1.0.0", "genre": "platformer",
                "players": 1, "screen": "9:16", "level": "level.json", "sky": ["#062040", "#1080a0"]}
    write("api-test-916", manifest, level, ["hero", "tiles", "coin", "goal", "portrait"])


if __name__ == "__main__":
    main_game()
    small_43()
    tall_916()
    print("examples/api-test, examples/api-test-43, examples/api-test-916")
