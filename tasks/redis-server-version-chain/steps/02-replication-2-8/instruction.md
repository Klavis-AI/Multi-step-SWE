# Step 2 — Redis 2.8 replication: `PSYNC`, the replication backlog, and Sentinel

## 1. Implement this milestone

The server in this workspace already speaks RESP and serves the Redis 2.8 data model and
both persistence formats. Add **replication and Sentinel**, in Redis 2.8's own terms:

- **`SLAVEOF <host> <port>` / `SLAVEOF NO ONE`** and the `slaveof` configuration
  directive, with the replica-side state machine: connect, `PING`,
  `REPLCONF listening-port`, `REPLCONF capa`, then `PSYNC`.
- **`SYNC` and `PSYNC <run id> <offset>`** on the master side: `+FULLRESYNC <run id>
  <offset>` followed by the RDB payload and then the live command stream, or
  `+CONTINUE` served out of the **replication backlog** when the replica's offset is
  still inside it. `repl-backlog-size`, `repl-backlog-ttl`, `repl-ping-slave-period`,
  `repl-timeout`, `repl-disable-tcp-nodelay` and `repl-diskless-sync` behave as
  `/app/SPEC/redis.conf` documents.
- **Propagation**: every write is replicated as the command the master actually applied,
  with `SELECT` emitted when the database changes. Keys that expire on the master are
  replicated as an explicit `DEL`; a replica never expires a key on its own, but hides
  logically-expired keys from reads.
- **Replica behaviour**: `slave-read-only`, `slave-serve-stale-data`, `slave-priority`,
  `min-slaves-to-write`/`min-slaves-max-lag`, and `REPLCONF ACK <offset>`.
- **`ROLE`** and the `# Replication` section of `INFO` with Redis's own field names —
  `role`, `connected_slaves`, one line per connected replica,
  `master_repl_offset`, `master_link_status`, `master_sync_in_progress`,
  `slave_repl_offset`, `repl_backlog_active`, `repl_backlog_histlen`, and the
  `sync_full` / `sync_partial_ok` / `sync_partial_err` counters in `# Stats`.
- **Redis Sentinel**, started as `src/redis-sentinel <config-file>` — the same program
  under that name, which is how Redis enters Sentinel mode. `make` must produce it
  alongside `src/redis-server`. Sentinel monitors the masters its configuration file
  names, discovers their replicas through `INFO` and the other sentinels through the
  `__sentinel__:hello` channel, detects subjective and objective failure, elects a
  leader by configuration epoch, performs automatic failover, reconfigures the remaining
  replicas, rewrites its own configuration file, and publishes the event stream
  (`+sdown`, `-sdown`, `+odown`, `+vote-for-leader`, `+failover-state-*`,
  `+switch-master`, …). It answers the `SENTINEL` command family as
  `/app/SPEC/redis-doc/sentinel.md` documents it, plus `PING`, `INFO`, `SUBSCRIBE`,
  `PUBLISH` and `SHUTDOWN`. That document's command list is not exhaustive; three
  further subcommands are required and are absent from it. `SENTINEL SET <master>
  <option> <value>` sets `quorum`, `down-after-milliseconds`, `failover-timeout`
  and `parallel-syncs` at run time. `SENTINEL master <master>` returns that one
  master's state as a field/value map — the singular of the documented `SENTINEL
  masters`. `SENTINEL ckquorum <master>` replies `+OK <n> usable Sentinels …`
  when both the configured quorum and the majority needed to authorise a failover
  are reachable, and an error whose message begins `NOQUORUM` when either is not.

Everything the server already does must keep working unchanged; `/app/tests` is the same
Redis 2.8 suite and is the definition of "unchanged".






## 2. Read these local references

- `/app/SPEC/redis-doc/replication.md` and `sentinel.md` — normative.
- `/app/SPEC/redis.conf` — the replication and Sentinel parameters.
- `/app/SPEC/commands.json` — the command table for this version. `SYNC`, `PSYNC`,
  `REPLCONF` and the `SENTINEL` family are replication-internal and are defined by
  `replication.md`, `sentinel.md` and Redis's own suite rather than by that table.
- `/opt/docs/redis/commands/` — Redis's command reference, one Markdown page per command, as
  redis.io published it in August 2025: behaviour, replies and errors. The pages cover every
  release; each one's history says which release added or changed what.
- `/app/SPEC/BUILD_AND_RUN.md` — how the artifact is built, started and driven.
- `/app/tests` with `/app/runtest`, `/app/runtest-sentinel` — Redis's own suites. The
  replication units are under `tests/integration/`; the Sentinel units under
  `tests/sentinel/tests/`, driven by `./runtest-sentinel`, which starts several
  `src/redis-server` and `src/redis-sentinel` instances of its own.
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
