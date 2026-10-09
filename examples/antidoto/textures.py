# Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com>
"""Turns a zone's tileset strip (4 tiles in a row from an image AI: floor
top, floor inside, floating platform, wall) into the engine's textures
(format 3's "textures", sides multiples of 16):

  ground_top  the floor top's upper band (its edge), 128 x 16
  ground      the floor's inside, 96 x 96, repeated over the level
  platform    the platform's upper band, 128 x 16
  brick       the floor's inside, darker, 48 x 48

  uv run --with pillow --with numpy textures.py source/tiles_colon.png OUT_DIR
"""
import os
import sys

import numpy as np
from PIL import Image, ImageEnhance


def tiles(img):
    """The 4 tiles' boxes, split by the columns with no visible pixel."""
    a = np.asarray(img)[..., 3] > 24
    cols = a.any(axis=0)
    runs, start = [], None
    for x, on in enumerate(list(cols) + [False]):
        if on and start is None:
            start = x
        elif not on and start is not None:
            if x - start > 40:
                runs.append((start, x))
            start = None
    boxes = []
    for x0, x1 in runs[:4]:
        rows = np.where(a[:, x0:x1].any(axis=1))[0]
        boxes.append((x0, int(rows[0]), x1, int(rows[-1]) + 1))
    if len(boxes) < 3:
        sys.exit(f"found {len(boxes)} tiles, expected 4")
    return boxes


def band(img, box, frac, size):
    """The upper `frac` of a tile, scaled to size."""
    x0, y0, x1, y1 = box
    part = img.crop((x0, y0, x1, y0 + max(1, int((y1 - y0) * frac))))
    return part.resize(size, Image.LANCZOS)


def inside(img, box, size):
    """A square from the middle of a tile, scaled to size."""
    x0, y0, x1, y1 = box
    side = min(x1 - x0, y1 - y0) * 3 // 5
    cx, cy = (x0 + x1) // 2, (y0 + y1) // 2 + (y1 - y0) // 10
    return img.crop((cx - side // 2, cy - side // 2, cx + side // 2, cy + side // 2)).resize(size, Image.LANCZOS)


def main():
    src, out = sys.argv[1], sys.argv[2]
    zone = os.path.basename(src)[len("tiles_"):-4]
    img = Image.open(src).convert("RGBA")
    b = tiles(img)
    os.makedirs(out, exist_ok=True)
    made = {
        "ground_top": band(img, b[0], 0.22, (128, 16)),
        "ground": inside(img, b[1], (96, 96)),
        "platform": band(img, b[2], 0.22, (128, 16)),
        "brick": ImageEnhance.Brightness(inside(img, b[1], (48, 48))).enhance(0.75),
    }
    for kind, im in made.items():
        im.save(os.path.join(out, f"tex_{zone}_{kind}.png"))
        print(f"tex_{zone}_{kind}.png", im.size)


if __name__ == "__main__":
    main()
