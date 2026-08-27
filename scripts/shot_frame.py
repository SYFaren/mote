#!/usr/bin/env python3
"""Frame a mote screenshot into a clean 960x600 gallery image.

Keeps the full editor (esp. status bar). Avoids aggressive ImageMagick
-fuzz trim that eats the cyan status strip or leaves a tiny postage stamp.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

from PIL import Image


W, H = 960, 600
BG = (14, 14, 14)
PAD = 16
# Target fill: editor should occupy most of the canvas.
MAX_FILL = 0.94


def is_dark(rgb, lim=28):
    return sum(rgb) <= lim


def is_status_blue(rgb):
    r, g, b = rgb
    # VS Code–ish status cyan/blue used by mote themes.
    return b > 140 and b > r + 30 and g > 80


def content_bbox(im: Image.Image):
    px = im.load()
    w, h = im.size
    xs, ys = [], []
    step = 1 if max(w, h) < 1400 else 2
    for y in range(0, h, step):
        for x in range(0, w, step):
            if not is_dark(px[x, y]):
                xs.append(x)
                ys.append(y)
    if not xs:
        return (0, 0, w, h)
    return (min(xs), min(ys), max(xs) + 1, max(ys) + 1)


def extend_for_status(im: Image.Image, box):
    """If a cyan/blue status strip sits just below the bbox, include it."""
    px = im.load()
    w, h = im.size
    x0, y0, x1, y1 = box
    # Search a band below current bottom for a continuous blue row.
    y_lo = max(0, y1 - 4)
    y_hi = min(h, y1 + max(48, h // 8))
    best = y1
    for y in range(y_lo, y_hi):
        blue = 0
        samples = 0
        for x in range(x0, x1, max(1, (x1 - x0) // 80)):
            samples += 1
            if is_status_blue(px[x, y]):
                blue += 1
        if samples and blue / samples >= 0.45:
            best = max(best, y + 1)
    # Also scan whole width near bottom of image for status (console crop misses).
    for y in range(max(0, h - 80), h):
        blue = 0
        samples = 0
        for x in range(0, w, max(1, w // 100)):
            samples += 1
            if is_status_blue(px[x, y]):
                blue += 1
        if samples and blue / samples >= 0.35:
            # Expand horizontal to full status width
            sx0, sx1 = w, 0
            for x in range(w):
                if is_status_blue(px[x, y]):
                    sx0 = min(sx0, x)
                    sx1 = max(sx1, x + 1)
            if sx1 > sx0:
                x0 = min(x0, max(0, sx0 - 2))
                x1 = max(x1, min(w, sx1 + 2))
                best = max(best, y + 1)
    return (x0, y0, x1, best)


def frame(src: Path, dst: Path, nearest: bool = False) -> None:
    im = Image.open(src).convert("RGB")
    box = content_bbox(im)
    box = extend_for_status(im, box)
    x0, y0, x1, y1 = box
    # Margin around content so chrome/status aren't flush to the edge.
    x0 = max(0, x0 - 6)
    y0 = max(0, y0 - 6)
    x1 = min(im.size[0], x1 + 6)
    y1 = min(im.size[1], y1 + 6)
    crop = im.crop((x0, y0, x1, y1))
    cw, ch = crop.size
    if cw < 8 or ch < 8:
        crop = im
        cw, ch = crop.size

    # Scale to fill most of canvas; never stretch aspect.
    avail_w = W - 2 * PAD
    avail_h = H - 2 * PAD
    scale = min(avail_w / cw, avail_h / ch)
    nw = max(1, int(round(cw * scale)))
    nh = max(1, int(round(ch * scale)))
    if nearest:
        # Prefer integer upscale when it still fills reasonably; else NEAREST to fit.
        iscale = max(1, min(avail_w // cw, avail_h // ch))
        if iscale >= 2 or (cw * iscale >= avail_w * 0.7 and ch * iscale >= avail_h * 0.7):
            nw, nh = cw * iscale, ch * iscale
        crop = crop.resize((nw, nh), Image.NEAREST)
    else:
        filt = Image.NEAREST if (scale >= 1.5 and max(cw, ch) < 900) else Image.LANCZOS
        crop = crop.resize((nw, nh), filt)

    canvas = Image.new("RGB", (W, H), BG)
    canvas.paste(crop, ((W - nw) // 2, (H - nh) // 2))
    dst.parent.mkdir(parents=True, exist_ok=True)
    canvas.save(dst, optimize=True)
    print(f"  frame {src.name}: {cw}x{ch} → {nw}x{nh} in {W}x{H} → {dst}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("dst")
    ap.add_argument("--nearest", action="store_true", help="integer scale (DOS/VGA)")
    args = ap.parse_args()
    frame(Path(args.src), Path(args.dst), nearest=args.nearest)
    return 0


if __name__ == "__main__":
    sys.exit(main())
