# Step 8 — Redis 6.2: `GETEX`, `COPY`, `RESET`, the ACL log and the `SCAN` guarantees

## 1. Implement this milestone

Move the server in this workspace to **Redis 6.2**. `/app/tests` has been replaced with
Redis 6.2's own test suite and `/app/SPEC/commands.json` with its command surface; both
are the definition of what must work, including everything that already worked.

New in this release:

- **`GETEX`** with `EX`/`PX`/`EXAT`/`PXAT`/`PERSIST` and its replication as
  `PEXPIREAT`/`PERSIST`; **`GETDEL`**; **`COPY`** with `DB` and `REPLACE`;
  **`RESET`**, which returns a connection to its post-handshake state.
- **`SMISMEMBER`**, **`ZRANGESTORE`**, **`ZDIFF`/`ZDIFFSTORE`**, **`ZUNION`/`ZINTER`**,
  **`ZRANDMEMBER`**, **`HRANDFIELD`**, **`LMOVE`/`BLMOVE`**, **`ZMSCORE`**
  and the `GEOSEARCH`/`GEOSEARCHSTORE` additions of the release.
- **`SCAN` with `TYPE`**, and the 6.2 cursor guarantees: an element present for the whole
  iteration is returned at least once, `COUNT` is a hint, `MATCH` filters after
  fetching, and `HSCAN`/`SSCAN`/`ZSCAN` behave the same way over their containers.
- **ACL channel patterns**: `&<pattern>` on `ACL SETUSER`, with `allchannels` and
  `resetchannels`, restricting which pub/sub channels a user may `SUBSCRIBE`,
  `PSUBSCRIBE` or `PUBLISH` to, and `ACL GETUSER` reporting them.
- **The ACL log**: `ACL LOG [count|RESET]` with Redis's own entry fields (`count`,
  `reason`, `context`, `object`, `username`, `age-seconds`, `client-info`), recorded for
  denied commands, keys and channels and for authentication failures.
- **`CLIENT TRACKINGINFO`**, **`CLIENT INFO`** and the `argv-mem` / `tot-mem` fields of
  the client line.
- **`FAILOVER`** for coordinated failover to a replica, and `CLIENT PAUSE` with `WRITE`
  and `ALL` modes.
- The `INFO` additions of the release, including the `errorstats` section.

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

- `/app/SPEC/commands.json` — every command this milestone exposes, with its argument grammar and the
  release that introduced it. Commands absent from this file do not belong in this milestone.
- `/opt/docs/redis/commands/` — Redis's command reference, one Markdown page per command, as
  redis.io published it in August 2025: behaviour, replies and errors. The pages cover every
  release; each one's history says which release added or changed what.
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
