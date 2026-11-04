#!/usr/bin/env bash
# Runs the driver that measures what the browser fetches before the
# application's first screen appears.
#
# Only the steps whose spec sets `entryCost` pay for this.  Steps 1-4 do not set
# it, so nothing in them starts a browser for it; step 5 is the step whose
# subject is what a visitor waits for, and runner/entrycost.mjs says at length
# why that number has to come from a browser rather than from a walk of the
# build output.
#
# There is no build here, unlike devconsole.sh.  The site under measurement is
# the one the behavior suite just replayed: serve-app.sh put the candidate's
# dist/ in the pristine server's static root and started it on PORT with the
# mock API on the same origin, which is the arrangement the recordings were made
# in.  So this stage runs inside the `serve_app` branch, needs the server that
# is already up, and adds no build of its own.
#
# It runs after the replay rather than before it.  The replay is the scored
# behavior family and nothing should be able to delay it; a browser that hangs
# here costs this one stage its 120 s and the guards then say the measurement
# did not happen, which is the reading it should have.
set -u

# Whether this step measures the entry cost at all, from its own spec - read the
# same way dev_console_wanted() reads devConsole, so the two flags behave
# identically and a step that sets neither runs neither.
entry_cost_wanted() {
    [ -f /tests/spec.json ] || return 1
    node -e 'process.exit(JSON.parse(require("fs").readFileSync("/tests/spec.json","utf8")).entryCost ? 0 : 1)' \
        2>/dev/null
}

# The second address this step measures at, if it has one.  Emitted as two
# lines - address then selector - so the shell can read it without a json
# parser of its own, and empty when the spec does not set `entryCost2`.  A step
# that does not set it runs exactly the one measurement it ran before this
# function existed, which is what keeps steps 1-7 unchanged by this file.
entry_cost_second() {
    [ -f /tests/spec.json ] || return 1
    node -e '
      const s = JSON.parse(require("fs").readFileSync("/tests/spec.json","utf8"));
      const e = s.entryCost2;
      if (!e || !e.address || !e.selector) process.exit(1);
      process.stdout.write(e.address + "\n" + e.selector + "\n");
    ' 2>/dev/null
}

measure_entry_cost() {
    local out=/logs/verifier
    local log=$out/entrycost.log

    entry_cost_wanted || return 0

    : > "$log"
    chmod 666 "$log"

    # Pre-created, before anything that can fail, so that a stage which never
    # runs leaves an empty record rather than none: the four checks that read it
    # take an empty one as "this did not happen", which is the reading it should
    # have, and json-flag never has to decide what a missing file means.  This is
    # devconsole.sh's pre-created strict.json, for the same reason.
    : > "$out/entrycost.json"
    chmod 644 "$out/entrycost.json" 2>/dev/null || true
    # The second record is pre-created on the same terms and for the same
    # reason, but only when this step asks for one: a step that does not set
    # `entryCost2` must not leave an empty second record for a later reader to
    # mistake for a stage that ran.
    local addr2="" sel2="" second=""
    # Declared above and assigned here: `local x=$(...)` would swallow the
    # command's exit status into local's own, and that status is the test.
    if second=$(entry_cost_second); then
        addr2=$(printf '%s' "$second" | sed -n '1p')
        sel2=$(printf '%s' "$second" | sed -n '2p')
        : > "$out/entrycost2.json"
        chmod 644 "$out/entrycost2.json" 2>/dev/null || true
    fi

    if [ "${APP_BUILD_OK:-0}" != "1" ]; then
        echo "entrycost: the tree did not build, so there was nothing served to measure" >>"$log"
        return 0
    fi

    # The browser loads candidate output, so it runs as nobody - the same
    # arrangement observe.sh uses for the replay and devconsole.sh for the
    # development run.  That unprivileged process cannot write into /logs, which
    # is mode 0700 and owned by root: path resolution needs `x` on every
    # directory on the way down, so making the file itself writable would not
    # help.  The driver therefore writes its record where it can and root puts it
    # in place.  (The log this function appends to is not a counterexample: root
    # opens that descriptor before setpriv runs and the child inherits it, so
    # nothing unprivileged opens /logs by name.)
    #
    # Root makes the scratch directory immediately before the run, and that is
    # what makes the record trustworthy.  Candidate code - the build, and every
    # probe build after it - runs as the same uid with TMPDIR=/tmp, so an
    # entrycost.json planted earlier would otherwise be indistinguishable from
    # one this driver wrote.
    local rec=/tmp/ecrec
    rm -rf "$rec"
    mkdir -p "$rec"
    chown "$SCORE_UID:$SCORE_GID" "$rec" 2>/dev/null || true
    chmod 700 "$rec"

    # 120 s, the cap on this whole stage: an honest run is one navigation, the
    # 30 s cap on the wait for the screen and the 2.5 s settle, plus a cold
    # browser start - under a minute at its worst - so 120 is roughly twice the
    # worst honest case and nothing that was going to finish is cut short by it.
    # It was 300, which is longer than the stage can honestly need and long
    # enough for a browser that hangs here to spend five minutes of the step's
    # own verifier budget on nothing while the rest of the run waits behind it.
    # A driver that is killed writes no record, and the guards read the empty one
    # above as a measurement that did not happen rather than as a tree with a
    # small entry.
    # The address and the screen are passed from here rather than left to the
    # driver's defaults, because they belong to the behavior suite and not to the
    # driver: #/nosuchpage is the address scenario r-notfound is recorded at and
    # the one devconsole.mjs visits as its no-route case, and .notFindPage is the
    # screen both of them look for.  Measuring at an address the recordings
    # already hold this tree to means the screen the cut is taken at is one the
    # replay has already checked pixel for pixel, rather than one this stage
    # invented and is the only witness to.
    run_scorer_for 120 env -C /tests/runner \
        node entrycost.mjs --base "http://127.0.0.1:${PORT:-3000}" \
            --address '#/nosuchpage' --selector '.notFindPage' \
            --dist /app/dist --out "$rec/entrycost.json" >>"$log" 2>&1 \
        || echo "entrycost: the driver exited non-zero" >>"$log"

    if [ -s "$rec/entrycost.json" ]; then
        cat "$rec/entrycost.json" >"$out/entrycost.json"
    else
        echo "entrycost: the driver wrote no record" >>"$log"
    fi
    chmod 644 "$out/entrycost.json" 2>/dev/null || true

    # The second address, when this step has one.  Same driver, same server,
    # same scratch directory and the same 120 s cap of its own - the stage's
    # worst case doubles and stays well inside the step's verifier budget,
    # because the expensive part of the first measurement was the build that
    # had already happened before it and neither of these does one.
    #
    # It runs after the first rather than beside it: two browsers against the
    # one server would make each one's byte count depend on the other's
    # scheduling, and the number being cut is a byte count at an instant.
    if [ -n "$addr2" ]; then
        run_scorer_for 120 env -C /tests/runner \
            node entrycost.mjs --base "http://127.0.0.1:${PORT:-3000}" \
                --address "$addr2" --selector "$sel2" \
                --dist /app/dist --out "$rec/entrycost2.json" >>"$log" 2>&1 \
            || echo "entrycost: the second driver exited non-zero" >>"$log"
        if [ -s "$rec/entrycost2.json" ]; then
            cat "$rec/entrycost2.json" >"$out/entrycost2.json"
        else
            echo "entrycost: the second driver wrote no record" >>"$log"
        fi
        chmod 644 "$out/entrycost2.json" 2>/dev/null || true
    fi

    rm -rf "$rec"
    return 0
}
