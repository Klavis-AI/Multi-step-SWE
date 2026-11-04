#!/usr/bin/env bash
# Replays the recorded behavior suite against the served candidate and compares
# the observations with the recordings.
#
# Both the driver and the recordings are the scorer's, mounted read-only from
# /tests; the candidate contributes only the built site.  A scenario counts only
# if every compared field matches exactly - DOM, per-chart canvas transcript,
# title, hash and the set of API paths requested - so this family measures
# behavior equivalence and nothing else.
#
# Writes /logs/verifier/behavior.json and sets BEHAVIOR_PASSED/BEHAVIOR_TOTAL.
set -u

BEHAVIOR_PASSED=0
BEHAVIOR_TOTAL=0

observe_app() {
    local workers="${OBSERVE_WORKERS:-4}"
    local obs=/tmp/obs
    local log=/logs/verifier/observe.log

    rm -rf "$obs"
    mkdir -p "$obs"
    chown 65534:65534 "$obs"
    : > "$log"
    chmod 666 "$log"

    # The browser runs as nobody: it loads candidate output, and Chromium's
    # sandbox is happier unprivileged anyway.
    # Bounded (run.sh, run_unprivileged_for).  A replay the bound cuts short
    # leaves the scenarios it never reached without an observation, and those
    # are scored as failures by the comparison below.
    run_scorer_for 1200 env -C /tests/runner \
        node drive.mjs --base "http://127.0.0.1:${PORT:-3000}" \
            --scenarios /tests/runner/scenarios.json \
            --out "$obs" --workers "$workers" >>"$log" 2>&1

    # The observations are compared from a copy only root can reach, taken after
    # every process of the driver's uid is gone: /tmp/obs is that uid's
    # directory, and the comparison must read what the replay wrote and nothing
    # written after it.  The data server runs as its own uid and stays up.
    reap_unprivileged
    local frozen
    frozen=$(mktemp -d)
    chmod 700 "$frozen"
    cp -r "$obs/." "$frozen/" 2>>"$log" || true

    # compare.mjs exits non-zero when anything differs, which is the normal case
    # for a partial migration, so its status is not an error here.  An absent
    # behavior.json is, and that is what the caller checks.
    node /tests/runner/compare.mjs \
        --expected /tests/expected --actual "$frozen" \
        --json /logs/verifier/behavior.json --diff-lines 6 >>"$log" 2>&1 || true
    rm -rf "$frozen"

    if [ ! -f /logs/verifier/behavior.json ]; then
        echo "observe: comparison produced no behavior.json" >>"$log"
        return 1
    fi
    # node, not python3: node is the one interpreter this image is guaranteed to
    # have, being a Playwright image.
    local jq='const d=require("/logs/verifier/behavior.json");console.log(d'
    BEHAVIOR_TOTAL=$(node -e "${jq}.total)")
    BEHAVIOR_PASSED=$(node -e "${jq}.passed)")
    # A zero denominator is a mean over an empty set, which would silently read
    # as a full behavior score; refuse it.
    if [ "$BEHAVIOR_TOTAL" -lt 1 ]; then
        echo "observe: expected suite is empty" >>"$log"
        return 1
    fi
    return 0
}
