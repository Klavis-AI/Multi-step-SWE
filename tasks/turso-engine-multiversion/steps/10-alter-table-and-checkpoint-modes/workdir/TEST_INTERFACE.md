# Test interface — step 10

The step's contract with the verifier. Every statement here is what the
harness does, not a paraphrase of it. If the verifier needs a name, a path, a
format or an exit status, it is here or in a document named by
`instruction.md`.

## The artefact

| | |
|---|---|
| Path | `/app/bin/limbo` |
| Kind | a regular file, executable, **not a symlink** |
| Built from | your workspace, by your own build |

The verifier never builds anything. If that path is missing, is a symlink, or
is not executable when checking starts, no requirement group can pass and
`reward_details.json` says why. `/app/target/` and `/app/.git/` are excluded
from the artefact handed to the verifier, so **installing is not optional** —
build output alone does not cross the boundary.

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
cd <empty dir> && /app/bin/limbo -m list -q main.db < case.sql
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
| `CLI` | `/app/bin/limbo` |
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

### Requirement groups

| Group |
|---|
| `s10-alter-table-columns` |
| `s10-alter-table-diagnostics` |
| `s10-alter-table-rename` |
| `s10-checkpoint-modes` |
| `s10-default-cache-size` |
| `s10-hex-function` |
| `s10-pragma-integrity-check` |

`s10-alter-table-columns`, `s10-alter-table-diagnostics`,
`s10-alter-table-rename`, `s10-checkpoint-modes`, `s10-hex-function` and
`s10-pragma-integrity-check` cover the capability this step introduces;
`s10-default-cache-size` covers breadth at this step. Nothing in scope here is
satisfiable by an earlier step's work alone.

The harness reads `tests/assets/groups.txt`, never this table; if the two ever
disagree, the file is what the harness follows.

This step's requirements are independent of every other step's.

## Which group covers which requirement

Every requirement group at this step exists for rows of the behaviour contract
below, and every row has a group. Nothing here is in scope that the table does
not name. Read this table rather than the step's directory name: the name says
"alter table and checkpoint modes" and three of the seven groups are about
neither.

| Group | Requirements it covers | What it observes |
|---|---|---|
| `s10-alter-table-rename` | R10.1, R10.2 | the `sqlite_schema` row -- `type`, `name`, `tbl_name` and `sql` -- after `RENAME TO`; the `sql` text and `pragma_table_info` shape after `RENAME COLUMN`; the rows read back through the new names; and, through stock `sqlite3` re-opening the same file, the re-parsed schema, the re-parsed `CREATE INDEX` text, which column the index is keyed on, a lookup by the new column name, `PRAGMA integrity_check` and the rows |
| `s10-alter-table-columns` | R10.3, R10.4, R10.5 | what the rows already on disk read in a column added with a `DEFAULT`, in one added without, and in one added `NOT NULL` without a default; the order `pragma_table_info` reports after two successive adds; `dflt_value` for the added column, which reports the default's **literal source text with its quotes intact** -- a column added `DEFAULT 'none'` reports `'none'`, not `none`, and a column with no default reports the empty string; the narrowed rows and shape after `DROP COLUMN`; and, through stock `sqlite3`, the same statements run on its own file, and the narrowed file re-parsed, re-read and integrity-checked |
| `s10-alter-table-diagnostics` | R10.6 | that `DROP COLUMN` naming a column that does not exist, and `DROP COLUMN` naming the table's `INTEGER PRIMARY KEY`, are each refused: the refusal lands at that point in the stream, produces no rows, the batch carries on, and `pragma_table_info` and the rows afterwards are what they were before -- with stock `sqlite3` refusing the same two statements and leaving the same shape |
| `s10-checkpoint-modes` | R10.7 | the shape of the `PRAGMA wal_checkpoint(PASSIVE)` answer -- three integer fields, busy `0`, a frame count **greater than zero**, and copied equal to frames, so that a triple of three zeroes is not a passing answer -- and that the answer is identical for `PASSIVE`, `passive` and `PaSsIvE` -- the three asked back to back in one batch over one WAL with no write between them, so passing requires that a `PASSIVE` checkpoint does not restart the log and the repeat therefore reports the same non-zero counts rather than zeroes; that an unknown mode name is refused; and that `FULL`, `RESTART` and `TRUNCATE` are each answered without stopping the batch, with the rows intact afterwards |
| `s10-hex-function` | R10.8 | the string `hex()` returns for blob literals and for blobs read back from a column, at `00`, `7F`, `80` and `FF`, beside the string stock `sqlite3` returns for the same statements |
| `s10-pragma-integrity-check` | R10.9 | that `PRAGMA integrity_check` parses and answers `ok` on a clean database, with stock `sqlite3` answering `ok` about the same file |
| `s10-default-cache-size` | R10.10 | bytes 48..51 of the page-1 header, read out of the file with `od`, and what `PRAGMA cache_size` answers about the same database |

## What this step re-checks from earlier work

The gate groups are not in the table above, because they add no new requirement
and can only veto. There are **ten** of them and they are the whole of this step's
inherited obligation. They are enumerated here, in full, so that you can tell
what is being re-run from this step's own documents alone -- you do not need
any earlier step's `instruction.md` or `TEST_INTERFACE.md`, which this step's
editions have replaced:

- `gate-proof-of-life` starts the installed shell and asks it `SELECT 1;`.
- `gate-01` is one step-01 case, re-run unchanged: five rows inserted, then
  returned through `WHERE`, `ORDER BY` and `count()` under `LIKE` patterns
  with `%` and `_` in every position.
- `gate-02` is one step-02 case, re-run unchanged: `json_object`,
  `json_array`, `json`, `json_type`, `json_valid`, `json_extract` and
  `json_array_length` over integer, real, `NULL`, text and JSON-literal
  arguments.
- `gate-03` is one step-03 case, re-run unchanged: single-row `DELETE` from
  the middle, the front and the end of a table, each followed by `changes()`,
  the surviving rows, and aggregates over them.
- `gate-04` is one step-04 case, re-run unchanged: `CAST` of text to
  `INTEGER`, `REAL` and `NUMERIC` -- leading whitespace, trailing junk, a
  truncated fraction, an exponent, an empty string and a non-numeric string.
- `gate-05` is one step-05 case, re-run unchanged: the hex `jsonb()` produces
  for each element kind -- null, true, false, integers of three sizes, reals,
  text, empty array, empty object, and nested arrays and objects.
- `gate-06` is one step-06 case, re-run unchanged: `UPDATE` changing the
  storage class held in an untyped column, including to and from `NULL`, with
  `typeof()` following the new value.
- `gate-07` is one step-07 case, re-run unchanged: a column omitted from the
  `INSERT` column list taking its declared `DEFAULT`, a column with no
  `DEFAULT` taking `NULL`, and an explicit value overriding both.
- `gate-08` is one step-08 case, re-run unchanged: `DROP INDEX` removes the
  index row from `sqlite_schema`, and the same query still answers correctly
  from the table scan that replaces it.
- `gate-09` is one step-09 case, re-run unchanged: `COLLATE NOCASE` in an
  `ORDER BY` and in an equality test, beside the byte-ordered `BINARY`
  default, over a table stock `sqlite3` wrote.

Those ten groups are the whole of what this step re-checks from earlier work.
There is no hidden inherited suite, and no earlier behaviour outside those ten
cases is in scope here *as an inherited requirement*. That is not the same as
saying earlier behaviour is unobserved. The cases in scope above are written in
ordinary SQL and lean on inherited surface as the instrument that measures the
new behaviour, so a regression in any of it surfaces here as an *in-scope* case
failing on its own terms rather than as a gate veto. That instrument set is
enumerated rather than summarised, because you should not have to guess what a
case in scope may use. The list was re-derived on 2026-09-21 by reading every
statement all eighteen cases in scope send **this engine**, and it is complete:

- statements: `CREATE TABLE` -- with an `INTEGER PRIMARY KEY`, with declared
  column types, and with a column given **no** declared type at all
  (`CREATE TABLE hdr(a)`) -- `CREATE INDEX`, `INSERT ... VALUES` in both the
  single-row and the multi-row form, `SELECT`, `ALTER TABLE`;
- result columns: named columns, expressions over them, and `SELECT *`; also
  a `SELECT` with no `FROM` clause at all, as in `SELECT length(x'80FF');`;
- clauses: `WHERE`, and `ORDER BY` over a named column and over the `rowid`
  pseudo-column (`ORDER BY rowid`);
- predicates: equality against a literal, and `IS NULL` / `IS NOT NULL`;
- aggregates and scalars: `count(*)`, `count()`, `sum()`, `length()`,
  `typeof()`, `hex()`;
- literals: integer, text and blob literals -- `1`, `'none'`, `x'80FF'`;
- comments: `--` to end of line, within the SQL fed on standard input;
- schema access: `sqlite_schema` read as an ordinary table, for its `type`,
  `name`, `tbl_name` and `sql` columns;
- pragmas: `PRAGMA cache_size`, `PRAGMA integrity_check`,
  `PRAGMA wal_checkpoint`, and `table_info` read through its table-valued
  form `pragma_table_info('t')`. No case in scope asks this engine
  `PRAGMA journal_mode=WAL`: WAL is a fixed choice of `spec/PROFILE.md`, so
  the cases assume it rather than setting it, and the only occurrences of
  that pragma in this step are on the stock `sqlite3` fixture's own file;
- and, outside SQL, the shell's `-m list -q` invocation, its exit status, and
  the `--help` / `--experimental-indexes` option probe from the command-line
  table above.

**Nothing outside that list is asked of this engine by a case in scope, and that
is a property of the cases, not a summary of them.** In particular no case in
scope asks this engine for `char()` or for any other function the list does not
name. The gate groups are the exception and are not what the list bounds: each
is one end-to-end case from an earlier step, re-run unchanged, and between them
they ask this engine for `DELETE`, `UPDATE` with `coalesce()` and `CAST`, `||`,
`min()`, `max()` and `changes()`, `LIKE`, the `json*` and `jsonb()` families,
`COLLATE NOCASE` and `DROP INDEX`. The list
bounds what the cases send **this engine**; the statements they send the stock
`sqlite3` fixture are stock's problem and are not bounded by it -- one of them,
in `rename-column-sqlite3-reparse.sh`, reads `pragma_index_info('idx_b')`, and
nothing in this step ever asks this engine for that. A case in scope that leaned
on a function no step's requirement establishes would fail an engine that had
implemented every documented requirement correctly, which is the one thing this
verifier must never do; one such assertion existed here and was removed in
round 3.

If any gate group fails, this step is not satisfied whatever else passes, and
`reward_details.json` names the group and the case that vetoed it.

## Three in-scope capabilities the step's name does not predict

The directory is called `10-alter-table-and-checkpoint-modes`, and three of
the seven requirement groups are about neither `ALTER TABLE` nor checkpoint
modes. They are in scope all the same, they are stated here and in the requirement
list, and each is a defect this checkpoint corrects:

- **`hex()` on bytes at or above `0x80`** (R10.8). Earlier checkpoints render
  such a byte as the UTF-8 replacement sequence `EFBFBD` instead of its own
  two hex digits, so `hex(x'80FF')` answers `EFBFBDEFBFBD` rather than
  `80FF`. Here it answers the correct two uppercase digits per byte, for a
  blob literal and for a blob read back out of a column alike, and
  `length(x'80FF')` is `2`.
- **`PRAGMA integrity_check`** (R10.9). Earlier checkpoints refuse the name
  outright (`Not a valid pragma name`). Here it parses, executes, and answers
  the single word `ok` for a clean database.
- **The default page-cache size in the page-1 header** (R10.10). SQLite's own
  default for header bytes 48..51 is the big-endian **signed** value `-2000`
  -- the bytes `ff ff f8 30` -- which means "2000 KiB of cache", not "2000
  pages". The two previous checkpoints wrote `500` there (`00 00 01 f4`), a
  positive value that tells every later reader to size its cache by page
  count instead. This checkpoint writes `-2000`, and `PRAGMA cache_size`
  answers `-2000` to match. Both readings are in scope: the raw bytes taken out
  of the file with `od`, and the pragma's answer. The note that page-1
  offset 48 is "corrected later in the ladder" means here: it is a field the
  earlier checkpoints' documents left to the writer, and this step's requirement
  is the one that fixes it.

## Where this step and stock `sqlite3` deliberately disagree

`spec/PROFILE.md` makes stock `sqlite3` the second reader of the files this
engine writes. Counted over this step's eighteen cases in scope on 2026-09-21:
**seventeen** of them invoke the pinned stock binary inside their own compared
output. Six hand stock the `main.db` **this engine wrote** and check what stock
reads back out of it; the other eleven run the same statements on a separate
`stock.db` of stock's own making and compare the two answers side by side. The
one case with no stock call is `add-column-null-fill.sh`, an in-scope case in
the `s10-alter-table-columns` group rather than a gate case; its own comment
names the three siblings in scope that carry the cross-read for the same
requirement. Two of the ten gate cases do run stock
(`gate-08` and `gate-09`), and both run it only *ahead* of the engine, to build
the fixture; `gate-09` reads back through stock afterwards, but its engine block
is four `SELECT`s and writes nothing. So no gate case anywhere in this step has
stock read a file this engine wrote.

Three places are exceptions, and each is checked as stated here rather than as
stock renders it:

- **An unknown checkpoint mode name is refused.** Stock `sqlite3` 3.50.2 does
  not refuse `PRAGMA wal_checkpoint(NOSUCHMODE)`: it treats an unrecognised
  mode as `PASSIVE` and answers a triple. R10.7 requires the refusal, which is
  what this engine does and is the stricter reading of SQLite's own
  documentation. The case records stock's acceptance beside the engine's
  refusal so the divergence is visible rather than assumed away.
- **`ADD COLUMN … NOT NULL` without a default is accepted.** Stock `sqlite3`
  refuses the statement (`Cannot add a NOT NULL column with default value
  NULL`). This engine accepts it, and the rows already on disk then read NULL
  in the new column. R10.4 covers the acceptance and that NULL read-back. The
  file the statement leaves behind is still ordinary SQLite and stock
  `sqlite3` reads the same rows out of it, which the case checks.
- **The `sql` text `sqlite_schema` holds after a rename.** This engine renders
  it under `spec/PROFILE.md` rule 2; stock renders it its own way -- it quotes
  the renamed identifier (`CREATE TABLE "staff"(...)`) after `RENAME TO`, and
  after `RENAME COLUMN` it preserves the original statement's spacing
  (`CREATE TABLE t(a INTEGER, label TEXT)`). The two cannot agree on that text
  by construction, so the engine's `sql` column is checked against rule 2 while
  the stock half of `rename-table-schema-and-rows` and
  `rename-column-schema-and-rows` leaves the `sql` column out of its query
  altogether. What is compared across the two engines there is `type`, `name`,
  `tbl_name`, the `pragma_table_info` shape, the rows and the count. That the
  rewritten text is *re-parsable* is asserted separately, by the two
  `-sqlite3-reparse` cases, which hand stock the file this engine wrote.

Everywhere else in this step, a value the engine prints is a value stock
`sqlite3` prints for the same statements, and the cases that can make that
comparison make it in their own compared output. Those three exceptions are the
whole of the list: no other value in any `.out` differs between the two.

## The schema text `ALTER TABLE` rebuilds

`spec/PROFILE.md` rule 2 fixes the rendering of `sqlite_schema.sql`: one space
before the opening parenthesis, one after each comma, and no space just inside
either parenthesis. `RENAME TO` and `RENAME COLUMN` rewrite the stored text in
place and keep that rendering, so the rename cases read the `sql` column back
and check it.

`ADD COLUMN` and `DROP COLUMN` rebuild the text rather than editing it, and at
this checkpoint the rebuilt text carries a space just inside each parenthesis
-- `CREATE TABLE t ( a INTEGER, b TEXT )` -- contrary to rule 2. That is
corrected later in this release chain and is checked there, at step 12 under
its R12.6. **No case at this step reads the `sql` text back after an
`ADD COLUMN` or a `DROP COLUMN`**, and nothing here requires the spacing to be
either right or wrong: what the two column cases check is the column list
`pragma_table_info` reports, the records, and what stock `sqlite3` reads out
of the file.

## What this milestone does not include

Five things a reader might reasonably expect from a step called "alter table
and checkpoint modes" are **not** in scope here. Each absence was measured
against the release chain on 2026-09-21 rather than decided by argument; do
not treat any of them as a defect.

- **`DROP COLUMN` on a column an index depends on.** At this checkpoint the
  statement is neither refused nor performed: the shell prints
  `Internal error: Column b is in index idx_b but not found in table t` --
  a line the `error report` fold below does not recognise, because it carries
  no `×` marker -- the schema still declares the dropped column, and the rows
  read back with that column blanked. There is no answer a correct engine
  could give that a case could pin, so R10.6 covers the unknown-column and
  primary-key refusals only, and no case creates an index and then drops its
  column.
- **Enforcing `NOT NULL` on a column added without a default.** R10.4 requires
  that the statement is accepted and that pre-existing rows read NULL. It does
  **not** check what happens to a *later* `INSERT` that leaves the column
  NULL: this checkpoint permits it, later checkpoints in the chain refuse it,
  and `pragma_table_info`'s `notnull` flag flips from `0` to `1` at the same
  point. No case inserts a NULL into such a column or reads that flag.
- **`FULL`, `RESTART` and `TRUNCATE` performing their modes.** R10.7 requires
  that each is *handled* -- answered, with the batch continuing and the rows
  intact afterwards -- and nothing more. At this checkpoint the engine reports
  them as unsupported and performs only `PASSIVE`; later checkpoints perform
  them and answer a triple. Both satisfy R10.7, and the case folds the two to
  the same token so that passing here does not require leaving them
  unimplemented. What separates this checkpoint from the previous one is that
  there a mode argument aborts the process altogether.
- **The exact value of the frame and page counts in a checkpoint triple.**
  The triple is checked by shape -- three integer fields, busy `0`, a frame
  count greater than zero, and copied equal to frames -- and by being
  identical across the three spellings of `PASSIVE` for the same WAL state.
  What is *not* checked is the number itself: how many frames a checkpoint
  moves depends on how many pages the engine wrote on its way to the answer,
  which the "Out of scope" list below excludes, so no case pins it to a value.
  Note which side of that line the positivity sits on. A checkpoint taken
  after committed writes has at least one frame to move, so `0|0|0` -- which
  satisfies "busy is `0`" and "copied equals frames" -- is a **failing**
  answer here, and an implementation that always answers three zeroes does
  not pass this group. That is R10.7's boundary, and it is the one numeric
  fact about the triple this step fixes.
- **Any index over a renamed column being *used*.** The rename case hands the
  file to stock `sqlite3` and checks that the rewritten `CREATE INDEX` text
  re-parses, that the index is keyed on the new name, and that a lookup by the
  new name returns the right row. It does not check which plan either engine
  chooses, and no case here runs `EXPLAIN`.

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
| R10.1 | `ALTER TABLE … RENAME TO` rewrites the stored `sql` text so stock `sqlite3` re-parses the identical schema under the new name. | stock sqlite3 reading the file |
| R10.2 | `ALTER TABLE … RENAME COLUMN` rewrites every reference to the column in the stored schema text, including the definitions of indexes on that column, so stock `sqlite3` re-parses them all. | stock sqlite3 reading the file |
| R10.3 | `ALTER TABLE … ADD COLUMN` extends the table without rewriting the rows already on disk: with a `DEFAULT` those short records read the default value, without one they read NULL, and a second `ADD COLUMN` appends after the first, so `pragma_table_info` reports the columns in the order they were added and a row inserted afterwards lines up with them. `pragma_table_info` reports the added column's `dflt_value` as the default's **literal source text**, exactly as it was written in the `ADD COLUMN` clause and including any quotes: a column added `DEFAULT 'none'` reports `'none'`, with the single quotes present, not `none`. A column with no default reports the empty string. | result rows |
| R10.4 | `ALTER TABLE … ADD COLUMN … NOT NULL` without a default is accepted — where stock `sqlite3` refuses it — and the rows already on disk read NULL in the new column. | result rows |
| R10.5 | `ALTER TABLE … DROP COLUMN` removes the column from the schema text and from every row's record. | stock sqlite3 reading the file |
| R10.6 | `ALTER TABLE … DROP COLUMN` is refused both when the named column does not exist and when it is the table's `INTEGER PRIMARY KEY`. In each case the statement produces no rows, the batch carries on, and the schema and the rows are left as they were. | error report |
| R10.7 | `PRAGMA wal_checkpoint(<MODE>)` accepts a mode argument, where before it accepted only the bare form. `PASSIVE` — in any mixture of upper and lower case — answers a well-formed busy/frames/copied triple in which busy is `0`, the frame count is **greater than zero** — a checkpoint taken after committed writes has at least one frame to move, so a triple of three zeroes is not a passing answer — and the copied count equals the frame count, and the same WAL state answers the same triple for every spelling. The three spellings are asked back to back inside one batch of one session, over one WAL, with no write between them, and that is the fixture the requirement is measured on: a `PASSIVE` checkpoint does **not** restart the log, so the second and the third report the same non-zero frame and copied counts as the first rather than falling to zero. An engine that resets its WAL on `PASSIVE` answers one well-formed triple and then two of zeroes, and does not satisfy this. An unknown mode name is refused. `FULL`, `RESTART` and `TRUNCATE` are each handled: the engine answers the statement, the batch carries on, and the rows are intact afterwards. | result rows |
| R10.8 | `hex()` on a blob containing bytes `>= 0x80` returns their correct two-digit uppercase encoding, where earlier checkpoints corrupted them to `EFBFBD`. | result rows |
| R10.9 | `PRAGMA integrity_check` parses and executes, returning `ok` for a clean database, where earlier checkpoints rejected it as `Not a valid pragma name`. | result rows |
| R10.10 | A database this engine creates carries SQLite's own default page-cache size in page-1 header bytes 48..51: big-endian signed `-2000`, that is the bytes `ff ff f8 30`, where the two previous checkpoints wrote `500` (`00 00 01 f4`). `PRAGMA cache_size` reports the same `-2000`. | database file bytes |

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
