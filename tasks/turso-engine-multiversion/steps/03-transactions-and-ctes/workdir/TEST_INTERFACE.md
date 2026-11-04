# Test interface — step 03

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

### Requirement groups

| Group |
|---|
| `s03-cte` |
| `s03-dml-completion` |
| `s03-expressions-and-pragmas` |
| `s03-json-completion` |
| `s03-refusal-surface` |
| `s03-transactions` |

`s03-cte`, `s03-dml-completion` and `s03-transactions` cover the capability
this step introduces; `s03-expressions-and-pragmas`, `s03-json-completion` and
`s03-refusal-surface` cover breadth at this step. Nothing in scope here is
satisfiable by an earlier step's work alone.

The harness reads `tests/assets/groups.txt`, never this table; if the two ever
disagree, the file is what the harness follows.

This step's requirements are independent of every other step's.

## The output-mode selector is spelled `list` from this step

Steps 1 and 2 were checked with `-m raw`. From this step the harness passes
`-m list`, which selects the same rendering under the name the project uses
from this release onward. `-m raw` need not be kept. Every case in scope runs
under `-m list`, and so does every gate case; the superseded spelling is passed
to the gate only when the shell cannot answer `SELECT 1;` under `-m list` at
all — the rename is this step's new capability, and a gate that demanded it
would veto a candidate for not having done this step's own work instead of
leaving that work to be checked on its own terms. A shell that answers under `-m list` never sees
`-m raw` again. The gate cases from steps 1 and 2 are re-run under `-m list`,
so the rendering must be unchanged from what those cases already expect.

## Which group covers which requirement

Every requirement group at this step exists for a row of the behaviour contract
below, and every row has a group. Nothing here is in scope that the table does
not name, with one exception, and it is the same exception at every step: the
process-level contract -- the exit-status rules under "Exit status" above --
is an inherited obligation rather than a row of this step's own table.
`s03-refusal-surface` is the group that observes it: `R01.16`, introduced and
stated at step 01, remains an obligation on your engine here, and it is not a
new requirement of this milestone.

| Group | Requirements it covers | What it observes |
|---|---|---|
| `s03-transactions` | R03.4, R03.5, R03.11, R03.12 | which transaction-control statement the shell **takes** and which it **refuses**. Each statement is isolated between two `SELECT 'mark';` rows and the block between the marks is reduced to one word: empty means accepted, non-empty means refused. So an accepted statement must print nothing at all, and a refused one must print something — no wording is compared, and neither is the number of lines a refusal occupies. The cases also read a `count(*)` back from inside the open transaction, which is R03.4's second half. Every verdict, and that `count(*)`, is asked of stock `sqlite3` 3.50.2 as well and printed beside this engine's answer, so no row of the expected output rests on this engine's word alone |
| `s03-cte` | R03.6 | the rows a non-recursive `WITH … AS (…) SELECT` returns, and their order, for twelve CTE shapes across two cases — filtering, aggregating, constants with no table under them, expression columns, grouping inside the CTE, `ORDER BY`/`LIMIT` inside the CTE while the outer query orders differently, one CTE feeding the next, and a CTE joined back to its base table |
| `s03-dml-completion` | R03.13, R03.14 | the rows that survive each `DELETE` and their order, listed in full after every one, so a `DELETE` that takes the wrong row and one that takes too many are both visible; `changes()` after each; and, once the table is empty, its `sqlite_schema` row, the empty result, and the rowid and `last_insert_rowid()` of the inserts that refill it |
| `s03-json-completion` | R03.15, R03.16 | the text `json_set` and `json_quote` return for each call, compared against stock `sqlite3`; and, for a path that does not parse, that the call produced no row, that the statement after it still ran, and that the stored document it was aimed at reads back unchanged. A row that came back where a refusal was required is not folded away: it reaches the comparison with its text intact. The rows this engine returned are compared against the rows stock `sqlite3` returns for the same batch |
| `s03-expressions-and-pragmas` | R03.17, R03.18 | what each `CAST` answers, compared against stock `sqlite3`; and `PRAGMA user_version` on a fresh database, cross-checked against the four bytes at page-1 header offset 60 read by `od` and against stock `sqlite3`'s answer for the same pragma |
| `s03-refusal-surface` | R01.16, inherited from step 01 | an invocation given a command-line argument the shell cannot use exits `2`, writes nothing at all to standard output and leaves no database file behind, while the same statement stream under a usable argument still runs and still answers |

`s03-transactions`, `s03-cte` and `s03-dml-completion` are the **new** groups;
the other two are **peripheral**.

## What this step re-checks from earlier work

The gate groups are not in that table, because they add no new requirement and
only veto. There are three of them and they are the whole of this step's inherited
obligation — there is no hidden regression suite behind them:

- `gate-proof-of-life` starts the installed shell and asks it `SELECT 1;`.
- `gate-01` is one step-01 case, re-run unchanged: it creates a table, inserts
  rows, and asks for them back through `WHERE`, `ORDER BY` and `count()` under
  `LIKE` patterns that put `%` and `_` in every position.
- `gate-02` is one step-02 case, re-run unchanged: the JSON functions over
  their text representation, including how an integer, a real, a `NULL` and a
  JSON literal are each coerced.

The two inherited cases are re-run under `-m list` rather than the `-m raw`
they were first checked with, and the rendering the two spellings select is the
same, so their expected output is unchanged. Nothing outside those two cases
is in scope here *as an inherited requirement*: the gate is the whole of what
this step asks you to carry forward, and passing it is all that earlier
behaviour must do. That is not the same as saying earlier behaviour is
unobserved. The cases in scope below are written in ordinary SQL and use
`CREATE TABLE`, `INSERT`, `SELECT`, `WHERE`, `ORDER BY`, `count()` and the
step-02 JSON functions as the instruments that measure the new behaviour, so
a regression in any of those surfaces shows up as a case failing on its own
terms, not as a gate veto. If any gate group fails, this step is not satisfied
and `reward_details.json` names the group and the case that vetoed it.

## What this milestone does not include

The step is titled for transactions and CTEs, and a reader can reasonably
expect more of both than is here. Eight such things are **out of scope** at this
step. Each was probed against the release chain on 2026-09-21 and the
measurement, not a judgement, is the reason. Do not treat any of them as a
defect, and do not implement against them expecting them to be required:

- **Two connections at once.** Nothing here observes one connection while
  another holds a transaction open: not rows invisible until a `COMMIT`
  returns, and not a second connection's `BEGIN IMMEDIATE` failing busy
  against an open `BEGIN EXCLUSIVE`. The shell reads one script per process
  and exits, so there is no second connection to hold open, and two shells
  writing to one stream interleave, which makes a case non-deterministic. What
  one connection can see is what is in scope: which statement is accepted, which
  is refused, and that a refusal leaves the open transaction usable.
- **Reading back what a transaction committed.** The single-connection
  substitute is not stable across this chain either. Three rows inserted
  inside `BEGIN IMMEDIATE` and committed come back as three rows at this
  checkpoint and at the last five, but as **one** — only the last `INSERT` —
  at the four checkpoints in between. No case in this task may depend on it,
  and that is also why nothing here checks `PRAGMA wal_checkpoint` over an
  explicit transaction's frames: the rows are already gone before the
  checkpoint is issued. Checkpointing itself is in scope at step 02.
- **`ROLLBACK`.** `BEGIN IMMEDIATE; INSERT; ROLLBACK;` answers
  `× Parse error: ROLLBACK not supported yet` at this checkpoint and leaves
  the inserted row in place. It answers the same way, and keeps the row, at
  every checkpoint from here through step 11's, and only discards the row from
  step 12's onward. So "rollback undoes the transaction" is false here and
  "rollback is refused" would be false later; neither is in scope at this
  step, and the statement is not in this step's contract at all. `BEGIN`,
  `COMMIT` and `END` are.
- **Recursive CTEs.** `WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x+1
  FROM c WHERE x<4)` is refused at **every** checkpoint in this task, first to
  last — `× Parse error: Recursive CTEs are not yet supported` from this one
  onward, and a different parse error at the two before it. No step checks it.
  R03.6 is the non-recursive form, and that is the whole of the CTE obligation
  here.
- **Virtual tables.** `CREATE VIRTUAL TABLE … USING …` answers
  `× Parse error: CREATE VIRTUAL TABLE not supported yet` here, and no module
  is registered: `SELECT … FROM generate_series(1,3)` answers
  `× Parse error: Virtual table generate_series not found`. The first
  checkpoint that returns rows from it is step 11's, which is where the
  table-valued function group lives. Nothing at this step covers a
  virtual-table interface.
- **Choosing a storage engine.** There is no engine selector to leave unset at
  this checkpoint: `--help` here lists `-m`, `-o`, `-q`, `-e`,
  `-i/--io {syscall,io-uring}`, `-h` and `-V`, and nothing more. The
  experimental-MVCC flag first appears at step 05's checkpoint, which is the
  step titled for MVCC as a selectable engine -- its appearance in `--help`
  is all that happens there, and no requirement at that step checks it
  either. A requirement
  about which engine a connection picks could not be made false by any build
  here, so there is not one.
- **`DELETE` without a `WHERE` clause, and `DROP TABLE`.** R03.13 and R03.14
  cover `DELETE FROM … WHERE`, including emptying a table by deleting its rows
  one at a time. Emptying it in a single statement, and removing the table
  itself, are out of scope at this step.
- **The process-level rejection of `spec/PROFILE.md` rule 8.** An unusable
  command-line argument exits `2` with nothing on standard output and no
  database file behind it, and it is checked at this step by
  `s03-refusal-surface`, which passes such an argument and then runs the same
  statement stream under a usable one. A panic at `101` remains an obligation
  that nothing here measures: no case at this step carries an `.exit` sidecar.
  The SQL-level rejection this step also checks is a statement refused where
  it stands under R03.4/R03.16, and between the two of them the shell that
  refuses is distinguished from the shell that accepts everything.

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
below is the whole of it. Some cases checked this way use one of these two `sed`
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
| R03.4 | A statement that cannot run is refused where it stands inside an open transaction rather than being swallowed by it, and the transaction stays usable afterwards: a query issued later, still inside the same transaction, still answers. | error report, then result rows |
| R03.5 | A `BEGIN`, a `BEGIN IMMEDIATE` or a `BEGIN EXCLUSIVE` issued while a transaction is already open is refused — in each of those three spellings — and refusing it does not close or disturb the transaction that is open. | error report |
| R03.6 | A non-recursive `WITH … AS (…) SELECT` returns the same rows in the same order as stock `sqlite3`: a CTE that filters, one that aggregates, one built from constants with no table under it, one whose columns are expressions, one grouped before the outer query sees it, one that orders and limits inside itself while the outer query orders differently, one CTE feeding the next, and a CTE joined back to the table it came from. | result rows |
| R03.11 | `BEGIN IMMEDIATE` and `BEGIN EXCLUSIVE` are accepted where the previous checkpoint refused them, with and without the optional `TRANSACTION` keyword, and an accepted one produces no report at all. | error report |
| R03.12 | A `COMMIT` or an `END` issued with no transaction open is refused rather than silently ignored. | error report |
| R03.13 | `DELETE FROM … WHERE` removes exactly the rows its predicate selects and no others — taking the first row of the table, one from the middle, and the last — the survivors keep their values and their rowid order, and `changes()` reports how many rows the statement actually took. | result rows |
| R03.14 | A table emptied by `DELETE`, one row at a time, is still a table: its `sqlite_schema` row survives, it answers a query with nothing rather than failing, and rows inserted afterwards are found again — with the rowid stock `sqlite3` hands out, which after an emptied table restarts at 1, as `last_insert_rowid()` reports. | result rows |
| R03.15 | `json_set` replaces the value at a path that exists and creates one at a path that does not — at the top level and nested, in an object and in an array, with a JSON value and with SQL `NULL` — and `json_quote` renders a SQL value as the JSON text that denotes it. Every answer is the text stock `sqlite3` returns for the same call. | result rows |
| R03.16 | A JSON path argument that does not parse is refused where it stands: the call yields no row, the statements after it still run, and the document is left exactly as it was. A path that parses but selects nothing is not an error — it answers `NULL`. | error report, then result rows |
| R03.17 | `CAST` between the storage classes answers as stock `sqlite3` answers: a cast to `INTEGER` takes the leading numeric prefix and yields `0` where there is not one, a negative real truncates toward zero rather than toward minus infinity, `NUMERIC` narrows a text value to the narrower storage class it fits while a REAL or INTEGER passed to `NUMERIC` is unchanged, and `NULL` stays `NULL` whatever it is cast to. | result rows |
| R03.18 | A freshly created database reports `PRAGMA user_version` as `0`, and the four bytes at page-1 header offset 60 that back it are zero in the file as well — as read by `od` and by stock `sqlite3`, neither of which asks this engine anything, so an engine that answers the pragma from memory without writing the header is caught by the disagreement. | result rows, then database file bytes, then stock sqlite3 reading the file |

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
