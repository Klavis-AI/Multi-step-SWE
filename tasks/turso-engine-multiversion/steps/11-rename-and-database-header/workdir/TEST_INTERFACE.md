# Test interface — step 11

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

The shell is renamed at this step, and the rename is part of the new work
rather than the gate: if you install nothing at the new path, the gate
groups still run against the shell you inherited, so what fails is the
capability you did not gain, not a regression you did not cause. The cases
in scope only ever run `/app/bin/tursodb`.

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
| `--help` | used two ways at this step. An inherited gate case reads the option list and passes an option only if it is offered, and a case in scope checks R11.6: the string `tursodb` must appear somewhere in what `--help` writes to standard output and the retired name `limbo` must appear nowhere in it in any capitalisation -- `Limbo` and `LIMBO` are rejected with it, so a help text that still calls the product Limbo fails the case even though the executable name is right. Nothing else about the text is compared -- not its wording, its ordering, its layout, nor which options it lists -- but writing nothing at all fails R11.6 at this step, unlike at the earlier steps that only probe it. A shell that opens an interactive session instead will hang the case until its 120-second timeout. |

In `list` mode a result row is one line, columns separated by `|`, no
header row, no alignment padding, and NULL rendered as the empty string. The
shell adds no line break of its own: a newline *inside* a text value is written
through, so a value holding one occupies more than one output line.

## How output is compared

- **SQL cases**: byte for byte, after blank lines are removed from the
  produced output. Blank separator lines between result blocks are therefore
  not part of the SQL-case contract; every value, its rendering, its column
  separator and its row order are. The expected files hold no blank lines, so removing
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

### Requirement groups

| Group |
|---|
| `s11-binary-rename` |
| `s11-database-creation` |
| `s11-shell-output-framing` |
| `s11-table-valued-functions` |

`s11-binary-rename`, `s11-database-creation` and `s11-table-valued-functions`
cover the capability this step introduces; `s11-shell-output-framing` covers
breadth at this step. Nothing in scope here is satisfiable by an earlier
step's work alone.

The harness reads `tests/assets/groups.txt`, never this table; if the two ever
disagree, the file is what the harness follows.

This step's requirements are independent of every other step's.

## The binary is renamed at this step

Steps 1 to 10 installed `/app/bin/limbo`. From this step the shell is
installed as `/app/bin/tursodb`, and **`/app/bin/limbo` must no longer
exist**. The verifier checks both: the new path must be a regular executable
file, and the old name must be gone. Leaving a copy, a hard link or a symlink
behind at the old path fails the step.

The upstream release notes for this version describe the project rename;
`CONTRIBUTING.md` in this step's `spec/turso/` names the new binary.

## What this step adds

The step's directory name names two of the four capabilities in scope here. In
full, this checkpoint adds:

- **The rename**, above: the shell is installed as `/app/bin/tursodb`, nothing
  executable is left behind under the retired name, and the shell's own usage
  text names the program you installed rather than the retired one — R11.1,
  R11.2, R11.6.
- **The page-1 header at database creation.** A database that has never been
  written to is present and zero length; after a write that has been folded
  into the file, the file carries the SQLite header magic, the page-size and
  version bytes `spec/PROFILE.md` rule 1 fixes, and an in-header page count
  the schema and the data determine — R11.4, R11.7.
- **`generate_series`.** The first table-valued function in this release
  chain: usable in a `FROM` clause and as the source of an `INSERT ...
  SELECT`, returning the same rows as stock `sqlite3` for the same bounds —
  R11.5, a negative stride included. It is an *eponymous* virtual table and
  the column it projects is named `value`: `spec/sqlite/vtab.html`, in this
  step's reading list, gives the schema as `CREATE TABLE
  generate_series(value, start HIDDEN, stop HIDDEN, step HIDDEN)`, and every
  case in the group selects that name. The previous checkpoint refuses the
  function as an unknown table-valued function.
- **Result framing in `-m list`.** Result blocks are no longer framed by blank
  lines, statements that return no rows write nothing at all, and NULL,
  zero-length text and blob values render as `spec/PROFILE.md` rules 4 and 5
  describe — R11.3, R11.8, R11.9, R11.10.

Nothing outside that list is new at this step, and nothing outside it is in
scope here except what the gate groups re-run from earlier milestones.

## Which group covers which requirement

Every requirement group at this step exists for rows of the behaviour contract
below, and every row has a group. Nothing here is in scope that the table does
not name.

| Group | Requirements it covers | What it observes |
|---|---|---|
| `s11-binary-rename` | R11.1, R11.2, R11.6 | whether anything at all — a file, a copy, a hard link or a symlink, executable or not — sits beside the installed path under the retired name `limbo`; that the shell at that path answers a SQL script fed to it on standard input; and whether the installed name appears anywhere in what `--help` writes to standard output while the retired name appears nowhere in it in any capitalisation |
| `s11-database-creation` | R11.4, R11.7 | the byte length of a database file after a session that only read, that the file is nevertheless present, and what stock `sqlite3` makes of it — its `sqlite_schema` row count and `PRAGMA integrity_check`; then, after a write folded into the file by `PRAGMA wal_checkpoint`, the header magic, the page-size bytes at offset 16, the write and read version bytes at offsets 18 and 19, the in-header database size at offset 28, and the rows stock `sqlite3` reads back from the same file |
| `s11-table-valued-functions` | R11.5 | the rows `generate_series` yields, under the column name `value`, for an ascending range, an explicit stride, negative bounds, a start past its stop, and under `count()` and `sum()`; and the same function as the source of an `INSERT ... SELECT`, a descending stride and a one-value range among the sources, with the tables they leave behind read back. The two `.sql` cases in this group are cross-checked against stock `sqlite3` row for row before they are accepted into the corpus |
| `s11-shell-output-framing` | R11.3, R11.8, R11.9, R11.10 | the exact bytes `-m list` writes for a multi-statement script, taken verbatim with nothing filtered: that no blank line frames a result block; that `CREATE TABLE`, `INSERT`, `UPDATE`, `DELETE`, `DROP TABLE` and a `SELECT` matching nothing contribute no bytes at all; that a NULL and a zero-length text value both render as an empty field with only `typeof()` separating them; and that a non-empty blob renders as its raw bytes between `\|` separators. Each case also runs the identical script through stock `sqlite3 -batch -list -separator '\|' -noheader` and reports whether the two outputs are byte-identical |

`s11-binary-rename`, `s11-database-creation` and `s11-table-valued-functions`
are the **new** groups; `s11-shell-output-framing` is **peripheral**.

## What this step re-checks from earlier work

The gate groups are not in that table because they add no new requirement and
only veto.
Beside `gate-proof-of-life`, which only starts the installed shell and asks it
`SELECT 1;`, this step's inherited checkpoint is ten cases: `gate-01` through
`gate-10`, one per earlier milestone, each re-run unchanged from the step that
introduced it. The gate table above names the group, the case file and what
that case re-runs, one row each. Those eleven groups are the whole of what
this step re-checks from earlier work — there is no hidden inherited suite,
and no earlier behaviour outside those eleven cases is in scope here. If any of
them fails, this step is not satisfied whatever else passes, and
`reward_details.json` names the group and the case that vetoed it.

## File size, page count and help text at this step

Three requirements here touch things the "Out of scope" list below mentions.
Each is in scope for a stated reason rather than in spite of the list.

- **R11.4 requires a zero length, not a byte count.** The "file size in bytes"
  entry frees you from matching stock `sqlite3`'s byte count for a file that
  holds pages: how you pack cells, when you defragment, and in what order you
  reuse free pages are yours. R11.4 sits at the boundary before that freedom
  begins — whether *any* page has been written at all. A session that only
  read must leave the file at zero length, which is a fact about your write
  path and not about your packing. The case also checks the file is present,
  because opening a path that does not exist still creates it.
- **R11.7 requires the page count the schema and the data force, not the file's
  length.** Page 1 holds the header and `sqlite_schema`, each table gets one
  root page, and a table whose rows all fit in that root page needs no further
  page — so the one small table the case creates makes a two-page database
  under any conforming packing, and the in-header database size at offset 28
  reads 2. The case asks for `PRAGMA wal_checkpoint` before it reads the
  header, because `spec/PROFILE.md`'s durability rule lets a conforming engine
  leave committed frames in the log, and the checkpoint is the only way to
  *require* the fold. Nothing in this group asserts how many bytes long
  `main.db` is.
- **R11.6 checks two names, not help text.** The "wording of any diagnostic,
  log line or help text" entry stands: no case here compares the phrasing, the
  order, the layout, or the option list of `--help`. What R11.6 compares is
  whether the string `tursodb` appears somewhere in that output and whether
  the retired name appears anywhere in it **in any capitalisation**: the case
  rejects `limbo`, `Limbo` and `LIMBO` alike, so renaming the executable while
  leaving a product name like "The Limbo interactive SQL shell" in the `about`
  line fails it. Any help text that names the program you installed and does
  not name the retired one, in any case, passes whatever else it says; the
  `--help` row in the command-line table above states what this step needs
  from it.

## What this milestone does not include

Each exclusion is a measurement rather than an argument: what follows the
dash is what was observed when the thing was tried against this step's two
readers, the engine and the pinned stock `sqlite3` 3.50.2.

- **The one-argument `generate_series(<start>)`** — not in scope, because the
  two readers do not agree on it. At this checkpoint
  `SELECT count(*) FROM generate_series(5);` answers `0`; stock enumerates
  with no upper bound and was still running when a 10-second timeout killed
  it. R11.5 is stated for the two- and three-argument forms, and no case uses
  any other.
- **The hidden-column call form** — `spec/sqlite/vtab.html` also shows
  `SELECT value FROM generate_series WHERE start=1 AND stop=3;`, driving the
  `HIDDEN` columns as inputs. Not in scope: stock returns the three rows and
  this checkpoint does not resolve the bare name, so there is no expected
  output both readers would produce. Implement it or do not; no case looks.
- **A zero stride** — both readers answer `5` to
  `SELECT count(*) FROM generate_series(1,5,0);`, so they agree, but no
  requirement here states the rule and no case asserts it. Out of scope at
  this step rather than silently assumed.
- **The `--version` banner** — R11.6 is about `--help` and nothing else. At
  this checkpoint `--version` still prints `limbo 0.1.0`: the banner lags the
  rename, so a check on it would require something no requirement states. No
  case reads it.
- **The byte length of `main.db` after a write** — measured and deliberately
  dropped; see "File size, page count and help text at this step" above. Only
  the zero length of a never-written file and the in-header page count at
  offset 28 are in scope.
- **Where your `generate_series` lives** — a built-in, an extension, a
  separate crate: not looked at. The "Out of scope" list below says so, and no
  case reads a source file.

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
| R11.1 | The shell is installed under the name `tursodb` and runs from that path. | installed binary path |
| R11.2 | No binary named `limbo` is installed at this checkpoint, the name having been retired by the rename. | installed binary path |
| R11.3 | The shell's stdout for a multi-statement script contains only result rows with no blank separator lines between result blocks, so the output is comparable byte-for-byte to another reader's rendering of the same rows. | result rows |
| R11.4 | A read-only session writes no pages to the main database file (the case compares the main file's bytes only; `spec/PROFILE.md` also asks that `main.db-wal` be left byte-identical, and no case at this step reads the log). Opening a database path that does not exist still creates the file, so a database that has never been written to is present and zero length after the session, and stock `sqlite3` recognises it as an empty database. | database file bytes |
| R11.5 | The `generate_series(<start>,<stop>[,<step>])` table-valued function enumerates integers from start to stop inclusive and returns the same rows as stock `sqlite3` for the same bounds, a negative stride — which counts down — included. Its rows are projected as a single column named `value`: that is the eponymous virtual table's schema, `CREATE TABLE generate_series(value, start HIDDEN, stop HIDDEN, step HIDDEN)`, which `spec/sqlite/vtab.html` gives. | result rows |
| R11.6 | The installed shell identifies itself by the name it is installed under: the string `tursodb` appears somewhere in what `--help` writes to standard output, and the retired name `limbo` appears nowhere in it **in any capitalisation** — `Limbo` and `LIMBO` are rejected with it, so the product name has to go and not only the lower-case executable name. Only the presence of the two names is compared — never the wording, order or layout of the help text. | `--help` output |
| R11.7 | The page-1 header is written when the first write needs it. After a write that has been folded into the main database file by `PRAGMA wal_checkpoint`, the file carries the header magic `SQLite format 3` and the page size and version bytes `spec/PROFILE.md` rule 1 fixes, and its in-header database size — the four-byte big-endian field at page-1 offset 28, which `spec/sqlite/fileformat2.html` names and locates — counts the pages the schema and the data actually occupy: page 1 holds the header and `sqlite_schema`, each table gets one root page, and a table whose rows all fit in that root page needs no further page — so one small table makes a two-page database. | database file bytes |
| R11.8 | A statement that returns no rows contributes no bytes to standard output. `CREATE TABLE`, `INSERT`, `UPDATE`, `DELETE`, `DROP TABLE` and a `SELECT` that matches nothing each print nothing at all: no header, no placeholder, no count line and no separator line, so the next statement's first row lands on the next line. | result rows |
| R11.9 | A NULL and a zero-length text value both render as an empty field, so a single-column row holding either is an empty line and only `typeof()` tells the two apart. | result rows |
| R11.10 | A non-empty blob renders as its raw bytes and columns are separated by `\|`, so a blob holding printable bytes is indistinguishable in the rendered row from the text of those bytes, and `typeof()`, `length()` and `hex()` are what distinguish them. | result rows |

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
