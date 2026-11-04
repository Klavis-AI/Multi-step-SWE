# Step 04 — SQLite's balance-siblings and balance-deeper, CAST and LIKE compatibility

## Implement this milestone

You are building a relational database engine that stores data in a single
file in the SQLite file format, with a command-line shell as its only
interface. Stock `sqlite3` must be able to open and read what the engine
writes.

This step adds:

- **R04.1** — An insert workload that forces sibling page splits redistributes cells by SQLite's balance-siblings rule, so every row reads back in key order from both ends of the tree and `PRAGMA page_count` on the finished file reports the count that rule determines — the same count stock `sqlite3` reports for the same statements at the same page size. The workload arrives as one multi-row `INSERT INTO t VALUES (…), (…), …` statement carrying all of the rows, highest key first.
- **R04.2** — An insert workload that grows the tree's depth (balance-deeper) splits the root into an interior page with leaves below it, so every row still reads back in key order, a range whose lower bound falls inside the split is answered through the new interior node, and `PRAGMA page_count` reports the pages that distribution leaves.
- **R04.4** — `CAST` from text to integer or real matches SQLite exactly for leading and trailing whitespace, a trailing non-numeric tail, an exponent and fractional truncation, and for the three text forms SQLite does **not** read as numbers — `'nan'`, `'inf'` and a `'0x…'` hex prefix — each of which converts to zero.
- **R04.5** — `CAST` of a wholly non-numeric text value yields zero rather than an error, as stock `sqlite3` does, and the storage class follows the target: `CAST('abc' AS REAL)` is the real `0.0`, while a cast to `INTEGER` or to `NUMERIC` yields the integer `0`, which `typeof()` reports as `integer`.
- **R04.6** — `LIKE` with an `ESCAPE` clause returns the same matches as stock `sqlite3`, including an escaped wildcard matched literally.
- **R04.7** — `LIKE` applied to a non-text argument (integer, real, blob or NULL) returns the value stock `sqlite3` returns rather than raising an error.
- **R04.8** — The shell's `.dump` renders a blob value as an `X'…'` literal with lowercase hex digits, and a fresh database stock `sqlite3` builds from that dump holds the identical bytes.

The numbering above skips `R04.3`. That is not an omission from this page. A requirement number is a permanent identity in this task's ledger: it is assigned once and never reused or renumbered, so when a requirement is withdrawn, folded into another one, or found to be already true of the engine you inherited, its number simply stops appearing. A number that is not listed here is out of scope at this step and nothing is hidden behind it -- the bullets above are the whole of what this step adds, and `TEST_INTERFACE.md` covers nothing beyond them as new behaviour. One **inherited** obligation is in scope there as well, in the `04-refusal-surface` group: that a command-line argument the shell cannot use is a process-level failure. It is not new work -- it has been yours since step 01 -- and nothing else at this step provokes it, which is why it is asserted again here.

Everything the earlier steps established must keep working. One case from each earlier step is re-run against your current shell as this step's gate: a single gate failure means this step is not satisfied.

## Read these local references

Normative specifications, under `/app/spec/` — verbatim public documents, no
implementation in them:

  - `/app/spec/sqlite/fileformat2.html`
  - `/app/spec/sqlite/lang_expr.html`
  - `/app/spec/sqlite/datatype3.html`
  - `/app/spec/sqlite/atomiccommit.html`
  - `/app/spec/sqlite/cli.html`
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
