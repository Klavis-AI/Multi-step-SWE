# Test interface — step 05

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

### Requirement groups

| Group |
|---|
| `s05-deferred-transactions` |
| `s05-json-text-edits` |
| `s05-jsonb` |
| `s05-jsonb-paths` |

`s05-jsonb` and `s05-jsonb-paths` cover the capability this step introduces;
`s05-deferred-transactions` and `s05-json-text-edits` cover breadth at this
step. Nothing in scope here is satisfiable by an earlier step's work alone.

The harness reads `tests/assets/groups.txt`, never this table; if the two ever
disagree, the file is what the harness follows.

This step's requirements are independent of every other step's.

## Which group covers which requirement

Every requirement group at this step exists for a row of the behaviour contract
below, and every row has a group. Nothing here is in scope that the table does
not name, and nothing the table does not name is in scope here.

| Group | Requirements it covers | What it observes |
|---|---|---|
| `s05-jsonb` | R05.7, R05.8, R05.9, R05.11, R05.15 | the hex of the element header byte `jsonb()` writes for each JSON value kind; `length()` of a payload on each side of each size-class boundary, one byte apart; `json(jsonb(X))` for every value kind, including a `jsonb()` of a `jsonb()`, and the source order of an object's members in the text and in the bytes; a JSONB value stored in a column, its storage class, its length and its bytes; the same value read back out of the file by stock `sqlite3`, which re-encodes what it read and compares the hex to what is stored |
| `s05-jsonb-paths` | R05.12, R05.13, R05.16 | the four path accessors called on a binary document with no conversion in the expression; `jsonb_remove` and `jsonb_replace` each editing a document handed to them as JSON text and as a JSONB blob, and the storage class and hex bytes of what `jsonb_remove` returns; and an unfollowable blob header and an unparseable path each being refused where they stand while the batch carries on and the stored document survives |
| `s05-deferred-transactions` | R05.5 | the rows present after a `BEGIN DEFERRED` block commits, and the rows and aggregates before and after a deferred transaction that writes nothing |
| `s05-json-text-edits` | R05.14 | which paths `json_replace` overwrites and which it leaves alone -- an absent object key, an array subscript past the end, a key in an empty object, several paths in one call, and a replacement value of SQL `NULL` applied both to a path that exists and to a path that does not |

`s05-jsonb` and `s05-jsonb-paths` are the two `new` groups; the other two are breadth at this step. The
split is deliberate and is what keeps a failure localised: the byte format and
the path operations are different surfaces, and no group holds a majority of
the requirement rows.

Stated plainly, and not left to be inferred from the step's name, the
capability this step adds is **JSONB, SQLite's binary JSON
encoding**: the element header byte for each value kind, the payload size
classes, the round trip back to JSON text with an object's members in the
order they were written, the path accessors and the
`jsonb_` mutators taking and returning the binary form, the value surviving
storage in a column, and -- the point of having a binary encoding at all --
stock `sqlite3` reading the bytes this engine wrote and agreeing with them
down to the byte. It also covers the two ways a JSONB call can be given
something it cannot use -- a blob whose leading header byte is not one a
reader can follow, and a path that does not parse -- each of which is refused
where it stands rather than answered or allowed to end the batch.
`BEGIN DEFERRED` as a statement form, and `json_replace` over paths that are
present and paths that are not, are the two peripheral additions.

Two of the four capabilities the step's **title** names are out of scope here.
The section below says which, and why, with the measurement. Read this table
as the statement of what is tested; the title is a summary of what changed
upstream at this release, which is not the same thing.

## What this step re-checks from earlier work

The gate groups are not in that table, because they add no new requirement and
only veto. There are five of them -- the four `gate-NN` rows `tests/assets/
groups.txt` lists, plus the `gate-proof-of-life` group the harness injects
ahead of them -- and they are the whole of this step's inherited obligation.
There is no hidden regression suite behind them, and nothing else from steps
01 to 04 is re-run here:

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
- `gate-04` is step 04's `cast-prefix-and-truncation.sql`: `CAST` of text to
  `INTEGER`, `REAL` and `NUMERIC` over leading whitespace, a trailing
  non-numeric tail, a truncated fraction, an exponent, an empty string and a
  string with no numeric prefix at all.

No other step-01 to step-04 behaviour is in scope here. If any of the five
fails, this step is not satisfied and `reward_details.json` names the group and
the case.

## The JSONB element header byte, stated

R05.11 and R05.9 check the bytes `jsonb()` writes, so the byte layout has to
be readable from the documents you are given rather than guessed at.
`spec/sqlite/json1.html` does not carry it, and section 3.2.1 of that page
actively steers a reader away: JSONB "is intended for internal use by SQLite
only", and applications "should not use JSONB outside of SQLite nor try to
reverse-engineer the JSONB format". That paragraph is addressed to
applications *using* SQLite. It is not addressed to an engine *implementing*
the format deliberately, which is what this step asks of you. So the layout
is stated here, and this statement is what the cases in scope assert.

Every element is one header byte, then -- for larger payloads only -- a size
field, then the payload. The header byte's **low nibble is the element type**
and its **high nibble is the payload size**.

Element types, with the bytes `jsonb()` produces on stock `sqlite3` 3.50.2:

| Low nibble | Element type | Payload |
|---|---|---|
| `0` | NULL | none |
| `1` | TRUE | none |
| `2` | FALSE | none |
| `3` | INT | the integer in RFC-8259 decimal, as ASCII digits |
| `5` | FLOAT | the real as ASCII, in the RFC-8259 spelling it was given in -- the source characters, not a re-rendering of the value |
| `7` | TEXT | the characters of the string, with no quotes and no escapes in them |
| `8` | TEXTJ | the characters of the string, with its JSON escapes still present as written |
| `B` | ARRAY | each element in turn, every one its own header, size field and payload |
| `C` | OBJECT | label, value, label, value, each of them its own header, size field and payload |

Two consequences of that table are in scope here, so neither is left to be
inferred from it.

**A real keeps the spelling it was written in.** `jsonb()` copies the source
characters of the number into the payload; it does not evaluate the real and
re-render it in a canonical form. `1e3` encodes as `35316533` -- header `35`,
then the three payload bytes `31 65 33`, which are the characters `1`, `e`,
`3` -- and not as the bytes of `1000.0`, which is what `jsonb('1000.0')`
gives. The same holds for `1E3`, `1e+3` and `1.50`, each of which keeps its
own spelling. An engine that normalised reals would write different bytes for
a value R05.11 names and fail that row on a number it had got arithmetically
right.

**An object keeps the order its members were written in.** The OBJECT payload
is label, value, label, value in source sequence, and `json()` walks it in
that sequence, so `json(jsonb('{"z":1,"a":2}'))` is `{"z":1,"a":2}` and never
`{"a":2,"z":1}`. R05.7's phrase "canonical JSON text" is about the *rendering*
-- no added whitespace, one spelling per token -- and not about an *ordering*:
nothing at this step sorts object keys, and every expected output in this
step's suite is in source order. One case asserts the rule directly on a
document whose members are out of order -- `jsonb-object-source-order.sql`,
which takes the text, the bytes and a nested object -- because every other
object in the suite already has its members in ascending key order and would
let a sorting encoder pass.

Further type codes exist in stock for the JSON5 spellings and for raw text --
`4` is an integer in a JSON5 form such as hexadecimal, `6` a JSON5 real such
as one written with no digit before the point, `9` a string carrying a JSON5
escape, and `A` a raw text form. **No case in scope at this step asserts any of
those four.** Every value R05.11 and R05.7 feed in is RFC-8259, so it comes
back as `3`, `5`, `7` or `8`, and an engine that never emits `4`, `6`, `9` or
`A` loses nothing here. `8` is in the table above rather than in this
paragraph because R05.7's round trip does put escaped strings through
`jsonb()`: a string whose characters are `a`, backslash, `n`, `b` encodes as
`48 61 5C 6E 62` -- type `8`, four payload bytes, the escape kept as the two
characters it was written as.

The high nibble is the payload size itself when the size is small enough to
fit in it, and otherwise selects a size field placed between the header byte
and the payload:

| High nibble | Meaning |
|---|---|
| `0`-`B` | the payload is exactly that many bytes long, 0 through 11; no size field |
| `C` | a 1-byte size field follows the header byte |
| `D` | a 2-byte size field follows the header byte |
| `E` | a 4-byte size field follows the header byte |
| `F` | an 8-byte size field follows the header byte |

Size fields are big-endian. `F` is the class R05.9 leaves out as
unconstructible -- see "What this milestone does not include" below.

### What makes an element header one a reader can follow

R05.16 checks the refusal of a blob whose outermost element header is not one
a reader can follow, and the layout above is what decides that. An element is
**followable** when both halves of its header byte check out against the blob
the header sits in:

- its low nibble is one of the thirteen element type codes of the format -- `0`
  through `C`, of which the table above names the nine that `jsonb()` produces
  in this task's corpus; and
- its declared extent -- the header byte, plus the size field its high nibble
  selects, plus the payload byte count that size field names -- fits inside
  the blob.

An element whose declared extent runs past the end of the blob is not
followable, however ordinary its type code looks; nor is one whose low nibble
is `D`, `E` or `F`, which are not element types at all.

Both blobs R05.16's case names are unfollowable, and the extent clause is the
one worth reading twice, because a reader that checked only the type code
would accept the second of them:

- `x'ff'` fails both clauses. Low nibble `F` is not a type code, and high
  nibble `F` announces an 8-byte size field with no bytes at all after the
  header to hold it.
- `x'c0ffee'` fails only the extent clause, which is the point of including
  it. Header `C0` has low nibble `0`, NULL, a perfectly valid type. Its high
  nibble `C` selects a 1-byte size field, that field reads `FF` = 255 payload
  bytes, and the blob has one byte of remainder.

Only the outermost element is examined. `spec/sqlite/json1.html` section 4.2
says so, and nothing at this step asks you to validate the elements nested
inside a document whose own outermost header is followable.

Worked through the values the cases in scope actually use, so that the prose
above and the expected output can be checked against each other:

| Value | Bytes | Read as |
|---|---|---|
| `null` | `00` | high `0` no payload, low `0` NULL |
| `true` | `01` | high `0`, low `1` TRUE |
| `1` | `1331` | one payload byte, INT, payload `31`, the digit `1` |
| `1.5` | `35312E35` | three payload bytes, FLOAT, payload the three characters `1.5` |
| `1e3` | `35316533` | three payload bytes, FLOAT, payload the three characters `1e3` -- the spelling as written, not `1000.0` |
| a one-character string `a` | `1761` | one payload byte, TEXT, payload `61` |
| the string `ab` | `276162` | two payload bytes, TEXT, payload `61 62` |
| the string `a`, a newline, `b` — written `"a\nb"` in JSON, escape and all | `48615C6E62` | four payload bytes, TEXTJ, payload `61 5C 6E 62` -- the escape kept as the backslash and the `n` |
| `[]` | `0B` | no payload, ARRAY |
| `{}` | `0C` | no payload, OBJECT |
| `[1,2]` | `4B13311332` | `4B` is four payload bytes of ARRAY, then the two INT elements |
| an 11-byte string | `B7` then 11 bytes | `B` is 11, the largest size the nibble itself holds |
| a 12-byte string | `C70C` then 12 bytes | `C` selects a 1-byte field, which reads 12 |
| a 255-byte string | `C7FF` then 255 bytes | the 1-byte field at its maximum |
| a 256-byte string | `D70100` then 256 bytes | `D` selects a 2-byte field, big-endian |
| a 65535-byte string | `D7FFFF` then 65535 bytes | the 2-byte field at its maximum |
| a 65536-byte string | `E700010000` then 65536 bytes | `E` selects a 4-byte field, big-endian |

Those last six rows are exactly what R05.9's case measures with `length()`,
one byte either side of each boundary. Of the eleven rows above them, nine
are in R05.11's expected output as hex; the two string rows `ab` and the newline-bearing
`a`, newline, `b`
are asserted by R05.7's case instead, which is where an escaped string is fed
through `jsonb()` and is the only place the TEXT and TEXTJ codes can be told
apart -- both render back to the same JSON text, so only their bytes
distinguish them.

## The length of a JSONB value, and why it is in scope

R05.9 asserts `length()` of a blob, and the "Out of scope" list below says the
size of a file is out of scope. The two are not in tension. The size in bytes of
a *file* is yours: it follows from the page size, from free space, from when
you choose to flush. The length of a *JSONB value* is fixed by the encoding
the requirement names -- one header byte whose high nibble either holds the
payload size itself or selects a 1-, 2- or 4-byte size field that follows it.
An engine writing that encoding produces 12 bytes for an 11-byte string
payload and 14 for a 12-byte one, because the second needs a size field the
first does not -- and, at the next two boundaries, 257 bytes for a 255-byte
payload against 259 for a 256-byte one, and 65538 against 65541. Every one of
those six numbers is fixed by the layout stated above and by nothing else.
Stock `sqlite3` 3.50.2 produces the same numbers for the same
values, and R05.8's case makes that agreement an assertion rather than a
claim: it re-encodes what it read and compares the hex against the bytes on
disk.

## SQL the cases in scope use that no requirement row names

Every requirement row above names a behaviour. The cases that check them are
ordinary SQL, and four pieces of that SQL are named by no row. They are stated
here so that none of them has to be guessed at or recovered from a worked
table. None of them is itself in scope -- a case fails on the behaviour its row
names -- but a shell that does not have them cannot reach the behaviour the
case checks.

- **`hex()` returns upper-case digits**, two per byte, no separator. Every hex
  expectation in this step's suite is upper case and the worked table above is
  written the same way, so a shell whose `hex()` answered `4c17621332` would
  fail the comparison on bytes it had got exactly right. `lang_corefunc.html`,
  in this step's reading list, says it in as many words -- "returns a string
  which is the upper-case hexadecimal rendering" -- and it is repeated here
  because no requirement row names `hex()` at all, so a reader working from the
  contract alone would never be sent to that page. Measured against stock
  `sqlite3` 3.50.2 for every byte. One bound is worth naming: at this checkpoint
  the reference renders a byte above `0x7f` through a UTF-8 replacement sequence
  -- `hex(x'00ff10')` answers `00EFBFBD10`, the defect step 04's edition
  measures and R10.8 fixes -- so no case at this step reads a byte above `0x7f`
  back through your `hex()`. The one place a value's stored bytes matter here,
  an object whose header byte is `8C` or higher, is read back with stock
  `sqlite3`'s own `hex()`, which is the reader R05.8 asks to confirm the bytes
  with anyway.
- **A blob literal is written `x'...'`** -- `x'ff'`, `x'c0ffee'` -- the form
  `spec/sqlite/lang_expr.html` gives under "Literal Values". R05.16's case
  builds both of its unfollowable blobs that way, so the literal has to parse
  before the refusal that row checks can happen at all.
- **`INSERT ... VALUES` takes several parenthesised rows in one statement**,
  comma-separated, as `spec/sqlite/lang_insert.html` describes. `COMPAT.md`
  marks `INSERT` `Partial` and does not say which parts, so the matrix neither
  confirms nor denies this form; cases in `s05-jsonb` and in
  `s05-deferred-transactions` use it -- `s05-json-text-edits` writes no table
  at all -- and it works at this checkpoint. It is surface inherited from step
  01 rather than new work at this step.
- **The other scalar and aggregate functions the cases lean on** -- `typeof`,
  `length`, `printf`, `substr`, `replace`, `zeroblob`, and the aggregates
  `count`, `sum`, `min` and `max` -- are each marked `Yes` in
  `spec/turso/COMPAT.md`, which `instruction.md` points you at by name.

## Where `COMPAT.md` and this contract disagree

`spec/turso/COMPAT.md` is the upstream compatibility matrix shipped with this
release, vendored unchanged and checksummed. Two of its rows do not match what
the reference build at this checkpoint actually does. Where they differ, the
requirement table and the measurement behind it are what you are checked against.

- The matrix says of `BEGIN TRANSACTION` that "`BEGIN DEFERRED` is not
  supported". At this checkpoint it is. `BEGIN DEFERRED`, an `INSERT` and a
  `COMMIT` run, and the rows are there afterwards. It is the *previous*
  checkpoint that answers `Parse error: BEGIN DEFERRED not supported yet` and
  then `Transaction error: cannot commit - no transaction is active`. The row
  is stale by one release, and R05.5 checks the behaviour, not the row.
- The matrix marks `DROP TABLE` as `No`, which agrees with the measurement in
  substance -- nothing usable happens -- but understates it: the statement
  ends the process rather than being refused. Either way nothing about `DROP
  TABLE` is in scope here.

An empty cell in the matrix is not a statement that a function is missing.
`jsonb_array`, `jsonb_object`, `jsonb_set`, `jsonb_insert`, `jsonb_patch`,
`jsonb_group_array` and `jsonb_group_object` all have blank cells at this
release. None of them is in scope at this step -- the JSONB rows here use
`jsonb`, `jsonb_extract`, `jsonb_remove` and `jsonb_replace`, which the matrix
marks `Yes` -- so the blanks cost you nothing here. Several of those functions
are checked at a later step.

## Where `json1.html` and this contract disagree

`spec/sqlite/json1.html` is stock SQLite's JSON documentation, vendored
verbatim. It describes stock `sqlite3`, and three passages in it read as
contradicting a requirement stated here. Each is resolved below, by
measurement against stock 3.50.2 and against this checkpoint on the same
machine. Where the page and the requirement table genuinely differ, the
requirement table and the measurement behind it are what you are checked against.

- **Section 4.11 -- "These routines throw an error if the first JSON argument
  is not well-formed or if any PATH argument is not well-formed or if any
  argument is a BLOB" -- against R05.13, which hands `jsonb_replace` a JSONB
  blob.** These do not actually conflict. The sentence is about a BLOB
  supplied as a *value to be written in*, not about a JSONB document supplied
  as the first argument; section 4.2 says the same thing from the other side,
  "If X is a BLOB and appears to be JSONB, then this routine simply returns a
  copy of X". Measured on stock 3.50.2: `json(json_replace(jsonb('{"a":1}'),
  '$.a', 8))` returns `{"a":8}`, while `json_replace('{"a":1}', '$.a',
  x'4142')` is refused with `JSON cannot hold BLOB values`. R05.13 checks the
  first of those. It says nothing about the second, and no case at this step
  passes a BLOB as a replacement value -- which is just as well, because this
  checkpoint returns an empty value there rather than refusing, and that
  difference is deliberately outside the in-scope set.
- **Section 3.8, "The JSON BLOB Input Bug", against R05.16's refusal.** That
  section documents, as officially supported, that a BLOB which is not JSONB
  but is valid JSON once cast to text is accepted as text JSON. This
  checkpoint does not implement that fallback: `json(x'5B312C325D')` -- four
  payload bytes behind a header claiming five, so not valid JSONB, but the
  text `[1,2]` when cast -- returns `[1,2]` on stock 3.50.2 and is refused
  with `malformed JSON` here. R05.16 therefore stays clear of that class
  entirely. The two blobs it names are neither valid JSONB nor valid JSON as
  text, and both stock and this checkpoint refuse both of them, so
  implementing section 3.8's fallback or not implementing it cannot
  change what this step requires.
- **Section 3.2's latitude for invalid JSONB, against R05.16 asserting a
  refusal at all.** The page permits almost any behaviour there -- "Invalid
  JSONB might lead to crazy answers, or it might cause queries to abort, but
  it won't cause a crash" -- and adds that the handling "might change from one
  version of SQLite to the next". R05.16 is deliberately narrower than that
  latitude rather than a contradiction of it: it asserts a refusal only for
  the two blobs and the two unparseable paths its case names, and only the
  three properties that case can observe -- no row comes back, the statements
  after it still run and still answer, and the stored document is unchanged
  and still readable. It asserts nothing about the wording of any refusal
  (the case folds every refusal to a single token), nothing about deep
  structure -- section 4.2 is explicit that only the outermost element is
  examined, and validating deeper is neither required nor in scope -- and
  nothing about any malformed blob other than those two.

## What this milestone does not include

Five things the step's title, the release notes for `v0.0.17`, or stock
`sqlite3`'s own behaviour might lead a reader to expect are **out of scope**
here. Each was probed against this checkpoint on 2026-09-21, and against the
checkpoints on either side of it where that was the point. None is a defect in
your build; none can regress here; and **no case observes any of them**, so
nothing you do about them changes what this step requires.

- **`DROP TABLE`** -- named in the step's title, and out of scope. At this
  checkpoint `DROP TABLE` on a table that exists does not fail, it ends the
  process: the reference shell panics at `core/vdbe/mod.rs:311:18`, `cursor id
  out of bounds`, and exits `101`, leaving every statement after it unrun. The
  previous checkpoint answers `Parse error: DROP TABLE not supported yet` and
  carries on; the next one, `v0.0.18`, executes it and carries on. So there is
  no dropped table here to inspect -- no `sqlite_schema` row to re-read, no
  freelist trunk or count to compare, no schema cookie to watch increment, no
  freed page to watch a later insert reuse. Implementing `DROP TABLE`
  correctly is not penalised; it is simply not measured at this step.
- **MVCC as a selectable engine** -- also named in the step's title, also out
  of scope. `--experimental-mvcc` appears in `--help` for the first time at this
  checkpoint, described there as `Enable experimental MVCC feature`, and that
  first appearance is what the title is reporting. Selecting it does not
  produce a database: `CREATE TABLE` under the flag never returns and was
  killed at 25 seconds, and pointing the flag at a database written without it
  answers `Parse error: Table t not found` for a table that is present. No
  file can be written with MVCC selected, so there is no file for stock
  `sqlite3` to open and no `integrity_check` verdict to assert. No case in
  this step's suite passes the flag.
- **A second connection, and so any form of lock contention.** `BEGIN
  DEFERRED` is checked as a statement form and for the visibility of its
  writes, not for when it takes its lock, because at this checkpoint the
  engine holds a whole-file exclusive lock for the life of the process. While
  one shell has the database open, a second engine process is refused with
  `Locking error: Failed locking file. File is locked by another process`, and
  stock `sqlite3` with `database is locked (5)` -- for a read, not only for a
  write. A deferred transaction's lock upgrade therefore cannot be made to
  fail, because there is no second connection for it to fail against, and the
  busy refusal that would follow is out of scope. No case tries to provoke one.
- **The 8-byte payload size class.** The 4-byte size field holds payloads up
  to 4294967295 bytes, so reaching the 8-byte class needs a single JSON value
  of four gibibytes or more. That is not constructible inside a test at this
  or any checkpoint. R05.9 names the four classes that are.
- **The storage class `jsonb_replace` returns.** R05.13 checks what
  `jsonb_remove` returns -- storage class blob, carrying the element encoding
  stated above -- and does not check what `jsonb_replace` returns, because the
  reference diverges from stock there for the whole of this task's release
  range. Measured on 2026-09-21 at every checkpoint from `v0.0.17` to
  `v0.1.5`: `typeof(jsonb_remove(jsonb('{"a":1,"b":2}'),'$.a'))` is `blob` and
  `hex(...)` of it is `4C17621332`, matching stock `sqlite3` 3.50.2 byte for
  byte; `typeof(jsonb_replace('{"a":1}','$.a',8))` is `text`, where the same
  call on stock 3.50.2 is `blob`, and handing `jsonb_replace` a JSONB blob
  instead of text does not change that. Both answers round-trip identically
  through `json()` and through every path accessor, which is why no case here
  can tell them apart and why R05.13 asserts the representation only for
  `jsonb_remove`. **Returning a blob from `jsonb_replace`, as stock does, is
  not a failure here** -- no expected output in this step contains the storage
  class of a `jsonb_replace` result, so either answer passes.

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
| R05.5 | `BEGIN DEFERRED` opens a transaction this engine accepts: every row written inside one is present once `COMMIT` has returned, and a deferred transaction that performs no write opens and closes leaving the table exactly as it found it. | result rows |
| R05.7 | `json(jsonb(X))` returns X's canonical JSON text for every JSON value kind — objects, arrays, integers, reals, both booleans, null, plain and escaped strings, and arbitrary nesting — and `jsonb()` applied to a value that is already JSONB is idempotent. | result rows |
| R05.8 | A JSONB blob the engine writes into a database file is read back by stock `sqlite3` to the same JSON text, the same `json_extract`, `json_type` and `json_array_length` answers, and storage class blob; re-encoding that text with stock `sqlite3`'s own `jsonb()` reproduces the stored bytes exactly. | stock sqlite3 reading the file |
| R05.9 | `length()` of a JSONB value names the width of its payload-size field: a payload counted in the header byte's own size nibble, then the 1-, 2- and 4-byte size fields above it, each boundary crossed by the smallest step that crosses it. | result rows |
| R05.11 | `hex(jsonb(X))` is the element header byte SQLite specifies for each JSON value kind — element type in the low nibble, payload size in the high nibble — for null, both booleans, integers of three widths, two real spellings, a short string, the empty array and the empty object, and arrays and objects with elements inside them, including nested ones. | result rows |
| R05.12 | The JSON path accessors `json_extract`, `jsonb_extract`, `json_type` and `json_array_length` take a JSONB argument directly, with no `json()` conversion in the expression, and return the same answers they return for the equivalent JSON text, through object keys, array subscripts and nested paths. | result rows |
| R05.13 | `jsonb_remove` and `jsonb_replace` accept either representation of the document they edit — JSON text or a JSONB blob — and `jsonb_remove` returns the binary one: storage class blob, carrying the element encoding this step specifies, so its result feeds straight into a path accessor with no conversion. `jsonb_replace` over a path that is absent leaves the document unchanged. | result rows |
| R05.14 | `json_replace` overwrites every path that exists — several in one call, and with a whole document as the replacement value — and leaves every path that does not exist alone, including an array subscript past the end and a key in an empty object. A replacement value of SQL `NULL` is a value like any other and does not change which paths are written: on a path that exists it is written and that path's value becomes JSON `null`, and on a path that does not exist it is still not written. | result rows |
| R05.15 | A JSONB value inserted into a column is stored with storage class blob at the length its encoding determines, comes back out of the table with its bytes unchanged, and is read by the path accessors after that round trip. | result rows |
| R05.16 | A blob whose outermost element header is not one a reader can follow, and a JSON path that does not parse, are each refused where they stand: the call returns no row, the statements after it still run and still answer, and the stored document is unchanged and still readable afterwards. A path that parses but selects nothing is not a refusal — it answers NULL. Validating the *deep* structure of a JSONB document is not required and is out of scope: `json1.html` §4.2 says only the outermost element is examined. | error report |

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
