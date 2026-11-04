# SQLite 3.8.0 documentation — navigation

Every `.html` file here is verbatim upstream documentation for **SQLite
3.8.0 (2013-08-26)**; see `LICENSE-NOTICE.md`. There is no implementation
source in this directory. `PROFILE.md` states which of this documentation is in
scope for step 01; `../TEST_INTERFACE.md` states how the verifier
reaches your code.

This directory holds **70 pages**, 27 of them under
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
| The `sqlite_master` schema table, its `sql` normalisation rules, the automatic `sqlite_autoindex_*` indexes, and `sqlite_sequence` | `fileformat2.html` §2.5, *Storage Of The SQL Database Schema* |
| `VACUUM` | `lang_vacuum.html` |
| Implementation limits | `limits.html` |
| The command-line shell | `sqlite.html` |
| C interface introduction and full reference | `cintro.html`, `capi3ref.html` |
| Individual C functions, types and result codes | `c3ref/` |
| Query planning, `EXPLAIN QUERY PLAN` (informational only) | `optoverview.html`, `eqp.html`, `lang_explain.html` |
| Upstream copyright statement | `copyright.html` |

## New at step 1

Everything. This is the first step; the whole documentation set above is new.
