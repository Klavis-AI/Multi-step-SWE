# Test interface — step 02

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
cd <empty dir> && /app/bin/limbo -m raw -q main.db < case.sql
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
| `CLI_MODE` | `raw` |
| `CASEDIR` | the directory holding the case |

Standard output is captured; standard error is ignored, exactly as it is for a
SQL case. **No case ever merges the two streams of the engine under test into
output it compares**: the error report that `PROFILE.md` rule 6 describes
arrives on standard output, and a case that folded your standard error in
would pass a build that had put the report on the wrong stream. An engine may
write anything it likes to standard error -- progress, warnings, a backtrace
on a panic -- and nothing that is compared reads it.

Where a shell case does redirect `2>&1`, it is on the **stock** `$SQLITE3`
fixture and never on the engine under test. That fixture is a pinned 3.50.2
binary and not the thing being checked, and folding its standard error into the
captured line is the opposite of a loosened assertion: a stock failure then
lands in the compared output and fails the case loudly. A case that instead
sends stock's standard error to `/dev/null` is asserting only on the rows
stock returns, so a stock failure there fails the case on the rows that did
not arrive rather than on any text. Read a `2>&1` in a case by which binary it
is attached to.


Every case is run under a 120-second timeout. A case that is killed by it
fails.

## Command-line surface the harness uses

| | |
|---|---|
| `-m <mode>` | select result rendering. This step passes `-m raw`. The harness always passes it explicitly, so your default may be anything. |
| `-q` | quiet: no banner, no prompt, no timing line, nothing on standard output but result rows and the error reports `spec/PROFILE.md` rule 6 describes. |
| *positional* | the database file path. It may not exist yet, in which case it is created. |

In `raw` mode a result row is one line, columns separated by `|`, no
header row, no alignment padding, and NULL rendered as the empty string. The
shell adds no line break of its own: a newline *inside* a text value is written
through, which is what `json_pretty`'s default layout does (`s02-json` checks
that text byte for byte).

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

### Requirement groups

| Group |
|---|
| `s02-json` |
| `s02-query-completion` |
| `s02-vector-functions` |
| `s02-wal-checkpoint` |

`s02-json`, `s02-vector-functions` and `s02-wal-checkpoint` cover the
capability this step introduces; `s02-query-completion` covers breadth at this
step. Nothing in scope here is satisfiable by an earlier step's work alone.

The harness reads `tests/assets/groups.txt`, never this table; if the two ever
disagree, the file is what the harness follows.

This step's requirements are independent of every other step's.

## Which group covers which requirement

Every requirement group at this step exists for rows of the behaviour contract
below, and every row has a group. Three of the four requirement groups cover
more than one row, so a group is not a requirement: it is a set of cases, and
every case in it must pass. Nothing here is in scope that the table does not
name.

| Group | Requirements it covers | What it observes |
|---|---|---|
| `s02-wal-checkpoint` | R02.1, R02.2, R02.3, R02.4 | the checkpoint's own result triple; a battery of queries run immediately before and immediately after the checkpoint in one connection and compared; then `main.db-wal` and `main.db-shm` are deleted and stock `sqlite3` is asked for `integrity_check`, the schema and every row |
| `s02-json` | R02.7, R02.8, R02.9, R02.10 | the text each JSON function returns, `json_pretty`'s layout byte for byte, `json_valid` over malformed text, path syntax and the refusal of a malformed path, and the coercion of integer, real, NULL, JSON-literal and blob arguments |
| `s02-vector-functions` | R02.11, R02.12 | what the vector functions return, and what stock `sqlite3` sees in a file that holds vector values |
| `s02-query-completion` | R02.13 | the rows each rowid range returns, and the aggregate over the same range |

## What this step re-checks from earlier work

The gate groups are not in that table because they add no new requirement. Beside
`gate-proof-of-life`, which only starts the installed shell and asks it
`SELECT 1;`, this step's inherited checkpoint is one case: `gate-01`, a
step-01 case re-run unchanged. It creates a table, inserts five rows, and asks
for them back through `WHERE`, `ORDER BY` and `count()` under `LIKE` patterns
that put `%` and `_` in every position, comparing the result rows byte for
byte in the rendering described above. Those two are the whole of this step's
inherited re-check: there is no hidden inherited suite, and no earlier step's
behaviour is in scope here *as an inherited obligation* beyond that one case.
(This step's own groups do of course run statements step 01 introduced —
`CREATE TABLE`, `INSERT`, `SELECT` with `ORDER BY`, `GROUP BY` and aggregates —
because that is how this step's own requirements are observed; none of those is
in scope as a re-check of step 01.) If either of the two groups above fails, this
step is not satisfied and `reward_details.json` names the group and the case.

## Vector values at this checkpoint

`spec/turso/COMPAT.md` lists the vector functions this release added but does
not describe the values they produce, and `spec/PROFILE.md` rule 9 says only
that stock `sqlite3` is not a second opinion on them. The part the verifier
depends on is therefore stated here, and it is what R02.11 and R02.12 check:

- A vector is an ordinary SQLite **blob** value. `typeof()` answers `blob`, it
  is stored in a normal column, it round-trips through the file format
  unchanged, and stock `sqlite3` reading the same database sees a blob of the
  same bytes and the same `length()`. No new serial type or column type is
  introduced.
- `vector(t)` takes the text form `'[1,2,3]'` and returns that blob at 32-bit
  element width: **four bytes per element**, so `length(vector('[1,0]'))` is
  `8` and `length(vector('[1,2,3]'))` is `12`.
- `vector_extract(v)` is the inverse: it returns the text rendering of a
  vector blob, in the same bracketed comma-separated form, and an element
  whose value is integral renders without a fractional part — `vector('[1,2]')`
  extracts back as `[1,2]`, not as `[1.0,2.0]`.
- `vector_distance_cos(a, b)` returns a real: one minus the cosine of the
  angle between the two vectors. The three values the definition fixes exactly
  are the ones checked — `0.0` for identical vectors, `1.0` for orthogonal
  ones, `2.0` for opposed ones. No inexact distance is compared as a decimal
  rendering.

## Where `spec/turso/COMPAT.md` disagrees with this step

The matrix describes the release at that tag; the requirement list is the
contract. Two rows disagree with what this step requires, and in both the
requirement list wins:

| `COMPAT.md` says | This step requires |
|---|---|
| `(NOT) BETWEEN ... AND ...` — `No` | `BETWEEN` and descending ranges, under R02.13 |
| `vector32(x)`, `vector64(x)` — `Yes` | out of scope here: R02.11 and R02.12 name `vector`, `vector_extract` and `vector_distance_cos`, and no case at this step asserts either width-specific variant |

## What this milestone does not include

Five things a reader might reasonably expect from a step called
"checkpointing and JSON" are **out of scope** here. Each has a measured reason
recorded against the release chain on 2026-09-21; do not treat any of them as
a defect.

- **The oldest-reader mark.** A checkpoint must not copy frames past the
  oldest reader's mark, and a reader open across a checkpoint must keep its
  snapshot. That needs two connections alive at once with a read transaction
  held open across the checkpoint, and neither half exists at this
  checkpoint: the shell reads one script per process and then exits, and the
  explicit transaction scope that would hold a snapshot open arrives at step
  03. What is checked instead is the single-connection half, R02.3: with
  nothing else reading, the checkpoint is not held back — `busy` is `0` and
  every frame in the log is copied.
- **The read side of the WAL.** "A read-only `SELECT` appends no frames and
  leaves the `-wal` bytes unchanged", and "a write committed before a
  simulated crash survives the reopen", are obligations on your engine —
  `spec/PROFILE.md`'s durability rule states both, and every shell case at
  this step writes in one invocation and reads in another, so a build that
  breaks either fails those cases from the side. They are not requirement
  rows here because they were already true at the previous checkpoint and are
  in scope at step 01. What this release changed is that a read stops *fsyncing*
  the log, which does not cross the process boundary at all.
- **Blobs that hold JSONB.** Stock `sqlite3` reads a blob argument to a JSON
  function as JSONB, its binary representation, and answers from it. That
  representation arrives at step 05. At this step R02.10's blob half is about
  a blob that is *not* valid JSON: `json_object` and `json_array` refuse it,
  as stock `sqlite3` does, and `json_valid` answers `0`.
- **The frame counts inside the checkpoint triple.** R02.1 and R02.3 check the
  triple's *shape* — one row, three integer columns — and the two invariants a
  single connection can observe: `busy` is `0`, and the second and third
  columns are equal. The counts themselves are out of scope, and you are free to
  report whatever your checkpoint actually copied. Measured over the release
  chain on 2026-09-21, this family answers `0|0|0` at the checkpoint this step
  is cut from and non-zero counts at later ones, so no case can require a
  non-zero count. What carries R02.1's "the frames reach the main database
  file" half is R02.4 instead — the `-wal` and `-shm` files are deleted and
  stock `sqlite3` still finds every row — and a second case in which stock
  `sqlite3` reads the rows back from the file your engine wrote.
- **Duplicate keys in one `json_object` call.** `json_object('a',1,'a',2)` is
  the one place in this step's JSON surface where this release and stock
  `sqlite3` do not agree, and it moves again later in the chain, so no case
  passes a duplicate key and R02.7 says in its own text that it is out of scope
  here. Either answer is accepted.

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
| R02.1 | `PRAGMA wal_checkpoint` copies WAL frames into the main database file and returns SQLite's result triple in the same shape stock `sqlite3` returns: one row of three integer columns. | result rows |
| R02.2 | A checkpoint is observationally invisible: every query run before it returns the identical result set after it. | result rows |
| R02.3 | With no other connection reading, nothing holds the checkpoint back: the triple's first column — SQLite's `busy` flag — is `0`, and the number of frames it copied equals the number of frames the log held, so the two remaining columns are equal. Both count columns render as plain non-negative decimal integers: the `-1` the format documents for a database with no write-ahead log cannot obtain here, because each case that reads the triple has just written in the same invocation. | result rows |
| R02.4 | After a checkpoint, stock `sqlite3` reading the `.db` file alone — with `main.db-wal` and `main.db-shm` deleted — sees every checkpointed row and returns `ok` from `PRAGMA integrity_check`, and the same stock read of `sqlite_schema` lists exactly the tables the script created — the engine writes no schema row of its own beyond them. | stock sqlite3 reading the file |
| R02.7 | `json_object`, `json_patch`, `json_remove` and `json_pretty` return exactly the text stock `sqlite3`'s JSON functions return for the same arguments, including `json_pretty`'s four-space-per-level default indentation and the layout its optional second argument selects instead. Two keys of the same name in one `json_object` call are out of scope at this step. | result rows |
| R02.8 | `json_valid` returns `0` for malformed JSON text instead of raising an error. | result rows |
| R02.9 | A JSON path argument — the second argument to `json_extract` — follows SQLite's `$.a[0]` syntax — an array subscript after an object key, nesting to any depth, and `$[0]` at the root — and a malformed path is refused rather than silently ignored: the statement yields no row, and the statements after it still run. A path that is well formed but selects nothing is not malformed: it is not refused, and it answers `NULL`. | result rows |
| R02.10 | A JSON function given a non-text argument applies SQLite's exact argument coercion: an integer or a real is emitted as a JSON number and never as a quoted string, `NULL` propagates, the JSON literals `true`/`false`/`null` keep their own types, and a blob that is not valid JSON is refused by `json_object` and `json_array` and answers `0` from `json_valid` — each of them the answer stock `sqlite3` gives. The functions this row covers are `json_object`, `json_array`, `json`, `json_type`, `json_valid`, `json_extract` and `json_array_length`, and `json_type` names what it finds with SQLite's own vocabulary — `true`, `false`, `null`, `integer`, `real` and `text`. | result rows |
| R02.11 | The vector functions introduced at this checkpoint are callable and return a value rather than an unknown-function diagnostic, and the values are the ones the definitions fix: `vector(t)` is a blob, `vector_extract` returns the bracketed comma-separated text the vector was built from, and `vector_distance_cos` answers `0.0` for identical vectors, `1.0` for orthogonal ones and `2.0` for opposed ones. | result rows |
| R02.12 | A vector value is an ordinary blob in an ordinary column: it survives the process that wrote it, it occupies four bytes per element, and stock `sqlite3` — which has never heard of the vector functions — opens the same file, reports the column as a blob and agrees about its length. | stock sqlite3 reading the file |
| R02.13 | A rowid range is seeked rather than mis-planned: `>`, `>=`, `<=`, `<`, `BETWEEN …` in ascending order and in descending order (`ORDER BY … DESC`) and `IN (…)` each return the rows stock `sqlite3` returns, each range agrees with the aggregate taken over the same range, and a range whose ends cross selects nothing. | result rows |

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
