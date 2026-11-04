#!/usr/bin/env python3
"""Write the visible sample workload.

A deterministic synthetic image, published with the task so the candidate has
something to build a round trip against and to reason about the codec's cost
from.  It is NOT the graded draw: the graded measurement runs per function, on
inputs the verifier generates from its own seed and the candidate never sees.
"""
import math
import sys
import os

def ppm(path, w, h, seed):
    """P6 RGB, smooth gradients plus a low-frequency texture."""
    rows = []
    for y in range(h):
        row = bytearray()
        for x in range(w):
            base = 128.0 + 96.0 * math.sin(x / 37.0) * math.cos(y / 53.0)
            tex = 28.0 * math.sin((x + 3 * y) / 11.0) + 14.0 * math.cos(x / 5.0)
            nz = ((x * 1103515245 + y * 12345 + seed) >> 7) % 9 - 4
            r = int(max(0, min(255, base + tex + nz)))
            g = int(max(0, min(255, base - 0.5 * tex + nz)))
            b = int(max(0, min(255, base + 0.3 * tex - nz)))
            row += bytes((r, g, b))
        rows.append(bytes(row))
    with open(path, "wb") as f:
        f.write(b"P6\n%d %d\n255\n" % (w, h))
        for r in rows:
            f.write(r)

def main():
    # verifier copy: the draw is chosen here, not published with the task
    path = sys.argv[1] if len(sys.argv) > 1 else "/workload/heldout.ppm"
    w = int(sys.argv[2]) if len(sys.argv) > 2 else 768
    h = int(sys.argv[3]) if len(sys.argv) > 3 else 512
    seed = int(sys.argv[4]) if len(sys.argv) > 4 else 771117
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    ppm(path, w, h, seed)
    print("wrote", path)

if __name__ == "__main__":
    main()
