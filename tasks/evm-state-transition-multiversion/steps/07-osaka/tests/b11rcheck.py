#!/usr/bin/env python3
"""Score one `b11r` case.

    b11rcheck.py --case <casedir> --block <block.json> --exit-status N

`b11r` writes `{"rlp": "0x...", "hash": "0x..."}` to `--output.block`.  Both are
graded: the hash alone would pass a tool that assembled the right header over
the wrong body, and the RLP alone would pass one that encoded the block
correctly but reported someone else's hash.
"""
import argparse, json, sys


def norm(s):
    return str(s).lower() if s is not None else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--case", required=True)
    ap.add_argument("--block", required=True)
    ap.add_argument("--exit-status", type=int, required=True)
    a = ap.parse_args()

    want = json.load(open(a.case + "/case.json"))["expect"]
    out = []
    out.append(("PASS", "exit", "") if a.exit_status == 0
               else ("FAIL", "exit", "tool exited %d" % a.exit_status))
    try:
        got = json.load(open(a.block))
        out.append(("PASS", "json", ""))
    except Exception as e:
        out.append(("FAIL", "json", "output block is not JSON: %s" % str(e)[:80]))
        for r in out:
            print("%s %s %s" % r)
        sys.exit(0)

    for key in ("hash", "rlp"):
        if key not in got:
            out.append(("FAIL", key, "absent from the output block"))
        elif norm(got[key]) != norm(want[key]):
            g, w = norm(got[key]), norm(want[key])
            detail = ("%s != %s" % (g, w) if key == "hash"
                      else "%d bytes vs %d, first difference at byte %d"
                           % (len(g) // 2, len(w) // 2,
                              next((i for i, (x, y) in enumerate(zip(g, w)) if x != y),
                                   min(len(g), len(w))) // 2))
            out.append(("FAIL", key, detail))
        else:
            out.append(("PASS", key, ""))

    for r in out:
        print("%s %s %s" % r)


if __name__ == "__main__":
    main()
