#!/usr/bin/env bash
# Runs the two provenance probes and leaves their verdict in
# /logs/verifier/provenance.json for run-gates.mjs to judge.
#
# What is being measured, and why it cannot be read off dist/, is in
# gates/provenance.mjs.  What is here is the part that has to be shell: the
# build is the candidate's own `npm run build`, so both probe builds go through
# run_unprivileged exactly as the scored build does.
#
# Three properties this has to keep:
#
#   * the scored dist/ survives the probes.  The probe builds run from scratch
#     copies under /tmp, but a build script chooses where it writes: a project
#     whose vite.config.js sets `build.outDir: '/app/dist'` is an ordinary,
#     honest project, and building its copy overwrites the directory the replay
#     and the artifact scan read.  So dist/ is snapshotted before the probes,
#     deliberately cleared around each probe build - that is how the output's
#     location is discovered - and put back afterwards, with the restoration
#     checked and recorded rather than assumed.
#   * they run after the behavior suite and after the artifact scan, so
#     `chunks` and `assets-self-contained` have already measured the scored
#     build whatever the probes go on to do.
#   * each build gets its own timeout and its own log.  Two more builds of a
#     tree whose build takes about eight seconds is a rounding error against a
#     210-scenario replay, but a candidate's build script is candidate code and
#     may not terminate.
set -u

PROBE_ROOT=/tmp/prov
# Left unset by default; probe_budget sizes it from the scored build's own
# measured wall clock.  An explicit value in the environment still wins, so a
# rehearsal can pin it.
PROBE_TIMEOUT="${PROBE_TIMEOUT:-}"
PROBE_SCORED_SECONDS=""

# Four times the scored build plus a minute, held between five and fifteen
# minutes.  The multiple is for the probe builds' handicap - a cold Vite cache
# in a fresh directory, two of them, on a machine also running the replay - and
# the floor is there because most of these builds take single-digit seconds, so
# a pure multiple would be a ceiling of half a minute.  Without a readable
# measurement the old fixed 600 s stands.
probe_budget() {
    local f=/logs/verifier/build-seconds.txt
    local scored=""
    if [ -r "$f" ]; then
        scored=$(tr -dc '0-9' <"$f" 2>/dev/null | head -c 6)
    fi
    PROBE_SCORED_SECONDS="$scored"
    if [ -n "${PROBE_TIMEOUT:-}" ]; then
        return 0
    fi
    if [ -z "$scored" ]; then
        PROBE_TIMEOUT=600
        return 0
    fi
    local budget=$((4 * scored + 60))
    [ "$budget" -lt 300 ] && budget=300
    [ "$budget" -gt 900 ] && budget=900
    PROBE_TIMEOUT="$budget"
}

# A digest of a directory's contents: every file's path and bytes, folded into
# one hash.  Used only to say whether the scored dist/ came back the way it
# went in - the probes are not allowed to be the reason a later reader sees
# something different.
dist_digest() {
    local dir="$1"
    if [ ! -d "$dir" ]; then
        printf 'absent\n'
        return 0
    fi
    ( cd "$dir" && find . -type f -print0 2>/dev/null | LC_ALL=C sort -z \
        | xargs -0 -r sha256sum 2>/dev/null ) | sha256sum | awk '{print $1}'
}

# A record is always written, including for the cases with nothing to measure:
# run-gates has to be able to tell "the probe says no" from "the probe never
# ran", and an absent file says neither.
probe_provenance() {
    local out=/logs/verifier
    local log=$out/provenance.log
    local rec=$out/provenance.json
    : > "$log"
    chmod 666 "$log"

    if [ "${APP_BUILD_OK:-0}" != "1" ]; then
        echo "provenance: the tree did not build, so there was nothing to rebuild" >>"$log"
        node /tests/gates/provenance.mjs --mode record --out "$rec" \
            --error "the tree did not build, so what its build compiles could not be measured" \
            >>"$log" 2>&1 || true
        return 0
    fi

    probe_budget
    echo "provenance: probe build budget ${PROBE_TIMEOUT}s (scored build took ${PROBE_SCORED_SECONDS:-unmeasured}s)" >>"$log"

    rm -rf "$PROBE_ROOT"
    mkdir -p "$PROBE_ROOT"

    # The snapshot, taken before any probe build can run.  If it cannot be
    # taken the probes do not run at all: a probe build may write over
    # /app/dist, and running one with nothing to put back would leave the
    # directory holding a marker-injected build rather than the scored one.
    local scored_digest
    scored_digest=$(dist_digest /app/dist)
    if ! cp -a /app/dist "$PROBE_ROOT/scored-dist" 2>>"$log"; then
        echo "provenance: the scored dist/ could not be copied aside, so the probes were not run" >>"$log"
        node /tests/gates/provenance.mjs --mode record --out "$rec" \
            --error "the built site could not be set aside, so re-running the build would have destroyed it" \
            --dist-restored yes >>"$log" 2>&1 || true
        return 0
    fi

    # One nonce per run, so a marker cannot be a leftover from an earlier one.
    local nonce
    nonce="p$(head -c 12 /dev/urandom | od -An -tx1 | tr -d ' \n')"

    local probe
    for probe in A B; do
        local dir="$PROBE_ROOT/$probe"
        mkdir -p "$dir"
        # The same exclusions Harbor's own collection makes, so the copy is the
        # tree as it would be handed over: the installed packages are linked
        # back in below, and dist/ is the thing being rebuilt.
        if ! tar -C /app --exclude=./node_modules --exclude=./dist -cf - . \
             | tar -C "$dir" -xf - 2>>"$log"; then
            echo "provenance: could not copy the tree for probe $probe" >>"$log"
            node /tests/gates/provenance.mjs --mode record --out "$rec" \
                --error "the tree could not be copied, so its build could not be re-run" \
                --dist-restored yes >>"$log" 2>&1 || true
            return 0
        fi
        node /tests/gates/provenance.mjs --mode inject --root "$dir" \
            --nonce "$nonce" --index "$PROBE_ROOT/$probe-index.json" >>"$log" 2>&1
    done

    # Probe B loses the module index.html loads, and nothing else.  Resolving it
    # is Vite's own contract; a tree where it cannot be resolved is a tree whose
    # build cannot be probed, and the guard says so rather than guessing.
    if ! node /tests/gates/provenance.mjs --mode blank --root "$PROBE_ROOT/B" \
         >"$PROBE_ROOT/entry.json" 2>>"$log"; then
        cat "$PROBE_ROOT/entry.json" >>"$log" 2>/dev/null || true
        node /tests/gates/provenance.mjs --mode record --out "$rec" \
            --entry-json "$PROBE_ROOT/entry.json" \
            --index "$PROBE_ROOT/A-index.json" --nonce "$nonce" \
            --dist-restored yes >>"$log" 2>&1 || true
        return 0
    fi

    # Where a probe build's output lands is the build script's choice, and there
    # are exactly two places it can be for a tree this verifier has already
    # scored.  build-app.sh required /app/dist/index.html to exist after
    # `env -C /app npm run build`, so the build command either writes its output
    # relative to the directory it runs in - $dir/dist for a probe - or writes
    # to /app/dist by a path that does not depend on that directory.  A build
    # that put its output anywhere else would have left /app/dist/index.html
    # absent, and the run would have stopped at the build.
    #
    # So /app/dist is cleared before each probe build and whichever of those two
    # holds an index.html afterwards is the output that gets walked.  The test
    # is presence at a location, not a comparison of contents: no mtimes, no
    # diffing against the snapshot.  A build that copies a stashed dist/ verbatim
    # is precisely what these probes exist to catch, and it has to reach the
    # marker walk like every other build for the measurement to say so.
    local status_A=1 status_B=1 outdir_A=none outdir_B=none
    for probe in A B; do
        local dir="$PROBE_ROOT/$probe"
        local blog="$out/provenance-build-$probe.log"
        : > "$blog"
        chmod 666 "$blog"
        chown -R "$SCORE_UID:$SCORE_GID" "$dir" 2>/dev/null || true
        # After the chown, so the recursive chown does not walk into the
        # installed tree.  Vite resolves packages through the link.
        ln -s /app/node_modules "$dir/node_modules"
        rm -rf /app/dist
        local rc=0
        run_unprivileged_for "$PROBE_TIMEOUT" env -C "$dir" npm run build \
            >>"$blog" 2>&1 || rc=$?
        # The output is read next, as root; nothing the build left running may
        # still be writing it (run.sh, reap_unprivileged).  The data server is
        # on its own uid and stays up for the development-console run.
        reap_unprivileged
        echo "provenance: probe $probe build exited $rc" >>"$log"
        local outdir=none
        if [ -f "$dir/dist/index.html" ]; then
            outdir="$dir/dist"
        elif [ -f /app/dist/index.html ]; then
            outdir=/app/dist
        fi
        echo "provenance: probe $probe output read from $outdir" >>"$log"
        if [ "$probe" = A ]; then
            status_A=$rc
            outdir_A="$outdir"
        else
            status_B=$rc
            outdir_B="$outdir"
        fi
        rm -f "$PROBE_ROOT/$probe-markers.json"
        if [ "$outdir" = none ]; then
            echo "provenance: probe $probe produced nothing to walk" >>"$log"
        else
            node /tests/gates/provenance.mjs --mode reach --dist "$outdir" \
                >"$PROBE_ROOT/$probe-markers.json" 2>>"$log" \
                || echo "provenance: probe $probe produced nothing to walk" >>"$log"
        fi
    done

    # Put the scored build back, and say whether it is the one that went away.
    # A failed restore is a fact about this run that a later reader needs, so it
    # is written to the log and carried in the record; it is never silent.
    rm -rf /app/dist
    local restored=no
    if cp -a "$PROBE_ROOT/scored-dist" /app/dist 2>>"$log"; then
        local back
        back=$(dist_digest /app/dist)
        if [ "$back" = "$scored_digest" ]; then
            restored=yes
        else
            echo "provenance: the scored dist/ was put back but does not match the snapshot ($back vs $scored_digest)" >>"$log"
        fi
    else
        echo "provenance: the scored dist/ could not be put back from $PROBE_ROOT/scored-dist" >>"$log"
    fi
    echo "provenance: scored dist/ restored: $restored" >>"$log"

    node /tests/gates/provenance.mjs --mode record --out "$rec" \
        --index "$PROBE_ROOT/A-index.json" --entry-json "$PROBE_ROOT/entry.json" \
        --nonce "$nonce" \
        --a "$PROBE_ROOT/A-markers.json" --b "$PROBE_ROOT/B-markers.json" \
        --a-status "$status_A" --b-status "$status_B" \
        --a-outdir "$outdir_A" --b-outdir "$outdir_B" \
        --budget "$PROBE_TIMEOUT" --scored-seconds "${PROBE_SCORED_SECONDS:-}" \
        --dist-restored "$restored" >>"$log" 2>&1 || true
    return 0
}
