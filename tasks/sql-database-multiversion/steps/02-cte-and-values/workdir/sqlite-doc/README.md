# SQLite 3.8.3 documentation — navigation

Every `.html` file here is verbatim upstream documentation for **SQLite
3.8.3 (2014-02-03)**; see `LICENSE-NOTICE.md`. There is no implementation
source in this directory. `PROFILE.md` states which of this documentation is in
scope for step 02; `../TEST_INTERFACE.md` states how the verifier
reaches your code.

This directory holds **71 pages**, 27 of them under
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
| The command-line shell | `sqlite.html` |
| C interface introduction and full reference | `cintro.html`, `capi3ref.html` |
| Individual C functions, types and result codes | `c3ref/` |
| Query planning, `EXPLAIN QUERY PLAN` (informational only) | `optoverview.html`, `eqp.html`, `lang_explain.html` |
| Upstream copyright statement | `copyright.html` |

## New at step 2

`lang_with.html` — the `WITH` clause — is new in this documentation set; it did
not exist in the SQLite 3.8.0 documentation. It is the only page
added.

Every other page is the SQLite 3.8.3 edition of a page you already had.
**35 of the 70 you had differ**: 30 differ in their prose, and 5 differ only
because the 3.8.3 documentation build wraps each syntax diagram in a
show/hide control whose labels are part of the page text. The pages that matter
most for this step's new scope are `lang_select.html` (the `VALUES` clause, the
`WITH` clause and the compound/`ORDER BY`/`LIMIT` restrictions) and
`lang_corefunc.html` (`printf()`, and two other new functions that `PROFILE.md`
§2.3 puts out of scope). `lang_insert.html`, `lang_update.html` and
`lang_delete.html` each acquire a `with-clause` element in their grammar, but
what they gain in *text* is only a block of syntax-diagram labels, `with-clause:`
among them — the diagrams themselves are images and are not shipped — and no
prose. So read the rule on `lang_with.html` and in `PROFILE.md` §2.1, not on
those three pages.

Pages you relied on at the previous step changed too — among the largest edits
are `lang_select.html`, `fileformat2.html`, `sqlite.html`, `capi3ref.html`,
`lang_createtable.html`, `optoverview.html`, `lang_expr.html`,
`lang_altertable.html`, `pragma.html`, `lang_createindex.html` and
`partialindex.html`. Re-read the parts your implementation relies on rather
than assuming they match the copies you saw last step. Note that
`fileformat2.html` renumbered its schema-layer sections at this release: a new
*Representation of WITHOUT ROWID Tables* section pushed *Storage Of The SQL
Database Schema* down one, so cite that section by its title, and use the
numbers in the navigation table above, which are this edition's.

Nothing this step's `PROFILE.md` puts in scope was *removed*. One in-scope page
did lose a sentence — `lang_altertable.html` dropped "It is not possible to
rename a column, remove a column, or add or remove constraints from a table" —
but renaming and dropping a column are not in scope at any step, so nothing you
must implement changed. The profile is cumulative, so step 1's section still
says what step 1 requires.

The navigation table above does not cover the new material, so:

| Topic | Page |
| --- | --- |
| The `WITH` clause and ordinary common table expressions | `lang_with.html` |
| The `VALUES` clause used as a query, and its restrictions | `lang_select.html`, sections *The VALUES clause*, *The WITH Clause*, *Compound Select Statements*, *The ORDER BY clause*, *The LIMIT clause* |
| A `WITH` clause on `INSERT` / `UPDATE` / `DELETE` | `lang_with.html`; the `with-clause` element of `lang_insert.html`, `lang_update.html`, `lang_delete.html` |
| The `printf()` SQL function | `lang_corefunc.html` |

The syntax diagrams on these pages are images, and the image files are **not**
part of this copy; where a page's grammar matters, its prose and worked
examples carry it, and `PROFILE.md` §2.1 and §2.2 state the parts this step is
scored on.
