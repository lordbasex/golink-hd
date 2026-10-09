# Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com>
"""Cuts the rubber-hose puppet's part sheets (source/red_parts_*.png and
Azul's blue_parts_*.png made from them by recolor.py, one row
of pieces each, from an image AI) into the engine's rig sheets in sprites/,
every piece in an equal cell with the pivot the engine's rig (src/rig.c)
expects:

  body  6 faces (normal, blink, shout, hurt, tired, yawn): bottom on the
        cell's bottom, centered
  hand  6 gloves (open, fist, gun, wave, up, palm): the wrist's cuff on the
        cell's left edge, the cuff's middle on the cell's middle row
  foot  4 shoes (flat, toe down, heel down, in the air): bottom on the
        cell's bottom, the ankle's opening a third of the way across

Every piece of a sheet is scaled by the same factor (its tallest piece gets
the height asked), so the faces, gloves and shoes keep their sizes.

  uv run --with pillow --with numpy --with scipy examples/antidoto/parts.py [HEIGHT_BODY HEIGHT_HAND HEIGHT_FOOT]
"""
import json
import os
import sys

import numpy as np
from PIL import Image
from scipy import ndimage

HERE = os.path.dirname(os.path.abspath(__file__))
ALPHA_MIN = 24
PAD = 2


def pieces(img, count):
    """The `count` biggest blobs of the sheet, left to right, as RGBA crops."""
    a = np.asarray(img)[..., 3] > ALPHA_MIN
    # join a piece's loose bits (a wave's motion lines) to it
    lab, n = ndimage.label(ndimage.binary_dilation(a, iterations=12))
    sizes = ndimage.sum(a, lab, range(1, n + 1))
    boxes = ndimage.find_objects(lab)
    keep = sorted((int(k) + 1 for k in np.argsort(sizes)[::-1][:count]), key=lambda k: boxes[k - 1][1].start)
    out = []
    for k in keep:
        ys, xs = boxes[k - 1]
        crop = np.asarray(img)[ys, xs].copy()
        crop[..., 3] = np.where((lab[ys, xs] == k) & a[ys, xs], crop[..., 3], 0)
        rows, cols = np.where(crop[..., 3] > ALPHA_MIN)
        out.append(Image.fromarray(crop[rows.min():rows.max() + 1, cols.min():cols.max() + 1]))
    return out


def scaled(parts, height):
    s = height / max(p.height for p in parts)
    return [p.resize((max(1, round(p.width * s)), max(1, round(p.height * s))), Image.LANCZOS) for p in parts]


def cuff_y(p):
    """The middle row of a glove's leftmost tenth (the cuff)."""
    a = np.asarray(p)[..., 3] > ALPHA_MIN
    rows = np.where(a[:, :max(2, p.width // 10)].any(axis=1))[0]
    return (rows.min() + rows.max()) / 2


def ankle_x(p):
    """The middle of a shoe's opening: the darkest pixels in its upper half, else its left third."""
    rgba = np.asarray(p).astype(int)
    top = rgba[: p.height // 2]
    dark = (top[..., 3] > ALPHA_MIN) & (top[..., :3].sum(axis=2) < 150)
    # the outline is dark too: only the inner dark blob (not touching the edge of the shoe's shape)
    lab, n = ndimage.label(dark)
    best = None
    for k in range(1, n + 1):
        ys, xs = np.where(lab == k)
        if len(xs) < 20 or ys.min() == 0 or xs.min() == 0 or xs.max() == p.width - 1:
            continue
        if best is None or len(xs) > best[0]:
            best = (len(xs), xs.mean())
    return best[1] if best else p.width / 3


def sheet(parts, cells, out):
    w, h = cells
    img = Image.new("RGBA", (w * len(parts), h))
    for i, (p, (x, y)) in enumerate(parts):
        img.alpha_composite(p, (i * w + x, y))
    img.save(out)


def cut(skin, hb, hh, hf):
    """One hero's three rig sheets; returns the manifest's "rig" parts."""
    made = {}
    src = lambda part: Image.open(os.path.join(HERE, "source", f"{skin}_parts_{part}.png")).convert("RGBA")
    out = lambda part: os.path.join(HERE, "sprites", f"{skin}_{part}.png")

    body = scaled(pieces(src("body"), 6), hb)
    w = max(p.width for p in body) + 2 * PAD
    sheet([(p, ((w - p.width) // 2, hb + PAD - p.height)) for p in body], (w, hb + PAD), out("body"))
    made["body"] = {"file": f"{skin}_body.png", "frame": [w, hb + PAD], "feet": PAD}

    hands = scaled(pieces(src("hands"), 6), hh)
    w = max(p.width for p in hands) + PAD
    half = max(max(cuff_y(p), p.height - cuff_y(p)) for p in hands)
    h = int(2 * half) + 2 * PAD
    sheet([(p, (0, round(h / 2 - cuff_y(p)))) for p in hands], (w, h), out("hands"))
    made["hand"] = {"file": f"{skin}_hands.png", "frame": [w, h]}

    feet = scaled(pieces(src("feet"), 4), hf)
    ax = [ankle_x(p) for p in feet]
    # the engine pins a shoe at a third of its width: every ankle goes there, and the cell holds
    # what lies left of the ankles (a third) and right of them (two thirds)
    behind = max(round(a) for a in ax)
    ahead = max(p.width - round(a) for p, a in zip(feet, ax))
    w = max(3 * behind, (3 * ahead + 1) // 2 + 1) + PAD
    left = w // 3
    sheet([(p, (left - round(a), hf + PAD - p.height)) for p, a in zip(feet, ax)], (w, hf + PAD), out("feet"))
    made["foot"] = {"file": f"{skin}_feet.png", "frame": [w, hf + PAD], "feet": PAD}
    return made


def main():
    hb, hh, hf = (int(v) for v in (sys.argv[1:4] if len(sys.argv) > 3 else (52, 22, 18)))
    os.makedirs(os.path.join(HERE, "sprites"), exist_ok=True)
    made = {}
    for skin in ("red", "blue"):
        if os.path.exists(os.path.join(HERE, "source", f"{skin}_parts_body.png")):
            made[skin] = cut(skin, hb, hh, hf)
    print(json.dumps(made))


if __name__ == "__main__":
    main()
