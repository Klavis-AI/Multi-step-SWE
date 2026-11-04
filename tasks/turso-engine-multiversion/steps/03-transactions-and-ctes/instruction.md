# Step 03 — BEGIN IMMEDIATE/EXCLUSIVE, non-recursive CTEs, and DELETE reporting cleanly

## Implement this milestone

You are building a relational database engine that stores data in a single
file in the SQLite file format, with a command-line shell as its only
interface. Stock `sqlite3` must be able to open and read what the engine
writes.

This step adds:

- **R03.4** — A statement that cannot run is refused where it stands inside an open transaction rather than being swallowed by it, and the transaction stays usable afterwards: a query issued later, still inside the same transaction, still answers.
- **R03.5** — A `BEGIN`, a `BEGIN IMMEDIATE` or a `BEGIN EXCLUSIVE` issued while a transaction is already open is refused — in each of those three spellings — and refusing it does not close or disturb the transaction that is open.
- **R03.6** — A non-recursive `WITH … AS (…) SELECT` returns the same rows in the same order as stock `sqlite3`: a CTE that filters, one that aggregates, one built from constants with no table under it, one whose columns are expressions, one grouped before the outer query sees it, one that orders and limits inside itself while the outer query orders differently, one CTE feeding the next, and a CTE joined back to the table it came from.
- **R03.11** — `BEGIN IMMEDIATE` and `BEGIN EXCLUSIVE` are accepted where the previous checkpoint refused them, with and without the optional `TRANSACTION` keyword, and an accepted one produces no report at all.
- **R03.12** — A `COMMIT` or an `END` issued with no transaction open is refused rather than silently ignored.
- **R03.13** — `DELETE FROM … WHERE` removes exactly the rows its predicate selects and no others — taking the first row of the table, one from the middle, and the last — the survivors keep their values and their rowid order, and `changes()` reports how many rows the statement actually took.
- **R03.14** — A table emptied by `DELETE`, one row at a time, is still a table: its `sqlite_schema` row survives, it answers a query with nothing rather than failing, and rows inserted afterwards are found again — with the rowid stock `sqlite3` hands out, which after an emptied table restarts at 1, as `last_insert_rowid()` reports.
- **R03.15** — `json_set` replaces the value at a path that exists and creates one at a path that does not — at the top level and nested, in an object and in an array, with a JSON value and with SQL `NULL` — and `json_quote` renders a SQL value as the JSON text that denotes it. Every answer is the text stock `sqlite3` returns for the same call.
- **R03.16** — A JSON path argument that does not parse is refused where it stands: the call yields no row, the statements after it still run, and the document is left exactly as it was. A path that parses but selects nothing is not an error — it answers `NULL`.
- **R03.17** — `CAST` between the storage classes answers as stock `sqlite3` answers: a cast to `INTEGER` takes the leading numeric prefix and yields `0` where there is not one, a negative real truncates toward zero rather than toward minus infinity, `NUMERIC` narrows a text value to the narrower storage class it fits while a REAL or INTEGER passed to `NUMERIC` is unchanged, and `NULL` stays `NULL` whatever it is cast to.
- **R03.18** — A freshly created database reports `PRAGMA user_version` as `0`, and the four bytes at page-1 header offset 60 that back it are zero in the file as well — as read by `od` and by stock `sqlite3`, neither of which asks this engine anything, so an engine that answers the pragma from memory without writing the header is caught by the disagreement.

The numbering above skips `R03.1`, `R03.2`, `R03.3`, `R03.7`, `R03.8`, `R03.9`, `R03.10`. That is not an omission from this page. A requirement number is a permanent identity in this task's ledger: it is assigned once and never reused or renumbered, so when a requirement is withdrawn, folded into another one, or found to be already true of the engine you inherited, its number simply stops appearing. A number that is not listed here is out of scope at this step and nothing is hidden behind it -- the bullets above are the whole of what this step adds, and `TEST_INTERFACE.md` covers nothing beyond them as new behaviour. One **inherited** obligation is in scope there as well, in the `03-refusal-surface` group: that a command-line argument the shell cannot use is a process-level failure. It is not new work -- it has been yours since step 01 -- and nothing else at this step provokes it, which is why it is asserted again here.

Everything the earlier steps established must keep working. One case from each earlier step is re-run against your current shell as this step's gate: a single gate failure means this step is not satisfied.

## Read these local references

Normative specifications, under `/app/spec/` — verbatim public documents, no
implementation in them:

  - `/app/spec/sqlite/lang_transaction.html`
  - `/app/spec/sqlite/lang_with.html`
  - `/app/spec/sqlite/lang_delete.html`
  - `/app/spec/sqlite/lockingv3.html`
  - `/app/spec/sqlite/json1.html`
  - `/app/spec/sqlite/datatype3.html`
  - `/app/spec/turso/COMPAT.md` — the upstream compatibility matrix at exactly
    this release: statement by statement and function by function, what the
    project claimed to support at that tag. Where it disagrees with the requirements listed in this step, **the requirements win**: the matrix is a description of the release, the requirement list is the contract.
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
