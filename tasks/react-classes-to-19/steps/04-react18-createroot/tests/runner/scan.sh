#!/usr/bin/env bash
# Runs the four scanners over the tree under test and leaves their JSON in
# /logs/verifier for run-gates.mjs to judge.
#
# Each is independent of the others: every question is asked of one scanner's
# output, and nothing here is derived from anything the build wrote into dist/
# besides dist/ itself.  The question "which of these files does the served page
# actually depend on?" is not asked here at all - it is answered by the two
# probe builds in provenance.sh, from the bundler's own behaviour rather than
# from a resolver written here or a map written by the candidate.
#
# These read candidate files but never execute them - a parse is not a run - so
# they run as root and write straight into the hardened reward directory, the
# same way compare.mjs does.  Everything that executes candidate code (install,
# build, server, browser) runs unprivileged in the other runner scripts.
#
# A scanner that fails leaves its file absent rather than writing a partial one;
# run-gates then scores only the checks that needed it as 0, each with its own
# message, instead of collapsing the family to one unexplained zero.
set -u

scan_app() {
    local log=/logs/verifier/scan.log
    local out=/logs/verifier
    : > "$log"
    chmod 666 "$log"

    # The artifact scan needs a build to have happened.  When there is none the
    # file is deliberately not written, and the artifact-level gates say so.
    if [ "${APP_BUILD_OK:-0}" = "1" ] && [ -d /app/dist ]; then
        node /tests/gates/artifacts.mjs /app/dist --json "$out/artifacts.json" >>"$log" 2>&1 \
            || echo "scan: artifacts.mjs failed" >>"$log"
    else
        echo "scan: no build output, artifacts scan skipped" >>"$log"
    fi

    node /tests/gates/ast-scan.mjs /app --json "$out/ast.json" >>"$log" 2>&1 \
        || echo "scan: ast-scan.mjs reported a problem" >>"$log"
    node /tests/gates/deps.mjs /app --json "$out/deps.json" >>"$log" 2>&1 \
        || echo "scan: deps.mjs failed" >>"$log"

    # eslint resolves its plugins out of the verifier's own node_modules, from a
    # config the candidate cannot edit.  Linting the candidate's config instead
    # would be asking it to grade itself.
    node /tests/gates/eslint-run.mjs --root /app \
        --config /tests/gates/eslint.config.mjs \
        --json "$out/eslint.json" >>"$log" 2>&1 \
        || echo "scan: eslint-run.mjs failed" >>"$log"

    for f in ast deps artifacts eslint; do
        [ -f "$out/$f.json" ] && chmod 644 "$out/$f.json"
    done
    return 0
}
