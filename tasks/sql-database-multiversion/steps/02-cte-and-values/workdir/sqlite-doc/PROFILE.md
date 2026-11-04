# Benchmark profile — step 02 of 5, SQLite 3.8.3

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
