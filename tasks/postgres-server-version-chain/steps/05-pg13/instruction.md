# Milestone 5 -- PostgreSQL 13

## 1. Implement this milestone

The workspace already contains a PostgreSQL-compatible server. Read its source
under `/app` -- and `/app/NOTES.md`, if the previous session left one -- before
changing anything.
Move the server to **PostgreSQL 13**: `SHOW server_version` reports
`13.23`, `server_version_num` reports `130023`, `PG_VERSION` contains `13`.

Add: `FETCH FIRST n ROWS WITH TIES` and its interaction with `ORDER BY` and
`OFFSET`; logical replication of partitioned tables --
`CREATE PUBLICATION ... WITH (publish_via_partition_root = true)` and
`pg_publication.pubviaroot`; `gen_random_uuid()` in core; the
`deduplicate_items` index storage parameter, settable with `CREATE INDEX ... WITH`
and `ALTER INDEX ... SET`, visible in `pg_class.reloptions` and rejected for index
methods that do not support it; and `ALTER TABLE ... ALTER COLUMN ... DROP
EXPRESSION`.

Flips: `wal_keep_segments` is replaced by `wal_keep_size`, so `SHOW` on the old
name fails and on the new one succeeds; the not-null violation message names the
relation (`null value in column "c" of relation "t" violates not-null
constraint`).

Also required in this release: `xid8` and its conversions; the Unicode
normalization functions; `jsonb` and `jsonpath` additions; `VACUUM`'s new
options; the statistics views this release adds; and the parameters it adds and
removes.

Also flipping in this release: `CREATE PUBLICATION` and `ALTER PUBLICATION` emit
`WARNING: wal_level is insufficient to publish logical changes` with the hint
`Set wal_level to logical before creating subscriptions.` whenever `wal_level`
is below `logical`; the not-null and partition-constraint failures name the
relation (`null value in column "j" of relation "cb" violates not-null
constraint`, `column "j" of relation "ca" contains null values`, `partition
constraint of relation "p1" is violated by some row`); a `BEFORE ... FOR EACH ROW`
trigger on a partitioned parent is now accepted and fires; and dropping a
generation expression gains the hint `Use ALTER TABLE ... ALTER COLUMN ... DROP
EXPRESSION instead.`

Also required: `EXPLAIN`'s option handling, including the
error for `WAL` without `ANALYZE` and the error for an unrecognized option;
`gcd`, `lcm`, `div`, `trunc`, `scale` and `min_scale` on `numeric` and the
integer types, with their error cases; `DROP DATABASE ... WITH (FORCE)`;
extended statistics as this release leaves them -- `ALTER STATISTICS ... SET
STATISTICS`, what `pg_statistic_ext.stxkind` holds, and what happens to a
statistics object when a column it names is dropped; `make_date`,
`make_time`, `make_timestamp` and `make_timestamptz` and the errors a bad field
raises; and the `pg_snapshot` functions (`pg_current_snapshot`,
`pg_snapshot_xmin`, `pg_snapshot_xmax`, `pg_snapshot_xip`).

This milestone is cumulative: the server must behave like PostgreSQL 13, which
means everything the server in `/app` already does still has to work, except
where this release deliberately changes it. Nothing is retired, and no earlier
feature may be dropped to make room for a new one.

Every requirement from earlier milestones must still hold, and every
behaviour whose correct answer changes in this release must give the new
answer: fixing this release by breaking an earlier one is wrong, and so is
leaving an earlier answer in place where this release changed it.

Beyond the items above this release also changes smaller observable details,
and those are required too: the wording of errors, notices, details and hints;
catalog columns and views; default settings and the parameters the release
adds, renames and removes; and functions and views it adds or renames.
`/app/SPEC/release-13.md` describes them. What is *not* required is anything
with no observable answer through this interface: planner and execution-
strategy changes, vacuum and storage internals, the replication stream,
standby and backup machinery, and the client programs. `/app/tests/` holds the
smoke corpus for this milestone and every earlier one.

The server must get all of the following right:

* **SQL behaviour** through a normal client connection -- results, types, and
  the exact text of errors, notices, details and hints, with their SQLSTATEs
  and their error positions.
* **The wire protocol itself** -- the startup and authentication exchange
  including SCRAM-SHA-256, the simple and extended query flows, `COPY` in
  (text and CSV) and out (text, CSV and binary), cancellation, and what the
  server does with a protocol version it does not know.
* **How concurrent sessions see each other**, with several connections open
  at once and their statements interleaved in a fixed order: read committed
  and repeatable read, `SELECT ... FOR UPDATE`, who blocks and who proceeds,
  serialization failures and their SQLSTATEs, and what a statement sees of a
  transaction that has not committed. Running each session correctly in
  isolation is not enough; the interleaving must be right too.
* **Restoring a dump.** Textual dumps produced from real PostgreSQL databases
  -- this release and every earlier one -- must replay into your server and
  then answer queries: each schema has to load, and the queries asked
  afterwards have to answer correctly -- some of them through a 9.6-era
  client that speaks protocol 3.0 rather than through a current one.
* **The data directory** your `initdb` writes -- the files and the version
  stamp a reader of the layout chapter would expect to find.

## 2. Read these local references

The protocol chapters, the `postgres` and `initdb` pages, the
`pg_hba.conf` page, the data-directory layout and the release notes of every
earlier release are already in `/app/SPEC/` and stay in force: everything they
define is still part of this server's behaviour and must still work,
`version()` and `SHOW server_version` among it. This milestone adds these
documents, which are normative for what changes here:

* `/app/SPEC/release-13.md`

The complete HTML documentation of PostgreSQL 13 is at
`/opt/docs/postgresql/13/` (start at `index.html`), including the SQL language,
functions and operators, data types, SQL commands and server configuration
chapters.

**Additional files:** `/app/SPEC/INTERFACE.md` states the interface the server
must provide -- the two programs, their flags, the wire protocol, the
configuration files and the exact commands that build, initialise, start and
drive the server.
`/app/WORKSPACE_HANDOFF.md` says what to leave behind. `/app/tests/README.md`
explains the smoke corpus.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the supplied source,
  manifests, documents, and handoff files. Do not create a prose answer or
  alternate entry point unless Section 1 explicitly requires one.
- Leave `/app` source-only and text-only when you finish: `make clean`, remove
  `bin/`, and remove any data directory, log or core file you created under
  `/app`. The workspace is archived as a text stream between milestones, so a
  binary file left behind costs the next session the files that follow it.
  `/app/WORKSPACE_HANDOFF.md` says this again with the exact commands.
- The server must be your own implementation. Do not vendor, copy, adapt or
  link PostgreSQL's own source code or binaries, and do not make your server
  delegate to another database. `make` in `/app` must compile and link
  `bin/postgres` and `bin/initdb` itself, and the server, once started, must
  still be that program while it runs: a script that fetches or execs another
  server is not an implementation of this milestone.
- Both programs must be C programs (C11) that use the C library alone. They
  must link nothing but glibc's own libraries (`libc`, `libm`, `libpthread`,
  `libdl`, `librt`, `libutil`, `libresolv`, `libanl`, `libmvec`) and gcc's
  `libgcc_s`, and neither may embed, load or start an interpreter or another
  language's runtime: no `dlopen` of `libpython` or of any other runtime, no
  interpreter as a child process, and no copy of one left in the tree. The
  image's Python is there for your own test scripts, not for the server. A
  program that hosts a server written in another language is not an
  implementation of this milestone.
- The source tree itself must contain none of PostgreSQL's own source. Keep
  the tree visibly your own project: nothing laid out as in PostgreSQL's
  source distribution (no directory such as `src/backend`, `src/common` or
  `contrib`, and no copy of its build files such as `configure.ac`); none of
  PostgreSQL's catalog bootstrap files (a `.bki` script or `pg_*.dat` catalog
  data), wherever they sit, and no reference to them in your programs; and no
  file you add -- comments and notes included -- that carries PostgreSQL's
  copyright banner or names PostgreSQL's own source files, headers or
  extension macros. After `make`, every program or shared library the build
  leaves in the tree is under `bin/`.
- The same applies to the bytes of the two produced programs. An ELF compiled
  from PostgreSQL's own source, or linked against PostgreSQL's own objects,
  carries strings that only such a build leaves behind, and a binary carrying
  one is not an implementation of this milestone. A from-scratch binary
  carries none of them. Text your server merely *prints* -- a
  `pg_controldata` field label, or `initdb`'s own progress line -- is the
  observable behaviour this milestone asks for and is not what this rule is
  about.
- Do not use online solutions or hints specific to this task.
- Run the local checks named by the references and leave a reproducible
  handoff for the next fresh session.
