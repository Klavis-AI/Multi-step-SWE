#!/usr/bin/env python3
"""Round-robin a stratum<TAB>relpath<TAB>message list so the codec sample spreads
across every stratum instead of exhausting one directory first.

Reads candidate lines on stdin, writes the reordered list to stdout.
"""

import sys
from collections import OrderedDict


def main():
    buckets = OrderedDict()
    for line in sys.stdin:
        line = line.rstrip("\n")
        if not line:
            continue
        stratum = line.split("\t", 1)[0]
        buckets.setdefault(stratum, []).append(line)

    for k in buckets:
        buckets[k].sort()
    ordered = [buckets[k] for k in sorted(buckets)]

    i = 0
    while any(b for b in ordered):
        for b in ordered:
            if i < len(b):
                print(b[i])
        i += 1


if __name__ == "__main__":
    main()
