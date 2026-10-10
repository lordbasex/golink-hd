#!/usr/bin/env python3
# Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com>
"""Makes a zone's background layer repeat with no seam and sizes it for the game.

  uv run --with pillow --with numpy layers.py SRC.png DST.png HEIGHT

An image AI's wide background does not end where it starts: laid side by side
(the engine repeats a layer across the level) a hard vertical cut shows. Its
last part is laid over its start and every row switches from one to the other
along the cut where the two are most alike (textures.py's fold, with a wide
overlap and a soft join for a painting), then it is resized as a repeating
picture to HEIGHT pixels tall.
"""
import sys

import numpy as np
from PIL import Image

import textures

OVERLAP = 0.18  # a sixth of the picture laid over its start
FEATHER = 28    # pixels each side of the cut mixed (a painting's soft shading hides a wider join)


def main():
    src, dst, height = sys.argv[1], sys.argv[2], int(sys.argv[3])
    im = Image.open(src).convert("RGBA")
    textures.OVERLAP, textures.FEATHER = OVERLAP, FEATHER
    a = np.asarray(im).astype(np.float64)
    a[..., :3] *= a[..., 3:] / 255
    a = textures.fold(a, 1)
    alpha = a[..., 3:]
    a[..., :3] = np.where(alpha > 0, a[..., :3] * 255 / np.maximum(alpha, 1e-6), 0)
    out = Image.fromarray(np.clip(np.round(a), 0, 255).astype(np.uint8), "RGBA")
    width = round(out.width * height / out.height)
    textures.wrap_resize(out, (width, height), True, False).save(dst)


if __name__ == "__main__":
    main()
