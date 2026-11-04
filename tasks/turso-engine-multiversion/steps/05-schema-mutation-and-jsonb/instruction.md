# Step 05 — DROP TABLE, BEGIN DEFERRED, MVCC as a selectable engine, JSONB

## Implement this milestone

You are building a relational database engine that stores data in a single
file in the SQLite file format, with a command-line shell as its only
interface. Stock `sqlite3` must be able to open and read what the engine
writes.

This step adds:

- **R05.5** — `BEGIN DEFERRED` opens a transaction this engine accepts: every row written inside one is present once `COMMIT` has returned, and a deferred transaction that performs no write opens and closes leaving the table exactly as it found it.
- **R05.7** — `json(jsonb(X))` returns X's canonical JSON text for every JSON value kind — objects, arrays, integers, reals, both booleans, null, plain and escaped strings, and arbitrary nesting — and `jsonb()` applied to a value that is already JSONB is idempotent.
- **R05.8** — A JSONB blob the engine writes into a database file is read back by stock `sqlite3` to the same JSON text, the same `json_extract`, `json_type` and `json_array_length` answers, and storage class blob; re-encoding that text with stock `sqlite3`'s own `jsonb()` reproduces the stored bytes exactly.
- **R05.9** — `length()` of a JSONB value names the width of its payload-size field: a payload counted in the header byte's own size nibble, then the 1-, 2- and 4-byte size fields above it, each boundary crossed by the smallest step that crosses it.
- **R05.11** — `hex(jsonb(X))` is the element header byte SQLite specifies for each JSON value kind — element type in the low nibble, payload size in the high nibble — for null, both booleans, integers of three widths, two real spellings, a short string, the empty array and the empty object, and arrays and objects with elements inside them, including nested ones.
- **R05.12** — The JSON path accessors `json_extract`, `jsonb_extract`, `json_type` and `json_array_length` take a JSONB argument directly, with no `json()` conversion in the expression, and return the same answers they return for the equivalent JSON text, through object keys, array subscripts and nested paths.
- **R05.13** — `jsonb_remove` and `jsonb_replace` accept either representation of the document they edit — JSON text or a JSONB blob — and `jsonb_remove` returns the binary one: storage class blob, carrying the element encoding this step specifies, so its result feeds straight into a path accessor with no conversion. `jsonb_replace` over a path that is absent leaves the document unchanged.
- **R05.14** — `json_replace` overwrites every path that exists — several in one call, and with a whole document as the replacement value — and leaves every path that does not exist alone, including an array subscript past the end and a key in an empty object. A replacement value of SQL `NULL` is a value like any other and does not change which paths are written: on a path that exists it is written and that path's value becomes JSON `null`, and on a path that does not exist it is still not written.
- **R05.15** — A JSONB value inserted into a column is stored with storage class blob at the length its encoding determines, comes back out of the table with its bytes unchanged, and is read by the path accessors after that round trip.
- **R05.16** — A blob whose outermost element header is not one a reader can follow, and a JSON path that does not parse, are each refused where they stand: the call returns no row, the statements after it still run and still answer, and the stored document is unchanged and still readable afterwards. A path that parses but selects nothing is not a refusal — it answers NULL. Validating the *deep* structure of a JSONB document is not required and is out of scope: `json1.html` §4.2 says only the outermost element is examined.

The numbering above skips `R05.1`, `R05.2`, `R05.3`, `R05.4`, `R05.6`, `R05.10`. That is not an omission from this page. A requirement number is a permanent identity in this task's ledger: it is assigned once and never reused or renumbered, so when a requirement is withdrawn, folded into another one, or found to be already true of the engine you inherited, its number simply stops appearing. A number that is not listed here is out of scope at this step and nothing is hidden behind it -- the bullets above are the whole of what this step adds, and `TEST_INTERFACE.md` covers exactly the same set.

Everything the earlier steps established must keep working. One case from each earlier step is re-run against your current shell as this step's gate: a single gate failure means this step is not satisfied.

## Read these local references

Normative specifications, under `/app/spec/` — verbatim public documents, no
implementation in them:

  - `/app/spec/sqlite/lang_droptable.html`
  - `/app/spec/sqlite/lang_transaction.html`
  - `/app/spec/sqlite/json1.html`
  - `/app/spec/sqlite/lang_corefunc.html`
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
