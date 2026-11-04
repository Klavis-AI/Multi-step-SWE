#!/usr/bin/env python3
"""Write a deterministic raw YUV420p clip for the end-to-end gates.

Regenerated inside the verifier from this generator rather than read from the
candidate's /app: the candidate controlled /app for the whole step and must not
choose its own input.

Usage: make_clip.py <out.yuv> [width] [height] [frames]
"""
import math
import os
import random
import sys


def main(argv):
    if len(argv) < 2:
        sys.stderr.write(__doc__)
        return 2
    out = argv[1]
    w = int(argv[2]) if len(argv) > 2 else 176
    h = int(argv[3]) if len(argv) > 3 else 144
    n = int(argv[4]) if len(argv) > 4 else 6

    rnd = random.Random(20260923)
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with open(out, "wb") as fh:
        for t in range(n):
            luma = bytearray(w * h)
            for y in range(h):
                for x in range(w):
                    v = (128 + 60 * math.sin((x + t * 7) / 23.0)
                         + 40 * math.cos((y - t * 5) / 19.0)
                         + 25 * math.sin((x + y + t * 3) / 11.0)
                         + rnd.randint(-6, 6))
                    luma[y * w + x] = max(0, min(255, int(v)))
            fh.write(luma)
            for plane in range(2):
                cw, ch = w // 2, h // 2
                chroma = bytearray(cw * ch)
                for y in range(ch):
                    for x in range(cw):
                        v = (128 + 30 * math.sin((x * 2 + t * 4) / 27.0 + plane)
                             + 20 * math.cos((y * 2 - t * 2) / 23.0)
                             + rnd.randint(-4, 4))
                        chroma[y * cw + x] = max(0, min(255, int(v)))
                fh.write(chroma)
    print("%s %d bytes" % (out, os.path.getsize(out)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
