#!/usr/bin/env python3
"""Record the behaviour suite against a built ise tree.

Runs the whole catalogue several times over and keeps only the items that came
out identical every time. An expectation that is not reproducible against the
baseline itself has no business being scored against anything else, so the
unstable ones are written out separately rather than quietly averaged away.

  python3 record.py --bindir /app/build/linux/debug/bin --repeat 3 \
                    --pin <commit> --out /authoring/fixtures

This script ships inside every step's tests/, beside the fixtures it wrote, so
that the one question a reader of a recorded expectation always has -- "what
produced this, and can I produce it again?" -- is answered without leaving the
package. Run it against a build of the untouched baseline at the pinned commit
and it rewrites expected.json; the provenance block in that file records what
it was run against and with which normaliser, so a fixture and a harness that
have drifted apart can be told from a fixture and a harness that agree.
"""
import argparse
import hashlib
import json
import os
import re
import sys
import time
from collections import Counter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from isefix import catalog                     # noqa: E402
from isefix.engine import run_session          # noqa: E402


def one_pass(only=None):
    """Run every session once; return {item_id: transcript}."""
    out = {}
    for session in catalog.sessions():
        if only and not any(session["id"].startswith(p) for p in only):
            continue
        started = time.time()
        got = run_session(session, BINDIR)
        out.update(got)
        print("    %-24s %6.1fs  %3d items" % (
            session["id"], time.time() - started, len(got)), file=sys.stderr)
        sys.stderr.flush()
    return out


def stratum_index():
    """item id -> (stratum, adversarial) for every declared step."""
    index = {}
    for session, step in catalog.all_steps():
        index[step["id"]] = (step.get("stratum", "core"), bool(step.get("adv")))
    for session in catalog.sessions():
        for suffix, stratum in (("stdout", "core"), ("log", "core")):
            index.setdefault("%s/%s" % (session["id"], suffix), (stratum, False))
        for extra in session.get("extra_items", []):
            index.setdefault("%s/%s" % (session["id"], extra["suffix"]), ("state", True))
    return index


# A recorded expectation carrying a wall-clock reading expires: it matches on
# the day it was taken and fails later, in a way nobody reading the failure
# will connect to the calendar. Every timestamp these programs emit is erased
# by normalize.py long before it reaches a fixture -- so this scan must come
# back empty, and it is run and reported rather than assumed, because "the
# normaliser covers that" is a belief that stops being true one regex at a
# time. See --selftest for the same scan run against text that should match.
#
# The last alternative -- a bare four-digit year -- over-reports on purpose,
# and cannot do otherwise: "nearest=2000" is a number of milliseconds and
# "2000" in a copyright line is a year, and no regex can tell those apart. So
# a non-empty list here is a list of things to account for, not a list of
# faults. What settles it is not a scan at all: record twice, once with the
# process tree's wall clock moved years forward, and diff. An item that is
# byte-identical under two different years has no year in it, whatever its
# digits look like. CLOCKSHIFT_SECONDS below is how that second recording
# announces itself, so each expected.json says which clock it was taken on.
RE_WALLCLOCK = re.compile(
    r"\b(?:19|20)\d{2}[-/]\d{1,2}[-/]\d{1,2}\b"
    r"|\b\d{1,2}:\d{2}:\d{2}\b"
    r"|\b(?:Mon|Tue|Wed|Thu|Fri|Sat|Sun), +\d{1,2} "
    r"|\b(?:19|20)\d{2}\b")

SELFTEST = [
    ("2025-09-16", True),
    ("Date: Tue, 16 Sep 2025 09:41:02 GMT", True),
    ("09:41:02", True),
    ("[TS] server started.", False),
    ("DATETIME", False),
    ("(PID|T0) (thread: T1)", False),
    ("!\"#$%&'()*+,-./0123456789:;<=>?@", False),
    # The bare-year rule, in both directions. A year standing on its own is
    # meant to be caught; a millisecond count that happens to fall between
    # 1900 and 2099 is caught too, and is meant to be, because the cost of
    # reading one more line of a report is nothing against the cost of a
    # fixture that expires. These two cases are here so that the day someone
    # narrows the rule to quieten the report, the selftest says what was lost.
    ("Copyright 2026 ise", True),
    ("nearest=2000 fired=[tick]", True),
    ("wire=2070 unpack=1", True),
    ("size 1899: ok", False),
]


def selftest():
    """Check the clock scan against answers already known, then report."""
    bad = [(text, want) for text, want in SELFTEST
           if bool(RE_WALLCLOCK.search(text)) != want]
    for text, want in SELFTEST:
        print("   %-4s %-42r expected %s" % (
            "ok" if (bool(RE_WALLCLOCK.search(text)) == want) else "BAD",
            text, want), file=sys.stderr)
    return bad


def digests(paths):
    out = {}
    for path in paths:
        with open(path, "rb") as handle:
            out[os.path.basename(path)] = hashlib.sha256(handle.read()).hexdigest()
    return out


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--bindir", default="/app/build/linux/debug/bin")
    parser.add_argument("--out", default="/authoring/fixtures")
    parser.add_argument("--repeat", type=int, default=3)
    parser.add_argument("--pin", default="",
                        help="commit of the baseline tree this was built from")
    parser.add_argument("--only", nargs="*")
    parser.add_argument("--selftest", action="store_true",
                        help="run the clock scan against known answers and stop")
    args = parser.parse_args()

    if args.selftest:
        bad = selftest()
        print("selftest: %s" % ("FAILED" if bad else "ok"), file=sys.stderr)
        return 1 if bad else 0
    if selftest():
        print("the clock scan does not agree with its own known answers; "
              "nothing was recorded", file=sys.stderr)
        return 1

    global BINDIR
    BINDIR = args.bindir

    passes = []
    for index in range(args.repeat):
        print("=== pass %d/%d" % (index + 1, args.repeat), file=sys.stderr)
        passes.append(one_pass(args.only))

    keys = set()
    for result in passes:
        keys |= set(result)

    stable, unstable = {}, {}
    for key in sorted(keys):
        values = [result.get(key) for result in passes]
        if all(value == values[0] for value in values) and values[0] is not None:
            stable[key] = values[0]
        else:
            unstable[key] = [
                {"pass": i, "len": (len(v) if v is not None else None),
                 "full": v}
                for i, v in enumerate(values)]

    index = stratum_index()
    strata = Counter()
    adversarial = 0
    for key in stable:
        stratum, adv = index.get(key, ("core", False))
        strata[stratum] += 1
        adversarial += 1 if adv else 0

    suspects = sorted(key for key, value in stable.items()
                      if RE_WALLCLOCK.search(value))

    here = os.path.dirname(os.path.abspath(__file__))
    provenance = {
        "pin": args.pin,
        "bindir": args.bindir,
        "passes": args.repeat,
        "argv": " ".join(sys.argv),
        # The normaliser and the catalogue as they were when this ran. A
        # fixture whose digests do not match the modules shipped beside it was
        # recorded by a different harness than the one that will replay it,
        # which is the one way a recording can be wrong and still look right.
        "recorded_by": digests([os.path.join(here, "isefix", name)
                                for name in ("catalog.py", "engine.py",
                                             "normalize.py")]),
        "seed": "none: every fixture payload is a literal in catalog.py",
        "clock": "none: timestamps are erased by normalize.py before an item "
                 "is compared, and the scan for date-shaped survivors "
                 "returned %d item(s) to account for" % len(suspects),
        "clock_suspects": suspects,
        # What clock this recording was taken on. Zero is the machine's own.
        # A second recording taken at a different value, with items identical
        # to this one's, is the evidence that nothing here has a shelf life --
        # and the two recordings can be told apart afterwards by this field
        # alone, which is why it is written even when it is zero.
        "clock_shift_seconds": int(os.environ.get("CLOCKSHIFT_SECONDS", "0")),
        "clock_shim": os.environ.get("LD_PRELOAD", ""),
    }

    os.makedirs(args.out, exist_ok=True)
    with open(os.path.join(args.out, "expected.json"), "w") as handle:
        json.dump({"items": stable,
                   "meta": {"strata": dict(strata), "adversarial": adversarial,
                            "total": len(stable), "passes": args.repeat,
                            "provenance": provenance}},
                  handle, indent=1, sort_keys=True)
    with open(os.path.join(args.out, "unstable.json"), "w") as handle:
        json.dump(unstable, handle, indent=1, sort_keys=True)

    print("\nstable   %d" % len(stable))
    print("unstable %d" % len(unstable))
    for key in sorted(unstable):
        print("   DROP %s" % key)
    print("strata   %s" % dict(strata))
    print("adversarial %d/%d = %.1f%%" % (
        adversarial, len(stable), 100.0 * adversarial / max(1, len(stable))))
    shift = int(os.environ.get("CLOCKSHIFT_SECONDS", "0"))
    print("clock    shift %+ds; %d item(s) carry date-shaped text%s" % (
        shift, len(suspects),
        "" if not suspects else ": " + ", ".join(suspects)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
