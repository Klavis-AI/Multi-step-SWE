# Test interface — step 07

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

### Requirement groups

| Group |
|---|
| `s07-column-defaults` |
| `s07-index-file-interop` |
| `s07-index-scan-results` |
| `s07-scalar-surface` |

`s07-column-defaults` and `s07-index-scan-results` cover the capability this
step introduces; `s07-index-file-interop` and `s07-scalar-surface` cover
breadth at this step. Nothing in scope here is satisfiable by an earlier
step's work alone.

The harness reads `tests/assets/groups.txt`, never this table; if the two ever
disagree, the file is what the harness follows.

This step's requirements are independent of every other step's.

## Which group covers which requirement

Every requirement group at this step exists for a row of the behaviour contract
below, and every row has a group. Nothing here is in scope that the table does
not name, and nothing the table does not name is in scope here.

A group is satisfied only when every one of its cases passes; the cases in a
group are not separate requirements of their own.

| Group | Requirements it covers | What it observes |
|---|---|---|
| `s07-index-scan-results` | R07.3, R07.5, R07.11, R07.12 | the same query run over an indexed table and over an unindexed copy of the same rows, tagged line by line so a disagreement shows up as a differing line; `ORDER BY ... DESC` over an INTEGER key and over a TEXT key, bounded, unbounded and under `LIMIT`, beside `min()` and `max()` over the same ranges; `>`, `>=`, `<` and `=` against a TEXT leading key; and `count()`, `sum()`, `group_concat()`, `min()` and `max()` over an index range, each beside stock `sqlite3`'s answer to the identical aggregate over the same file |
| `s07-column-defaults` | R07.14, R07.15 | four `INSERT`s that between them omit a defaulted column, supply it, omit a column that has no `DEFAULT`, and give every column explicitly, read back row by row and then counted; and one row of a six-column table whose five `DEFAULT`s are an unsigned integer, a real, a string literal, a signed integer and a parenthesised constant, each read back beside `typeof()` |
| `s07-index-file-interop` | R07.13 | a database stock `sqlite3` created and indexed, queried through the engine, and then re-opened by stock `sqlite3`, which reports `PRAGMA integrity_check`, the surviving index row in `sqlite_schema`, and its own answer to the query the engine just answered |
| `s07-scalar-surface` | R07.16, R07.17, R07.18 | hexadecimal literals stored, compared, added and passed through `typeof()`; `likely()` and `likelihood()` as bare expressions and wrapped around a predicate whose unwrapped row count is asserted beside them; and `julianday()` with day, month, year, `start of ...` and `weekday` modifiers and chains of them, plus `timediff()` over positive, negative, zero, sub-minute, multi-hour and whole-day intervals, all of them inside a single month |

Stated plainly, and not left to be inferred from the step's directory name,
the capabilities this step adds are: **reading index B-trees** -- range
predicates, equality seeks and aggregates resolved through an index return
exactly the rows the range describes, where the previous checkpoint resolves
the bounds against the wrong key and folds an extra row into every total;
**backwards index scans**, so `ORDER BY <indexed column> DESC` hands back
descending rows, which the previous checkpoint gets wrong in ORDER while
getting right in SET; **column `DEFAULT` expressions**, so a column left out
of an `INSERT` takes its declared constant and a column without one takes
NULL; and, as the `peripheral` breadth of the step, **hexadecimal integer
literals**, the **`likely()`/`likelihood()`** planner hints, and
**`julianday()` modifiers together with `timediff()`**, none of which parse at
the previous checkpoint.

`CREATE INDEX` issued to your own engine, `STRICT` tables, `WITHOUT ROWID`
tables and an injectable clock are **not** in that list, although the release
this milestone tracks contains the first three. Each was removed against a
measurement; see "What this milestone does not include" below. The index
B-trees the cases above read are built by stock `sqlite3` before your shell is
invoked, so none of them requires your engine to create one.

## The database these cases hand you is one stock `sqlite3` wrote

Five of this step's ten cases in scope -- all four of `s07-index-scan-results`
and the one in `s07-index-file-interop` -- begin by having stock `sqlite3`
create `main.db`, fill it and build
the index, and only then run your shell against that file. What that file is
is therefore part of what those cases assume, so it is stated here rather than
left to be discovered:

- **It is in rollback-journal mode, not WAL.** Stock 3.50.2 creates a database
  whose `PRAGMA journal_mode` is `delete` and whose page-1 read and write
  version bytes at offsets 18 and 19 are both `1`. Measured on 2026-09-21 with
  the same pinned binary the harness exports as `$SQLITE3`.
- **`spec/PROFILE.md` rule 1 does not describe it.** Rule 1 fixes the header of
  a database **your engine creates** -- WAL, version bytes `2`, 4096-byte
  pages -- and it now says so in terms. Read as a description of every file in
  this task it would be wrong, and an engine that refuses to open a database
  whose header does not match its own profile loses half of this step.
- **Its page size is 4096 and its text encoding UTF-8**, which is what stock
  writes by default, so nothing here asks you to read a page geometry you
  would not have written yourself.

Your engine only reads these files: no case at this step asks it to write to a
database stock `sqlite3` created, so nothing here fixes what your engine may
do to such a file's journal mode if you do write to one.

## What this milestone does not include

Four things a reader might reasonably expect from a step whose directory is
named `07-index-creation` are **out of scope** here. Each was probed on
2026-09-21 against the whole release chain, not only against this checkpoint
(`v0.0.19`), and the measurement, not a judgement, is the reason. None is a
defect in your build, and implementing any of them costs you nothing here.

- **`CREATE INDEX` performed by your engine, and an `ORDER BY` planned without
  a sorter.** The reference engine does build a usable index at this
  checkpoint: stock `sqlite3` reading the file afterwards answers `PRAGMA
  integrity_check` with `ok` and finds the index row, and `EXPLAIN` of an
  `ORDER BY` over the indexed column emits an index `OpenRead` and `Rewind`
  with no sorter opcode. Three later checkpoints in the chain disable the
  statement by default and ignore an index that already exists, planning the
  same `ORDER BY` through `SorterOpen`/`SorterInsert`/`SorterSort` instead. A
  case that asserted either behaviour would be correct here and wrong there,
  and the corpus generator only accepts a case whose compared output every later
  checkpoint reproduces. So nothing at this step asserts that your engine can
  create an index -- but every index-reading case does assume you can read one
  that stock `sqlite3` built, which is the direction that is stable across the
  whole chain. Implement `CREATE INDEX` if you wish; no case here looks.
- **`STRICT` tables.** `spec/sqlite/stricttables.html` is shipped because
  `STRICT` is part of the release this milestone tracks, and the reference
  engine does enforce it here: a `TEXT` value written to an `INT` column of a
  `STRICT` table is refused and the row does not land, where the previous
  checkpoint stores it. A later checkpoint in the chain puts `STRICT` behind an
  experimental flag and refuses every `STRICT` declaration without it, so the
  same forward-agreement rule applies and no case here checks it. A `STRICT`
  declaration whose column type is outside the permitted set fails a second,
  independent test: the previous checkpoint rejects it identically, so it is
  not a capability this step adds, and the refusal is rendered as a multi-line
  drawn box carrying the offending statement text, which neither
  canonicalisation this package documents removes.
- **`WITHOUT ROWID` tables.** `CREATE TABLE ... WITHOUT ROWID` is refused at
  this checkpoint and at every later one, and here the failed statement leaves
  the file in a state the next statement reports as corrupt. There is no such
  table for an index to key, and no case creates one. Nothing here requires
  your engine to refuse it either: the refusal is out of scope, only the absence
  of any assertion about it.
- **An injectable clock.** The release notes name a deterministic clock, but it
  is internal: no flag in `--help` selects it, no environment variable this
  package tried changes what `date('now')` returns, `PRAGMA clock` is not a
  valid pragma name, and `current_timestamp` aborts the process at this
  checkpoint. Nothing here reads the wall clock, and R07.18 checks the
  deterministic half of the same surface instead -- the date functions applied
  to literal arguments, which read no clock at all. No case in this step's
  suite calls `'now'`.

## Three boundaries the cases in scope stop at

Each of these is a place where a reader could reasonably expect the assertion
to go further than it does. Each is stated with the measurement that drew the
line, so you can see what is in scope and what merely is not contradicted.

- **An integral real rendered through `||`.** `spec/PROFILE.md` rule 4 governs
  how a *result column* is rendered; it says nothing about the text a real
  becomes inside string concatenation, and this engine and stock disagree there.
  Measured 2026-09-21: `SELECT julianday('2025-01-01 12:00:00')` prints
  `2460677.0` from both, but `SELECT 'jd='||julianday('2025-01-01 12:00:00')`
  prints `jd=2460677` from this engine and `jd=2460677.0` from stock 3.50.2.
  Non-integral reals agree (`'c='||2.5` is `c=2.5` from both). Every
  `julianday()` argument in scope in `s07-scalar-surface` lands on a half-day, so every
  value this step concatenates is non-integral and no case checks the
  disagreement in either direction. Do not read the passing case as fixing the
  integral form.
- **`group_concat()` element order.** The aggregate case checks
  `group_concat(v)` over an index range and expects `2,3,4,5`, which fixes the
  default separator `,` -- but the rows are drawn from a subquery with an
  explicit `ORDER BY v`, because no document in this package fixes the order in
  which `group_concat()` visits rows. What is in scope is the set of rows the
  index range selects and the separator; the case fixes the visit order by ordering
  its own subquery, so the expected string is `2,3,4,5` and no general ordering
  guarantee is asserted.
- **`timediff()` spans that cross a month boundary.** R07.18's intervals in scope are all
  under 24 hours or a whole number of days within one month, and over those
  this engine and stock agree value for value. They do not agree further out:
  measured 2026-09-21, `timediff('2024-03-01','2024-01-01')` is
  `+0000-02-00 00:00:00.000` from stock and `+0000-00-60 00:00:00.000` from
  this engine, which carries the span in days rather than promoting it to
  months. That is the reference behaviour at this checkpoint, so the
  requirement claims stock agreement only for the intervals it checks, and a
  month- or year-spanning `timediff()` is not asserted here in either
  direction.

## Nothing at this step checks a refusal

This is a read-and-compute milestone, and it is worth saying plainly that the
error path is not exercised: **no case in this step's suite expects an error
report, and none ends the shell early.** Across all sixteen case files this
step stages -- the ten in scope and the six inherited; `gate-proof-of-life` is
injected and is `SELECT 1;` -- no expected-output file contains the word
`ERROR` and no case issues `.exit`. Three of the sixteen do carry the
`s/^.*×.*$/ERROR/` fold in their pipeline, but it never fires: it is there so
that an unexpected refusal would surface as a stable line instead of
engine-worded prose, not because a refusal is being checked. Whether your
engine refuses `CREATE INDEX`, `STRICT` or `WITHOUT ROWID`, and how it words
that refusal, changes nothing this step requires.

The work a refusal test would do is done instead by assertions that a wrong
answer must break. They are the ones that would catch an engine returning too
many or too few rows:

- `count-gt`, `count-two-sided` and `count-all` in the aggregate case pin the
  row count of an index range from both sides against stock's own count;
- `asc-unchanged` in the backwards-scan case re-reads the same range forwards,
  so an engine that loses or duplicates an endpoint fails whichever direction
  it got wrong;
- `null-plain=3` in the defaults case asserts that a column with no `DEFAULT`
  stays NULL for every row -- an assertion that something did *not* happen;
- `sqlite:indexes:1` in the interop case has stock count the indexes in the
  file after your engine has read it, so an engine that drops or rewrites the
  index B-tree is caught by the reference engine rather than by its own
  report.

`spec/PROFILE.md` rule 6 -- a refused statement is reported on standard output
and the shell keeps going -- is still an obligation on your engine, and so is
rule 8 -- an unusable command-line argument exits `2`, writes nothing at all to
standard output and leaves no database file behind. Neither is provoked by a
case at this step: nothing here passes a bad argument and nothing here expects a
refusal. They are checked at milestones whose cases do provoke them, not here,
and an engine that exits on its first bad statement will fail those even though
this step's suite never notices.

## What this step re-checks from earlier work

The inherited checkpoint is exactly seven cases. The gate groups are not in
the requirement table because they add no new requirement. This step inherits one
end-to-end case from each of the six earlier milestones, plus proof of life,
and **there is no hidden inherited suite beyond them**. The "Gate groups"
table above names each one with the behaviour it re-runs; in one line each:

- `gate-proof-of-life` -- the installed shell starts and answers `SELECT 1;`.
- `gate-01` -- step 01: `LIKE` patterns with `%` and `_` in every position,
  over five inserted rows, through `WHERE`, `ORDER BY` and `count()`.
- `gate-02` -- step 02: the JSON function surface -- `json_object`,
  `json_array`, `json`, `json_type`, `json_valid`, `json_extract` and
  `json_array_length` -- over integer, real, NULL, text and JSON-literal
  arguments.
- `gate-03` -- step 03: single-row `DELETE` from the middle, the front and the
  end of a table, each followed by `changes()`, the surviving rows, and
  aggregates over them.
- `gate-04` -- step 04: `CAST` of text to `INTEGER`, `REAL` and `NUMERIC`
  across leading whitespace, trailing junk, a truncated fraction, an exponent,
  an empty string and a non-numeric string.
- `gate-05` -- step 05: the hex `jsonb()` produces for each element kind, from
  `null` and the booleans through integers of three sizes, reals, text, the
  empty array and object, and nested arrays and objects.
- `gate-06` -- step 06: `UPDATE` changing the storage class held in an untyped
  column, including to and from NULL, with `typeof()` following the new value.

Every one of the seven passed at this checkpoint when the gate set was
measured, so all seven are things your build already does if you have not
regressed. Nothing else from steps 01 to 06 is re-run here **as an inherited
requirement**.

That is not the same as saying earlier behaviour is unobserved. The cases in
scope above are ordinary SQL, and they use inherited surface as the instrument
with which the new behaviour is measured: a defaulted column is read back with
`||` and `coalesce()`, an index range is counted with `count(*)`. If any of
that is missing or wrong, a case in scope fails even though its subject is
something else. So the instruments are enumerated here rather than summarised,
because you should not have to read the case files to find out what a case in scope
may lean on. In full, and this is the whole of it:

- **statements the engine is asked to execute**: `CREATE TABLE` with declared
  column types (`INTEGER`, `TEXT`, `REAL`) -- the `DEFAULT` clause on those
  columns is this step's own new surface, not inherited; `INSERT ... VALUES`
  with a single tuple and with several; `INSERT` with an explicit column list
  and without one; `SELECT`.
- **clauses and operators**: `WHERE` with `=`, `>`, `>=`, `<`, `AND` and
  `IS NULL`; `ORDER BY` on one key and on two, ascending and `DESC`; `LIMIT`;
  and a sub-`SELECT` in a `FROM` clause.
- **aggregates**: `count(*)`, `sum()`, `min()`, `max()` and `group_concat()`,
  the last with its default `,` separator.
- **scalar functions and operators**: `typeof()`, `coalesce()`, `||`, and
  integer `+`.
- **storage classes**: integer, real, text and NULL, with `typeof()` naming
  them.

Two things this list is careful about. The fixture half of the five
stock-written cases -- `CREATE TABLE`, `INSERT`, `CREATE INDEX`, and in one
case `INSERT INTO ... SELECT` -- is executed by stock `sqlite3`, never by your
engine, so it is not in the list above and `INSERT ... SELECT` is a form no
case at this step asks your engine to run. And the gate re-checks only the six
inherited behaviours enumerated above, plus proof of life: `group_concat()`,
`LIMIT`, `coalesce()` and the sub-`SELECT` are instruments the cases in scope
rely on without any gate case covering them, which is why they are named.

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
| R07.3 | Creating an index never changes a query's result set: the same query over an indexed table and over an unindexed copy of the same rows returns identical rows, sums and counts. | result rows |
| R07.5 | A backwards scan of an index on an INTEGER key and on a TEXT key returns rows in descending key order, under `LIMIT` as well as unbounded, and `min()`/`max()` over a bounded range agree with that order. | result rows |
| R07.11 | A range predicate resolved through an index whose leading key is TEXT returns exactly the rows the range describes, for `>`, `>=`, `<` and `=`, and the counts over those ranges agree with the rows. | result rows |
| R07.12 | `count()`, `sum()`, `group_concat()`, `min()` and `max()` computed over a range of an index see exactly the rows the range describes, and agree value for value with stock `sqlite3` reading the same file. | result rows |
| R07.13 | An index B-tree stock `sqlite3` built is read by the engine, and stock `sqlite3` then re-opens the same file, answers `PRAGMA integrity_check` with `ok`, still finds the index's `sqlite_schema` row and answers the same query identically. | stock sqlite3 reading the file |
| R07.14 | A column omitted from an `INSERT` column list takes its declared `DEFAULT`, a column with no `DEFAULT` takes NULL, and an explicit value overrides both. | result rows |
| R07.15 | A `DEFAULT` clause accepts an unsigned integer, a signed integer, a real, a string literal and a parenthesised constant, and the stored value keeps the storage class the literal names, as `typeof()` reports it. | result rows |
| R07.16 | Hexadecimal integer literals parse as integers, store with storage class `integer`, and compare and arithmetically combine as the decimal value they name. | result rows |
| R07.17 | `likely()` and `likelihood()` parse and return their first argument unchanged, so wrapping a predicate in one of them cannot change which rows come back. | result rows |
| R07.18 | `julianday()` accepts one or more modifiers after its time value — day, month and year offsets, `start of month`, `start of year`, `start of day`, `weekday N`, and chains of them — and `timediff()` returns the signed interval between two time values. For literal arguments `julianday()` agrees with stock `sqlite3` value for value, and `timediff()` agrees over the intervals in scope at this step — spans shorter than 24 hours, and whole days inside a single month. Spans that cross a month boundary are outside the claim and are out of scope; the measurement that drew that line is recorded with this step's other boundaries. | result rows |

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
