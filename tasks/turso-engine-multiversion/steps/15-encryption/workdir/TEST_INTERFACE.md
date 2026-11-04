# Test interface — step 15

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
  them from one side and not the other is the same comparison. The removal is
  tolerance for the earlier checkpoints in the chain whose shell framed its
  result blocks with a blank line; the shell at this checkpoint writes result
  rows and nothing else, which is `R11.3`'s obligation from step 11's
  checkpoint onward, and one `.sh` case at this step reads raw stdout and
  depends on it.
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
| `gate-12` | step 12, `single-element-and-wide-vectors.sh` — the stored length and the extracted text of a one-element and of an eight-element float32 vector, and vector_distance_l2 over the whole vector |
| `gate-13` | step 13, `transaction-ordering.sql` — five mutations inside one transaction - three INSERTs, an UPDATE and a DELETE issued in non-key order - read back after COMMIT as five capture rows in mutation order, with change_ids 1 to 5 and no gap, each carrying its own change_type and the rowid it touched, beside the count of them and the rows the transaction left in the table |
| `gate-14` | step 14, `view-referencing-view.sql` — a view defined over a view defined over a view: three levels resolved transitively, the rows each level returns, the fully expanded result, and the count of view rows in sqlite_schema |

### Requirement groups

| Group |
|---|
| `s15-encryption-diagnostics` |
| `s15-encryption-file-format` |
| `s15-encryption-off-format` |
| `s15-encryption-roundtrip` |
| `s15-encryption-session` |
| `s15-synchronous-pragma` |

`s15-encryption-diagnostics`, `s15-encryption-file-format`,
`s15-encryption-roundtrip` and `s15-encryption-session` cover the capability
this step introduces; `s15-encryption-off-format` and `s15-synchronous-pragma`
cover breadth at this step. Nothing in scope here is satisfiable by an earlier
step's work alone.

The harness reads `tests/assets/groups.txt`, never this table; if the two ever
disagree, the file is what the harness follows.

This step's requirements are independent of every other step's.

## Which group covers which requirement

Every requirement group at this step exists for rows of the behaviour contract
below, and every row has a group. Nothing is in scope here that the table does
not name.

| Group | Requirements it covers | What it observes |
|---|---|---|
| `s15-encryption-session` | R15.11, R15.12, R15.13 | the two pragmas' query forms with nothing else going on, and what each half does before the other has arrived: the canonical cipher name reported back from two different spellings of the same cipher, selected twice in one session because with no key held the set-once rule does not yet bind; the same question asked of two *different* ciphers in turn, where the newest selection is the one the subsequent key is checked against and the one the file is written under; that the cipher query form answers no row at all when nothing is selected -- read against stock `sqlite3`'s rendering of the same script, since a blank row and no row are the same bytes once blank lines are stripped -- and exactly one row, the canonical name, once a cipher is; and the sentence the key pragma answers with before a key is supplied, after one is, and after `CREATE`/`INSERT`/`SELECT` have run under a key held with no cipher selected, which writes and reads on the ordinary unencrypted path |
| `s15-encryption-diagnostics` | R15.6, R15.11, R15.12, R15.13 | a cipher name the engine does not implement; a key that is not hex; two hex keys whose decoded lengths no recognised cipher uses; a key of the wrong length for the cipher already selected; and the same mismatch arriving the other way round, with the key supplied first and the cipher completing the pair -- that each is refused at exactly that point in the stream with no rows of its own, that the query form afterwards shows the refused value did not become the session's, and that the session then takes a correct value and writes through it. Also that once a cipher and a key are both held, re-selecting either is refused while the original pair keeps working -- in that session and in a later process that reopens the file with it |
| `s15-encryption-file-format` | R15.3, R15.5, R15.6, R15.11 | page 1's plaintext header on an encrypted file -- the magic, the page size, the two format versions, the three payload fractions and the reserved-space byte; that byte's value for each of the three recognised ciphers in turn, beside the rows a second process holding that cipher's pair reads back and the absence of the row text from the file; and one workload written twice, once plain and once encrypted, compared on page-1 magic, on the bytes of page 2, and on whether the row text is findable |
| `s15-encryption-roundtrip` | R15.6, R15.7, R15.8, R15.9, R15.10 | what comes back out of an engaged cipher: the rows a second process reads with the correct pair; that a wrong key and an absent key each yield no row of the encrypted table and a non-zero exit status, while the correct key yields both rows and exit zero; that stock `sqlite3` does not answer `ok` on that file and recovers no row from it; the checkpoint result triple by value with the transform engaged and the rows after it; and the rows returned by a copy of the database and its log, taken as they lie on disk and reopened with the pair |
| `s15-encryption-off-format` | R15.1, R15.2, R15.14 | that naming a cipher and never supplying a key changes not one byte -- three runs of one workload, one with no cipher and two with it declared, digested and compared -- and that the reserved-space byte on the result is `00`; that stock `sqlite3` answers `ok` on that file and returns the same rows this engine returns from it; and that with the transform configured and not engaged every checkpoint mode still answers its triple and every row still reads back |
| `s15-synchronous-pragma` | R15.14 | the mode the synchronous pragma reports before anything sets it, and after each of `OFF`, `FULL`, `0` and `2`; that rows written under either mode survive a `TRUNCATE` checkpoint and read back unchanged; and that the checkpoint answers the same triple it answers in full mode |

Read it the other way and every requirement is covered: R15.1 and R15.2 by
`s15-encryption-off-format`, R15.3 and R15.5 by `s15-encryption-file-format`,
R15.6 by three groups, R15.7, R15.8, R15.9 and R15.10 by
`s15-encryption-roundtrip`, R15.11 by three groups, R15.12 by two, R15.13 by
`s15-encryption-diagnostics` (the refusal, once both halves are held) and
`s15-encryption-session` (the permission, while only one is), and R15.14 by
`s15-synchronous-pragma` and `s15-encryption-off-format`.

## What this step re-checks from earlier work

The gate groups are not in that table because they add no new requirement --
they only veto. This is the last step of the ladder, so its inherited obligation
is the largest in the package, and it is still finite and fully written down:

- `gate-proof-of-life` starts the installed shell and asks it `SELECT 1;`.
- `gate-01` through `gate-14` are **one case each, one per earlier
  milestone**, re-run unchanged from the step that introduced it -- the same
  statements and the same expected output, byte for byte. (A later step's copy
  may carry a rewritten comment header, where that step's edition of this
  document replaces the one the case was written against, and blank lines
  carry no meaning in a `.sql` case; the statements and the expected output
  never change.) Fourteen milestones, fourteen cases. The "Gate groups" table
  above is the enumeration: it names the group, the case file it re-runs and
  what that case covers, one row per milestone, so you can read this step's
  whole inherited checkpoint off this document without opening any earlier
  step's workdir.

Those fifteen groups are the whole of it. There is no hidden regression
suite, no sampling, and no earlier behaviour outside those cases is in scope
here *as an inherited requirement*. If any of them fails, this step is not
satisfied whatever else passes, and `reward_details.json` names the group and
the case that vetoed it.

That is not the same as saying earlier behaviour is unobserved. The cases in
scope below are ordinary SQL and shell, and they lean on `CREATE TABLE`,
`INSERT`, `SELECT`, `ORDER BY`, `count()`, transactions, `PRAGMA
wal_checkpoint` and the WAL format as the instruments that measure the new
behaviour -- so a regression in any of those surfaces as a case in scope
failing on its own terms, in addition to whatever the gate catches.

## Encryption

Page-level encryption is opt-in and is configured per session, before the
database is created or opened for work, by two pragmas:

```sql
PRAGMA cipher = 'aegis256';
PRAGMA hexkey = '000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f';
```

The manual shipped at this release describes the feature, and one of its claims
is stale at this tag: it says encryption must be enabled with a build-time
`encryption` feature flag and shows a `--features encryption` invocation. At this
checkpoint the transform is part of the built shell -- nothing here builds with
a flag and the artifact is the one described in "The artefact" above -- so read a
requirement that names a pragma, and not the flag, as the contract. The parts the
verifier depends on beyond the manual are below, and every one of them is
asserted by a case.

**The two pragmas, exactly.** Each has a set form and a query form. The query
form is a result row like any other -- it is not a diagnostic. The **set form
produces no result row at all**: `PRAGMA cipher = 'aegis256';` prints nothing
on success, and so do `PRAGMA hexkey = '<hex>'` and `PRAGMA synchronous =
<mode>`. Every expected output at this step is written on that basis, so a
build that echoes back the value it has just accepted fails cases whose
subject is not encryption at all.

- A name outside the recognised ones is not required to be accepted: the
  corpus probes exactly one such name (`rot13`) and requires it to be
  refused. An implementation that also accepts other spellings the reference
  happens to know passes every case here.
- `PRAGMA cipher = '<name>'` selects the cipher. The recognised names are
  `aegis256`, `aes256gcm` and `aegis128l`, and **recognition is
  case-insensitive** -- `AEGIS256` selects the same cipher as `aegis256`, and
  a case here relies on it. Any other name is refused under the error contract
  in `PROFILE.md`, leaves no cipher selected, and does not end the session.
- `PRAGMA cipher` with no argument returns **one row, the selected name in
  the engine's canonical spelling**: the lower-case spelling from the list
  above, whichever spelling was used to select it. With no cipher selected it
  returns **no row at all** -- not an empty string, not a `NULL` row. The
  distinction is checked by a case that reads raw standard output beside stock
  `sqlite3`'s rendering of the same script, because a blank row and no row
  are the same bytes once blank lines are stripped: the shell writes result
  rows and nothing else, with no blank line framing a result block, which is
  the framing `R11.3` fixed at step 11's checkpoint and every checkpoint since
  has kept.
- `PRAGMA hexkey = '<hex>'` supplies the key. The argument is hexadecimal and
  must decode to a length one of the recognised ciphers uses: **16 bytes (32
  hex characters) for `aegis128l`, 32 bytes (64 hex characters) for
  `aegis256` and `aes256gcm`**. Non-hex text, and hex of any other decoded
  length, is refused.
- `PRAGMA hexkey` with no argument returns **one row saying whether a key is
  held for the session**. It never echoes the key. That row is a result row
  and is compared like any other, so its text is fixed: exactly
  `encryption key is set for this session` while the session holds a key, and
  exactly `encryption key is not set for this session` before one has been
  supplied, and after a key refused for its **format** -- a value that is not
  hex, or whose decoded length is not one of the two lengths above. The one
  spot this does not fix is a key refused by the *length cross-check* below:
  the reference holds that key, so its own query form answers `set` there
  while an engine that discards the refused key answers `not set`, and which
  of the two a build does is not checked. Nothing else reads that state, and
  the values the case above asserts are the format refusals, where the two
  readings agree.
- When a cipher and a key are both present, the pair is cross-checked at the
  moment the second of the two arrives -- either half may be the one that
  completes the pair: a key whose length is not the selected cipher's key
  length is refused, and so is a cipher too small for a key already held. The refusal is not terminal -- the session stays usable, no encryption
  context is built from a mismatched pair (so a session left holding one keeps
  writing and reading on the ordinary unencrypted path), and it takes a
  correctly sized counterpart afterwards. Two things about that moment are
  deliberately left free, because the reference and a stricter reading of this
  paragraph disagree and a case would have to fail one of them: which
  selection the cipher query form *reports* after a refusal (the reference
  records the new name even when the context is not built from it), and
  whether a key refused by the cross-check is still held -- see the key
  pragma's bullet above. What is checked is the refusal itself, the state the
  file is left in, and that the session carries on.

**The pair is set once.** Once a session holds both a cipher and a key, the
pager's encryption context is fixed. Re-selecting either is refused -- even
to another recognised cipher, even with another well-formed key of the right
length -- rather than re-keying the pager underneath open cursors. The
context already in force keeps working for the rest of that session, and a
later process that reopens the file with the original pair reads the rows
back. Which name the cipher query form *reports* after such a refusal is not
checked, for the same reason as in the bullet above -- the reference records
the new name while the context keeps the old one -- and what is checked is
what the context does, not what the query form says about it.

**Before the pair is complete, neither half is binding.** Both halves of this
are in scope, by `s15-encryption-session`, and neither is left to be inferred
from the rule above:

- **A key on its own does not engage the transform.** A session that supplies
  a key and never selects a cipher writes and reads on the ordinary
  unencrypted path: `CREATE TABLE`, `INSERT` and `SELECT` behave exactly as
  they do when no key was ever supplied, the rows come straight back, and the
  file on disk is the unencrypted file described below. Holding a key is not
  by itself a request to encrypt anything.
- **Re-selecting the cipher is allowed while no key is held.** The set-once
  rule above binds only once the session holds *both* halves. Until a key
  arrives, `PRAGMA cipher = '<name>'` may be issued again, is not refused,
  and takes effect: the query form reports the newest selection, and that is
  the cipher a key supplied afterwards is cross-checked against for length.

**What encryption does to the file.**

- A database created **without** these pragmas is unencrypted, is byte-format
  identical to what earlier steps produced, and remains readable by stock
  `sqlite3`. So is one created with a cipher named but **no key supplied**:
  the transform is configured and not engaged, and the bytes are the same
  bytes, reserved-space byte and all. Every gate case relies on this —
  enabling the feature must not change the default path.
- With a cipher **and** a key in force, page bodies are transformed. The row
  text a workload wrote is not findable anywhere in the file, and page 2 --
  the first table leaf -- is not the page 2 the same workload writes
  unencrypted. Every case that compares bytes this way ends its workload with
  `PRAGMA wal_checkpoint(TRUNCATE);` before the file is read, on both the
  encrypted and the plaintext side. That is deliberate and it is the only
  reason the comparison is well defined: `spec/PROFILE.md`'s durability rule
  lets a writer leave committed frames in the log instead of folding them
  into the database file, and names that statement as the only way to
  *require* the fold. So the latitude the profile grants you decides nothing
  here -- what is checked is what the database file holds **after** an
  explicit checkpoint, never whether you chose to fold early.
- **Page 1 keeps its plaintext header.** The `SQLite format 3\0` magic at
  offsets 0-15 and the standard page-1 fields up to offset 99 are unchanged,
  so the file is still recognisable as a database of the declared page size.
  The **reserved-space byte at offset 20** is where the engaged transform
  declares the room it takes per page for its nonce and tag: 48 for
  `aegis256`, 32 for `aegis128l`, 28 for `aes256gcm`, and 0 when the
  transform is not engaged.
- An **encrypted database is not readable by stock `sqlite3`** and is not
  expected to be: `PRAGMA integrity_check` does not return `ok` and no row
  comes back. Cases that check SQLite interoperability use unencrypted
  databases.
- Opening an encrypted database with a wrong key, or with no key, fails with
  a non-zero exit status rather than returning wrong data, and prints no row
  of the encrypted table. This is one of the few places the exit status is
  checked, because the failure is not a refused statement but a failure to
  read the file at all.
- **Ciphertext is not required to be reproducible.** Encrypting the same rows
  twice may produce different bytes. No case compares the bytes of an
  encrypted database against a fixed value, or against another encrypted
  database; what is compared is what comes back out, what cannot be found
  inside, and the plaintext header fields.
- Checkpointing and crash recovery keep working underneath the transform:
  `PRAGMA wal_checkpoint` returns the same result triple it returns
  unencrypted, and a database reopened with its key after a simulated crash
  returns the committed rows.
- **What "either state, never a mixture" means for the crash case in scope.**
  The requirement allows recovery to land on the pre-transaction or the
  post-transaction state. The case that checks it takes its copy *after* the
  writing process has exited — the database file and its `-wal` exactly as
  they lie, never the `-shm` — and `spec/PROFILE.md`'s durability rule requires a
  process that writes and exits to leave its committed data recoverable by the
  next reader that opens those files. So at this step the post-transaction
  state is the only conforming outcome: the pre-transaction half of the
  requirement belongs to a copy taken before the `COMMIT` returns, and no case
  takes one. A recovery that replays part of the transaction, or drops it
  after it committed, fails.
- **The checkpoint triple is checked by value at this step**, with the
  transform engaged, configured and absent alike. Earlier steps checked it
  only by shape, so the frame accounting behind it becomes part of the
  contract
  here. The triple is SQLite's `busy | frames-in-log | frames-copied`; `busy`
  is always `0`, because no case opens a second connection to the same file.
  The other two are fixed as obligations rather than derived: the
  harness asserts the value `0|0|0` for a `TRUNCATE` checkpoint -- SQLite reports
  zeros for a truncating checkpoint rather than the frames it moved, which is why
  the value is the zeros and not a count -- and a checkpoint that completed
  restarts the log on the next write, so a later `FULL` checkpoint counts only the
  pages written since. One row inserted
  into an existing table changes one page and is one frame — an ordinary
  commit does not rewrite page 1 — so the required sequence "checkpoint,
  insert one row, checkpoint in `FULL` mode" answers `0|0|0` and then
  `0|1|1`. A statement that changes the schema changes page 1 as well and
  frames two.

## `PRAGMA synchronous`

The pager also gains a selectable synchronous mode at this checkpoint.
`PRAGMA synchronous = <mode>` takes `OFF` and `FULL` and their numeric
spellings; `PRAGMA synchronous` with no argument reports the mode
**numerically**, and the default is `2`. Which number answers which spelling
is checked, so it is printed here rather than left to be looked up: **`OFF` and
`0` are the same mode and the query form answers `0` for it; `FULL` and `2`
are the same mode and the query form answers `2`.** These are SQLite's own
numbers, as `spec/sqlite/pragma.html` lists them, and the default of `2` is
therefore `FULL` — a session that has set nothing answers `2`. Because the
reply is always the number, `= OFF` and `= 0` are indistinguishable in the
query form afterwards, and so are `= FULL` and `= 2`; what the numeric
spellings add is that the set form accepted a number at all. The mode selects
when the pager fsyncs and never what it stores: rows written under either mode
checkpoint into the main database and read back unchanged, and
`PRAGMA wal_checkpoint` returns the same result triple in either mode. The
fsync discipline itself is not observable from the shell, and nothing checks it.

## What this step states but does not check

- **WAL frame payloads under encryption.** The requirement list carried a row
  saying encrypted WAL frame bodies must differ from their plaintext pages.
  It cannot be checked here and has been withdrawn: at this release the
  `-wal` file is 0 bytes once the writing process exits, so there is no frame
  body for a case to look at. What that row was reaching for is covered by
  the page-body requirement in the main file, and by the checkpoint and
  crash-recovery requirements, which drive data through the WAL and back out.
  Nothing you do to WAL framing under encryption is checked directly.
- **Which cipher is stronger, and any property of the ciphertext itself.**
  No case measures key derivation, nonce construction, tag placement or
  ciphertext entropy. The claims in scope are the ones above: what is absent
  from the file, what the plaintext header says, and what comes back out
  through a correct key.
- **The rest of the `synchronous` domain.** `spec/sqlite/pragma.html`
  describes a wider set of modes than this step fixes. What is checked is the
  default report of `2`, and `OFF`, `FULL`, `0` and `2` as arguments -- those
  spellings, those reported numbers. `NORMAL`, `1`, `3` and an argument that
  names no mode at all are deliberately left open: no case issues them, no
  document here says what they do, and nothing you choose for them can fail a
  case. If you follow SQLite for the rest of the domain, no case is affected
  by it either way.

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
| R15.1 | Encryption is opt-in: with no cipher selected, and with a cipher selected but no key supplied, a given write workload produces the same database file it produced before encryption existed. Two independent runs of one workload write byte-identical files, and declaring a cipher without a key changes not one byte of them. | database file bytes |
| R15.2 | With encryption off, stock `sqlite3` opens the file and returns `ok` from `PRAGMA integrity_check`, so every earlier format guarantee still holds. | stock sqlite3 reading the file |
| R15.3 | With encryption on, every page written past page 1 is transformed, so no page body after byte 100 matches its plaintext content, while the `SQLite format 3\0` magic at offset 0 and the standard page-1 fields at offsets 16-99 remain plaintext. Page 1's own body (bytes 100-4095) is not observed either way: the reference leaves it untransformed, and no case distinguishes that from transforming it. | database file bytes |
| R15.5 | With encryption on, the `SQLite format 3\0` magic at offsets 0-15 and the standard page-1 fields up to offset 99 remain plaintext, so the file is still recognisable as a database of the declared page size, while the reserved-space byte at offset 20 is nonzero to accommodate the per-page nonce and tag: 48 bytes per page for `aegis256`, 32 for `aegis128l` and 28 for `aes256gcm`. | database file bytes |
| R15.6 | An encrypted database reopened with the correct key returns exactly the rows that were written to it. | result rows |
| R15.7 | An encrypted database opened with a wrong or absent key fails outright rather than returning garbage rows: no row of the encrypted table is produced, and the failure is visible in the process's exit status. | exit status |
| R15.8 | Stock `sqlite3` opening an encrypted database does not return `ok` from `PRAGMA integrity_check`, the byte-level interoperability guarantee being explicitly suspended while encryption is on. | stock sqlite3 reading the file |
| R15.9 | A `PRAGMA wal_checkpoint` run with encryption on copies frames correctly, and the rows remain readable through the encrypted pager afterwards. The checkpoint's result triple is checked by value at this step, not merely by shape: it is SQLite's `busy \| frames-in-log \| frames-copied`, `busy` is `0` because no case opens a second connection to the same file, a `TRUNCATE` checkpoint resets the log and answers `0\|0\|0`, and a `FULL` checkpoint taken after one row was inserted into an existing table answers `0\|1\|1`. | result rows |
| R15.10 | Recovery after a simulated crash with encryption on yields either the pre-transaction or the post-transaction state and never a mixture. Where the copy of the database and its log is taken after the writing process has exited, `spec/PROFILE.md`'s durability rule requires the committed transaction to be recoverable from that pair, so the post-transaction state is the only conforming outcome there. | result rows |
| R15.11 | `PRAGMA cipher = '<name>'` selects the session's cipher and `PRAGMA cipher` with no argument reports the selected name, in the engine's canonical spelling and as a single result row; with no cipher selected it reports no row at all. The recognised names are `aegis256`, `aes256gcm` and `aegis128l`; recognition of the name is case-insensitive and the reported spelling is always the lower-case one, so a name selected as `AEGIS256` reports back as `aegis256`. Any other name is refused under the `PROFILE.md` error contract, leaves no cipher selected, and does not end the session. | result rows |
| R15.12 | `PRAGMA hexkey = '<hex>'` supplies the session's key and `PRAGMA hexkey` with no argument reports whether a key is held, as one result row, without ever echoing the key; that row's text is fixed, exactly `encryption key is set for this session` while a key is held and exactly `encryption key is not set for this session` before one has been supplied and after one has been refused. The argument must be hexadecimal and must decode to a length some recognised cipher uses -- 16 bytes (`aegis128l`) or 32 bytes (`aegis256`, `aes256gcm`); anything else is refused. When a cipher and a key are both present the pair is cross-checked at the moment the second arrives, and a key whose length is not the selected cipher's key length is refused, leaving the session usable and able to take a correctly sized key. A key on its own does not engage the transform: with a key held and no cipher selected, the session writes and reads on the ordinary unencrypted path, so `CREATE`, `INSERT` and `SELECT` behave exactly as they do when no key was ever supplied. | result rows |
| R15.13 | The pair is set once. Once a session holds both a cipher and a key, re-selecting either -- even to another recognised cipher, even with another well-formed key of the right length -- is refused rather than re-keying the pager underneath open cursors, and the context already in force keeps working for the rest of the session and for a later process that reopens the file with the original pair. The rule binds only once both halves are held: while no key has been supplied, selecting a cipher again is allowed, is not refused, and the last selection is the one the query form reports and the one a key would later be cross-checked against. | result rows |
| R15.14 | `PRAGMA synchronous = <mode>` selects the pager's fsync discipline and `PRAGMA synchronous` reports the mode numerically, defaulting to 2. `OFF`/`FULL` and their numeric spellings both select, and the two pair up as SQLite pairs them: `OFF` and `0` are the same mode and the query form answers `0` for it, `FULL` and `2` are the same mode and the query form answers `2`, so the default of `2` is full. The mode changes when the pager fsyncs and never what it stores: rows written under either mode checkpoint into the main database, read back unchanged, and `PRAGMA wal_checkpoint` returns the same result triple it returns in full mode. | result rows |

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

The page *count* of a finished file is a separate matter. It is checked only
at a step whose behaviour contract above has a row that names it: where a
requirement fixes the cell distribution a workload produces -- SQLite's
balance-siblings rule, which is stated and checked at the one step that
introduces it -- then `PRAGMA page_count` after that workload is determined by
the rule rather than by your packing, and the requirement that states the rule
is what makes it checkable. Nothing checks the page count of a file no
requirement describes, so if no row above names it, page count is not in scope
at this step either.
