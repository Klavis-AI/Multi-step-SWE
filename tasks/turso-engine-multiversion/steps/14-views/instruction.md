# Step 14 — CREATE VIEW and DROP VIEW, expanded at plan time

## Implement this milestone

You are building a relational database engine that stores data in a single
file in the SQLite file format, with a command-line shell as its only
interface. Stock `sqlite3` must be able to open and read what the engine
writes.

This step adds:

- **R14.1** — `CREATE VIEW` stores the view's text in `sqlite_schema` such that stock `sqlite3` re-parses it as the same view.
- **R14.2** — Selecting from a view returns exactly the rows its defining `SELECT` returns when written inline.
- **R14.3** — A view over a join returns the same rows as the join written inline, in the same order.
- **R14.4** — A view over an aggregate returns the same rows as the aggregate written inline, including its `GROUP BY` grouping.
- **R14.5** — A view that references another view resolves transitively and returns the rows the fully expanded query returns.
- **R14.6** — An `INSERT`, `UPDATE` or `DELETE` targeting a view is rejected: the engine reports an error for the statement, produces no rows for it, execution continues with the next statement, and the view's underlying table is unchanged. The specific wording of the diagnostic is not part of the requirement.
- **R14.7** — `DROP VIEW` removes the view, while `DROP TABLE` naming a view (or `DROP VIEW` naming a table) is rejected with an error report distinguishing the two cases: the engine reports an error, produces no rows, execution continues, and the schema remains unchanged. The specific wording is not part of the requirement.
- **R14.8** — A view's `sqlite_schema` row carries a root page of zero, because a view allocates no B-tree.
- **R14.9** — An `ALTER TABLE … ADD COLUMN` on a table a view reads from leaves the view's stored text byte-for-byte unchanged — the text is not rewritten to mention the new column — and the view still resolves against the altered table, returning the columns it projects for rows inserted before and after the alteration.
- **R14.10** — The CDC table gains an eighth column, `updates`, at this checkpoint. It is a BLOB; it is populated only for an `UPDATE`, where it holds the patch record of the columns that update named, in the layout the step's `TEST_INTERFACE.md` fixes; and it is NULL for an `INSERT` and for a `DELETE`. It is filled under capture mode `full` and under no other mode; no case at this step reads it under another mode, so populating it under every mode also passes.
- **R14.11** — Schema mutations are captured as well as data at this checkpoint: with capture on, a statement that writes a `sqlite_schema` row produces its own capture row whose `table_name` is `sqlite_schema` and whose `id` is the rowid of that schema row, ordered ahead of any capture row for data in the object it created.

Everything the earlier steps established must keep working. One case from each earlier step is re-run against your current shell as this step's gate: a single gate failure means this step is not satisfied.

## Read these local references

Normative specifications, under `/app/spec/` — verbatim public documents, no
implementation in them:

  - `/app/spec/sqlite/lang_createview.html`
  - `/app/spec/sqlite/lang_dropview.html`
  - `/app/spec/sqlite/lang_select.html`
  - `/app/spec/sqlite/schematab.html`
  - `/app/spec/sqlite/fileformat2.html`
  - `/app/spec/sqlite/pragma.html`
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
