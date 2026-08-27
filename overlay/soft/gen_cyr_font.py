#!/usr/bin/env python3
"""Regenerate overlay/soft/font_cyr8x16.inc from consolefonts PSF (optional)."""
import gzip, os, struct, sys

SRC = "/usr/share/consolefonts/FullCyrSlav-Terminus16.psf.gz"
OUT = os.path.join(os.path.dirname(__file__), "font_cyr8x16.inc")

def parse_psf1(path):
  data = gzip.open(path, "rb").read()
  assert data[:2] == b"\x36\x04"
  mode, charsize = data[2], data[3]
  length = 512 if mode & 1 else 256
  glyphs = data[4 : 4 + length * charsize]
  utab = data[4 + length * charsize :] if mode & 2 else b""
  maps, i, gi = [], 0, 0
  while i + 1 < len(utab) and gi < length:
    cps = []
    while i + 1 < len(utab):
      cp = utab[i] | (utab[i + 1] << 8)
      i += 2
      if cp == 0xFFFF:
        break
      if cp != 0xFFFE:
        cps.append(cp)
    maps.append(cps)
    gi += 1
  return charsize, glyphs, maps

def main():
  if not os.path.exists(SRC):
    print("missing", SRC, file=sys.stderr)
    return 1
  charsize, glyphs, maps = parse_psf1(SRC)
  cyr = {}
  for gi, cps in enumerate(maps):
    for cp in cps:
      if 0x0400 <= cp <= 0x045F:
        cyr[cp] = gi
  entries = []
  for cp in sorted(cyr):
    gi = cyr[cp]
    g = glyphs[gi * charsize : (gi + 1) * charsize]
    entries.append((cp, g))
  lines = [
    "/* Cyrillic 8x16 from FullCyrSlav-Terminus16.psf — regenerate via gen_cyr_font.py */",
    "enum { SOFT_FONT_CYR_N = %d };" % len(entries),
    "static const unsigned short soft_font_cyr_cp[SOFT_FONT_CYR_N] = {",
    ",".join("0x%04X" % cp for cp, _ in entries),
    "};",
    "static const unsigned char soft_font_cyr_bits[SOFT_FONT_CYR_N][16] = {",
  ]
  for cp, g in entries:
    lines.append(
      "  {%s}, /* U+%04X */" % (",".join("0x%02x" % b for b in g), cp)
    )
  lines.append("};")
  open(OUT, "w").write("\n".join(lines) + "\n")
  print("wrote", OUT, len(entries), "glyphs")
  return 0

if __name__ == "__main__":
  raise SystemExit(main())
