# Step 06 — WAL header and frame formats, salts and cumulative checksums, and the UPDATE statement

## Implement this milestone

You are building a relational database engine that stores data in a single
file in the SQLite file format, with a command-line shell as its only
interface. Stock `sqlite3` must be able to open and read what the engine
writes.

This step adds:

- **R06.1** — The 32-byte WAL header carries, at their specified offsets, one of the format's two magic values (`0x377f0682` or `0x377f0683` — a header carries one, not both), the format number 3007000, the database page size, a checkpoint sequence of zero in a log that has never been checkpointed, and a salt pair that is not all zeros; and every frame header in that log repeats the header's salt pair unchanged. A writing invocation that leaves no frame in the log at all is held instead to the durability rule: the committed rows must still be readable from the database file, which is what the case checks on that route.
- **R06.2** — The WAL carries the format's cumulative Fibonacci-weighted checksum pair: the header's pair checks the header's own first 24 bytes, and each frame's pair continues the running value over that frame header's first 8 bytes and its whole page image, computed in the byte order the header magic selects.
- **R06.3** — Each 24-byte frame header names a page number counted from one and, on a frame that ends a transaction, the database size in pages. A completed log ends on a commit record, and successive commit records never report the database shrinking. Whether a frame that does *not* end a transaction carries a zero there is not part of this contract: stock sqlite3's own log for the measured workload reads 0, 2, 2 across its three frames while this engine's reads 2, 2, 2, so requiring the zero would fail the checkpoint this step is cut from.
- **R06.10** — An `UPDATE` with no `WHERE` clause changes every row, and an assigned value may be an expression over that row's existing values, so each row is read before it is written.
- **R06.11** — An `UPDATE` with a `WHERE` clause changes exactly the matching rows and leaves every other row, and every column it does not assign, untouched. Several columns may be assigned in one statement, including one assigned from another column of the same row, and every assignment reads the row's pre-update values.
- **R06.12** — An `UPDATE` whose predicate matches no row is a no-op and not an error: the table is unchanged, no diagnostic is produced, and the session continues.
- **R06.13** — An `UPDATE` changes the storage class held in a column declared without a type, including to and from NULL, and `typeof()` reports the new class rather than the old one.
- **R06.14** — A `DELETE` under a predicate removes exactly the matching rows, with none skipped because the tree moved underneath the scan, and an unqualified `DELETE` empties the table completely.
- **R06.15** — `jsonb_object` and `jsonb_array` return the format's binary encoding — `typeof()` reports `blob`, where `json_object` and `json_array` with the same arguments report `text` — and that blob decodes to the document it names; `jsonb_set` overwrites an existing path where `json_insert` leaves it alone; and `json_group_array` and `json_group_object` collect a result set into a single document, whose element order follows the order their input was given in and is checked only where an `ORDER BY` fixes it.

The numbering above skips `R06.4`, `R06.5`, `R06.6`, `R06.7`, `R06.8`, `R06.9`. That is not an omission from this page. A requirement number is a permanent identity in this task's ledger: it is assigned once and never reused or renumbered, so when a requirement is withdrawn, folded into another one, or found to be already true of the engine you inherited, its number simply stops appearing. A number that is not listed here is out of scope at this step and nothing is hidden behind it -- the bullets above are the whole of what this step adds, and `TEST_INTERFACE.md` covers exactly the same set.

Everything the earlier steps established must keep working. One case from each earlier step is re-run against your current shell as this step's gate: a single gate failure means this step is not satisfied.

## Read these local references

Normative specifications, under `/app/spec/` — verbatim public documents, no
implementation in them:

  - `/app/spec/sqlite/fileformat2.html`
  - `/app/spec/sqlite/walformat.html`
  - `/app/spec/sqlite/wal.html`
  - `/app/spec/sqlite/atomiccommit.html`
  - `/app/spec/sqlite/pragma.html`
  - `/app/spec/sqlite/lang_update.html`
  - `/app/spec/sqlite/lang_delete.html`
  - `/app/spec/sqlite/json1.html`
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
