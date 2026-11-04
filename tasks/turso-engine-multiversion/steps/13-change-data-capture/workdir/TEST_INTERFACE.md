# Test interface — step 13

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

### Requirement groups

| Group |
|---|
| `s13-cdc-capture` |
| `s13-cdc-interop` |
| `s13-cdc-modes` |
| `s13-cdc-table-shape` |
| `s13-refusal-surface` |

`s13-cdc-capture` and `s13-cdc-modes` cover the capability this step
introduces; `s13-cdc-interop`, `s13-cdc-table-shape` and `s13-refusal-surface`
cover breadth at this step. Nothing in scope here is satisfiable by an earlier
step's work alone.

The harness reads `tests/assets/groups.txt`, never this table; if the two ever
disagree, the file is what the harness follows.

This step's requirements are independent of every other step's.

## Which group covers which requirement

Every requirement group at this step exists for rows of the behaviour contract
below, and every row has a group. Nothing here is in scope that the table does
not name, with one exception, and it is the same exception at every step: the
process-level contract -- the exit-status rules under "Exit status" above --
is an inherited obligation rather than a row of this step's own table.
`s13-refusal-surface` is the group that observes it: `R01.16`, introduced and
stated at step 01, remains an obligation on your engine here, and it is not a
new requirement of this milestone.

| Group | Requirements it covers | What it observes |
|---|---|---|
| `s13-cdc-capture` | R13.1, R13.2, R13.3, R13.4, R13.5, R13.10 | the `change_type`, `table_name` and `id` of the capture row an INSERT, an UPDATE and a DELETE each produce, and how many there are -- including one row per *row* a multi-row statement mutates; which of the two images each mutation kind leaves, through `typeof()` and through the `hex()` of the image byte for byte; the order and the numbering of the capture rows one transaction produces, read back after `COMMIT`, and the same numbering carried across the boundary between two invocations of the shell; and, across a rolled-back transaction, both the rows that survive it, the id the next capture row receives, and whether the bytes of `main.db` and `main.db-wal` changed |
| `s13-cdc-modes` | R13.6, R13.11, R13.12 | whether a capture table is in `sqlite_schema` and how many capture rows exist in the default state, under `off`, and after a capturing mode is selected part way through a session -- including what an `off` issued *after* a capturing mode does: it stops capture without disturbing the table or its rows; which images `before`, `after` and `id` keep, through `typeof()` over both image columns; and, under `full,audit_log`, which table the captured rows arrive in and under which names the schema then holds the tables the script asked about |
| `s13-cdc-table-shape` | R13.8, R13.10, R13.13, R13.14 | the `cid` and `name` `pragma_table_info` reports for the capture table's leading seven columns, in order, and the `type`, `name` and `tbl_name` of its `sqlite_schema` row; and, across an `ALTER TABLE ... ADD COLUMN` followed later by an `ALTER TABLE ... DROP COLUMN`, the capture rows the INSERTs and the UPDATE between and after them produce, the `change_id` each receives, the column width their images are encoded at, the `change_id` and `table_name` of each `ALTER`'s own capture row -- selected by `table_name = 'sqlite_schema'` rather than inferred from the gaps -- and the total row count, which counts both of those |
| `s13-cdc-interop` | R13.7, R13.9 | stock `sqlite3` opening a file this engine wrote with capture on: what `PRAGMA integrity_check` answers, which of the two tables it finds in the schema, the capture rows it selects, and what `typeof()` over the time column reports |
| `s13-refusal-surface` | R01.16, inherited from step 01 | an invocation given a command-line argument the shell cannot use exits `2`, writes nothing at all to standard output and leaves no database file behind, while the same statement stream under a usable argument still runs and still answers |

## What this step re-checks from earlier work

The gate groups are not in the table above because they add no new requirement.
Beside `gate-proof-of-life`, which only starts the installed shell and asks it
`SELECT 1;`, this step's inherited checkpoint is twelve cases: `gate-01`
through `gate-12`, one per earlier milestone, each re-run unchanged from the
step that introduced it. The gate table earlier in this file names the group,
the case file and what that case re-runs, one row each, so what each inherited
case asserts is readable here without opening any earlier step's documents.
Those thirteen groups are the whole of what this step re-checks from earlier
work -- there is no hidden inherited suite, no sampling beyond those cases,
and no earlier behaviour outside them is in scope here. A gate group adds no
new requirement and can only veto: if any one of them fails, this step is not
satisfied whatever else passes, and `reward_details.json` names the group and
the case.

The inherited cases are frozen: their statements, their normalisation and
their expected output were fixed when each was introduced and are re-run
byte-identically here. So a change you make for this step that alters any
earlier behaviour shows up as a gate failure rather than as anything the
step's own groups measure.

## Change data capture

The release notes and `README.md` shipped at this release announce change data
capture but do not specify it, so the contract the verifier depends on is
stated here in full. Every fact below was measured against this step's
checkpoint binary on 2026-09-21; none of it is inferred from the step's name.

**Enabling it**, per connection:

```sql
PRAGMA unstable_capture_data_changes_conn('<mode>[,<table>]');
```

- `<mode>` is one of `off`, `id`, `before`, `after`, `full`.
  - `off` -- capture disabled. **This is the default**: a connection that never
    issues the pragma writes no capture rows and creates no capture table.
  - `id` -- record only the rowid of the affected row.
  - `before` -- additionally record the row's state before an UPDATE or DELETE.
  - `after` -- additionally record the row's state after an INSERT or UPDATE.
  - `full` -- record both.
- `<table>` is optional and names the capture table. Omitted, the capture
  table is `turso_cdc`.
- **The pragma produces no result row of its own.** It is a setting, not a
  query: a case that issues it sees nothing between the statement before it
  and the statement after it.
- **It applies from the next statement onwards and is never retroactive.**
  Mutations issued before a capturing mode was selected are not captured after
  the fact, and capture rows already written are not rewritten when the mode
  changes. One session may therefore select two modes in turn and check both.
- **The capture table is created on demand**, when a capturing mode is first
  selected -- so in the default state, and under an `off` issued before any
  capturing mode has been selected, it is absent from
  `sqlite_schema` entirely, not merely empty. Whether it appears at the moment
  the mode is selected or at the first mutation that follows is not observable
  here, and either is conforming.
- **Selecting a mode for which the capture table already exists reuses that
  table.** Arming capture a second time -- later in the same session, or in a
  later invocation of the shell against the same file -- does not drop it,
  recreate it or renumber it. The rows already in it are left alone and
  `change_id` carries on from the highest already there rather than restarting
  at 1, so the numbering in the table is continuous across the boundary
  between two invocations. A case arms, writes two rows, exits, and in a
  second invocation arms again before updating and deleting, so that the ids
  it reads back -- 3 and 4, beside the 1 and 2 committed earlier -- are the
  observable that separates a table numbered from its own rows from one
  recreated or restarted at the boundary.
- **Selecting `off` after a capturing mode has been in use stops capture and
  leaves the table and its rows in place.** `off` is a setting, not a schema
  statement and not a drop: a mutation issued after it writes no capture row,
  the table is still there to be selected from with its rows untouched, and an
  arming later in the same session or in a later invocation continues from
  them. A case reads all three: the count before the `off`, the count after a
  mutation that follows it, and the capture table's own `sqlite_schema` row.
  Issuing the pragma when the table is already there
  writes nothing at all: an invocation that arms capture and then only reads
  leaves `main.db` and `main.db-wal` byte-identical, which is what
  `spec/PROFILE.md`'s durability rule requires of a connection that changes
  nothing. A case depends on both halves of this: it hashes the two files,
  then runs a whole second invocation that re-arms capture before rolling a
  transaction back, and compares the hashes across it -- and it asserts that both files are still *there* as well as
  unchanged: `spec/PROFILE.md`'s durability rule has the engine leave the log on
  disk rather than unlink it, so an invocation that wrote and rolled back must
  leave `main.db-wal` present, not absent (two absent readings would compare
  equal to each other).

**The capture table** is an ordinary table on disk -- stock `sqlite3` opening
the database can select from it, and `PRAGMA integrity_check` still answers
`ok` -- whose leading columns are, in this order:

| # | Column | Type | Meaning |
|---|---|---|---|
| 0 | `change_id` | INTEGER | primary key; starts at 1 and increases by 1 per capture row |
| 1 | `change_time` | INTEGER | Unix epoch seconds. **An integer, not text** -- `typeof()` over it is `integer`. No ordering is guaranteed, and no case asserts its value, only its type |
| 2 | `change_type` | INTEGER | `1` INSERT, `0` UPDATE, `-1` DELETE |
| 3 | `table_name` | TEXT | the affected table |
| 4 | `id` | INTEGER -- declared, and **not asserted**: no case reads the declared *type* of this column (the step's cases do read `pragma_table_info` for the column names), so either a declared type or none passes | rowid of the affected row |
| 5 | `before` | BLOB | row state before, or NULL |
| 6 | `after` | BLOB | row state after, or NULL |

A later checkpoint appends a further column, so cases here read
`pragma_table_info` with `cid <= 6` and assert only the leading seven. The
names and the order are checked, not just the count.

**Transactional behaviour.** Capture rows are written in the same transaction
as the change that produced them.

- A `ROLLBACK` discards the capture rows along with the change, leaves the
  capture rows committed before the transaction untouched, and leaves
  `main.db` and `main.db-wal` byte-identical to their pre-transaction bytes.
  A case checks the bytes as well as the rows, because a query cannot see a
  page the rollback failed to drop. It also consumes none of the numbering:
  `change_id` increases by one per capture row, and a discarded row never
  became one, so the next capture row written after the rollback takes the id
  after the last committed one and the ids stay gapless across it.
- After a `COMMIT` the capture rows of that transaction are ordered by
  `change_id` in the order the mutations were issued -- not in key order, and
  not in the order the rows happen to sit in the table -- with consecutive ids
  and no gap, and all of them are there: one capture row per row the
  transaction mutated, and none belonging to anything else, so their count is
  the number of rows the transaction mutated.
- **What the `COMMIT` fixes is what a later reader sees, not what the writing
  connection sees.** Inside the connection that issued the mutations each
  capture row is already selectable before the `COMMIT`, exactly as an
  uncommitted user-table row is to its own writer; that was measured here, not
  assumed. So "written in the same transaction as its mutation" is a claim
  about durability and about rollback, not a claim that a capture row is
  hidden from the connection that produced it. No case at this step reads the
  capture table part way through a transaction and nothing here checks that
  moment. What is checked is the pair of end states: after a `COMMIT` all of
  the transaction's capture rows are present and no others are, and after a
  `ROLLBACK` none of them is.

## The bytes of a `before` or `after` image

An image is an ordinary SQLite **record**, the same encoding a table row uses,
over the captured table's columns *as they stand at the moment of the
mutation*. `spec/sqlite/fileformat2.html` section 2.1 is the reference: a
varint header size, then one serial-type varint per column, then the bodies in
the same order. Serial type `0` is NULL, `1`-`6` are integers of 1, 2, 3, 4, 6
and 8 bytes, `8` and `9` are the integers 0 and 1 held in the header with no
body at all, an even type `N >= 12` is a blob of `(N-12)/2` bytes, and an odd
type `N >= 13` is text of `(N-13)/2` bytes. An integer takes the smallest of
those widths that holds it, and the constants `0` and `1` are held in the header
by themselves.

**One place the image deliberately differs from an on-disk table row.**
Section 2.3 of that same document says that when a table has an
`INTEGER PRIMARY KEY` -- a rowid alias -- that column appears in the table's
own record as a NULL, because the value is already the rowid. In a capture
image it does **not**: the column carries its own value, so the image can be
read on its own without the rowid beside it.

Worked examples, all measured, over
`CREATE TABLE t(id INTEGER PRIMARY KEY, v TEXT)`:

- `INSERT INTO t VALUES(1,'ab')` gives an `after` image whose `hex()` is
  `0309116162`: header size `03`, serial types `09` (the integer 1, no body)
  and `11` = 17 (text of `(17-13)/2` = 2 bytes), then the body `6162`, `ab`.
  Had `id` been stored the way an on-disk row stores a rowid alias, the first
  serial type would have been `00`.
- After `ALTER TABLE t ADD COLUMN extra INTEGER`, an
  `INSERT INTO t VALUES(2,'b',99)` gives `04010F01026263`: header size `04`,
  serial types `01` (a one-byte integer), `0F` = 15 (text of 1 byte) and `01`,
  then the bodies `02`, `62` and `63` -- 2, `b` and 99. Three serial types,
  because the image is encoded against the column set the table has now.

An INSERT leaves `before` NULL, a DELETE leaves `after` NULL and an UPDATE
carries both; under mode `before` and mode `after` the unwanted image is NULL
too, and under `id` both are.

## Which statements produce a capture row

Every `INSERT`, `UPDATE` and `DELETE` against a table produces exactly one
capture row per row it mutates, naming that table in `table_name`.

Schema statements are the narrow case, and only one form of them is captured
at this checkpoint. It was measured rather than assumed, because a case
depends on it: `ALTER TABLE ... ADD COLUMN` and `ALTER TABLE ... DROP COLUMN`
each produce **one** capture row whose `table_name` is `sqlite_schema`, so an
`ALTER` consumes a `change_id` and the ids a user table's own mutations
receive are not consecutive across one. `CREATE TABLE`, `DROP TABLE`,
`ALTER TABLE ... RENAME TO` and `ALTER TABLE ... RENAME COLUMN` produce no
capture row here. The general rule -- every statement that writes a
`sqlite_schema` row is captured -- arrives at the next checkpoint, not at this
one.

## What this milestone does not include

- **The `updates` column.** The capture table gains an eighth column, holding
  the patch record of an `UPDATE`, at the next checkpoint. Cases here read
  only `cid <= 6`, so an implementation that already has that column is not
  failed for it and one that does not is not failed either.
- **Capture of `CREATE TABLE`, `DROP TABLE` and `CREATE INDEX`.** See the
  paragraph above: that is the next checkpoint's work, and the tolerance here
  is symmetric, as it is for the `updates` column. Every case at this step
  creates the tables it needs before it arms the pragma, and none of them
  drops a table or creates an index once capture is on, so an implementation
  that already captures those three statements is not failed for it and one
  that does not is not failed either. One creation is outside that tolerance in
  both directions, because the counts the cases assert depend on it: the
  capture table's own `CREATE TABLE`, issued by the engine inside the arming
  statement, is not captured. A capture row for it would change the count every
  counting case asserts.
- **The value of `change_time`.** It is a wall clock. Only its type is
  asserted. The reason the type is asserted at all is this engine's own
  release history rather than anything you built at an earlier step: one
  release back the column was named `operation_time`, was declared `INTEGER`
  and held text, so stock `sqlite3` answered
  `TEXT value in turso_cdc.operation_time` to `PRAGMA integrity_check`
  instead of `ok`. No earlier step of this task asks for a capture table under
  any name, so there is nothing of your own to migrate here; R13.9 is just the
  requirement that the column you create now hold an integer.
- **Any ordering guarantee between connections or between transactions.** The
  ordering rule stated above is within one transaction.
- **Refusals, and the process-level rejection.** No expected output at this step
  folds a refusal to the `ERROR` token and no case carries an `.exit` sidecar, so
  an engine that silently accepted a malformed pragma value would not be
  distinguished here. `spec/PROFILE.md` rules 6 to 8 remain obligations on your
  engine all the same, and the process-level half of rule 8 -- an unusable
  command-line argument exits `2` with nothing on standard output and no
  database file behind it -- is checked at this step by `s13-refusal-surface`,
  which also runs the same statement stream under a usable argument.

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
| R13.1 | With capture enabled, an `INSERT` appends exactly one capture row whose `change_type` is `1`, whose `table_name` is the mutated table and whose `id` is the mutated row's rowid; its `after` column holds an image and its `before` column is NULL. | result rows |
| R13.2 | With capture enabled, an `UPDATE` appends one capture row whose `change_type` is `0` and which holds both a `before` and an `after` image. | result rows |
| R13.3 | With capture enabled, a `DELETE` appends one capture row whose `change_type` is `-1`, which holds a `before` image and whose `after` column is NULL. | result rows |
| R13.4 | Within one transaction the capture rows are ordered by `change_id` in the order the mutations were issued, and the ids are consecutive from 1 with no gap (true of every case here, each of which starts from an empty capture table; where a re-armed table already holds rows, the continuation rule stated in `TEST_INTERFACE.md` governs instead); after the `COMMIT` every one of that transaction's capture rows is present and no others are, so their count is the number of rows the transaction mutated. | result rows |
| R13.5 | A capture row is written in the same transaction as its mutation, so a `ROLLBACK` leaves no capture row for it, leaves rows committed before the transaction untouched, and leaves `main.db` and `main.db-wal` byte-identical to their pre-transaction bytes. | database file bytes |
| R13.6 | Capture is off until `PRAGMA unstable_capture_data_changes_conn` selects a capturing mode: in the default state and under mode `off` no capture table appears in `sqlite_schema` and no mutation produces a capture row, and selecting a mode does not retroactively capture mutations issued before it. | result rows |
| R13.7 | Stock `sqlite3` opening a capture-enabled database reads the capture table as an ordinary table — it appears in `sqlite_schema` and its rows select — and `PRAGMA integrity_check` returns `ok`. | stock sqlite3 reading the file |
| R13.8 | A mutation performed after an `ALTER TABLE` on the captured table is still captured, and its image is encoded against the altered column set. | result rows |
| R13.9 | The capture table's time column is declared `INTEGER` and holds an integer, so `typeof()` over it reports `integer` and stock `sqlite3`'s `PRAGMA integrity_check` answers `ok` rather than reporting a type mismatch on that column. | stock sqlite3 reading the file |
| R13.10 | A `before` or `after` image is the SQLite record encoding of that row over the captured table's current columns, as `spec/sqlite/fileformat2.html` §2.1 defines it, with an `INTEGER PRIMARY KEY` column carrying its own value rather than the serial type 0 an on-disk table row stores for a rowid alias. | result rows |
| R13.11 | The pragma value may be given as `<mode>,<name>`, in which case the capture table is created under that name and the default name is not used. | result rows |
| R13.12 | Mode `before` records only the pre-image, mode `after` only the post-image and mode `id` neither, each still appending exactly one capture row per mutation; a mode selected part way through a session applies from the next statement onwards and does not alter rows already captured. | result rows |
| R13.13 | The capture table is an ordinary table whose leading seven columns are, in order, `change_id`, `change_time`, `change_type`, `table_name`, `id`, `before`, `after`, as `pragma_table_info` reports them. | result rows |
| R13.14 | `ALTER TABLE ... ADD COLUMN` and `ALTER TABLE ... DROP COLUMN` on a captured table each append one capture row whose `table_name` is `sqlite_schema`, so the capture ids a user table's own mutations receive are not consecutive across an `ALTER`. | result rows |

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
