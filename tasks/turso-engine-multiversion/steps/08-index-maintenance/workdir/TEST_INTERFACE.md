# Test interface — step 08

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
statements in scope are the non-comment lines; a comment in a case file names the
requirement it covers and is not itself in scope.

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

### Requirement groups

| Group |
|---|
| `s08-descending-index` |
| `s08-index-maintenance` |
| `s08-pragma-surface` |
| `s08-refusal-surface` |
| `s08-schema-mutation` |

`s08-descending-index`, `s08-index-maintenance` and `s08-schema-mutation`
cover the capability this step introduces; `s08-pragma-surface` and
`s08-refusal-surface` cover breadth at this step. Nothing in scope here is
satisfiable by an earlier step's work alone.

The harness reads `tests/assets/groups.txt`, never this table; if the two ever
disagree, the file is what the harness follows.

This step's requirements are independent of every other step's.

## Which group covers which requirement

Every requirement group at this step exists for a row of the behaviour contract
below, and every row has a group. Nothing here is in scope that the table does
not name, with one exception, and it is the same exception at every step:
the **process-level contract** -- the exit-status rules under "Exit status"
above -- is an inherited obligation rather than a row of this step's own
table. `s08-refusal-surface` is the group that observes it, together with the
inherited statement-level half of the same obligation. Both halves were
introduced and are stated at step 01 and remain obligations on your engine
here; neither is a new requirement of this milestone.

| Group | Requirements it covers | What it observes |
|---|---|---|
| `s08-schema-mutation` | R08.1, R08.2, R08.9 | reads of `sqlite_schema` taken either side of a `DROP INDEX`, so the row's disappearance is observed rather than inferred from the statement not erroring; the same three-column query run once with the index in place and once after it is gone, row for row; and `ALTER TABLE ... RENAME TO` followed by the new name in `sqlite_schema`, the rows read back under it, and stock `sqlite3` re-opening the file and finding the same table |
| `s08-descending-index` | R08.3, R08.7 | a range scan, a `LIMIT 1` and a full scan over a `b DESC` index, each `ORDER BY b DESC`, beside predicates below every stored key and above every stored key; **stock `sqlite3`'s `PRAGMA integrity_check` over the file your engine wrote**, which is what observes the order the index keys are *stored* in rather than the order the rows came back in; and the same statements replayed through stock `sqlite3` on its own database |
| `s08-index-maintenance` | R08.6 | an index-driven lookup of two keys a `DELETE` removed and one it did not, the surviving row count, that the index object itself is still listed in `sqlite_schema`, and stock `sqlite3`'s `PRAGMA integrity_check` and its own lookups over the resulting file |
| `s08-pragma-surface` | R08.11, R08.12 | what `PRAGMA page_size` and `PRAGMA schema_version` answer for a database stock `sqlite3` created, each beside stock `sqlite3`'s own reading of the same header field, and beside a query of a table in the same invocation. **Those databases are deliberately not built with default settings.** In the page-size fixture stock creates the file at a page size of **2048** and leaves the schema cookie at **1**; in the schema-version fixture the page size stays at stock's default **4096** and stock makes five schema changes -- three `CREATE TABLE`s, a `CREATE INDEX` and a `DROP TABLE` -- so the cookie it leaves behind is **5**. Neither number is one your engine could supply from a constant -- 2048 is not the 4096 `spec/PROFILE.md` rule 1 fixes for a database your engine *creates*, and 5 is not the 1 a freshly made file carries. Both lines have to come from the page-1 header of the file that was opened, at offsets 16 and 40, which is what the two rows say |
| `s08-refusal-surface` | R01.13 and R01.16, inherited from step 01 | `DROP INDEX` for a name that is not in `sqlite_schema` is refused as a report on standard output -- the batch carries on and the statements after it still answer -- while `DROP INDEX IF EXISTS` on the same name is a silent no-op; and an invocation given a command-line argument the shell cannot use exits `2`, writes nothing at all to standard output and leaves no database file behind, while the same statement stream with a usable argument still runs and still answers |

Stated plainly, and not left to be inferred from the step's name, the four
capabilities this step adds are: **`DROP INDEX`** -- the statement does not
parse at all before this checkpoint -- with the schema row removed and every
query that could have used the index still answering from the table scan that
replaces it; **descending indexes**, so `CREATE INDEX ... (b DESC)` stores its
keys in descending order -- which is checked both as the order the rows come
back in and, through stock `sqlite3`'s `PRAGMA integrity_check`, as the order
the keys are stored in; **index upkeep on `DELETE`**, so a deleted row's index
entries go with it, the index object itself stays, and stock `sqlite3` finds no
orphan; and **`ALTER TABLE ... RENAME TO`**, the one `ALTER TABLE` form this
checkpoint implements. The two pragmas,
`page_size` and `schema_version`, are breadth: they first answer at this
checkpoint, and they are the `peripheral` group.

The third `peripheral` group, `s08-refusal-surface`, adds no capability: it
observes the two inherited obligations -- a statement the engine cannot
execute is refused rather than silently ignored, and a command-line argument
the shell cannot use is a process-level failure. Nothing else at this step
provokes either, so without it an engine that silently accepted a `DROP INDEX`
of a name that is not there, and created a database after an argument it could
not use, would look exactly like one that refuses both.

Index upkeep on `UPDATE`, `xBestIndex`, and a *rejected* `ALTER TABLE` form
are **not** in that list. Each was in an earlier draft of this contract and
each was removed against a measurement; see "What this milestone does not
include" below.

## The four `COMPAT.md` rows this step overrides

`spec/turso/COMPAT.md` is the upstream compatibility matrix **as it stood at
the release you are starting from**, not a description of what you are being
asked to build. `instruction.md` says the requirements win where the two
disagree. This step is one of the places they disagree the most, so the rows
are named here rather than left for you to trip over: four of them are marked
`No` for capabilities this step requires.

| `COMPAT.md` row | What the matrix says | What this step requires anyway |
|---|---|---|
| `ALTER TABLE`, under "Statements" | `No` | R08.9 -- `ALTER TABLE ... RENAME TO` runs, and the new name is visible in `sqlite_schema` and to stock `sqlite3` re-opening the file. Only `RENAME TO`; the other three forms really are absent, and "What this milestone does not include" says so |
| `DROP INDEX`, under "Statements" | `No` | R08.1 and R08.2 -- the statement runs, the index's `sqlite_schema` row goes, and every query that could have used the index still answers row for row |
| `PRAGMA page_size` | `No` | R08.11 -- the pragma answers with the page size in the header of the file that was opened |
| `PRAGMA schema_version` | `No` | R08.12 -- the pragma answers with the schema cookie in the header of the file that was opened |

Read a `No` in that matrix as "the release this workspace was cut from did not
have it", which is exactly why it is this step's work. Nothing else in the
matrix contradicts a requirement stated here: `CREATE INDEX` and `DELETE` are
both already `Yes`, and the `DESC` and index-upkeep behaviour this step adds
sits underneath those two rows rather than beside them.

## Access paths are not an observable at this step

R08.2, R08.3 and R08.7 are the three rows about indexes, and it is worth being
exact about what each one can and cannot see, because an earlier wording of all
three named a *mechanism* -- "via a table scan", "without a sort", "an index
seek" -- that nothing here checks.

Nothing your shell prints on standard output says which access path answered a
query. Two engines that reach the same rows by different routes emit
byte-identical output, so no case here can distinguish them, and none tries to.
The one surface that would show the path is `EXPLAIN` / `EXPLAIN QUERY PLAN`,
and that is not checked anywhere in this task: this engine renders the plan in
its own form, that form changes inside the release chain, and it matches neither
the previous checkpoint nor stock `sqlite3`. Checking it would pin one
implementation's bytecode rather than a contract. **So: plan freely. Use an
index, or do not; sort, or do not. No case at this step looks, and choosing
differently from the reference costs nothing.**

What that leaves in scope, row by row:

- **R08.2** -- that the result set after the `DROP INDEX` is the same set of
  rows, in the same order, as the result set before it. Not how it was
  produced.
- **R08.7** -- that a predicate lying entirely below every stored key, and one
  lying entirely above every stored key, each return *no* rows. What this
  catches is an off-end seek clamped to the first or last stored entry, which
  would return one row instead of none. Not whether an index seek happened.
- **R08.3** -- both halves, and this one is not purely about result rows.
  That a `DESC`-ordered query returns descending values is checked from the
  rows. That the index *stores* its keys descending is checked separately and
  by a different observer: stock `sqlite3` running `PRAGMA integrity_check`
  over the file your engine wrote walks each index against its table using the
  key order the schema text declares, so an index b-tree holding ascending keys
  under a `b DESC` schema row is reported as rows missing from the index rather
  than `ok`. `s08-descending-index` prints that verdict on a line of its own.
  This is the one place where "stores its keys in descending order" is more
  than a restatement of "the rows came out descending", and it is why R08.3's
  observable names stock `sqlite3` as well as result rows.

## Your engine must modify a database stock `sqlite3` wrote

This is the first step where that is required, and four of this step's cases
depend on it, so it is stated rather than left to be discovered by failing.

At earlier steps stock `sqlite3` appears as a *reader* of files your engine
wrote, or as a second opinion on a query answer. Here it also appears as the
**writer that goes first**: a case runs stock `sqlite3` to create `main.db`,
populate a table and build an index, and then hands that file to your shell,
which drops the index, deletes rows from it, or renames the table. Your engine
has to open that file and write to it.

The file it is handed is not one of your own. Stock `sqlite3` creates a
database in its default rollback-journal mode, so page 1's read and write
version bytes at offsets 18 and 19 are `1 1`, not the `2 2` that
`spec/PROFILE.md` rule 1 fixes for a database *you* create. Rule 1 governs
creation; it does not license refusing, or silently re-formatting, a file that
arrives in the older mode.

What was measured at this checkpoint, and what the cases therefore assume: the
reference engine opens such a file, applies the change, leaves offsets 18 and
19 at `1 1`, and creates a `main.db-wal` sidecar beside it. Stock `sqlite3`
then re-opens the modified file, answers `PRAGMA integrity_check` with `ok`,
and sees the change. Nothing here checks the journal-mode bytes themselves --
no case asserts `1 1` or `2 2` for a stock-created file -- and nothing checks
whether the sidecar exists. What is in scope is that the write lands and that
stock `sqlite3` can still read the file afterwards.

`spec/PROFILE.md` opens by requiring stock `sqlite3` to open, read and query
every **unencrypted** database this engine writes; the obligation stated here
is that same interoperability read in the other direction, and it is likewise
about unencrypted databases -- which is every database at this step.

## What this milestone does not include

Five things a reader might reasonably expect from a step about index
maintenance are **out of scope** here. Each was probed against this checkpoint
(`v0.0.20`) on 2026-09-21, and the measurement, not a judgement, is the
reason -- where the measurement is of the checkpoint's behaviour on a workload,
and where it is of the checkpoint's own handling of a statement, the bullet
says so. None is a defect in your build and none can regress here.

- **Index upkeep on `UPDATE`.** After `CREATE INDEX i_b ON t(b)` and `UPDATE t
  SET b=99 WHERE a=2`, this checkpoint answers `WHERE b=99` with **0 rows**
  and `WHERE b=20` -- the key that was replaced -- with **1 row**: the index
  keeps the stale entry and never gains the new one. `v0.0.19` answers
  identically, so a case here would not even distinguish the two checkpoints.
  `v0.0.21` loses the `UPDATE` entirely (`Internal error: expected record`),
  and the first checkpoint that answers `new:1 old:0` is `v0.0.22`. A case
  asserting the correct behaviour would fail here; a case asserting what
  happens here would freeze a defect and fail from `v0.0.22` on. Index upkeep
  on **`DELETE`** is a different matter -- it is correct at this checkpoint,
  wrong at the one before, and it is R08.6, which is in scope.
- **`xBestIndex` and virtual-table query plans.** No virtual-table module is
  registered at this checkpoint. `CREATE VIRTUAL TABLE v USING <m>` is
  answered `Parse error: no such module: <m>` for `generate_series`,
  `pragma_table_info`, `fts5` and `json_each`, and selecting from
  `generate_series(1,3)` directly is answered `Extension error: Virtual table
  module not found: generate_series`. `EXPLAIN` itself works and prints a
  bytecode listing, but there is no virtual table for a plan to consult, so
  `xBestIndex` is never reached and nothing about it is observable. Step 04's
  contract records the same measurement at `v0.0.16`; the first checkpoint
  that returns rows from a table-valued function is step 11's, which is where
  that group lives.
- **A rejected `ALTER TABLE` form.** The three forms this checkpoint does not
  implement -- `ADD COLUMN`, `RENAME COLUMN` and `DROP COLUMN` -- do not
  produce a refusal. Each aborts the process: `thread 'main' panicked at
  core/translate/mod.rs:114:22: not yet implemented`, exit status **101**.
  That is a crash, not an `error report` in the sense of `spec/PROFILE.md`
  rules 6 to 8, so there is nothing for a negative case to recognise, and the
  corpus generator declines to freeze an unimplemented-feature panic as
  expected output. The rest of `ALTER TABLE` arrives at step 10, and that is
  where rejected forms are checked. Nothing at this step requires your engine
  to panic -- if you implement `ADD COLUMN` early, no case here notices.
- **Where a dropped index's pages go.** R08.1 is checked on the `sqlite_schema`
  row alone. Returning the freed pages to the freelist is *not* asserted, here
  or anywhere at this step: after the reference engine drops an index, page
  1's `freelist_count` reads **0** and `PRAGMA page_count` is unchanged, at
  `v0.0.20` and at every later checkpoint through `v0.1.5`, while stock
  `sqlite3` performing the same `DROP INDEX` leaves `freelist_count` **1** --
  and stock's `integrity_check` on the engine's file reports the stranded page
  as `never used`. Asserting the freelist would pin that defect. Page
  reclamation is yours to get right or not; no case at this step looks.
- **An index attached to a renamed table.** `spec/sqlite/lang_altertable.html`
  says that indices attached to a table remain attached to it when the table is
  renamed. Nothing at this step checks that, and the reason is what this
  checkpoint's rename actually does: it rewrites **every** `sqlite_schema` row
  whose `tbl_name` is the old table name, so an index row's `name` becomes the
  table's new name and its stored `sql` becomes that table's own text. A case
  that renamed an indexed table and had stock `sqlite3` re-resolve the index
  would fail against this checkpoint, and one that pinned what this checkpoint
  does instead would freeze one release's behaviour as the milestone's
  contract. The case that does check the rename renames a table that carries no
  index, where the new name is unambiguous, and its own comment records that
  the stored `sql` text is not asserted.

## The two rejections this step's corpus does provoke

The step's own negative space is its unimplemented `ALTER TABLE` forms, and
those abort rather than report -- see the bullet above. The rejections that
*are* obligations under `spec/PROFILE.md` rules 6 to 8 are provoked in exactly
two places here, both in `s08-refusal-surface`:

- **A statement-level refusal.** `DROP INDEX` naming an index that is not in
  `sqlite_schema` is refused: the shell reports it on standard output, the
  batch carries on, and the statements after it still answer. The same
  statement spelled `DROP INDEX IF EXISTS` is not a refusal at all -- it is a
  no-op with no report -- and both halves are asserted side by side, so an
  engine that accepts the first, or refuses the second, fails the case. The
  wording of the report is out of scope; the case reduces every report to the
  token `ERROR` exactly as "How output is compared" describes.
- **The process-level rejection of rule 8.** An invocation given a command-line
  argument the shell cannot use exits `2`, writes nothing at all to standard
  output, and leaves no database file behind, while the same statement stream
  with a usable argument still runs and still answers. Both halves are on one
  labelled line, and no expected output text depends on the diagnostic, which
  goes to standard error.

Two things stay outside even those cases:

- No case carries an `.exit` sidecar, so the `err` branch of the exit-status
  contract is still the one side of it this step does not exercise: the
  process-level rejection above is folded to a line on standard output rather
  than compared as a status.
- The three `ALTER TABLE` forms this checkpoint does not implement are not
  refusals to be recognised either: they panic, which is why the generator
  declines to freeze them.

What the corpus asserts besides those two is the state a wrong implementation
would lose: row counts pinned from
both sides, `PRAGMA integrity_check` over the file, `sqlite_schema` rows read
either side of a `DROP INDEX` or a rename, and stock `sqlite3` re-reading the
result. The two rejections above are the whole of the refusal surface this
step measures; a refusal outside them -- an `ALTER TABLE` form, a malformed
pragma value, anything else -- is not distinguished by any case here.

## The inherited checkpoint is exactly these eight cases

The gate groups are not in the requirement table because they add no new requirement.
This step inherits one end-to-end case from each of the seven earlier
milestones, plus proof of life, and **there is no hidden inherited suite
beyond them**. The "Gate groups" table above names each one with the
behaviour it re-runs; in one line each:

- `gate-proof-of-life` -- the installed shell starts and answers `SELECT 1;`.
- `gate-01` -- step 01: `LIKE` patterns with `%` and `_` in every position,
  through `WHERE`, `ORDER BY` and `count()`.
- `gate-02` -- step 02: the JSON function family over integer, real, NULL,
  text and JSON-literal arguments.
- `gate-03` -- step 03: single-row `DELETE` from the middle, front and end of
  a table, each followed by `changes()` and the surviving rows.
- `gate-04` -- step 04: `CAST` of text to `INTEGER`, `REAL` and `NUMERIC`
  across whitespace, trailing junk, a truncated fraction and an exponent.
- `gate-05` -- step 05: the hex `jsonb()` produces for each element kind.
- `gate-06` -- step 06: `UPDATE` changing the storage class an untyped column
  holds, with `typeof()` following the new value.
- `gate-07` -- step 07: a column omitted from an `INSERT` column list taking
  its declared `DEFAULT`.

No other behaviour from steps 01 to 07 is in scope here. If any of the eight
fails, this step is not satisfied and `reward_details.json` names the group and
the case. Note what that means for the new work: `DROP INDEX` and `DELETE`
must not disturb step 03's `DELETE` semantics or step 06's `UPDATE`
semantics, and `ALTER TABLE ... RENAME TO` must not disturb step 07's column
defaults -- those three are re-run against your current shell, not against
the shell you had when you passed them.

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
| R08.1 | `DROP INDEX` removes the index's row from `sqlite_schema`, so a read of `sqlite_schema` taken after the statement no longer lists the index that a read taken before it did list. | result rows |
| R08.2 | After `DROP INDEX`, every query that could have used the index returns the identical result set, row for row and in the same order, that it returned while the index existed. Which access path answers it afterwards is out of scope; `TEST_INTERFACE.md` says why, under "Access paths are not an observable at this step". | result rows |
| R08.3 | A `DESC` index stores its keys in descending order: a scan of it yields values in descending order, **and** stock `sqlite3` running `PRAGMA integrity_check` over the resulting file walks the index against its table in the order the schema text declares and reports `ok`. | result rows, and stock sqlite3 reading the file |
| R08.6 | A `DELETE` removes the row's index entries — an index-driven lookup of a deleted key finds nothing, the index object itself remains in `sqlite_schema`, and stock `sqlite3` finds no orphan index entry when it runs `PRAGMA integrity_check` on the resulting file. | stock sqlite3 reading the file |
| R08.7 | A predicate selecting only keys below every stored key, and one selecting only keys above every stored key, each return no rows rather than the nearest stored entry. Which access path answers them is out of scope; `TEST_INTERFACE.md` says why, under "Access paths are not an observable at this step". | result rows |
| R08.9 | `ALTER TABLE … RENAME TO` — the one `ALTER TABLE` form this checkpoint implements — runs, and the new table name is visible both in a subsequent query of `sqlite_schema` and to stock `sqlite3` re-opening the file. | result rows |
| R08.11 | `PRAGMA page_size` returns the page size recorded in the database header of the file that was opened, which is the same value stock `sqlite3` reads from that file. | result rows |
| R08.12 | `PRAGMA schema_version` returns the schema cookie stored in the page-1 header of the file that was opened, which is the same value stock `sqlite3` reads from that file. | result rows |

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
