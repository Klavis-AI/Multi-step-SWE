# Step 10 — Redis 7.2 and 7.4: `CLIENT NO-TOUCH`, `WAITAOF`, hash-field expiration

## 1. Implement this milestone

Move the server in this workspace to **Redis 7.4**, via the 7.2 release. `/app/tests` has
been replaced with Redis 7.4's own test suite, `/app/SPEC/commands.json` with its command
surface and `/app/SPEC/commands/` with Redis's own machine-readable command metadata; all
three are the definition of what must work, including everything that already worked.

New in these releases:

- **Hash-field expiration** (7.4): `HEXPIRE`, `HPEXPIRE`, `HEXPIREAT`, `HPEXPIREAT`,
  `HPERSIST`, `HTTL`, `HPTTL`, `HEXPIRETIME` and `HPEXPIRETIME`, each with the
  `FIELDS <numfields> <field> ...` argument form and Redis's per-field reply codes. A field with a TTL disappears from every read once it expires; the key is
  deleted when its last field goes; the `hexpire`/`hpersist`/`hexpired` keyspace events
  fire; the TTLs survive `SAVE`/restart and `DUMP`/`RESTORE`, which means the RDB gains
  the hash-with-metadata encodings; and the expiry is propagated to replicas and the AOF
  as `HPEXPIREAT`, with an `HDEL` when a field actually disappears.
- **`WAITAOF numlocal numreplicas timeout`** (7.2) and the `fsync`-offset bookkeeping it
  reports.
- **`CLIENT NO-TOUCH ON|OFF`** (7.2), which stops commands from updating a key's LRU/LFU
  access information, and the `T` flag in the client line.
- **`ACL` additions** of these releases: `ACL GETUSER` reporting selectors in the 7.2
  shape, `ACL SETUSER` validation of key and channel patterns, and the `ACL` categories
  the new commands belong to.
- The 7.2 command surface additions and behaviour changes the suite exercises, including
  `ZRANK`/`ZREVRANK` with `WITHSCORE`, `SINTERCARD` limits, listpack conversions, and
  `CLUSTER` sharded pub/sub propagation.

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
- `/app/SPEC/redis.conf` — the parameters of these releases.
- `/app/SPEC/redis-doc/` — the normative topic documents.
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
