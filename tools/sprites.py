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
frame is scaled by the same factor (--separate first splits frames that
reach into each other's columns by their blobs): --scale, or --shell, which measures the
colored half of a capsule hero (its biggest strongly colored blob) in every
frame and scales the strip so its median height is that many pixels, since
an image AI draws each strip at its own size (take the "shell" printed for
the idle strip), and is put in a cell of
the widest reach's width, its bottom on the cell's bottom and its body (the
biggest strongly colored blob) on the cell's middle, so the body stays still
while the limbs move. It prints the cell
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


def separate(rgba: np.ndarray, n: int) -> np.ndarray:
    """The strip redrawn with its n frames side by side, none overlapping: for strips whose frames
    reach into each other's columns or touch (a spray, a burst), where any vertical cut would slice
    one of them. The blobs are thinned until n big cores stand apart (thin bridges between frames
    break first); every piece then goes with its frame (below), and far specks are dropped."""
    alpha = rgba[..., 3]
    solid = alpha > ALPHA_MIN
    for thin in range(0, 40, 2):
        core = ndimage.binary_erosion(solid, iterations=thin) if thin else solid
        labels, count = ndimage.label(core)
        if count < n:
            continue
        sizes = ndimage.sum(core, labels, range(1, count + 1))
        order = np.argsort(sizes)[::-1]
        # n cores of a like size (a speck or a lone shoe is not a frame), clearly bigger than the rest
        top = sizes[order[:n]]
        if top[-1] * 4 >= np.median(top) and (count == n or top[-1] > 4 * sizes[order[n]]):
            break
    else:
        sys.exit(f"could not find {n} frames in the strip")
    cores = sorted(order[:n], key=lambda i: ndimage.find_objects(labels)[i][1].start)
    marks = np.zeros_like(labels)
    for k, c in enumerate(cores):
        marks[labels == c + 1] = k + 1
    # a piece drawn in one stroke (its visible pixels joined; the faint haze around a drawing would join
    # everything) holding one core goes whole with that frame; a piece holding two (frames touching) is
    # split pixel by pixel, each to its nearest core
    pieces_of, count = ndimage.label(alpha > ALPHA_MIN)
    frame_of = np.zeros_like(labels)
    loose = []
    dist, (iy, ix) = ndimage.distance_transform_edt(marks == 0, return_indices=True)
    for k, box in enumerate(ndimage.find_objects(pieces_of), start=1):
        here = pieces_of[box] == k
        cores_in = np.unique(marks[box][here])
        cores_in = cores_in[cores_in > 0]
        if len(cores_in) == 1:
            frame_of[box][here] = int(cores_in[0])
        elif len(cores_in) > 1:
            frame_of[box][here] = marks[iy[box], ix[box]][here]
        else:
            loose.append((int(here.sum()), k, box))
    # a loose piece (a drop of a sneeze, a loose arm, stars over a head) goes with the frame whose drawing
    # is nearest it (the spray it flies from, not the next frame's body); a speck far from every frame is dropped
    reach = alpha.shape[1] / n * 0.6
    near = [ndimage.distance_transform_edt(frame_of != f) for f in range(1, n + 1)]
    for _, k, box in sorted(loose, reverse=True):
        here = pieces_of[box] == k
        gaps = [d[box][here].min() for d in near]
        f = int(np.argmin(gaps))
        if gaps[f] <= reach:
            frame_of[box][here] = f + 1
    # the faint haze takes the frame of the nearest visible pixel next to it
    taken = frame_of > 0
    dist, (iy, ix) = ndimage.distance_transform_edt(~taken, return_indices=True)
    faint = (alpha > 0) & ~taken & (dist <= 3)
    frame_of[faint] = frame_of[iy, ix][faint]
    crops = []
    for k in range(1, n + 1):
        mask = frame_of == k
        cols = np.where(mask.any(axis=0))[0]
        part = rgba[:, cols[0]:cols[-1] + 1].copy()
        part[..., 3] = np.where(mask[:, cols[0]:cols[-1] + 1], part[..., 3], 0)
        crops.append(part)
    slot = max(c.shape[1] for c in crops) + 2 * GAP_MIN
    out = np.zeros((alpha.shape[0], slot * n, 4), dtype=rgba.dtype)
    for k, c in enumerate(crops):
        x = k * slot + (slot - c.shape[1]) // 2
        out[:, x:x + c.shape[1]] = c
    return out


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


def body_center(rgba: np.ndarray, x0: int, x1: int) -> float:
    """The x of a frame's body: the center of its biggest strongly colored blob (a capsule's colored half, a virus's body)."""
    rgb = rgba[:, x0:x1, :3].astype(np.float32) / 255.0
    mx, mn = rgb.max(axis=2), rgb.min(axis=2)
    sat = np.where(mx > 0, (mx - mn) / np.maximum(mx, 1e-6), 0)
    colored = (sat > 0.45) & (mx > 0.3) & (rgba[:, x0:x1, 3] > ALPHA_MIN)
    labels, n = ndimage.label(colored)
    if not n:
        return (x1 - x0) / 2.0
    sizes = ndimage.sum(colored, labels, range(1, n + 1))
    _, bx = ndimage.find_objects(labels)[int(np.argmax(sizes))]
    return (bx.start + bx.stop) / 2.0


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
    ap.add_argument("--shell-frames", type=int, default=0, help="measure the colored half only in the first N frames (a character that breaks apart, like a dissolve)")
    ap.add_argument("--separate", action="store_true", help="with --frames: frames reach into each other's columns (a spray, a burst); split them by blobs, not by columns")
    args = ap.parse_args()

    img = Image.open(args.src).convert("RGBA")
    rgba = np.asarray(img)
    if args.separate and args.frames:
        rgba = separate(rgba, args.frames)
    alpha = rgba[..., 3]
    runs = even(alpha, args.frames) if args.frames else pieces(alpha)
    # each frame keeps only its character (separated frames hold only their own pieces already)
    clean = np.zeros_like(alpha)
    for x0, x1 in runs:
        mask = alpha[:, x0:x1] > 0 if args.separate and args.frames else keep_character(alpha[:, x0:x1])
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
    shell = shell_height(rgba, runs[:args.shell_frames] if args.shell_frames else runs)
    if args.shell and shell:
        scale = args.shell / shell
    # every frame is placed by its body's center (not its box's): arms and
    # legs swing, the body does not jump left and right from frame to frame
    centers = [x0 + body_center(rgba, x0, x1) for (x0, x1) in runs]
    reach = max(max(c - b[0], b[2] - c) for c, b in zip(centers, boxes))
    cell_w = int(np.ceil(2 * reach * scale)) + 2 * args.pad
    cell_h = int(np.ceil(tallest * scale)) + 2 * args.pad
    # frames whose feet sit lower than others (a jump drawn along its arc) are
    # put on the cell's bottom all the same: the engine moves the character
    sheet = Image.new("RGBA", (cell_w * len(boxes), cell_h), (0, 0, 0, 0))
    for i, (x0, y0, x1, y1) in enumerate(boxes):
        part = img.crop((x0, y0, x1, y1))
        w, h = max(1, round((x1 - x0) * scale)), max(1, round((y1 - y0) * scale))
        part = part.resize((w, h), Image.LANCZOS)
        left = round(cell_w / 2 - (centers[i] - x0) * scale)
        sheet.alpha_composite(part, (i * cell_w + left, cell_h - args.pad - h))
    sheet.save(args.dst)
    print(json.dumps({"file": args.dst.split("/")[-1], "frame": [cell_w, cell_h], "frames": len(boxes), "scale": round(scale, 5), "shell": round(shell * scale, 2)}))


if __name__ == "__main__":
    main()
