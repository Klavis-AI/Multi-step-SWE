# Checking your work

`tests/` holds a small smoke corpus and the runner that drives it. The
expected files are the output of the real PostgreSQL release this milestone
targets, captured through the same `psql` invocation the runner uses.

    bash /app/tests/run.sh

starts your server on a free port, replays `tests/cases/*.sql` through
`psql -X -a -q` reading each case from standard input, and diffs the result
against `tests/expected/`. It is a smoke test: it is much smaller and much
shallower than what this milestone requires, and passing it is necessary, not
sufficient.
