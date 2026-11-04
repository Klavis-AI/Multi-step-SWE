# Step 13 — A CDC table populated transactionally by every mutation

## Implement this milestone

You are building a relational database engine that stores data in a single
file in the SQLite file format, with a command-line shell as its only
interface. Stock `sqlite3` must be able to open and read what the engine
writes.

This step adds:

- **R13.1** — With capture enabled, an `INSERT` appends exactly one capture row whose `change_type` is `1`, whose `table_name` is the mutated table and whose `id` is the mutated row's rowid; its `after` column holds an image and its `before` column is NULL.
- **R13.2** — With capture enabled, an `UPDATE` appends one capture row whose `change_type` is `0` and which holds both a `before` and an `after` image.
- **R13.3** — With capture enabled, a `DELETE` appends one capture row whose `change_type` is `-1`, which holds a `before` image and whose `after` column is NULL.
- **R13.4** — Within one transaction the capture rows are ordered by `change_id` in the order the mutations were issued, and the ids are consecutive from 1 with no gap (true of every case here, each of which starts from an empty capture table; `change_id` increases by one per capture row, so a rolled-back transaction consumes none and the ids stay gapless across it; where a re-armed table already holds rows, the continuation rule stated in `TEST_INTERFACE.md` governs instead); after the `COMMIT` every one of that transaction's capture rows is present and no others are, so their count is the number of rows the transaction mutated.
- **R13.5** — A capture row is written in the same transaction as its mutation, so a `ROLLBACK` leaves no capture row for it, leaves rows committed before the transaction untouched, and leaves `main.db` and `main.db-wal` byte-identical to their pre-transaction bytes.
- **R13.6** — Capture is off until `PRAGMA unstable_capture_data_changes_conn` selects a capturing mode: in the default state, and under mode `off` when no capturing mode has been selected yet, no capture table appears in `sqlite_schema` and no mutation produces a capture row, and selecting a mode does not retroactively capture mutations issued before it. An `off` issued *after* a capturing mode has been in use stops capture without disturbing the table or the rows already in it.
- **R13.7** — Stock `sqlite3` opening a capture-enabled database reads the capture table as an ordinary table — it appears in `sqlite_schema` and its rows select — and `PRAGMA integrity_check` returns `ok`.
- **R13.8** — A mutation performed after an `ALTER TABLE` on the captured table is still captured, and its image is encoded against the altered column set.
- **R13.9** — The capture table's time column is declared `INTEGER` and holds an integer, so `typeof()` over it reports `integer` and stock `sqlite3`'s `PRAGMA integrity_check` answers `ok` rather than reporting a type mismatch on that column.
- **R13.10** — A `before` or `after` image is the SQLite record encoding of that row over the captured table's current columns, as `spec/sqlite/fileformat2.html` §2.1 defines it, with an `INTEGER PRIMARY KEY` column carrying its own value rather than the serial type 0 an on-disk table row stores for a rowid alias.
- **R13.11** — The pragma value may be given as `<mode>,<name>`, in which case the capture table is created under that name and the default name is not used.
- **R13.12** — Mode `before` records only the pre-image, mode `after` only the post-image and mode `id` neither, each still appending exactly one capture row per mutation; a mode selected part way through a session applies from the next statement onwards and does not alter rows already captured.
- **R13.13** — The capture table is an ordinary table whose leading seven columns are, in order, `change_id`, `change_time`, `change_type`, `table_name`, `id`, `before`, `after`, as `pragma_table_info` reports them.
- **R13.14** — `ALTER TABLE ... ADD COLUMN` and `ALTER TABLE ... DROP COLUMN` on a captured table each append one capture row whose `table_name` is `sqlite_schema`, so the capture ids a user table's own mutations receive are not consecutive across an `ALTER`.

Everything the earlier steps established must keep working. One case from each earlier step is re-run against your current shell as this step's gate: a single gate failure means this step is not satisfied.

## Read these local references

Normative specifications, under `/app/spec/` — verbatim public documents, no
implementation in them:

  - `/app/spec/sqlite/lang_createtable.html`
  - `/app/spec/sqlite/lang_transaction.html`
  - `/app/spec/sqlite/schematab.html`
  - `/app/spec/sqlite/fileformat2.html`
  - `/app/spec/sqlite/lang_altertable.html`
  - `/app/spec/sqlite/pragma.html`
  - `/app/spec/turso/COMPAT.md` — the upstream compatibility matrix at exactly
    this release: statement by statement and function by function, what the
    project claimed to support at that tag. Where it disagrees with the requirements listed in this step, **the requirements win**: the matrix is a description of the release, the requirement list is the contract.
  - `/app/spec/turso/manual.md` — the project manual at this release. Its **Limitations** list in particular is a description of the release and not of what is in scope here: where a line in it denies something this step's requirements ask for, the requirements win, exactly as with the matrix.
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
- **Install the artefact.** `/app/bin/tursodb` must be a regular
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
