# Step 1 — Redis 2.8: RESP, the core types, expiry, pub/sub, transactions, scripting, RDB and AOF

## 1. Implement this milestone

Build, in **Java**, a server compatible with **Redis 2.8** — complete enough that Redis's
own test suite for 2.8 drives it. That means, all in Redis 2.8's own terms:

- the **RESP** protocol on the TCP port, including inline commands, the multibulk limits
  and the protocol-error replies;
- the **`redis.conf`** surface: the file format, the parameter names of
  `/app/SPEC/redis.conf`, and `CONFIG GET`/`CONFIG SET`;
- the **2.8 command surface** listed in `/app/SPEC/commands.json`, other than the
  replication commands SLAVEOF, SYNC, PSYNC and REPLCONF, which this milestone does not
  cover — strings,
  lists, sets, sorted sets, hashes, HyperLogLog, key management, `SORT`, `SCAN`/`HSCAN`/
  `SSCAN`/`ZSCAN`, bit operations, `DUMP`/`RESTORE`/`MIGRATE`, `INFO`, `CLIENT`,
  `SLOWLOG`, `MONITOR`, `DEBUG` — with Redis's arities, reply shapes and error strings;
- **key expiry**: the `EXPIRE` family, `TTL`/`PTTL`/`PERSIST`, and lazy and active
  expiration;
- **keyspace notifications** as `/app/SPEC/redis-doc/notifications.md` defines
  them, driven by `notify-keyspace-events`;
- **pub/sub**: `SUBSCRIBE`/`UNSUBSCRIBE`/`PSUBSCRIBE`/`PUNSUBSCRIBE`/`PUBLISH`/`PUBSUB`
  and the subscriber-mode command restriction;
- **transactions**: `MULTI`/`EXEC`/`DISCARD`/`WATCH`/`UNWATCH` with Redis's queueing,
  abort and CAS semantics;
- **Lua scripting**: `EVAL`/`EVALSHA`/`SCRIPT`, Redis's Lua↔RESP conversion rules, the
  `redis.*` table, `cjson`, the deterministic-randomness rules, and the sandbox, as
  `/app/SPEC/redis-doc/eval-intro.md` describes them;
- **persistence**: the **RDB** file format and the **append-only file**, both written and
  read, as `/app/SPEC/redis-doc/persistence.md` describes them — `SAVE`, `BGSAVE`,
  `BGREWRITEAOF`, the `save`/`appendonly`/`appendfsync` parameters, the
  `rdb_bgsave_in_progress` and `aof_rewrite_in_progress` fields of `INFO persistence`
  that tell a client when a background child has finished, and loading at startup.

Two things about persistence are required in their own right, so they are worth stating
twice: an RDB file **written by another Redis server** must load and yield exactly the same
dataset, and an append-only file **written by another Redis server** must replay to exactly
the same dataset. The RDB format is versioned and self-describing; older files stay loadable.

Error messages, reply shapes and edge-case behaviour are part of the specification.
Nothing from a Redis release after 2.8 belongs in this milestone.

## 2. Read these local references

**Normative semantics.**
- `/app/SPEC/redis-doc/protocol.md` — RESP.
- `/app/SPEC/redis-doc/keyspace.md`, `notifications.md`, `transactions.md`,
  `eval-intro.md`, `persistence.md`.
- `/opt/docs/lua-5.1/manual.html` — the Lua 5.1 reference manual, the scripting language
  `eval-intro.md` names.
- `/app/SPEC/commands.json` — every command this milestone exposes, with its
  argument grammar and the release that introduced it.
- `/opt/docs/redis/commands/` — Redis's command reference, one Markdown page per command, as
  redis.io published it in August 2025: behaviour, replies and errors. The pages cover every
  release; each one's history says which release added or changed what.
- `/app/SPEC/redis.conf` — Redis 2.8's own annotated configuration file.

**Interface and workspace.**
- `/app/SPEC/BUILD_AND_RUN.md` — how the artifact is built, started and driven: the build
  command, the launcher contract, the startup log the harness waits for, the
  configuration hook table, and how the test harness drives the server. Read it first.
- `/opt/docs/luaj-3.0.1/index.html` — the API documentation (javadoc) of LuaJ 3.0.1, the one
  dependency `/app/SPEC/BUILD_AND_RUN.md` allows.
- `/app/SPEC/PROVENANCE.md` — where each shipped file came from.
- `/app/WORKSPACE_HANDOFF.md` — keep it current for the next fresh session.
- `/app/tests/` with `/app/runtest` — Redis's own test suite for this version. Build, then
  `cd /app && ./runtest --clients 16`, or `./runtest --single unit/type/hash`. Your copy
  of the suite is representative, not exhaustive, so a clean local run is necessary but
  not sufficient.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`; the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the supplied documents, suite and
  handoff files. Do not create a prose answer or an alternate entry point: the only entry
  point is the one `/app/SPEC/BUILD_AND_RUN.md` defines.
- Do not search for, download, copy, wrap, or invoke an online
  implementation or task-specific answer; work only from this workspace, its documentation
  and `/opt/docs`, and do not search the system for other implementations or answers.
  Do not cheat by using online solutions or hints specific to this task.
- Run the local checks named above and leave a reproducible handoff for the next fresh
  session.
