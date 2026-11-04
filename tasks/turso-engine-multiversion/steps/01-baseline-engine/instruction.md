# Step 01 — The baseline engine: parser, planner, VDBE, pager, table B-tree, first WAL

## Implement this milestone

You are building a relational database engine that stores data in a single
file in the SQLite file format, with a command-line shell as its only
interface. Stock `sqlite3` must be able to open and read what the engine
writes.

This step adds:

- **R01.1** — A `CREATE TABLE` statement is recorded in the `sql` column of its `sqlite_schema` row in the profile's normalised rendering, and stock `sqlite3` opening the file re-parses it to the same schema. Checked in two halves: the row is read back **through the engine's own shell** (there is no other way to see a rendering that is the engine's, not the format's), and the same file is then handed to stock `sqlite3`, whose `PRAGMA integrity_check` and `sqlite_schema` re-parse must agree.
- **R01.2** — A script mixing `INSERT`, `UPDATE`, `DELETE` and `SELECT` with `WHERE`, `ORDER BY`, `GROUP BY`, `LIMIT` and `LIKE` returns row-for-row identical output to stock `sqlite3` run on the same script. `LIKE` is checked in a `WHERE` clause with the `%` and `_` wildcards and with ASCII case folding, which overrides the `(NOT) LIKE` row of `spec/turso/COMPAT.md`; `GROUP BY` is checked with the aggregate functions `count()` and `sum()`. **At this step the in-scope statements are `INSERT` and `SELECT` only.** `UPDATE` is a parse error at this checkpoint and `DELETE` is accepted without deleting anything, so no case at this step writes either one and neither is in scope here.
- **R01.3** — `changes()` and `last_insert_rowid()` return the same values after each statement as stock `sqlite3` returns for the same script.
- **R01.4** — Every SQLite serial type — NULL, the 1/2/3/4/6/8-byte integers, the 0 and 1 constants, IEEE float, text and blob — round-trips through the record encoding and is returned unchanged by a later `SELECT`.
- **R01.5** — A row payload one byte above the spill threshold `((usable-12)*64/255)-23` (index pages) or `usable-35` (table leaves) allocates an overflow page, while a payload one byte below stays entirely on the leaf page. The two payloads are written through this engine's own shell as `zeroblob(n)`, so `zeroblob()` is an engine obligation at this step.
- **R01.6** — A value stored across an overflow chain is read back byte-for-byte identically by stock `sqlite3`. `PRAGMA integrity_check` returning `ok` is required at this step on the companion file whose payload stays on the leaf, not on the file holding the chain: two later checkpoints in the reference chain leave one page of every overflow chain unreferenced, so asserting `ok` on an overflow file here would freeze an expectation the chain cannot keep. On a file holding a chain the same `ok` is an obligation on a conforming engine that this step does not observe.
- **R01.7** — A primary-key scan and a single-column index scan return exactly the same result set as the equivalent full table scan over the same data. **At this step only the primary-key half is in scope** — a seek on an `INTEGER PRIMARY KEY` rowid against a full scan of the same table. `CREATE INDEX` arrives later in the chain and nothing at this step creates a secondary index. The two access paths are compared through the aggregate functions `count()`, `sum()`, `min()` and `max()` and the scalar `ifnull()`, so those are required here too.
- **R01.10** — A newly created database carries the page-1 header that the profile's rule 1 fixes: the write and read version bytes at offsets 18 and 19 are both `2` rather than `1`, so the file is in WAL mode; the page size at offset 16 is 4096; the reserved-space byte at offset 20 is `0`, so the usable page size is the whole 4096; and the four-byte text encoding at offset 56 is `1`, UTF-8. Eight further page-1 fields are read alongside those five and must read as the format itself requires: the magic string at offset 0, the payload fractions at 21, the two freelist fields at 32 and 36 of a database with an empty freelist, the twenty reserved zeros at 72, and the three four-byte fields the format fixes at zero for a database created the way this step creates one — the largest-root-page pointer at offset 52 and the incremental-vacuum flag at offset 64, which the format defines as zero outside auto-vacuum and incremental-vacuum modes and nothing at this step engages either mode, and the application id at offset 68, which is zero until `PRAGMA application_id` sets it and nothing at this step sets it. Thirteen fields in all are read. A page-1 field outside those thirteen — one the format leaves to the writer and no task document chooses, such as the default page-cache size at offset 48 or the user version at offset 60 — is out of scope.
- **R01.11** — A committed write is durable across the process boundary, in the three ways the profile's durability rule names. A committed write appends frames to the `-wal` file, the `-wal` is still on disk after the writing process exits, and the written rows are visible to a subsequent query before any checkpoint has run. A connection that only reads changes nothing: `main.db` and `main.db-wal` are byte-identical after a read-only invocation. And a simulated crash is a file copy — `main.db` and `main.db-wal` copied as they lie, without `main.db-shm` and without a clean close — from which every committed row is read back.
- **R01.12** — With the output mode selected explicitly on the command line, the shell renders each result row as one line of `|`-separated columns with no header and no padding, and renders NULL and blob values in exactly the profile's renderings — an empty field for NULL, raw bytes for a blob, per the profile's rules 4 and 5, which for these two classes are stock `sqlite3`'s `-list` renderings as well. The shell's own default mode is out of scope. The rendering is read off a result row built with `typeof()`, `length()` and `coalesce()`, so those are engine obligations here as well.
- **R01.13** — A statement that cannot be executed is refused rather than silently ignored: the shell reports it on standard output, produces no rows for it, and carries on with the next statement. The wording of the report is not part of the requirement.
- **R01.14** — A statement that would violate the `PRIMARY KEY` of a rowid table — a second row with a rowid already in use — is refused under R01.13's contract rather than applied: the conflicting row is not stored, the statements on either side of it still run, and the file left behind holds exactly the rows that were accepted, as stock `sqlite3` reading it confirms. `UNIQUE` constraints and `ON CONFLICT` clauses are not in scope at this step.
- **R01.15** — Scalar expressions evaluated with no table involved return exactly what stock `sqlite3` returns for the same script: integer and real arithmetic, integer division and the modulo operator with both signs of operand, the comparison operators over integers, reals and text, a comparison against `NULL`, `typeof()` over all five storage classes, and `length()` and `abs()`.
- **R01.16** — A command-line argument the shell cannot use is a process-level failure rather than a diagnostic it recovers from: that invocation exits `2`, writes nothing at all to standard output, and leaves no database file behind, while the identical statement stream given a usable argument still runs and still answers.

The numbering above skips `R01.8`, `R01.9`. That is not an omission from this page. A requirement number is a permanent identity in this task's ledger: it is assigned once and never reused or renumbered, so when a requirement is withdrawn, folded into another one, or found to be already true of the engine you inherited, its number simply stops appearing. A number that is not listed here is out of scope at this step and nothing is hidden behind it -- the bullets above are the whole of what this step adds, and `TEST_INTERFACE.md` covers exactly the same set.

This is the first step, so there is nothing yet to preserve.

## Read these local references

Normative specifications, under `/app/spec/` — verbatim public documents, no
implementation in them:

  - `/app/spec/sqlite/fileformat2.html`
  - `/app/spec/sqlite/lang_createtable.html`
  - `/app/spec/sqlite/lang_insert.html`
  - `/app/spec/sqlite/lang_select.html`
  - `/app/spec/sqlite/lang_expr.html`
  - `/app/spec/sqlite/lang_corefunc.html`
  - `/app/spec/sqlite/lang_aggfunc.html`
  - `/app/spec/sqlite/datatype3.html`
  - `/app/spec/sqlite/rowidtable.html`
  - `/app/spec/sqlite/schematab.html`
  - `/app/spec/turso/COMPAT.md` — the upstream compatibility matrix at exactly
    this release: statement by statement and function by function, what the
    project claimed to support at that tag. Where it disagrees with the requirements listed in this step, **the requirements win**: the matrix is a description of the release, the requirement list is the contract. `TEST_INTERFACE.md` names each row it overrides and the requirement that overrides it.
  - `/app/spec/turso/CHANGELOG.md`, `/app/spec/turso/README.md` — what this release
    added, in the project's own words
  - `/app/spec/README.md` — how to navigate the rest of the corpus

Task documents:

  - `/app/TEST_INTERFACE.md` — the contract: the artefact path, the exact
    invocation, the output mode, how output and exit status are compared, what
    is in scope, and what is explicitly out of scope. **Read this before writing
    code.**
  - `/app/WORKSPACE_HANDOFF.md` — what you start with, what must be true when
    you stop, and how to check your work locally
  - `/app/spec/PROFILE.md` — where this engine deliberately differs from stock
    SQLite, and what the tests assert instead

## Meta requirements

- **Budget.** Run `bash /app/timer.sh` to see the time remaining in this
  step. The configured timeout is authoritative; the timer is an estimate.
- **The workspace is cumulative.** The next step continues in this same
  `/app`. Leave your sources in place, keep `/app/bin/` populated, and do not
  clean the tree. `/app/target/` and `/app/.git/` are not part of the
  deliverable; everything else under `/app` is.
- **Install the artefact.** `/app/bin/limbo` must be a regular
  executable file when you stop. Nothing is built for you; if it
  is absent, no requirement group can be satisfied.
- **Network.** This environment has no network access, and everything the build needs is already installed. The upstream project's source, its releases, mirrors and package registries are unreachable, so a copy cannot be fetched by any route. Searching for, downloading, copying, wrapping or
  invoking an online implementation of this task, or a task-specific answer,
  is prohibited.
- **Off-limits paths.** Do not inspect `/tests`, later steps, or any
  solution material, and do not write to them.
- **Check your own work.** Stock `sqlite3` is on this machine and is a second
  implementation of the same format. Use it to check any file you write and
  any answer you are unsure of. It is a development tool: nothing in `/app`
  may link to it, vendor it, or shell out to it at run time.
