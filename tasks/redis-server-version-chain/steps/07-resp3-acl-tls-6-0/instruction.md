# Step 7 — Redis 6.0: RESP3, ACLs, TLS, client-side caching and I/O threads

## 1. Implement this milestone

Move the server in this workspace to **Redis 6.0**. `/app/tests` has been replaced with
Redis 6.0's own test suite and `/app/SPEC/commands.json` with its command surface; both
are the definition of what must work, including everything that already worked.

New in this release:

- **RESP3**, as `/app/SPEC/redis-doc/resp3-spec.md` specifies it. `HELLO 2|3` negotiates
  the protocol; RESP3 connections receive maps (`%`), sets (`~`), doubles (`,`), booleans
  (`#`), big numbers (`(`), verbatim strings (`=`), nulls (`_`) and out-of-band pushes
  (`>`) where the command's RESP3 reply schema says so. **A RESP2 client must keep seeing
  exactly the replies it saw before** — the protocol is per connection and defaults to
  RESP2. Pub/sub messages become pushes on RESP3, and a RESP3 subscriber may run any
  command. Redis's own `unit/protocol` RESP3 tests (which negotiate with `HELLO 3`) and
  the `unit/tracking` tests exercise RESP3 in this release's suite; `HELLO` itself is
  required for negotiation.
- **ACLs**, as `/app/SPEC/redis-doc/acl.md` specifies them: users with `on`/`off`,
  passwords (`>`, `<`, `#`, `!`, `nopass`, `resetpass`), command rules (`+@all`, `-@all`,
  `+<command>`, `-<command>`, `+<command>|<subcommand>`, `+@<category>`), key patterns
  (`~<pattern>`, `allkeys`, `resetkeys`); the `ACL` command family (`WHOAMI`, `LIST`, `USERS`,
  `CAT`, `GETUSER`, `SETUSER`, `DELUSER`, `GENPASS`, `LOAD`, `SAVE`, `HELP`); the
  `user` directive in the configuration file and the `aclfile` parameter; `AUTH` with a
  username; and the `NOPERM`/`WRONGPASS`/`NOAUTH` errors.
- **TLS**: the `tls-port` parameter and the whole `tls-*` family of
  `/app/SPEC/redis.conf` — certificate, key, CA and client-certificate files,
  `tls-auth-clients`, `tls-protocols`, `tls-ciphers`, `tls-prefer-server-ciphers`,
  `tls-session-caching`, `tls-replication`, `tls-cluster`. A TLS connection is an
  ordinary RESP connection once the handshake completes.
- **Client-side caching**, as `/app/SPEC/redis-doc/client-side-caching.md` specifies it:
  `CLIENT TRACKING ON|OFF` with `REDIRECT`, `PREFIX`, `BCAST`, `OPTIN`, `OPTOUT`,
  `NOLOOP`; `CLIENT CACHING YES|NO`; `CLIENT GETREDIR`; and the `invalidate` push (or
  the `__redis__:invalidate` pub/sub message when redirecting) when a tracked key
  changes, expires, is evicted, or the database is flushed.
- **`io-threads`** and the other `io-threads-*` parameters of `/app/SPEC/redis.conf`,
  with `io_threads_active` in `INFO`.
- **`STRALGO LCS`** over `KEYS` or `STRINGS`, with `LEN`, `IDX`, `MINMATCHLEN` and
  `WITHMATCHLEN`.

## Startup log

The test harness decides your server is up by grepping its log, and it greps
for two different strings:

- `Ready to accept` — initial capital, matched **case-sensitively** by the
  harness of this release — is what the suite of this release counts. Redis's
  own sentence is `Ready to accept connections`; text after it on the same line
  is ignored. (The 4.0- and 5.0-era harnesses, under which this server must also run,
  match this with `grep -i`, so the sentence satisfies those too.)
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
- `/app/SPEC/redis-doc/resp3-spec.md` — normative for RESP3; `protocol.md` for RESP2;
  `acl.md` and `client-side-caching.md` for the other two subsystems.
- `/app/SPEC/redis.conf` — the parameters of this release, including the `tls-*` family.
- `/app/SPEC/BUILD_AND_RUN.md` — how the artifact is built, started and driven.
- `/app/tests` with `/app/runtest` — Redis's own suite for this version. `./runtest --tls`
  runs it over TLS; `utils/gen-test-certs.sh` in Redis's own tree is how its certificates
  are generated, and the server must work with freshly generated certificates of that kind.
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
