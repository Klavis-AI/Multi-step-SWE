#!/usr/bin/env python3
"""Flatten a BoGo runner -json-output file to `<OUTCOME> <case-name>` lines.

The runner writes Chromium-style JSON: {"tests": {"<name>": {"actual": "PASS",
...}}}. `actual` is a space-separated list when a case was retried, so a case
counts as passing only if every recorded attempt passed -- a case that failed
and then passed is exactly the nondeterminism this task refuses to score.

Exits non-zero on anything unparseable, so the verifier can tell "the runner
produced no usable result set" apart from "the candidate failed cases".
"""
import json
import sys

if len(sys.argv) != 2:
    sys.exit("usage: bogo-outcomes.py <json-output-file>")

with open(sys.argv[1], "r", encoding="utf-8") as fh:
    doc = json.load(fh)

tests = doc.get("tests")
if not isinstance(tests, dict) or not tests:
    sys.exit("bogo json has no 'tests' object")

VALID = {"PASS", "FAIL", "SKIP", "CRASH", "TIMEOUT"}
out = []
for name, rec in tests.items():
    actual = (rec or {}).get("actual", "")
    attempts = [a for a in str(actual).split() if a]
    if not attempts:
        outcome = "UNKNOWN"
    elif all(a == "PASS" for a in attempts):
        outcome = "PASS"
    elif all(a == "SKIP" for a in attempts):
        outcome = "SKIP"
    else:
        # Includes the retried "FAIL PASS" shape.
        outcome = next((a for a in attempts if a != "PASS"), "FAIL")
    if outcome not in VALID and outcome != "UNKNOWN":
        outcome = "FAIL"
    if " " in name or "\n" in name:
        sys.exit("bogo case name contains whitespace: %r" % name)
    out.append("%s %s" % (outcome, name))

out.sort()
sys.stdout.write("\n".join(out) + "\n")
