# Step 4 — Redis 3.2: geospatial indexes, `BITFIELD`, `SPOP` with a count, effects replication

## 1. Implement this milestone

Move the server in this workspace to **Redis 3.2**. `/app/tests` has been replaced with
Redis 3.2's own test suite and `/app/SPEC/commands.json` with its command surface; both
are the definition of what must work, including everything that already worked.

New in this release:

- **Geospatial indexes** on the sorted-set type: `GEOADD`, `GEOPOS`, `GEODIST`,
  `GEOHASH`, `GEORADIUS`, `GEORADIUSBYMEMBER` and their `_RO` variants, with Redis's
  52-bit interleaved geohash score, its distance units (`m`, `km`, `mi`, `ft`), and the
  `WITHCOORD` / `WITHDIST` / `WITHHASH` / `COUNT` / `ASC` / `DESC` / `STORE` /
  `STOREDIST` options. A geo key is an ordinary sorted set and every sorted-set command
  keeps working on it.
- **`BITFIELD`** with its `GET`/`SET`/`INCRBY` operations, `u1`–`u63` and `i1`–`i64`
  types, `#`-relative offsets and the `OVERFLOW WRAP|SAT|FAIL` policy.
- **`SPOP key count`**, **`HSTRLEN`**, **`TOUCH`**, and the `quicklist` list encoding.
- **Effects replication for scripts**: a script may switch from whole-script replication
  to replicating the write commands it actually performed, in order and wrapped in
  `MULTI`/`EXEC`, by calling `redis.replicate_commands()`; `redis.set_repl()` then
  selects where those effects go (`REPL_ALL`, `REPL_AOF`, `REPL_SLAVE`, `REPL_NONE`) and
  is an error outside effects mode. Whole-script replication remains the default at this
  release.
- The `CONFIG` parameters this release adds, including `list-max-ziplist-size`,
  `protected-mode` and `supervised`.

## 2. Read these local references

- `/app/SPEC/commands.json` — every command this milestone exposes, with its argument grammar and the
  release that introduced it. Commands absent from this file do not belong in this milestone, with
  three exceptions that this snapshot of the table does not list but this release has and section 1
  requires: `TOUCH`, `GEORADIUS_RO` and `GEORADIUSBYMEMBER_RO` (their pages are under
  `/opt/docs/redis/commands/`).
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
