#!/usr/bin/env python3
"""Render winconsole MOTECELL dump → PNG (full UI incl. status bar)."""
from __future__ import annotations

import struct
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


def load_font(size: int):
    for path in (
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
        "/usr/share/fonts/dejavu/DejaVuSansMono.ttf",
    ):
        try:
            return ImageFont.truetype(path, size)
        except Exception:
            pass
    return ImageFont.load_default()


def render(src: Path, dst: Path, cw: int = 9, ch: int = 16) -> None:
    data = src.read_bytes()
    nl = data.find(b"\n")
    if nl < 0 or not data.startswith(b"MOTECELL "):
        raise SystemExit("bad cell dump")
    hdr = data[:nl].decode("ascii")
    _, cols_s, rows_s = hdr.split()
    cols, rows = int(cols_s), int(rows_s)
    body = data[nl + 1 :]
    need = cols * rows * 10
    if len(body) < need:
        raise SystemExit(f"short dump: {len(body)} < {need}")
    im = Image.new("RGB", (cols * cw, rows * ch), (30, 30, 30))
    draw = ImageDraw.Draw(im)
    font = load_font(max(10, ch - 3))
    for i in range(cols * rows):
        off = i * 10
        cp = body[off] | (body[off + 1] << 8) | (body[off + 2] << 16) | (body[off + 3] << 24)
        fg = (body[off + 4], body[off + 5], body[off + 6])
        bg = (body[off + 7], body[off + 8], body[off + 9])
        x, y = (i % cols) * cw, (i // cols) * ch
        draw.rectangle([x, y, x + cw - 1, y + ch - 1], fill=bg)
        if cp > 32:
            chs = chr(cp) if cp < 0x110000 else "?"
            draw.text((x + 1, y), chs, fill=fg, font=font)
    dst.parent.mkdir(parents=True, exist_ok=True)
    im.save(dst)
    # Prove status row is not empty / not editor-bg
    last = []
    for x in range(cols):
        i = (rows - 1) * cols + x
        off = i * 10
        bg = (body[off + 7], body[off + 8], body[off + 9])
        last.append(bg)
    statusish = sum(1 for r, g, b in last if b > 120 and b > r + 20)
    print(f"  cells {cols}x{rows} → {dst}  status_blue_cells={statusish}/{cols}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("usage: render_cells.py in.cells out.png", file=sys.stderr)
        raise SystemExit(2)
    render(Path(sys.argv[1]), Path(sys.argv[2]))
