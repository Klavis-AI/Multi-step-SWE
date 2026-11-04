# Test interface — step 09

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
| `gate-08` | step 08, `drop-index-then-table-scan.sh` — DROP INDEX removes the index row from sqlite_schema, and the same query still answers correctly from the table scan that replaces it |

### Requirement groups

| Group |
|---|
| `s09-collation` |
| `s09-distinct-aggregates` |
| `s09-insert-select` |
| `s09-pragma-surface` |
| `s09-refusal-surface` |
| `s09-set-operations` |
| `s09-values-rows` |

`s09-collation`, `s09-distinct-aggregates`, `s09-insert-select`,
`s09-set-operations` and `s09-values-rows` cover the capability this step
introduces; `s09-pragma-surface` and `s09-refusal-surface` cover breadth at
this step. Nothing in scope here is satisfiable by an earlier step's work
alone.

The harness reads `tests/assets/groups.txt`, never this table; if the two ever
disagree, the file is what the harness follows.

This step's requirements are independent of every other step's.

## Which group covers which requirement

Every requirement group at this step exists for a row of the behaviour contract
below, and every row has a group. Nothing here is in scope that the table does
not name, with one exception, and it is the same exception at every step: the
process-level contract -- the exit-status rules under "Exit status" above --
is an inherited obligation rather than a row of this step's own table.
`s09-refusal-surface` is the group that observes it: `R01.16`, introduced and
stated at step 01, remains an obligation on your engine here, and it is not a
new requirement of this milestone. Read the table
rather than the step's directory name: the directory is called
`09-set-operations-and-collations`, and four of the six requirement groups are
about neither set operations nor collations.

| Group | Requirements it covers | What it observes |
|---|---|---|
| `s09-values-rows` | R09.1, R09.2 | four bare `VALUES` statements -- a three-row list containing a repeated row, a one-column list, a one-row three-column list, and a list whose first value is `NULL` -- read back as result lines in the order the list writes them, with a count over an ordinary table afterwards showing the batch carried on; `SELECT *` and `count(*)` over a `(VALUES …)` used as a FROM-clause source, beside the same rows taken from a real table; and a statement naming a column the list does not define, which must be reported without returning a row and without stopping the batch. Stock `sqlite3` replays the first script statement for statement on the same file, and answers the same VALUES projections and the same unresolvable-column statement for the second |
| `s09-set-operations` | R09.3 | which rows `UNION` and `UNION ALL` return over the same two tables, and how many times each row appears -- the multiset, not the sequence (see "Two orders this step checks" below) -- for a projected column, for a count, and with an ordinary query after them showing the batch carried on; the same four statements replayed through stock `sqlite3` on the same file under the same canonicalisation; and a re-read of the table through a second, fresh process, so the deduplication is pinned to the query rather than to one process's cache |
| `s09-distinct-aggregates` | R09.4 | `count(DISTINCT x)`, `count(x)`, `count(*)` and `sum(DISTINCT x)` over a column holding both duplicates and NULLs; `SELECT DISTINCT` over one column and over two, counted through a subquery, with and without a NULL-excluding `WHERE`; a `DISTINCT` aggregate under a `WHERE` that narrows the table to three rows carrying one non-NULL value between them; and a `DISTINCT` aggregate whose `WHERE` selects nothing. The whole script is replayed through stock `sqlite3` and compared line for line |
| `s09-insert-select` | R09.5 | the rows `INSERT INTO … SELECT` leaves in the destination table, read back under an `ORDER BY`, and `changes()` after each of four such inserts -- one selecting three rows, one selecting one, one selecting none, and one whose projection computes its values with arithmetic and `upper()` -- with the destination's running count after each |
| `s09-collation` | R09.6 | for an expression-level `COLLATE NOCASE`: how many rows an equality matches under it and under the default `BINARY`, and the full ordering under each, one result row per line; for a column *declared* `COLLATE NOCASE`: the rows an equality, a second equality whose argument is a different, differently-cased string, a non-matching equality and a range scan return from a table carrying an index on that column, beside the identical queries run against a table with the same declaration and no index; and the full ordering under that collation over the indexed table. Stock `sqlite3` gives the second opinion in both cases: statement for statement over the whole script for the declared-collation case, and the `NOCASE` ordering and the `NOCASE` equality for the expression case |
| `s09-pragma-surface` | R09.10 | what `PRAGMA user_version` answers about a database stock `sqlite3` created and nothing has yet set it in (`0`), what it answers immediately after `PRAGMA user_version = 42`, what stock `sqlite3` then reads out of the file itself, and that the table in that file still holds its rows. The assignment is **silent** and is checked as such: the case sends four statements and the expected output holds three lines, because `PRAGMA user_version = 42` returns no row of its own. An engine that echoed the value it had just stored would emit a fourth line and fail the group |
| `s09-refusal-surface` | R01.16, inherited from step 01 | an invocation given a command-line argument the shell cannot use exits `2`, writes nothing at all to standard output and leaves no database file behind, while the same statement stream under a usable argument still runs and still answers |

A group is satisfied only when every one of its cases passes; the cases in a
group are not separate requirements of their own.

## What this step re-checks from earlier work

The gate groups are not in the table above, because they add no new requirement and can
only veto. There are nine of them and they are the whole of this step's
inherited obligation:

- `gate-proof-of-life` starts the installed shell under `-m list -q` and asks
  it `SELECT 1;`.
- `gate-01` through `gate-08` are one case from each earlier milestone, in
  milestone order, each re-run unchanged against your current shell. The gate
  table above names the group, the case file and, in a sentence, what that
  case exercises -- so you can tell what is being re-run without the earlier
  steps' documents, which this step's editions have replaced.

Those nine groups are the whole of what this step re-checks from earlier work.
There is no hidden inherited suite, and no earlier behaviour outside those
nine cases is in scope here as an inherited requirement. That is not the same as
saying earlier behaviour is unobserved: the cases in scope below are ordinary
SQL and lean on `CREATE TABLE`, `INSERT`, `SELECT`, `WHERE`, `ORDER BY`,
`count()`, `sum()`, `upper()`, `changes()` and `CREATE INDEX` as the
instruments that measure the new behaviour, so a regression in any of those
surfaces as a case in scope failing on its own terms rather than as a gate veto.
If any gate group fails, this step is not satisfied whatever else passes, and
`reward_details.json` names the group and the case that vetoed it.

## What the engine is handed, and what it must hand back

Five of this step's eight *in-scope* shell cases do not create their own
database -- the step runs nine `.sh` cases in all, the ninth being
`gate-08` -- and in those five, stock
`sqlite3` creates `main.db` first, and your shell is then pointed at that
file. A database stock `sqlite3` creates is in **rollback-journal mode, not
WAL** -- its page-1 header carries file format read and write version `1`,
and there is no `-wal` file beside it. `spec/PROFILE.md` rule 1 governs the
files this engine *creates*; it says nothing about the ones it is asked to
open, and this is that case. Your shell must open such a file, read its
schema and its rows, and answer from it.

One case goes further and requires a **write** into a stock-created
rollback-journal file: `s09-pragma-surface` has the engine set
`PRAGMA user_version`, and stock `sqlite3` then reopens the same file, reads
the value back and reads the table's rows. `spec/PROFILE.md`'s durability
rule governs that write -- the invocation must not return until the committed
value is recoverable from the files on disk, because the next reader here is
a separate process. What this step does *not* do is inspect the journalling
machinery: no case at this step reads a `-wal` file's bytes, counts its
frames, or asserts whether one exists beside `main.db`. The facts in scope are
the value stock `sqlite3` reads back out of the header and the rows it still
finds in the table.

## Two orders this step checks, and two it does not

Row order is the easiest thing at this step to over- or under-assert, so both
sides are stated.

**In scope.** A bare `VALUES` list, and a `VALUES` list used as a FROM-clause
source, return their rows in the order the list writes them (R09.1, R09.2) --
`lang_select.html` describes a `VALUES` clause as producing exactly those rows
in that sequence, and stock `sqlite3` does the same. An `ORDER BY` on an
ordinary (non-compound) `SELECT` is binding, and the collation cases assert
their orderings through one, over a total order -- the unique integer key, or
the text column followed by it.

**Out of scope.** The sequence in which a compound `SELECT` returns its rows.
`lang_select.html` section 4 states that a `SELECT` returning more than one
row without an `ORDER BY` returns them in an undefined order, and at this
checkpoint there is no in-SQL way to impose one on a compound: `ORDER BY` on a
compound is refused (`Parse error: ORDER BY is not supported for compound
SELECTs yet`), and so is a compound inside a FROM-clause subquery
(`Parse error: Only non-compound SELECT queries are currently supported in
FROM clause subqueries`). So the set-operations case sorts its compound block
-- and stock `sqlite3`'s answer to the same statements under the same sort --
before comparing them. What it checks is which rows come back and how many
times, which is what R09.3 says. Deduplicate however you like.

Also out of scope: `group_concat`'s concatenation order.
`spec/sqlite/lang_aggfunc.html`, which this step's reading list points you at,
calls it arbitrary unless the `ORDER BY` is an argument of the aggregate
itself -- an `ORDER BY` in a subquery underneath it does not bind it -- so no
case in this step renders an ordering in scope through `group_concat`.

## What this milestone does not include

Seven things a reader might reasonably expect from a step that adds `VALUES`,
`UNION`, the `DISTINCT` forms and `COLLATE NOCASE` are **out of scope** here,
and none of them is listed above. Each absence was measured against the
release chain on 2026-09-21, not decided by argument. Do not treat any of them
as a defect, and do not spend the step on them:

- **Join reordering and `EXPLAIN`.** A three-table join already returns the
  same rows under every `FROM`-clause order at the previous checkpoint, so
  nothing here is new at this step. The plan text is a separate problem: this
  engine renders `EXPLAIN QUERY PLAN` in its own form, that form changed
  inside this release chain, and it matches neither the previous checkpoint
  nor stock `sqlite3`. No document you receive states it, so no case asserts
  it. Choose whatever join order you like.
- **`OR`-chain subexpression lifting.** `(x>2 AND y='c') OR (x>2 AND y='d')`
  and `x>2 AND (y='c' OR y='d')` already return the same rows at the previous
  checkpoint and in stock `sqlite3`. Nothing at this step checks the
  transformation, and no case looks for it in a plan.
- **Offset-based reads inside an overflowing payload.** `length()` and
  `substr()` over a value stored across an overflow chain already answer
  correctly at the previous checkpoint. Overflow-chain traversal is checked at
  step 1 and its gate case carries it forward; there is no new obligation
  here.
- **`WITHOUT ROWID` tables.** Out of scope at this step or any other. No
  checkpoint in this release chain accepts one, so there is no reference
  answer to compare against. Refusing `CREATE TABLE ... WITHOUT ROWID` under
  the error contract in `PROFILE.md` costs you nothing here.

- **An `INSERT` whose source is a `VALUES` list.** `INSERT INTO dest SELECT *
  FROM (VALUES(1,'x'),(2,'y'),(3,'z'))` stores `(6,1),(9,2),(12,3)` at this
  checkpoint -- not the rows the inner `SELECT` returns. That is a write-side
  defect in the reference build, and no case checks it, so you are not being
  asked to reproduce it and not being asked to fix it. R09.2 covers the
  `SELECT` form only; `s09-insert-select` measures the plain
  `INSERT INTO … SELECT` of R09.5, whose source is a table.
- **Which plan a query uses, and so whether an index was consulted.** R09.6's
  index arm is checked on the rows the indexed query returns beside the rows
  the same query returns from an unindexed table with the same declared
  collation -- never on the plan. There is no observable for plan choice here:
  `EXPLAIN` and `EXPLAIN QUERY PLAN` are out of scope for the reason above,
  and no other statement reports it. Use the index or don't; what has to hold
  is that the rows agree, and that `NOCASE` is the key's collation.
- **The sequence in which a compound `SELECT` returns its rows.** Set out
  under "Two orders this step checks, and two it does not" above; listed again
  here so this section is the whole list. `UNION`'s output order is out of
  scope.

What *is* new at this step, and what the requirement groups measure, is the list
above: `VALUES`, `UNION` against `UNION ALL`, the `DISTINCT` forms,
`INSERT ... SELECT` with `changes()`, `COLLATE NOCASE` both as an expression
and carried by an index, and `PRAGMA user_version`.

One obligation the list above does not cover is the process-level rejection of
`spec/PROFILE.md` rule 8, and it is checked here rather than left to another
milestone: `s09-refusal-surface` invokes the shell with an argument it cannot
use and reads back that the invocation exits `2`, writes nothing at all to
standard output and leaves no database file behind, while the same statement
stream under a usable argument still runs and still answers. The refusals the
rest of the corpus exercises are the statement-level ones the cases fold to
`ERROR`.

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
marker on it, and only puts `×` in front of it from `v0.1.2` onward, while a
*statement-level* parse error -- a syntax error in the statement itself -- carries
`×` at every release in the chain. A parse error raised *inside* a scalar function
is the narrower class `spec/PROFILE.md` measures, and that one can come back bare. The cases that do
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
scalar-function parse error `spec/PROFILE.md` measures is the exception, and it is
the reason a bare `Parse error: ...` line is not a form to rely on. A shell whose refusals
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
| R09.1 | A bare `VALUES (…),(…)` statement returns its rows in the written order, as stock `sqlite3` does. | result rows |
| R09.2 | `VALUES` used as the source of a `SELECT` — `SELECT … FROM (VALUES …)` — returns the same rows, in the order the list writes them, as stock `sqlite3` does for the same statement, and a column name the list does not define is reported rather than resolved. | result rows |
| R09.3 | `UNION` removes duplicate rows while `UNION ALL` preserves every duplicate, matching stock `sqlite3` on the same inputs. | result rows |
| R09.4 | The `DISTINCT` forms match stock `sqlite3`, including their treatment of NULLs: a `DISTINCT` aggregate such as `count(DISTINCT x)` or `sum(DISTINCT x)` folds duplicate arguments and skips NULL ones, and a row-level `SELECT DISTINCT` — over one column and over several — eliminates duplicate result rows, counting two NULLs in the same column as one another's duplicate. | result rows |
| R09.5 | `INSERT INTO … SELECT` inserts exactly the selected rows and reports the matching `changes()` count. | result rows |
| R09.6 | `COLLATE NOCASE` applies both as an expression-level collation — in an `ORDER BY` term and in an equality test, where the default `BINARY` collation stays byte-ordered — and when carried by an index on a column declared with it, where the indexed query returns the same rows as the identical query run without the index. | result rows |
| R09.10 | `PRAGMA user_version` reads the page-1 header's `user_version` field at offset 60 — answering `0` for a database in which it has never been set — and `PRAGMA user_version = N` writes N into that field, so that the engine reads N back and stock `sqlite3` reads the same N out of the file afterwards with the rest of the database intact. The assignment form is silent: `PRAGMA user_version = N` writes the value and returns no result row of its own, so every line of a batch's compared output comes from the surrounding statements — a batch of four in which one is the assignment is checked on three lines, not four. Silence is not the rule for every pragma assignment, which is why this is stated rather than left to be inferred: stock `sqlite3` answers `PRAGMA journal_mode=wal` with the line `wal`. For `user_version` it answers nothing, and an engine that echoed the value it had just stored would emit a line this requirement does not allow. | result rows, then database file bytes |

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
