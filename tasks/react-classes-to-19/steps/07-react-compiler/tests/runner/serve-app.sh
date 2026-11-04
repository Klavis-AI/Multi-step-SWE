#!/usr/bin/env bash
# Starts the project's own data server on 127.0.0.1:3000 with the tree's build
# output as its static root, and waits until it answers.
#
# The server is the scorer's pristine copy of server/demo1, never the
# candidate's: the API is the fixed half of the contract the recordings were
# made against, and nothing the candidate writes may be read as evidence.  What
# does come from the candidate is dist/, which goes where the README says the
# packaged build goes - the server's static directory - so the application's
# hardcoded http://localhost:3000/ API base is same-origin, exactly as recorded.
set -u

# Where the scorer's pristine copy of the project's server lives in the verifier
# image, and the directory express.static serves from - app.js mounts
# path.join(__dirname, 'public').  This is the recorder's arrangement moved into
# the image unchanged: the expectations were recorded with the built site sitting
# in the server's own public/ and the API on the same origin, so the replay has
# to serve them the same way or every scenario differs for a reason that is not
# the candidate's.
SERVER_DIR=/tests/server/demo1
SERVER_ROOT="$SERVER_DIR/public"

serve_app() {
    local port="${PORT:-3000}"

    rm -rf "$SERVER_ROOT"
    cp -r /app/dist "$SERVER_ROOT" || return 1

    # The site is candidate output and the server executes none of it, but the
    # server does write nothing and needs nothing privileged, so it runs
    # unprivileged too and root stays out of the replay entirely.  It runs as
    # SERVER_UID rather than as nobody (run.sh says why), and the copy it serves
    # stays root's: readable by the server, writable by nothing that runs later.
    chmod -R a+rX "$SERVER_ROOT" 2>/dev/null || true
    # Its HOME is the drivers' root-owned one (run.sh, run_scorer_for): nothing
    # the candidate left in /tmp is on its module path. (wave D 2026-10-12)
    scorer_home || return 1
    setpriv --reuid "$SERVER_UID" --regid "$SERVER_UID" --clear-groups \
        env -C "$SERVER_DIR" HOME="$SCORER_HOME" TMPDIR=/tmp WS_NO_BUFFER_UTIL=1 PORT="$port" \
        node --require /tests/runner/det.cjs ./bin/www >/logs/verifier/server.log 2>&1 &

    for _ in $(seq 1 100); do
        if curl -fsS "http://127.0.0.1:${port}/userList" >/dev/null 2>&1; then
            return 0
        fi
        sleep 0.2
    done
    echo "data server did not start on ${port}" >&2
    return 1
}
