#!/usr/bin/env python3
"""Per-function speedups from a set of Simd `Test -m=a -ot=` reports.

    score_reports.py <funcs-file> <report> [<report> ...]

A scored function may report more than one row (the class-based APIs report one
row per configuration), so a function's ratio for one report is the mean over
its matching rows; with several reports the function's ratio is the MEDIAN
across reports.  A median, not a mean or a best-of: a host-contention spike
inflates one side of one run, and the median discards it, while best-of would
keep a spuriously fast reading and report it as speedup.

Prints "<row-or-func>\t<api_ms>\t<base_ms>\t<speedup>" per scored function, and
exits 1 if any scored function has no usable measurement in any report -- an
empty input set must not read like a clean pass.
"""
import os
import statistics
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import parse_report as pr


def ratios(report, funcs):
    """func -> (mean_ratio, mean_api_ms, mean_base_ms) or None if unmeasured."""
    _, rows = pr.parse(report)
    acc = {}
    for r in rows:
        name = r['_row']
        if name.startswith('Common,'):
            continue
        for f in funcs:
            if name == f or name.startswith(f + '['):
                try:
                    a, b = float(r.get('API', '0')), float(r.get('Base', '0'))
                except ValueError:
                    break
                if a > 0 and b > 0:
                    acc.setdefault(f, []).append((b / a, a, b))
                break
    out = {}
    for f, v in acc.items():
        out[f] = (sum(x[0] for x in v) / len(v),
                  sum(x[1] for x in v) / len(v),
                  sum(x[2] for x in v) / len(v))
    return out


def main():
    funcs = [l.strip() for l in open(sys.argv[1]) if l.strip()]
    reports = sys.argv[2:]
    per = [ratios(r, funcs) for r in reports]
    missing = []
    for f in funcs:
        vals = [p[f] for p in per if f in p]
        if not vals:
            missing.append(f)
            print('%s\t-\t-\tNaN' % f)
            continue
        sp = statistics.median(v[0] for v in vals)
        api = statistics.median(v[1] for v in vals)
        base = statistics.median(v[2] for v in vals)
        print('%s\t%.6f\t%.6f\t%.4f' % (f, api, base, sp))
    if missing:
        sys.stderr.write('unmeasured scored functions: %d of %d\n' % (len(missing), len(funcs)))
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
