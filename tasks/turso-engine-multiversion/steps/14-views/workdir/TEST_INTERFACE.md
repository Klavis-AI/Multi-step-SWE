# Test interface — step 14

The step's contract with the verifier. Every statement here is what the
harness does, not a paraphrase of it. If the verifier needs a name, a path, a
format or an exit status, it is here or in a document named by
`instruction.md`.

## The artefact

| | |
|---|---|
| Path | `/app/bin/tursodb` |
| Kind | a regular file, executable, **not a symlink** |
| Built from | your workspace, by your own build |

The verifier never builds anything. If that path is missing, is a symlink, or
is not executable when checking starts, no requirement group can pass and
`reward_details.json` says why. `/app/target/` and `/app/.git/` are excluded
from the artefact handed to the verifier, so **installing is not optional** —
build output alone does not cross the boundary.

`/app/bin/limbo` belongs to the steps before the rename at step
11. Every case in scope here runs `/app/bin/tursodb`.

`/app/build.sh` does the build and the install. Use it or do the same two
things your own way.

## The runtime sandbox

Your binary is checked inside a container running under Docker's default seccomp
profile. That profile does not permit the `io_uring` family of syscalls:
`io_uring_setup` returns `EPERM`. An engine that sets up an io_uring instance
on its default startup path cannot open a database here -- it exits with
`Operation not permitted (os error 1)` before it reads any SQL, and every group
then fails on the proof-of-life check.

This is a property of the sandbox, not of the milestone. No behaviour contract
in this task depends on which I/O backend you use. Choose one that starts.

## The invocation

**SQL cases.** The shell is run once per case, with the current directory set
to a fresh empty directory, so a case always starts with no database:

```
cd <empty dir> && /app/bin/tursodb -m list -q main.db < case.sql
```

The case file is pure SQL on standard input, one or more statements. Standard
output is captured; standard error is ignored.

A case file may carry `--` line comments and blank lines. Both are fed to the
shell on standard input along with the statements, and SQL defines `--` as a
comment to end of line, so the shell must consume them silently: an engine that
reports a comment line as a syntax error fails every group at this step. The
statements in scope are the non-comment lines; a comment in a case file names
the requirement it covers and is not itself in scope.

**Shell cases.** Run with `bash`, current directory likewise a fresh empty
directory, with these variables exported:

| Variable | Value |
|---|---|
| `CLI` | `/app/bin/tursodb` |
| `SQLITE3` | `/tests/assets/bin/sqlite3`, stock SQLite 3.50.2 |
| `CLI_MODE` | `list` |
| `CASEDIR` | the directory holding the case |

Standard output is captured; standard error is ignored, exactly as it is for a
SQL case. **No case ever merges the two streams of the engine under test into
output it compares**: the error report that `PROFILE.md` rule 6 describes
arrives on standard output, and a case that folded your standard error in
would pass a build that had put the report on the wrong stream. An engine may
write anything it likes to standard error -- progress, warnings, a backtrace
on a panic -- and nothing that is compared reads it.

Some shell cases do redirect `2>&1` on the **stock** `$SQLITE3` fixture, which
is a pinned 3.50.2 binary and not the thing being checked. That is deliberate
and it is the opposite of a loosened assertion: folding stock's standard error
into the captured line means a stock failure lands in the compared output and
fails the case loudly, where discarding it would leave a silent empty line
that happens to compare equal. Read a `2>&1` in a case by which binary it is
attached to.


Every case is run under a 120-second timeout. A case that is killed by it
fails.

## Command-line surface the harness uses

| | |
|---|---|
| `-m <mode>` | select result rendering. This step passes `-m list`. The harness always passes it explicitly, so your default may be anything. |
| `-q` | quiet: no banner, no prompt, no timing line, nothing on standard output but result rows and the error reports `spec/PROFILE.md` rule 6 describes. |
| *positional* | the database file path. It may not exist yet, in which case it is created. |
| `--help` | asked for by at least one case at this step, which reads the option list and passes an option only if it is offered. The option looked for is the experimental index switch, and the two spellings recognised are named here so that nothing about your help text has to be guessed: a case that finds the substring `--experimental-indexes <` passes `--experimental-indexes=true`, a case that finds `--experimental-indexes` without it passes `--experimental-indexes`, and a case that finds neither passes no such option and runs anyway. Write your options to standard output and exit, or write nothing and exit; either is fine. That probe is the one place a case reads your two streams together -- it runs `--help` with `2>&1` because it is looking for an option's name rather than checking output, so either stream will do and nothing it finds there is compared. A shell that opens an interactive session instead will hang the case until its 120-second timeout. |

In `list` mode a result row is one line, columns separated by `|`, no
header row, no alignment padding, and NULL rendered as the empty string. The
shell adds no line break of its own: a newline *inside* a text value is written
through, so a value holding one occupies more than one output line.

## How output is compared

- **SQL cases**: byte for byte, after blank lines are removed from the
  produced output. Blank separator lines between result blocks are therefore
  not part of the contract; every value, its rendering, its column separator
  and its row order are. The expected files hold no blank lines, so removing
  them from one side and not the other is the same comparison.
- **Shell cases**: byte for byte, with no normalisation at all. A shell case
  owns its own standard output, and any canonicalisation a case needs it does
  itself, visibly, inside the case.

In both kinds, standard error is captured separately and never compared.

## Exit status

Every case has an expected exit status, and it is `0` — the process ran —
unless the case carries a sibling `.exit` file holding the word `err`, in
which case any non-zero status satisfies it. Exit statuses 124 and 137 are
read as a timeout and always fail, whichever status was expected.

Which process the status belongs to differs by case kind, and this is the
part worth getting right:

- For a **SQL case** the status is the engine's own, because the engine *is*
  the process the harness ran. A statement the engine refuses is reported and
  the batch continues, so a refusal does not move the status: it stays `0`.
- For a **shell case** the status is the **script's**, not the engine's. The
  script may run the engine many times, and an invocation that fails at the
  process level — a panic at `101`, an unusable argument at `2`, a file the
  engine will not open — sets the script's status only if the script lets it.
  A shell case that expects such a failure must catch it and turn it into a
  line on standard output, the way it canonicalises everything else it
  reports. A shell case that leaves an expected failure uncaught fails on the
  status rather than on the comparison.

The exit status is never used to express "this SQL was rejected" — that
condition is carried, on standard output, by the marker described above.

No test compares the text of a diagnostic.

## Scope

The step's groups come from `tests/assets/groups.txt`, plus the
`gate-proof-of-life` group, which the harness injects rather than reading from
that file. The "Gate groups" table below includes the injected group; the
"Requirement groups" table does not, because a gate group is not a new
requirement.

**The gate** holds when every gate group passes every one of its cases. A gate
group adds no new requirement; a failure in one means the step is not satisfied.
Proof of life (`gate-proof-of-life`: the installed shell starts and answers
`SELECT 1;`) is always in the gate; the other gate groups are one end-to-end
case from each earlier step, re-run unchanged against your current shell.

**The new work** covers the requirement groups below. Every group is in scope
and every case in a group must pass. A build that preserves everything earlier
and implements nothing new satisfies none of this step's new work. A build that
implements everything new and breaks one earlier behaviour is not satisfied
either, and `reward_details.json` names the gate group and the case that
failed it.

### Gate groups — must all pass

| Group | Re-runs |
|---|---|
| `gate-proof-of-life` | `SELECT 1;` against the installed shell |
| `gate-01` | step 01, `like-predicates.sql` — five rows inserted, then returned through WHERE, ORDER BY and count() under LIKE patterns with % and _ in every position |
| `gate-02` | step 02, `json-argument-coercion.sql` — json_object, json_array, json, json_type, json_valid, json_extract and json_array_length over integer, real, NULL, text and JSON-literal arguments |
| `gate-03` | step 03, `delete-single-rows.sql` — single-row DELETE from the middle, the front and the end of a table, each followed by changes(), the surviving rows, and aggregates over them |
| `gate-04` | step 04, `cast-prefix-and-truncation.sql` — CAST of text to INTEGER, REAL and NUMERIC: leading whitespace, trailing junk, a truncated fraction, an exponent, an empty string and a non-numeric string |
| `gate-05` | step 05, `jsonb-element-headers.sql` — the hex jsonb() produces for each element kind - null, true, false, integers of three sizes, reals, text, empty array, empty object, and nested arrays and objects |
| `gate-06` | step 06, `update-value-types.sql` — UPDATE changing the storage class held in an untyped column, including to and from NULL, with typeof() following the new value |
| `gate-07` | step 07, `insert-omitted-columns-default.sql` — a column omitted from the INSERT column list taking its declared DEFAULT, a column with no DEFAULT taking NULL, and an explicit value overriding both |
| `gate-08` | step 08, `drop-index-then-table-scan.sh` — DROP INDEX removes the index row from sqlite_schema, and the same query still answers correctly from the table scan that replaces it |
| `gate-09` | step 09, `collate-nocase-ordering.sh` — COLLATE NOCASE in ORDER BY and in an equality test, beside the byte-ordered BINARY default, over a table stock sqlite3 wrote |
| `gate-10` | step 10, `add-column-null-fill.sh` — ALTER TABLE ADD COLUMN twice: existing rows read back NULL in the new columns, a later INSERT fills them, and pragma_table_info reports the new shape |
| `gate-11` | step 11, `generate-series-bounds.sql` — generate_series in a FROM clause: an ascending range, an explicit step, negative bounds, aggregates over the series, and a range that yields no rows |
| `gate-12` | step 12, `single-element-and-wide-vectors.sh` — the stored length and the extracted text of a one-element and of an eight-element float32 vector, and vector_distance_l2 over the whole vector |
| `gate-13` | step 13, `transaction-ordering.sql` — five mutations inside one transaction - three INSERTs, an UPDATE and a DELETE issued in non-key order - read back after COMMIT as five capture rows in mutation order, with change_ids 1 to 5 and no gap, each carrying its own change_type and the rowid it touched, beside the count of them and the rows the transaction left in the table |

### Requirement groups

| Group |
|---|
| `s14-cdc-updates` |
| `s14-views-diagnostics` |
| `s14-refusal-surface` |
| `s14-views-interop` |
| `s14-views-query` |
| `s14-views-schema` |

`s14-views-query` and `s14-views-schema` cover the capability this step
introduces; `s14-cdc-updates`, `s14-views-diagnostics`, `s14-refusal-surface`
and `s14-views-interop` cover breadth at this step. Nothing in scope here is
satisfiable by an earlier step's work alone.

The harness reads `tests/assets/groups.txt`, never this table; if the two ever
disagree, the file is what the harness follows.

This step's requirements are independent of every other step's.

## Which group covers which requirement

Every requirement group at this step exists for rows of the behaviour contract
below, and every row has a group. Nothing here is in scope that the table does
not name, with one exception, and it is the same exception at every step: the
process-level contract -- the exit-status rules under "Exit status" above --
is an inherited obligation rather than a row of this step's own table.
`s14-refusal-surface` is the group that observes it: `R01.16`, introduced and
stated at step 01, remains an obligation on your engine here, and it is not a
new requirement of this milestone.

| Group | Requirements it covers | What it observes |
|---|---|---|
| `s14-views-query` | R14.2, R14.3, R14.4, R14.5 | the rows a `SELECT` from a view returns, set beside the rows the same query written inline returns -- over a filter, over a two-table join, over a `GROUP BY` aggregate -- under the outer query's own `WHERE`, `ORDER BY` and `LIMIT`; and, through three levels of view-over-view, the rows each level returns and the number of `view` rows in `sqlite_schema` |
| `s14-views-schema` | R14.7, R14.8, R14.9 | the `type`, `name`, `tbl_name` and `rootpage` of a view's schema row; the text stored in `sqlite_schema.sql`; which schema rows `DROP VIEW` removes and which it leaves, together with the base table's rows; and whether `ALTER TABLE … ADD COLUMN` on the base table changes either the stored text or the rows the view returns |
| `s14-views-diagnostics` | R14.6, R14.7 | that an `INSERT`, an `UPDATE` and a `DELETE` aimed at a view, and a `DROP TABLE` naming a view and a `DROP VIEW` naming a table, are each refused at exactly that point in the stream with no rows of their own; that the statements after each refusal still run; and that the base table's rows and the schema are unchanged afterwards, to this engine and to stock `sqlite3` reading the same file |
| `s14-views-interop` | R14.1, R14.8 | stock `sqlite3` opening a file this engine wrote: that it re-parses the stored view text as the same view, returns the rows the view defines, reports root page zero for it, and still answers `ok` to `PRAGMA integrity_check` |
| `s14-cdc-updates` | R14.10, R14.11 | the full column list of `turso_cdc`; the `typeof` and `hex` of its `updates` column after an `INSERT`, an `UPDATE` and a `DELETE`; and the capture rows a schema mutation produces, with their `change_type`, `table_name` and `id` |
| `s14-refusal-surface` | R01.16, inherited from step 01 | an invocation given a command-line argument the shell cannot use exits `2`, writes nothing at all to standard output and leaves no database file behind, while the same statement stream under a usable argument still runs and still answers |

## What this step re-checks from earlier work

The gate groups are not in the table above because they add no new requirement.
Beside `gate-proof-of-life`, which only starts the installed shell and asks it
`SELECT 1;`, this step's inherited checkpoint is thirteen cases: `gate-01`
through `gate-13`, one per earlier milestone, each re-run unchanged from the
step that introduced it. The gate table earlier in this file names the group,
the case file and what that case re-runs, one row each, so what each inherited
case asserts is readable here without opening any earlier step's documents.
Those fourteen groups are the whole of what this step re-checks from earlier
work -- there is no hidden inherited suite, no sampling, and no earlier
behaviour outside those cases is in scope here. If any of them fails, this
step is not satisfied whatever else passes, and `reward_details.json` names the
group and the case.

The inherited cases are frozen: their expected output was fixed when each was
introduced and are re-run
byte-identically here. So a change you make for this step that alters any
earlier behaviour shows up as a gate failure rather than as anything the
step's own groups measure.
Nothing in this step's own requirements asks you to change earlier behaviour;
views and the `updates` column are both additions.

## Views at this checkpoint

`CREATE VIEW` and `DROP VIEW` are the capability this step introduces, and the
facts below are what the verifier depends on. They were measured against this
step's checkpoint binary and against the next one on 2026-09-21.

- **Views are on unconditionally. No switch turns them on.** The manual
  shipped at this release, `spec/turso/manual.md`, still lists "No views"
  among its limitations; that line is stale at this tag, and R14.2 through
  R14.9 cover views. As with the compatibility matrix, where a shipped
  upstream document and this step's requirement list disagree, the requirement
  list wins: the document records what upstream wrote at the tag, and a
  candidate who read it as the statement of scope would leave most of this
  step unimplemented.
- **Indexes are on by default at this checkpoint, and nothing turns them on.**
  The compatibility matrix marks `CREATE INDEX` and `DROP INDEX` `Partial` with
  the note "Disabled by default", and the manual shipped at this release carries
  the same note under both commands; both are stale at this tag, which the
  release's own changelog records ("Enable indexes by default"). The `--help`
  probe above (under "Command-line surface the harness uses") looks for the
  experimental index switch, but a case in scope in this
  step creates an index with no option passed, so `CREATE INDEX` must work when
  the shell is invoked exactly as the command-line table describes -- the option
  is passed only if your own help text advertises it.
- **`--experimental-views` is not that switch.** The flag exists at this
  release, but the only thing it gates is `CREATE MATERIALIZED VIEW`, which is
  refused without it. Plain `CREATE VIEW`, `DROP VIEW` and selection from a
  view need no flag, and the harness never passes one -- every case here runs
  the shell exactly as `TEST_INTERFACE.md` describes.
- **A view has no storage.** Its `sqlite_schema` row carries `type` of `view`,
  the view's own name in both `name` and `tbl_name`, and `rootpage` of `0`,
  because no B-tree is allocated for it. `spec/sqlite/schematab.html` is the
  reference for the row's shape; the `0` rather than NULL is fixed here and is
  what the cases assert.
- **The non-zero root page beside it is fixed by the file format, not by this
  engine's choice.** A case that reads `rootpage` for a view reads it for the
  base table in the same result, so that number is checked too and you should
  know where it comes from. `spec/sqlite/fileformat2.html` puts the schema
  B-tree itself on page 1, so the first B-tree allocated in a fresh database
  is page 2, the next is page 3, and they are assigned in creation order. Both
  cases that assert a root page create exactly one table in a fresh file
  before the view, which is why both read back `2` for the table and `0` for
  the view. No case asserts a root page for an object it did not itself create
  in a fresh file, and none asserts a page number this rule does not fix.
- **Expansion happens at plan time, and it is transitive.** Selecting from a
  view must return exactly what its defining `SELECT` returns written inline,
  including when the outer query adds its own `WHERE`, `ORDER BY` and `LIMIT`,
  when the body is a join, and when the body aggregates -- a view over a
  `GROUP BY` keeps its own grouping. A view may be defined over another view;
  resolution follows the chain to the end, so a three-level chain returns what
  the fully expanded query returns and not what a single-level expansion
  would.
- **The stored text is the engine's re-print, not the source bytes.**
  `spec/PROFILE.md` rule 2 governs it and says so explicitly: it applies to
  every statement that writes a `sqlite_schema` row, and two of its worked
  examples are views. So `create view v as select id,tag from base where tag
  is not null` reads back as `CREATE VIEW v AS SELECT id, tag FROM base WHERE
  tag IS NOT NULL`. Where a case asserts a view's stored text, that rule is
  what fixes it.
- **DML aimed at a view is refused, and the two `DROP` forms are not
  interchangeable.** An `INSERT`, `UPDATE` or `DELETE` naming a view is
  refused and leaves the base table exactly as it was; `DROP TABLE` naming a
  view and `DROP VIEW` naming a table are each refused and leave the schema
  intact. In every case the refusal is an error report in the sense of
  `PROFILE.md` rules 6 and 7: it produces no row, the batch keeps running, and
  its wording is not compared.

## The CDC `updates` column

Change data capture arrived at the previous step. That step's
`TEST_INTERFACE.md` is not in this workspace -- this step's overlay replaced
it, as `WORKSPACE_HANDOFF.md` says -- so every part of it this step still
depends on is restated here rather than referred back to: capture is armed per
connection by `PRAGMA unstable_capture_data_changes_conn('<mode>[,<table>]')`,
`<mode>` is one of `off` (the default), `id`, `before`, `after` and `full`, and
the capture table is named `turso_cdc` unless the pragma's optional second
field names another. Two things change here, and both are in scope.

**The capture table gains an eighth column.** Its columns, in order, are now:

| # | Column | Type | Meaning |
|---|---|---|---|
| 0 | `change_id` | INTEGER | primary key, strictly increasing |
| 1 | `change_time` | INTEGER | Unix epoch seconds; no ordering is guaranteed and no test asserts its value |
| 2 | `change_type` | INTEGER | `1` INSERT, `0` UPDATE, `-1` DELETE |
| 3 | `table_name` | TEXT | the affected table |
| 4 | `id` | INTEGER | rowid of the affected row |
| 5 | `before` | BLOB | row state before, or NULL |
| 6 | `after` | BLOB | row state after, or NULL |
| 7 | `updates` | BLOB | the patch record described below for an UPDATE, NULL for an INSERT and for a DELETE |

`pragma_table_info('turso_cdc')` is read directly by a case, so the names and
the order are checked, not just the count. It is used in its table-valued form,
which `spec/sqlite/pragma.html` describes: `SELECT ... FROM
pragma_table_info('<name>')` yields one row per column of `<name>`, with the
result columns `cid`, `name`, `type`, `notnull`, `dflt_value` and `pk`, in
that order. The case selects `cid` and `name` and orders by `cid`, so the
column names and their positions are what it checks; the declared type, the
NOT NULL flag, the default and the primary-key flag of the capture table are
not checked and you may choose them.

**`updates` is filled under capture mode `full` and under no other mode.**
Measured at this checkpoint and the next: under `id`, `before` and `after` an
`UPDATE` still produces its capture row with `change_type` 0, but `updates`
reads NULL. The column is part of the table's shape in every mode -- which is
why the column-list assertion above does not depend on the mode -- and only
`full` populates it. Every case here that reads a patch record sets `full`
first.

**What a patch record is.** For a table of *n* columns, the `updates` value is
an ordinary SQLite record -- the same encoding a table row uses -- holding
**2n** values:

- first *n* integers, one per column in declaration order: `1` if the
  `UPDATE`'s `SET` list named that column, `0` if it did not;
- then *n* values: the column's **new** value where its flag is `1`, and NULL
  where its flag is `0`.

Worked example, and the one a case asserts. With
`CREATE TABLE t(id INTEGER PRIMARY KEY, a TEXT, b TEXT)`, the statement
`UPDATE t SET b='z' WHERE id=1` stores an `updates` blob whose `hex()` is
`0708080900000F7A`: header length `07`, then the six serial types
`08 08 09` -- integer 0, integer 0, integer 1, the three flags -- and
`00 00 0F` -- NULL, NULL, a one-byte text -- followed by the single body byte
`7A`, which is `z`.

Two consequences worth stating, both measured, and both asserted by the same
case as the example above:

- The flags record **what the statement named**, not what changed value. On
  the same table and the same row, `UPDATE t SET a=a` stores an `updates`
  blob whose `hex()` is `07080908000F0078`: header length `07`, then the
  three flags `08 09 08` -- integer 0, integer 1, integer 0, so `a` is
  flagged and `id` and `b` are not -- then `00 0F 00` for the values -- NULL,
  a one-byte text, NULL -- and the single body byte `78`, which is `x`. The
  row is unchanged by that statement; the flag is set because the `SET` list
  named the column.
- An `UPDATE` that moves a row's rowid is not captured as an update at all:
  it is captured as a delete followed by an insert, so neither capture row
  carries a patch record and both read `typeof(updates)` of `null`.

Both blobs are decoded here byte by byte deliberately. A case asserting a hex
constant it cannot derive would be pinning the reference implementation rather
than the contract; these two follow from the record encoding in
`spec/sqlite/fileformat2.html` together with the flag rule above, and they are
the only `updates` blobs any case in the corpus asserts.

**Schema changes are captured too.** From this checkpoint a statement that
writes a `sqlite_schema` row for a *table* or an *index* produces its own
capture row while capture is on, with `table_name` of `sqlite_schema` and `id`
the rowid of the schema row it wrote. A view's own schema row is not captured:
the reference emits no capture row for a `CREATE VIEW` or a `DROP VIEW`, and no
case at this step turns capture on around one, so both behaviours pass. `CREATE TABLE` and `CREATE INDEX` appear as inserts (`change_type` 1),
`ALTER TABLE … ADD COLUMN` as an update (`change_type` 0, carrying an
`updates` blob over the schema row's own columns), and `DROP TABLE` as a
delete (`change_type` -1). All four forms are asserted by one case. The schema
row's capture row is ordered ahead of any capture row for data in the object
it created. At the previous checkpoint the same script captured only the data
rows, which is what makes this gradable here.

**Which rowid a schema capture row names is fixed, not incidental.** The
pragma materialises the capture table at the point it is issued, so when the
pragma is the first statement of a batch `turso_cdc` takes the first
`sqlite_schema` rowid and every object created after it takes the next, in
creation order. A batch that turns capture on and then creates a table, an
index on it and a second table therefore produces schema capture rows with
`id` of 2, 3 and 4, and an `ALTER TABLE ... ADD COLUMN` on the first table
produces an update naming 2 again, because it rewrites that same schema row.
A case asserts exactly that sequence, so the numbers are readable here rather
than taken from a reference run.

**The capture table's own creation is not itself captured.** Creating
`turso_cdc` writes a `sqlite_schema` row like any other object, but no capture
row names it. The same case counts the rows with `table_name` of
`sqlite_schema` to pin that, so the absence is checked and not merely stated.

## What this milestone does not include

Seven things a reader might reasonably expect from this checkpoint -- five about
views, one about the capture table it also extends, and one about the rejection
paths -- are **not** in scope here.
Each absence was measured against this step's checkpoint binary and the next
one on 2026-09-21 rather than decided by argument; do not treat any of them as
a defect.

- **`ALTER TABLE … RENAME TO` on a table a view reads from.** This is why
  R14.9 names `ADD COLUMN` rather than `ALTER TABLE` in general. At this
  checkpoint and at the next one alike the statement does not fail -- it
  *panics*: the process terminates with `not yet implemented` and exit status
  `101`, so there is no result, no error report and no surviving connection
  for a case to observe. Nothing here checks it, and a build that refuses the
  statement cleanly instead is not penalised.
- **`ALTER TABLE … RENAME COLUMN` on a column a view projects.** The rename
  succeeds and the view's stored text is *not* rewritten, so the view is left
  naming a column that no longer exists; this engine and stock `sqlite3` then
  fail the view identically. The two readers agree, but what they agree on is
  a broken view, so no case pins it. `ALTER TABLE` does not rewrite views in
  either direction at this release: `ADD COLUMN` is the only form that leaves
  a view still resolving, and R14.9 is stated in those terms.
- **Materialized and incremental views.** `CREATE MATERIALIZED VIEW` is the
  only thing `--experimental-views` gates, the harness never passes the flag,
  and nothing here covers materialized views, their incremental maintenance,
  or the machinery behind them.
- **`IF NOT EXISTS`, an explicit view column list, and `TEMP` in the stored
  text.** All three are measured to be dropped from the re-print:
  `CREATE VIEW IF NOT EXISTS v …`, `CREATE VIEW v(a,b) AS …` and
  `CREATE TEMP VIEW v AS …` all read back as plain `CREATE VIEW v AS …`, the
  last of them from the main schema. `CREATE VIEW IF NOT EXISTS` on a name
  already taken is still refused rather than accepted. No case writes any of
  these forms and no row checks what the re-print does with them.
- **The wording of any refusal.** R14.6 and R14.7 assert that a refusal landed
  at that point in the stream, produced no rows of its own, and left the rest
  of the batch running. Every line carrying the shell's `×` marker is folded
  to the single word `ERROR` before comparison, exactly as the `error report`
  observable below describes, so no diagnostic text, error code or position
  within a message is compared. The refusals themselves do have a stock second
  opinion and it agrees -- stock `sqlite3` refuses the same five statements --
  but stock writes its diagnostic to standard error and stops the batch at the
  first error, so only the *state* the refusals leave behind is cross-read,
  never the report.
- **What a `DROP TABLE` captures beyond the dropped table's own schema row.**
  Dropping a table that carries an index removes both schema rows from
  `sqlite_schema`, but it is captured as a *single* delete naming the table's
  schema rowid: the index's schema row goes uncaptured, and the table's data
  rows produce no capture rows either. Both were measured at this checkpoint
  and the next and are identical at each, but they are this engine's choices
  rather than anything the documents fix, so the case that checks the delete
  drops a table that has neither an index nor a row, where the single delete
  capture row is unambiguous. A build that also captures the index row, or the data rows, is
  not penalised.
- **The exit status after a refusal, and the panic path.** The two refusal
  cases at this step check that a report landed at that point in the stream,
  that it produced no rows and that the batch continued; neither checks the
  process's own exit status, so `spec/PROFILE.md` rule 6's "the process still
  exits `0` after a refused statement" is still not measured here. Rule 8's
  process-level rejection -- an unusable command-line argument exits `2` with
  nothing on standard output and no database file behind it -- **is** checked,
  by `s14-refusal-surface`, which passes such an argument and then runs the
  same statement stream under a usable one.

## Behaviour contract for this step

Each row is checked through the observable named. Nothing outside the named
observable is looked at.

Two observables need their comparison spelled out. They are defined once for
the whole task, not once per step, so a step whose behaviour contract below
names neither of them still gets both definitions here. Read them as the
meaning the words carry wherever they are used -- in a contract row that names
one as its observable, and equally in the body of a `.sh` case that reduces an
error report or reads an exit status without any row naming it. The presence
of a definition here is not a claim that this step checks it.

**`error report`.** Refusals surface on standard output and do not stop the
batch (`spec/PROFILE.md`, rules 6 to 8). A case checked this way canonicalises
the captured standard output before comparing it, so that every reported
failure becomes the single token `ERROR` on its own line. Each case carries the
reduction it applies in its own body, and those bodies are out of bounds to you
— `instruction.md` puts `/tests` off limits — so nothing you have to do may
depend on which reduction a case chose, and nothing does: the obligation stated
below is the whole of it. Most cases checked this way use one of these two `sed`
folds:

```
sed '/^[[:space:]]*│/d; /^[[:space:]]*$/d; s/^.*×.*$/ERROR/'
sed '/^[[:space:]]*│/d; /^[[:space:]]*$/d; s/^.*×.*$/ERROR/; s/^Runtime error:.*$/ERROR/'
```

Not every case uses one of those two. A case that has to check something a fold
of that shape would erase reduces its output some other way, and the shapes
those reductions take vary from case to case. No count of them is claimed here,
and you cannot tell from this document which one any particular case used. Two
examples give the range. One checks **which** of two things
happened — the statement was taken, or it was refused — rather than the text of
either, by testing whether the statement produced any output at all: empty
means accepted, non-empty means refused. So an accepted statement must print
nothing, and a refused one must print something; nothing else about it is
looked at, not even how many lines its report occupies. Another is an `awk`
variant of the fold, used where a refusal has to be told apart from a row that
should never have come back: every row the case expects carries a leading tag,
only a run of untagged output becomes `ERROR`, and so a row returned where a
refusal was required reaches the comparison with its text intact and fails
there, rather than folding into the same `ERROR` as a correct refusal. Others
do not fold at all: they classify an answer by its shape — that a checkpoint
answered a well-formed `busy`/`frames`/`copied` triple, say — and a report
arriving at such a classifier is simply not the answer that was required, and
fails there.

Two things hold under every one of them, and together they are the whole of
what this costs you. **No expected output anywhere in this task contains the
wording of a report** — measured over every expected-output file in the corpus
— so no reduction is ever checking your phrasing. And wherever a reduction has
to tell a report apart from an answer, it does so by the `×` on the report's
first line. Put `×` there, keep continuations behind the gutter described
under **Wrapping** below, and the reduction a case happened to choose cannot
cost you anything.

**The recognition rule, stated exactly.** A line is treated as a report if it
carries `×` anywhere on it, or — under the second fold — if it begins with
the literal text `Runtime error:`. A line that does neither is not recognised:
it survives the fold verbatim, lands in the compared stream, and fails the
comparison. **So put `×` on the first line of every report your shell
writes.** That is `PROFILE.md` rule 6, it is recognised by both folds and by
every case in this task, and it is the only form you need.

Do not read the second rule as an alternative you may choose instead. It is
there for one measured reason: the reference engine reports a *runtime* error
— a constraint violation, say — as a bare `Runtime error: ...` line with no
marker on it, and only puts `×` in front of it from `v0.1.2` onward, while
a *statement-level* parse error -- a syntax error in the statement itself --
carries `×` at every release in the chain (a parse error raised *inside* a scalar
function is the narrower class `spec/PROFILE.md` measures, and that one can come
back bare). The cases that do
not apply the second rule would not recognise a `Runtime error:` line, so a
shell that reported that way would fail them. `×` on every report is the form
that always works.

This is the one place where rule 7's "no test compares the wording" has a
boundary, and it is worth being plain about it: the wording *after* the marker
is free — message, code, position, none of it is compared — but the line has
to be recognisable as a report before its wording can be thrown away.

**Wrapping.** If a report is too long for one line, begin every continuation
line with the `│` gutter marker (U+2502, bytes `\342\224\202`), with
nothing before it but spaces. The first rule of each fold drops such a line,
so a wrapped report canonicalises to the same single `ERROR` as an unwrapped
one. Your shell is free to wrap or not to wrap, and free to choose its width;
neither survives into what is compared. Nothing else may begin a line with
`│`, and no result row in this task does.

This one is an obligation on your shell rather than a description of the
reference, and the difference matters: the reference at `v0.0.13` wraps a
parse error into a drawn box whose lines begin `╭`, ` 1 │`, `·` and `╰`,
and the gutter rule drops none of them, because on the one line that has a
`│` it is not the first non-space character. No case in this corpus depends
on that — every diagnostic the reference emits for the statements these cases
contain is a single line, and no expected output in this task contains a
box-drawing character — but a shell that wraps and does not follow the gutter
rule will have its continuation lines compared as if they were output.

**What the fold does and does not recognise, measured against the reference.**
The reference reports a constraint violation as a bare `Runtime error: ...`
line: no `×`, no gutter, no blank line around it. Only the second fold turns
that line into `ERROR`; the first leaves it standing. The reference reports a
statement-level parse error with `×` on its headline at every release; the
scalar-function parse error `spec/PROFILE.md` measures is the exception. A shell whose refusals
begin `Error:` and carry neither `×` nor `Runtime error:` is not recognised by
either fold and fails every error-report row — that is the boundary of rule 7
stated in the next paragraph, and it is why the instruction above says `×` on
every report and nothing else.

**Blank lines.** The reference frames a report with blank lines at some
checkpoints and not at others, and does the same around result blocks. Every
fold drops blank lines for exactly that reason, so no blank line anywhere near
a report is part of the contract.

The `×` (U+00D7, bytes `\303\227`) and the `│` are byte patterns here,
not characters: the environment runs `LC_ALL=C`, so `sed` matches each one
literally and cannot fail on a sequence it does not recognise.

What the comparison then pins is that a report appeared **at exactly that
point in the stream**, that the refused statement produced **no rows**, that
the statements after it **still ran and still produced theirs**, and, where
the row says so, that a query issued afterwards shows the stored state
**unchanged**. The wording between the markers is discarded before anything is
compared, so it cannot pass or fail a case. Where a row says a statement is
rejected "with the busy diagnostic" or "with a specific diagnostic", the named
diagnostic identifies *which condition must be detected*, not any text.

**`exit status`.** Used only for failures that are not SQL-level, because only
those move it: an unusable command-line argument exits `2`, a panic exits
`101`. A row checked on exit status is a row about the process failing, or
about it conspicuously not panicking.

| # | Requirement | Observable |
|---|---|---|
| R14.1 | `CREATE VIEW` stores the view's text in `sqlite_schema` such that stock `sqlite3` re-parses it as the same view. | stock sqlite3 reading the file |
| R14.2 | Selecting from a view returns exactly the rows its defining `SELECT` returns when written inline. | result rows |
| R14.3 | A view over a join returns the same rows as the join written inline, in the same order. | result rows |
| R14.4 | A view over an aggregate returns the same rows as the aggregate written inline, including its `GROUP BY` grouping. | result rows |
| R14.5 | A view that references another view resolves transitively and returns the rows the fully expanded query returns. | result rows |
| R14.6 | An `INSERT`, `UPDATE` or `DELETE` targeting a view is rejected: the engine reports an error for the statement, produces no rows for it, execution continues with the next statement, and the view's underlying table is unchanged. The specific wording of the diagnostic is not compared. | error report |
| R14.7 | `DROP VIEW` removes the view, while `DROP TABLE` naming a view (or `DROP VIEW` naming a table) is rejected with an error report distinguishing the two cases: the engine reports an error, produces no rows, execution continues, and the schema remains unchanged. The specific wording is not compared. | error report |
| R14.8 | A view's `sqlite_schema` row carries a root page of zero, because a view allocates no B-tree. | database file bytes |
| R14.9 | An `ALTER TABLE … ADD COLUMN` on a table a view reads from leaves the view's stored text byte-for-byte unchanged — the text is not rewritten to mention the new column — and the view still resolves against the altered table, returning the columns it projects for rows inserted before and after the alteration. | result rows |
| R14.10 | The CDC table gains an eighth column, `updates`, at this checkpoint. It is a BLOB; it is populated only for an `UPDATE`, where it holds the patch record of the columns that update named, in the layout the step's `TEST_INTERFACE.md` fixes; and it is NULL for an `INSERT` and for a `DELETE`. It is filled under capture mode `full` and under no other mode; no case at this step reads it under another mode, so populating it under every mode also passes. | result rows |
| R14.11 | Schema mutations are captured as well as data at this checkpoint: with capture on, a statement that writes a `sqlite_schema` row produces its own capture row whose `table_name` is `sqlite_schema` and whose `id` is the rowid of that schema row, ordered ahead of any capture row for data in the object it created. | result rows |

## Out of scope

- The organisation of your source: crate names, module paths, file layout,
  public or private API, whether you keep the starter's two-crate split.
- The wording of any diagnostic, log line or help text.
- Anything written to standard error.
- Timing, memory use, allocation behaviour, or how many pages a statement
  reads or writes on its way to an answer.
- File size in bytes, free-page reuse order, and defragmentation timing --
  yours, within `spec/PROFILE.md`, provided the contents are correct and stock
  `sqlite3` accepts the file.
- Your own tests, benchmarks and scratch files.
- Anything under `/app/target/`, which never reaches the verifier.

The page *count* of a finished file is a separate matter. It is checked only at a
step whose behaviour contract above has a row that names it: where a requirement
fixes the cell distribution a workload produces -- SQLite's balance-siblings
rule, which is stated and checked at the one step that introduces it -- then
`PRAGMA page_count` after that workload is determined by the rule rather than by
your packing, and the requirement that states the rule is what makes it
checkable. Nothing checks the page count of a file no requirement describes, so
if no row above names it, page count is not in scope at this step either.
