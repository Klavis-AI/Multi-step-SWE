# Step 5 — Redis 4.0: `UNLINK`, asynchronous flushes, LFU eviction and the `MEMORY` surface

## 1. Implement this milestone

Move the server in this workspace to **Redis 4.0**. `/app/tests` has been replaced with
Redis 4.0's own test suite and `/app/SPEC/commands.json` with its command surface; both
are the definition of what must work, including everything that already worked.

New in this release:

- **Lazy freeing**: `UNLINK`, `FLUSHALL ASYNC`, `FLUSHDB ASYNC`, and the
  `lazyfree-lazy-eviction`, `lazyfree-lazy-expire`, `lazyfree-lazy-server-del` and
  `slave-lazy-flush` parameters, with the `lazyfree_pending_objects` field of `INFO`.
- **LFU eviction**: the `allkeys-lfu` and `volatile-lfu` `maxmemory-policy` values, the
  `lfu-log-factor` and `lfu-decay-time` parameters, `OBJECT FREQ`, and the existing
  LRU/random/TTL policies, all as `/app/SPEC/redis-doc/lru-cache.md` describes them.
  Eviction must actually keep the dataset under `maxmemory`, report
  `evicted_keys` in `INFO stats`, and reply
  `OOM command not allowed when used memory > 'maxmemory'.` to a command that would
  add memory when it cannot -- while commands that only read still succeed.
- **The `MEMORY` command family**: `MEMORY USAGE key [SAMPLES n]`, `MEMORY STATS`,
  `MEMORY DOCTOR`, `MEMORY PURGE`, `MEMORY MALLOC-STATS`.
- **`SWAPDB`**, and `RANDOMKEY`/`SCAN` interaction with the above.
- **The RDB version bump**: this release writes a newer RDB version, and `DUMP` payloads
  carry it. Files written by every earlier Redis release must still load, and this
  release's files must be what a Redis 4.0 server would produce.

Redis 4.0 also introduced the C modules API. It is **out of scope for this
workspace**: the server here is Java, and loading a shared object written against
`redismodule.h` is not something it can do. Nothing about `MODULE` is required at this
milestone.

## 2. Read these local references

- `/app/SPEC/commands.json` — every command this milestone exposes, with its argument grammar and the
  release that introduced it. Commands absent from this file do not belong in this milestone.
- `/opt/docs/redis/commands/` — Redis's command reference, one Markdown page per command, as
  redis.io published it in August 2025: behaviour, replies and errors. The pages cover every
  release; each one's history says which release added or changed what.
- `/opt/docs/redis-rdb-format/` — a third-party description of the RDB file format, from
  redis-rdb-tools (MIT licence), not Redis's own specification. `RDB_File_Format.textile`
  gives the file layout and value encodings byte by byte for the formats up to Redis 2.8
  (RDB version 6, with the version-6 notes in `RDB_Version_History.textile`); for the later
  versions, up to and including this release's, the history only lists what each one added,
  without byte layouts.
- `/app/SPEC/redis.conf` — the parameters of this release.
- `/app/SPEC/redis-doc/lru-cache.md` and the other normative topic documents.
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
