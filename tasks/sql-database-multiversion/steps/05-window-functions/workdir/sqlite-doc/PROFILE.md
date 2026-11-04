# Benchmark profile — step 05 of 5, SQLite 3.25.0

This file is **task-owned**. It does not define SQL semantics: every semantic
statement in scope lives in the verbatim upstream pages beside it. What this
file does is draw the boundary — which documented behaviour this benchmark
requires, and what it deliberately leaves out — so that the target is finite.

Rules of reading:

* If a behaviour is listed as **in scope** below, the upstream page named for it
  is normative and the verifier may exercise anything that page states about it.
* If a behaviour is listed as **out of scope**, the verifier never exercises it,
  and you are free to omit it, reject it, or implement it as you like.
* If a behaviour is in neither list, it is out of scope.
* `../TEST_INTERFACE.md` §6 lists the *observations* that are never scored
  (message wording, query plans, internal layout); that list applies on top of
  this one.

Scope is **cumulative**: everything in scope at an earlier step stays in scope
here, and the verifier re-exercises it.

**Supersessions — read this file to the end before relying on any one
section.** Scope is cumulative, but a *rule* is not permanent. Where upstream
reworded a rule between two of this benchmark's releases, the step that ships
the reworded page restates it in its own section and says there which earlier
section it **supersedes**, and the earlier section warns that this may happen
to it. So a rule you were scored on at an earlier step can be overturned lower
down this file, and where two sections state different rules for the same
thing the later-numbered one is the one in force. Two such pairs exist by step
4 — and step 5, the last step, adds none, so this table is complete — each in
force from the step that introduces the superseding section onward:

| Superseding section | Supersedes | Subject |
| --- | --- | --- |
| §3.5 (from step 3) | §5's `PRAGMA index_list` row | the pragma's result columns |
| §4.6 (from step 4) | the fifth scored rejection of §3.4 | what a partial index's `WHERE` clause may contain |

If a section named in that table is not present in the copy of this file in
front of you, that step has not happened yet and the earlier rule still stands
in full.

## Step 1 — the core engine (SQLite 3.8.0)

Nothing is inherited: this step builds the whole substrate. The capability
below is what the verifier exercises.

### 1. Statements in scope

Every statement is as documented on its own `lang_*.html` page; the syntax
diagrams there are normative, including the clauses listed as optional.

| Statement | Page | In scope |
| --- | --- | --- |
| `CREATE TABLE` | `lang_createtable.html` | column definitions with a declared type or none, `IF NOT EXISTS`, `PRIMARY KEY` (column and table form, `ASC`/`DESC`, `AUTOINCREMENT`), `NOT NULL`, `UNIQUE`, `CHECK`, `DEFAULT` (literal and parenthesised constant expression), `COLLATE`, and `CREATE TABLE … AS SELECT` |
| `DROP TABLE` | `lang_droptable.html` | with and without `IF EXISTS` |
| `ALTER TABLE` | `lang_altertable.html` | `RENAME TO` and `ADD COLUMN` |
| `CREATE INDEX` | `lang_createindex.html`, `partialindex.html` | `UNIQUE`, `IF NOT EXISTS`, multi-column, `ASC`/`DESC` ordering, `COLLATE`, and the partial-index `WHERE` clause |
| `DROP INDEX` | `lang_dropindex.html` | with and without `IF EXISTS` |
| `INSERT` | `lang_insert.html` | `VALUES` with one or many row lists, `DEFAULT VALUES`, `INSERT … SELECT`, an explicit column list, and every `INSERT OR …` conflict form |
| `REPLACE` | `lang_replace.html` | as a synonym for `INSERT OR REPLACE` |
| `UPDATE` | `lang_update.html` | `SET` with several columns, `WHERE`, and every `UPDATE OR …` conflict form |
| `DELETE` | `lang_delete.html` | with and without `WHERE` |
| `SELECT` | `lang_select.html` | result columns with `*`, `table.*`, expressions and `AS` aliases; `DISTINCT` and `ALL`; `FROM` with a table, a subquery, or several joined sources; `INNER`/`LEFT OUTER`/`CROSS` joins with `ON` or `USING`; `WHERE`; `GROUP BY` with `HAVING`; the compound operators `UNION`, `UNION ALL`, `INTERSECT`, `EXCEPT`; `ORDER BY` with `ASC`/`DESC`/`COLLATE` and by result-column position or alias; `LIMIT` with `OFFSET` |
| `BEGIN` / `COMMIT` / `END` / `ROLLBACK` | `lang_transaction.html` | including `DEFERRED`, `IMMEDIATE` and `EXCLUSIVE` |
| `SAVEPOINT` / `RELEASE` / `ROLLBACK TO` | `lang_savepoint.html` | nested savepoints |
| `PRAGMA` | `pragma.html` | the subset in section 5 below |
| `EXPLAIN`, `EXPLAIN QUERY PLAN` | `lang_explain.html`, `eqp.html` | must parse and execute without error; **output never compared** |
| `VACUUM` | `lang_vacuum.html` | must run without error and preserve the database contents |

Comments (`--` and `/* */`) are in scope, as documented on `lang_comment.html`.
Identifier quoting with `"…"`, `[…]` and backticks, and the keyword rules, are
in scope as documented on `lang_keywords.html` and `lang_naming.html`.

Two consequences of those pages that the verifier does observe, spelled out
here because they decide what a scored comparison prints:

* **`ALTER TABLE … ADD COLUMN`.** Rows that already existed when the column was
  added read back the column's `DEFAULT` value, or `NULL` when the column
  definition has no `DEFAULT`. This is why `lang_altertable.html` requires a
  non-NULL default whenever the added column is `NOT NULL`.
* **`ALTER TABLE … RENAME TO`.** The rows in `sqlite_master` follow the table:
  its own `name` and `tbl_name`, and the `tbl_name` of every index on it,
  become the new name, and the indexes stay attached and stay in use. The exact
  *text* left in `sqlite_master.sql` after a **table** rename is **not**
  compared. (This says nothing about `ALTER TABLE … ADD COLUMN`, which no step
  observes through that text either, nor about any later step's own sections.)

### 2. The value model

`datatype3.html` and `nulls.html` are normative and fully in scope:

* the five storage classes NULL, INTEGER, REAL, TEXT, BLOB, and `typeof()`;
* type-affinity determination from a declared type name, including the rules
  for a name containing `INT`, `CHAR`/`CLOB`/`TEXT`, `BLOB` or none,
  `REAL`/`FLOA`/`DOUB`, and otherwise NUMERIC;
* affinity applied on assignment (INSERT and UPDATE) and in comparisons;
* the sort order across storage classes, and comparison operator semantics;
* three-valued logic for NULL in `WHERE`, `AND`/`OR`/`NOT`, `IS`/`IS NOT`,
  `IN`, aggregate functions, `DISTINCT`, `GROUP BY`, `UNION` and `ORDER BY`;
* `INTEGER PRIMARY KEY` as an alias for the rowid, and `AUTOINCREMENT`
  behaviour with `sqlite_sequence` (`autoinc.html`);
* the `BINARY`, `NOCASE` and `RTRIM` collating sequences.

### 3. Expressions

`lang_expr.html` is normative and in scope, including: literal values (integer,
floating point, string, blob `X'…'`, `NULL`); bound parameters in the `?`,
`?NNN`, `:name`, `@name` and `$name` forms; the arithmetic, string
concatenation, bitwise, comparison and logical operators with their documented
precedence; `IS` / `IS NOT`; `IN` over a value list and over a subquery;
`BETWEEN`; `LIKE`, `GLOB` and their `ESCAPE` clause; `EXISTS` and scalar
subqueries; `CASE` in both forms; `CAST`; `COLLATE`.

One operator on that page's list is in scope **only as a rejection**, and the
reason is worth spelling out because the page hedges. `lang_expr.html` says the
`REGEXP` operator "is a special syntax for the regexp() user function", that
"No regexp() user function is defined by default and so use of the REGEXP
operator will **normally** result in an error message", and that a run-time
`regexp` function would change that. Section 8 puts user-defined functions out
of scope at every step, so that route is closed here and the hedge has nothing
left to cover: **`X REGEXP Y` must fail**, leaving the database unchanged
(`../TEST_INTERFACE.md` §2.3), and the verifier scores that. `MATCH` is the
same shape — a special syntax for an application-defined `match()` — but the
verifier never writes it, so it is unscored in both directions.

### 4. Functions

* **Scalar** (`lang_corefunc.html`): `abs`, `changes`, `char`, `coalesce`,
  `glob`, `hex`, `ifnull`, `instr`, `length`, `like`, `lower`, `ltrim`, `max`,
  `min`, `nullif`, `quote`, `replace`, `round`, `rtrim`, `substr`,
  `total_changes`, `trim`, `typeof`, `unicode`, `upper`, `zeroblob`,
  `last_insert_rowid`, `sqlite_version`. `like()` and `glob()` are the function
  spellings of the `LIKE` and `GLOB` operators of section 3; as
  `lang_corefunc.html` states, their arguments are **reversed** relative to
  the infix operators, so `like(Y,X)` answers `X LIKE Y`.
* **Aggregate** (`lang_aggfunc.html`): `avg`, `count(*)`, `count(X)`,
  `group_concat` in both arities, `max`, `min`, `sum`, `total`, each with and
  without `DISTINCT` wherever the form admits it — `count(*)` has no argument
  to be distinct over, and a `DISTINCT` aggregate takes exactly one argument,
  so `group_concat` admits it only in its one-argument form.

Those two lists are **closed for this section**. Scope is cumulative, so later
steps add functions of their own in their own sections of this file — read it
to the end, as the preamble says, and take the union of every function list it
gives you. A name in none of them is one you need not provide, and which extra
names an implementation happens to provide is its own business.

One behaviour about such a name is nevertheless **in scope and scored**, so it
is listed here rather than left to the preamble's "in neither list, therefore
out of scope" rule: **calling a function the implementation does not provide is
an error.** The statement fails and the database is unchanged
(`../TEST_INTERFACE.md` §2.3); the name is not silently accepted, and the call
does not return NULL or any other value. Message wording is not scored, as
always.

### 5. `PRAGMA` statements in scope

From `pragma.html`: `page_size` (read only), `encoding` (read only, must
report `UTF-8`),
`user_version` (read and write), `journal_mode` (read, and set to `delete`,
`truncate`, `persist`, `memory`, `off` and `wal`), `wal_checkpoint`,
`integrity_check` (must report `ok` for a healthy database),
`table_info`, `index_list`, `index_info`, `cache_size`, `synchronous`.

`pragma.html` names the *information* each introspection pragma returns but not
the shape of the result set, and a verifier has to compare a result set. The
required shapes are therefore fixed here. Each is a query result like any
other, so the CLI prints it in the current output mode.

| Pragma | Result columns, in this order | Rows |
| --- | --- | --- |
| `PRAGMA table_info(T)` | `cid`, `name`, `type`, `notnull`, `dflt_value`, `pk` | one per column of `T`, in declaration order. `cid` counts from 0; `type` is the declared type text, empty when the column was declared without one; `notnull` is 1 when the column has a `NOT NULL` constraint and 0 otherwise; `dflt_value` is the text of the `DEFAULT` clause, or NULL when there is none; `pk` is 0 for a column outside the primary key and otherwise the column's 1-based position within it |
| `PRAGMA index_list(T)` | `seq`, `name`, `unique` | one per index on `T`, including the automatic ones. `seq` is the row's own 0-based position in this result set, so a table with a single index reports `seq` 0. `unique` is 1 for a `UNIQUE` index and 0 otherwise. **Row order is not compared.** *This column list is the one required until a later step supersedes it: a later release's `pragma.html` enumerates more output columns than this, and the step that ships that page restates the shape in its own section of this file and says that it supersedes this row. Read this file to the end before relying on the column list here* |
| `PRAGMA index_info(I)` | `seqno`, `cid`, `name` | one per column of index `I`, in index-key order; `seqno` is the 0-based rank within the index and `cid` the column's `table_info.cid` |
| `PRAGMA integrity_check` | one column | the single row `ok` when the database is healthy |
| `PRAGMA journal_mode` | one column | the current mode as a **lower-case** string (`delete`, `truncate`, `persist`, `memory`, `off`, `wal`). A database that has never been switched to another mode is in `delete` mode — `pragma.html` calls it "the normal behavior" — so the first query on a newly created database reports `delete`. Setting the mode returns the mode in force after the statement, in the same form |
| `PRAGMA user_version` | one column | the current user version; `PRAGMA user_version = N` returns no rows |
| `PRAGMA encoding` | one column | the single row `UTF-8` |
| `PRAGMA page_size` | one column | the page size in bytes |
| `PRAGMA wal_checkpoint` | — | must run without error; **its result row is never compared** |

`PRAGMA page_size` and `PRAGMA encoding` are in scope **as queries only**. The
verifier never writes either as a setter; section 8 already excludes
`PRAGMA encoding = X`, and `PRAGMA page_size = N` is excluded for the same kind
of reason — what a page-size change means depends on whether the database file
has been written to yet, which nothing in scope pins down — so whatever an
implementation does with it, including printing something, is unscored.

`PRAGMA cache_size` and `PRAGMA synchronous` must parse and run in both forms,
and the value either one *reports* is not compared: the verifier never writes
`PRAGMA cache_size` or `PRAGMA synchronous` as a query. What it does write is
the assignment form, and **`PRAGMA cache_size = N` and `PRAGMA synchronous = X`
return no rows** — they write nothing at all to standard output, as
`PRAGMA user_version = N` does in the table above.

That is a fact about these two pragmas and does **not** generalise to the
assignment form of every pragma: `PRAGMA journal_mode = X` *does* return a row,
as its own row in the table above says. It is stated here because standard
output is compared byte for byte (`../TEST_INTERFACE.md` section 2.3), so
printing where this file says a statement is silent fails a comparison exactly
as printing a wrong value would.

### 6. Catalog

The `sqlite_master` table is in scope exactly as
**`fileformat2.html` §2.6
(*Storage Of The SQL Database Schema*)** defines it: the columns `type`,
`name`, `tbl_name`, `rootpage` and `sql`, one row per table and per index, the
five text normalisations applied to the `CREATE` statement stored in `sql`, and
`sql` being `NULL` for an index SQLite created itself.

One property of that stored text the page leaves implicit, and which every
group that reads `sql` compares: **the text is the `CREATE` statement without
the `;` that terminated it.** The page calls `sql` "a copy of the original
CREATE statement text that created the object", and `../TEST_INTERFACE.md` §2.1
makes `;` a statement *separator* rather than a part of the statement, so the
semicolon was never part of what is copied. It is said here because it is not
one of the five normalisations and would otherwise have to be inferred.
§2.6.1 (*Internal Schema Objects*) names
those automatic indexes `sqlite_autoindex_TABLE_N` and reserves every name beginning
with `sqlite_` for internal use, so an attempt to `CREATE` a table or index
with such a name is rejected. **`rootpage` values are internal and are never
compared** beyond being a positive page number for a table or an index.

`sqlite_sequence` is in scope for `AUTOINCREMENT` tables with the two columns
`name` and `seq` that `fileformat2.html`
§2.6.2 gives it, holding the largest
`INTEGER PRIMARY KEY` the named table has ever used.

### 7. Transactions and durability

`lang_transaction.html`, `atomiccommit.html` and `wal.html` are normative:

* a committed transaction survives closing and reopening the database file, and
  survives the process exiting;
* a rolled-back transaction leaves no trace, including a transaction rolled
  back by a statement-level `ON CONFLICT ROLLBACK`;
* a statement that violates a constraint leaves the database exactly as it was,
  according to the conflict resolution algorithm on `lang_conflict.html`
  (`ROLLBACK`, `ABORT`, `FAIL`, `IGNORE`, `REPLACE`), with `ABORT` the default;
* a database that has not been switched to WAL uses a rollback journal, so
  file header bytes 18 and 19 (`../TEST_INTERFACE.md` section 4) read `1`;
* `PRAGMA journal_mode = wal` switches the database to write-ahead logging,
  is persistent across connections, sets file header bytes 18 and 19 to `2`,
  and `PRAGMA wal_checkpoint` runs without error; switching back to a rollback
  journal mode restores `1`;
* two separate `sqlite3` **processes** opened on the same file one after the
  other see each other's committed data, in both rollback-journal and WAL mode.

### 8. Out of scope at every step

The verifier never exercises these, in this step or a later one:

* `CREATE TRIGGER`, `CREATE VIEW`, `CREATE VIRTUAL TABLE`, and every extension
  including FTS and R-Tree;
* `FOREIGN KEY` enforcement (a foreign-key clause may be parsed and ignored;
  `PRAGMA foreign_keys` is not used);
* `ATTACH` / `DETACH` and multi-database statements;
* `WITHOUT ROWID` tables;
* `REINDEX`, `ANALYZE` and the `sqlite_stat*` tables;
* user-defined functions, collations, virtual file systems and any C API beyond
  `TEST_INTERFACE.md` §3;
* date and time functions (`date`, `time`, `datetime`, `julianday`,
  `strftime`) and the `CURRENT_DATE`/`CURRENT_TIME`/`CURRENT_TIMESTAMP`
  keywords;
* `random()`, `randomblob()`, `soundex()`, `load_extension()`;
* text encodings other than UTF-8, and `PRAGMA encoding` as a *setter*;
* concurrent access from more than one process **at the same time**, busy
  handling under contention, and `SQLITE_BUSY` recovery behaviour;
* on-disk compatibility with upstream SQLite in either direction.
## Step 2 — common table expressions and `VALUES` (SQLite 3.8.3)

Everything in step 1's section above stays in scope and the verifier
re-exercises a sample of it. Step 1's sections keep the numbers 1-8; this
step's sections are numbered `2.1` upward so a cross-reference is unambiguous.

Step 2 refreshed the whole documentation set: every page beside this file
became its SQLite 3.8.3 edition rather than step 1's 3.8.0 edition, and
`lang_with.html` was added. The `sqlite-doc/README.md` shipped with whichever
step you are reading this at says what is new and what changed at *that* step.

Step 2 adds three things to step 1's profile, and nothing else:

* the `WITH` clause and ordinary common table expressions (§2.1);
* the `VALUES` clause used as a query rather than only inside `INSERT` (§2.2);
* the `printf()` SQL function (§2.3).

### 2.1 The `WITH` clause — ordinary common table expressions

`lang_with.html`, new at 3.8.3, is normative for this section,
together with the passages of `lang_select.html` headed *The WITH Clause*,
*The VALUES clause* and the compound-`SELECT`, `ORDER BY` and `LIMIT` sections.

In scope:

* a `WITH` clause prepended to a `SELECT`, an `INSERT`, an `UPDATE` or a
  `DELETE` statement — the four statements `lang_with.html` names;
* a single `WITH` clause defining **several** comma-separated common table
  expressions, where a later one may reference an earlier one;
* a common table expression written **with** a parenthesised column-name list
  and one written **without** one;
* a common table expression referenced **more than once** in the same
  statement, including joined to itself under two different aliases;
* a common table expression referenced from a **subquery** of the statement
  that defines it — in `FROM`, in an `IN` subquery, in a scalar subquery and in
  `EXISTS`;
* a `WITH` clause at the beginning of a **subquery**, which `lang_with.html`
  explicitly permits ("at the beginning of a top-level SELECT statement or at
  the beginning of a subquery");
* a common table expression whose body is any `SELECT` already in scope at step
  1 — a join, an aggregate query with `GROUP BY`/`HAVING`, a compound select, a
  query over an indexed table — or a `VALUES` clause (§2.2).

Three consequences of `lang_with.html` are spelled out here because they decide
what a scored comparison prints, in the same way step 1 §1 spells out
`ADD COLUMN`:

* **Lifetime and scope.** "Common Table Expressions … exist only for the
  duration of a single SQL statement" is observable: the name is unknown to the
  *next* statement, and using it there is an error.
* **Shadowing.** A common table expression "works as if it were a view that
  exists for the duration of a single statement", so inside the statement that
  defines it its name takes precedence over a real table of the same name. In
  every other statement the same name means the real table again.
* **The column-name list.** When a common table expression is written with a
  column-name list, that list names its columns, and it must contain exactly as
  many names as the expression's query returns columns — too few names and too
  many names are both errors. When it is written without a list, its columns
  are named as its own query names them.

Rejections the verifier scores, each of them stated above or on a shipped page:

* a common table expression's name used outside the statement that defines it;
* a column-name list whose length does not match the number of columns its
  query returns;
* a `WITH` clause prepended to the second or a later `SELECT` of a compound
  select — `lang_with.html` *Limitations And Caveats* ("The WITH clause cannot
  be prepended to the second or subsequent SELECT statement of a compound
  select") and `lang_select.html` ("The WITH clause must occur on the first
  SELECT of a compound SELECT. It cannot follow a compound-operator").

### 2.2 The `VALUES` clause as a query

`lang_select.html`'s *The VALUES clause* section is normative:
`VALUES(expr-list)` means the same thing as `SELECT expr-list`, and
`VALUES(list-1),…,(list-N)` means the same thing as
`SELECT list-1 UNION ALL … UNION ALL SELECT list-N`.

In scope:

* a `VALUES` clause as a **complete statement**, with one row-value list or
  several, holding literals, expressions and `NULL`;
* a `VALUES` clause as a **subquery in `FROM`**, and inside the `IN`, scalar
  and `EXISTS` subquery forms of step 1 §3 — it is a `SELECT` by the
  equivalence above, and `lang_select.html` defines a `FROM` subquery as a
  `SELECT` statement in parentheses;
* a `VALUES` clause as an **operand of a compound operator** (`UNION`,
  `UNION ALL`, `INTERSECT`, `EXCEPT`), on either side, subject to the
  restriction below;
* a `VALUES` clause as the source of an **`INSERT`**, both directly and through
  a `FROM` subquery, a compound select or a common table expression;
* a `VALUES` clause as the **body of a common table expression** (§2.1), which
  is how the verifier gives its columns names.

Two consequences, spelled out here for the same reason as in §2.1:

* **Row order.** A `VALUES` clause with N row-value lists produces exactly
  those N rows, in the order they are written. `lang_select.html` forbids an
  `ORDER BY` on a statement whose right-most element is a `VALUES` clause, so
  this is the one row order at this step that the verifier compares without an
  `ORDER BY`, and it compares it only for the rows a `VALUES` clause itself
  contributes. (`../TEST_INTERFACE.md` §5 has the general rule.)
* **Row width.** Every row-value list of one `VALUES` clause must have the same
  number of terms, and where a `VALUES` clause is an operand of a compound
  operator its width must match the other operands' — the compound rule of
  `lang_select.html` ("all the constituent SELECTs must return the same number
  of result columns") applied through the equivalence above. A mismatch is an
  error.

Rejections the verifier scores, all three stated on `lang_select.html`:

* `ORDER BY` after a `VALUES` clause. `lang_select.html`'s restrictions list
  says a `VALUES` clause cannot be followed by `ORDER BY` — as one sentence
  covering `ORDER BY` and `LIMIT` together at SQLite 3.8.3, and as two separate
  sentences from SQLite 3.9.0 onward, with the rule unchanged — and the same
  page adds "If the right-most element of a compound SELECT is a VALUES clause,
  then no ORDER BY clause is allowed on that statement";
* `LIMIT` after a `VALUES` clause, by the other half of that restriction and by
  "If the right-most simple SELECT is a VALUES clause then no LIMIT clause is
  allowed";
* row-value lists of differing width, and a compound operand whose width does
  not match.

### 2.3 The `printf()` SQL function

`lang_corefunc.html` gains `printf(FORMAT,...)` at 3.8.3; it joins the
closed scalar-function list of step 1 §4. The same page also gains
`likelihood(X,Y)` and `unlikely(X)`, which are query-planner hints; both are
**out of scope** and neither is ever called. The page states that `printf()`
"works like
the sqlite3_mprintf() C-language function and the printf() function from the
standard C library", and then states five rules that the verifier scores
directly: a missing or `NULL` `FORMAT` gives a `NULL` result; `%n` is silently
ignored and consumes no argument; `%p` is an alias for `%X`; `%z` is
interchangeable with `%s`; and a missing argument is taken to be `NULL`, which
is rendered as `0` for the integer conversions, `0.0`-style output for the
floating conversions, and an empty string for `%s`. An argument that *is* `NULL`
is rendered the same way as a missing one.

The conversions in scope are the standard C ones `%d`, `%i`, `%o`, `%x`, `%X`,
`%s`, `%f`, `%e`, `%E`, `%g` and `%%`, together with the `-`, `0`, `+` and
space flags, a field width and a precision. Three C rules that fix the exact
text of a floating conversion, and that the verifier therefore relies on:

* the default precision of `%f`, `%e` and `%E` is **6**;
* the exponent produced by `%e`/`%E` has **at least two digits**
  (`1.500000e+00`);
* `%g` takes its precision as a count of **significant digits**, defaulting to
  **6**, chooses `%e` style when the exponent is below −4 or not less than the
  precision and `%f` style otherwise, and **removes trailing zeros** from the
  fractional part along with a trailing decimal point (`0.5`, `100`, `0.25`).

Wherever a `printf()` call produces a **non-empty** result, that result is a
**TEXT** value: `typeof()` reports `text` for it whatever conversions the
format used. A `NULL` `FORMAT` gives `NULL` by the first rule above, and a
call whose whole result would be empty is left unscored in both directions by
§2.5.

Each argument is written with the conversion that matches its storage class —
`%d`/`%i`/`%o`/`%x`/`%X` for INTEGER, `%f`/`%e`/`%g` for REAL, `%s` for TEXT,
and a BLOB through `hex()` or `CAST(… AS TEXT)` first.

### 2.4 What step 2 does *not* change

`WITH` and `VALUES` are additive syntax and `printf()` is one more scalar
function. Every statement, value-model rule, catalog fact, pragma, durability
guarantee and C entry point of step 1 keeps exactly the meaning step 1 gave it,
and the verifier re-exercises a sample of each: DDL and DML, the value model,
transactions and durability, the command-line contract, and the C library.

"Sample" is meant literally: at step 2 these checks re-run a handful of step
1's own cases rather than step 1's whole suite. They are **gate** cases rather
than score groups: breaking an earlier milestone fails this step's gate
outright rather than costing a fraction of its score. One inherited rule is
deliberately left out of the sample — **`make clean`** (`../TEST_INTERFACE.md`
§1) is still a requirement of the workspace, and step 1 scores it, but step 2
scores no group on it. Nothing else about step 1 is relaxed: a rule the sample
happens not to reach is still binding, because step 3 starts from what you
leave here.

The one plumbing value that moves is the release string —
`../TEST_INTERFACE.md` §3.3.

### 2.5 Out of scope at step 2

In addition to step 1 §8, which still applies:

* **Recursive common table expressions.** `WITH RECURSIVE`, the `recursive-cte`
  form, and every hierarchical, graph, Mandelbrot and Sudoku example on
  `lang_with.html` are **out of scope**. The verifier never writes `RECURSIVE`
  and never writes a common table expression that references itself. You may
  reject it, and nothing is scored either way.
* `WITH` inside `CREATE TRIGGER`, which `lang_with.html` excludes anyway,
  because triggers are out of scope at every step.
* Whether a common table expression defined by a statement's own `WITH` clause
  is visible inside the **left-most** arm of that statement's top-level
  compound select. The verifier never writes such a query.
* Whether a bare `VALUES` statement may be preceded by a `WITH` clause.
  `lang_select.html` lists "a simple SELECT that consists of just a VALUES
  clause cannot be preceded by a WITH clause" among its restrictions; accepting
  it and rejecting it are both unscored. A `VALUES` clause as the **first
  element of a compound select** under a `WITH` clause is in scope and is
  scored, because the same sentence allows it.
* How a `VALUES` clause with **more than one** row-value list groups when it is
  not the left-most operand of a compound select. The verifier writes a
  multi-row `VALUES` clause only as the left-most operand; as any other operand
  it writes a single row-value list.
* The **column names** a bare `VALUES` clause gives its result columns. Nothing
  names them, so the verifier reaches those columns only by `*`, by ordinal
  position in `ORDER BY`, or by wrapping the `VALUES` clause in a common table
  expression with a column-name list.
* A table-alias column list on a `FROM` subquery (`… AS v(a,b)`); SQLite 3.8.3
  does not have it.
* `printf()` conversions outside §2.3's list — in particular `%c`, and the
  `sqlite3_mprintf()` extensions `%q`, `%Q` and `%w`, whose page is not shipped
  — and applying a conversion to a value whose storage class it does not match.
  A `printf()` call whose whole result is the empty string is never compared.
## Step 3 — JSON and indexes on expressions (SQLite 3.9.0)

Everything in steps 1 and 2's sections above stays in scope and the verifier
re-exercises a sample of it. Step 1's sections keep the numbers 1-8 and step
2's keep 2.1-2.5; this step's sections are numbered `3.1` upward so a
cross-reference is unambiguous.

Step 3 refreshed the whole documentation set: every page beside this file
became its SQLite 3.9.0 edition, `json1.html` and `expridx.html` were added,
and the command-line shell page was renamed by upstream from `sqlite.html` to
`cli.html`. The `sqlite-doc/README.md` shipped with whichever step you are
reading this at says what is new and what changed at *that* step.

Step 3 adds two capabilities to steps 1 and 2's profile, and nothing else:

* the JSON1 scalar function family and its path syntax (§3.1-§3.3);
* indexes on expressions (§3.4).

One inherited result shape moved with the release rather than with this step's
capability, and §3.5 records it so the profile and the shipped pages agree.

### 3.1 The JSON1 scalar function family

`json1.html`, new at 3.9.0, is normative for §3.1-§3.3. The functions in scope
are the **eleven scalar SQL functions** named in the table below — the eleven
the SQLite 3.9.0 edition of that page enumerates — and no others. A later
release's edition of the page documents more scalar functions than these
eleven; unless a later step's own section of this file puts one of them in
scope, it stays out of scope by the preamble's third rule.

| Function | `json1.html` section |
| --- | --- |
| `json(X)` | *The json() function* |
| `json_array(V1,V2,…)` | *The json_array() function* |
| `json_array_length(X)`, `json_array_length(X,P)` | *The json_array_length() function* |
| `json_extract(X,P1,P2,…)` | *The json_extract() function* |
| `json_insert(X,P,V,…)` | *The json_insert(), json_replace, and json_set() functions* |
| `json_object(L1,V1,…)` | *The json_object() function* |
| `json_remove(X,P,…)` | *The json_remove() function* |
| `json_replace(X,P,V,…)` | *The json_insert(), json_replace, and json_set() functions* |
| `json_set(X,P,V,…)` | *The json_insert(), json_replace, and json_set() functions* |
| `json_type(X)`, `json_type(X,P)` | *The json_type() function* |
| `json_valid(X)` | *The json_valid() function* |

They join the closed scalar-function list of step 1 §4 and are called the same
way as any function there — from a `SELECT` result column, a `WHERE` clause, an
`ORDER BY` term, an `INSERT` or `UPDATE` value, a common table expression
(§2.1) or a `VALUES` clause (§2.2). `../TEST_INTERFACE.md` §7 says where
they have to be available.

Each function's own section of `json1.html` is normative for it, **including
its worked examples**: the verifier compares the results those examples state.

Four consequences of that page are spelled out here because they decide what a
scored comparison prints, in the same way step 1 §1 spells out `ADD COLUMN`:

* **How a produced document is written — order, spacing and numbers.** This is
  the single most load-bearing paragraph of §3.1, because every JSON document
  the verifier compares is compared as exact text. `json1.html` states
  minification only for `json(X)` ("returns a minified version of that JSON
  string (with all unnecessary whitespace removed)") and for `json_remove(X)`
  with no path arguments ("the input X reformatted, with excess whitespace
  removed"), and it never states an ordering rule in prose at all — its worked
  examples
  are the whole evidence, and they are consistent. **This bullet therefore
  fixes the rule for this benchmark**, for every function of §3.1, so that a
  JSON result has one expected text:
  * **Order is preserved.** `json_object()` emits its label/value pairs in the
    order they were given and `json_array()` its elements in the order they
    were given; `json()`, `json_remove()` and the three mutators return the
    members of an object and the elements of an array in the order they appear
    in the input, with new members appended at the end. Every worked example on
    the page shows this (`json_object('a',2,'c',4)` → `'{"a":2,"c":4}'`,
    `json_insert('{"a":2,"c":4}','$.e',99)` → `'{"a":2,"c":4,"e":99}'`).
  * **Every produced document is minified**, not only `json()`'s: no space,
    tab or newline appears anywhere in the result except inside a JSON string
    value. A label is followed immediately by `:`, and consecutive members of
    an object or elements of an array are separated by a single `,` with
    nothing around it. Again every worked example on the page shows it.
  * **A number inside a produced document is written exactly as
    `../TEST_INTERFACE.md` §2.3 writes that value in a result column** — an
    INTEGER in decimal, and a REAL with at most 15 significant digits, trailing
    fractional zeros removed and always at least one digit after the point, so
    `json_array(2.5,-7)` is `[2.5,-7]` and `json_array(2.0)` is `[2.0]`, not
    `[2]`. §5 of that file applies here too: every floating-point value
    the verifier puts inside a compared document needs no more than 15
    significant digits, so §2.3's rule fixes its text exactly.
  * **A string inside a produced document is a JSON string**, quoted with `"`
    and escaped as RFC-7159 requires. The only escape any scored comparison
    contains is `\"`, in the form `json_array()`'s own worked example
    `json_array(1,null,'3','[4,5]','{"six":7.7}')` →
    `'[1,null,"3","[4,5]","{\"six\":7.7}"]'` fixes it.
* **What comes back is TEXT.** `json1.html` §3 (*Interface Overview*) says "The json1 extension
  (currently) stores JSON as ordinary text", so every function that returns a
  JSON document — `json`, `json_array`, `json_object`, `json_remove`, and the
  three mutators — returns an SQL **TEXT** value, and `typeof()` reports
  `text` for it. `json_type()` returns TEXT too. `json_valid()` and
  `json_array_length()` return **INTEGER** (`json_array_length()` returns NULL
  when its path selects nothing, as its own section says).
  `json_extract()` is the exception and its own section fixes its type — see
  below.
* **`json_extract()`'s result type.** Its section states it exactly: "the SQL
  datatype of the result is NULL for a JSON null, INTEGER or REAL for a JSON
  numeric value, an INTEGER zero for a JSON false value, an INTEGER one for a
  JSON true value, the dequoted text for a JSON string value, and a text
  representation for JSON object and array values", and with more than one path
  argument the result is "SQLite text which is a well-formed JSON array holding
  the various values".

### 3.2 JSON arguments, PATH arguments and VALUE arguments

`json1.html` gives three rules in three consecutive sections that apply to
every function of §3.1, and all three are in scope. Each is cited here by its
title, because `json1.html` renumbers its sections between SQLite releases:

* ***JSON arguments*** (§3.1 at this release).
  The leading argument "can be a JSON object, array,
  number, string, or null", and an SQLite numeric value "interpreted as" a JSON
  number is accepted there. Whitespace before, after and inside a JSON input is
  ignored. The accepted grammar is "exactly the rfc-7159 JSON syntax — no more
  and no less", so an unquoted object label, a trailing comma and an
  unterminated container are all *not* JSON. Text that is not well-formed JSON
  makes the function throw — except `json_valid()`, which answers `0`.
* ***PATH arguments*** (§3.2 at this release).
  "A well-formed PATH is a text value that begins with exactly one '$' character followed by zero or more instances of
  '.objectlabel' or '[arrayindex]'." A path may select **no element** of the
  document — because the document has no member with that label, or because
  the array index is past the end — and that is not an error; what each
  function returns in that case is stated in that function's own section
  (`NULL` for `json_extract()`, `json_type()` and `json_array_length()`, and a
  silently ignored path for `json_remove()`).
* ***VALUE arguments*** (§3.3 at this release).
  A value argument with SQL type TEXT becomes a
  quoted JSON string "even if the input value strings look like well-formed
  JSON"; but "if a value argument come directly from the result of another json1
  function, then the argument is understood to be actual JSON and the complete
  JSON is inserted rather than a quoted string". This is what lets `json()`,
  `json_array()` and `json_object()` nest.

### 3.3 Rejections the verifier scores

Each is stated on `json1.html`, and each is observed the way every other
rejection in this task is — the statement fails and the observable database
state is unchanged, never by the message text (`../TEST_INTERFACE.md` §2.3,
§6). All are scored at both boundaries.

* text that is not well-formed JSON in the leading JSON argument of `json`,
  `json_type`, `json_extract`, `json_array_length`, `json_remove`,
  `json_insert`, `json_replace` or `json_set` (*JSON arguments*, §3.2);
* a PATH argument that is not well-formed — one that does not begin with `$`,
  and the empty string (*PATH arguments*, §3.2);
* a **BLOB** argument to `json_array()` ("If any argument to json_array() is a
  BLOB then an error is thrown") or to `json_object()` (the same sentence in
  its section), and a **BLOB value argument** to `json_insert()`,
  `json_replace()` or `json_set()` (their section: "throw an error … if any
  argument is a BLOB"). §3.7 says what is *not* scored here;
* an **even** number of arguments to `json_insert()`, `json_replace()` or
  `json_set()`, which "always take an odd number of arguments", and an **odd**
  number to `json_object()`, which "accepts zero or more *pairs* of arguments".

### 3.4 Indexes on expressions

`expridx.html`, new at 3.9.0, is normative for this section, together with the
`CREATE INDEX` text of `lang_createindex.html` and the `WHERE`-clause rules of
`partialindex.html`.

In scope:

* `CREATE INDEX … ON table(expr)` — "an index can also be formed on expressions
  involving table columns" — with one expression, several, or expressions mixed
  with plain column names in one index key;
* an index expression built from the operators, literals, `CASE`, `CAST` and
  the deterministic scalar functions of step 1 §§3-4, and from the JSON1
  functions of §3.1;
* `ASC`/`DESC` and `COLLATE` after an indexed expression, which
  `lang_createindex.html` allows after "each column name or expression";
* `UNIQUE` on such an index, and `IF NOT EXISTS`;
* the partial-index `WHERE` clause of step 1 §1 on such an index;
* `DROP INDEX` (with and without `IF EXISTS`) on such an index;
* the index appearing in `sqlite_master` like any other, with the text of the
  `CREATE INDEX` statement in its `sql` column, under the normalisation rules
  step 1 §6 already fixes.

Two consequences, spelled out for the same reason as in §3.1:

* **An index on an expression changes no answer.** `expridx.html` says of its
  worked example that "Both of the above example queries would work fine
  without the acctchng_magnitude index. The acctchng_magnitude index index
  merely helps the queries to run faster". So every query in this profile
  returns the same rows whether such an index exists or not, and that is the
  only thing about it the verifier observes: **which** index the planner picks,
  and whether it picks one at all, is never scored
  (`../TEST_INTERFACE.md` §6). The page's own remark that the planner "does not
  do algebra" and matches only expressions written exactly as indexed is
  therefore about performance here, not about results.
* **The index is maintained like any other.** The indexed value is recomputed
  from the row, so `INSERT`, `UPDATE` and `DELETE` keep it in step, a `UNIQUE`
  index on an expression rejects a row whose expression duplicates an existing
  one under the conflict algorithms of step 1 §1, `PRAGMA integrity_check`
  reports `ok`, a rolled-back transaction or savepoint leaves it as it was
  (step 1 §7), and `VACUUM` preserves it (step 1 §1).

Rejections the verifier scores, the first four from `expridx.html`'s
*Restrictions* list and repeated on `lang_createindex.html`, the fifth from
`partialindex.html`:

* an index expression that calls a **non-deterministic** function.
  `expridx.html` permits function calls in an index key, "but only to functions
  whose output is always determined completely by its input parameters" — the
  page calls those *deterministic* functions — so a function that does *not*
  meet that condition is rejected there; `lang_createindex.html` states the same
  restriction the other way round, as "functions whose result might change".
  A deterministic function in an index key is in scope and required — see the
  second in-scope bullet above, and the gate, whose index is over
  `json_extract()`. The two functions the pages name as failing the condition
  are `random()` and `sqlite_version()`, and those are the two the verifier
  writes. `random()` is out of scope as a *function* at every step (step 1 §8):
  the verifier writes it only inside a `CREATE INDEX` statement that must be
  rejected, and that rejection is required whether or not your implementation
  provides `random()` at all;
* an index expression that uses a **subquery**;
* an index expression that refers to a column of **another table**;
* an expression inside a `UNIQUE` or `PRIMARY KEY` **constraint of a
  `CREATE TABLE` statement**: "Expressions may only be used in CREATE INDEX
  statements, not within UNIQUE or PRIMARY KEY constraints within the CREATE
  TABLE statement";
* a function call, a subquery, another table's column or a bound parameter in
  the **`WHERE` clause of a partial index** — `partialindex.html` as shipped at
  SQLite 3.9.0: "The WHERE clause may not contains subqueries, references to
  other tables, functions, or bound parameters", and since `LIKE`, `GLOB`,
  `MATCH` and `REGEXP` "are implemented as functions by the same name … so too
  are" they prohibited there. This restriction is on the `WHERE` clause only; a
  function call in the index **key** of the same statement is exactly what §3.4
  permits. A later SQLite release loosens this sentence — if you are reading
  this section at a later step, the `partialindex.html` shipped beside you is
  the authority for the wording, and that step's own scope section for what is
  scored.

### 3.5 `PRAGMA index_list` at this release

Step 1 §5 fixed the result shape of `PRAGMA index_list(T)` at three columns,
because `pragma.html` as it stood at step 1 named only "the index name and a
flag to indicate whether or not the index is UNIQUE". At **SQLite 3.9.0 that
page enumerates five output columns**, so from this step onward the shipped
page is the authority and this row supersedes step 1 §5's:

| Pragma | Result columns, in this order | Rows |
| --- | --- | --- |
| `PRAGMA index_list(T)` | `seq`, `name`, `unique`, `origin`, `partial` | one per index on `T`, including the automatic ones. `seq` keeps the meaning step 1 §5 gives it — the row's own 0-based position in this result set, so a table with a single index reports `seq` 0; `unique` is `1` for a `UNIQUE` index and `0` otherwise, and an index SQLite creates itself to enforce a `UNIQUE` or `PRIMARY KEY` constraint is a unique index, so it reports `1` too; `origin` is `c` for an index made by `CREATE INDEX`, `u` for one made by a `UNIQUE` constraint and `pk` for one made by a `PRIMARY KEY` constraint (step 1 §6 and `fileformat2.html` name those automatic indexes `sqlite_autoindex_TABLE_N`); `partial` is `1` for a partial index and `0` otherwise. **Row order is not compared** — the verifier reads this pragma only on tables carrying a single index |

Nothing else in step 1 §5 moves: `table_info`, `index_info`, `integrity_check`,
`journal_mode`, `user_version`, `encoding`, `page_size` and `wal_checkpoint`
keep the shapes step 1 gives them.

### 3.6 What step 3 does *not* change

The JSON1 functions are eleven more scalar functions and an index on an
expression is a new `CREATE INDEX` form. Every statement, value-model rule,
expression construct, catalog fact, pragma, durability guarantee and C entry
point of steps 1 and 2 keeps exactly the meaning those steps gave it, and the
verifier re-exercises a sample of each: DDL and DML, the value model,
transactions and durability, the command-line contract, the C library, and step
2's `WITH` clause, `VALUES` queries and `printf()`.

"Sample" is meant literally: these checks re-run a handful of the earlier
steps' own cases rather than their whole suites. They are **gate** cases
rather than score groups: breaking an earlier milestone fails this step's gate
outright rather than costing a fraction of its score. One inherited rule stays
outside the sample as it did at step 2 — **`make clean`**
(`../TEST_INTERFACE.md` §1) is still a requirement of the workspace and step 1
scores it, but step 3 scores no group on it. Nothing else is relaxed: a rule the
sample happens not to reach is still binding, because step 4 starts from what
you leave here.

Two things a reader of the refreshed pages should not mistake for changes of
substance:

* `datatype3.html` renames the type affinity formerly called **NONE** to
  **BLOB** at this release, and says so ("Historical note: The 'BLOB' type
  affinity used to be called 'NONE'"). Only the name changed; step 1 §2's
  affinity rules are unaffected and nothing scored compares an affinity's name.
* `lang_corefunc.html` gains `likely(X)` and `lang_expr.html` documents
  hexadecimal integer literals. Neither is in step 1 §3 or §4's closed lists, so
  by the preamble's third rule both are out of scope.

The one plumbing value that moves is the release string —
`../TEST_INTERFACE.md` §3.3.

### 3.7 Out of scope at step 3

In addition to step 1 §8 and step 2 §2.5, which still apply:

* **`json_each()` and `json_tree()`.** `json1.html` documents these two
  table-valued functions; they are virtual tables, which step 1 §8 excludes at
  every step. The verifier never writes either name, and nothing is scored
  either way.
* **`json_quote()`, `json_group_array()` and `json_group_object()`.** These are
  *not* part of SQLite 3.9.0's JSON1: the release's `json1.html` enumerates
  eleven scalar functions and two table-valued ones and names none of the
  three. They are out of scope, and nothing is scored either way.
* **A `NULL` where a json1 function expects JSON.** `json1.html`'s *JSON arguments*
  section says a NULL value is "interpreted as" a JSON null, but no section says what any
  particular function then returns. The verifier never passes a NULL **in the
  leading JSON argument position** of any function, and never as a PATH; for
  such a call, returning NULL, returning `'null'`, or throwing are all
  unscored. A NULL in a **value** argument position is a different matter and
  *is* scored, for `json_array()` only: that function's own worked example
  `json_array(1,null,'3','[4,5]','{"six":7.7}')` →
  `'[1,null,"3","[4,5]","{\"six\":7.7}"]'` fixes the answer, so a NULL value
  argument to `json_array()` becomes the JSON literal `null` in the result, as
  §3.1 requires of every worked example. The verifier passes a NULL value
  argument nowhere else.
* **A BLOB in the leading (JSON) argument position.** `json1.html` §3 (*Interface Overview*) says
  "All json1 functions currently throw an error if any of their arguments are
  BLOBs", and several sections repeat it. §3.3 scores that only where a
  function's own section states it for the arguments the verifier passes — any
  argument of `json_array()` and `json_object()`, and the *value* arguments of
  the three mutators. A BLOB in the leading JSON argument is never passed and
  accepting or rejecting it is unscored.
* **The harder reading of "Edits occurs sequentially from left to right"**
  (`json_insert`/`json_replace`/`json_set`). §3.3's scored form is the one the
  page's own examples support: a later pair that addresses the same path as an
  earlier one, or that appends the next element of an array, sees the earlier
  edit. Whether a later pair can *descend into a container that an earlier pair
  created* is not scored in either direction.
* **What *VALUE arguments*' "come directly from another json1 function" rule
  means for a `json_extract()` result.** With a single path `json_extract()` returns an
  ordinary SQL value of the JSON element's own type — a dequoted string, an
  integer, a real, or NULL — rather than a JSON document, so it is not obvious
  whether *VALUE arguments* treats such a value as "actual JSON" or as a
  literal string when
  it is passed as a **value** argument to `json_array()`, `json_object()` or a
  mutator. The verifier never does that. The value arguments it writes are
  literals, ordinary columns, INTEGER results, or the result of `json()`,
  `json_array()` or `json_object()`, where the page's own examples settle the
  answer.
* **Duplicate object labels.** `json1.html` says of `json()` that "it is
  undefined whether or not the duplicates are preserved" and of `json_object()`
  that it "currently allows duplicate labels without complaint, though this
  might change". The verifier never builds or parses a document with a repeated
  label.
* **Path forms outside the *PATH arguments* grammar**, including the `$[#]`
  append form,
  wildcards, and a path given as anything other than a text value. A NULL path,
  and `json_extract()` called with no path argument at all, are likewise
  unscored.
* **`PRAGMA index_info` on an index whose key is an expression.**
  `pragma.html` defines its second and third output columns as the rank of the
  column *within the table* and the *name of the column* being indexed, and an
  expression is neither, so the page does not say what those columns hold. The
  verifier reads `index_info` only for indexes whose key columns are plain
  column names. `PRAGMA index_xinfo` is not in scope at any step.
* **Whether the query planner uses an index on an expression**, and every other
  plan question, exactly as `../TEST_INTERFACE.md` §6 says.
* **Loading JSON1 at run time.** `json1.html` §2 (*Compiling the JSON1 Extension*) describes json1 as a
  loadable extension that upstream's amalgamation leaves off by default; that
  is a statement about upstream's build, not about this workspace.
  `load_extension()` is out of scope at every step (step 1 §8), and
  `../TEST_INTERFACE.md` §7 says where the functions have to be available
  instead.
* **`rescode.html`'s extended result codes.** The page joined this
  documentation set at step 3; `../TEST_INTERFACE.md` §3.3 remains the closed
  list of result codes the verifier compares.
## Step 4 — UPSERT (SQLite 3.24.0)

Everything in steps 1, 2 and 3's sections above stays in scope and the verifier
re-exercises a sample of it. Step 1's sections keep the numbers 1-8, step 2's
keep 2.1-2.5 and step 3's keep 3.1-3.7; this step's sections are numbered `4.1`
upward so a cross-reference is unambiguous.

Step 4 refreshed the whole documentation set: every page beside this file
became its SQLite 3.24.0 edition, and `lang_UPSERT.html` — **note the
capitalised file name** — was added. The `sqlite-doc/README.md` shipped with
whichever step you are reading this at says what is new and what changed at
*that* step.

Step 4 adds one capability to steps 1-3's profile:

* UPSERT — `INSERT … ON CONFLICT … DO NOTHING` and
  `INSERT … ON CONFLICT (target) DO UPDATE SET …` (§4.1-§4.5).

and carries one **inherited** rule that upstream reworded at this release, so
that the profile and the shipped page agree: what a partial index's `WHERE`
clause may contain (§4.6). That section **supersedes the fifth scored
rejection of step 3 §3.4** — read it even if you think you already finished
partial indexes.

### 4.1 The statement form

`lang_UPSERT.html`, new at 3.24.0, is normative for §4.1-§4.5, together with
the `INSERT` text of `lang_insert.html` and the conflict-algorithm text of
`lang_conflict.html`.

> UPSERT is a special syntax addition to INSERT that causes the INSERT to
> behave as an UPDATE or a no-op if the INSERT would violate a uniqueness
> constraint.

> An UPSERT is an ordinary INSERT statement that is followed by the special ON
> CONFLICT clause shown above.

So an upsert is one of step 1 §1's `INSERT` statements with a trailing clause;
everything step 1 §1 already fixes about `INSERT` — the optional column list,
`VALUES` with one or many row lists, `INSERT … SELECT`, the `INSERT OR …`
prefixes — is unchanged and still in scope.

The syntax diagrams on `lang_UPSERT.html` and `lang_insert.html` are images and
the image files are not part of this documentation copy, so the trailing
clause's grammar is written out here:

```text
ON CONFLICT [ ( indexed-column [, indexed-column]... ) [ WHERE expr ] ]
   DO NOTHING

ON CONFLICT   ( indexed-column [, indexed-column]... ) [ WHERE expr ]
   DO UPDATE SET column-name = expr [, column-name = expr]... [ WHERE expr ]
```

At most one such clause appears on a statement, after everything else the
`INSERT` contains. An `indexed-column` is a column name or an expression,
exactly as in the `CREATE INDEX` key that `lang_createindex.html` calls "a
parenthesized list of table column names and/or expressions". The verifier
writes plain column names and expressions there and nothing else.

`lang_conflict.html` warns that the same two words mean two different things —
the `ON CONFLICT` clause of a `CREATE TABLE` constraint and the `OR` prefix of
an `INSERT` are step 1 §1's inherited conflict algorithms:

> The phrase "ON CONFLICT" is also part of UPSERT, which is an extension to
> INSERT added in version 3.24.0 (2018-06-04). Do not confuse these two
> separate uses of the "ON CONFLICT" phrase.

§4.5 says how the two compose.

Two further rules from `lang_insert.html` are in scope:

* **`AS alias`.**

  > The optional "AS alias" phrase provides an alternative name for the table
  > into which content is being inserted. The alias name can be used within
  > WHERE and SET clauses of the UPSERT.

  Where the phrase sits is fixed by the `INSERT` header's own syntax diagram,
  which is an image and is not part of this documentation copy either, so that
  much of the header is written out here as well:

  ```text
  INSERT INTO table-name [ AS alias ] [ ( column-name [, column-name]... ) ]
  ```

  The alias comes immediately after the table name and before the optional
  column list; everything else about the statement is unchanged. The verifier
  writes an alias both with and without a column list after it, and wherever a
  statement carries an alias it qualifies that table's columns with the alias
  and never with the table's own name.

* **`INSERT … SELECT` needs a `WHERE` clause.**

  > To avoid a parsing ambiguity, the SELECT statement should always contain a
  > WHERE clause, even if that clause is simply "WHERE true", if the
  > upsert-clause is present. Without the WHERE clause, the parser does not
  > know if the token "ON" is part of a join constraint on the SELECT, or the
  > beginning of the upsert-clause.

  The form the verifier writes is therefore
  `INSERT INTO … SELECT … WHERE <expr> ON CONFLICT …`, and it must work. It
  writes `WHERE 1` rather than the page's `WHERE true`, because `TRUE` and
  `FALSE` are not literal values in step 1 §3 and this release's
  `lang_expr.html` and `lang_keywords.html` do not name them. §4.9 says that
  what happens to the *ambiguous* form is not scored either way.

### 4.2 The conflict target

> The syntax that occurs in between the "ON CONFLICT" and "DO" keywords is
> called the "conflict target". The conflict target specifies a specific
> uniqueness constraint that will trigger the upsert. The conflict target is
> required for DO UPDATE upserts, but is optional for DO NOTHING.

In scope, read against the uniqueness constraints step 1 §1 and step 3 §3.4
already put in scope:

* a target naming the column of an `INTEGER PRIMARY KEY` or of any other
  `PRIMARY KEY`;
* a target naming the column of a `UNIQUE` column constraint, of a `UNIQUE`
  table constraint, or of a `CREATE UNIQUE INDEX`;
* a target naming **several** columns, for a multi-column `UNIQUE` constraint
  or index. The target identifies a constraint, not an ordering, so the columns
  may be written in either order;
* a target naming an **expression**, for a `UNIQUE` index whose key is that
  expression (step 3 §3.4), including one built from the JSON1 functions of
  step 3 §3.1. The expression is written the way it is written in the
  `CREATE INDEX`;
* a target followed by **its own `WHERE` clause**, which is how a target
  identifies a **partial** unique index (step 1 §1; `partialindex.html`
  §2.1, *Unique Partial Indexes*).
  The `WHERE` expression is written the way it is written in the
  `CREATE INDEX`.

Because the target "specifies a specific uniqueness constraint", two things
follow, and the verifier scores both:

* **A conflict target that identifies no uniqueness constraint of the table is
  rejected** (§4.7). Naming a column that carries no `UNIQUE` or `PRIMARY KEY`
  constraint and no unique index is one such statement; so is naming the
  indexed column of a **partial** unique index *without* the `WHERE` clause
  that would identify it, because the constraint that exists applies only to
  the rows that partial index covers.
* **A uniqueness constraint the target does not identify is not handled by the
  upsert.** When the row being inserted violates some *other* constraint, the
  upsert clause does not apply to it and the statement's own conflict algorithm
  decides what happens — §4.5.

### 4.3 `DO NOTHING`

> If the insert operation would cause the uniqueness constraint identified by
> the conflict-target clause to fail, then the insert is omitted and either the
> DO NOTHING or DO UPDATE operation is performed instead. In the case of a
> multi-row insert, this decision is made separately for each row of the
> insert.

So a `DO NOTHING` upsert inserts the rows that fit and silently skips the rows
that would violate the identified constraint, one row at a time; the statement
succeeds, and every other row of the same `INSERT` is inserted normally.

"Separately for each row" also fixes the *order*, and it does so for
`DO UPDATE` (§4.4) exactly as for `DO NOTHING`: the rows of a multi-row
`INSERT` are taken in the order they are written, and each row's decision is
made against the table as the rows before it have already left it. So when two
rows of one statement carry the same key, the first is inserted and the second
finds it there — under `DO UPDATE` the second updates what the first inserted,
and under `DO NOTHING` the second is skipped. The verifier compares the table
after such a statement.

Without a conflict target the rule is stated as an equivalence — "A DO NOTHING
upsert without a conflict target works the same as an INSERT OR IGNORE." The
verifier scores that equivalence directly: the same multi-row insert written
both ways leaves the same rows in the table. So a target-less `DO NOTHING`
skips a row that violates **any** uniqueness constraint, not only one.

### 4.4 `DO UPDATE`, `excluded.` and the trailing `WHERE`

> Column names in the expressions of a DO UPDATE refer to the original
> unchanged value of the column, before the attempted INSERT. To use the value
> that would have been inserted had the constraint not failed, add the special
> "excluded." table qualifier to the column name.

In scope:

* the `SET` list assigns one or more of the table's columns from any expression
  step 1 §3 puts in scope, including the columns of the conflicting row and of
  `excluded`. The three worked examples on `lang_UPSERT.html` — `vocabulary`,
  `phonebook` and `phonebook2` — are normative and the verifier compares the
  results they describe;
* an unqualified column name in `SET` or in the trailing `WHERE` is the
  conflicting row's **original** value, and the same name qualified with the
  table name, or with the `AS alias` of §4.1, means the same thing. The page
  says so of its own example:

  > The "count+1" expression could also be written as "vocabulary.count".
  > PostgreSQL requires the second form, but SQLite accepts either.

* `excluded.<column>` is the value that row would have been inserted with. For
  a column the `INSERT` did not name, that is the value `lang_insert.html`
  gives it — "Table columns that do not appear in the column list are populated
  with the default column value (specified as part of the CREATE TABLE
  statement), or with NULL if no default value is specified";
* the `DO UPDATE` "acts only on the single row that experienced the constraint
  error during INSERT", so every other row of the table is left exactly as it
  was. `SET` may assign the conflict-target column itself; the result is an
  ordinary update of that one row;
* the trailing `WHERE` clause. "The only use for the WHERE clause at the end of
  the DO UPDATE is to optionally change the DO UPDATE into a no-op depending on
  the original and/or new values": when it is true the update happens, and when
  it is not, the upsert is a no-op for that row — the row is neither inserted
  nor updated, and the statement still succeeds. `phonebook2` is the worked
  case.

An upsert whose `DO UPDATE` would itself violate a constraint is a statement
that violates a constraint, and step 1 §7 already fixes what that means: with
no `OR` prefix the default `ABORT` algorithm of `lang_conflict.html` applies,
the statement is rejected, and the database is left exactly as it was.

### 4.5 UPSERT and the inherited `INSERT OR …` algorithms

An upsert may be written on any of step 1 §1's `INSERT OR …` forms. The two
mechanisms divide the work along the line §4.2 draws, and the division is what
the verifier scores:

* a violation of **the uniqueness constraint the conflict target identifies**
  is handled by the upsert clause — "the insert is omitted and either the DO
  NOTHING or DO UPDATE operation is performed instead" — whatever `OR`
  algorithm the statement carries;
* a violation of **anything else** — another uniqueness constraint, a
  `NOT NULL` constraint, a `CHECK` constraint — is handled by the statement's
  conflict algorithm exactly as it is on an `INSERT` with no upsert clause.
  `lang_conflict.html` fixes that: "The algorithm specified in the OR clause of
  an INSERT or UPDATE overrides any algorithm specified in a CREATE TABLE. If
  no algorithm is specified anywhere, the ABORT algorithm is used."

So `INSERT OR IGNORE … ON CONFLICT(k) DO UPDATE …` updates a row that collides
on `k` and silently skips one that collides on something else, while the same
statement written without `OR IGNORE` updates the first and is rejected by the
second, leaving the database as it was.

### 4.6 What a partial index's `WHERE` clause may contain at this release

**This section supersedes the fifth scored rejection of step 3 §3.4, and the
page-derived rule behind it, from step 4 onward.** Upstream reworded
`partialindex.html` §2
(*Creating Partial Indexes*) between SQLite 3.9.0 and SQLite 3.24.0: the
sentence that prohibited *functions* there now prohibits only
*non-deterministic* functions, and the paragraph that extended the prohibition
to the `LIKE`, `GLOB`, `MATCH` and `REGEXP` operators was deleted. Steps 1-3
are unaffected — each is scored against the page shipped beside it — but from
step 4 onward the rule is the one the `partialindex.html` beside this file
states, and it is this:

> The expression following the WHERE clause may contain operators, literal
> values, and names of columns in the table being indexed.

> The WHERE clause may not contain subqueries, references to other tables,
> non-deterministic functions, or bound parameters.

In scope, and scored in both directions:

* a **deterministic** function call in a partial index's `WHERE` clause is
  accepted, and the index it creates behaves like any other partial index. So
  are the `LIKE` and `GLOB` operators: step 1 §4 already records that `like()`
  and `glob()` are their function spellings, and the deleted paragraph is the
  only place a shipped page ever kept them out of a partial index's `WHERE`
  clause;
* a **non-deterministic** function call there is rejected. As at step 3, the
  two functions the shipped pages name as failing that condition are `random()`
  and `sqlite_version()`; `random()` stays out of scope as a *function* (step 1
  §8) and the verifier writes it only inside a `CREATE INDEX` that must be
  rejected, so that rejection is required whether or not your implementation
  provides `random()` at all;
* a **subquery**, a reference to **another table's column**, and a **bound
  parameter** are rejected there, as they were at step 3.

Nothing else about partial indexes moves: step 1 §1's `CREATE INDEX … WHERE`
form, step 3 §3.4's expression index keys, and step 3 §3.5's `PRAGMA
index_list` shape all keep their meaning.

### 4.7 Rejections the verifier scores

Each is observed the way every other rejection in this task is — the statement
fails and the observable database state is unchanged — never by the message
text (`../TEST_INTERFACE.md` §2.3, §6):

* `DO UPDATE` **without a conflict target**: "The conflict target is required
  for DO UPDATE upserts" (§4.2);
* an upsert clause on **`INSERT … DEFAULT VALUES`**: "The upsert-clause is not
  supported after DEFAULT VALUES", and again, in the last paragraph of
  `lang_insert.html`, "The upsert clause is not allowed on an "INSERT ...
  DEFAULT VALUES"";
* a **conflict target that identifies no uniqueness constraint** of the table,
  including the indexed column of a partial unique index written without the
  `WHERE` clause that identifies it (§4.2);
* an upsert whose **`DO UPDATE` would itself violate a constraint**, under the
  default `ABORT` algorithm (§4.4);
* a row that violates a uniqueness, `NOT NULL` or `CHECK` constraint the
  conflict target does **not** identify, when the statement carries no `OR`
  prefix (§4.5);
* the four things a partial index's `WHERE` clause may not contain at this
  release (§4.6).

### 4.8 What step 4 does *not* change

UPSERT is a trailing clause on `INSERT`. Every statement, value-model rule,
expression construct, function, catalog fact, pragma, durability guarantee and
C entry point of steps 1, 2 and 3 keeps exactly the meaning those steps gave
it, with the single exception §4.6 states, and the verifier re-exercises a
sample of each: DDL and DML, the value model, transactions and durability, the
command-line contract, the C library, step 2's `WITH` clause, `VALUES` queries
and `printf()`, and step 3's JSON1 functions and indexes on expressions.

"Sample" is meant literally: those checks re-run a handful of the earlier
steps' own cases rather than their whole suites. They are **gate** cases
rather than score groups: breaking an earlier milestone fails this step's gate
outright rather than costing a fraction of its score. One inherited rule stays
outside the sample as it did at steps 2 and 3 — **`make clean`**
(`../TEST_INTERFACE.md` §1) is still a requirement of the workspace and step 1
scores it, but step 4 scores no group on it. Nothing else is relaxed: a rule
the sample happens not to reach is still binding, because step 5 starts from
what you leave here.

An upsert is an ordinary write, so the rest of the profile applies to it
unchanged: it is undone by `ROLLBACK` and by `ROLLBACK TO` a savepoint,
survives `COMMIT`, a reopen and a second process, keeps every index on the
table — including a `UNIQUE` index, a partial index and an index on an
expression — consistent, and leaves `PRAGMA integrity_check` reporting `ok`
(step 1 §1, §7; step 3 §3.4).

Two things a reader of the refreshed pages should not mistake for changes of
substance:

* `lang_update.html` documents a parenthesised column list on the left of a
  `SET` assignment, and `lang_vacuum.html` a schema name after `VACUUM`. Both
  are additions upstream made between 3.9.0 and 3.24.0; neither is in step 1
  §1's list, so by the preamble's third rule both are out of scope.
* `lang_corefunc.html` and `lang_aggfunc.html` list functions that step 1 §4's
  closed lists do not, and this release's `json1.html` opens by saying it
  "implements fifteen application-defined SQL functions" where the edition step
  3 read said eleven. The four it adds are `json_patch()`, `json_quote()`,
  `json_group_array()` and `json_group_object()`; step 3 §3.7 already excludes
  the last three, having found them absent at SQLite 3.9.0, and all four are
  out of scope here for the ordinary reason — step 3 §3.1's list is closed and
  no section of this file reopens it. Everything outside those closed lists is
  out of scope, and nothing is scored either way. `json1.html` also renumbered
  its sections between the two releases, which is why step 3 §3.1-§3.3 cite it
  by section title.

The one plumbing value that moves is the release string —
`../TEST_INTERFACE.md` §3.3.

### 4.9 Out of scope at step 4

In addition to step 1 §8, step 2 §2.5 and step 3 §3.7, which still apply:

* **`changes()`, `total_changes()` and `last_insert_rowid()` after a statement
  that carried an upsert clause.** No shipped page settles them.
  `c3ref/changes.html` and `c3ref/last_insert_rowid.html` are the 3.24.0
  editions and neither mentions UPSERT, while `pragma.html` says of the
  deprecated `count_changes` pragma that its count "does not include any
  insertions, modifications or deletions performed by triggers, any changes
  made automatically by foreign key actions, or updates caused by an upsert".
  The verifier never reads any of the three after an upsert, in SQL or through
  the C library, and returning any value is unscored. All three keep their step
  1 §4 meaning after every other statement.
* **`INSERT … SELECT … ON CONFLICT …` with no `WHERE` clause on the `SELECT`.**
  `lang_insert.html` says the `SELECT` "should always contain a WHERE clause"
  and explains that without one "the parser does not know" which of two things
  the token `ON` begins. That is a statement about an ambiguous grammar rather
  than a rule that the statement is rejected, so the verifier never writes the
  ambiguous form and accepting or rejecting it is unscored in both directions.
  The `WHERE`-bearing form of §4.1 *is* required.
* **The boolean keyword literals `TRUE` and `FALSE`.** `lang_insert.html`
  writes `WHERE true` inside the sentence quoted in §4.1, but this release's
  `lang_expr.html` does not list them among the literal values and
  `lang_keywords.html` does not list them among the keywords, so they are
  outside step 1 §3. The verifier writes `WHERE 1`.
* **A conflict target whose `WHERE` clause does not correspond to a partial
  index.** §4.2 requires the `WHERE` clause to be accepted and to identify the
  partial index when one exists. Whether a `WHERE` clause written on a target
  that resolves to an ordinary, non-partial index is accepted or rejected is
  stated on no shipped page, and the verifier never writes one.
* **An upsert clause written on the `REPLACE INTO …` spelling.**
  `lang_insert.html` calls `REPLACE` an alias for `INSERT OR REPLACE`, so
  §4.5's rule covers it in principle, but no shipped page writes the
  combination and the verifier never writes it either — the
  `INSERT OR REPLACE … ON CONFLICT …` spelling is the one that is scored, and
  it is scored in full.
* **More than one `ON CONFLICT` clause on a single `INSERT`**, and an upsert
  clause anywhere other than at the end of the statement. The shipped prose
  describes one trailing clause and nothing else; the verifier writes nothing
  else.
* **`ON CONFLICT(rowid)`** and the other undeclared rowid spellings as a
  conflict target. `lang_UPSERT.html` speaks only of uniqueness constraints and
  whether the implicit rowid counts as one is not stated, so the verifier names
  the declared `INTEGER PRIMARY KEY` column instead.
* **UPSERT on a virtual table or a `WITHOUT ROWID` table.** Both kinds of table
  are already out of scope at every step by step 1 §8.
* **Which uniqueness constraint an implementation reports** when a row could
  violate more than one, and in what order it checks them. Every scored case
  either arranges for exactly one constraint to be violated or observes only
  the resulting table contents.
## Step 5 — window functions and `RENAME COLUMN` (SQLite 3.25.0)

Everything in steps 1, 2, 3 and 4's sections above stays in scope and the
verifier re-exercises a sample of it. Step 1's sections keep the numbers 1-8,
step 2's keep 2.1-2.5, step 3's keep 3.1-3.7 and step 4's keep 4.1-4.9; this
step's sections are numbered `5.1` upward so a cross-reference is unambiguous.

This is the **last** step of the benchmark. Every rule below is the rule in
force; nothing supersedes it later, and §5.13's exclusions are final.

Step 5 refreshed the whole documentation set: every page beside this file
became its SQLite 3.25.0 edition, and `windowfunctions.html` was added. The
`sqlite-doc/README.md` shipped beside this file says what is new and what
changed.

Step 5 adds two capabilities to steps 1-4's profile:

* **window functions** — the `OVER` clause, `PARTITION BY`, the window
  `ORDER BY`, frame specifications, the eleven built-in window functions,
  ordinary aggregate functions used as window functions, the `FILTER` clause
  and the `WINDOW` clause (§5.1-§5.8);
* **`ALTER TABLE … RENAME COLUMN`**, including what it leaves in
  `sqlite_master.sql` (§5.9-§5.10).

It supersedes nothing. Step 3 §3.5's `PRAGMA index_list` shape and step 4
§4.6's partial-index `WHERE` rule are both still in force, and both are still
correct against the pages shipped beside this file: `partialindex.html` and
`pragma.html`'s `index_list` entry are unchanged in substance between SQLite
3.24.0 and SQLite 3.25.0. If you are reading this file at step 5, read §3.5 and
§4.6 as written; they are not restated here.

### 5.1 What a window function is, and where one may appear

`windowfunctions.html`, new at 3.25.0, is normative for §5.1-§5.8, together
with `lang_select.html`'s `SELECT` text and `lang_aggfunc.html`'s aggregate
functions, both of which step 1 already put in scope.

> A window function is a special SQL function where the input values are taken
> from a "window" of one or more rows in the results set of a SELECT statement.

> Window functions are distinguished from ordinary SQL functions by the
> presence of an OVER clause. If a function invocation has an OVER clause then
> it is a window function, and if lacks a OVER clause it is an ordinary
> function.

The `window-function-invocation`, `window-defn` and `frame-spec` syntax
diagrams on that page are images, and the image files are not part of this
documentation copy, so the grammar is written out here:

```text
window-function-invocation:
    function-name ( [ expr [, expr]... | * ] )
        [ FILTER ( WHERE expr ) ]
        OVER ( window-defn ) | OVER window-name

window-defn:
    [ PARTITION BY expr [, expr]... ]
    [ ORDER BY ordering-term [, ordering-term]... ]
    [ frame-spec ]

frame-spec:
      { RANGE | ROWS } BETWEEN frame-bound AND frame-bound
    | { RANGE | ROWS } frame-bound            -- start only; see §5.3

frame-bound:
      UNBOUNDED PRECEDING | expr PRECEDING | CURRENT ROW
    | expr FOLLOWING      | UNBOUNDED FOLLOWING
```

An `ordering-term` inside a `window-defn` is the `ordering-term` step 1 §1
already puts in scope for `ORDER BY`, so `ASC`, `DESC` and `COLLATE` may be
written there. A `window-name` is a name introduced by the `WINDOW` clause of
§5.6. Every `window-defn` part is optional, so `OVER ()` is a legal window
definition and means one partition containing every row, with no ordering.

**Where a window function may appear** is a closed rule the verifier scores in
both directions:

> Unlike ordinary functions, window functions cannot use the DISTINCT keyword.
> Also, Window functions may only appear in the result set and in the ORDER BY
> clause of a SELECT statement.

So a window function is in scope in a `SELECT`'s result columns and in the
`ORDER BY` attached to that `SELECT`, and nowhere else: not in a `WHERE`
clause, not in a `GROUP BY` clause, not in a `HAVING` clause, not in the `SET`
list or `WHERE` clause of an `UPDATE` — including the `DO UPDATE` half of step
4's upsert — and not in the `WHERE` clause of a `DELETE`. §5.11 lists the
rejections.

The `SELECT` a window function appears in may be any of the `SELECT`s step 1 §1
and step 2 §2.1-§2.2 put in scope: a top-level query, the body of a common
table expression, a `FROM` subquery, one arm of a compound select, and the
`SELECT` that feeds an `INSERT … SELECT` — including one carrying step 4's
upsert clause.

### 5.2 `PARTITION BY`, the window `ORDER BY`, and the order of the output

> A window-defn may include a PARTITION BY clause. If so, the rows returned by
> the SELECT statement are divided into groups - partitions - with the same
> values for each PARTITION BY expression, and then window-function processing
> is performed separately for each partition. This is similar to the way the
> rows are grouped by the GROUP BY clause of an aggregate query.

With no `PARTITION BY` clause every row of the result set is in one partition.

The window `ORDER BY` fixes the order **within** a partition, and the page is
explicit that it does not fix the order of the query's output:

> Note that this does not affect the order in which results are returned from
> the overall query. The order of the final output is still governed by the
> ORDER BY clause attached to the SELECT statement.

`../TEST_INTERFACE.md` §5's determinism rule therefore applies to a windowed
query exactly as to any other: **every scored query that can return more than
one row carries a total `ORDER BY` on the outer `SELECT`**, and a window
`ORDER BY` is never mistaken for one. The page's own first example — a window
ordered by `y` and an output ordered by `x`, returning `1|aaa|1`, `2|ccc|3`,
`3|bbb|2` — is normative and the verifier compares it.

The `ORDER BY` inside a `window-defn` also decides which rows are **peers**:

> If the frame type is RANGE, then rows with the same values for all ORDER BY
> expressions are considered "peers". Or, if there are no ORDER BY terms, all
> rows are peers. Rows that are peers always have the same window frames.

and, for the built-in functions of §5.4:

> Some of the window functions (rank(), dense_rank(), percent_rank() and
> ntile()) use the concept of "peer groups" (rows within the same partition
> that have the same values for all ORDER BY expressions). In these cases, it
> does not matter whether the frame-spec specifies ROWS or RANGE - for the
> purposes of built-in window function processing, rows with the same values
> for all ORDER BY expressions are considered peers regardless of the frame
> type.

§5.8 says what the verifier does about the values the documents leave to the
order *within* a peer group.

### 5.3 Frame specifications

> The frame-spec determines which output rows are read by an aggregate window
> function.

> The frame-spec consists of three parts:
> A frame type - either RANGE or ROWS.
> A starting frame boundary, and
> An ending frame boundary.

Both frame types are in scope. `GROUPS` is **not** — see §5.13.

> The ending frame boundary can be omitted, in which case it defaults to
> CURRENT ROW.

> The default frame-spec is:
> RANGE BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW

> The default means that aggregate window functions read all rows from the
> beginning of the partition up to and including the current row and its peers.

So a `window-defn` that carries no `frame-spec` at all has that frame, and the
verifier scores the equivalence directly: the same query written with and
without `RANGE BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW` returns the same
rows.

The five frame boundaries and the constraint between them are the page's:

| Frame boundary | In scope as |
| --- | --- |
| `UNBOUNDED PRECEDING` | "The start of the frame is the first row in the set" |
| `<expr> PRECEDING` | "the start or end of the frame is `<expr>` rows before the current row"; `<expr>` "is a constant expression that evaluates to a non-negative integer value"; `0 PRECEDING` "is the same as CURRENT ROW"; **ROWS frames only** |
| `CURRENT ROW` | "The current row. For RANGE frame types, all peers of the current row are also included in the window frame, regardless of whether CURRENT ROW is used as the starting or ending frame boundary" |
| `<expr> FOLLOWING` | as `<expr> PRECEDING`, `<expr>` rows after the current row; `0 FOLLOWING` "is the same as CURRENT ROW"; **ROWS frames only** |
| `UNBOUNDED FOLLOWING` | "The end of the frame is the last row in the set" |

> The ending frame boundary must not take a form that appears higher in the
> above list than the starting frame boundary.

The verifier writes every one of those five boundaries, in both bound
positions where the rules above allow it, and it scores the three rejections
§5.11 lists — those three and no others. Two spellings the rule above neither
allows nor forbids — a frame whose two boundaries are **both**
`UNBOUNDED PRECEDING`, and one whose two boundaries are **both**
`UNBOUNDED FOLLOWING` — are unscored in both directions; §5.13 says why.

The three worked frame examples on the page — `ROWS BETWEEN 1 PRECEDING AND 1
FOLLOWING` over `t1`, `ROWS BETWEEN CURRENT ROW AND UNBOUNDED FOLLOWING` over
`t1`, and `PARTITION BY c ORDER BY a RANGE BETWEEN CURRENT ROW AND UNBOUNDED
FOLLOWING` over `t1` — are normative and the verifier compares the results they
describe, with a total `ORDER BY` added to the outer `SELECT` where the page
omitted one. So is the `ORDER BY c` example that demonstrates the default
frame and peer behaviour.

### 5.4 The eleven built-in window functions

> SQLite supports the following 11 built-in window functions

All eleven are in scope, with the semantics that page gives them:

| Function | In scope as |
| --- | --- |
| `row_number()` | "The number of the row within the current partition. Rows are numbered starting from 1 in the order defined by the ORDER BY clause in the window definition" |
| `rank()` | "The row_number() of the first peer in each group - the rank of the current row with gaps. If there is no ORDER BY clause, then all rows are considered peers and this function always returns 1" |
| `dense_rank()` | "The number of the current row's peer group within its partition - the rank of the current row without gaps" |
| `percent_rank()` | "always returns a value between 0.0 and 1.0 equal to ( rank - 1)/( partition-rows - 1)"; "If the partition contains only one row, this function returns 0.0" |
| `cume_dist()` | "Calculated as row-number / partition-rows , where row-number is the value returned by row_number() for the last peer in the group and partition-rows the number of rows in the partition" |
| `ntile(N)` | "divides the partition into N groups as evenly as possible and assigns an integer between 1 and N to each group, in the order defined by the ORDER BY clause"; "If necessary, larger groups occur first" |
| `lag(expr)`, `lag(expr, offset)`, `lag(expr, offset, default)` | the value of `expr` on the previous row, or the row `offset` rows before, in the partition; NULL when there is no such row, or `default` when the third argument is given |
| `lead(expr)`, `lead(expr, offset)`, `lead(expr, offset, default)` | the same, forwards |
| `first_value(expr)` | "the value of expr evaluated against the first row in the window frame" |
| `last_value(expr)` | "the value of expr evaluated against the last row in the window frame" |
| `nth_value(expr, N)` | "the value of expr evaluated against the row N of the window frame"; NULL when the frame has fewer than `N` rows, which is what the page's own worked example shows |

Three page-level rules about them are in scope too:

> Built-in window functions honor any PARTITION BY clause in the same way as
> aggregate window functions - each selected row is assigned to a partition and
> each partition is processed separately.

> Most built-in window functions ignore the frame-spec , the exceptions being
> first_value(), last_value() and nth_value().

> It is a syntax error to specify a FILTER clause as part of a built-in window
> function invocation.

`percent_rank()` and `cume_dist()` return REAL values, so
`../TEST_INTERFACE.md` §2.3 and §5 govern how their text is compared: the
verifier only ever writes partitions whose results are exactly representable
or have an unambiguous shortest form — `0.0`, `0.25`, `0.5`, `0.75`, `1.0` for
a five-row partition, `0.2`/`0.4`/`0.6`/`0.8`/`1.0` for its `cume_dist()`.

Two columns of the page's own six-row ranking example are **not** compared, and
the other four are. Its `cume_dist` column is **rounded for display** — it
prints `0.66` where the value is `4/6` — so it is not an expected output at
all; and its `row_number` column depends on the order of rows *inside* a peer
group, which §5.8 says nothing fixes. Its `a`, `rank`, `dense_rank` and
`percent_rank` columns are compared.

`row_number()`, `rank()`, `dense_rank()` and `ntile()` return INTEGER values.

### 5.5 Aggregate functions used as window functions, and `FILTER`

> An aggregate window function is similar to an aggregate function, except
> adding it to a query does not change the number of rows returned. Instead,
> for each row the result of the aggregate window function is as if the
> corresponding aggregate were run over all rows in the "window frame".

> All of SQLite's aggregate functions may be used as aggregate window
> functions.

"All of SQLite's aggregate functions" is read against step 1 §4's closed list,
which is the set of aggregate functions this benchmark requires: `avg`,
`count(*)`, `count(X)`, `group_concat` in both arities, `max`, `min`, `sum` and
`total`. Every one of them is in scope with an `OVER` clause, over both frame
types, and the value it returns for a row is the value the same aggregate
returns over that row's frame. `DISTINCT` may not be written on any of them in
this position (§5.1).

The `FILTER` clause is in scope on an aggregate window function:

> If a FILTER clause is provided, then only rows for which the expr is true are
> included in the window frame. The aggregate window still returns a value for
> every row, but those for which the FILTER expression evaluates to other than
> true are not included in the window frame for any row.

The page's worked `FILTER` example is normative and the verifier compares it.
`FILTER` on a **built-in** window function is rejected (§5.11). `FILTER`
written without an `OVER` clause is out of scope (§5.13).

**`group_concat()` as a window function concatenates the frame's values in the
frame's own order.** Step 1 §4 puts `group_concat` in scope through
`lang_aggfunc.html`, which says of the ordinary aggregate that "The order of
the concatenated elements is arbitrary"; `windowfunctions.html` then uses
`group_concat()` in four worked examples and states each result, and every one
of them is the frame read from its first row to its last — the page explains
one of them in prose: "the frame for the row with (a=3) consists of rows
(2, 'B', 'two'), (3, 'C', 'three') and (4, 'D', 'one'). The result of
group_concat(b, '.') for that row is therefore 'B.C.D'." That reading is the
only one under which those four results are true, so it is the rule this step
scores — but only where the window `ORDER BY` totally orders the frame, which
is what makes "the frame's own order" mean something (§5.8, §5.13). Every other
aggregate this step writes as a window function is order-independent, so the
question does not arise for it.

### 5.6 The `WINDOW` clause and named windows

> Named window-defn clauses may also be added to a SELECT statement using a
> WINDOW clause and then referred to by name within window function
> invocations.

> The WINDOW clause, when one is present, comes after any HAVING clause and
> before any ORDER BY.

In scope:

* a `WINDOW` clause defining one name, and one defining several separated by
  commas, as in the page's `win1`/`win2` example;
* one name used by several window function invocations in the same `SELECT`,
  including invocations of different functions;
* a named window and an inline `OVER ( … )` in the same `SELECT`;
* the clause's position — after `GROUP BY` and `HAVING` where those are
  present, and before the statement's `ORDER BY`.

The grammar is `WINDOW window-name AS ( window-defn ) [, window-name AS
( window-defn )]...`, and an invocation refers to a name as `OVER window-name`,
without parentheses. Both spellings appear in the page's examples. Every
worked example on the page that uses a `WINDOW` clause is normative and the
verifier compares its result, with a total outer `ORDER BY` added where the
page omitted one.

### 5.7 Window functions with `GROUP BY`, `HAVING` and `ORDER BY`

A window function's input is "the results set of a SELECT statement" (§5.1),
and the `WINDOW` clause "comes after any HAVING clause and before any ORDER BY"
(§5.6). Read together, those two sentences fix the interaction this step
scores:

* in a query with `GROUP BY`, the rows a window function sees are the rows the
  grouping produced — one per group, after `HAVING` has discarded any — so a
  window function in such a query's result set may take the grouping
  expressions and the aggregates of that same `SELECT` as its arguments and as
  its window `ORDER BY` terms, and its partitions and frames are over those
  grouped rows;
* a window function may appear in the statement's `ORDER BY`, which is
  evaluated after the window functions have been computed;
* a window function may **not** appear in `GROUP BY` or `HAVING` themselves
  (§5.1, §5.11).

### 5.8 What fixes the value a window function returns

This section is a determinism rule, not new semantics; it says which
observations the verifier is willing to compare, so that a conforming
implementation is not failed by a choice the documentation leaves open.

The page fixes an order *within* a partition only through the `ORDER BY` of the
`window-defn`, and it says explicitly that rows with equal `ORDER BY` values
are peers. The order **among peers** is therefore not fixed by anything. So:

* wherever the verifier compares a value that depends on the position of a row
  inside a peer group — `row_number()`, `ntile()`, `lag()`, `lead()`,
  `first_value()`, `last_value()`, `nth_value()`, and any aggregate over a
  `ROWS` frame — the window `ORDER BY` it writes **totally orders** the rows of
  every partition, so there are no peers to disambiguate;
* where the verifier deliberately exercises peers, it compares only values the
  page makes peer-invariant: `rank()`, `dense_rank()`, `percent_rank()` and
  `cume_dist()`, which are defined in terms of the peer group itself, and
  aggregates over a `RANGE` frame, for which "Rows that are peers always have
  the same window frames";
* the output order of every scored query is fixed by a total `ORDER BY` on the
  outer `SELECT`, never by a window `ORDER BY` (§5.2);
* the column *name* a window function's result column gets when no `AS` alias
  is written is not compared anywhere; every scored window result column that
  is read by name carries an `AS` alias.

### 5.9 `ALTER TABLE … RENAME COLUMN`

`lang_altertable.html` is the SQLite 3.25.0 edition and is normative:

> The ALTER TABLE command in SQLite allows the user to rename a table, to
> rename a column within a table, or to add a new column to an existing table.

> The RENAME COLUMN TO syntax changes the column-name of table table-name into
> new-column-name . The column name is changed both within the table definition
> itself and also within all indexes, triggers, and views that reference the
> column.

The `alter-table-stmt` syntax diagram is an image and is not part of this
documentation copy, so the statement form is written out here:

```text
ALTER TABLE table-name RENAME COLUMN column-name TO new-column-name
```

Step 1 §1 already puts `ALTER TABLE … RENAME TO` and `ALTER TABLE … ADD COLUMN`
in scope and neither changes at this release. `RENAME COLUMN` joins them.

In scope, and scored:

* the column is known by its new name afterwards, in `SELECT` result columns,
  in `WHERE`, in `ORDER BY`, in `INSERT` and `UPDATE` column lists, and as a
  step-4 conflict target; **and by its old name nowhere** — a statement naming
  the old name is rejected exactly as one naming a column that never existed
  is (step 1 §1's `SELECT` and `lang_expr.html`);
* `PRAGMA table_info(T)` (step 1 §5) reports the new name in the renamed
  column's row, with the same `cid`, `type`, `notnull`, `dflt_value` and `pk`
  as before;
* **no change is made to table content** — "The ALTER TABLE command works by
  modifing the SQL text of the schema stored in the sqlite_master table. No
  changes are made to table content." Every row, and every value in the renamed
  column, reads back exactly as it did before;
* every index on the table stays attached and stays in force: a `UNIQUE` index
  still rejects a duplicate, a partial index still covers exactly the rows its
  `WHERE` clause selects, an index on an expression (step 3 §3.4) still answers
  the same queries, and `PRAGMA integrity_check` still reports `ok`;
* the rename takes part in transactions like any other statement (step 1 §7):
  it is undone by `ROLLBACK` and by `ROLLBACK TO` a savepoint, and it survives
  `COMMIT`, a reopen and a second process.

Renaming the column of a **non-existent table**, renaming a column that **does
not exist**, and renaming a column to a name the table **already uses** are the
three rejections §5.11 lists.

### 5.10 What `RENAME COLUMN` leaves in `sqlite_master.sql`

This is the one place in the benchmark where the *text* stored in
`sqlite_master.sql` is compared, and it is compared because two shipped pages
say what has to be there.

`lang_altertable.html`:

> The ALTER TABLE command works by modifing the SQL text of the schema stored
> in the sqlite_master table.

and, of `RENAME COLUMN` specifically, "The column name is changed both within
the table definition itself and also within all indexes … that reference the
column" (§5.9).

`fileformat2.html`
§2.6, *Storage Of The
SQL Database Schema*, which step 1 §6 already puts in scope:

> The text in the sqlite_master.sql column is a copy of the original CREATE
> statement text that created the object, except normalized as described above
> and as modified by subsequent ALTER TABLE statements.

Put together: the text is the original statement, subject to the five
normalisations step 1 §6 lists, **modified** by the rename — and the
modification `lang_altertable.html` describes is the change of the column name,
nothing else. So the rule this step scores is:

> After `ALTER TABLE t RENAME COLUMN old TO new`, the `sql` column of every
> `sqlite_master` row for `t` and for an index on `t` holds the text it held
> before, with each occurrence of the column identifier `old` replaced by
> `new`, and nothing else changed.

That covers the table's own `CREATE TABLE` text, the key of an ordinary or
`UNIQUE` `CREATE INDEX`, the key expression of an index on an expression
(step 3 §3.4), and the `WHERE` clause of a partial index (step 1 §1, step 4
§4.6). An index whose text does not mention the column keeps its text
unchanged, and `sql` stays `NULL` for the automatic `sqlite_autoindex_*`
indexes (step 1 §6).

Three restrictions the verifier puts on itself so that this comparison rests on
the rule above and on nothing else:

* every `CREATE` statement whose stored text is compared is written in
  **already-normalised** form — the leading keywords upper case, no `TEMP`, no
  database qualifier, no leading spaces, single spaces after the first two
  keywords — so the "before" text is the text as typed and the five
  normalisations of step 1 §6 have nothing left to do;
* both the old and the new column name are **ordinary unquoted identifiers**
  written in the same case everywhere they appear, and the old name never
  occurs in the statement except as a **reference to the column being
  renamed**. It may well occur as a *substring* of another identifier, of a
  keyword or of a literal, and in a scored case it does: the rule above is
  about "each occurrence of the column **identifier**", so a column named `a`
  is renamed without disturbing the `a` inside `TABLE`, `REAL` or `DEFAULT`, or
  inside a neighbouring column named `delta`. Rewriting the stored text by
  plain substring replacement is not enough;
* the comparison is only ever made on tables and indexes the verifier itself
  created in the same case.

Step 1 §1 says the text left in `sqlite_master.sql` after `ALTER TABLE …
RENAME TO` is **not** compared, and that stays true: no page says whether a
*table* rename rewrites the table name inside the stored text, and this
benchmark does not require it either way. `RENAME COLUMN` is the different
thing `lang_altertable.html` does state, and it is the only one compared.

### 5.11 Rejections the verifier scores

Each is observed the way every other rejection in this task is — the statement
fails and the observable database state is unchanged — never by the message
text (`../TEST_INTERFACE.md` §2.3, §6):

Window functions (§5.1, §5.3, §5.4, §5.5):

* a window function in a `WHERE` clause, in a `GROUP BY` clause, in a `HAVING`
  clause, in the `SET` list or `WHERE` clause of an `UPDATE` — including the
  `DO UPDATE` half of a step-4 upsert — or in the `WHERE` clause of a `DELETE`:
  "Window functions may only appear in the result set and in the ORDER BY
  clause of a SELECT statement";
* `DISTINCT` inside a window function invocation: "window functions cannot use
  the DISTINCT keyword";
* a `FILTER` clause on a built-in window function: "It is a syntax error to
  specify a FILTER clause as part of a built-in window function invocation";
* `<expr> PRECEDING` or `<expr> FOLLOWING` in a `RANGE` frame: "This frame
  boundary type may only be used with ROWS frames";
* an ending frame boundary that "take[s] a form that appears higher in the
  above list than the starting frame boundary", such as
  `ROWS BETWEEN CURRENT ROW AND 1 PRECEDING`;
* a frame boundary offset that is not a non-negative integer, such as
  `ROWS BETWEEN -1 PRECEDING AND CURRENT ROW`: `<expr>` "is a constant
  expression that evaluates to a non-negative integer value".

`RENAME COLUMN` (§5.9):

* `ALTER TABLE` naming a table that does not exist;
* `RENAME COLUMN` naming a column the table does not have — the page defines
  the statement as changing "the column-name of table table-name", and there is
  no such column to change;
* `RENAME COLUMN` whose new name is already the name of another column of the
  same table — `lang_createtable.html` and step 1 §1 give a table one column
  per name, and `PRAGMA table_info` (step 1 §5) returns one row per column, so
  a table cannot have two columns called the same thing.

In all three cases the schema is left exactly as it was: `sqlite_master` and
`PRAGMA table_info` report what they reported before, and the old column name
still works.

### 5.12 What step 5 does *not* change

A window function is an expression in a `SELECT`'s result set, and
`RENAME COLUMN` is a third form of a statement step 1 already has. Every
statement, value-model rule, expression construct, function, catalog fact,
pragma, durability guarantee and C entry point of steps 1, 2, 3 and 4 keeps
exactly the meaning those steps gave it, and the verifier re-exercises a sample
of each: DDL and DML, the value model, transactions and durability, the
command-line contract, the C library, step 2's `WITH` clause, `VALUES` queries
and `printf()`, step 3's JSON1 functions and indexes on expressions, and step
4's UPSERT.

Because this is the last step, the sample is the widest in the benchmark: one
check for each of the four earlier steps' own capabilities plus the four
substrate families. They are **gate** cases rather than score groups: breaking
an earlier milestone fails this step's gate outright rather than costing a
fraction of its score, and this is the last step, so the gate is the only
thing standing between a regression and a clean-looking final artifact.
"Sample" is still meant literally — those checks re-run a handful of the
earlier steps' own cases rather than their whole suites, and two inherited
rules stay outside the sample: **`make clean`**
(`../TEST_INTERFACE.md` §1), outside it as it has been since step 2, and the
**database file's header fields** (`../TEST_INTERFACE.md` §4), outside it since
step 2 as well. Both are still requirements of the workspace and step 1 scores
both, but step 5 scores no group on either — the eight checks listed above are
the whole of this step's inherited sample. Nothing is relaxed: a rule the
sample happens not to reach is still binding.

A windowed query and a `RENAME COLUMN` are both ordinary work, so the rest of
the profile applies to them unchanged. A `SELECT` carrying window functions
reads through the same journal, transaction and durability machinery as any
other query and changes nothing; a `RENAME COLUMN` is undone by `ROLLBACK`,
survives `COMMIT` and a reopen, keeps every index consistent, and leaves
`PRAGMA integrity_check` reporting `ok` (step 1 §1, §7).

Three things a reader of the refreshed pages should not mistake for changes of
substance:

* `lang_UPSERT.html` gains a **Limitations** section at this release saying
  that UPSERT "does not currently work for virtual tables". Virtual tables are
  already out of scope at every step by step 1 §8, so nothing moves; step 4
  §4.9 already said so.
* `lang_keywords.html` now lists **136** keywords where the 3.24.0 edition
  listed 124. Twelve names became keywords, **ten** of them for this step's own
  syntax — `CURRENT`, `FILTER`, `FOLLOWING`, `OVER`, `PARTITION`, `PRECEDING`,
  `RANGE`, `ROWS`, `UNBOUNDED`, `WINDOW` — and the other two, `DO` and
  `NOTHING`, for step 4's, whose keyword-list entry lagged a release. All
  twelve belong to step 4's or step 5's syntax; none is anything else.
  The page also mentions three `sqlite3_keyword_*` C interfaces; they are not
  in `../TEST_INTERFACE.md` §3.2's list, so they are not part of the interface.
* `c3ref/changes.html`, `c3ref/total_changes.html`, `c3ref/column_blob.html`
  and `c3ref/errcode.html` were reworded, mostly to add cross-references and to
  describe out-of-memory behaviour. None of it changes what
  `../TEST_INTERFACE.md` §3.2 requires or what §6 declines to score;
  out-of-memory behaviour is not observed anywhere in this benchmark.

The one plumbing value that moves is the release string —
`../TEST_INTERFACE.md` §3.3.

### 5.13 Out of scope at step 5

In addition to step 1 §8, step 2 §2.5, step 3 §3.7 and step 4 §4.9, which all
still apply:

* **`GROUPS` frames and the `EXCLUDE` clause.** `windowfunctions.html`
  §2.1 (*Frame Specifications*)
  says the frame type is "either RANGE or ROWS" and names no third type and no
  `EXCLUDE` clause, and `lang_keywords.html`'s 136-keyword list contains
  neither `GROUPS` nor `EXCLUDE`. They are not part of this release and are not
  in scope; the verifier never writes either word, and accepting or rejecting
  one is unscored.
* **A frame whose two boundaries are both `UNBOUNDED PRECEDING`, and one whose
  two boundaries are both `UNBOUNDED FOLLOWING`.** §5.1's grammar makes
  `frame-bound` the same production in both positions, and the only ordering
  rule `windowfunctions.html` states is that the ending boundary "must not take
  a form that appears higher in the above list than the starting frame
  boundary" — which two boundaries of the *same* form satisfy, and which the
  legal `ROWS BETWEEN CURRENT ROW AND CURRENT ROW`,
  `ROWS BETWEEN 2 PRECEDING AND 1 PRECEDING` and
  `ROWS BETWEEN 1 FOLLOWING AND 2 FOLLOWING` show is not read as forbidding
  equality of form. Those two are the only placements of `UNBOUNDED PRECEDING`
  at the end, or of `UNBOUNDED FOLLOWING` at the start, that the ordering rule
  does not already reject, and whether they are *additionally* rejected is
  stated by no shipped page. So the verifier never writes either one, and
  accepting or rejecting one is unscored. The three frame rejections §5.11
  lists are the only frame rejections scored.
* **The concatenation order of `group_concat()` over a frame that the window
  `ORDER BY` does not totally order.** §5.5 fixes the order only where the
  frame is totally ordered. The page's `group_concat(b, '.') OVER (ORDER BY c)`
  example concatenates over a peer group of three rows that `ORDER BY c` does
  not order among themselves, so **that example's `group_concat` column is not
  compared**; the peer and default-frame behaviour it demonstrates is scored
  with order-independent aggregates instead, and every other column of the
  example is compared.
* **User-defined aggregate window functions.**
  `windowfunctions.html` §4
  (*User-Defined Aggregate Window Functions*) documents
  `sqlite3_create_window_function()` and the `xStep`/`xFinal`/`xValue`/
  `xInverse` callbacks. User-defined functions are already out of scope at
  every step by step 1 §8, that entry point is not in
  `../TEST_INTERFACE.md` §3.2's list, and the verifier never calls it. The
  section's `sumint()` example is not scored.
* **A window function in a `SELECT` that also carries `LIMIT` or `OFFSET`.**
  `windowfunctions.html` never mentions either clause and `lang_select.html`'s
  prose never mentions window functions, so no shipped page says how they
  combine — in particular, whether `LIMIT` applies before or after the window
  functions are computed. The verifier never writes the combination, and any
  behaviour is unscored. `LIMIT` and `OFFSET` keep their step 1 §1 meaning on
  every query without a window function.
* **A `FILTER` clause without an `OVER` clause**, on an ordinary aggregate.
  `windowfunctions.html` §2.3
  (*The FILTER Clause*) documents `FILTER` only as part of a window function
  invocation and `lang_aggfunc.html` does not mention it at all, so the verifier
  always writes `FILTER` together with `OVER`, and the bare form is unscored in
  both directions.
* **A reference to a window name that no `WINDOW` clause defines**, and two
  `WINDOW` definitions sharing one name. The page says a named window is
  "referred to by name" and says nothing about either case.
* **`ntile(N)` for `N` that is not a positive integer**, and a negative
  `offset` argument to `lag()` or `lead()`. The page says `N` "is handled as an
  integer" and that `offset` "must be a non-negative integer" without saying
  what happens otherwise.
* **Whether a window function's `expr` arguments may themselves contain
  another window function**, and whether a window `ORDER BY` may name a result
  column alias of the same `SELECT`. Neither is stated. The verifier writes
  ordinary expressions over the query's own columns, and — in a `GROUP BY`
  query — the grouping expressions and aggregates §5.7 allows.
* **The result column *name* of a window function invocation written without
  an `AS` alias**, and `sqlite3_column_decltype()` for such a column
  (§5.8, `../TEST_INTERFACE.md` §6).
* **`ALTER TABLE … RENAME COLUMN` with the `COLUMN` keyword omitted.** The
  shipped prose calls this "The RENAME COLUMN TO syntax" and never writes the
  shorter spelling; the grammar that would settle it is on the unshipped
  diagram. The verifier always writes `RENAME COLUMN`, and the short form is
  unscored in both directions.
* **`RENAME COLUMN` on a quoted, keyword or non-ASCII column name**, and
  renaming to one. §5.10's text comparison depends on how a new identifier is
  quoted when it is written back into the schema, which no page states; the
  verifier writes ordinary unquoted identifiers on both sides.
* **`RENAME COLUMN` where a trigger or a view references the column**,
  including the "semantic ambiguity" failure `lang_altertable.html` describes.
  Triggers and views are out of scope at every step by step 1 §8, so no scored
  case creates one and that sentence's rejection is never exercised.
* **`ALTER TABLE` on `sqlite_master` or on any other `sqlite_`-prefixed
  object.** Step 1 §6 reserves those names for internal use; what an
  implementation does when asked to alter one is not stated on any shipped
  page.
* **Renaming a column that an index on an expression mentions inside a string
  literal**, and any other occurrence of the identifier's spelling that is not
  a reference to the column. §5.10's rule is stated for the column identifier;
  the verifier never constructs a schema where the two are confusable.
