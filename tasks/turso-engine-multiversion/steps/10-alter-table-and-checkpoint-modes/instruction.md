# Step 10 — ALTER TABLE ADD/DROP/RENAME COLUMN, wal_checkpoint accepting an explicit mode argument, hex() on high bytes, and PRAGMA integrity_check

## Implement this milestone

You are building a relational database engine that stores data in a single
file in the SQLite file format, with a command-line shell as its only
interface. Stock `sqlite3` must be able to open and read what the engine
writes.

This step adds:

- **R10.1** — `ALTER TABLE … RENAME TO` rewrites the stored `sql` text so stock `sqlite3` re-parses the identical schema under the new name.
- **R10.2** — `ALTER TABLE … RENAME COLUMN` rewrites every reference to the column in the stored schema text, including the definitions of indexes on that column, so stock `sqlite3` re-parses them all.
- **R10.3** — `ALTER TABLE … ADD COLUMN` extends the table without rewriting the rows already on disk: with a `DEFAULT` those short records read the default value, without one they read NULL, and a second `ADD COLUMN` appends after the first, so `pragma_table_info` reports the columns in the order they were added and a row inserted afterwards lines up with them. `pragma_table_info` reports the added column's `dflt_value` as the default's **literal source text**, exactly as it was written in the `ADD COLUMN` clause and including any quotes: a column added `DEFAULT 'none'` reports `'none'`, with the single quotes present, not `none`. A column with no default reports the empty string.
- **R10.4** — `ALTER TABLE … ADD COLUMN … NOT NULL` without a default is accepted — where stock `sqlite3` refuses it — and the rows already on disk read NULL in the new column.
- **R10.5** — `ALTER TABLE … DROP COLUMN` removes the column from the schema text and from every row's record.
- **R10.6** — `ALTER TABLE … DROP COLUMN` is refused both when the named column does not exist and when it is the table's `INTEGER PRIMARY KEY`. In each case the statement produces no rows, the batch carries on, and the schema and the rows are left as they were.
- **R10.7** — `PRAGMA wal_checkpoint(<MODE>)` accepts a mode argument, where before it accepted only the bare form. `PASSIVE` — in any mixture of upper and lower case — answers a well-formed busy/frames/copied triple in which busy is `0`, the frame count is **greater than zero** — a checkpoint taken after committed writes has at least one frame to move, so a triple of three zeroes is not a passing answer — and the copied count equals the frame count, and the same WAL state answers the same triple for every spelling. The three spellings are asked back to back inside one batch of one session, over one WAL, with no write between them, and that is the fixture the requirement is measured on: a `PASSIVE` checkpoint does **not** restart the log, so the second and the third report the same non-zero frame and copied counts as the first rather than falling to zero. An engine that resets its WAL on `PASSIVE` answers one well-formed triple and then two of zeroes, and does not satisfy this. An unknown mode name is refused. `FULL`, `RESTART` and `TRUNCATE` are each handled: the engine answers the statement, the batch carries on, and the rows are intact afterwards.
- **R10.8** — `hex()` on a blob containing bytes `>= 0x80` returns their correct two-digit uppercase encoding, where earlier checkpoints corrupted them to `EFBFBD`.
- **R10.9** — `PRAGMA integrity_check` parses and executes, returning `ok` for a clean database, where earlier checkpoints rejected it as `Not a valid pragma name`.
- **R10.10** — A database this engine creates carries SQLite's own default page-cache size in page-1 header bytes 48..51: big-endian signed `-2000`, that is the bytes `ff ff f8 30`, where the two previous checkpoints wrote `500` (`00 00 01 f4`). `PRAGMA cache_size` reports the same `-2000`.

Everything the earlier steps established must keep working. One case from each earlier step is re-run against your current shell as this step's gate: a single gate failure means this step is not satisfied.

## Read these local references

Normative specifications, under `/app/spec/` — verbatim public documents, no
implementation in them:

  - `/app/spec/sqlite/lang_altertable.html`
  - `/app/spec/sqlite/pragma.html`
  - `/app/spec/sqlite/wal.html`
  - `/app/spec/sqlite/schematab.html`
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
