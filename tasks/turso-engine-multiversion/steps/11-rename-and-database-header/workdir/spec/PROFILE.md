# Compatibility profile

Stock `sqlite3` must be able to open, read and query every unencrypted
database this engine writes. The engine is not a clone of SQLite's behaviour
in every respect, and the file format deliberately leaves a writer some
freedom. This document fixes the choices the specification does not, and **it
is what the tests assert**. It records measured upstream behaviour; it does
not invent a target.

## The second implementation on this machine

Stock SQLite 3.50.2, built from the pinned public-domain amalgamation, is the
reader that must accept your files, the writer whose files you must read, and
the second opinion every query answer can be checked against.

While you are developing it is the `sqlite3` on your `PATH`, installed at
`/usr/local/bin/sqlite3`; `sqlite3 --version` reports the `3.50.2` this
document means. Use it freely. The verifier keeps its own copy of the same
pinned build and hands a test case its path in `$SQLITE3`, which resolves
inside `/tests` -- a tree you are told not to inspect and do not need. The two
are the same build, so an answer you check against the `sqlite3` on your
`PATH` is an answer checked against the one that grades you.

It is a development tool only. Nothing in `/app` may link against it, embed
it, vendor its source, or shell out to it at run time — the engine must be
your own implementation.

## Fixed choices

1. **Databases are created in WAL mode, with 4096-byte pages and UTF-8
   text.** A freshly created database has read and write version bytes of `2`
   at page-1 offsets 18 and 19, not `1`, and a page size of 4096 at offset 16.
   "Freshly created" fixes the *values*, not the moment they are written: the
   fields below are those the header carries once a write has introduced it,
   and the step whose requirement list grades a database that has not been
   written to yet says what such a file is (a zero-length file with no header
   at all, at step 11's checkpoint). Two further page-1 header fields are fixed
   here because the format leaves them to the writer and the steps whose
   requirements name them read them:

   - **No reserved space at the end of a page, unless an engaged page
     transform claims it.** With no such transform in force the reserved-size
     byte at offset 20 is `0`, so the usable page size is the whole 4096. The
     spill arithmetic for the largest inline payload is stated in terms of that
     usable size, and is given here in full so that this rule needs nothing
     else to be read alongside it: the format charges a table-leaf cell 35
     bytes of page and cell overhead, so the largest payload such a cell
     holds inline is `4096 - 35 = 4061` bytes, and a payload one byte larger
     spills onto an overflow page. Only step 01 grades that boundary, and
     only step 01's `TEST_INTERFACE.md` restates the arithmetic; the byte at
     offset 20 is fixed at every step, which is why the rule lives here.
     The byte is `0` on every file this engine writes unless a step's own
     requirement list gives the engine a page transform that claims room at
     the end of every page for its own per-page material, and that transform
     is engaged. Where a step's requirement fixes this byte, the requirement
     governs and this rule yields to it.
   - **The text encoding is UTF-8.** The four-byte field at offset 56 is `1`.
     The format also permits `2` and `3` (UTF-16le and UTF-16be) and stock
     sqlite3 reads all three, so this is a choice rather than a consequence:
     this profile makes it and a step whose requirements name the field grades
     it.

   Stock sqlite3 reads such a file without complaint.

   **This rule fixes the header of a database this engine creates.** It is not
   a description of every database this engine is asked to open. A database
   that stock `sqlite3` wrote is in rollback-journal mode, with read and write
   version bytes of `1` at offsets 18 and 19 rather than `2` — that is what
   the pinned 3.50.2 binary produces by default, measured on 2026-09-21 — and
   cases in this task do hand the engine exactly such a file and ask it to
   read from it. An engine that asserts its own profile when it opens a file
   it did not create fails those cases. The page size and text encoding stock
   writes by default are the same 4096 and UTF-8 this rule fixes, so the
   journal mode and the two version bytes are the whole of the difference.
2. **Schema SQL text is normalised, not stored verbatim.** The statement
   text `sqlite_schema.sql` holds is re-printed from the parsed statement,
   not copied from the source. Stock SQLite preserves the original bytes;
   this engine does not, and the tests assert the normalised form — so the
   rule is stated here rather than left to be inferred from an example.

   - **Spacing.** Exactly one space before the opening parenthesis of any
     parenthesised list — a column list, a constraint, a type's parameters —
     and one space after each comma. Runs of whitespace are collapsed, so a
     statement spread over several lines reads back on one.
   - **Binary operators are surrounded by single spaces**, so `x>0` reads
     back as `x > 0`.
   - **Table constraints are re-printed after the column list**, in the order
     they were written, each with that same space before its parenthesis.

   Written and read back:

   | Written | Stored |
   |---|---|
   | `CREATE TABLE a(x INTEGER)` | `CREATE TABLE a (x INTEGER)` |
   | `CREATE TABLE b(  y  TEXT  ,   z   REAL )` | `CREATE TABLE b (y TEXT, z REAL)` |
   | `CREATE TABLE c(x INTEGER, PRIMARY KEY(x), CHECK (x>0))` | `CREATE TABLE c (x INTEGER, PRIMARY KEY (x), CHECK (x > 0))` |
   | `CREATE TABLE d(m int, n VarChar(20))` | `CREATE TABLE d (m int, n VarChar (20))` |
   | `create view v as select id,tag from base where tag is not null` | `CREATE VIEW v AS SELECT id, tag FROM base WHERE tag IS NOT NULL` |
   | `create view w as select region, sum(amt) as t from sale group by region` | `CREATE VIEW w AS SELECT region, sum (amt) AS t FROM sale GROUP BY region` |

   Identifiers, type names and literals keep the source's spelling:
   `INTEGER` stays `INTEGER`, `int` stays `int`, `VarChar` stays `VarChar`,
   `'hi'` stays `'hi'`. Keywords are emitted upper-case, which is why
   `create table` reads back as `CREATE TABLE`. What is *not* promised is the
   spelling of an operator: the re-print emits it from the parsed expression,
   so an alternative spelling may come back as the canonical one. This rule
   applies to every statement that writes a `sqlite_schema` row, not only to
   `CREATE TABLE` — a `CREATE VIEW`'s stored text is the re-print of its
   defining `SELECT` under these same spacing, operator and keyword rules, as
   the last two example rows show, and where a step grades a view's stored
   text it is this rule that fixes it. What the re-print preserves is the
   expression structure, not the source's punctuation: parentheses the source
   wrote are kept and none are added.
3. **Free pages are reused last-in-first-out.** Page-internal defragmentation
   during B-tree balancing is required. Opportunistic compaction of the whole
   file on free-page reuse is not performed, so file sizes need not match what
   stock SQLite would produce for the same statements.
4. **NULL renders as the empty string** in result output, and no
   `-nullvalue` setting changes that. **A real renders with a decimal
   point**: a real whose value is integral prints as `0.0`, `3.0` and
   `-725.0`, never as `0`, `3` or `-725`, so a real result and an integer
   result of the same value are told apart in the output without calling
   `typeof()`. That much is stock `sqlite3`'s `-list` rendering too.
   Magnitudes large or small enough to print in exponent form are where this
   rule stops, and the exponent spelling is deliberately **not** fixed here.
   The measurement is the reason: across the release chain the reference
   prints `1e16`, then `1e+16`, then `1.0e+16`, changing spelling twice
   along the way, while stock 3.50.2 prints `1.0e+16` throughout — and the
   two cross into exponent form at different magnitudes, stock at `1e15`
   where the reference is still printing `1000000000000000.0`. Nothing
   stable is there to fix. So **no test asserts the rendering of a real
   outside the plain decimal range**, and none may. Inside that range the
   decimal point is what this rule fixes, and it holds at every checkpoint.
5. **A non-empty blob renders as its raw bytes**, not as a word or a
   hexadecimal rendering. A blob holding the three bytes `A`, `B`, `C` prints
   as `ABC` and is indistinguishable in the output from the text value `'ABC'`;
   only `typeof()` tells them apart. A zero-length blob prints as an empty
   field. This is stock `sqlite3`'s `-list` rendering, which the profile
   follows.
6. **A refused statement is reported on standard output, and the shell keeps
   going.** When a statement cannot be executed, the shell writes a report to
   standard output — not standard error; it produces no result rows for that
   statement; it continues with the next statement in the stream; and the
   process still exits `0`. A batch is not abandoned at its first failure.

   **Put `×` on the first line of every report you write.** That is the
   obligation, and the marker is the one part of a report's form that is
   normative, because it is how the harness *finds* the report in the stream.
   Everything after it is free text, under rule 7.

   The harness recognises two forms, not one: a line carrying `×` anywhere on
   it, or a line beginning with the literal text `Runtime error:`. The second
   exists because the reference engine does not use the marker everywhere.

   How far that goes is worth stating exactly, because it is narrower than it
   sounds. Measured across this release chain with a *statement-level syntax*
   error (`SELEC 1;`), the reference writes `× near "SELEC": syntax error`
   from `v0.0.15` onward, so that class does carry the marker. Measured with a
   refusal raised **inside a scalar function** — `json_extract('{"a":1}','a')`,
   a malformed JSON path — it does not: the reference writes `Runtime error:
   JSON path error near: "a"` at `v0.0.14`, and then a bare `Parse error: Bad
   json path: a` with no marker at all from `v0.0.15` through `v0.1.0`, adding
   `×` only from `v0.1.2`. So a **parse** error is not uniformly marked, and a
   bare `Parse error: ...` line matches neither recognised form. Do not read
   the reference's habit as a second sanctioned spelling: `Parse error:` is
   not recognised, which is the concrete reason the rule is `×` on the first
   line of every report you write, without exception. The
   `error report` observable in `TEST_INTERFACE.md` states the recognition
   rule exactly.

   Read the second form as tolerance rather than as a choice offered to you.
   It is recognised so that a shell which follows the reference at an early
   release is not failed for doing so, but not every case applies it, and a
   shell that reports only that way fails the cases that do not. `×` on every
   report is the form that always works, which is why it is the rule.
7. **Everything after the marker is free text.** No test compares the wording
   of an error message: not the message, not an error code, not a position, not
   a SQLSTATE. What a test compares is the *shape* described in rule 6 — that a
   report appeared at that point in the output, that the statement yielded no
   rows, and that what follows still ran. Rule 6's marker is the exception that
   makes rule 7 checkable, not a counter-example to it: the harness has to
   recognise a line as a report before it can discard what the line says.
   `TEST_INTERFACE.md` gives the exact canonicalisation.
8. **The exit status reports process-level failure only.** An **unusable
   command-line argument** — one the shell does not recognise, or one whose
   value it will not accept — is rejected before any SQL is read, and that
   invocation exits `2`, writes **nothing at all to standard output**, and
   **leaves no database file behind**: the argument is refused before the
   database path is opened or created, so a mistyped flag cannot leave a
   database on disk as though the command had run. Its diagnostic goes to
   standard error, where nothing reads it. A panic exits `101` with its message
   on standard error. SQL that the engine refuses does not move the exit
   status — see rule 6.
9. **This engine's surface is wider than SQLite's, and the extra surface has
   no second opinion.** Vector values and their functions, change-data
   capture, and encryption exist here and not in stock SQLite, which refuses
   the statements that reach them. No test cross-checks the *results* of that
   surface against stock sqlite3, because there is nothing there to compare
   against. That is a statement about the *second opinion*, not about
   grading: where a step's own requirement list fixes what one of these
   functions returns, that value is compared against the requirement like any
   other result, and the requirement is where the value comes from. What the
   tests still require of the file is this: an unencrypted database
   holding vector values or a capture table is ordinary SQLite on disk, and
   stock sqlite3 must open it, return `ok` from `PRAGMA integrity_check`, and
   read every table it can name. An encrypted database is the one place in
   this document where that does not hold — its pages are ciphertext, stock
   sqlite3 reports the file as malformed, and that is the intended result
   rather than a compatibility failure.

## The durability rule

A process that writes and then exits must leave a database that any later
reader — this engine or stock `sqlite3` — sees complete. In practice that
means the writing invocation must not return until its committed data is
recoverable from the files on disk. Tests that write in one invocation and
read in another depend on this, and a case that loses data across the process
boundary fails.

Three consequences are graded, and the first two differ from what stock
`sqlite3` does:

- **A write leaves a `-wal` file behind.** The engine does not unlink the
  write-ahead log when the last connection goes away: after a writing
  invocation `main.db-wal` is still on disk, and that is the state a reading
  invocation must cope with. Stock `sqlite3` removes the `-wal` as it closes,
  so it is not a model for this behaviour.

  What the log still *holds* at that moment is deliberately not fixed. A
  writer may leave the committed frames in it, or may have folded them into
  the database file before exiting, and both are conforming — so no test
  asserts that the log is non-empty, and none asserts that it is empty. What
  *is* fixed is the pair: `main.db` together with whatever `main.db-wal`
  holds must carry every committed row. `PRAGMA wal_checkpoint` is how you
  ask for the fold explicitly, and it is the only way to *require* it.

  Latitude about which of the two holds the rows is not latitude to leave
  both empty, and a test that reads the log's bytes does not treat silence as
  conformance. Such a test grades the format of a log that has frames in it;
  where it finds none it grades the other half of the pair instead, and the
  bar there is positive evidence — `main.db` carrying the WAL-mode header
  bytes, passing stock `sqlite3`'s `integrity_check`, and giving back every
  committed row. An invocation that writes nothing at all satisfies neither
  branch and fails. Both branches are live in this task: measured over the
  release chain on 2026-09-21, a three-row write leaves a 12392-byte log at
  every checkpoint from the third to the second-to-last, and the final
  checkpoint folds the log in and truncates the file to zero bytes. Note what
  that does and does not change: the file is still **there** after the write —
  it is present in the directory at every checkpoint on the chain, and the
  first consequence above holds throughout. What varies is only its length.

  That latitude is about the rows, and it stops where a requirement names a
  field of the **page-1 header**. Where a requirement says a reader reading
  only the header sees such a field, the obligation falls on the writing
  invocation: the field reads current out of `main.db` itself once that
  invocation has exited, whatever the log still holds, so a case may read it
  with `od` immediately afterwards with no checkpoint in between. Rule 1's
  format fields are graded that way at every step, and so is the schema
  cookie at the step whose requirements name it. The one page-1 field this
  does not cover is the in-header database size at offset 28, which tracks
  the fold rather than the database — and the single requirement that names
  it says so itself, stating what the file carries *after* a
  `PRAGMA wal_checkpoint`, so the case grading it runs that checkpoint first.
- **A connection that only reads changes nothing.** Re-running a SELECT leaves
  both `main.db` and `main.db-wal` byte-identical: no frame is appended for a
  read, and the log header is not rewritten on open.
- **A simulated crash is a file copy, and that is the only mechanism used.**
  No test kills a process, pulls power, or injects a fault. Where a test says
  "after a simulated crash" it means exactly this: the writer exits, then
  `main.db` and `main.db-wal` are copied as they lie on disk into a fresh
  directory — **and `main.db-shm` is deliberately not copied**, because it is
  derived state a real crash would leave stale or absent — and the copy is
  opened there on its own. No checkpoint is run before the copy and the copy
  is never closed cleanly first. Recovery from that pair must yield either
  the pre-transaction or the post-transaction state and never a mixture, so
  an implementation that needs a live `-shm`, or that cannot rebuild it, does
  not survive the copy.

The durability rule above is what makes all three safe: a `-wal` that survives
the writer must still be complete enough, together with the database file
beside it, for the next reader to recover the committed data.

## What is never asserted

The tests never compile, import, link against or type-check your code. Crate
names, module paths, source layout, internal APIs, log output, timing and
memory use are entirely yours. Grading crosses the process boundary only: the
shell is run as a subprocess, and its standard output, its exit status and the
bytes it wrote to disk are what is compared.
