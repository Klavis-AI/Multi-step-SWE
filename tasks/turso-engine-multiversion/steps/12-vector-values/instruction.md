# Step 12 — Vector values and distance functions that leave the file SQLite-readable

## Implement this milestone

You are building a relational database engine that stores data in a single
file in the SQLite file format, with a command-line shell as its only
interface. Stock `sqlite3` must be able to open and read what the engine
writes.

This step adds:

- **R12.1** — The page-1 schema cookie starts at zero on a database with no schema and increments by one per `CREATE TABLE`, holding the same values as stock `sqlite3` does for the same script.
- **R12.2** — Bytes 40..43 of the page-1 header hold the schema cookie and move with each schema change, so a reader reading only the header sees the same value `PRAGMA schema_version` reports.
- **R12.3** — A float32 vector — what `vector(x)` and `vector32(x)` return — is stored as four raw little-endian bytes per element with no marker, and a float64 vector — `vector64(x)` — as eight little-endian bytes per element followed by a single `02` marker byte, so an empty float64 vector is one byte long and an empty float32 vector is zero bytes long.
- **R12.4** — `vector_distance_l2` returns the Euclidean distance between two vectors, and a call between vectors of different dimensions is reported as an error rather than returning a number.
- **R12.5** — An `ORDER BY vector_distance_l2(…) LIMIT k` query over a table of vectors returns the k nearest rows in increasing distance order.
- **R12.6** — `ALTER TABLE` — `ADD COLUMN`, `DROP COLUMN` and `RENAME COLUMN` alike — rebuilds the stored schema text with the same spacing `spec/PROFILE.md` rule 2 gives a table created directly: no space inside the parentheses, one space after each comma.
- **R12.7** — Stock `sqlite3` opening a database containing vector values returns `ok` from `PRAGMA integrity_check`.
- **R12.8** — Stock `sqlite3`'s `.dump` of a table containing vector values emits the values as blob literals that re-import to the identical bytes.
- **R12.9** — A reader that does not know the vector type sees a vector column's value as an ordinary blob rather than an error.
- **R12.10** — `vector_distance_l2` is defined between two vectors of the same element width only: a call that mixes a float32 vector with a float64 vector is reported as an error rather than coerced to a common width, even where the two have the same number of elements.
- **R12.11** — `vector_extract(v)` returns the text form of a vector blob — the elements in order, comma-separated, inside square brackets, with no spaces — and an element whose value is integral is rendered without a fractional part, so `vector('[1,2]')` extracts back as `[1,2]`.

Everything the earlier steps established must keep working. One case from each earlier step is re-run against your current shell as this step's gate: a single gate failure means this step is not satisfied.

## Read these local references

Normative specifications, under `/app/spec/` — verbatim public documents, no
implementation in them:

  - `/app/spec/sqlite/datatype3.html`
  - `/app/spec/sqlite/fileformat2.html`
  - `/app/spec/sqlite/lang_expr.html`
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
