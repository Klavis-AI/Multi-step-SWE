# Milestone 1 -- PostgreSQL 9.6

## 1. Implement this milestone

Build the server. It must speak the PostgreSQL frontend/backend protocol
version 3.0 on a TCP port and on a Unix-domain socket, and behave as
**PostgreSQL 9.6** does: `SHOW server_version` reports `9.6.24`,
`server_version_num` reports `90624`, and `<datadir>/PG_VERSION` contains `9.6`.

The protocol surface: the startup packet and its parameters, `SSLRequest`
(refused), `CancelRequest`, `NegotiateProtocolVersion` for a newer 3.x minor,
a refusal for a startup packet whose protocol major version is not 3, the
`trust` and `md5` authentication methods selected by `pg_hba.conf`, the simple
query protocol, the extended query protocol
(`Parse`/`Bind`/`Describe`/`Execute`/`Close`/`Sync`/`Flush`, named and unnamed
statements and portals, `Execute` with a row limit and `PortalSuspended`), text
and binary parameter and result formats, `COPY` in and out in text and CSV and
`COPY` out in binary framing, `NoticeResponse`, and `ErrorResponse` carrying
severity, SQLSTATE, message, detail, hint, position and context fields.

The SQL surface: an executor and type system good enough that upstream `psql`
output matches PostgreSQL 9.6 byte for byte -- integers, `numeric`, `float4`/
`float8`, the character types, `bytea`, `boolean`, the date/time types and
`interval`, `uuid`, `json`, `jsonb`, arrays, and the range types `int4range`,
`int8range`, `numrange`, `daterange`, `tsrange` and `tstzrange` with their
constructors, bound accessors and containment, overlap and adjacency operators,
with PostgreSQL's input syntax, output formatting, casts, comparison and sort
order; `SELECT` with
joins, subqueries, `LATERAL`, set operations, `GROUP BY`/`HAVING`, aggregates,
window functions, `DISTINCT`/`DISTINCT ON`, `ORDER BY` with `NULLS FIRST/LAST`,
`LIMIT`/`OFFSET`, common table expressions including `WITH RECURSIVE`, and
set-returning functions in the select list; `INSERT` (including `ON CONFLICT`),
`UPDATE`, `DELETE` and `RETURNING`; `CREATE`/`ALTER`/`DROP` for tables, indexes,
views, sequences, schemas, roles and SQL functions; `PREPARE`/`EXECUTE`;
`DECLARE`/`FETCH`/`MOVE`/`CLOSE` cursors; `SET`/`SHOW`/`RESET` over the
configuration parameters PostgreSQL 9.6 has, rendering a value with its unit the
way `SHOW` does; `version()`, `current_setting()` and `set_config()`; `COPY`;
and transactions with `BEGIN`/`COMMIT`/`ROLLBACK`, `SAVEPOINT`, the
`READ COMMITTED` and
`REPEATABLE READ` isolation levels, transactional DDL, and row-level locking, so
that one session's `UPDATE` blocks another's `UPDATE` of the same row and blocks
another's `SELECT ... FOR UPDATE` of it, while a plain `SELECT` of that row does
not block.

The catalog surface: the `pg_catalog` and `information_schema` relations that
drivers read -- `pg_class`, `pg_attribute`, `pg_namespace`, `pg_type`,
`pg_constraint`, `pg_index`, `pg_indexes`, `pg_tables`, `pg_views`, `pg_proc`,
`pg_roles`, `pg_authid`, `pg_database`, `pg_settings`, `pg_sequences`,
`pg_stat_activity`, and `information_schema.tables`, `.columns`,
`.table_constraints`, `.key_column_usage`, `.referential_constraints`,
`.check_constraints`, `.views`, `.sequences`, `.schemata`, `.routines`.

The server must get all of the following right:

* **SQL behaviour** through a normal client connection -- results, types, and
  the exact text of errors, notices, details and hints, with their SQLSTATEs
  and their error positions.
* **The wire protocol itself** -- the startup and authentication exchange
  with the methods this release's `pg_hba.conf` names, the simple and
  extended query flows, `COPY` in (text and CSV) and out (text, CSV and
  binary), cancellation, and what the server does with a protocol version it
  does not know.
* **How concurrent sessions see each other**, with several connections open
  at once and their statements interleaved in a fixed order: read committed
  and repeatable read, `SELECT ... FOR UPDATE`, who blocks and who proceeds,
  serialization failures and their SQLSTATEs, and what a statement sees of a
  transaction that has not committed. Running each session correctly in
  isolation is not enough; the interleaving must be right too.
* **Restoring a dump.** A textual dump produced from a real PostgreSQL 9.6
  database must replay into your server and then answer queries: each schema
  has to load, and the queries asked afterwards have to answer correctly --
  some of them through a 9.6-era client that speaks protocol 3.0 rather than
  through a current one.
* **The data directory** your `initdb` writes -- the files and the version
  stamp a reader of the layout chapter would expect to find.

## 2. Read these local references

**Normative semantics** -- the PostgreSQL documentation for this release,
shipped into the workspace:

* `/app/SPEC/protocol-overview.md`
* `/app/SPEC/protocol-flow.md`
* `/app/SPEC/protocol-message-types.md`
* `/app/SPEC/protocol-message-formats.md`
* `/app/SPEC/protocol-error-fields.md`
* `/app/SPEC/errcodes-appendix.md`
* `/app/SPEC/app-postgres.md`
* `/app/SPEC/app-initdb.md`
* `/app/SPEC/auth-pg-hba-conf.md`
* `/app/SPEC/storage-file-layout.md`
* `/app/SPEC/release-9.6.md`

The complete HTML documentation of PostgreSQL 9.6 is at
`/opt/docs/postgresql/9.6/` (start at `index.html`), including the SQL language,
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
