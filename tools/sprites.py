# Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com>
"""Cuts an animation strip (frames in one row on a transparent background,
like an image AI draws them) into an engine sprite sheet: equal frames, each
character aligned on its feet and centered, scaled to the game's size.

  uv run --with pillow --with numpy --with scipy tools/sprites.py IN.png OUT.png --height 80 [--frames N] [--scale S]

How: with --frames N the strip is cut in N near the N equal parts' borders,
each cut moved to the emptiest column within a tenth of a frame of it (an
arm may reach into the next frame's space); without it, the columns with no
visible pixel split it. In each frame only the character is kept: its
biggest blob and what lies near it (a puff of smoke at the hand stays, a
sliver of the next frame or a bullet already flying is dropped). Every
frame is scaled by the same factor: --scale, or --shell, which measures the
colored half of a capsule hero (its biggest strongly colored blob) in every
frame and scales the strip so its median height is that many pixels, since
an image AI draws each strip at its own size (take the "shell" printed for
the idle strip), and is put in a cell of
the widest frame's width, its bottom on the cell's bottom. It prints the cell
size and the frame count as JSON for the package's manifest.
"""
import argparse
import json
import sys

import numpy as np
from PIL import Image
from scipy import ndimage

ALPHA_MIN = 24      # a pixel more transparent than this is background
GAP_MIN = 6         # empty columns that separate two frames


def pieces(alpha: np.ndarray) -> list:
    """[(x0, x1)] of the runs of columns with visible pixels."""
    cols = (alpha > ALPHA_MIN).any(axis=0)
    runs, start, gap = [], None, 0
    for x, on in enumerate(cols):
        if on:
            if start is None:
                start = x
            gap = 0
            end = x
        elif start is not None:
            gap += 1
            if gap >= GAP_MIN:
                runs.append((start, end + 1))
                start = None
    if start is not None:
        runs.append((start, end + 1))
    return runs


def even(alpha: np.ndarray, n: int) -> list:
    """[(x0, x1)] of n frames: equal parts, each cut at the emptiest column near its border."""
    w = alpha.shape[1]
    load = (alpha > ALPHA_MIN).sum(axis=0)
    cuts = [0]
    for k in range(1, n):
        mid = round(k * w / n)
        reach = max(1, w // (n * 10))
        lo, hi = max(1, mid - reach), min(w - 1, mid + reach)
        cuts.append(lo + int(np.argmin(load[lo:hi + 1])))
    cuts.append(w)
    return [(cuts[i], cuts[i + 1]) for i in range(n)]


def keep_character(alpha: np.ndarray) -> np.ndarray:
    """The mask of a frame's character: its biggest blob and the blobs that touch its box, a little grown."""
    solid = alpha > ALPHA_MIN
    labels, n = ndimage.label(solid)
    if n <= 1:
        return solid
    sizes = ndimage.sum(solid, labels, range(1, n + 1))
    main = int(np.argmax(sizes)) + 1
    ys, xs = np.where(labels == main)
    h, w = ys.max() - ys.min() + 1, xs.max() - xs.min() + 1
    y0, y1 = ys.min() - h // 8, ys.max() + h // 8
    x0, x1 = xs.min() - w // 8, xs.max() + w // 8
    keep = np.zeros_like(solid)
    for lab, box in enumerate(ndimage.find_objects(labels), start=1):
        by, bx = box
        if by.start <= y1 and by.stop - 1 >= y0 and bx.start <= x1 and bx.stop - 1 >= x0:
            keep |= labels == lab
    return keep


def shell_height(rgba: np.ndarray, runs: list) -> float:
    """The median height, over the frames, of the biggest strongly colored blob (a capsule hero's colored half)."""
    rgb = rgba[..., :3].astype(np.float32) / 255.0
    mx, mn = rgb.max(axis=2), rgb.min(axis=2)
    sat = np.where(mx > 0, (mx - mn) / np.maximum(mx, 1e-6), 0)
    colored = (sat > 0.45) & (mx > 0.3) & (rgba[..., 3] > ALPHA_MIN)
    heights = []
    for x0, x1 in runs:
        labels, n = ndimage.label(colored[:, x0:x1])
        if not n:
            continue
        sizes = ndimage.sum(colored[:, x0:x1], labels, range(1, n + 1))
        by, _ = ndimage.find_objects(labels)[int(np.argmax(sizes))]
        heights.append(by.stop - by.start)
    return float(np.median(heights)) if heights else 0.0


def merge(runs: list, want: int) -> list:
    """Joins the closest neighbours until there are `want` frames."""
    runs = list(runs)
    while len(runs) > want:
        gaps = [runs[i + 1][0] - runs[i][1] for i in range(len(runs) - 1)]
        i = int(np.argmin(gaps))
        runs[i:i + 2] = [(runs[i][0], runs[i + 1][1])]
    return runs


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("dst")
    ap.add_argument("--height", type=int, required=True, help="the tallest frame's height in the game, in pixels")
    ap.add_argument("--frames", type=int, default=0, help="how many frames the strip has (0: as found)")
    ap.add_argument("--pad", type=int, default=2, help="empty pixels around each frame")
    ap.add_argument("--scale", type=float, default=0, help="game pixels per picture pixel (0: the tallest frame gets --height)")
    ap.add_argument("--shell", type=float, default=0, help="scale so the colored half measures this many game pixels (an image AI draws each strip at its own size)")
    args = ap.parse_args()

    img = Image.open(args.src).convert("RGBA")
    rgba = np.asarray(img)
    alpha = rgba[..., 3]
    runs = even(alpha, args.frames) if args.frames else pieces(alpha)
    # each frame keeps only its character
    clean = np.zeros_like(alpha)
    for x0, x1 in runs:
        mask = keep_character(alpha[:, x0:x1])
        clean[:, x0:x1] = np.where(mask, alpha[:, x0:x1], 0)
    alpha = clean
    rgba = rgba.copy()
    rgba[..., 3] = alpha
    img = Image.fromarray(rgba, "RGBA")
    # each frame's box: its columns, and the rows with visible pixels
    boxes = []
    for x0, x1 in runs:
        part = alpha[:, x0:x1] > ALPHA_MIN
        rows, cols = np.where(part.any(axis=1))[0], np.where(part.any(axis=0))[0]
        if not len(rows):
            sys.exit(f"{args.src}: frame {len(boxes) + 1} is empty")
        boxes.append((x0 + int(cols[0]), int(rows[0]), x0 + int(cols[-1]) + 1, int(rows[-1]) + 1))
    tallest = max(b[3] - b[1] for b in boxes)
    scale = args.scale or args.height / tallest
    shell = shell_height(rgba, runs)
    if args.shell and shell:
        scale = args.shell / shell
    widest = max(b[2] - b[0] for b in boxes)
    cell_w = int(np.ceil(widest * scale)) + 2 * args.pad
    cell_h = int(np.ceil(tallest * scale)) + 2 * args.pad
    # frames whose feet sit lower than others (a jump drawn along its arc) are
    # put on the cell's bottom all the same: the engine moves the character
    sheet = Image.new("RGBA", (cell_w * len(boxes), cell_h), (0, 0, 0, 0))
    for i, (x0, y0, x1, y1) in enumerate(boxes):
        part = img.crop((x0, y0, x1, y1))
        w, h = max(1, round((x1 - x0) * scale)), max(1, round((y1 - y0) * scale))
        part = part.resize((w, h), Image.LANCZOS)
        sheet.alpha_composite(part, (i * cell_w + (cell_w - w) // 2, cell_h - args.pad - h))
    sheet.save(args.dst)
    print(json.dumps({"file": args.dst.split("/")[-1], "frame": [cell_w, cell_h], "frames": len(boxes), "scale": round(scale, 5), "shell": round(shell * scale, 2)}))


if __name__ == "__main__":
    main()
