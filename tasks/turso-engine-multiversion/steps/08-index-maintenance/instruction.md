# Step 08 — DROP INDEX, descending indexes, index upkeep on DELETE, ALTER TABLE ... RENAME TO, and the page_size and schema_version pragmas

## Implement this milestone

You are building a relational database engine that stores data in a single
file in the SQLite file format, with a command-line shell as its only
interface. Stock `sqlite3` must be able to open and read what the engine
writes.

This step adds:

- **R08.1** — `DROP INDEX` removes the index's row from `sqlite_schema`, so a read of `sqlite_schema` taken after the statement no longer lists the index that a read taken before it did list.
- **R08.2** — After `DROP INDEX`, every query that could have used the index returns the identical result set, row for row and in the same order, that it returned while the index existed. Which access path answers it afterwards is out of scope; `TEST_INTERFACE.md` says why, under "Access paths are not an observable at this step".
- **R08.3** — A `DESC` index stores its keys in descending order: a scan of it yields values in descending order, **and** stock `sqlite3` running `PRAGMA integrity_check` over the resulting file walks the index against its table in the order the schema text declares and reports `ok`.
- **R08.6** — A `DELETE` removes the row's index entries — an index-driven lookup of a deleted key finds nothing, the index object itself remains in `sqlite_schema`, and stock `sqlite3` finds no orphan index entry when it runs `PRAGMA integrity_check` on the resulting file.
- **R08.7** — A predicate selecting only keys below every stored key, and one selecting only keys above every stored key, each return no rows rather than the nearest stored entry. Which access path answers them is out of scope; `TEST_INTERFACE.md` says why, under "Access paths are not an observable at this step".
- **R08.9** — `ALTER TABLE … RENAME TO` — the one `ALTER TABLE` form this checkpoint implements — runs, and the new table name is visible both in a subsequent query of `sqlite_schema` and to stock `sqlite3` re-opening the file.
- **R08.11** — `PRAGMA page_size` returns the page size recorded in the database header of the file that was opened, which is the same value stock `sqlite3` reads from that file.
- **R08.12** — `PRAGMA schema_version` returns the schema cookie stored in the page-1 header of the file that was opened, which is the same value stock `sqlite3` reads from that file.

The numbering above skips `R08.4`, `R08.5`, `R08.8`, `R08.10`. That is not an omission from this page. A requirement number is a permanent identity in this task's ledger: it is assigned once and never reused or renumbered, so when a requirement is withdrawn, folded into another one, or found to be already true of the engine you inherited, its number simply stops appearing. A number that is not listed here is out of scope at this step and nothing is hidden behind it -- the bullets above are the whole of what this step adds, and `TEST_INTERFACE.md` covers nothing beyond them as new behaviour. Two **inherited** obligations are in scope there as well, in the `s08-refusal-surface` group: that a statement the engine cannot execute is refused rather than silently ignored, and that a command-line argument the shell cannot use is a process-level failure. Neither is new work -- both have been yours since step 01 -- and nothing else at this step provokes either, which is why they are asserted again here.

Everything the earlier steps established must keep working. One case from each earlier step is re-run against your current shell as this step's gate: a single gate failure means this step is not satisfied.

## Read these local references

Normative specifications, under `/app/spec/` — verbatim public documents, no
implementation in them:

  - `/app/spec/sqlite/lang_dropindex.html`
  - `/app/spec/sqlite/lang_createindex.html`
  - `/app/spec/sqlite/lang_altertable.html`
  - `/app/spec/sqlite/pragma.html`
  - `/app/spec/sqlite/fileformat2.html`
  - `/app/spec/sqlite/schematab.html`
  - `/app/spec/sqlite/lang_update.html` — the reference for `UPDATE` syntax.
    It is on this list as that reference and not as a promise that updating
    an indexed column is in scope: see "What this milestone does not
    include" in `/app/TEST_INTERFACE.md`
  - `/app/spec/sqlite/optoverview.html`
  - `/app/spec/turso/COMPAT.md` — the upstream compatibility matrix at exactly
    this release: statement by statement and function by function, what the
    project claimed to support at that tag. Where it disagrees with the requirements listed in this step, **the requirements win**: the matrix is a description of the release, the requirement list is the contract.
  - `/app/spec/turso/CHANGELOG.md`, `/app/spec/turso/README.md` — what this release
    added, in the project's own words. The same precedence rule applies to them:
    a capability the release notes announce is not in scope here unless a
    requirement above names it
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
