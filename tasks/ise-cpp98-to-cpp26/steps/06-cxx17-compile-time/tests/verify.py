#!/usr/bin/env python3
"""Replay the recorded behaviour suite against a built tree and score it.

The runner is glue. Every expectation in fixtures/expected.json came out of a
recording of the untouched baseline; nothing here decides what is correct.

  python3 verify.py --bindir /app/build/linux/debug/bin \\
                    --fixtures /tests/fixtures/expected.json \\
                    --out /logs/verifier/behavior.json
"""
import argparse
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from isefix import catalog                     # noqa: E402
from isefix.engine import run_session, SessionError, expected_item_ids  # noqa: E402

MAX_DIFF = 600


def diff_note(expected, got):
    if got is None:
        return "item was not produced"
    for index, (left, right) in enumerate(zip(expected, got)):
        if left != right:
            start = max(0, index - 60)
            return "first difference at byte %d\n  expected: %r\n  actual:   %r" % (
                index, expected[start:index + 60], got[start:index + 60])
    return "length differs: expected %d bytes, got %d bytes\n  expected tail: %r\n  actual tail:   %r" % (
        len(expected), len(got), expected[len(got):][:MAX_DIFF],
        got[len(expected):][:MAX_DIFF])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--bindir", default="/app/build/linux/debug/bin")
    parser.add_argument("--fixtures", default="/tests/fixtures/expected.json")
    parser.add_argument("--out", default="/logs/verifier/behavior.json")
    parser.add_argument("--only", nargs="*")
    args = parser.parse_args()

    with open(args.fixtures) as handle:
        expected = json.load(handle)["items"]

    index = {}
    for _session, step in catalog.all_steps():
        index[step["id"]] = (step.get("stratum", "core"), bool(step.get("adv")))

    got = {}
    for session in catalog.sessions():
        if args.only and not any(session["id"].startswith(p) for p in args.only):
            continue
        started = time.time()
        try:
            got.update(run_session(session, args.bindir))
        except SessionError as exc:
            # A session that cannot even be started fails every item it owns
            # rather than removing them from the denominator.
            for item in expected_item_ids(session):
                got[item] = "<session-failed: %s>" % exc
        except Exception as exc:                      # noqa: BLE001
            for item in expected_item_ids(session):
                got[item] = "<session-error: %r>" % (exc,)
        print("  %-24s %6.1fs" % (session["id"], time.time() - started),
              file=sys.stderr)

    results = []
    for name in sorted(expected):
        want = expected[name]
        have = got.get(name)
        ok = have == want
        stratum, adv = index.get(name, ("core", False))
        entry = {"name": name, "ok": ok, "stratum": stratum, "adversarial": adv}
        if not ok:
            entry["detail"] = diff_note(want, have)[:MAX_DIFF]
        results.append(entry)

    passed = sum(1 for entry in results if entry["ok"])
    payload = {"results": results, "passed": passed, "total": len(results)}
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "w") as handle:
        json.dump(payload, handle, indent=1)
    print("behaviour: %d/%d" % (passed, len(results)))


if __name__ == "__main__":
    main()
