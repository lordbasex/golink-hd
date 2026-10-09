# Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com>
"""Makes Azul from Rojo: every red_*.png sheet becomes blue_*.png with the
reds (the capsule's top, the shoes, the antibodies) turned blue, and the
rest (the white half, the face, the gloves, the outlines) kept, so both
heroes move exactly alike.

  uv run --with pillow --with numpy examples/antidoto/recolor.py [DIR]
"""
import pathlib
import sys

import numpy as np
from PIL import Image

RED_HUES = (340, 25)    # degrees: what counts as red (wrapping around 0)
BLUE_HUE = 212          # Azul's blue
MIN_SATURATION = 0.30   # skin, white and grey are left alone


def recolor(src: pathlib.Path, dst: pathlib.Path) -> None:
    img = Image.open(src).convert("RGBA")
    rgba = np.asarray(img).astype(np.float32) / 255.0
    rgb, alpha = rgba[..., :3], rgba[..., 3:]
    hsv = np.asarray(Image.fromarray((rgb * 255).astype(np.uint8)).convert("HSV")).astype(np.float32)
    hue = hsv[..., 0] * 360.0 / 255.0
    sat = hsv[..., 1] / 255.0
    lo, hi = RED_HUES
    red = ((hue >= lo) | (hue <= hi)) & (sat >= MIN_SATURATION)
    # shift the hue of red pixels to blue, keeping saturation and value
    hsv[..., 0] = np.where(red, BLUE_HUE * 255.0 / 360.0, hsv[..., 0])
    out = Image.fromarray(hsv.astype(np.uint8), "HSV").convert("RGB")
    out = np.concatenate([np.asarray(out), (alpha * 255).astype(np.uint8)], axis=2)
    Image.fromarray(out, "RGBA").save(dst)


def main() -> None:
    folder = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else pathlib.Path(__file__).parent / "source")
    for src in sorted(folder.glob("red_*.png")):
        dst = src.with_name("blue_" + src.name[len("red_"):])
        recolor(src, dst)
        print(dst.name)


if __name__ == "__main__":
    main()
