#!/usr/bin/env python3
"""Regenerate `environment/timer.sh` and its thirteen `steps/*/workdir/` copies.

WHAT THE FILE IS
    The agent-visible budget countdown: `bash /app/timer.sh` prints the step's
    budget, how much of it has elapsed and how much is left. The configured
    per-step timeout is the authoritative one (steps.agent.timeout_sec in
    task.toml, and the instruction says so); this is the convenience copy, and
    nothing is graded on it. The harness replaces /app/TEST_INTERFACE.md,
    /app/docs/ and /app/timer.sh with fresh copies at the start of every step.

WHY THE STAMP IS PER STEP
    The whole ladder runs in ONE container, so a single shared
    `/tmp/.step-start` would be created by the first step to run the script
    and never reset, and every later step would print `left 0s`. The stamp is
    therefore keyed by step: `/tmp/.step-start-NN`, with NN written into the
    copy that the harness installs for that step. A step's clock starts the
    first time that step's own copy is run.

USAGE
    gen-timer.py [--pkg DIR] [--check]
"""

import argparse
import os
import re
import sys

TEMPLATE = """#!/usr/bin/env bash
# Reports this step's remaining budget.
#
# The configured timeout is authoritative; this script is a convenience so the
# budget is visible from inside the container. BUDGET matches
# steps.agent.timeout_sec in task.toml.
#
# The stamp is per step. The whole ladder runs in one container, so a single
# shared stamp would be created once -- by whichever step ran the script first
# -- and every later step would read it as an already-exhausted budget. This
# copy carries its own step number, and the harness installs the copy belonging
# to the current step, so a step's clock starts the first time its own copy
# runs. Regenerate with environment/assets/gen-timer.py.
set -u
STEP=@@STEP@@
BUDGET=7200
STAMP=/tmp/.step-start-$STEP

if [ ! -f "$STAMP" ]; then
    date +%s > "$STAMP" 2>/dev/null || true
fi

start=$(cat "$STAMP" 2>/dev/null || date +%s)
now=$(date +%s)
elapsed=$(( now - start ))
left=$(( BUDGET - elapsed ))
[ "$left" -lt 0 ] && left=0

printf 'budget   %ds (%dm)\\n' "$BUDGET" "$(( BUDGET / 60 ))"
printf 'elapsed  %ds (%dm)\\n' "$elapsed" "$(( elapsed / 60 ))"
printf 'left     %ds (%dm)\\n' "$left" "$(( left / 60 ))"
"""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pkg", default=os.path.dirname(os.path.dirname(
        os.path.dirname(os.path.abspath(__file__)))))
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()
    pkg = args.pkg
    steps = sorted(d for d in os.listdir(os.path.join(pkg, "steps"))
                   if re.match(r"^\d\d-", d))
    if len(steps) != 13:
        raise SystemExit("expected 13 steps, found %d" % len(steps))

    files = {os.path.join(pkg, "environment/timer.sh"): TEMPLATE.replace("@@STEP@@", "01")}
    for d in steps:
        files[os.path.join(pkg, "steps", d, "workdir/timer.sh")] = \
            TEMPLATE.replace("@@STEP@@", d[:2])

    drift = []
    for path, text in sorted(files.items()):
        old = open(path).read() if os.path.exists(path) else None
        if old != text:
            drift.append(path)
            if not args.check:
                with open(path, "w") as f:
                    f.write(text)
                os.chmod(path, 0o755)
    if args.check and drift:
        for p in drift:
            print("drift: %s" % p)
        raise SystemExit(1)
    print("timer.sh: %d copies%s"
          % (len(files), " (%d rewritten)" % len(drift) if drift and not args.check else ""))


if __name__ == "__main__":
    sys.exit(main())
