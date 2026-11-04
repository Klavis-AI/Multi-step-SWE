# Benchmark profile — step 01 of 5, SQLite 3.8.0

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
**`fileformat2.html` §2.5
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
§2.5.1 (*Internal Schema Objects*) names
those automatic indexes `sqlite_autoindex_TABLE_N` and reserves every name beginning
with `sqlite_` for internal use, so an attempt to `CREATE` a table or index
with such a name is rejected. **`rootpage` values are internal and are never
compared** beyond being a positive page number for a table or an index.

`sqlite_sequence` is in scope for `AUTOINCREMENT` tables with the two columns
`name` and `seq` that `fileformat2.html`
§2.5.2 gives it, holding the largest
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
