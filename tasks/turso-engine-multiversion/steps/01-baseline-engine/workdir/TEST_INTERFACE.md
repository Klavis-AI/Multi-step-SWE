# Test interface — step 01

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
| `-m <mode>` | select result rendering. This step passes `-m raw`. The harness always passes it explicitly, so your default may be anything. |
| `-q` | quiet: no banner, no prompt, no timing line, nothing on standard output but result rows and the error reports `spec/PROFILE.md` rule 6 describes. |
| *positional* | the database file path. It may not exist yet, in which case it is created. |

In `raw` mode a result row is one line, columns separated by `|`, no
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

### Requirement groups

| Group |
|---|
| `s01-ddl-and-schema` |
| `s01-dml-and-select` |
| `s01-file-interop` |
| `s01-shell-surface` |
| `s01-wal-durability` |

`s01-ddl-and-schema`, `s01-dml-and-select` and `s01-file-interop` cover the
capability this step introduces; `s01-shell-surface` and `s01-wal-durability`
cover breadth at this step. Nothing in scope here is satisfiable by an earlier
step's work alone.

The harness reads `tests/assets/groups.txt`, never this table; if the two ever
disagree, the file is what the harness follows.

This step's requirements are independent of every other step's.

## What this milestone does not include

This step's requirement list is the whole of the milestone. Six things a
reader might reasonably expect from the words "baseline engine" are **not**
in scope here. Each has a measured reason, and for four of them the release
chain is that reason. Do not treat any of their absences as a defect:

- **`UPDATE` and `DELETE` statements.** R01.2's text names them, but what this
  step covers is `INSERT` and `SELECT`. `UPDATE` is a parse error at this
  checkpoint and `DELETE` is accepted without deleting anything. Nothing here
  checks either, and nothing here checks R01.2's full statement list.
- **`CREATE INDEX` and index scans.** R01.7 is about a primary-key or rowid
  lookup agreeing with a full table scan. Secondary indexes arrive later in
  the chain; nothing at this step creates one.
- **A byte-identical replay of a write workload.** No case at this step runs
  the same write workload twice and compares the two files. (One case does
  compare bytes: `s01-wal-durability/readonly-select-appends-no-frames` digests
  `main.db` and `main.db-wal` before and after a *read-only* invocation and
  requires both to be unchanged. That is R01.11's second consequence, not a
  replay: nothing is written either time.) The obligation on the *engine* -- no
  opportunistic compaction of the whole file on free-page reuse -- is
  `spec/PROFILE.md` rule 3 and binds you whether or not it is checked, but no
  requirement in the list below states it, because nothing here observes it.
- **Free-page reuse order.** `spec/PROFILE.md` rule 3 also requires free pages
  to be reused last-in-first-out, and nothing at this step observes that
  either: `DELETE` is accepted at this checkpoint and deletes nothing, so no
  statement you can write frees a page. The rule still binds an
  implementation; the step that can check it is the step that brings a working
  `DELETE`.
- **Three particular negative integers: `-32769`, `-8388608` and
  `-2147483649`.** R01.4 names every serial type and is checked on every one of
  them; what is excluded is not a width but these three values, the ones the
  file format gives its 3-byte and 6-byte forms to. The reference engine at
  this checkpoint writes all three correctly -- stock `sqlite3` reads every one
  of them back exactly from the file this engine wrote -- but the engine's own
  reader sign-extends them wrongly, returning `16744447`, `8388608` and
  `281472829227007`, so there is no correct expected output to freeze against.
  Everything else round-trips and is checked: the positive 3, 4 and 6-byte
  forms, the negative end of the 4-byte range (`-2147483648`) and both ends of
  the 8-byte range (`-9223372036854775807` and `9223372036854775807`). The
  3- and 6-byte negatives are still binding on an implementation -- the
  profile's record-encoding rules say what a conforming writer stores -- they
  are simply not observed here.
- **`UNIQUE` constraints and `ON CONFLICT` clauses.** R01.14 requires a
  statement that would reuse a rowid already taken by a row of the same table
  -- the `PRIMARY KEY` of a rowid table -- to be refused under R01.13's
  contract, and that is the whole of what is checked: the conflicting row is
  not stored, the statements around it still run, and the file holds exactly
  the rows that were accepted. A `UNIQUE` column or table constraint is a
  different mechanism: enforcing one needs the secondary-index machinery that
  arrives later in the chain, so nothing at this step checks a `UNIQUE`
  violation and no case here writes a statement that would raise one. And how
  a candidate might be *asked* to resolve a conflict instead -- `ON CONFLICT
  REPLACE`, `IGNORE`, `DO UPDATE` -- is not in scope here either, and
  `spec/turso/COMPAT.md` marks that clause unsupported at this release.
  Refusing the duplicate rowid is the behaviour; the rest is not.

## Which group covers which requirement

Every requirement group at this step exists for rows of the behaviour contract
below, and every row has a group. Nothing here is in scope that the table does
not name.

| Group | Requirements it covers | What it observes |
|---|---|---|
| `s01-ddl-and-schema` | R01.1, first half | the `type`, `name` and `tbl_name` of each `sqlite_schema` row, and the `sql` text read back through this engine's own shell -- over five statements across two cases which between them cover every clause of `spec/PROFILE.md` rule 2 and do not overlap. One case takes the punctuation clauses: the space before a parenthesised list, the space after a comma, a parameterised type, a DEFAULT, a table constraint re-printed after the column list, and a binary operator inside a CHECK. The other takes the three clauses the first one cannot reach, because every statement in it is already single-line and already upper-case: runs of whitespace collapsed, so a statement written over several source lines reads back on one; keywords emitted upper-case, so `create table` reads back as `CREATE TABLE`; and identifiers and type names keeping the source's spelling, so `int` stays `int` and `VarChar` stays `VarChar` while the keywords around them go up |
| `s01-dml-and-select` | R01.2, R01.3, R01.7, R01.15 | the rows each query returns and their order, for `INSERT`/`SELECT` with `WHERE`, `ORDER BY`, `GROUP BY`, `LIMIT` and `LIKE` -- `LIKE` with the `%` and `_` wildcards and with ASCII case folding, `GROUP BY` with `count()` and `sum()`; `changes()` and `last_insert_rowid()` after each statement; a rowid seek and a full scan over the same table returning the same answer through `count()`, `sum()`, `min()`, `max()` and `ifnull()`; and scalar expressions with no table under them, through `typeof()`, `length()` and `abs()`. Every case in this group is a `.sql` case, so every line of every one is replayed against stock `sqlite3` 3.50.2 and compared |
| `s01-file-interop` | R01.1 second half, R01.4, R01.5, R01.6, R01.10 | what a foreign reader sees: stock `sqlite3` returning `ok` from `PRAGMA integrity_check` on a file this engine wrote and answering a query from the schema text it re-parsed out of it; **stock `sqlite3`'s own** `dbstat` table reporting a `leaf` page for a payload below the spill threshold and a `leaf` plus an `overflow` for one byte more (`dbstat` and `sha3()` are stock's, used here as the measuring instrument -- neither is an obligation on your engine); every serial type read back with its type and value; the two spill payloads written through your own shell as `zeroblob(n)`, which your engine must therefore provide; and the thirteen page-1 header fields, five of which `spec/PROFILE.md` rule 1 chooses by name and eight of which the format itself fixes |
| `s01-shell-surface` | R01.12, R01.13, R01.14, R01.16 | the shell's own surface: every byte of a rendered result row through `sed -n l`, the row built with `typeof()`, `length()` and `coalesce()`; where a refusal lands in the stream, that it produced no rows, and that the statements after it still ran; that a duplicate-rowid `PRIMARY KEY` conflict is refused, with `count()` and a stock re-read confirming the file holds exactly the accepted rows; and that an unusable argument exits `2`, writes nothing to standard output and leaves no database behind |
| `s01-wal-durability` | R01.11 | the three consequences the profile's durability rule states, one case each: a `-wal` still on disk after the writer exits with its rows visible to a later connection and no checkpoint in between; `main.db` and `main.db-wal` byte-identical across a read-only invocation; and a database plus `-wal` copied as they lie, without `main.db-shm`, yielding every committed row when opened |

## Functions the in-scope cases call through your shell

Every SQL function below is invoked through `/app/bin/limbo` by an in-scope case
at this step, so each one is an obligation here. They are listed because the
requirement rows name the behaviour and not always the spelling, and because
`spec/turso/COMPAT.md` is a record of what the release claimed rather than a
statement of what is in scope. This list is what is in scope.

| Function | Where |
|---|---|
| `count(*)`, `count(x)` | every group except `s01-ddl-and-schema`; row counts and the aggregate half of `GROUP BY` |
| `sum(x)` | `GROUP BY` totals, and the two access paths of R01.7 |
| `min(x)`, `max(x)` | the two access paths of R01.7, and the read-only WAL case |
| `ifnull(x, y)` | the two access paths of R01.7 |
| `coalesce(x, y)` | the rendered-row case of R01.12 |
| `typeof(x)` | R01.15's storage classes, the serial-type round trip, the rendered row |
| `length(x)` | R01.15, the serial-type round trip, the rendered row |
| `abs(x)` | R01.15 |
| `zeroblob(n)` | the two spill payloads of R01.5 |
| `changes()`, `last_insert_rowid()` | R01.3, after each statement |

`LIKE` is an operator rather than a function and is checked in the same place:
in a `WHERE` clause, with the `%` and `_` wildcards and with ASCII case
folding, which is the `LIKE` stock SQLite defines in `spec/sqlite/lang_expr.html`
and which this step checks over the `(NOT) LIKE` row of `COMPAT.md`.
`spec/sqlite/lang_corefunc.html` and `spec/sqlite/lang_aggfunc.html` define
every function in the table above; where the profile does not say otherwise,
stock's semantics are what is asserted, because the `.sql` cases are compared
against stock `sqlite3` 3.50.2.

**Three functions in the cases are NOT yours.** `sha3()`, `hex()` and
`group_concat()`, and the `dbstat` virtual table, appear only inside
invocations of the stock `sqlite3` binary the harness uses as a second reader.
Nothing in this step requires your engine to provide any of them.

## Where the expected output comes from

Every `.sql` case at this step is replayed against stock `sqlite3` 3.50.2 by
the corpus generator and is rejected if the two disagree, so those
expectations are pinned to stock SQLite rather than to any one
implementation's behaviour.

Two cases are deliberate exceptions, and they are the two in
`s01-ddl-and-schema`: the `sql` column they read back is this engine's
normalised rendering, which by construction cannot match stock's verbatim copy
of the source bytes. Their expected text is derived from `spec/PROFILE.md`
rule 2 clause by clause and from nothing else. That is why rule 2 is worth
reading closely rather than pattern-matching from one example, and it is the
only place at this step where "what stock does" is not also what is asserted.

**This step has no inherited checkpoint.** It is the first milestone, so there
is nothing earlier to re-check and no `gate-NN` group in its group list. The
only gate here is `gate-proof-of-life`, which the harness injects into every
step: it starts the installed shell and asks it `SELECT 1;`. If that fails the
step is not satisfied and `reward_details.json` names it. Later steps carry a
`gate-NN` group as well -- a case from an earlier milestone re-run unchanged --
and `s01-dml-and-select/like-predicates` is the case that becomes milestone
1's, which is why its expected output is re-measured at every later checkpoint
and must not change.

## Overflow pages

R01.5 and R01.6 are **in scope at this step**, in the `s01-file-interop`
group, and the profile's record-encoding rules are what they test.

A table-leaf cell holds its payload inline while it fits and spills the
surplus into an overflow chain past that point. At this engine's 4096-byte
page size the boundary is one byte wide: a row holding a 4057-byte blob
stays on the leaf and a row holding a 4058-byte blob does not. Stock
`sqlite3` splits at the same byte, and its `dbstat` table is where the split
is read off — for the smaller blob the table owns a single `leaf` page, and
for the larger one it owns a `leaf` page **and** an `overflow` page. Two
databases, one row each, and the difference between their page listings is
the threshold.

The requirement states the same threshold as arithmetic — a table leaf keeps
a cell whose payload is at most `usable-35` bytes, which is 4061 at a
4096-byte page. The two numbers agree, because the payload is the whole
record and not just the blob. The row the case writes is `(1, zeroblob(n))`
into `t(id INTEGER PRIMARY KEY, b BLOB)`: `id` is the rowid alias, so it is
stored as a null and costs one header byte and no body byte; the blob's
serial type is a two-byte varint at this size; the header length itself is
one byte. Four bytes of header, so a 4057-byte blob is a 4061-byte payload —
exactly the limit — and 4058 is 4062, one over. `dbstat`'s `payload` column
reports those sizes if you want to read them off the file.

The case does not assert the arithmetic; it asserts the step. Implement the
spill path so that a payload past the boundary allocates overflow pages, a
payload below it does not, and a spilled value comes back from stock
`sqlite3` byte for byte — which the case checks with a digest of the stored
bytes, not with their length.

`PRAGMA integrity_check` is asserted on the database whose payload is local,
and R01.6 says so: at this step the `ok` is checked on that file and not on
the file holding the chain. A build that stores and returns a spilled value
correctly therefore passes this group even if its page accounting on such a
file is not yet clean. The obligation to keep an overflow file clean is real
and R01.6 states it -- it is simply not what is observed here.

## Where `COMPAT.md` disagrees with this step

`spec/turso/COMPAT.md` records what the project claimed at this release. It
contradicts the requirement list in three places, and in each the list wins —
the matrix describes the release, the requirement list is the contract:

| `COMPAT.md` says | This step covers |
|---|---|
| `SELECT ... WHERE ... LIKE` — `Yes`; `(NOT) LIKE` — `No` | `LIKE` filters, wildcards and ASCII case folding, under R01.2 |
| `changes()` — `No` | `changes()` and `last_insert_rowid()` after each `INSERT`, under R01.3 |
| `binary operator` — `Partial`, "Only `%`, `!<`, and `!>` are unsupported" | the modulo operator on integers, under R01.15 |

The `LIKE` row is there because the matrix disagrees with itself: its
statement section marks `SELECT ... WHERE ... LIKE` supported and its
expression section marks `(NOT) LIKE` unsupported. The case checks `LIKE`
in a `WHERE` clause and does not use `NOT LIKE`.

The `binary operator` row is the awkward one, because it is `Partial` rather
than `No` and it carries a comment: it marks `%` unsupported while the list
covers `10%3`, `7%2` and `-7%2`. Read it as scoping the *rest* of that
comment — `!<` and `!>` are genuinely out of scope here and no case uses
them — and as an over-claim about `%`, which this release implements and
which R01.15 requires: modulo truncates toward zero like the division beside
it, so `-7%2` is `-1` and not `1`.

Note also that this section covers the rows whose status is worse than the
step's own scope. A row marked `Yes` that no case exercises is simply work
this milestone does not reach.

Nothing else in the matrix is overridden here: no row outside that table changes
what this step covers. A row marked `No` or `Partial` that is not named there
records a gap in the release at that tag — `CREATE TABLE` and `INSERT` are both
marked `Partial` there and both are in scope above — so a row the table does not
name is not a licence to skip something the requirement list asks for. What the
list asks for is the contract; the matrix is a description of the release.

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
| R01.1 | A `CREATE TABLE` statement is recorded in the `sql` column of its `sqlite_schema` row in the profile's normalised rendering, and stock `sqlite3` opening the file re-parses it to the same schema. Checked in two halves: the row is read back **through the engine's own shell** (there is no other way to see a rendering that is the engine's, not the format's), and the same file is then handed to stock `sqlite3`, whose `PRAGMA integrity_check` and `sqlite_schema` re-parse must agree. | result rows, then stock sqlite3 reading the file |
| R01.2 | A script mixing `INSERT`, `UPDATE`, `DELETE` and `SELECT` with `WHERE`, `ORDER BY`, `GROUP BY`, `LIMIT` and `LIKE` returns row-for-row identical output to stock `sqlite3` run on the same script. `LIKE` is checked in a `WHERE` clause with the `%` and `_` wildcards and with ASCII case folding, which overrides the `(NOT) LIKE` row of `spec/turso/COMPAT.md`; `GROUP BY` is checked with the aggregate functions `count()` and `sum()`. **At this step the statements in scope are `INSERT` and `SELECT` only.** `UPDATE` is a parse error at this checkpoint and `DELETE` is accepted without deleting anything, so no case at this step writes either one and neither is in scope here. | result rows |
| R01.3 | `changes()` and `last_insert_rowid()` return the same values after each statement as stock `sqlite3` returns for the same script. | result rows |
| R01.4 | Every SQLite serial type — NULL, the 1/2/3/4/6/8-byte integers, the 0 and 1 constants, IEEE float, text and blob — round-trips through the record encoding and is returned unchanged by a later `SELECT`. | result rows |
| R01.5 | A row payload one byte above the spill threshold `((usable-12)*64/255)-23` (index pages) or `usable-35` (table leaves) allocates an overflow page, while a payload one byte below stays entirely on the leaf page. The two payloads are written through this engine's own shell as `zeroblob(n)`, so `zeroblob()` is an engine obligation at this step. | database file bytes |
| R01.6 | A value stored across an overflow chain is read back byte-for-byte identically by stock `sqlite3`. `PRAGMA integrity_check` returning `ok` is checked at this step on the companion file whose payload stays on the leaf, not on the file holding the chain: two later checkpoints in the reference chain leave one page of every overflow chain unreferenced, so asserting `ok` on an overflow file here would freeze an expectation the chain cannot keep. On a file holding a chain the same `ok` is an obligation on a conforming engine that this step does not observe. | stock sqlite3 reading the file |
| R01.7 | A primary-key scan and a single-column index scan return exactly the same result set as the equivalent full table scan over the same data. **At this step only the primary-key half is in scope** — a seek on an `INTEGER PRIMARY KEY` rowid against a full scan of the same table. `CREATE INDEX` arrives later in the chain and nothing at this step creates a secondary index. The two access paths are compared through the aggregate functions `count()`, `sum()`, `min()` and `max()` and the scalar `ifnull()`, so those are in scope here too. | result rows |
| R01.10 | A newly created database carries the page-1 header that the profile's rule 1 fixes: the write and read version bytes at offsets 18 and 19 are both `2` rather than `1`, so the file is in WAL mode; the page size at offset 16 is 4096; the reserved-space byte at offset 20 is `0`, so the usable page size is the whole 4096; and the four-byte text encoding at offset 56 is `1`, UTF-8. Eight further page-1 fields are read alongside those five and must read as the format itself requires: the magic string at offset 0, the payload fractions at 21, the two freelist fields at 32 and 36 of a database with an empty freelist, the twenty reserved zeros at 72, and the three four-byte fields the format fixes at zero for a database created the way this step creates one — the largest-root-page pointer at offset 52 and the incremental-vacuum flag at offset 64, which the format defines as zero outside auto-vacuum and incremental-vacuum modes and nothing at this step engages either mode, and the application id at offset 68, which is zero until `PRAGMA application_id` sets it and nothing at this step sets it. Thirteen fields in all are read. A page-1 field outside those thirteen — one the format leaves to the writer and no task document chooses, such as the default page-cache size at offset 48 or the user version at offset 60 — is not in scope. | database file bytes |
| R01.11 | A committed write is durable across the process boundary, in the three ways the profile's durability rule names. A committed write appends frames to the `-wal` file, the `-wal` is still on disk after the writing process exits, and the written rows are visible to a subsequent query before any checkpoint has run. A connection that only reads changes nothing: `main.db` and `main.db-wal` are byte-identical after a read-only invocation. And a simulated crash is a file copy — `main.db` and `main.db-wal` copied as they lie, without `main.db-shm` and without a clean close — from which every committed row is read back. | database file bytes |
| R01.12 | With the output mode selected explicitly on the command line, the shell renders each result row as one line of `\|`-separated columns with no header and no padding, and renders NULL and blob values in exactly the profile's renderings — an empty field for NULL, raw bytes for a blob, per the profile's rules 4 and 5, which for these two classes are stock `sqlite3`'s `-list` renderings as well. The shell's own default mode is not in scope. The rendering is read off a result row built with `typeof()`, `length()` and `coalesce()`, so those are engine obligations here as well. | result rows |
| R01.13 | A statement that cannot be executed is refused rather than silently ignored: the shell reports it on standard output, produces no rows for it, and carries on with the next statement. The wording of the report is not part of the requirement. | error report |
| R01.14 | A statement that would violate the `PRIMARY KEY` of a rowid table — a second row with a rowid already in use — is refused under R01.13's contract rather than applied: the conflicting row is not stored, the statements on either side of it still run, and the file left behind holds exactly the rows that were accepted, as stock `sqlite3` reading it confirms. `UNIQUE` constraints and `ON CONFLICT` clauses are not in scope at this step. | error report |
| R01.15 | Scalar expressions evaluated with no table involved return exactly what stock `sqlite3` returns for the same script: integer and real arithmetic, integer division and the modulo operator with both signs of operand, the comparison operators over integers, reals and text, a comparison against `NULL`, `typeof()` over all five storage classes, and `length()` and `abs()`. | result rows |
| R01.16 | A command-line argument the shell cannot use is a process-level failure rather than a diagnostic it recovers from: that invocation exits `2`, writes nothing at all to standard output, and leaves no database file behind, while the identical statement stream given a usable argument still runs and still answers. | exit status |

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
