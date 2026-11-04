# Step 09 — VALUES, UNION, the DISTINCT forms, INSERT ... SELECT, COLLATE NOCASE

## Implement this milestone

You are building a relational database engine that stores data in a single
file in the SQLite file format, with a command-line shell as its only
interface. Stock `sqlite3` must be able to open and read what the engine
writes.

This step adds:

- **R09.1** — A bare `VALUES (…),(…)` statement returns its rows in the written order, as stock `sqlite3` does.
- **R09.2** — `VALUES` used as the source of a `SELECT` — `SELECT … FROM (VALUES …)` — returns the same rows, in the order the list writes them, as stock `sqlite3` does for the same statement, and a column name the list does not define is reported rather than resolved.
- **R09.3** — `UNION` removes duplicate rows while `UNION ALL` preserves every duplicate, matching stock `sqlite3` on the same inputs.
- **R09.4** — The `DISTINCT` forms match stock `sqlite3`, including their treatment of NULLs: a `DISTINCT` aggregate such as `count(DISTINCT x)` or `sum(DISTINCT x)` folds duplicate arguments and skips NULL ones, and a row-level `SELECT DISTINCT` — over one column and over several — eliminates duplicate result rows, counting two NULLs in the same column as one another's duplicate.
- **R09.5** — `INSERT INTO … SELECT` inserts exactly the selected rows and reports the matching `changes()` count.
- **R09.6** — `COLLATE NOCASE` applies both as an expression-level collation — in an `ORDER BY` term and in an equality test, where the default `BINARY` collation stays byte-ordered — and when carried by an index on a column declared with it, where the indexed query returns the same rows as the identical query run without the index.
- **R09.10** — `PRAGMA user_version` reads the page-1 header's `user_version` field at offset 60 — answering `0` for a database in which it has never been set — and `PRAGMA user_version = N` writes N into that field, so that the engine reads N back and stock `sqlite3` reads the same N out of the file afterwards with the rest of the database intact. The assignment form is silent: `PRAGMA user_version = N` writes the value and returns no result row of its own, so every line of a batch's measured output comes from the surrounding statements — a batch of four in which one is the assignment produces three lines, not four. Silence is not the rule for every pragma assignment, which is why this is stated rather than left to be inferred: stock `sqlite3` answers `PRAGMA journal_mode=wal` with the line `wal`. For `user_version` it answers nothing, and an engine that echoed the value it had just stored would emit a line this requirement does not allow.

The numbering above skips `R09.7`, `R09.8`, `R09.9`. That is not an omission from this page. A requirement number is a permanent identity in this task's ledger: it is assigned once and never reused or renumbered, so when a requirement is withdrawn, folded into another one, or found to be already true of the engine you inherited, its number simply stops appearing. A number that is not listed here is out of scope at this step and nothing is hidden behind it -- the bullets above are the whole of what this step adds, and `TEST_INTERFACE.md` covers nothing beyond them as new behaviour. One **inherited** obligation is in scope there as well, in the `09-refusal-surface` group: that a command-line argument the shell cannot use is a process-level failure. It is not new work -- it has been yours since step 01 -- and nothing else at this step provokes it, which is why it is asserted again here.

Everything the earlier steps established must keep working. One case from each earlier step is re-run against your current shell as this step's gate: a single gate failure means this step is not satisfied.

## Read these local references

Normative specifications, under `/app/spec/` — verbatim public documents, no
implementation in them:

  - `/app/spec/sqlite/lang_select.html`
  - `/app/spec/sqlite/lang_aggfunc.html`
  - `/app/spec/sqlite/datatype3.html`
  - `/app/spec/sqlite/lang_expr.html`
  - `/app/spec/sqlite/lang_createindex.html`
  - `/app/spec/sqlite/lang_insert.html`
  - `/app/spec/sqlite/optoverview.html`
  - `/app/spec/turso/COMPAT.md` — the upstream compatibility matrix at exactly
    this release: statement by statement and function by function, what the
    project claimed to support at that tag. Where it disagrees with the requirements listed in this step, **the requirements win**: the matrix is a description of the release, the requirement list is the contract.
  - `/app/spec/turso/manual.md` — the project manual at this release.
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
