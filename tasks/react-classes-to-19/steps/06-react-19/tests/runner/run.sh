#!/usr/bin/env bash
# The whole scoring pipeline for one step, in one function.
#
# Every step's test.sh does four things: source the helpers, source this, call
# run_step with the number spec_step() below reads out of the shipped gate spec,
# report the reward.  The step contributes a gate spec and a set of recordings;
# nothing else differs between steps - test.sh included - so nothing else is
# duplicated eight times where it could drift.
#
# run_step computes; it does not report.  `report` exits the shell, so a runner
# that called it would make every line after it unreachable - including the
# flush of a log the caller had redirected.  The reward is left in STEP_REWARD
# and test.sh reports it as its last act.
#
# Shape of the score, fixed for every step:
#
#     reward = gates x behavior          (product scoring, 2026-10-13)
#
# Two families, multiplied, so that the untouched previous-step tree scores 0 -
# it passes the behavior family completely, because a correct application that
# has not been migrated still behaves identically, but fails the gate family
# completely, because the gates ask what the step was about.  A step is passed
# on both halves at once, not on either.
#
# Failure is graded, not cliff-edged, with one exception: the gate family is
# anchored to liveness, and a tree that does not still serve the application
# scores 0 for both families rather than for behavior alone.
#
# Two reasons, and the spec carries a two-part guard that every check is
# evaluated behind.
#
# The gates cannot tell "migrated" from "absent" on their own.  Every removal
# gate - no classes, no string refs, no findDOMNode - is satisfied for free by a
# tree with no JavaScript in it, and so is "every file parses".  That is 15 of
# the 31 gate weight, so an unanchored family would pay a candidate roughly a
# quarter of the step for deleting the repository, and rule 8 requires that an
# empty tree score 0.  So the built site must have reproduced a floor of the
# recorded behavior.
#
# And the two families read two different things: behavior replays dist/, the
# gates read the source tree.  Nothing otherwise requires them to be the same
# program, so a dist/ built once from the untouched application and put back by
# the build script satisfies the first while a gutted tree satisfies the second.
# So the build is run twice more, on scratch copies of the tree, and what the
# served page contains is measured against what the tree's own modules put
# there - a property of the build, not of anything the candidate writes into
# dist/.  provenance.sh runs it; gates/provenance.mjs says why nothing static
# can answer the same question.
#
# What that gives up is real and worth stating: a candidate that converts every
# construct correctly but cannot get the site to build and serve, or ships a
# build its own source did not produce, now scores 0 on the gate family rather
# than its share of it.  That is the right trade only because the two are not
# symmetric - such a candidate can see that its build is broken and fix it,
# while nothing in a gate score distinguishes a deleted or borrowed application
# from a migrated one.
set -u

. /tests/runner/build-app.sh
. /tests/runner/serve-app.sh
. /tests/runner/observe.sh
. /tests/runner/provenance.sh
. /tests/runner/scan.sh
. /tests/runner/devconsole.sh
. /tests/runner/entrycost.sh

STEP_REWARD=0

# --- keeping candidate code inside its phase ---------------------------------
#
# These live here and not in helpers.sh for the reason spec_step() gives below:
# helpers.sh is the harbor template's file, verbatim.  (wave A 2026-10-12,
# reward-hacking review F1/F2)
#
# The data server gets a uid of its own.  It is the scorer's pristine copy and
# executes nothing the candidate wrote, and it has to stay up from the replay to
# the development-console run; on a uid of its own, the sweeps below can kill
# every process of SCORE_UID between phases without taking it down, and nothing
# running as SCORE_UID can signal it.
SERVER_UID=65533

# run_unprivileged under a wall-clock bound the candidate cannot remove: the
# `timeout` runs as root, outside the setpriv, so a SCORE_UID process can
# neither kill it nor outwait it, and -k turns an ignored SIGTERM into a SIGKILL
# thirty seconds later.  Exit 124/137 is the bound firing.
run_unprivileged_for() {
    local secs="$1"
    shift
    timeout -k 30 "$secs" setpriv --reuid "$SCORE_UID" --regid "$SCORE_GID" --clear-groups \
        env HOME=/tmp TMPDIR=/tmp "$@"
}

# The browser drivers' own environment.  drive.mjs, entrycost.mjs and
# devconsole.mjs run as SCORE_UID and write the records the scorer trusts, and
# Node appends $HOME/.node_modules and $HOME/.node_libraries to every bare
# module lookup.  playwright-core's websocket client does a bare
# require('bufferutil') that /tests does not install, so with HOME=/tmp - the
# HOME the candidate's install and build had, as the same uid - a build that
# leaves /tmp/.node_modules/bufferutil/index.js gets it loaded into the driver.
# So the drivers (and the data server) get a HOME that is root-owned, empty and
# not writable by either uid, and the two websocket add-ons are switched off.
# Chromium still needs a writable crash database, so TMPDIR and the XDG
# directories point into a directory root creates fresh before each driver,
# after the reap; Node resolves no module through any of them.
# (wave D 2026-10-12, final review C2)
SCORER_HOME=/run/rc19-scorer-home
SCORER_TMP=/run/rc19-scorer-tmp

scorer_home() {
    [ -d "$SCORER_HOME" ] && [ "$(stat -c '%u %a' "$SCORER_HOME" 2>/dev/null)" = "0 555" ] && return 0
    rm -rf "$SCORER_HOME"
    mkdir -p "$SCORER_HOME" && chown root:root "$SCORER_HOME" && chmod 555 "$SCORER_HOME"
}

# run_scorer_for <seconds> <command...>: run_unprivileged_for, in the
# environment above.  Returns 125 without running anything if the directories
# cannot be made, which every caller already reads as "the driver failed".
run_scorer_for() {
    local secs="$1"
    shift
    scorer_home || return 125
    rm -rf "$SCORER_TMP"
    mkdir -p "$SCORER_TMP" && chown "$SCORE_UID:$SCORE_GID" "$SCORER_TMP"         && chmod 700 "$SCORER_TMP" || return 125
    timeout -k 30 "$secs" setpriv --reuid "$SCORE_UID" --regid "$SCORE_GID" --clear-groups         env HOME="$SCORER_HOME" TMPDIR="$SCORER_TMP"             XDG_CONFIG_HOME="$SCORER_TMP/.config" XDG_CACHE_HOME="$SCORER_TMP/.cache"             WS_NO_BUFFER_UTIL=1 WS_NO_UTF_8_VALIDATE=1 "$@"
}

# Kill every process of one uid in this container.  run_unprivileged returns
# when the command it started returns, not when what that command spawned is
# gone; a build script that backgrounds a process (`setsid sh -c ... &`) would
# otherwise still be running, as the same uid the replay's drivers write their
# observations as, while those observations are taken and read.
#
# One atomic sweep first: kill(-1) from a process running as that uid reaches
# every process of it in this PID namespace except the sweeper, under one kernel
# lock, so a candidate forking faster than a /proc walk cannot slip past it.
# Then a walk that checks nothing but zombies is left, a few times over.  procps
# is not relied on.  Always returns 0.
reap_uid() {
    local uid="$1" d pid st left round
    for round in 1 2 3 4 5 6 7 8 9 10; do
        setpriv --reuid "$uid" --regid "$uid" --clear-groups \
            bash -c 'kill -s KILL -- -1' >/dev/null 2>&1
        left=0
        for d in /proc/[0-9]*; do
            pid=${d#/proc/}
            [ "$pid" = "$$" ] && continue
            [ "$(stat -c %u "$d" 2>/dev/null)" = "$uid" ] || continue
            st=$(cat "$d/stat" 2>/dev/null) || continue
            st=${st##*) }
            [ "${st%% *}" = Z ] && continue
            kill -9 "$pid" 2>/dev/null
            left=$((left + 1))
        done
        [ "$left" -eq 0 ] && break
        sleep 0.2
    done
    return 0
}

reap_unprivileged() {
    reap_uid "$SCORE_UID"
}

# Which step this verifier is for, read out of the gate spec it ships beside.
#
# The number used to be written into test.sh as well, and that was a second
# place for it to be wrong. A local BuildKit defect once handed the step-2
# verifier image step 1's copy of test.sh: the two files were the same length
# with the same mtime, and its local-context sync treats same path plus same
# size plus same mtime as unchanged, so it never re-read the file. The step-2
# spec did reach that image - spec.json's sizes differ between steps - so the
# run measured step 2 and filed the evidence under step01-*, which is exactly
# the shape of failure the multi-step build is supposed to make visible.
#
# The spec is the file that decides which checks run, so it is also the file
# that decides what the run is called. With the number gone from test.sh, every
# step's test.sh is byte-identical, spec.json is the only per-step file in the
# build context, and that defect has nothing left to swap.
#
# This lives here rather than in helpers.sh because helpers.sh is the harbor
# template's file, verbatim: it is the scoring contract, and a local addition to
# it would be a local divergence from the contract.
spec_step() {
    node -e 'const s = require("/tests/spec.json").step;
             if (!/^[0-9]{2}$/.test(s || "")) process.exit(1);
             process.stdout.write(s)' 2>/dev/null
}

run_step() {
    local step="$1"
    local spec="/tests/spec.json"
    local out=/logs/verifier
    STEP_REWARD=0

    # The caller's step number and the spec's have one authority between them,
    # and it is the spec: the spec is what decides which checks run, so it is
    # what the evidence gets named after.  test.sh reads the same number out of
    # the same file, so the two disagree only if this runner was called by hand
    # or by a test.sh from another step - the second of which is the build
    # defect spec_step() above describes, and which this turns from a silent
    # mislabelling into a line in the step's own stderr log.
    local spec_step_id=""
    [ -f "$spec" ] && spec_step_id=$(spec_step)
    if [ -n "$spec_step_id" ] && [ "$spec_step_id" != "$step" ]; then
        echo "run: called for step $step but /tests/spec.json is step $spec_step_id;" \
             "scoring and naming for $spec_step_id" >&2
        step="$spec_step_id"
    fi

    # Named for this step, by this step.  A verifier directory that mentions only
    # other numbers was never run for the step whose reward it holds - that is
    # exactly the multi-step build failure that scores a step it never executed,
    # so the evidence is written where a later check can insist on it.
    local detail="$out/step${step}-detail.json"

    if [ ! -f "$spec" ]; then
        echo "run: no gate spec at $spec" >&2
        return 0
    fi

    # Order: build, then serve and observe, then scan, then the provenance
    # probes.  The scan reads dist/ and the installed tree, so it has to come
    # after the install and build; the observation needs the server, which needs
    # the build.
    #
    # The entry-cost measurement sits inside the same served window, immediately
    # after the replay, and both halves of that placement are deliberate.  It has
    # to be inside it because it visits the site the replay visits, on the server
    # serve-app.sh started, with the mock API on the same origin; and it has to be
    # after the replay rather than before it because the replay is the scored
    # behavior family and nothing should be able to delay it - a browser that
    # hangs in the measurement costs that stage its own budget and the guards
    # then report that the measurement did not happen.
    #
    # The probes come last, and that is the point rather than an accident of
    # ordering.  They rebuild the tree in scratch copies under /tmp, but where a
    # build script puts its output is the build script's choice: an absolute
    # `build.outDir` pointing at /app/dist is an honest thing for a project to
    # write, and a probe build of such a tree writes over the very directory the
    # scan and the replay just read.  So the probes snapshot /app/dist, clear it
    # around each probe build in order to see where the output landed, put the
    # snapshot back and check that they did.  Running them after everything that
    # reads /app/dist means that check is a guard on a promise already kept, not
    # the thing holding the earlier stages up.
    build_app || true
    # Nothing the install or the build started may still be running when the
    # replay starts writing observations as the same uid.
    reap_unprivileged

    if [ "${APP_BUILD_OK:-0}" = "1" ]; then
        if serve_app; then
            observe_app || true
            measure_entry_cost || true
            reap_unprivileged
        else
            echo "run: the built site did not come up" >>"$out/observe.log" 2>/dev/null || true
        fi
    fi

    scan_app || true

    probe_provenance || true
    reap_unprivileged

    # Last, and only in the step whose spec asks for it.  It builds the tree once
    # more in development mode and runs it, which is the only configuration in
    # which React reports the things it is about to stop supporting; it is also
    # the one stage that can land on the scored dist/, so it goes after
    # everything that reads it.  devconsole.sh says the rest.
    observe_dev_console || true
    reap_unprivileged
    # The data server's work is over.
    reap_uid "$SERVER_UID"

    node /tests/gates/run-gates.mjs --spec "$spec" \
        --ast "$out/ast.json" --deps "$out/deps.json" \
        --artifacts "$out/artifacts.json" --lint "$out/eslint.json" \
        --provenance "$out/provenance.json" \
        --out "$out/gates.json" 2>>"$out/scan.log"

    # behavior.json is absent when the tree did not build or did not serve.
    # score.mjs scores a missing family as 0 and says so in problems[], rather
    # than dropping the family and letting the other one stand in for the whole
    # reward - a mean over one family is not the contract.
    node /tests/runner/score.mjs --step "$step" \
        --behavior "$out/behavior.json" --gates "$out/gates.json" \
        --out "$detail" | tee "$out/step${step}-score.log"

    local reward
    reward=$(awk '/^REWARD /{print $2}' "$out/step${step}-score.log" | tail -1)
    if [ -z "$reward" ]; then
        echo "run: score.mjs printed no reward line" >&2
        return 0
    fi
    STEP_REWARD="$reward"
    return 0
}
