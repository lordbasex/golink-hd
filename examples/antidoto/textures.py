# Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com>
"""Turns a zone's tileset strip (4 tiles in a row from an image AI: floor
top, floor inside, floating platform, wall) into the engine's textures
(format 3's "textures", sides multiples of 16):

  ground_top  the floor top's upper band (its edge), 128 x 16, its cut
              bottom given a wavy inked edge: the engine lays it over the
              inside, so it meets any cell under or beside it
  ground      what lies under that band in the same tile, 128 x 48,
              seamless both ways
  platform    the platform's upper band, 128 x 16, its cut bottom given a
              wavy inked edge
  brick       a wall: the floor's inside darker (brick_top the floor's band
              darker, brick_bottom with each cell's bottom edged, for a
              wall that floats)

Every texture repeats across the level, so each one is made seamless: it is
cut a little bigger and the extra strip past one edge is faded into the
opposite edge, so the last column leads into the first (the bands only
across, the insides both ways); it is then resized as a repeating picture.
Cut straight from the middle of a tile, the floor showed a hard seam every
6 cells.

Each texture also gets its ends (ground_top_left, ..., platform_right): the
same picture with every cell's piece cut as the end of a run, its outer side
rounded off (a bulge per cell, so the cells of a column meet), inked and
shaded, the rest see-through. The engine draws them where a floor stops at a
pit or a platform ends; being cut from the same picture, they lead into the
next cell.

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


FEATHER = 2  # pixels softened on each side of the cut


def fold(a, axis):
    """
    A picture (premultiplied float array) that repeats along `axis`: its
    last 1 - 1/(1 + OVERLAP) is laid over its start, and every row switches
    from one to the other along a cut where the two are most alike (the
    cheapest path from top to bottom, moving at most a pixel a row), so no
    stroke shows twice. Only FEATHER pixels each side of the cut are mixed.
    """
    a = np.moveaxis(a, axis, 1)
    total = a.shape[1]
    keep = round(total / (1 + OVERLAP))
    over = total - keep
    ext, own = a[:, keep:keep + over], a[:, :over]
    diff = np.abs(ext - own).sum(axis=2)
    lo, hi = FEATHER + 1, over - FEATHER - 1  # the cut stays inside the overlap
    cost = np.full(diff.shape, np.inf)
    cost[0, lo:hi] = diff[0, lo:hi]
    step = np.zeros(diff.shape, dtype=np.int64)
    for r in range(1, diff.shape[0]):
        prev = cost[r - 1]
        best = np.stack([np.roll(prev, 1), prev, np.roll(prev, -1)])
        best[0, 0] = best[2, -1] = np.inf
        k = best.argmin(axis=0)
        cost[r, lo:hi] = diff[r, lo:hi] + best[k, np.arange(over)][lo:hi]
        step[r] = k - 1
    cut = np.zeros(diff.shape[0], dtype=np.int64)
    cut[-1] = int(np.argmin(cost[-1]))
    for r in range(diff.shape[0] - 1, 0, -1):
        cut[r - 1] = cut[r] + step[r, cut[r]]
    # left of the cut: what lies past the far edge (so the far edge leads in); right of it: the picture's own start
    x = np.arange(over)[None, :]
    t = np.clip((x - cut[:, None] + FEATHER + 0.5) / (2 * FEATHER + 1), 0, 1)[..., None]
    out = a[:, :keep].copy()
    out[:, :over] = ext * (1 - t) + own * t
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


def surface(img, box, band_h):
    """Where a tile's top begins in its middle half (an AI tile often has tufts higher up at its ends), a little above it."""
    x0, y0, x1, y1 = box
    a = np.asarray(img)[y0:y1, x0 + (x1 - x0) // 4: x1 - (x1 - x0) // 4, 3]
    solid = np.where((a > 128).mean(axis=1) > 0.5)[0]
    top = y0 + int(solid[0]) if len(solid) else y0
    return max(y0, top - band_h // 6)


def band(img, box, frac, size):
    """The upper `frac` of a tile, seamless across, scaled to size."""
    x0, y0, x1, y1 = box
    band_h = max(1, int((y1 - y0) * frac))
    y0 = surface(img, box, band_h)
    part = img.crop((x0, y0, x1, y0 + band_h))
    return wrap_resize(seamless(part, True, False), size, True, False)


def body(img, box, frac, size):
    """What lies under a tile's top band, seamless both ways, scaled to size."""
    x0, y0, x1, y1 = box
    band_h = max(1, int((y1 - y0) * frac))
    top = surface(img, box, band_h) + band_h
    return wrap_resize(seamless(img.crop((x0, top, x1, y1)), True, True), size, True, True)


def underside(tex, cell):
    """Each cell's bottom cut off by a wavy edge (a bump every 2 cells) inked like the art: a band cut straight, a floating wall."""
    a = np.asarray(tex).astype(np.float64)
    h, w = a.shape[:2]
    x = np.arange(w) + 0.5
    edge = cell - cell * (0.16 + 0.06 * np.sin(2 * np.pi * x / (2 * cell)))  # each cell's bottom, per column
    dist = edge[None, :] - (np.arange(h)[:, None] % cell + 0.5)  # pixels above the edge
    cover = np.clip(dist + 0.5, 0, 1)
    ink = np.clip(INK_W * cell - dist + 0.5, 0, 1) * cover
    shade = 1 - SHADE * np.clip(1 - dist / (SHADE_W * cell), 0, 1)
    out = a.copy()
    out[..., :3] = a[..., :3] * shade[..., None]
    out[..., :3] = out[..., :3] * (1 - ink[..., None]) + INK * ink[..., None]
    out[..., 3] = a[..., 3] * cover
    return Image.fromarray(np.clip(np.round(out), 0, 255).astype(np.uint8), "RGBA")


# the ink of the outlines (the tiles' own dark brown) and the ends' shapes, in a cell's share
INK = np.array([43, 20, 16], dtype=np.float64)
EDGE = 0.22      # how far into its cell an end's side lies
BULGE = 0.07     # its bulge in each cell
INK_W = 0.075    # the ink line's width
SHADE_W = 0.35   # the shading inside the edge
SHADE = 0.4      # how dark it gets at the edge


def end(tex, cell, side, round_top, round_bottom):
    """tex cut as a run's end in every cell: side "left" or "right"."""
    a = np.asarray(tex).astype(np.float64)
    h, w = a.shape[:2]
    y = (np.arange(h) % cell + 0.5) / cell  # 0..1 down the cell
    edge = EDGE + BULGE * np.sin(np.pi * y) * -1  # bulging outwards in the middle of each cell
    # rounded corners: the side moves in near the band's top (ground_top) or both ends (a platform)
    r = 0.5
    for on, d in ((round_top, y), (round_bottom, 1 - y)):
        if on:
            inside = np.clip(r - d, 0, None)
            edge = edge + r - np.sqrt(np.clip(r * r - inside * inside, 0, None))
    x = (np.arange(w) % cell + 0.5) / cell  # 0..1 across the cell
    if side == "right":
        x = 1 - x
    dist = (x[None, :] - edge[:, None]) * cell  # pixels inside the edge (negative: outside)
    cover = np.clip(dist + 0.5, 0, 1)
    ink = np.clip(INK_W * cell - dist + 0.5, 0, 1) * cover
    shade = 1 - SHADE * np.clip(1 - dist / (SHADE_W * cell), 0, 1)
    out = a.copy()
    out[..., :3] = a[..., :3] * shade[..., None]
    out[..., :3] = out[..., :3] * (1 - ink[..., None]) + INK * ink[..., None]
    out[..., 3] = a[..., 3] * cover
    return Image.fromarray(np.clip(np.round(out), 0, 255).astype(np.uint8), "RGBA")


def main():
    src, out = sys.argv[1], sys.argv[2]
    zone = os.path.basename(src)[len("tiles_"):-4]
    img = Image.open(src).convert("RGBA")
    b = tiles(img)
    os.makedirs(out, exist_ok=True)
    k = int(os.environ.get("RES", "1"))  # 2 (720p) or 3 (1080p): every texture that many times bigger
    cell = 16 * k
    made = {
        "ground_top": underside(band(img, b[0], 0.22, (128 * k, 16 * k)), cell),
        "ground": body(img, b[0], 0.22, (128 * k, 48 * k)),
        "platform": underside(band(img, b[2], 0.22, (128 * k, 16 * k)), 16 * k),
    }
    dark = lambda im: ImageEnhance.Brightness(im).enhance(0.75)
    made["brick_top"], made["brick"] = dark(made["ground_top"]), dark(made["ground"])
    made["brick_bottom"] = underside(made["brick"], cell)
    for kind, top, bottom in (("ground_top", True, False), ("ground", False, False), ("platform", True, True),
                              ("brick_top", True, False), ("brick", False, False), ("brick_bottom", False, True)):
        for side in ("left", "right"):
            made[f"{kind}_{side}"] = end(made[kind], cell, side, top, bottom)
    for kind, im in made.items():
        im.save(os.path.join(out, f"tex_{zone}_{kind}.png"))
        print(f"tex_{zone}_{kind}.png", im.size)


if __name__ == "__main__":
    main()
