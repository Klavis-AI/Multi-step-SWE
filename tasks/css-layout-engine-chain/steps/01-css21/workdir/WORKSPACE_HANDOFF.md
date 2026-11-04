# Workspace handoff

`/app` is a cumulative workspace: this directory is the project. It contains this directory and nothing else — no conversation, no notes beyond these files.

## Layout of this workspace

```
/app/Makefile             create this; `make` must build ./build/cssdriver
/app/src/                 create this; your engine sources
/app/TEST_INTERFACE.md    how the engine is built, started and driven
/app/SPEC/                the normative documents each milestone rests on
/app/css-corpus/          practice documents, their recorded values and a runner
/app/WORKSPACE_HANDOFF.md this file
```

## What to leave behind

* `make` must succeed from a clean checkout of this directory and must produce
  `/app/build/cssdriver`. It is rebuilt from source, by an unprivileged user, in
  a container that has a C toolchain (cc, no C++ compiler) and python3.
  The remote end is written in C11; a C++ source tree will not build.
* Keep `TEST_INTERFACE.md` and `SPEC/` intact and extend them rather than
  replacing them.
* Record in this file what is implemented and what is not, so it does not have
  to be inferred from the source.

## State of the engine

(Replace this section as you implement.)
