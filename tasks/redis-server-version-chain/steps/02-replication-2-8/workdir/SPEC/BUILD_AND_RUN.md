# How this server is built, started and driven

This document describes the *interface*: how the artifact is compiled, how it is
started, and how the test harness drives it. Every behavioural requirement is defined
by Redis's own specifications — see `SPEC/redis-doc/`, `SPEC/commands.json` and
`SPEC/redis.conf`.

## 1. Language and build

The server is written in **Java**. Nothing else in the tree is compiled: no source
file in another compiled language (C, C++, Go, Rust, Swift, C#, Kotlin, Scala,
Objective-C, ...) may appear anywhere under `/app` outside `tests/`.

```sh
make -C /app
```

`make` with no target must build from the Java sources in the tree and produce two executables, **`/app/src/redis-server`** and
**`/app/src/redis-sentinel`**.
Before `make` runs, every `.class`, `.jar`, `.so`, `.o` and `.a` under `/app` and
every `build/`, `out/`, `target/` and `classes/` directory is deleted,
so nothing may be pre-built and no compiled artefact may be checked in. The rest
of the collected tree is left as it is, launchers included: `make` may generate
the launchers or keep ones you wrote under `/app/src`, as long as they are there
and executable when it exits, and it must compile the classes they run. If that
`make` exits non-zero, compiles nothing, or leaves a launcher missing or not
executable, the collected tree is restored, the launchers are deleted as
well (`src/redis-server`, `src/redis-sentinel`, `src/redis-cli`,
`src/redis-benchmark`, `src/redis-check-aof`, `src/redis-check-rdb`) and
`make` runs once more; the second build is the one used. A `make` that times
out is not run again.

**Resource ceiling.** The container is given 7 CPUs and **16 GB**, and
the test harness starts up to sixteen server instances at once. The arithmetic
that matters is `16 x your -Xmx` against that 16 GB, not what one server needs:
keep the ceiling at or below **768 MB** per instance (`-Xmx768m`), which leaves
about 3.7 GB for sixteen JVMs' own overhead and the harness. A ceiling above the
container's limit is worse than no ceiling, because the kernel then kills the
whole container rather than one JVM. Pass
`-XX:+ExitOnOutOfMemoryError` so a server that does exhaust its heap dies by
itself. Your `Makefile` is used unchanged, so the ceiling you write is
the ceiling that applies. Do not size any parallelism from
`nproc` or `Runtime.availableProcessors()` — inside a container those report the
node's CPU count, not the pod's.

The only third-party dependency available is **LuaJ** at
`/opt/redis-deps/luaj-jse-3.0.1.jar`, present at that path both in this container
and in the clean container your sources are built in. Reference it from the
classpath; do not copy it into the tree. `make` must not depend on anything
else, because your sources are built with that single dependency at that path
and nothing else.

### What is collected

What gets built is a snapshot of your work, not the container. Exactly three
paths are collected and restored into a clean `/app` before building:

```
/app/src                  your Java sources and the launchers
/app/Makefile
/app/WORKSPACE_HANDOFF.md
```

Everything your build needs must live under those paths, and every byte of them
must be **plain ASCII**. The transport that collects them decodes as UTF-8 and
replaces what it cannot decode, so one non-ASCII character anywhere under
`/app/src`, in your `Makefile`, or in `WORKSPACE_HANDOFF.md` can corrupt the
collected copy, and the corrupted copy is then what gets compiled, not
yours. Keep comments and identifiers ASCII, and write a non-ASCII character in a
Java string literal as a backslash-u escape rather than as the character itself.
Worth checking before you finish -- nothing should be listed:

```sh
LC_ALL=C grep -rlP '[^\x00-\x7F]' /app/src /app/Makefile /app/WORKSPACE_HANDOFF.md
```

Build output belongs under `/app/build`, which is not collected, so the build
always starts from source in a container that has never seen your build.

## 2. The launcher

`/app/src/redis-server` is started the way Redis's own test harness starts Redis:

```sh
src/redis-server <config-file>
```

with the working directory set to the root of a Redis source tree (a separate,
unmodified copy of the test suite), and with `<that tree>/src/redis-server` a
**symbolic link** to `/app/src/redis-server`, or a small wrapper that `exec`s
it. `make` and every server instance run as an unprivileged user,
not root; as in Redis, a pid file the server cannot write is logged, not fatal.
Therefore:

- the launcher must resolve its own real path (`readlink -f "$0"`) rather than
  trusting `$0`'s directory, and must work from any working directory;
- it must `exec` the JVM, so the process id the caller sees is the process that
  serves clients and receives `SIGTERM`;
- the process that holds the listening socket must be a JVM, and its class path
  must point at the classes `make` produced under `/app` (the class files must
  be newer than the start of that build). Wrapping, embedding,
  proxying or re-exporting an existing Redis server — or any other existing
  key-value server — is not allowed. That applies to every process, not only
  the one holding the socket: from the end of the build until testing ends,
  every process running as your server's user must be the system `java` (or
  the JDK's own process-spawn helper), or a
  program from one of Debian's Essential packages (a shell, or coreutils such as
  `env`, `readlink` and `sleep`; not `awk`, `perl` or `python3`) used as a
  launcher step or a stand-in child process. An
  executable or native library loaded from anywhere your server's user can
  write, a language interpreter, or a native object whose name looks like an
  existing key-value server (`redis`, `hiredis`, `valkey`, `keydb`,
  `dragonfly`, `memcached`) is not allowed either.

`SIGTERM` shuts the server down the way Redis does: it stops accepting
connections, performs the shutdown save if one is configured, and exits.

`/app/src/redis-sentinel` starts the same program in Sentinel mode — that is how
Redis does it, and `redis-server <config> --sentinel` is equivalent. `make` must
produce it too.

## 3. Startup log

The harness watches the server log (`logfile`, or standard output when `logfile`
is empty) to decide the server is up. Reproduce Redis's own startup output; the
parts this version's harness reads are:

- a line containing `PID: <pid>` with the real process id;
- the sentence `the server is now ready to accept connections on port <port>` once the listening socket is open — one such line per
  start, because the harness counts them to detect restarts.

If the configured port cannot be bound, log it the way Redis does — a line
beginning `Warning: Could not create server TCP listening socket <addr>:` and
ending with the reason (`Address already in use`), then
`Failed listening on port <port> (tcp), aborting.` — and exit non-zero. The
harness greps for those and retries on a different port.

### `DEBUG`

Redis's own test suite uses Redis's `DEBUG` command as a control surface. The
tests this server is run against use exactly these subcommands, with Redis's
own meanings:

`DEBUG DIGEST`, `DEBUG LOADAOF` (flush the append-only buffer and reload from it), `DEBUG OBJECT <key>`, `DEBUG POPULATE <count> [prefix] [size]`, `DEBUG RELOAD` (save the dataset and load it back), `DEBUG SET-ACTIVE-EXPIRE 0|1` and `DEBUG SLEEP <seconds>`.

The `DEBUG DIGEST` family is the SHA-1-based dataset and
per-value checksum `src/debug.c` computes, which the suite uses to compare a
master with its replica and a dataset with its reloaded self.

Nothing outside that list is required.

## 4. Hooks — one hook, and it is `redis.conf`

The server is configured **only** through Redis's own configuration
mechanism: a `redis.conf` file in Redis's own format passed as the first
argument, plus `CONFIG SET` over the wire. No other flag is
ever passed. Everything needed to find and drive the server is a
documented Redis parameter:

| what is needed | Redis parameter |
| --- | --- |
| where to listen | `bind`, `port` |
| where data lives | `dir`, `dbfilename`, `appendfilename` |
| where the log goes | `logfile` |
| where the pid file goes | `pidfile` |

There is no `--mode`, no `--profile`, no `--compat`, no invented `--enable-*`,
no version banner and no marker file. Behaviour is selected
the way Redis selects it: by the command the client sends and by the parameters
`SPEC/redis.conf` documents.

## 5. How the server is driven

The server is driven by **Redis's own Tcl test suite** — `./runtest` — run from
a separate, unmodified copy of the suite, with `src/redis-server` linked to your
build. It therefore uses the harness's own contract:

- it writes a configuration file derived from `tests/assets/default.conf` and
  passes it as the first argument, with per-test overrides written into that file;
- it starts and restarts the server many times, in parallel (up to 16 concurrent
  test clients), each instance with its own `dir` and its own port;
- it gives each test run through `./runtest` 240 seconds to finish, with `./runtest --timeout 240`;
- it drives the server through `CONFIG GET`/`CONFIG SET` for the parameters the
  tests exercise;
- it kills each instance with `SIGTERM` and waits for the process to exit.

Redis's own multi-instance harness `./runtest-sentinel` is run as well; it starts
several `src/redis-server` and `src/redis-sentinel` processes from generated configuration files and drives
them.

Beyond what the suite asserts, the server's replies must match what a **real
Redis of this release** answers to the same commands. That includes behaviour
this release defines that its own test suite never asserts directly --
configuration parameters and their defaults, `INFO` field sets, and the command
surfaces this milestone introduces, including their error replies.

Only the part of a reply the release actually defines has to match: values no
two servers could agree on -- memory figures, internal counters, scan cursors,
approximate search results, opaque dump payloads and clock-derived values --
are exempt.

Edits you make under `/app/tests` affect only your own local runs: the server is
always driven by that separate copy of the suite.

Your server is also started on directories that already contain
RDB and AOF files **written by real Redis servers**, and the dataset it then
serves — through `KEYS`, `TYPE`, `PTTL`, `GET`, `LRANGE`,
`SMEMBERS`, `ZRANGE ... WITHSCORES`, `HGETALL` — must match what real Redis
serves from the same files.

## 6. Running the suite yourself

The workspace carries Redis's own test suite under `/app/tests`, with
`/app/runtest` to run it:

```sh
make -C /app
cd /app && ./runtest --clients 16              # everything in your copy
cd /app && ./runtest --single unit/type/list   # one file
```

Your copy of the harness carries two adjustments, documented at the top of
`tests/support/agent_harness.tcl`, and every run of the suite against this
server makes the same two: it waits up
to a minute rather than five seconds for a starting server, because a JVM
under sixteen concurrent test clients needs longer than a C binary and the older
harness would otherwise report `Cant' start the Redis server` and skip the rest
of the file; and a test that raises is recorded as a failed test rather than
ending its test client, which is what redis's own `--durable` does from 6.2
onward. Neither changes what makes a test pass.

A local run is a guide rather than a verdict in both directions. Some of the
suite's files are not in your copy, so a clean local run does not cover
everything the server is run against. And tests that reach into the server's
internals -- `DEBUG OBJECT` encodings, `DEBUG LISTPACK`/`SDS`, memory
introspection -- are not required, so a local failure in one of those is not
necessarily a defect.
