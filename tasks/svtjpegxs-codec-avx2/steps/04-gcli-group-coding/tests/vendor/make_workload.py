#!/usr/bin/env python3
"""Deterministic raw pictures for the end-to-end gate.

Used by the verifier to regenerate its own inputs inside the verifier container;
the candidate's /app is never read for them.  The scored numbers come from the
per-function harness -- this input only drives the pass/fail digest check and
the non-regression guard.

    make_workload.py <out.yuv> <w> <h> <frames> <yuv422|yuv420|yuv444> <depth>

Frame layout is planar, chroma planes last, samples little-endian above 8 bits.
"""
import os
import random
import sys

SEED = 0x5EED1234ABCDEF01


def plane_sizes(fmt, w, h):
    if fmt == "yuv422":
        return [(w, h), (w // 2, h), (w // 2, h)]
    if fmt == "yuv420":
        return [(w, h), (w // 2, h // 2), (w // 2, h // 2)]
    if fmt == "yuv444":
        return [(w, h), (w, h), (w, h)]
    raise SystemExit("unknown colour format " + fmt)


def main(out, w, h, frames, fmt, depth):
    rnd = random.Random(SEED)
    planes = plane_sizes(fmt, w, h)
    if depth > 8:
        mask = (1 << depth) - 1
        one = rnd.randbytes(2 * planes[0][0] * planes[0][1])
    with open(out, "wb") as f:
        for _ in range(frames):
            for (pw, ph) in planes:
                n = pw * ph
                if depth > 8:
                    raw = rnd.randbytes(2 * n)
                    # Keep samples inside the declared bit depth.
                    buf = bytearray(raw)
                    for i in range(0, len(buf), 2):
                        v = (buf[i] | (buf[i + 1] << 8)) & mask
                        buf[i] = v & 0xFF
                        buf[i + 1] = (v >> 8) & 0xFF
                    f.write(buf)
                else:
                    f.write(rnd.randbytes(n))
    print("workload:", out, os.path.getsize(out), "bytes")


if __name__ == "__main__":
    main(sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]), sys.argv[5], int(sys.argv[6]))
