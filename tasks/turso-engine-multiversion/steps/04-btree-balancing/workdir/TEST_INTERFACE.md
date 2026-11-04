# Test interface — step 04

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

### Requirement groups

| Group |
|---|
| `s04-btree-balancing` |
| `s04-cast-compatibility` |
| `s04-dump-blob-literals` |
| `s04-like-compatibility` |
| `s04-refusal-surface` |

`s04-btree-balancing` covers the capability this step introduces;
`s04-cast-compatibility`, `s04-dump-blob-literals`, `s04-like-compatibility`
and `s04-refusal-surface` cover breadth at this step. Nothing in scope here is
satisfiable by an earlier step's work alone.

The harness reads `tests/assets/groups.txt`, never this table; if the two ever
disagree, the file is what the harness follows.

This step's requirements are independent of every other step's.

## Which group covers which requirement

Every requirement group at this step exists for a row of the behaviour contract
below, and every row has a group. Nothing here is in scope that the table does
not name, with one exception, and it is the same exception at every step: the
process-level contract -- the exit-status rules under "Exit status" above --
is an inherited obligation rather than a row of this step's own table.
`s04-refusal-surface` is the group that observes it: `R01.16`, introduced and
stated at step 01, remains an obligation on your engine here, and it is not a
new requirement of this milestone.

| Group | Requirements it covers | What it observes |
|---|---|---|
| `s04-btree-balancing` | R04.1, R04.2 | two insert workloads large enough to split pages -- one of 100 rows arriving from the highest key down, so every split falls at the left edge of the tree, one of eight rows, just past the four a page holds, so the root itself splits -- each listed in full afterwards, one row per line with the key its own payload carries, so that a cell that came through a split under the wrong key is visible individually and not only through `count(*)`, `sum(id)` and `sum(length(v))`; the descending workload is read from both ends of the key order as well, the root-split workload through a range whose lower bound falls inside the split; and both end by asking `PRAGMA page_count` for the page count the split rule leaves behind |
| `s04-cast-compatibility` | R04.4, R04.5 | what `CAST(... AS INTEGER)`, `AS REAL` and `AS NUMERIC` answer for text with leading and trailing blanks, a trailing non-numeric tail, an exponent, a truncated fraction, an empty string, text with no numeric prefix at all, and the three forms SQLite does not read as numbers -- `'nan'`, `'inf'` and a `'0x...'` hex prefix -- together with `typeof()` on the results |
| `s04-like-compatibility` | R04.6, R04.7 | which rows `LIKE` matches when an `ESCAPE` clause is present, including an escaped wildcard matched literally and the escape character escaping itself; and what `LIKE` answers when either operand is an integer, a real, a blob or NULL: an integer or a real is matched as its own text rendering — `100 LIKE '1%'` is `1`, `1.5 LIKE '1%'` is `1`, `100 LIKE '2%'` is `0` — a NULL operand makes the result NULL, never an error and never `0` (`typeof(NULL LIKE 'a')` is `null`, `typeof('x' LIKE NULL)` is `null`, and `coalesce(NULL LIKE 'a', -1)` is `-1`), and a blob is matched by its bytes (`x'6162' LIKE 'ab'` is `1`) |
| `s04-dump-blob-literals` | R04.8 | the `X'...'` literals the shell's own `.dump` writes for four blob values, in row order; and the `quote()` and `length()` stock `sqlite3` reports for every value it re-imported from that dump text |
| `s04-refusal-surface` | R01.16, inherited from step 01 | an invocation given a command-line argument the shell cannot use exits `2`, writes nothing at all to standard output and leaves no database file behind, while the same statement stream under a usable argument still runs and still answers |

Stated plainly, and not left to be inferred from the step's name, the four
capabilities this step adds are: **SQLite's page-splitting rules** --
balance-siblings when a leaf overflows and balance-deeper when the root does;
**text-to-number `CAST` matching the reference exactly**, including the forms
that are not numbers; **`LIKE` matching the reference exactly**, with an
`ESCAPE` clause and over non-text operands; and **`.dump` rendering a blob as
an `X'...'` literal** that stock `sqlite3` can re-import.
`s04-btree-balancing` is the `new` group;
the other three are breadth at this step.

## What this step re-checks from earlier work

The gate groups are not in that table, because they add no new requirement and
only veto. There are four of them and they are the whole of this step's inherited
obligation — there is no hidden regression suite behind them, and nothing
else from steps 01, 02 or 03 is re-run here:

- `gate-proof-of-life` starts the installed shell and asks it `SELECT 1;`.
- `gate-01` is step 01's `like-predicates.sql`, re-run unchanged: five rows
  inserted, then returned through `WHERE`, `ORDER BY` and `count()` under
  `LIKE` patterns that put `%` and `_` in every position.
- `gate-02` is step 02's `json-argument-coercion.sql`: `json_object`,
  `json_array`, `json`, `json_type`, `json_valid`, `json_extract` and
  `json_array_length` over integer, real, NULL, text and JSON-literal
  arguments.
- `gate-03` is step 03's `delete-single-rows.sql`: single-row `DELETE` from
  the middle, the front and the end of a table, each followed by `changes()`,
  the surviving rows, and aggregates over them.

That is the list in full: four groups, one case each, and the four cases are
the only inherited material staged at this step. `gate-01` and `gate-02` were
first checked under `-m raw`, the spelling the shell accepted at those two
checkpoints, and are re-run here under `-m list`. Their expected output is
byte-for-byte the output those steps recorded, and it is unchanged because the
two select the same rendering -- the same `|`-separated rows with no header --
not because anything was re-recorded. Measured: `like-predicates.sql` under
`-m raw` at the step 01 checkpoint and under `-m list` at this one produce the
same 14 lines, and `json-argument-coercion.sql` the same 8. `raw` is not a
mode you have to accept: from the step 03 checkpoint onward the shell takes
only `list` and `pretty`, which is why the harness passes `-m list` from step
03 on and why the flag table above names that one.

Passing those four is all that earlier behaviour must do here, and
that is not the same as saying earlier behaviour is unobserved. The cases in
scope below are ordinary SQL: they use `CREATE TABLE`, `INSERT`, `SELECT`,
`ORDER BY`, `substr()`, the aggregate functions and the shell's own rendering
as the instruments that measure the new behaviour, so a regression in any of
those surfaces shows up as a case failing on its own terms rather than as a
gate veto. If any gate group fails, this step is not satisfied and
`reward_details.json` names the group and the case that vetoed it.

## The page count of a split file, and why it is in scope

R04.1 and R04.2 each end by asking the finished database `PRAGMA page_count`.
That is not a measurement of your packing efficiency, and the "Out of scope"
list below is not in tension with it: the *size in bytes* of a file is yours,
but the page count of a file whose cell distribution a requirement fixes is a
consequence of the rule the requirement names.

The rule is SQLite's, and so is the number. Stock `sqlite3` 3.50.2, replaying
each of these two cases' statements at the same 4096-byte page size, reports
the same counts the cases assert -- 49 for the descending 900-byte workload
and 4 for the root split. An engine that redistributes cells the way
balance-siblings and balance-deeper redistribute them arrives at those counts;
one that splits pages down the middle, or that leaves a freelist page behind,
does not. Both cases also list every row back in key order -- the whole table, one row
per line, each with the key its payload carries -- so a build cannot reach
the count by losing a row, by duplicating one, or by leaving a cell on the
wrong side of a split.

## What `.dump` renders for a blob at this checkpoint

`.dump` is a shell command, not SQL, and the case that checks R04.8 reaches
it the way every other statement is reached: by writing it on the shell's
standard input, which is a pipe and not a terminal. So the shell has to
**accept dot-commands on a non-tty standard input, interleaved with SQL in
the same stream** — the case sends one `CREATE TABLE`, four `INSERT`s and
then a `.dump` line to a single invocation and reads the dump off standard
output. That is the only dot-command any case at this step uses, and it is
the only thing this step asks of the command-line surface beyond the flags in
the table above.

R04.8 is checked through the dump *text* and through stock `sqlite3`'s reading
of it, and the form is fixed here because nothing in `COMPAT.md` states it:

- A blob column is written as an `X'...'` literal: a capital `X`, a
  single-quoted run of **lowercase** hex digits, two digits per byte, no `0x`
  prefix and no separators. An empty blob is `X''`.
- The dump is a script stock `sqlite3` can run. The case builds a second
  database from the dump text and compares `quote(v)` and `length(v)` for
  every row; `quote()` renders the same bytes with *uppercase* digits, which
  is stock `sqlite3`'s own convention and not something your engine controls.
- Nothing in the case reopens your engine's database file with another
  process. Only the dump text crosses between the two.

Two things are deliberately left alone. The dump's `CREATE TABLE` line -- the
engine's normalised rendering of the schema text -- is **not** compared, so
its spacing cannot fail this case; that is a separate obligation checked where
a requirement names it. And the case never asks your engine's own `hex()` for
a second opinion on the bytes, because at this checkpoint the reference build
renders a byte above `0x7f` through a UTF-8 replacement sequence
(`hex(x'00ff10')` answers `00EFBFBD10`); a case that read the bytes back that
way would be checking a defect rather than the literal.

A blob column holding NULL is also outside R04.8. From this checkpoint until
`v0.1.5` the reference renders one as `X''` rather than as `NULL`, and
freezing that into the contract would teach the wrong behaviour.

## What this milestone does not include

Five things the release notes for `v0.0.16` might lead a reader to expect are
**out of scope** here. Each was probed against this checkpoint on 2026-09-21 and
each measurement agrees with `COMPAT.md` at this release; none is a defect in
your build and none can regress here.

- **Virtual tables.** The changelog lists "Virtual table support" under
  *Added*, but `CREATE VIRTUAL TABLE ... USING <m>` is answered `Extension
  error: Virtual table module not found: <m>` for every module name tried --
  `generate_series`, `fts5`, `rtree`, `csv`, `series`, `json_each`,
  `json_tree`. No module is registered at this checkpoint, so no virtual table
  can be created or queried. `COMPAT.md` marks `CREATE VIRTUAL TABLE` as `No`.
  The first checkpoint that returns rows from one is step 11's.
- **URI filenames.** The changelog lists "Handle parsing URI according to
  SQLite specification" under *Fixed*, but the shell still takes its database
  argument as a path: given `file:main.db?mode=ro` it creates a file *named*
  `file:main.db?mode=ro` in the working directory, with a matching `-wal`
  sidecar. Neither the path component nor the `mode=ro` parameter is
  reachable, so the read-only refusal is not provokable and is out of scope.
- **Auto-vacuum and pointer-map pages.** `PRAGMA auto_vacuum`, and the same
  pragma set to `1`, `FULL` or `incremental`, and `PRAGMA incremental_vacuum`,
  are each answered `Parse error: Not a valid pragma name`; `VACUUM` is
  answered `Parse error: VACUUM not supported yet`. Auto-vacuum cannot be
  turned on, so no file with a pointer-map page exists to assert anything
  about. `COMPAT.md` marks all of these `No`.
- **The rejection paths.** `spec/PROFILE.md` rules 6 to 8 — a refused
  statement reported on standard output with the batch continuing and the
  process still exiting `0`, and an unusable command-line argument exiting `2`
  with nothing on standard output and no database file behind it — remain
  obligations on your engine. The **process-level** half is checked at this
  step, by `s04-refusal-surface`, which passes an argument the shell cannot use
  and reads the exit status, the standard output and the file system back,
  beside the same statement stream under a usable argument. The **statement-level**
  half is not: no case here carries an `.exit` sidecar or an expected output
  line that a refusal produces, so nothing at this step distinguishes a shell
  that refuses a statement it cannot do from one that accepts it silently. The
  rejections the documents do describe at this checkpoint — a virtual table,
  and `PRAGMA auto_vacuum`/`PRAGMA incremental_vacuum`/`VACUUM` — are quoted
  above and none of them is checked either; the URI read-only case is not
  provokable at all rather than refused.
- **Freelist reuse and page *reclamation*.** `DELETE` that empties pages is
  checked at step 03 for its row effects. Which pages a delete returns to the
  freelist, and whether a later insert reuses them, is not asserted at this
  step: the two balancing cases only ever insert.

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
| R04.1 | An insert workload that forces sibling page splits redistributes cells by SQLite's balance-siblings rule, so every row reads back in key order from both ends of the tree and `PRAGMA page_count` on the finished file reports the count that rule determines — the same count stock `sqlite3` reports for the same statements at the same page size. The workload arrives as one multi-row `INSERT INTO t VALUES (…), (…), …` statement carrying all of the rows, highest key first. | result rows |
| R04.2 | An insert workload that grows the tree's depth (balance-deeper) splits the root into an interior page with leaves below it, so every row still reads back in key order, a range whose lower bound falls inside the split is answered through the new interior node, and `PRAGMA page_count` reports the pages that distribution leaves. | result rows |
| R04.4 | `CAST` from text to integer or real matches SQLite exactly for leading and trailing whitespace, a trailing non-numeric tail, an exponent and fractional truncation, and for the three text forms SQLite does **not** read as numbers — `'nan'`, `'inf'` and a `'0x…'` hex prefix — each of which converts to zero. | result rows |
| R04.5 | `CAST` of a wholly non-numeric text value yields zero rather than an error, as stock `sqlite3` does, and the storage class follows the target: `CAST('abc' AS REAL)` is the real `0.0`, while a cast to `INTEGER` or to `NUMERIC` yields the integer `0`, which `typeof()` reports as `integer`. | result rows |
| R04.6 | `LIKE` with an `ESCAPE` clause returns the same matches as stock `sqlite3`, including an escaped wildcard matched literally. | result rows |
| R04.7 | `LIKE` applied to a non-text argument (integer, real, blob or NULL) returns the value stock `sqlite3` returns rather than raising an error. | result rows |
| R04.8 | The shell's `.dump` renders a blob value as an `X'…'` literal with lowercase hex digits, and a fresh database stock `sqlite3` builds from that dump holds the identical bytes. | result rows |

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
