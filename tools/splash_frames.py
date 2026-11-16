#!/usr/bin/env python3
"""Convert dump_splash's raw .rgba frames to PNG and report fully black rows.
usage: splash_frames.py <frame.rgba> [<frame.rgba>...]   (writes <frame>.png next to each)"""
import sys
from PIL import Image

for path in sys.argv[1:]:
    raw = open(path, "rb").read()
    im = Image.frombytes("RGBA", (256, 256), raw)
    out = path.rsplit(".", 1)[0] + ".png"
    im.convert("RGB").save(out)
    rows = [y for y in range(256) if not any(im.getpixel((x, y))[:3] != (0, 0, 0) for x in range(256))]
    print(f"{out}: black rows {rows if rows else 'none'}")
