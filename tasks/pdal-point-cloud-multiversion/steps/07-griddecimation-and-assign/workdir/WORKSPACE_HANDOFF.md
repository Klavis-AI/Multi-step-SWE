# Workspace handoff

This file is the same in every step. It describes what carries forward between
steps and how to pick the work up from a cold start.

## What carries forward

`/app` is cumulative. Whatever you leave there at the end of a step is what the
next step begins with — your source changes, your build scripts, your notes.
The next step then has its own `workdir/` copied on top of it, which adds and
replaces files but never deletes any.

`/app` is a working tree, not a repository you are being asked to keep tidy for
its own sake. Nothing inspects your history, your branch names, or your commit
messages. If you find version control useful for your own work, use it; if you
do not, nothing is lost.

## What does not carry forward

* **The conversation.** Each step is a fresh session with no memory of the
  previous one. Anything you want your successor to know has to be written into
  a file under `/app`.
* **Build output.** Build directories are excluded from the snapshot, and the
  grader always builds from scratch into a fresh directory. Do not leave
  something important only in `build/`.
* **Anything outside `/app`.** Files you write to `/tmp`, `$HOME`, or anywhere
  else are gone.

## Leaving a usable handoff

The single highest-value thing you can do in the last few minutes of a step is
write down what the next session cannot re-derive cheaply. A short
`/app/NOTES.md` that you append to each step is enough. Worth recording:

* which parts of the new behaviour you finished and which you left incomplete,
  and what specifically remains;
* any decision you made that is not obvious from the code — especially places
  where the specification was ambiguous and you picked a reading;
* anything you tried that did not work, so it is not tried twice;
* where the code that implements each recent capability actually lives.

Do not write a summary of the specification back into `NOTES.md`. Your
successor has the same documents you do. Write what only you know.

## Checkpointing during a step

Build and exercise your work as you go rather than at the end:

```sh
cmake -S /app -B /tmp/build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release -DWITH_TESTS=OFF \
      -DBUILD_PLUGIN_HDF=OFF -DBUILD_PLUGIN_NITF=OFF \
      -DBUILD_PLUGIN_PGPOINTCLOUD=OFF -DBUILD_PLUGIN_TILEDB=OFF \
      -DBUILD_PLUGIN_DRACO=OFF -DBUILD_PLUGIN_E57=OFF \
      -DBUILD_PLUGIN_ICEBRIDGE=OFF -DBUILD_PLUGIN_TRAJECTORY=OFF
cmake --build /tmp/build -j"$(nproc)"
/tmp/build/bin/pdal --version
```

That is the grader's build, verbatim. A full build takes roughly five minutes
on this machine, so there is time to run it several times; there is not time to
discover at the end that it never worked.

Use `/app/timer.sh` to see how much of the step's wall-clock budget is left.

## Restoring from cold

If you are resuming this work outside the normal step sequence, the workspace
is the previous step's snapshot with this step's `workdir/` on top:

```sh
cp -a <prior-artifacts>/workspace/. /app/
cp -a <this-step>/workdir/. /app/
```

The `workdir/` overlay alone is not a workspace — it contains the step's
documents and helpers, not the source tree.
