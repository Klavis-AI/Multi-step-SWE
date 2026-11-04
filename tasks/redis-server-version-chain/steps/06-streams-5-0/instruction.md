# Step 6 — Redis 5.0: the stream type and its consumer groups

## 1. Implement this milestone

Move the server in this workspace to **Redis 5.0**. `/app/tests` has been replaced with
Redis 5.0's own test suite and `/app/SPEC/commands.json` with its command surface; both
are the definition of what must work, including everything that already worked.

New in this release:

- **The stream type**, as `/app/SPEC/redis-doc/streams-intro.md` specifies it: `XADD` with
  explicit and auto-generated `<ms>-<seq>` IDs and `MAXLEN [~]` trimming, `XLEN`,
  `XRANGE`/`XREVRANGE` with partial IDs and `COUNT`, `XREAD` with `COUNT` and `BLOCK`,
  `XDEL`, `XTRIM`, `XSETID`, and `XINFO STREAM|GROUPS|CONSUMERS`.
- **Consumer groups**: `XGROUP CREATE|SETID|DESTROY|DELCONSUMER` (with `MKSTREAM`),
  `XREADGROUP` with `>` and with a history ID and `NOACK`, the pending-entries list,
  `XACK`, `XPENDING` in both its summary and extended forms, and `XCLAIM` with
  `IDLE`/`TIME`/`RETRYCOUNT`/`FORCE`/`JUSTID`.
- Streams are persisted: a stream, its groups, its consumers and their pending entries
  survive `SAVE`/restart and appear in the AOF, and `DUMP`/`RESTORE` round-trip them.
- Blocking `XREAD`/`XREADGROUP` behave like the other blocking commands: they do not
  block inside `MULTI`, and when a push wakes them the effect is replicated as the
  equivalent non-blocking command.
- **`ZPOPMIN`, `ZPOPMAX`, `BZPOPMIN`, `BZPOPMAX`** on sorted sets.
- **`CLIENT ID`** and **`CLIENT UNBLOCK`**, and the `XINFO HELP` reply.
- **`REPLICAOF`** as the new name of `SLAVEOF`, and the `replica-*` aliases of every
  `slave-*` parameter, both accepted. Both spellings must be accepted from a client and in
  the configuration file, and `CONFIG GET` must answer for the `replica-*` names as well as
  the `slave-*` ones: clients may use the `REPLICAOF` spelling, while Redis's own suite
  still drives replication with `SLAVEOF`.

## Startup log

The test harness decides your server is up by grepping its log, and it greps
for two different strings:

- `Ready to accept` — initial capital, matched **case-sensitively** by the
  harness of this release — is what the suite of this release counts. Redis's
  own sentence is `Ready to accept connections`; text after it on the same line
  is ignored. (The 4.0-era harness, under which this server must also run, matches
  this with `grep -i`, so the sentence satisfies that one too.)
- `ready to accept` — lower case, also matched **case-sensitively** — is what
  the 2.8-era harness counts; an earlier release's suite must also run against this
  server under that harness, and it waits for this line before it runs a single
  test. Redis's own sentence there is
  `the server is now ready to accept connections on port <port>`.

Neither sentence satisfies the other grep, so your server must log **both**, once
each per start, on standard output or in its log file once the listening socket
is open. `SPEC/BUILD_AND_RUN.md` section 3 has the full startup log contract.

## 2. Read these local references

- `/app/SPEC/commands.json` — every command this milestone exposes, with its argument grammar and the
  release that introduced it. Commands absent from this file do not belong in this milestone, with
  one exception that this snapshot of the table does not list but this release has and section 1
  requires: `XSETID` (its page is under `/opt/docs/redis/commands/`).
- `/opt/docs/redis/commands/` — Redis's command reference, one Markdown page per command, as
  redis.io published it in August 2025: behaviour, replies and errors. The pages cover every
  release; each one's history says which release added or changed what.
- `/opt/docs/redis-rdb-format/` — a third-party description of the RDB file format, from
  redis-rdb-tools (MIT licence), not Redis's own specification. `RDB_File_Format.textile`
  gives the file layout and value encodings byte by byte for the formats up to Redis 2.8
  (RDB version 6, with the version-6 notes in `RDB_Version_History.textile`); for the later
  versions, up to and including this release's, the history only lists what each one added,
  without byte layouts.
- `/app/SPEC/redis-doc/streams-intro.md` — normative for the stream type.
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
