# SQLite 3.9.0 documentation — navigation

Every `.html` file here is verbatim upstream documentation for **SQLite
3.9.0 (2015-10-14)**; see `LICENSE-NOTICE.md`. There is no implementation
source in this directory. `PROFILE.md` states which of this documentation is in
scope for step 03; `../TEST_INTERFACE.md` states how the verifier
reaches your code.

This directory holds **74 pages**, 27 of them under
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

## New at step 3

Two pages are new because SQLite 3.9.0 added the features they describe,
and neither existed in the SQLite 3.8.3 documentation:
`json1.html` (the JSON1 function family) and `expridx.html` (indexes on
expressions). Two more are new for other reasons:

* **The command-line shell page is now `cli.html`.** Upstream renamed it
  from `sqlite.html` at this release and left a redirect stub behind under the
  old name; the stub is not part of this copy, so `sqlite.html` is **not** in
  this directory and the navigation table above points at `cli.html`. The
  content is the same page, in its SQLite 3.9.0 edition.
* `rescode.html`, upstream's consolidated list of result codes, appears for the
  first time in this documentation set. `../TEST_INTERFACE.md` §3.3 is still
  the closed list of result codes the verifier compares, and `PROFILE.md` §3.7
  says so.

Every other page is the SQLite 3.9.0 edition of a page you already had.
**46 of the 70 you had differ**: 45 differ in their prose and one
(`lang_savepoint.html`) only in the labels of the show/hide syntax-diagram
controls. Nothing this step's `PROFILE.md` puts in scope was removed.

The pages that matter most for this step's new scope are `expridx.html` and
`lang_createindex.html`, which now carries the same restrictions in one
paragraph ("Expressions in an index may not reference other tables and may not
use subqueries nor functions whose result might change"), and `json1.html`.
`partialindex.html` matters too, because an index key may now be a function
call while the partial-index `WHERE` clause still may not contain one.

One navigation warning about `json1.html`: its own section numbers repeat in
this edition — two sections are numbered 3.7 (*The json_remove() function* and
*The json_type() function*) and two are numbered 3.9 (*The json_valid()
function* and *The json_each() and json_tree() table-valued functions*), and
there is no 3.8. `PROFILE.md` §3.1 therefore lists each function's section by
**title**, and so should you when you make a note of one.

Two edits to inherited pages are worth knowing about before you re-read them:

* **`pragma.html` now enumerates the output columns of `index_list`** — five of
  them, where the previous edition named only the index name and the UNIQUE
  flag. `PROFILE.md` §3.5 records the shape and says it supersedes step 1 §5's
  three-column row from this step onward. `index_info` was reworded on the same
  page without changing its three columns.
* **`datatype3.html` renames the NONE type affinity to BLOB**, and says so in a
  historical note. The affinity rules themselves did not change; `PROFILE.md`
  §3.6 records it so the rename is not mistaken for one.

Among the other larger edits are `capi3ref.html`, `fileformat2.html`,
`cintro.html`, `wal.html`, `lang_expr.html` (which now documents hexadecimal
integer literals — out of scope, `PROFILE.md` §3.6), `lang_altertable.html`,
`optoverview.html` and `lang_with.html`. Re-read the parts your implementation
relies on rather than assuming they match the copies you saw last step.

The navigation table above does not cover the new material, so:

| Topic | Page |
| --- | --- |
| The JSON1 functions, JSON path syntax, and the JSON/PATH/VALUE argument rules | `json1.html` |
| Indexes on expressions and their restrictions | `expridx.html`, `lang_createindex.html` |
| What a partial index's `WHERE` clause may contain | `partialindex.html` |
| The `WITH` clause and ordinary common table expressions | `lang_with.html` |
| The `VALUES` clause used as a query, and its restrictions | `lang_select.html`, sections *The VALUES clause*, *The WITH Clause*, *Compound Select Statements*, *The ORDER BY clause*, *The LIMIT clause* |
| A `WITH` clause on `INSERT` / `UPDATE` / `DELETE` | `lang_with.html`; the `with-clause` element of `lang_insert.html`, `lang_update.html`, `lang_delete.html` |
| The `printf()` SQL function | `lang_corefunc.html` |

The syntax diagrams on these pages are images, and the image files are **not**
part of this copy; where a page's grammar matters, its prose and worked
examples carry it, and `PROFILE.md` states the parts this step is scored on.
