# Milestone 8 -- PostgreSQL 16

## 1. Implement this milestone

The workspace already contains a PostgreSQL-compatible server. Read its source
under `/app` -- and `/app/NOTES.md`, if the previous session left one -- before
changing anything.
Move the server to **PostgreSQL 16**: `SHOW server_version` reports
`16.15`, `server_version_num` reports `160015`, `PG_VERSION` contains `16`.

Add: the SQL/JSON constructors and predicates -- `JSON_ARRAY`, `JSON_OBJECT`,
`JSON_ARRAYAGG`, `JSON_OBJECTAGG` with their `ABSENT ON NULL`/`NULL ON NULL` and
`RETURNING` clauses, and `IS JSON [VALUE|SCALAR|ARRAY|OBJECT] [WITH|WITHOUT
UNIQUE KEYS]`; `any_value()`; `array_sample` and `array_shuffle`; and the
bidirectional-replication guard `CREATE SUBSCRIPTION ... WITH (origin = none)`
with `pg_subscription.suborigin`, which earlier releases reject.

Flips: `force_parallel_mode` is renamed `debug_parallel_query`, and
`promote_trigger_file` and `vacuum_defer_cleanup_age` are removed, so `SHOW` and
`pg_settings` change accordingly; `numeric`'s exponentiation operator changes its
result scale for integral exponents, so `2::numeric ^ 10` now has thirteen
fractional digits rather than sixteen.

Also required in this release: the input-validation additions; the `walfile`
functions; the datetime-with-timezone additions; the SQL/JSON DDL surface; and
the parameters this release adds and removes.

Also flipping in this release: `pg_get_viewdef` no longer qualifies the output
columns of a single-table view with the relation alias; and `xid8` input is
validated, so a non-numeric literal raises `invalid input syntax for type xid8`
instead of yielding 0.

Also required: the functions this release adds, by name --
`erf`, `erfc`, `system_user`, `xmltext`, `to_bin`, `to_oct`, `array_sample`,
`random_normal`, `pg_log_standby_snapshot` -- each callable, or present in
`pg_proc`, exactly as the release notes describe it; the aggregates it adds;
the `pg_stat_io` view and its columns; the columns added to
`pg_auth_members`; and the predefined roles this release adds.

This milestone is cumulative: the server must behave like PostgreSQL 16, which
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
`/app/SPEC/release-16.md` describes them. What is *not* required is anything
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

* `/app/SPEC/release-16.md`

The complete HTML documentation of PostgreSQL 16 is at
`/opt/docs/postgresql/16/` (start at `index.html`), including the SQL language,
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
