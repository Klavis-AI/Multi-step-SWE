# Step 02 — PRAGMA wal_checkpoint, the JSON function family, and the first vector constructor and distance functions

## Implement this milestone

You are building a relational database engine that stores data in a single
file in the SQLite file format, with a command-line shell as its only
interface. Stock `sqlite3` must be able to open and read what the engine
writes.

This step adds:

- **R02.1** — `PRAGMA wal_checkpoint` copies WAL frames into the main database file and returns SQLite's result triple in the same shape stock `sqlite3` returns: one row of three integer columns.
- **R02.2** — A checkpoint is observationally invisible: every query run before it returns the identical result set after it.
- **R02.3** — With no other connection reading, nothing holds the checkpoint back: the triple's first column — SQLite's `busy` flag — is `0`, and the number of frames it copied equals the number of frames the log held, so the two remaining columns are equal. Both count columns render as plain non-negative decimal integers: the `-1` the format documents for a database with no write-ahead log cannot obtain here, because each case that reads the triple has just written in the same invocation.
- **R02.4** — After a checkpoint, stock `sqlite3` reading the `.db` file alone — with `main.db-wal` and `main.db-shm` deleted — sees every checkpointed row and returns `ok` from `PRAGMA integrity_check`, and the same stock read of `sqlite_schema` lists exactly the tables the script created — the engine writes no schema row of its own beyond them.
- **R02.7** — `json_object`, `json_patch`, `json_remove` and `json_pretty` return exactly the text stock `sqlite3`'s JSON functions return for the same arguments, including `json_pretty`'s four-space-per-level default indentation and the layout its optional second argument selects instead. Two keys of the same name in one `json_object` call are out of scope at this step.
- **R02.8** — `json_valid` returns `0` for malformed JSON text instead of raising an error.
- **R02.9** — A JSON path argument — the second argument to `json_extract` — follows SQLite's `$.a[0]` syntax — an array subscript after an object key, nesting to any depth, and `$[0]` at the root — and a malformed path is refused rather than silently ignored: the statement yields no row, and the statements after it still run. A path that is well formed but selects nothing is not malformed: it is not refused, and it answers `NULL`.
- **R02.10** — A JSON function given a non-text argument applies SQLite's exact argument coercion: an integer or a real is emitted as a JSON number and never as a quoted string, `NULL` propagates, the JSON literals `true`/`false`/`null` keep their own types, and a blob that is not valid JSON is refused by `json_object` and `json_array` and answers `0` from `json_valid` — each of them the answer stock `sqlite3` gives. The functions this row covers are `json_object`, `json_array`, `json`, `json_type`, `json_valid`, `json_extract` and `json_array_length`, and `json_type` names what it finds with SQLite's own vocabulary — `true`, `false`, `null`, `integer`, `real` and `text`.
- **R02.11** — The vector functions introduced at this checkpoint are callable and return a value rather than an unknown-function diagnostic, and the values are the ones the definitions fix: `vector(t)` is a blob, `vector_extract` returns the bracketed comma-separated text the vector was built from, and `vector_distance_cos` answers `0.0` for identical vectors, `1.0` for orthogonal ones and `2.0` for opposed ones.
- **R02.12** — A vector value is an ordinary blob in an ordinary column: it survives the process that wrote it, it occupies four bytes per element, and stock `sqlite3` — which has never heard of the vector functions — opens the same file, reports the column as a blob and agrees about its length.
- **R02.13** — A rowid range is seeked rather than mis-planned: `>`, `>=`, `<=`, `<`, `BETWEEN …` in ascending order and in descending order (`ORDER BY … DESC`) and `IN (…)` each return the rows stock `sqlite3` returns, each range agrees with the aggregate taken over the same range, and a range whose ends cross selects nothing.

The numbering above skips `R02.5`, `R02.6`. That is not an omission from this page. A requirement number is a permanent identity in this task's ledger: it is assigned once and never reused or renumbered, so when a requirement is withdrawn, folded into another one, or found to be already true of the engine you inherited, its number simply stops appearing. A number that is not listed here is out of scope at this step and nothing is hidden behind it -- the bullets above are the whole of what this step adds, and `TEST_INTERFACE.md` covers exactly the same set.

Everything the earlier steps established must keep working. One case from each earlier step is re-run against your current shell as this step's gate: a single gate failure means this step is not satisfied.

## Read these local references

Normative specifications, under `/app/spec/` — verbatim public documents, no
implementation in them:

  - `/app/spec/sqlite/wal.html`
  - `/app/spec/sqlite/walformat.html`
  - `/app/spec/sqlite/pragma.html`
  - `/app/spec/sqlite/json1.html`
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
