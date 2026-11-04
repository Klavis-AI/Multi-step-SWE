# Test interface — step 12

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

### Requirement groups

| Group |
|---|
| `s12-schema-and-diagnostic-text` |
| `s12-refusal-surface` |
| `s12-schema-cookie` |
| `s12-vector-distance` |
| `s12-vector-storage` |

`s12-vector-distance` and `s12-vector-storage` cover the capability this step
introduces; `s12-schema-and-diagnostic-text`, `s12-refusal-surface` and
`s12-schema-cookie` cover breadth at this step. Nothing in scope here is
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
`s12-refusal-surface` is the group that observes it: `R01.16`, introduced and
stated at step 01, remains an obligation on your engine here, and it is not a
new requirement of this milestone.

| Group | Requirements it covers | What it observes |
|---|---|---|
| `s12-vector-storage` | R12.3, R12.7, R12.8, R12.9, R12.11 | the `typeof`, `length` and `hex` of a vector column at each element width and at the empty boundary; the text `vector_extract` renders a stored vector back as; and, through stock `sqlite3` reading the same file, `PRAGMA integrity_check`, the same bytes and length for the same column, the `.dump` text, and the bytes a fresh database re-imported from that dump holds |
| `s12-vector-distance` | R12.3, R12.4, R12.5, R12.10, R12.11 | the value `vector_distance_l2` returns for pairs whose distance is fixed by arithmetic alone, at both element widths; the refusal of a call between different dimensions and of a call mixing the two widths; the stored length and extracted text of a one-element and of an eight-element vector; and which rows an `ORDER BY vector_distance_l2(…) LIMIT k` returns, in which order |
| `s12-schema-cookie` | R12.1, R12.2 | what `PRAGMA schema_version` answers on a database with no schema, after each `CREATE TABLE` and after an `INSERT` -- the sequence stock `sqlite3` produces for the same script, pinned as this group's expected output, with a `.sh` sibling asking stock for the same pragma on the file this engine wrote; and bytes 40..43 of the page-1 header, read out of the file with `od` after each schema change |
| `s12-schema-and-diagnostic-text` | R12.6 | the `sqlite_schema.sql` text after `ADD COLUMN`, after `DROP COLUMN` and after `RENAME COLUMN`, beside the text stored for a table created outright with the same columns |
| `s12-refusal-surface` | R01.16, inherited from step 01 | an invocation given a command-line argument the shell cannot use exits `2`, writes nothing at all to standard output and leaves no database file behind, while the same statement stream under a usable argument still runs and still answers |

The gate groups are not in that table because they add no new requirement.
Beside `gate-proof-of-life`, which only starts the installed shell and asks it
`SELECT 1;`, this step's inherited checkpoint is eleven cases: `gate-01`
through `gate-11`, one per earlier milestone, each re-run unchanged from the
step that introduced it. The gate table above names the group, the case file
and what that case re-runs, one row each. Those twelve groups
are the whole of what this step re-checks from earlier work — there is no
hidden inherited suite, and no earlier behaviour outside those cases is in scope
here. If any of them fails, this step is not satisfied whatever else passes, and
`reward_details.json` names the group and the case.

## Vector values at this checkpoint

`spec/turso/COMPAT.md` lists the vector functions this release supports but
does not describe the values they produce, and `spec/PROFILE.md` rule 9 says
only that stock `sqlite3` is not a second opinion on them. The part the
verifier depends on is therefore stated here, and it is what R12.3, R12.4,
R12.5, R12.10 and R12.11 require:

- A vector is an ordinary SQLite **blob** value. `typeof()` answers `blob`, it
  is stored in a normal column, it round-trips through the file format
  unchanged, and stock `sqlite3` reading the same database sees a blob of the
  same bytes and the same `length()`. No new serial type, column type or page
  type is introduced.
- **Float32 is the default width.** `vector(x)` and `vector32(x)` are the same
  constructor: each element is stored as four raw little-endian IEEE-754
  single-precision bytes, in order, with **no marker byte**. So
  `length(vector('[1,2]'))` is `8`, `hex(vector32('[1,2]'))` is
  `0000803F00000040`, and a one-element float32 vector is four bytes long.
- **Float64 carries a trailing marker.** `vector64(x)` stores each element as
  eight little-endian double-precision bytes followed by a single `02` byte,
  so `length(vector64('[1,2]'))` is `17` — sixteen bytes of elements plus the
  marker — and `hex(vector64('[1,2]'))` is
  `000000000000F03F000000000000004002`.
- **An element is a number, integral or fractional.** The constructors take the
  bracketed comma-separated text form, and an element carrying a fraction is
  in scope -- `vector('[0.5,-0.5]')` among others -- so a reader that accepted
  only integer elements is not enough.
- **Stored vectors are reached by ordinary statements.** The cases feed them
  with the surface earlier steps introduced, `INSERT ... SELECT` among it, so
  nothing here needs a path of its own.
- **The empty vector is the boundary of that arithmetic.** `vector('[]')` is
  zero bytes long; `vector64('[]')` is one byte long, the marker alone. Both
  lengths are in scope.
- `vector_extract(v)` is the inverse of the constructors: it returns the text
  form of a vector blob — the elements in order, comma-separated, inside
  square brackets, **with no spaces** — and an element whose value is integral
  is rendered **without a fractional part**. `vector('[1,2]')` extracts back
  as `[1,2]`, not as `[1.0, 2.0]`. It renders a vector read out of a column
  exactly as it renders one built inline.
- `vector_distance_l2(a, b)` returns the Euclidean distance between two
  vectors as a REAL. It is defined **between two vectors of the same element
  width and the same dimension only**. A call between vectors of different
  dimensions is refused, and so is a call mixing a float32 vector with a
  float64 vector — even where the two have the same number of elements. The
  refusal is an error report in the sense of `PROFILE.md` rules 6 and 7: it
  produces no row, the batch keeps running, and its wording is not compared.
- **How a distance is compared.** An integral REAL renders with a single
  trailing `.0`, as stock `sqlite3`'s `-list` output does, so a distance of
  five is the field `5.0` and not `5`. Where a distance is *not* exactly
  representable, no test compares its full-precision rendering: it is compared
  either through an ordering, or through `round(d, 6)`. The width at which an
  engine accumulates the sum of squared differences is deliberately **not**
  fixed by this document, and every rounded comparison in this step is over a
  pair whose squared differences sum to an exactly representable integer — 2,
  5, 25 and 204 — so the value asserted at six decimal places is the same
  whichever width the sum is accumulated at.

## `PRAGMA schema_version` overrides the matrix

`spec/turso/COMPAT.md` at this release lists `PRAGMA schema_version` as `No`.
It is implemented at this checkpoint and R12.1 checks what it answers. As the
note introducing the matrix says, where the matrix and this step's requirement
list disagree the requirement list wins: the matrix records what upstream
claimed at the tag, and a candidate who read it as the statement of scope
would leave an in-scope requirement unimplemented.

## Where the `ALTER TABLE` forms R12.6 reads come from

R12.6 names `ADD COLUMN`, `DROP COLUMN` and `RENAME COLUMN`, and none of the
three is new here: all three arrive at step 10, under that step's R10.3, R10.5
and R10.2, and each is already in scope there for what it does to the rows, to
the column list `pragma_table_info` reports and to what stock `sqlite3` reads
out of the file. What is new at this checkpoint is only the **spacing** of the
text the rebuild leaves in `sqlite_schema.sql`: at step 10 `ADD COLUMN` and
`DROP COLUMN` rebuilt it with a space just inside each parenthesis, contrary
to `spec/PROFILE.md` rule 2, and step 10 deliberately reads no `sql` text back
after either. R12.6 is that correction and nothing more — the three forms
themselves must keep working, but they are inherited work, not new work.

## What this milestone does not include

Five things a reader might reasonably expect from a step called "vector
values" are **not** in scope here. Each absence was measured against the release
chain on 2026-09-21 rather than decided by argument; do not treat any of them
as a defect.

- **Cosine distance.** `vector_distance_cos` is not new at this checkpoint —
  it and the vector constructors already existed, which is why
  `vector_distance_l2` is the only new name in the list above. Its values are
  in scope at step 02, under that step's R02.11 and R02.12, and no case here
  calls it.
- **The text form of an empty vector.** Both empty *lengths* are in scope, at
  both widths. The *rendering* is not: at this checkpoint `vector_extract` of
  an empty float64 vector terminates the shell instead of returning a value,
  so there is no behaviour to fix here and no case asks for one. Nothing in
  this step depends on what `vector_extract(vector('[]'))` returns.
- **Any index over a vector column.** R12.5 checks which rows an
  `ORDER BY vector_distance_l2(…) LIMIT k` returns and in what order, not how
  the engine arrives at them. No case at this step creates an index on a
  vector column, none runs `EXPLAIN`, and no approximate or specialised
  nearest-neighbour structure is required: a full scan that sorts correctly
  passes. The order is pinned by the distance and then by `id`, so no two rows
  can tie into an unspecified order.
- **`DROP TABLE` moving the schema cookie.** At this checkpoint it does not
  move the cookie, and from `v0.1.3` it does. A case pinning either answer
  would be wrong on one side of that change, so the cookie cases use
  `CREATE TABLE` and `INSERT` only, and R12.1 is stated in those terms.
- **The wording of any refusal.** R12.4's negative half and R12.10 assert that
  a refusal landed at that point in the stream, produced no rows of its own,
  and left the rest of the batch running. Every line carrying the shell's `×`
  marker is folded to the single word `ERROR` before comparison, exactly as
  the `error report` observable in `TEST_INTERFACE.md` describes, so no
  diagnostic text, error code or position is compared.

One obligation the list above does not cover is the process-level rejection of
`spec/PROFILE.md` rule 8, and it is checked here rather than left to another
milestone: `s12-refusal-surface` invokes the shell with an argument it cannot
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
| R12.1 | The page-1 schema cookie starts at zero on a database with no schema and increments by one per `CREATE TABLE`, holding the same values as stock `sqlite3` does for the same script. | result rows |
| R12.2 | Bytes 40..43 of the page-1 header hold the schema cookie and move with each schema change, so a reader reading only the header sees the same value `PRAGMA schema_version` reports. | database file bytes |
| R12.3 | A float32 vector — what `vector(x)` and `vector32(x)` return — is stored as four raw little-endian bytes per element with no marker, and a float64 vector — `vector64(x)` — as eight little-endian bytes per element followed by a single `02` marker byte, so an empty float64 vector is one byte long and an empty float32 vector is zero bytes long. | result rows |
| R12.4 | `vector_distance_l2` returns the Euclidean distance between two vectors, and a call between vectors of different dimensions is reported as an error rather than returning a number. | result rows |
| R12.5 | An `ORDER BY vector_distance_l2(…) LIMIT k` query over a table of vectors returns the k nearest rows in increasing distance order. | result rows |
| R12.6 | `ALTER TABLE` — `ADD COLUMN`, `DROP COLUMN` and `RENAME COLUMN` alike — rebuilds the stored schema text with the same spacing `spec/PROFILE.md` rule 2 gives a table created directly: no space inside the parentheses, one space after each comma. | result rows |
| R12.7 | Stock `sqlite3` opening a database containing vector values returns `ok` from `PRAGMA integrity_check`. | stock sqlite3 reading the file |
| R12.8 | Stock `sqlite3`'s `.dump` of a table containing vector values emits the values as blob literals that re-import to the identical bytes. | stock sqlite3 reading the file |
| R12.9 | A reader that does not know the vector type sees a vector column's value as an ordinary blob rather than an error. | stock sqlite3 reading the file |
| R12.10 | `vector_distance_l2` is defined between two vectors of the same element width only: a call that mixes a float32 vector with a float64 vector is reported as an error rather than coerced to a common width, even where the two have the same number of elements. | error report |
| R12.11 | `vector_extract(v)` returns the text form of a vector blob — the elements in order, comma-separated, inside square brackets, with no spaces — and an element whose value is integral is rendered without a fractional part, so `vector('[1,2]')` extracts back as `[1,2]`. | result rows |

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
