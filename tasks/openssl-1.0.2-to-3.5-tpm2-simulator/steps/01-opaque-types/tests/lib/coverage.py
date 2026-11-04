#!/usr/bin/env python3
"""Line coverage of the engine sources, from a --coverage build that has run.

    coverage.py <build-dir> <src-dir> [--min PERCENT]

<src-dir> is the directory whose files the bar applies to -- for this task
src/OsslCryptoEngine, the only code the migration touches.  gcov emits a .gcov
for every file that contributed code, headers and system headers included; only
files under <src-dir> are counted, so an inlined libc macro cannot flatter the
number.

Lines are gcov's own: "#####" is an instrumented line that never ran, a count
is one that did, "-" is not instrumented and is in neither total.  A source
file with no .gcda at all still appears, at 0 of 0 -- RSAKeySieve.c compiles to
nothing here because RSA_KEY_SIEVE is not defined, and a file that silently
stopped being compiled should look different from one that is simply empty.

Exit status is 1 if the aggregate is under --min, so this is usable as a gate.
"""
import os
import pathlib
import subprocess
import sys
import tempfile


def gcov_files(build, scratch):
    """Run gcov over every .gcda under build; return the .gcov paths."""
    gcda = sorted(str(p) for p in pathlib.Path(build).rglob("*.gcda"))
    if not gcda:
        sys.stderr.write("coverage: no .gcda under %s -- did the build use "
                         "--coverage, and did the driver run?\n" % build)
        sys.exit(2)
    for g in gcda:
        subprocess.run(["gcov", "-o", os.path.dirname(g), g],
                       cwd=scratch, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL, check=False)
    return sorted(pathlib.Path(scratch).glob("*.gcov"))


def tally(path):
    """(source-path, executed, instrumented) for one .gcov file."""
    src, hit, tot = None, 0, 0
    with open(path, errors="replace") as fh:
        for line in fh:
            f = line.split(":", 2)
            if len(f) < 3:
                continue
            count, lineno = f[0].strip(), f[1].strip()
            if lineno == "0":
                if f[2].startswith("Source:"):
                    src = f[2][len("Source:"):].strip()
                continue
            if count == "-":
                continue
            tot += 1
            if not count.startswith("#") and not count.startswith("="):
                hit += 1
    return src, hit, tot


def main(argv):
    if len(argv) < 3:
        sys.stderr.write(__doc__)
        return 2
    build, src = argv[1], os.path.realpath(argv[2])
    floor = float(argv[argv.index("--min") + 1]) if "--min" in argv else None

    rows = {}
    with tempfile.TemporaryDirectory() as scratch:
        for gc in gcov_files(build, scratch):
            path, hit, tot = tally(gc)
            if not path:
                continue
            path = os.path.realpath(path)
            if os.path.dirname(path) != src:
                continue
            # gcov writes one .gcov per (object, source) pair; a header pulled
            # into several objects therefore lands here more than once.
            h, t = rows.get(path, (0, 0))
            rows[path] = (max(h, hit), max(t, tot))
    for f in sorted(pathlib.Path(src).glob("*.c")):
        rows.setdefault(os.path.realpath(str(f)), (0, 0))

    hits = sum(h for h, _ in rows.values())
    tots = sum(t for _, t in rows.values())
    width = max(len(os.path.basename(p)) for p in rows)
    for p in sorted(rows, key=lambda p: -rows[p][1]):
        h, t = rows[p]
        pct = "%6.1f%%" % (100.0 * h / t) if t else "     --"
        print("  %-*s  %s  %5d/%-5d" % (width, os.path.basename(p), pct, h, t))
    pct = 100.0 * hits / tots if tots else 0.0
    print("  %-*s  %6.1f%%  %5d/%-5d" % (width, "TOTAL", pct, hits, tots))

    if floor is not None and pct < floor:
        sys.stderr.write("coverage: %.1f%% is under the %.1f%% bar\n"
                         % (pct, floor))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
