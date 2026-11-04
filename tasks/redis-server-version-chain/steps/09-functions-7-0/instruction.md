# Step 9 — Redis 7.0: functions, multi-part AOF, sharded pub/sub, ACL selectors

## 1. Implement this milestone

Move the server in this workspace to **Redis 7.0**. `/app/tests` has been replaced with
Redis 7.0's own test suite, `/app/SPEC/commands.json` with its command surface and
`/app/SPEC/commands/` with Redis's own machine-readable command metadata; all three are
the definition of what must work, including everything that already worked.

New in this release:

- **Functions**, as `/app/SPEC/redis-doc/functions-intro.md` specifies them: a Lua library
  is loaded with `FUNCTION LOAD [REPLACE] <code>` where the code starts with
  `#!lua name=<library>` and registers callbacks with `redis.register_function`; the
  library is invoked with `FCALL`/`FCALL_RO <function> <numkeys> ...`. `FUNCTION LIST`
  (with `LIBRARYNAME` and `WITHCODE`), `DELETE`, `FLUSH`, `DUMP`, `RESTORE`, `STATS` and
  `KILL` complete the family, and libraries survive `SAVE`/restart and appear in the RDB.
- **Multi-part AOF**: the append-only file becomes a directory named by `appenddirname`
  holding a base file, incremental files and a **manifest**, in Redis's own format.
  `BGREWRITEAOF` produces a new base and rotates the manifest; `aof_rewrites`,
  `aof_base_size`, `aof_current_size` and the `aof_*` INFO fields follow. An
  **old single-file append-only file must still load**: Redis 7.0 upgrades it in place.
- **Sharded pub/sub**: `SSUBSCRIBE`, `SUNSUBSCRIBE`, `SPUBLISH`, `PUBSUB SHARDCHANNELS`
  and `PUBSUB SHARDNUMSUB`.
- **ACL selectors and key permissions**: `ACL SETUSER u (...)` selectors, `%R~`, `%W~`
  and `%RW~` key patterns, `ACL DRYRUN`, and the `NOPERM` messages that name the key or
  channel.
- **Client eviction**: `maxmemory-clients`, the `client_recent_max_input_buffer` /
  `client_recent_max_output_buffer` / `evicted_clients` INFO fields, and
  `CLIENT NO-EVICT ON|OFF`.
- **Command metadata**: `COMMAND INFO` with ACL categories, tips and key specifications,
  `COMMAND LIST [FILTERBY ...]`, `COMMAND GETKEYSANDFLAGS`, and `COMMAND COUNT`, all
  matching `/app/SPEC/commands/`. `COMMAND DOCS` is part of the 7.0 surface
  (`/app/SPEC/commands/` lists it) but is not required at this step, unlike
  `COMMAND COUNT`, `COMMAND GETKEYS`, `COMMAND GETKEYSANDFLAGS`, `COMMAND INFO` and
  `COMMAND LIST`.
- The release's other additions: `OBJECT FREQ` under LFU, `EXPIRETIME`/`PEXPIRETIME`,
  `SINTERCARD`, `LPOS`-era list behaviour, `ZRANGESTORE` edge cases, `RESET`, the
  `listpack` encodings for lists and sets, and `DEBUG`-free `LATENCY HISTOGRAM`.

## Startup log

The test harness decides your server is up by grepping its log, and it greps
for two different strings:

- `Ready to accept` — initial capital, matched **case-sensitively** by the
  harness of this release — is what the suite of this release counts. Redis's
  own sentence is `Ready to accept connections`; text after it on the same line
  is ignored. (The 4.0-, 5.0- and 6.0-era harnesses, under which this server must also
  run, match it with `grep -i`, so this sentence satisfies them too.)
- `ready to accept` — lower case, also matched **case-sensitively** — is what
  the 2.8-era harness counts; an earlier release's suite must also run against this
  server under that harness, and it waits for this line before it runs a single
  test. Redis's own sentence there is
  `the server is now ready to accept connections on port <port>`.

Neither sentence satisfies the other grep, so your server must log **both**, once
each per start, on standard output or in its log file once the listening socket
is open. `SPEC/BUILD_AND_RUN.md` section 3 has the full startup log contract.

## 2. Read these local references

- `/app/SPEC/commands.json` and `/app/SPEC/commands/` — every command this milestone
  exposes, with arity, flags, key specifications, arguments and reply schema. Commands
  absent from them do not belong in this milestone.
- `/opt/docs/redis/commands/` — Redis's command reference, one Markdown page per command, as
  redis.io published it in August 2025: behaviour, replies and errors. The pages cover every
  release; each one's history says which release added or changed what.
  `/opt/docs/redis/develop/reference/` explains the metadata format of `/app/SPEC/commands/`
  (`command-arguments.md`, `key-specs.md`, `command-tips.md`).
- `/opt/docs/redis-rdb-format/` — a third-party description of the RDB file format, from
  redis-rdb-tools (MIT licence), not Redis's own specification. It covers RDB formats only up
  to Redis 5.0 (RDB version 9): `RDB_File_Format.textile` gives the file layout and value
  encodings byte by byte for the formats up to Redis 2.8 (RDB version 6), and
  `RDB_Version_History.textile` lists what versions 7 to 9 added, without byte layouts.
  Nothing newer than Redis 5.0's format is described there.
- `/app/SPEC/redis-doc/functions-intro.md` — normative for functions.
- `/opt/docs/lua-5.1/manual.html` — the Lua 5.1 reference manual, the language of scripts and
  function libraries.
- `/opt/docs/luaj-3.0.1/index.html` — the API documentation (javadoc) of LuaJ 3.0.1, the one
  dependency `/app/SPEC/BUILD_AND_RUN.md` allows.
- `/app/SPEC/redis.conf` — the parameters of this release.
- `/app/SPEC/BUILD_AND_RUN.md` — how the artifact is built, started and driven.
- `/app/tests` with `/app/runtest` — Redis's own suite for this version.
- `/app/WORKSPACE_HANDOFF.md` — keep it current.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`; the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the supplied documents, suite and
  handoff files. Do not create a prose answer or an alternate entry point.
- Do not search for, download, copy, wrap, or invoke an online
  implementation or task-specific answer; work only from this workspace, its documentation
  and `/opt/docs`, and do not search the system for other implementations or answers.
  Do not cheat by using online solutions or hints specific to this task.
- Run the local checks named above and leave a reproducible handoff for the next fresh
  session.
