# Workspace handoff

This file is the running log this workspace carries between work sessions. A new
session starts with no memory of the previous one and only this repository plus
its own ticket. Keep this file accurate: what exists, where it lives, how to
build it, how to run it, what is known to work, and what is known to be missing.

## Layout

- `/spec/` — outside this workspace: the published Matrix specification
  releases, read-only. Every release the sessions so far have targeted is
  there, one directory each.
- `DENDRITE/` — the published interface this server is built to: Dendrite's own
  sample configuration, its manual installation documentation, and the source
  files that define its command-line flags.
- `selfcheck.sh` — starts the built server the way it is meant to be started and
  walks a handful of requests through it.

## Build

(record the build command and what it produces, and whether it tracks header
dependencies -- these headers keep gaining fields as the server grows, and a build
that recompiles only the edited `.c` files will link objects that disagree about
a struct's layout without reporting anything)

**Before you hand this workspace on, `make clean`.** What is left here must
rebuild from source without reusing any build output, and a workspace that
still holds object files or a linked binary is not a clean handoff.

## Run

(record the exact start command and the configuration keys that are honoured)

## State of the implementation

(record what is implemented, what is partially implemented, and what is absent)
