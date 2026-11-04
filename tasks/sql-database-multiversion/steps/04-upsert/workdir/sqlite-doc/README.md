# SQLite 3.24.0 documentation — navigation

Every `.html` file here is verbatim upstream documentation for **SQLite
3.24.0 (2018-06-04)**; see `LICENSE-NOTICE.md`. There is no implementation
source in this directory. `PROFILE.md` states which of this documentation is in
scope for step 04; `../TEST_INTERFACE.md` states how the verifier
reaches your code.

This directory holds **75 pages**, 27 of them under
`c3ref/`. They are a selected subset of the release's documentation, chosen to
cover everything in scope; the pages are unmodified but the set is not the
whole upstream site, so a cross-reference may point at a page that is not here.

## Where each part of the contract is defined

| Topic | Page |
| --- | --- |
| Index of every SQL statement | `lang.html` |
| Value model, storage classes, type affinity, comparison and sorting | `datatype3.html` |
| NULL handling | `nulls.html` |
| Expression syntax and semantics, operators, precedence, `CAST`, `CASE`, `LIKE`, `IN`, parameters | `lang_expr.html` |
| Keywords and how to quote identifiers | `lang_keywords.html`, `lang_naming.html` |
| `CREATE TABLE`, column constraints, table constraints, `PRIMARY KEY`, `AUTOINCREMENT` | `lang_createtable.html`, `autoinc.html` |
| `DROP TABLE`, `ALTER TABLE` | `lang_droptable.html`, `lang_altertable.html` |
| `CREATE INDEX`, `DROP INDEX`, partial indexes | `lang_createindex.html`, `lang_dropindex.html`, `partialindex.html` |
| `INSERT`, `REPLACE`, `UPDATE`, `DELETE` | `lang_insert.html`, `lang_replace.html`, `lang_update.html`, `lang_delete.html` |
| `SELECT`: joins, `WHERE`, `GROUP BY`, `HAVING`, compound selects, `ORDER BY`, `LIMIT` | `lang_select.html` |
| Conflict resolution (`ON CONFLICT`, `INSERT OR …`) | `lang_conflict.html` |
| Transactions, `SAVEPOINT` | `lang_transaction.html`, `lang_savepoint.html`, `atomiccommit.html` |
| Write-ahead logging and checkpointing | `wal.html` |
| File locking states | `lockingv3.html` |
| `PRAGMA` statements | `pragma.html` |
| Built-in scalar functions | `lang_corefunc.html` |
| Aggregate functions | `lang_aggfunc.html` |
| Date and time functions | `lang_datefunc.html` |
| Database file header fields | `fileformat2.html` §1.2, *The Database Header* |
| The `sqlite_master` schema table, its `sql` normalisation rules, the automatic `sqlite_autoindex_*` indexes, and `sqlite_sequence` | `fileformat2.html` §2.6, *Storage Of The SQL Database Schema* |
| `VACUUM` | `lang_vacuum.html` |
| Implementation limits | `limits.html` |
| The command-line shell | `cli.html` |
| C interface introduction and full reference | `cintro.html`, `capi3ref.html` |
| Individual C functions, types and result codes | `c3ref/` |
| Query planning, `EXPLAIN QUERY PLAN` (informational only) | `optoverview.html`, `eqp.html`, `lang_explain.html` |
| Upstream copyright statement | `copyright.html` |

## New at step 4

`lang_UPSERT.html` — **note the capitalised file name**, the only page in this
directory with one — is new in this documentation set and did not exist in the
SQLite 3.9.0 documentation. It is the only page added.
`lang_insert.html` and `lang_conflict.html` are the SQLite 3.24.0 editions
and both changed for UPSERT: `lang_insert.html` gained the parsing-ambiguity
rule, the `AS alias` phrase and two sentences about `DEFAULT VALUES`, and
`lang_conflict.html` gained a paragraph warning you not to confuse the
`ON CONFLICT` *clause* it documents with the `ON CONFLICT` phrase inside an
UPSERT. Re-read both.

**One inherited page changed a rule you were scored on at step 3.**
`partialindex.html` now says that a partial index's `WHERE` clause "may not
contain subqueries, references to other tables, non-deterministic functions, or
bound parameters" — where the SQLite 3.9.0 edition said *functions*,
with no qualifier, and carried an extra paragraph extending the prohibition to
`LIKE`, `GLOB`, `MATCH` and `REGEXP`. That paragraph is gone. `PROFILE.md` §4.6
records the change and says in as many words that it supersedes step 3 §3.4.

The gap between this release and the last is two years and eight months and
fifteen minor releases, so the rest of the set moved a long way. Every other page is the SQLite 3.24.0
edition of a page you already had, and **49 of the 74 you had differ in
prose**; the remaining 25 differ only in the site template and the labels of
the show/hide syntax-diagram controls. Nothing this step's `PROFILE.md` puts in
scope was removed. Among the largest edits are `capi3ref.html` (542 KB — do not
read it end to end), `cli.html`, `pragma.html`, `json1.html`,
`fileformat2.html`, `optoverview.html`, `eqp.html`, `lang_keywords.html`,
`wal.html`, `atomiccommit.html`, `datatype3.html`, `lang_expr.html` and
`lang_corefunc.html`.

Four of those are worth knowing about before you re-read them:

* **`json1.html` renumbered its sections** between the two releases — *Interface
  Overview* is §3 here and was §2.0, *JSON arguments* is §3.1 and was §2.1 —
  and it now documents more functions than the eleven `PROFILE.md` §3.1 names.
  The eleven are still the closed list; the extras are out of scope.
* **`lang_corefunc.html` and `lang_aggfunc.html` list functions that step 1
  §4's closed lists do not.** Everything outside those lists stays out of
  scope, at this step as at every other.
* **`lang_update.html` documents a parenthesised column list on the left of a
  `SET` assignment, and `lang_vacuum.html` a schema name after `VACUUM`.** Both
  are out of scope; `PROFILE.md` §4.8 says so.
* **`c3ref/last_insert_rowid.html` now mentions
  `sqlite3_set_last_insert_rowid()`.** It is not in `../TEST_INTERFACE.md`
  §3.2's list, so it is not part of the interface you must provide.

The navigation table above does not cover the new material, so:

| Topic | Page |
| --- | --- |
| UPSERT: `ON CONFLICT`, the conflict target, `DO NOTHING`, `DO UPDATE`, the `excluded.` qualifier | `lang_UPSERT.html` |
| Where the upsert clause may and may not appear on an `INSERT`, the `AS alias` phrase, and the `INSERT … SELECT` parsing ambiguity | `lang_insert.html` |
| The inherited `ON CONFLICT` clause and the five `INSERT OR …` algorithms — a different thing with the same name | `lang_conflict.html` |
| What a partial index's `WHERE` clause may contain, and unique partial indexes | `partialindex.html` |
| The JSON1 functions, JSON path syntax, and the JSON/PATH/VALUE argument rules | `json1.html` |
| Indexes on expressions and their restrictions | `expridx.html`, `lang_createindex.html` |
| The `WITH` clause and ordinary common table expressions | `lang_with.html` |
| A `WITH` clause on `INSERT` / `UPDATE` / `DELETE` | `lang_with.html`; the `with-clause` element of `lang_insert.html`, `lang_update.html`, `lang_delete.html` |
| The `VALUES` clause used as a query, and its restrictions | `lang_select.html`, sections *The VALUES clause*, *The WITH Clause*, *Compound Select Statements*, *The ORDER BY clause*, *The LIMIT clause* |
| The `printf()` SQL function | `lang_corefunc.html` |

The command-line shell page has been `cli.html` since step 3; there is no
`sqlite.html` in this directory.

The syntax diagrams on these pages are images, and the image files are **not**
part of this copy. That matters more at this step than at the last one: the
grammar of the upsert clause — that the conflict target is a parenthesised list
which may be followed by its own `WHERE` clause, and that `DO UPDATE SET` may
be followed by one too — appears on `lang_UPSERT.html` only in the diagram.
`PROFILE.md` §4.1 writes that grammar out in text, and §§4.2-4.7 state the
parts this step is scored on.
