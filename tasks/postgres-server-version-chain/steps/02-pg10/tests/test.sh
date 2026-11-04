#!/usr/bin/env bash
# The whole body is a function so that bash parses to the closing brace
# BEFORE executing anything.  bash reads a script incrementally as it runs,
# so a candidate that can write this file could rewrite its tail and change
# what happens after the score is decided -- and every defence in this file
# lives in that tail.
_verify() {
    set -u
    source /tests/helpers.sh

    # The candidate is a server process inside this container, and its own build
    # file runs inside it too.  So the reward must not be something the candidate
    # can write.  Three things make that true, in order of how much they are relied
    # on:
    #
    #   1. The scorer prints the reward on STDOUT, and this script keeps it in a
    #      shell variable.  A shell variable in the entrypoint is the one place a
    #      candidate process cannot reach -- not a file, not an environment
    #      variable, not a descriptor it can open.
    #   2. /logs/verifier is created root-owned and not group- or world-writable,
    #      with the mode set here rather than inherited from whatever umask the pod
    #      happened to hand us.
    #   3. The candidate's build and its server both run as an unprivileged user.
    #
    # A reward file with no matching line on stdout is DISTRUSTED and replaced with
    # zero, because an unexplained reward file is exactly what an attack produces.
    umask 022
    mkdir -p /logs/verifier
    chmod 755 /logs/verifier
    # Write the reward before scoring starts, so that a verifier which is killed
    # outright -- by the kernel, by the step timeout -- still leaves a result behind.
    # A step with no reward file does not score zero, it FAILS, and a failed step
    # aborts every step after it.
    printf '{"reward": 0.0}\n' > /logs/verifier/reward.json
    chmod 644 /logs/verifier/reward.json

    # The scorer's output is captured into a SHELL VARIABLE first and only then
    # written to a file.  Reading the reward back out of the file would hand the
    # decision to something on disk again, and a candidate that can write the log
    # can write a HARNESS_REWARD line into it.
    #
    # The whole scorer is wrapped in `timeout` and run from `/` (not the
    # candidate's /app).  The candidate is a server process that keeps running
    # during scoring: it can make one of the root-read inputs block -- a FIFO in
    # place of the datadir's PG_VERSION, a build child that holds make's stdout
    # pipe open past EOF -- which would otherwise hang this verifier past the
    # step timeout.  A step that never writes reward.json does not score zero, it
    # FAILS, and a failed step aborts every step after it and is dropped from the
    # mean.  The bound turns any such hang into a zero for THIS step instead: when
    # the scorer is killed it prints no HARNESS_REWARD line, and the block below
    # writes zero.  `cd /` keeps the scorer's cwd off the candidate's tree.
    OUT=$(cd / && timeout -k 30 3300 python3 /tests/runner/score.py --corpus /tests/corpus --app /app \
            --out /logs/verifier --work /tmp/pgvc --workers 6 2>&1)
    rc=$?
    printf '%s\n' "$OUT" > /logs/verifier/score.log

    # The authoritative number, from the scorer's own stdout.  Existence of the file
    # is not provenance: this asks "did the scorer say so", not "is something there".
    REWARD=$(printf '%s\n' "$OUT" | sed -n 's/^HARNESS_REWARD \([0-9.]*\)$/\1/p' | tail -1)
    if [ -z "${REWARD}" ]; then
        echo "pgvc: the scorer produced no HARNESS_REWARD line (exit $rc); the reward" \
             "file is not trusted and this milestone scores zero."
        printf '{"reward": 0.0}\n' > /logs/verifier/reward.json
    else
        # Binary step reward: 1 only when the scorer reports a perfect score.
        if awk -v r="$REWARD" 'BEGIN { exit !(r == 1) }'; then
            REWARD="1.000000"
        else
            REWARD="0.000000"
        fi
        printf '{"reward": %s}\n' "$REWARD" > /logs/verifier/reward.json
    fi
    chmod 644 /logs/verifier/reward.json

    printf '%s\n' "$OUT" | tail -40
    exit 0
}
_verify "$@"
