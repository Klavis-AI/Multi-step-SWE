#!/usr/bin/env python3
"""Geometric-mean end-to-end ratio between two end-to-end timing files.

    e2e_ratio.py <base-times-file> <candidate-times-file>

Each file holds one "<config> <best_ms>" line per configuration. The ratio is
the geometric mean of candidate/base over the configurations both files carry,
so a single noisy configuration cannot decide it. Lower than 1 is better; the
caller applies the limit.
"""
import math
import sys


def read(path):
    out = {}
    for line in open(path):
        parts = line.split()
        if len(parts) >= 2:
            try:
                out[parts[0]] = float(parts[1])
            except ValueError:
                pass
    return out


def main(argv):
    a, b = read(argv[1]), read(argv[2])
    common = [k for k in a if k in b and a[k] > 0 and b[k] > 0]
    if not common:
        print("9.0")
        return 0
    logs = [math.log(b[k] / a[k]) for k in common]
    print("%.4f" % math.exp(sum(logs) / len(logs)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
