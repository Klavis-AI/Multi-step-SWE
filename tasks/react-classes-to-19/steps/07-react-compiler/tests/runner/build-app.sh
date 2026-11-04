#!/usr/bin/env bash
# Builds the tree under test with the project's own build command and leaves the
# site in /app/dist.  Sourced by test.sh, which has already called
# harden_reward_dir, so everything here runs as nobody: the build executes the
# candidate's package scripts, and a build script is candidate code.
#
# Prints nothing on success beyond the logs it writes; sets APP_BUILD_OK.
set -u

APP_BUILD_OK=0

build_app() {
    local log=/logs/verifier/build.log

    give_app_to_unprivileged
    : > "$log"
    chmod 666 "$log"

    # The install is performed from the lockfile: npm ci fails outright when
    # package.json and package-lock.json disagree rather than quietly resolving
    # some other tree, and that is the property a step states when it requires a
    # lockfile.  A tree that has no lockfile at all has not met that requirement
    # either, and falling back to npm install would install whatever resolves
    # today and let a tree that dropped the file past a requirement its own
    # instruction makes of it - so a missing lockfile takes the same road a
    # failing npm ci takes, and says which file is missing.
    #
    # Which steps require one is each step's own business and is declared in its
    # spec: the first step inherits a tree that has no lockfile, and its
    # instruction asks only that a lockfile agree with package.json if there is
    # one.
    local requires_lock=0
    if [ -f /tests/spec.json ] && node -e \
        'process.exit(JSON.parse(require("fs").readFileSync("/tests/spec.json","utf8")).requireLockfile ? 0 : 1)' \
        2>/dev/null; then
        requires_lock=1
    fi

    local install=(npm install --no-audit --no-fund)
    if [ -f /app/package-lock.json ]; then
        install=(npm ci --no-audit --no-fund)
    elif [ "$requires_lock" = "1" ]; then
        echo "build: /app/package-lock.json is missing; this step installs from the lockfile" >>"$log"
        return 1
    fi

    # Bounded, like every other run of candidate code (run.sh,
    # run_unprivileged_for): an install or a build that never returns would
    # otherwise hold the verifier past its timeout, and Harbor drops a step whose
    # verifier does that instead of scoring it.  Both are minutes at most on
    # every reference tree.
    if ! run_unprivileged_for 900 env -C /app "${install[@]}" >>"$log" 2>&1; then
        echo "build: dependency install failed" >>"$log"
        return 1
    fi
    # The scored build is timed, and the number is written out for
    # provenance.sh: the probes run this same command on a copy of this same
    # tree, so the only honest basis for "this rebuild is not going to finish" is
    # how long this tree's own build actually took.  A fixed ceiling is either
    # loose enough to let a build that never terminates spend the whole run, or
    # tight enough to fail an honest slow tree.  Whole seconds; a build faster
    # than a second reads 0 and the floor downstream covers it.
    local started ended rc=0
    started=$(date +%s)
    run_unprivileged_for 900 env -C /app npm run build >>"$log" 2>&1 || rc=$?
    ended=$(date +%s)
    printf '%s\n' "$((ended - started))" >/logs/verifier/build-seconds.txt 2>/dev/null || true
    chmod 644 /logs/verifier/build-seconds.txt 2>/dev/null || true
    if [ "$rc" -ne 0 ]; then
        echo "build: npm run build failed" >>"$log"
        return 1
    fi
    if [ ! -d /app/dist ]; then
        echo "build: no dist/ after npm run build" >>"$log"
        return 1
    fi
    # An index.html is what the server has to hand a browser; a dist without one
    # is not a site, and every later failure would be a confusing 404 instead.
    if [ ! -f /app/dist/index.html ]; then
        echo "build: dist/ has no index.html" >>"$log"
        return 1
    fi
    APP_BUILD_OK=1
    return 0
}
