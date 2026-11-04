# Step 11 — The limbo to turso rename, the header written at database creation, and generate_series

## Implement this milestone

You are building a relational database engine that stores data in a single
file in the SQLite file format, with a command-line shell as its only
interface. Stock `sqlite3` must be able to open and read what the engine
writes.

This step adds:

- **R11.1** — The shell is installed under the name `tursodb` and runs from that path.
- **R11.2** — No binary named `limbo` is installed at this checkpoint, the name having been retired by the rename.
- **R11.3** — The shell's stdout for a multi-statement script contains only result rows with no blank separator lines between result blocks, so the output is comparable byte-for-byte to another reader's rendering of the same rows.
- **R11.4** — A read-only session writes no pages to the main database file (the case compares the main file's bytes only; `spec/PROFILE.md` also asks that `main.db-wal` be left byte-identical, and no case at this step reads the log). Opening a database path that does not exist still creates the file, so a database that has never been written to is present and zero length after the session, and stock `sqlite3` recognises it as an empty database.
- **R11.5** — The `generate_series(<start>,<stop>[,<step>])` table-valued function enumerates integers from start to stop inclusive and returns the same rows as stock `sqlite3` for the same bounds, a negative stride — which counts down — included. Its rows are projected as a single column named `value`: that is the eponymous virtual table's schema, `CREATE TABLE generate_series(value, start HIDDEN, stop HIDDEN, step HIDDEN)`, which `spec/sqlite/vtab.html` gives.
- **R11.6** — The installed shell identifies itself by the name it is installed under: the string `tursodb` appears somewhere in what `--help` writes to standard output, and the retired name `limbo` appears nowhere in it **in any capitalisation** — `Limbo` and `LIMBO` are rejected with it, so the product name has to go and not only the lower-case executable name. Only the presence of the two names is compared — never the wording, order or layout of the help text.
- **R11.7** — The page-1 header is written when the first write needs it. After a write that has been folded into the main database file by `PRAGMA wal_checkpoint`, the file carries the header magic `SQLite format 3` and the page size and version bytes `spec/PROFILE.md` rule 1 fixes, and its in-header database size — the four-byte big-endian field at page-1 offset 28, which `spec/sqlite/fileformat2.html` names and locates — counts the pages the schema and the data actually occupy: page 1 holds the header and `sqlite_schema`, each table gets one root page, and a table whose rows all fit in that root page needs no further page — so one small table makes a two-page database.
- **R11.8** — A statement that returns no rows contributes no bytes to standard output. `CREATE TABLE`, `INSERT`, `UPDATE`, `DELETE`, `DROP TABLE` and a `SELECT` that matches nothing each print nothing at all: no header, no placeholder, no count line and no separator line, so the next statement's first row lands on the next line.
- **R11.9** — A NULL and a zero-length text value both render as an empty field, so a single-column row holding either is an empty line and only `typeof()` tells the two apart.
- **R11.10** — A non-empty blob renders as its raw bytes and columns are separated by `|`, so a blob holding printable bytes is indistinguishable in the rendered row from the text of those bytes, and `typeof()`, `length()` and `hex()` are what distinguish them.

Everything the earlier steps established must keep working. One case from each earlier step is re-run against your current shell as this step's gate: a single gate failure means this step is not satisfied.

## Read these local references

Normative specifications, under `/app/spec/` — verbatim public documents, no
implementation in them:

  - `/app/spec/sqlite/pragma.html`
  - `/app/spec/sqlite/fileformat2.html`
  - `/app/spec/sqlite/vtab.html`
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
  is absent, no requirement group can pass -- the gate groups still run against the
  shell you inherited, and the new capability is not satisfied.
- **Network.** This environment has no network access, and everything the build needs is already installed. The upstream project's source, its releases, mirrors and package registries are unreachable, so a copy cannot be fetched by any route. Searching for, downloading, copying, wrapping or
  invoking an online implementation of this task, or a task-specific answer,
  is prohibited.
- **Off-limits paths.** Do not inspect `/tests`, later steps, or any
  solution material, and do not write to them.
- **Check your own work.** Stock `sqlite3` is on this machine and is a second
  implementation of the same format. Use it to check any file you write and
  any answer you are unsure of. It is a development tool: nothing in `/app`
  may link to it, vendor it, or shell out to it at run time.
