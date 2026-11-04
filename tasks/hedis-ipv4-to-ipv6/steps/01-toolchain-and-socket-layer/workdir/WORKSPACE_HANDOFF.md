# Workspace handoff

This file describes the workspace itself: what is in it, how to build it, how
to run it, and what is expected of the state you leave behind. It says nothing
about what to change — that is in the instruction you were given.

## What is here

`/app` is a C source tree: a fork of an in-memory data-structure server, with
its bundled dependencies under `deps/` and its own test suite under `tests/`.
It is the working copy: whatever you leave in it is the result of this work.

| Path | What it is |
| --- | --- |
| `Makefile`, `src/` | The server, the two command-line tools, and the checkers. |
| `deps/hiredis/` | The bundled client library the tools and the server use. |
| `deps/jemalloc/`, `deps/lua/`, `deps/linenoise/` | Vendored third-party code. |
| `redis.conf`, `sentinel.conf` | The shipped, annotated configuration files. |
| `tests/`, `runtest`, `runtest-cluster`, `runtest-sentinel` | The repository's own suite. |
| `README.md`, `INSTALL`, `00-RELEASENOTES` | The project's own documentation. |
| `DOCS/` | Reference notes placed here for this work. Read all of them. |
| `timer.sh` | Advisory countdown for the current session's budget. |

## Building

From `/app`:

```sh
make                 # builds deps/ and then src/
make -j"$(nproc)"    # the same, in parallel
make distclean       # removes build output, including deps/
```

The compiler and C library are the ones the container provides; `cc --version`
reports which. The `Makefile` passes the standard `CFLAGS` variable through to
every compilation, so `make CFLAGS=...` is the supported way to add a flag to a
build without editing the tree, and it must keep working.

A successful build leaves six programs in `src/`: `redis-server`, `redis-cli`,
`redis-sentinel`, `redis-benchmark`, `redis-check-rdb` and `redis-check-aof`.
The last two are hard links to the server binary.

## Running

```sh
src/redis-server --port 7777 --bind ::1            # a server on one address
src/redis-server /path/to/redis.conf              # or from a config file
src/redis-cli -h ::1 -p 7777 ping                 # the interactive client
src/redis-benchmark -h ::1 -p 7777 -n 100 -q      # the load generator
```

`redis.conf` documents every configuration directive, including `bind`,
`unixsocket` and `port`. The server logs to standard output unless `logfile` is
set.

## The repository's own suite

```sh
./runtest                    # the main suite (tcl is installed)
./runtest --single unit/type/string
./runtest-sentinel
./runtest-cluster
```

These are the repository's own tests, and they are part of the source tree: do
not weaken, skip, rename or delete them, and do not edit them. A suite that had
to be changed before it would pass is not a suite that passed. If one of them
looks as though it contradicts the work you were asked to do, leave it as it is
and write down here which test it was and what you concluded.

## What to leave behind

* A tree that builds from clean with `make` and runs.
* This file, kept accurate: if you changed how the tree is built or run, or
  found something about it that is not obvious from the tree itself, write it
  down here.
* The files under `DOCS/`, unchanged.
* Nothing else is required. There is no report to write.
