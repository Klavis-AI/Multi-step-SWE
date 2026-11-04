# Step 3 — Redis 3.0 Cluster: hash slots, redirection, the cluster bus and failover

## 1. Implement this milestone

Add **Redis Cluster** to the server in this workspace, as Redis 3.0 defines it and as
`/app/SPEC/redis-doc/cluster-spec.md` specifies it:

- **`cluster-enabled yes`** starts the node in cluster mode: a single database, the
  `cluster-config-file` persisted node table in Redis's own `nodes.conf` format, and a
  second listening socket on `port + 10000` for the cluster bus.
- **Key space partitioning**: 16384 hash slots, `CRC16(key) mod 16384` with hash tags
  (`{…}`), and the `CLUSTER` command family — `INFO`, `MYID`, `NODES`, `SLOTS`, `MEET`,
  `FORGET`, `REPLICATE`, `ADDSLOTS`, `DELSLOTS`, `SETSLOT` with
  `MIGRATING`/`IMPORTING`/`STABLE`/`NODE`, `GETKEYSINSLOT`, `COUNTKEYSINSLOT`, `KEYSLOT`,
  `SET-CONFIG-EPOCH`, `BUMPEPOCH`, `SLAVES`, `FAILOVER`, `RESET`,
  `COUNT-FAILURE-REPORTS`, `SAVECONFIG`.
- **Redirection**: `-MOVED <slot> <ip>:<port>` for a slot this node does not serve,
  `-ASK` during migration with the one-shot `ASKING` flag, `-CROSSSLOT` for a multi-key
  command whose keys do not hash to one slot, `-TRYAGAIN` while keys are in flight,
  `-CLUSTERDOWN` when the cluster is not `ok` and `cluster-require-full-coverage`
  demands it, and `READONLY`/`READWRITE` for replica reads.
- **The cluster bus** on `port + 10000`, as `/app/SPEC/redis-doc/cluster-spec.md`
  describes it: the binary message format Redis nodes exchange, `PING`/`PONG`/`MEET`
  gossip carrying each node's flags, epochs and slot ownership, `PFAIL` promoted to
  `FAIL` once a majority of masters report it, the `FAIL` broadcast, `PUBLISH`
  propagation, and `UPDATE` for stale configurations. Nodes of this implementation only
  ever talk to each other, so the exact byte layout is yours to choose — but it must
  carry everything the specification says a node has to learn from gossip.
- **Failover**: a replica whose master is `FAIL` waits its rank delay, bumps
  `currentEpoch`, requests votes with `FAILOVER_AUTH_REQUEST`, and on a majority of
  `FAILOVER_AUTH_ACK` takes over its master's slots with a greater `configEpoch`.
  `CLUSTER FAILOVER` performs the manual and `FORCE`/`TAKEOVER` variants.
- **Resharding**: moving a slot with `CLUSTER SETSLOT … MIGRATING` / `… IMPORTING`,
  `CLUSTER GETKEYSINSLOT` and `MIGRATE … KEYS …`, finishing with
  `CLUSTER SETSLOT <slot> NODE <id>` on both sides.

Everything the server already does in non-cluster mode must keep working unchanged, and
`/app/tests` is the definition of "unchanged".

**What must work end to end.** Redis's own `./runtest-cluster` must pass against
twenty instances of your build -- slot allocation, join and auto-discovery, fifty
thousand keys written and read back through the cluster client (which follows
`-MOVED` for you, so a wrong redirection fails the read), crash and restart of every
instance, replica consistency and the failover loop. The single-node cluster surface
must also answer as a real Redis 3.0.7 answers: `CLUSTER MYID`, `KEYSLOT`, `INFO`,
`COUNTKEYSINSLOT`, `GETKEYSINSLOT`, `SETSLOT` with
`MIGRATING`/`IMPORTING`/`STABLE`, `SLOTS`, `NODES`, `SAVECONFIG`,
`COUNT-FAILURE-REPORTS`, `SLAVES`, `ASKING`, `READONLY`, `READWRITE`, and
`-CROSSSLOT` for a multi-key command whose keys span slots. The `-ASK` redirect and
`MIGRATE ... KEYS ...` of a live slot migration between two nodes are required too, as
section 1 describes them.






## 2. Read these local references

- `/app/SPEC/redis-doc/cluster-spec.md` — normative for slots, redirection, the bus,
  epochs and failover.
- `/app/SPEC/redis.conf` — the `cluster-*` parameters.
- `/app/SPEC/commands.json` — the `CLUSTER` family and `MIGRATE`. This snapshot of the
  table does not list `ASKING`, `READONLY`, `READWRITE`, `CLUSTER MYID` or
  `CLUSTER BUMPEPOCH`, which section 1 requires; their pages are under
  `/opt/docs/redis/commands/`.
- `/opt/docs/redis/commands/` — Redis's command reference, one Markdown page per command, as
  redis.io published it in August 2025: behaviour, replies and errors. The pages cover every
  release; each one's history says which release added or changed what.
- `/app/SPEC/BUILD_AND_RUN.md` — how the artifact is built, started and driven.
- `/app/tests` with `/app/runtest` and `/app/runtest-cluster` — Redis's own suites. The
  cluster units live under `tests/cluster/tests/` and are driven by `./runtest-cluster`,
  which starts twenty `cluster-enabled` instances of `src/redis-server` and forms a
  cluster out of them.
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
