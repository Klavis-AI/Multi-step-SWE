# Step 07 — Index-driven range and backwards scans, column DEFAULT expressions, hex integer literals, and julianday() modifiers

## Implement this milestone

You are building a relational database engine that stores data in a single
file in the SQLite file format, with a command-line shell as its only
interface. Stock `sqlite3` must be able to open and read what the engine
writes.

This step adds:

- **R07.3** — Creating an index never changes a query's result set: the same query over an indexed table and over an unindexed copy of the same rows returns identical rows, sums and counts.
- **R07.5** — A backwards scan of an index on an INTEGER key and on a TEXT key returns rows in descending key order, under `LIMIT` as well as unbounded, and `min()`/`max()` over a bounded range agree with that order.
- **R07.11** — A range predicate resolved through an index whose leading key is TEXT returns exactly the rows the range describes, for `>`, `>=`, `<` and `=`, and the counts over those ranges agree with the rows.
- **R07.12** — `count()`, `sum()`, `group_concat()`, `min()` and `max()` computed over a range of an index see exactly the rows the range describes, and agree value for value with stock `sqlite3` reading the same file.
- **R07.13** — An index B-tree stock `sqlite3` built is read by the engine, and stock `sqlite3` then re-opens the same file, answers `PRAGMA integrity_check` with `ok`, still finds the index's `sqlite_schema` row and answers the same query identically.
- **R07.14** — A column omitted from an `INSERT` column list takes its declared `DEFAULT`, a column with no `DEFAULT` takes NULL, and an explicit value overrides both.
- **R07.15** — A `DEFAULT` clause accepts an unsigned integer, a signed integer, a real, a string literal and a parenthesised constant, and the stored value keeps the storage class the literal names, as `typeof()` reports it.
- **R07.16** — Hexadecimal integer literals parse as integers, store with storage class `integer`, and compare and arithmetically combine as the decimal value they name.
- **R07.17** — `likely()` and `likelihood()` parse and return their first argument unchanged, so wrapping a predicate in one of them cannot change which rows come back.
- **R07.18** — `julianday()` accepts one or more modifiers after its time value — day, month and year offsets, `start of month`, `start of year`, `start of day`, `weekday N`, and chains of them — and `timediff()` returns the signed interval between two time values. For literal arguments `julianday()` agrees with stock `sqlite3` value for value, and `timediff()` agrees over the intervals in scope for this step — spans shorter than 24 hours, and whole days inside a single month. Spans that cross a month boundary are outside the claim and out of scope; the measurement that drew that line is recorded with this step's other boundaries.

The numbering above skips `R07.1`, `R07.2`, `R07.4`, `R07.6`, `R07.7`, `R07.8`, `R07.9`, `R07.10`. That is not an omission from this page. A requirement number is a permanent identity in this task's ledger: it is assigned once and never reused or renumbered, so when a requirement is withdrawn, folded into another one, or found to be already true of the engine you inherited, its number simply stops appearing. A number that is not listed here is out of scope at this step and nothing is hidden behind it -- the bullets above are the whole of what this step adds, and `TEST_INTERFACE.md` covers exactly the same set.

Everything the earlier steps established must keep working. One case from each earlier step is re-run against your current shell as this step's gate: a single gate failure means this step is not satisfied.

## Read these local references

Normative specifications, under `/app/spec/` — verbatim public documents, no
implementation in them:

  - `/app/spec/sqlite/lang_createindex.html`
  - `/app/spec/sqlite/optoverview.html`
  - `/app/spec/sqlite/stricttables.html`
  - `/app/spec/sqlite/queryplanner.html`
  - `/app/spec/sqlite/lang_datefunc.html`
  - `/app/spec/sqlite/lang_createtable.html`
  - `/app/spec/sqlite/lang_expr.html`
  - `/app/spec/sqlite/lang_corefunc.html`
  - `/app/spec/sqlite/lang_aggfunc.html`
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
