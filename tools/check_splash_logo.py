#!/usr/bin/env python3
"""Checks the ZEROPOINT plaque of a settled Boot ROM splash frame against the Boot ROM's own logo bitmap.

  check_splash_logo.py <frame.rgba> <ZPbootROM/build/logo_patch.bin>

The logo is 200 x 48, 4 bits per pixel, 100 bytes a row, drawn at (28, 104) with palette entries 13 (black) and 14 (grey); entry 15 is
see-through (the lightning shows). Every black or grey pixel of the bitmap must be that colour in the frame. A hash golden would not catch
a wrong picture it had recorded: the first rev-13 splash had the plaque's right edge overwritten by background tiles on the first row of
every band (34 pixels), and only this comparison says so. Prints the wrong pixels and exits 1 if there are any.
Use it only on a frame after the slide-in has finished (the rows are still moving before that).
"""
import sys

W, LX, LY, LW, LH, STRIDE = 256, 28, 104, 200, 48, 100


def color32(c16):
    ex = lambda v: (v << 3) | (v >> 2)
    return (ex((c16 >> 1) & 31), ex((c16 >> 6) & 31), ex((c16 >> 11) & 31), 255 if c16 & 1 else 0)


frame = open(sys.argv[1], "rb").read()
logo = open(sys.argv[2], "rb").read()
want = {13: color32(0x0001), 14: color32(0x8421)}      # black, grey: LOGO_COLORS16 in ppu/code/gen_mosaic.py
bad = []
for r in range(LH):
    for x in range(LW):
        b = logo[r * STRIDE + x // 2]
        idx = (b >> 4) if x % 2 == 0 else (b & 15)
        if idx in want:
            o = ((LY + r) * W + LX + x) * 4
            if tuple(frame[o:o + 4]) != want[idx]:
                bad.append((LX + x, LY + r))
if bad:
    rows = sorted(set(y for _, y in bad))
    print("%d logo pixels are wrong, in rows y = %s (x %d..%d)" % (len(bad), rows[:12], min(x for x, _ in bad), max(x for x, _ in bad)))
    sys.exit(1)
