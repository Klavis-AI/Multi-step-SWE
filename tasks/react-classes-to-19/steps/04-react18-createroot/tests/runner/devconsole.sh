#!/usr/bin/env bash
# Builds the tree a second time - in development mode - and runs the driver that
# counts what the application reports to the console.
#
# Only the steps whose spec sets `devConsole` pay for this.  Every warning React
# emits about the things it is about to stop supporting is development-only code,
# compiled out of the production build the behavior suite replays, so a step
# whose subject is StrictMode is the one step that needs the application started
# a second way.  runner/devconsole.mjs says what it then measures.
#
# The development build is the candidate's own `npm run build` with NODE_ENV set:
# Vite takes NODE_ENV from the environment when it is already set and only
# defaults it from the mode when it is not, so this is the project's build
# command producing the project's build in the other configuration rather than a
# second, scorer-invented build.  Nothing here reads the output as a site to be
# compared - it is read as a program to be run.
#
# Runs last in the pipeline, after the provenance probes, for the same reason
# they run after the replay: a build script chooses where it writes, an absolute
# `build.outDir` of /app/dist is an honest thing for a project to have, and this
# build would then land on the scored one.  So the scored dist/ is set aside
# around it and put back, exactly as provenance.sh does, and the restoration is
# checked rather than assumed.
set -u

DEV_ROOT=/tmp/devrun
# The port the development build is served on.  Not 3000: the mock API is there,
# it is still running, and the application asks it for data by absolute URL.
DEV_PORT="${DEV_PORT:-3100}"

# Whether this step observes a development run at all, from its own spec.
dev_console_wanted() {
    [ -f /tests/spec.json ] || return 1
    node -e 'process.exit(JSON.parse(require("fs").readFileSync("/tests/spec.json","utf8")).devConsole ? 0 : 1)' \
        2>/dev/null
}

observe_dev_console() {
    local out=/logs/verifier
    local log=$out/devconsole.log

    dev_console_wanted || return 0

    : > "$log"
    chmod 666 "$log"

    if [ "${APP_BUILD_OK:-0}" != "1" ]; then
        echo "devconsole: the tree did not build, so there was no development run to observe" >>"$log"
        return 0
    fi

    # The same budget the provenance probes get, for the same build command on
    # the same tree.  probe_budget returns early when PROBE_TIMEOUT is already
    # set, so calling it here is either a no-op or the one place it gets sized.
    probe_budget
    echo "devconsole: build budget ${PROBE_TIMEOUT}s" >>"$log"

    rm -rf "$DEV_ROOT"
    mkdir -p "$DEV_ROOT"

    local scored_digest
    scored_digest=$(dist_digest /app/dist)
    if ! cp -a /app/dist "$DEV_ROOT/scored-dist" 2>>"$log"; then
        echo "devconsole: the scored dist/ could not be set aside, so the development build was not run" >>"$log"
        return 0
    fi

    local dir="$DEV_ROOT/tree"
    mkdir -p "$dir"
    if ! tar -C /app --exclude=./node_modules --exclude=./dist -cf - . \
         | tar -C "$dir" -xf - 2>>"$log"; then
        echo "devconsole: the tree could not be copied, so its development build was not run" >>"$log"
        rm -rf "$DEV_ROOT"
        return 0
    fi
    chown -R "$SCORE_UID:$SCORE_GID" "$dir" 2>/dev/null || true
    # After the chown, so it does not walk the installed tree.
    ln -s /app/node_modules "$dir/node_modules"

    local blog="$out/devconsole-build.log"
    : > "$blog"
    chmod 666 "$blog"
    rm -rf /app/dist
    local rc=0
    run_unprivileged_for "$PROBE_TIMEOUT" env -C "$dir" NODE_ENV=development npm run build \
        >>"$blog" 2>&1 || rc=$?
    # The driver below writes its record as the same uid the build ran as;
    # nothing the build started may still be running when it does (run.sh).
    reap_unprivileged
    echo "devconsole: development build exited $rc" >>"$log"

    # Two places the output can be, for the reason provenance.sh gives at
    # length: the build either writes relative to the directory it ran in or by
    # a path that does not depend on it, and build-app.sh already established
    # that it produces /app/dist/index.html when it runs in /app.
    local outdir="$dir/dist"
    if [ ! -f "$outdir/index.html" ] && [ -f /app/dist/index.html ]; then
        outdir=/app/dist
        # The development build landed on the scored one.  It is about to be
        # served, so it has to be read before the snapshot goes back; copy it
        # aside instead of racing the restore.
        rm -rf "$DEV_ROOT/devdist"
        cp -a /app/dist "$DEV_ROOT/devdist" 2>>"$log" && outdir="$DEV_ROOT/devdist"
        chown -R "$SCORE_UID:$SCORE_GID" "$DEV_ROOT/devdist" 2>/dev/null || true
    fi
    echo "devconsole: development build read from $outdir" >>"$log"

    # Put the scored build back before anything else runs, and say whether it is
    # the one that went away.
    rm -rf /app/dist
    local restored=no
    if cp -a "$DEV_ROOT/scored-dist" /app/dist 2>>"$log"; then
        [ "$(dist_digest /app/dist)" = "$scored_digest" ] && restored=yes
    fi
    echo "devconsole: scored dist/ restored: $restored" >>"$log"

    # The browser loads candidate output and the server serves it, so both run as
    # nobody - the same arrangement observe.sh uses for the replay.  The record
    # is written by that same unprivileged process, and that is the whole
    # difficulty: /logs is mode 0700 and owned by root, so nobody cannot
    # traverse it.  Making the file itself writable does not help and this stage
    # spent a measurement proving it - path resolution needs `x` on every
    # directory on the way down, and the open fails before the file's own mode
    # is ever consulted.  The logs this function appends to are not a
    # counterexample: root opens those descriptors before setpriv runs and the
    # child inherits them, so nothing unprivileged ever opens /logs by name.
    #
    # So the driver writes its record where it can and root puts it in place -
    # the same split observe.sh uses, where the replay writes /tmp/obs
    # unprivileged and root turns it into behavior.json.
    #
    # Root makes the scratch directory immediately before the run, which is what
    # makes the record trustworthy.  Candidate code - the development build just
    # above - runs as the same uid with TMPDIR=/tmp, so a strict.json it planted
    # earlier would otherwise be indistinguishable from one the driver wrote.
    local rec=/tmp/devrec
    rm -rf "$rec"
    mkdir -p "$rec"
    chown "$SCORE_UID:$SCORE_GID" "$rec" 2>/dev/null || true
    chmod 700 "$rec"

    # Pre-created so that a driver that never starts leaves an empty record
    # rather than none: the five guards read an empty one as "the development
    # run did not happen", which is the reading it should have, and json-flag
    # never has to decide what a missing file means.
    : > "$out/strict.json"

    run_scorer_for 900 env -C /tests/runner \
        node devconsole.mjs --dist "$outdir" --port "$DEV_PORT" \
            --out "$rec/strict.json" >>"$log" 2>&1 \
        || echo "devconsole: the driver exited non-zero" >>"$log"

    if [ -s "$rec/strict.json" ]; then
        cat "$rec/strict.json" >"$out/strict.json"
    else
        echo "devconsole: the driver wrote no record" >>"$log"
    fi
    rm -rf "$rec"
    chmod 644 "$out/strict.json" 2>/dev/null || true
    return 0
}
