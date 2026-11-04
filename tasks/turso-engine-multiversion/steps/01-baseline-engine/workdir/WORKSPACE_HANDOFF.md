# Workspace handoff — step 01

## What you start with

`/app` holds a starter Rust workspace that compiles and installs, and whose
shell does nothing yet:

```
/app/Cargo.toml  /app/Cargo.lock  /app/rust-toolchain.toml
/app/core/       engine internals, empty
/app/cli/        the shell, a stub that exits non-zero
/app/build.sh    build and install
/app/bin/        where the installed shell goes
```

There is no engine and no file-format support of any kind. The layout is a
starting point, not a requirement — see `TEST_INTERFACE.md`, "Not scored".

## What must be true when you stop

1. `/app/bin/limbo` is a regular executable file, built from the sources
   in this workspace.
2. Every requirement in `TEST_INTERFACE.md` is implemented and observable
   through that shell.
3. Nothing is inherited. The gate is proof of life only, and this step's cases are the whole of what is scored.

## Preserve the workspace

The next step continues in this same `/app`. Leave your sources in place and
committed to whatever structure you like; do not clean the tree, and do not
delete `/app/bin/`.

`/app/target/` and `/app/.git/` are excluded from the snapshot handed to the
verifier — everything else under `/app` is included. Anything grading depends
on must be the installed binary itself, not build output.

## Checking your work locally

You have the same tools the verifier has. To exercise the shell the way it
will be exercised:

```bash
mkdir -p /tmp/check && cd /tmp/check && rm -f main.db main.db-wal
printf 'CREATE TABLE t(x);\nINSERT INTO t VALUES (1);\nSELECT * FROM t;\n' \
  | /app/bin/limbo -m raw -q main.db
```

To check a file you wrote is genuinely SQLite-readable, open it with the stock
binary, which is a different implementation and will not share your bugs:

```bash
sqlite3 main.db 'SELECT * FROM t;'
sqlite3 main.db 'PRAGMA integrity_check;'
```

Use `sqlite3` as the second opinion on any query answer you are unsure of. It
is a development tool only; see `spec/PROFILE.md`.

## Restoring a standalone continuation

This workspace is self-contained. `/app/spec/` carries the full specification
corpus for this step, `TEST_INTERFACE.md` the complete contract, and
`/app/build.sh` the build. A fresh session needs nothing from outside `/app`
and nothing from the network.
