# WORKSPACE_HANDOFF — step 02 of 5 (SQLite 3.8.3)

This file explains how the workspace travels between steps. It does not define
the product — `sqlite-doc/PROFILE.md` and `TEST_INTERFACE.md` do that.

## What `/app` is

`/app` is a **cumulative workspace**. Each step runs in a fresh conversation
with no memory of the previous one, but on the *same* `/app` tree, with that
step's documentation overlaid on top. Everything you write under `/app` is what
the next step starts from, so the workspace is the handoff.

At the start of this step `/app` contains:

* the implementation you (or a previous step) wrote: `Makefile`, `include/`,
  `src/`, `shell/`, and anything else you added;
* `sqlite-doc/` — refreshed to the SQLite 3.8.3 documentation for this
  step, with `PROFILE.md` and `README.md` for this step;
* `TEST_INTERFACE.md` — refreshed for this step;
* this file and `timer.sh`, both refreshed for this step.

The overlay **replaces** files with the same name. Documentation, the profile,
this file and `timer.sh` are overlay-owned and will be replaced again next
step; everything else is yours and is preserved exactly as you leave it.

## Preservation rules

1. **Do not delete or move `sqlite-doc/`, `TEST_INTERFACE.md`,
   `WORKSPACE_HANDOFF.md` or `timer.sh`.** Later steps and the checkpoint
   assume they are present at these paths.
2. **Keep the build working at every point you might be interrupted.** The
   snapshot is taken when the step's time runs out, not when you decide you are
   finished, so a workspace that does not build is a workspace that scores
   nothing — commit to a compiling state early and improve it in place.
3. `build/` is **excluded** from the snapshot. Sources are what carry forward;
   never treat a built artefact as the deliverable.
4. Do not add generated code that cannot be rebuilt offline from the workspace,
   and do not add a dependency the image does not already have.
5. Leave the workspace in the layout `TEST_INTERFACE.md` §1 describes.

## Checkpoint

Before the step ends, confirm the checkpoint by hand. These commands use only
what `TEST_INTERFACE.md` already promises, and cost a few seconds:

```sh
cd /app
rm -rf build && make            # must succeed
test -f build/libsqlite3.a && test -f build/sqlite3

rm -f /tmp/checkpoint.db
./build/sqlite3 -init /dev/null /tmp/checkpoint.db \
  "CREATE TABLE t(a INTEGER PRIMARY KEY, b TEXT); INSERT INTO t VALUES(1,'x');"
./build/sqlite3 -init /dev/null /tmp/checkpoint.db "SELECT a, b FROM t ORDER BY a;"
# expected on stdout:  1|x

head -c 16 /tmp/checkpoint.db | od -c | head -1   # SQLite format 3\0

printf '#include "sqlite3.h"\n#include <stdio.h>\nint main(void){sqlite3*d;printf("%%d %%s\\n",sqlite3_open(":memory:",&d),sqlite3_libversion());return 0;}\n' > /tmp/h.c
cc -I/app/include /tmp/h.c /app/build/libsqlite3.a -o /tmp/h -lm -lpthread -ldl
/tmp/h                                            # expected: 0 3.8.3
```

The last line is the one that depends on *when* you run the checkpoint. The
release string comes from your own code, so it reads `3.8.3` only once
this step's implementation reports it (`TEST_INTERFACE.md` §3.3). On a step
that inherits an implementation, running the checkpoint before you change
anything will still print the **previous** step's release on that line — the
inherited build working correctly, not a regression. Every other line above
must pass at both moments, and the whole checkpoint must pass before the step
ends.


Run `bash /app/timer.sh` to see the remaining budget for this step.

## Restoring this workspace elsewhere

A standalone continuation restores the complete preceding snapshot first and
overlays this step's documentation on top of it — never the other way round.
The overlay is documentation, not a repository:

```sh
cp -a <previous-step-artifact>/workspace/. /app/
cp -a <this-step>/workdir/. /app/
```

If `/app` contained only this step's `workdir/`, there would be no
implementation to continue and the checkpoint above would fail at `make`.

## Verifier visibility

The verifier runs in a separate container, on a copy of `/app`, with its own
toolchain and its own cases. You cannot see it and you must not try to: nothing
under `/app` is read as a test, and the verifier's own build of your code
ignores anything you leave in `build/`.

## This step's starting point

`/app` already contains the implementation delivered for step 1 against SQLite
3.8.0: a working `Makefile`, `include/sqlite3.h`, `src/` and
`shell/`. Run the checkpoint above **before** you change anything — it is the
inherited contract, and this step's verifier re-exercises a sample of it.

Only the documentation, the profile, `TEST_INTERFACE.md`, this file and
`timer.sh` were replaced by this step's overlay. `sqlite-doc/` is now the
SQLite 3.8.3 documentation, and `sqlite-doc/README.md` lists what is new
in it.
