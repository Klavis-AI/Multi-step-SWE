# Step 11 — Redis 8.0: the bundled JSON, time-series, probabilistic and vector-set types

## 1. Implement this milestone

Move the server in this workspace to **Redis 8.0**. `/app/tests` has been replaced with
Redis 8.0's own test suite, `/app/SPEC/commands.json` with its core command surface and
`/app/SPEC/commands/` with Redis's own machine-readable command metadata.

Redis 8.0 ships four additional data types in the server itself. Their complete command
surface — name, arity, every argument, every reply — is in
**`/app/SPEC/commands-modules.json`**, in Redis's own command-metadata format.
Implement them so that they behave exactly as that surface describes and as the servers
that introduced them behave:

- **JSON** (`JSON.SET`, `JSON.GET`, `JSON.MGET`, `JSON.DEL`, `JSON.FORGET`, `JSON.TYPE`,
  `JSON.NUMINCRBY`, `JSON.NUMMULTBY`, `JSON.STRAPPEND`, `JSON.STRLEN`, `JSON.ARRAPPEND`,
  `JSON.ARRINDEX`, `JSON.ARRINSERT`, `JSON.ARRLEN`, `JSON.ARRPOP`, `JSON.ARRTRIM`,
  `JSON.OBJKEYS`, `JSON.OBJLEN`, `JSON.TOGGLE`, `JSON.CLEAR`, `JSON.MERGE`,
  `JSON.MSET`, `JSON.RESP`, `JSON.DEBUG MEMORY`) with **JSONPath** (`$`-rooted) and the
  legacy dot-path syntax, and the two different reply shapes they select.
- **Probabilistic types**: Bloom filters (`BF.*`), cuckoo filters (`CF.*`),
  count-min sketches (`CMS.*`) and top-k (`TOPK.*`), each with its reserve/insert/query
  surface, its `*.INFO` reply and its error strings.
- **Time series** (`TS.CREATE`, `TS.ADD`, `TS.MADD`, `TS.INCRBY`, `TS.DECRBY`,
  `TS.ALTER`, `TS.DEL`, `TS.GET`, `TS.MGET`, `TS.RANGE`, `TS.REVRANGE`, `TS.MRANGE`,
  `TS.MREVRANGE`, `TS.CREATERULE`, `TS.DELETERULE`, `TS.INFO`, `TS.QUERYINDEX`) with
  retention, labels, duplicate policies, aggregation buckets and compaction rules.
- **Vector sets** (`VADD`, `VREM`, `VSIM`, `VDIM`, `VCARD`, `VEMB`, `VLINKS`, `VINFO`,
  `VSETATTR`, `VGETATTR`, `VISMEMBER`, `VRANDMEMBER`) with `FP32`/`VALUES` input,
  `REDUCE`, `CAS`, `EF`, `COUNT`, `WITHSCORES`, `WITHATTRIBS`, `FILTER` expressions and
  quantisation options.

- **RESP3 across the whole surface.** Every command in this release -- the core surface
  and the four bundled types alike -- must answer correctly to a client that negotiated
  RESP3 with `HELLO 3`, in the RESP3 reply shape `SPEC/commands/` and
  `SPEC/commands-modules.json` describe for it: maps where Redis sends a map, sets where
  it sends a set, doubles, big numbers, booleans, verbatim strings and out-of-band push
  messages. Redis's own suite must also pass a second time with every client on RESP3.

Each new type is a first-class value: `TYPE` names it, `DEL`/`EXISTS`/`EXPIRE`/`TTL`/
`KEYS`/`SCAN`/`RENAME` work on it, `OBJECT ENCODING` reports it, and — this is required —
it survives `SAVE` and restart and round-trips through `DUMP`/`RESTORE`. The type names
are the ones Redis 8.0 reports, and `SCAN ... TYPE` filters on them: `ReJSON-RL` (JSON),
`MBbloom--` (Bloom filter), `MBbloomCF` (cuckoo filter), `CMSk-TYPE` (count-min sketch),
`TopK-TYPE` (top-k), `TDIS-TYPE` (t-digest, the `TDIGEST.*` commands in
`SPEC/commands-modules.json`), `TSDB-TYPE` (time series) and `vectorset` (vector set).
`OBJECT ENCODING` reports `raw` for every one of them.

Redis 8.0 also extends the core surface: **`HGETEX`**, **`HGETDEL`** and **`HSETEX`**
join the hash commands, with the same `FIELDS <numfields> <field> ...` form as the other
hash-field commands and with `HGETEX`'s `EX`/`PX`/`EXAT`/`PXAT`/`PERSIST` options.
`SPEC/commands/hgetex.json`, `hgetdel.json` and `hsetex.json` carry their full argument
grammar and reply schemas; `SPEC/commands.json` is the older redis-doc command table and
does not list them.

Everything the server already does must keep working unchanged.

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

- `/app/SPEC/commands-modules.json` — the complete normative surface for the four new
  types, in Redis's command-metadata format, with `since`, `arity` and full argument
  trees. `/app/SPEC/commands-modules-PROVENANCE.txt` says where each part came from.
- `/app/SPEC/commands.json` and `/app/SPEC/commands/` — the core command surface.
- `/opt/docs/redis/commands/` — Redis's command reference, one Markdown page per command, as
  redis.io published it in August 2025, including a page for each command of the four new
  types: behaviour, replies and errors. The types themselves are described under
  `/opt/docs/redis/develop/data-types/` (`json/`, with `path.md` for JSONPath and the legacy
  path syntax and `resp3.md` for the RESP3 replies; `probabilistic/`; `timeseries/`;
  `vector-sets/`). `/opt/docs/redis/develop/reference/` explains the command-metadata format
  (`command-arguments.md`, `key-specs.md`, `command-tips.md`).
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
