# Workspace handoff — step 09

## What you start with

`/app` is exactly as you left it at the end of step 08
(DROP INDEX, descending indexes, index upkeep on DELETE, ALTER TABLE ... RENAME TO, and the page_size and schema_version pragmas) — your sources, your `Cargo.lock`, your installed shell at
`/app/bin/limbo`, and your `/app/target/` build cache, so an
incremental build is fast.

The documents in `/app/spec/`, together with `/app/TEST_INTERFACE.md`,
`/app/WORKSPACE_HANDOFF.md` and `/app/build.sh`, have been **replaced** with
this step's editions. Re-read `TEST_INTERFACE.md`: the
graded groups differ from the last step's, and the interface may have changed.
Any file of your own under those names would have been overwritten, so keep
your notes elsewhere.

## What must be true when you stop

1. `/app/bin/limbo` is a regular executable file, built from the sources
   in this workspace.
2. Every requirement in `TEST_INTERFACE.md` is implemented and observable
   through that shell.
3. One end-to-end case from each earlier step is re-run here unchanged, as the gate groups in `TEST_INTERFACE.md`. The gate earns nothing: if any gate case fails, this step's reward is 0 whatever else passes, and `reward_details.json` names the case. It does not retroactively change the earlier step's reward. The fifteen rewards are averaged.

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
  | /app/bin/limbo -m list -q main.db
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
