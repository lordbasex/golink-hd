# Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com>
"""Turns a zone's tileset strip (4 tiles in a row from an image AI: floor
top, floor inside, floating platform, wall) into the engine's textures
(format 3's "textures", sides multiples of 16):

  ground_top  the floor top's upper band (its edge), 128 x 16
  ground      the floor's inside, 96 x 96, repeated over the level
  platform    the platform's upper band, 128 x 16
  brick       the floor's inside, darker, 48 x 48

Every texture repeats across the level, so each one is made seamless: it is
cut a little bigger and the extra strip past one edge is faded into the
opposite edge, so the last column leads into the first (the bands only
across, the insides both ways); it is then resized as a repeating picture.
Cut straight from the middle of a tile, the floor showed a hard seam every
6 cells.

  [RES=2|3] uv run --with pillow --with numpy textures.py source/tiles_colon.png OUT_DIR
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


# the share of a cut faded into the opposite edge
OVERLAP = 0.3


def fold(a, axis):
    """A picture (premultiplied float array) whose last 1 - 1/(1 + OVERLAP) along `axis` is faded into its start."""
    a = np.moveaxis(a, axis, 1)
    total = a.shape[1]
    keep = round(total / (1 + OVERLAP))
    over = total - keep
    out = a[:, :keep].copy()
    t = (np.arange(over) + 0.5) / over
    t = (t * t * (3 - 2 * t))[None, :, None]  # smoothstep: the fade starts and ends gently
    out[:, :over] = a[:, keep:keep + over] * (1 - t) + a[:, :over] * t
    return np.moveaxis(out, 1, axis)


def seamless(part, across, down):
    a = np.asarray(part).astype(np.float64)
    a[..., :3] *= a[..., 3:] / 255  # premultiplied: a see-through pixel's color does not bleed
    if across:
        a = fold(a, 1)
    if down:
        a = fold(a, 0)
    alpha = a[..., 3:]
    a[..., :3] = np.where(alpha > 0, a[..., :3] * 255 / np.maximum(alpha, 1e-6), 0)
    return Image.fromarray(np.clip(np.round(a), 0, 255).astype(np.uint8), "RGBA")


def wrap_resize(im, size, across, down):
    """Resized as a repeating picture: laid 3 times each way it repeats, resized, the middle kept (no edge of its own)."""
    nx, ny = (3 if across else 1), (3 if down else 1)
    big = Image.new("RGBA", (im.width * nx, im.height * ny))
    for j in range(ny):
        for i in range(nx):
            big.paste(im, (i * im.width, j * im.height))
    big = big.resize((size[0] * nx, size[1] * ny), Image.LANCZOS)
    x, y = size[0] * (nx // 2), size[1] * (ny // 2)
    return big.crop((x, y, x + size[0], y + size[1]))


def band(img, box, frac, size):
    """The upper `frac` of a tile, seamless across, scaled to size."""
    x0, y0, x1, y1 = box
    part = img.crop((x0, y0, x1, y0 + max(1, int((y1 - y0) * frac))))
    return wrap_resize(seamless(part, True, False), size, True, False)


def inside(img, box, size):
    """A square from the middle of a tile, seamless both ways, scaled to size."""
    x0, y0, x1, y1 = box
    side = min(x1 - x0, y1 - y0) * 3 // 5
    cut = round(side * (1 + OVERLAP))
    cx, cy = (x0 + x1) // 2, (y0 + y1) // 2 + (y1 - y0) // 10
    left, top = min(cx - side // 2, x1 - cut), min(cy - side // 2, y1 - cut)  # the bigger cut stays inside the tile
    part = img.crop((left, top, left + cut, top + cut))
    return wrap_resize(seamless(part, True, True), size, True, True)


def main():
    src, out = sys.argv[1], sys.argv[2]
    zone = os.path.basename(src)[len("tiles_"):-4]
    img = Image.open(src).convert("RGBA")
    b = tiles(img)
    os.makedirs(out, exist_ok=True)
    k = int(os.environ.get("RES", "1"))  # 2 (720p) or 3 (1080p): every texture that many times bigger
    made = {
        "ground_top": band(img, b[0], 0.22, (128 * k, 16 * k)),
        "ground": inside(img, b[1], (96 * k, 96 * k)),
        "platform": band(img, b[2], 0.22, (128 * k, 16 * k)),
        "brick": ImageEnhance.Brightness(inside(img, b[1], (48 * k, 48 * k))).enhance(0.75),
    }
    for kind, im in made.items():
        im.save(os.path.join(out, f"tex_{zone}_{kind}.png"))
        print(f"tex_{zone}_{kind}.png", im.size)


if __name__ == "__main__":
    main()
