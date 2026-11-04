# Test interface — step 06

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

### Requirement groups

| Group |
|---|
| `s06-json-builders` |
| `s06-multi-row-delete` |
| `s06-update` |
| `s06-wal-format` |

`s06-update` and `s06-wal-format` cover the capability this step introduces;
`s06-json-builders` and `s06-multi-row-delete` cover breadth at this step.
Nothing in scope here is satisfiable by an earlier step's work alone.

The harness reads `tests/assets/groups.txt`, never this table; if the two ever
disagree, the file is what the harness follows.

This step's requirements are independent of every other step's.

## What this step measures

Two capabilities are new here, and they are unrelated to each other.

The first is the `UPDATE` statement. It does not parse at the previous
checkpoint -- `UPDATE t SET a=1;` answers a parse error there -- so every row
of behaviour it has is new at this step: an unqualified `UPDATE` over every
row, a predicated one that must leave non-matching rows and unassigned columns
untouched, several columns assigned in one statement including one assigned
from another column of the same row, a predicate that matches nothing being a
no-op rather than an error, and a change of storage class in a column declared
without a type, with `typeof()` following the new value.

The second is the shape of the write-ahead log this engine leaves on disk. The
previous checkpoint already writes a log; what is in scope here is that the log
is in SQLite's WAL format down to the byte -- the 32-byte header's fields at
their specified offsets, the 24-byte frame headers' page numbers and commit
records, the salts repeated in every frame, and the cumulative
Fibonacci-weighted checksum pair recomputed from the log's own bytes. Nothing
here checks what a reader *does* with a log that fails those checks; see the
exclusions at the end of this file.

Two further surfaces are in scope because they are refinements of
capabilities earlier steps already established rather than new ones:
multi-row `DELETE` (single-row `DELETE` was in scope at step 3), and the JSONB
builder functions and the JSON aggregates (the `json_*` text functions were
in scope at steps 2 and 3, and `jsonb()` itself at step 5).

## Which group covers which requirement

Every requirement group at this step exists for a row of the behaviour contract
above, and every row has a group. Nothing here is in scope that the table does
not name, and nothing the table does not name is in scope here.

| Group | Requirements it covers | What it observes |
|---|---|---|
| `s06-update` | R06.10, R06.11, R06.12, R06.13 | the rows an `UPDATE` leaves, listed in full in key order after every statement, so a write that disturbs a neighbouring row is visible and not just a changed count. Across five cases: an unqualified `UPDATE` whose new value is an expression over the row's existing values; a predicated one read back beside the rows it must not have touched; several columns assigned at once, one of them from another column of the same row, followed by a simultaneous swap `SET a=b, b=a` whose read-back `id, a, b` is `1\|100\|1` only if every assignment read the row's pre-update values; a predicate matching no row, and a `WHERE 0`, each followed by the unchanged table; and a column declared without a type -- starting out holding text, an integer and `NULL` -- updated to `NULL`, to a real and to text respectively, read back in key order with `typeof()` beside each new value, then the count of rows still holding `NULL`. The storage classes that case moves between are exactly those four: **no case at this step updates a column to a blob**, and none asserts anything about a blob-valued `UPDATE`. `typeof()` returning `blob` *is* in scope here, but in `s06-json-builders`, over what `jsonb_object` and `jsonb_array` return |
| `s06-wal-format` | R06.1, R06.2, R06.3 | the bytes of `main.db-wal` after a three-row write, read with `od` and reduced to one `conforming`/`nonconforming` word per claim, so no offset or salt value appears in the expected output. Across three cases: the header's magic, format number, page-size field, checkpoint sequence and salt pair, with the salts re-read out of every frame header; the frame headers' page numbers, commit records and the advance of the checksum pair; and the Fibonacci-weighted checksum chain recomputed in `awk` from the log's own bytes in the byte order the magic selects. Each of the three also has stock sqlite3 3.50.2 write a WAL of its own and runs the identical check over it, so a rule this engine satisfies and an independent implementation does not would show up as a failing verdict rather than passing unnoticed |
| `s06-multi-row-delete` | R06.14 | the rows that survive each `DELETE`, counted and then listed in key order: fifteen of twenty rows removed by a modulo predicate, where a scan that lets the tree move underneath it skips the row after each deletion and leaves ten; ten of twelve removed leaving two non-adjacent survivors; and a predicate on a non-key column followed by an unqualified `DELETE` that must empty the table completely |
| `s06-json-builders` | R06.15 | what the JSONB builders return and what it decodes to: `typeof()` over `jsonb_object`/`jsonb_array` beside `json_object`/`json_array` on the same arguments, which is where "builds a value directly rather than by parsing text" becomes observable as `blob` against `text`; `jsonb_set` overwriting an existing path where `json_insert` leaves it alone; and the two `json_group_*` aggregates, each fed from a subquery carrying an `ORDER BY` and then re-checked in order-free form by length, `json_type` and lookup by key |

`s06-update` and `s06-wal-format` are the **new** groups;
`s06-multi-row-delete` and `s06-json-builders` are **peripheral**.

## What this step re-checks from earlier work

The gate groups are not in that table, because they add no new requirement and
only veto. There are six of them and they are the whole of this step's inherited
obligation -- there is no hidden regression suite behind them:

- `gate-proof-of-life` starts the installed shell and asks it `SELECT 1;`.
- `gate-01` is one step-01 case, re-run unchanged: five rows inserted, then
  returned through `WHERE`, `ORDER BY` and `count()` under `LIKE` patterns
  with `%` and `_` in every position.
- `gate-02` is one step-02 case, re-run unchanged: `json_object`, `json_array`,
  `json`, `json_type`, `json_valid`, `json_extract` and `json_array_length`
  over integer, real, `NULL`, text and JSON-literal arguments.
- `gate-03` is one step-03 case, re-run unchanged: single-row `DELETE` from the
  middle, the front and the end of a table, each followed by `changes()`, the
  surviving rows, and aggregates over them.
- `gate-04` is one step-04 case, re-run unchanged: `CAST` of text to `INTEGER`,
  `REAL` and `NUMERIC` -- leading whitespace, trailing junk, a truncated
  fraction, an exponent, an empty string and a non-numeric string.
- `gate-05` is one step-05 case, re-run unchanged: the hex `jsonb()` produces
  for each element kind -- null, true, false, integers of three sizes, reals,
  text, empty array, empty object, and nested arrays and objects.

Nothing outside those five inherited cases is in scope here *as an inherited
requirement*: the gate is the whole of what this step asks you to carry
forward, and passing it is all that earlier behaviour must do. That is
not the same as saying earlier behaviour is unobserved. The cases in scope above
are written in ordinary SQL, and they lean on inherited surface to measure the
new behaviour. That surface is enumerated here rather than summarised, because
a candidate reading this file should not have to guess what a case in scope may
use as an instrument. It is, in full:

- statements: `CREATE TABLE` (including `INTEGER PRIMARY KEY` and a column
  declared with no type at all), `INSERT ... VALUES` with several row tuples,
  `SELECT`, `DELETE`;
- clauses: `WHERE`, `ORDER BY`, and a sub-`SELECT` in a `FROM` clause;
- aggregates: `count(*)` and `sum()`;
- scalars: `typeof()`, `coalesce()`, `upper()`, `CAST(... AS TEXT)`;
- operators: `||`, `*`, `+`, `%`, `=`, `<>`, `<`, `>`, `>=`, `AND`, `IS NULL`,
  and a constant-false predicate `WHERE 0`;
- JSON: `json()`, `json_object()`, `json_array()`, `json_extract()`,
  `json_type()`, `json_array_length()` and `jsonb()`.

Measured on 2026-09-21: every item in that list already answers correctly at
the **previous** checkpoint, so none of them is something this step asks you to
add. The two `json_group_*` aggregates are deliberately not in the list --
`json_group_array` is a "no such function" parse error at the previous
checkpoint, which is why it is new work under R06.15 rather than an instrument.

### The two routes by which earlier work is reached, and what each reaches

Earlier work is reached at this step on two separate routes, and it is worth
knowing which route each milestone is on.

The **veto route** is the gate: one case per milestone, the six listed above.
It adds no new requirement, and if any of it fails this step is not satisfied
outright, with `reward_details.json` naming the group and the case that vetoed
it.

The **instrument route** is the cases in scope. They are written in the
inherited SQL just enumerated, so breaking any of it makes a case fail on its
own terms, inside the requirement group that uses it.

Which milestone is reached by which, here:

| Milestone | Also exercised by a case in scope here | Reached only by that milestone's one gate case | Reached by neither at this step |
|---|---|---|---|
| 1 -- baseline engine | `CREATE TABLE`, `INSERT ... VALUES`, `SELECT`, `WHERE`, `ORDER BY`, `count(*)`, `sum()`: the nine `.sql` cases that build a table read every answer back through them | `LIKE` with `%` and `_` in every position | -- |
| 2 -- checkpointing, JSON, vectors | `json()`, `json_object()`, `json_array()`, `json_extract()`, `json_type()`, `json_array_length()`, in `s06-json-builders` | `json_valid`, and the argument coercions `gate-02` drives | `PRAGMA wal_checkpoint` and the vector functions: no case here, in a requirement group or the gate, calls either |
| 3 -- transactions, CTEs, `DELETE` | `DELETE` predicated and unqualified, in all three `s06-multi-row-delete` cases | `changes()` after a single-row `DELETE` | `BEGIN IMMEDIATE`, `BEGIN EXCLUSIVE`, and CTEs |
| 4 -- B-tree balancing, `CAST`, `LIKE` | `CAST(... AS TEXT)`, in `s06-update`'s storage-class case | `CAST` of text to `INTEGER`, `REAL` and `NUMERIC`, with its prefix, truncation and exponent rules | balance-siblings and balance-deeper: the largest table here is twenty short rows, nowhere near enough to split a page |
| 5 -- `DROP TABLE`, deferred transactions, MVCC, JSONB | `jsonb()` and the blob it returns, decoded, in `s06-json-builders` | the JSONB element header bytes, as hex | `DROP TABLE`, `BEGIN DEFERRED`, and selecting the MVCC engine |

The last column is the sampling `instruction.md` already declares when it says
one case from each earlier step is re-run. The obligation is that everything
the earlier steps established keeps working; the gate is a sample of it, not a
full re-run, and there is no hidden suite behind it. Nothing in that column is
licence to break something -- a later step's gate may draw a different case
from the same milestone.

There is deliberately **no requirement group here built out of earlier steps'
cases**. A group re-running gate material could measure nothing the gate does
not already veto: in every situation such a group exists to measure, the gate
has already failed the step. Inherited work is therefore reached where it is
observable -- as a veto for the surface only the gate reaches, and as an
instrument wherever a case in scope uses that surface. Plan for the
consequence: the **previous checkpoint's own binary, installed here unchanged,
satisfies no requirement group**. It passes all six gate groups and fails all
four requirement groups, so carrying earlier work forward correctly satisfies
nothing at this step by itself.

## Three renderings the compared output depends on

The `.sql` cases at this step are compared byte for byte against a recorded
expected output, so anything that reaches that output is part of what you have
to get right. `spec/PROFILE.md` rules 4 and 5 fix how `NULL`, a real and a blob
render. Three further points are used by cases here and are stated in this file
because no requirement row at this step names them:

- **`typeof()` returns one of exactly five lowercase tokens** -- `null`,
  `integer`, `real`, `text`, `blob` -- with no other spelling and no capital
  letter. `s06-update` reads `null`, `real` and `text` out of it and
  `s06-json-builders` reads `blob` and `text`. Measured 2026-09-21 against
  stock `sqlite3` 3.50.2 on the same values: identical, token for token.
- **A JSON value rendered as text is compact**: no space after `:` and none
  after `,`, so a two-key object prints as `{"a":1,"b":"x"}` and never as
  `{"a": 1, "b": "x"}`. This holds for `json()`, `json_object()`,
  `json_array()` and for the text form of what the `json_group_*` aggregates
  return. It is inherited -- steps 02 and 05 already check it, and `gate-02`
  re-runs a case that pins it -- and it is restated here because three of this
  step's cases in scope compare such text directly. Measured 2026-09-21: stock
  `sqlite3` 3.50.2 renders the same values the same way, so the pinned stock
  binary this step gives you is a sufficient check on it.
- **A sub-`SELECT`'s `ORDER BY` reaches the enclosing aggregate.** In
  `SELECT json_group_array(v) FROM (SELECT v FROM t ORDER BY k)` the aggregate
  must visit the rows in `k` order, so the answer is `[1,2,3]` and not some
  other permutation. R06.15 places the order obligation on the aggregate; this
  is the other half of it, on the planner, and it is stated because a planner
  that discarded the inner `ORDER BY` as redundant would fail a case in scope
  while satisfying every requirement row as written. Measured 2026-09-21: the
  inner `ORDER BY` survives at this checkpoint, at the previous one, and in
  stock `sqlite3` 3.50.2. Nothing here asks you to preserve an inner `ORDER BY`
  in any other position, and the same collections are re-checked in an
  order-free form as well.

## Where `json1.html` reads as forbidding what a case in scope here requires

`spec/sqlite/json1.html` is stock SQLite's JSON documentation, vendored
verbatim and unedited. Two sentences in it read as contradicting what
`s06-json-builders` requires. Both are resolved below by measurement rather
than by argument, and the page itself carries the resolution in each case.

- **Section 4.13, "If any argument to `json_object()` is a BLOB then an error
  is thrown", together with section 4.14, which says `jsonb_object()` works
  just like it.** Read alone that forbids `jsonb_object('nested',
  jsonb_array(1,2))`, which a case in scope here requires to build the JSONB
  encoding of `{"nested":[1,2]}`. Section 4.13 supplies the resolution two
  sentences later: an argument that is "the direct result from another JSON
  function" is "treated as JSON and all of its JSON type information and
  substructure is preserved. This allows calls to json_object() and
  json_array() to be nested." The blanket sentence is about a blob that is not
  a JSON value -- arbitrary bytes offered as a value to store. Measured
  2026-09-21 on the same machine: stock `sqlite3` 3.50.2 answers
  `json(jsonb_object('nested',jsonb_array(1,2)))` with `{"nested":[1,2]}` and
  refuses `json_object('k', x'4142')`; this checkpoint answers and refuses the
  same two. **No case at this step passes a non-JSONB blob to any JSON
  function**, so the refusal half is in scope here in neither direction.
- **Section 4.11, "These routines throw an error if the first JSON argument is
  not well-formed or if any PATH argument is not well-formed or if any
  argument is a BLOB", carried to the `jsonb_` forms by section 4.12 --
  against a case in scope that hands `jsonb_set` a JSONB blob as its first
  argument.** Section 4.2 states the other side: "If X is a BLOB and appears to
  be JSONB, then this routine simply returns a copy of X", with only the
  outermost element examined. A JSONB document supplied as the *document*
  argument is a JSON value, not the blob that sentence excludes. Measured
  2026-09-21: `json(jsonb_set(jsonb('{"a":{"b":1}}'),'$.a.b',json('[1]')))` is
  `{"a":{"b":[1]}}` on stock 3.50.2 and on this checkpoint alike, and that is
  what the case requires.

Where the page and a requirement row here genuinely differ, the requirement
row and the measurement behind it are what you are checked against. This section
resolves those two sentences and no others; nothing else in `json1.html` is
displaced by it.

## What the WAL format leaves open, and what it must hold here

`sqlite/fileformat2.html` carries the WAL header layout, the frame header
layout and the checksum algorithm. `sqlite/walformat.html` does **not**: that
page is the `-shm` wal-index document and it defers the `-wal` file's own
structure to the file-format description. Read the first for bytes, the second
for locking and recovery.

Three things the tests depend on are left open by both pages, so they are fixed
here:

- **The checkpoint sequence number at header offset 12 is zero in a log that
  has never been checkpointed.** The format names the field and gives its
  width, but not its initial value. Every case at this step creates its
  database, writes it and closes it within one run, without checkpointing, so
  the field is zero and a case may require that.
- **The log holds no bytes past its last frame.** A `-wal` file ends on a frame
  boundary, so the frame count a case derives from the file's own length --
  `(size - 32) / 4120` -- is the number of frames the log holds, and nothing is
  left unread at the end.
- **Both magic values are conforming, and the magic's low bit selects the byte
  order the checksum reads its input in**: `0x377f0682` selects little-endian
  input, `0x377f0683` big-endian. Every integer stored *in* the WAL header and
  in a frame header is big-endian either way. A case reads the magic and
  follows it rather than pinning one of the two, so either choice passes.

The frame stride follows from the header's own page-size field: a frame is a
24-byte frame header followed by one whole page image, so 4120 bytes at this
profile's 4096-byte page size.

Whether the committed frames are still *in* the log when the writing
invocation exits is governed by the durability rule in `PROFILE.md`, not here:
a writer may leave them there or may have folded them into `main.db` first, and
both are conforming. What no case here does is treat that latitude as
permission to check nothing. Each of the three `s06-wal-format` cases checks
`committed_write_durable` before anything else, and that verdict has no silent
route to `conforming`: a writer satisfies it either by leaving at least one whole
frame in `main.db-wal`, whose format is then checked strictly by the verdicts
below it, or -- having left none -- by `main.db` carrying the WAL-mode header
bytes, passing stock sqlite3's `integrity_check`, and giving back all three
committed rows in order. An invocation that writes nothing satisfies neither
branch and answers `nonconforming` on every verdict in all three cases. The
empty-log route is not available to a writer that left bytes behind: a log with
a frame in it is always checked on its format.

Both branches are real in this task rather than hypothetical. Measured over the
release chain on 2026-09-21, the same three-row write leaves a 12392-byte log
at this checkpoint and at every one up to the second-to-last, and the final
checkpoint folds the log into the database and truncates the file to zero
bytes. The file itself is never missing: `main.db-wal` is present in the
directory after the write at every checkpoint measured, including the final
one, so the profile's first consequence -- a write leaves a `-wal` file behind
-- holds all the way along. What changes at the end of the chain is the file's
length, not its existence. That is why the split above exists, and it is why a
case that simply required a non-empty log would fail a later checkpoint for
doing something the profile expressly allows: `PROFILE.md`'s durability rule
says in as many words that no test asserts the log is non-empty and none
asserts it is empty.

## What this milestone does not include

The WAL requirements above are about the log's **format**. Its **authority** is
out of scope at this step, and neither are nine things a reader might reasonably
expect from a step called "WAL integrity" -- or from the `UPDATE` statement it
also introduces. Each absence was measured against the release chain on
2026-09-21, not decided by argument. Do not treat any of them as a defect, and
do not implement against one expecting it to be required:

- **Recovery stopping at a bad frame checksum, and whole-log rejection on a
  salt mismatch.** At this checkpoint a committed page reaches `main.db` as
  well as the log, so a query answers correctly even with the log deleted.
  Corrupting a checksum or a salt therefore changes no answer, and no case
  asserts one. Write the checksums correctly -- they are checked, as bytes --
  but nothing here tests what you do with a log that fails them.
- **A torn or truncated final frame.** Same measurement, same reason. No case
  truncates a log and no case checks a diagnostic for one.
- **Crash atomicity at a frame boundary.** The crash copy `PROFILE.md`
  defines reopens to the same rows at this checkpoint and at the previous one,
  so nothing here is new and no case performs one.
- **A read-only invocation leaving both files byte-identical.** `PROFILE.md`'s
  durability rule states it and step 01's `s01-wal-durability` cases check it;
  no case at this step opens a database it did not write in the same invocation,
  so nothing here distinguishes a reader that touches the log from one that does
  not.
- **The `-shm` wal-index file.** No `-shm` file survives process exit at this
  checkpoint; only `main.db` and `main.db-wal` are left behind. There is no
  file to inspect and no case inspects one. `walformat.html` describes it in
  full and you may write one, but its contents are out of scope here.
- **`PRAGMA wal_checkpoint` advancing the checkpoint sequence or rewriting the
  salts.** At this checkpoint the pragma returns `0|0|0` and leaves the WAL
  header byte-identical. Checkpoint behaviour is a later step's subject; no
  case here runs a checkpoint, and none requires the header to change.
- **A commit size of zero on a frame that does not end a transaction.** The
  format reserves the frame header's second field for the database size in
  pages on a committing frame and zero elsewhere, and R06.3 used to say so.
  Measured on 2026-09-21 for the three-frame log the measurement workload produces:
  stock sqlite3 3.50.2 writes `0, 2, 2` across its frames; this engine marks every
  frame a commit and writes `2, 2, 2` on most runs, and was observed to record
  the `CREATE TABLE` commit at a smaller size on a minority of them -- which the
  format permits, because the size may grow from one commit record to the next
  and must never shrink. A case requiring the zero would therefore fail the
  checkpoint this whole step is cut from, and pinning either value would pin a
  number that moves, so R06.3 no longer claims it and no case asserts it.
  Writing the zero is not penalised; it is simply not measured. What *is*
  measured is the other two halves: a completed log ends on a commit record,
  and successive commit records never report the database shrinking.
- **The order in which a JSON aggregate visits its input.** `json_group_array`
  and `json_group_object` are in scope here, but the element order of the
  document they build is only asserted where an `ORDER BY` in a subquery fixes
  it. Without one the order is the scan's, not the aggregate's, and an expected
  output naming it would be pinning the query planner. In the `s06-json-builders`
  cases, every collection whose *order* is asserted is fed from an ordered
  subquery, and the same collections are re-checked in forms no order can change --
  a length, the document's `json_type`, and lookup by key -- over the unordered
  table directly.
- **Every `UPDATE` form the requirement rows do not name.** R06.10 to R06.13
  are the whole of the `UPDATE` surface in scope here. Out of scope, and not
  attempted by any case at this step: `UPDATE ... FROM`,
  `UPDATE ... RETURNING`, `UPDATE OR REPLACE` / `OR IGNORE` / `OR ABORT` /
  `OR FAIL` / `OR ROLLBACK`, `UPDATE ... ORDER BY ... LIMIT`, reassigning an
  `INTEGER PRIMARY KEY`, a constraint violation raised by an `UPDATE`,
  updating through a view, and `AFTER UPDATE` triggers. The measurement is the
  reason. Run at this checkpoint on 2026-09-21 against stock `sqlite3` 3.50.2
  on the same input, each one of the eight diverges: `RETURNING` is parsed and
  the update is applied, but no row comes back; `UPDATE ... FROM` is a parse
  error and changes nothing, where stock performs the join; `UPDATE OR REPLACE`
  leaves both rows of a `UNIQUE` conflict standing instead of replacing one;
  `UPDATE t SET id=...` on an `INTEGER PRIMARY KEY` leaves the key where it
  was; storing `NULL` into a `NOT NULL` column is accepted here and refused by
  stock; `CREATE VIEW` and `CREATE TRIGGER` both answer "not supported yet", so
  there is nothing to update through and nothing to fire; and
  `UPDATE ... ORDER BY ... LIMIT` is accepted here while stock refuses to parse
  it at all. A case asserting any of the eight would therefore either pin this
  engine against the format's second implementation or fail the checkpoint this
  step is cut from. Implementing any of them is neither required nor penalised;
  none is measured.
- **What `changes()` reports after an `UPDATE`.** `spec/turso/COMPAT.md` marks
  `changes()` `Partial` at this release, with the comment "Still need to
  support update statements and triggers", and that row is accurate. Measured
  on 2026-09-21: after `UPDATE t SET a=a+1 WHERE id<3` over a three-row table,
  this checkpoint answers `3` where stock `sqlite3` 3.50.2 answers `2` -- it
  reports rows considered, not rows changed. **No requirement at this step
  names `changes()` and no case in any `s06-*` group calls it**, so nothing
  here checks the divergence in either direction and you may fix it or leave
  it. What you may not do is regress it after a `DELETE`, where the two already
  agree: `gate-03` re-runs a step-03 case that reads `changes()` after each of
  three single-row `DELETE`s, and a gate failure zeroes this step.

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
| R06.1 | The 32-byte WAL header carries, at their specified offsets, one of the format's two magic values (`0x377f0682` or `0x377f0683` — a header carries one, not both), the format number 3007000, the database page size, a checkpoint sequence of zero in a log that has never been checkpointed, and a salt pair that is not all zeros; and every frame header in that log repeats the header's salt pair unchanged. A writing invocation that leaves no frame in the log at all is held instead to the durability rule: the committed rows must still be readable from the database file, which is what the case checks on that route. | database file bytes |
| R06.2 | The WAL carries the format's cumulative Fibonacci-weighted checksum pair: the header's pair checks the header's own first 24 bytes, and each frame's pair continues the running value over that frame header's first 8 bytes and its whole page image, computed in the byte order the header magic selects. | database file bytes |
| R06.3 | Each 24-byte frame header names a page number counted from one and, on a frame that ends a transaction, the database size in pages. A completed log ends on a commit record, and successive commit records never report the database shrinking. Whether a frame that does *not* end a transaction carries a zero there is not part of this contract: stock sqlite3's own log for the measurement workload reads 0, 2, 2 across its three frames while this engine's reads 2, 2, 2, so requiring the zero would fail the checkpoint this step is cut from. | database file bytes |
| R06.10 | An `UPDATE` with no `WHERE` clause changes every row, and an assigned value may be an expression over that row's existing values, so each row is read before it is written. | result rows |
| R06.11 | An `UPDATE` with a `WHERE` clause changes exactly the matching rows and leaves every other row, and every column it does not assign, untouched. Several columns may be assigned in one statement, including one assigned from another column of the same row, and every assignment reads the row's pre-update values. | result rows |
| R06.12 | An `UPDATE` whose predicate matches no row is a no-op and not an error: the table is unchanged, no diagnostic is produced, and the session continues. | result rows |
| R06.13 | An `UPDATE` changes the storage class held in a column declared without a type, including to and from NULL, and `typeof()` reports the new class rather than the old one. | result rows |
| R06.14 | A `DELETE` under a predicate removes exactly the matching rows, with none skipped because the tree moved underneath the scan, and an unqualified `DELETE` empties the table completely. | result rows |
| R06.15 | `jsonb_object` and `jsonb_array` return the format's binary encoding — `typeof()` reports `blob`, where `json_object` and `json_array` with the same arguments report `text` — and that blob decodes to the document it names; `jsonb_set` overwrites an existing path where `json_insert` leaves it alone; and `json_group_array` and `json_group_object` collect a result set into a single document, whose element order follows the order their input was given in and is checked only where an `ORDER BY` fixes it. | result rows |

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
