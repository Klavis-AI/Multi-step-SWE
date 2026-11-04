# SQLite 3.25.0 documentation — navigation

Every `.html` file here is verbatim upstream documentation for **SQLite
3.25.0 (2018-09-15)**; see `LICENSE-NOTICE.md`. There is no implementation
source in this directory. `PROFILE.md` states which of this documentation is in
scope for step 05; `../TEST_INTERFACE.md` states how the verifier
reaches your code.

This directory holds **76 pages**, 27 of them under
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
| Database file header fields | `fileformat2.html` §1.3, *The Database Header* |
| The `sqlite_master` schema table, its `sql` normalisation rules, the automatic `sqlite_autoindex_*` indexes, and `sqlite_sequence` | `fileformat2.html` §2.6, *Storage Of The SQL Database Schema* |
| `VACUUM` | `lang_vacuum.html` |
| Implementation limits | `limits.html` |
| The command-line shell | `cli.html` |
| C interface introduction and full reference | `cintro.html`, `capi3ref.html` |
| Individual C functions, types and result codes | `c3ref/` |
| Query planning, `EXPLAIN QUERY PLAN` (informational only) | `optoverview.html`, `eqp.html`, `lang_explain.html` |
| Upstream copyright statement | `copyright.html` |

## New at step 5

`windowfunctions.html` is new in this documentation set, did not exist in the
SQLite 3.24.0 documentation, and is the only page added. It is the
normative page for everything `PROFILE.md` §§5.1-5.8 puts in scope, including
the `WINDOW` clause — **`lang_select.html` does not document the `WINDOW`
clause**: at this release the clause appears there only inside the `select-stmt`
syntax diagram, and the diagrams are images that are not part of this copy.

`lang_altertable.html` is the SQLite 3.25.0 edition and is the other page
you must re-read: it now documents `RENAME COLUMN`, it says that `ALTER TABLE`
"works by modifing the SQL text of the schema stored in the sqlite_master
table", and the three paragraphs the 3.24.0 edition carried about
`RENAME TO` not updating triggers and views have been replaced by one sentence
saying that from 3.25.0 those references *are* renamed.

The gap between this release and the last is **103 days**, with no release in
between, so the rest of the set barely moved. Of the 75 pages you already had,
**40 are byte-for-byte identical**, **21 changed without a word of prose
changing**, and **14 differ in prose**. Of the 21: sixteen differ only in the
syntax-diagram show/hide controls — the new `window-defn`, `frame-spec` and
`filter` elements were added to the diagram trees of pages that embed `expr`,
which is most of the `lang_*` set, and upstream numbers those controls
sequentially across the whole set, so one insertion renumbers every later page.
The other five differ in that and in one invisible detail apiece: a renamed
HTML anchor (`lang_expr.html`, `lang_with.html`), a new search-index entry
(`c3ref/sqlite3.html`), and the `hotjrnl` cross-reference that moved from
`lockingv3.html` to `fileformat2.html` (`lockingv3.html`, `rescode.html`).
Nothing a reader sees changed on any of the 21. The fourteen that did:

| Page | What changed |
| --- | --- |
| `lang_altertable.html` | `RENAME COLUMN`, and the `RENAME TO` paragraphs above |
| `lang_keywords.html` | the keyword list grew from **124** to **136**. **All twelve** additions are this step's syntax or step 4's — ten of this step's, and `DO` and `NOTHING` for step 4's UPSERT, whose keyword-list entry lagged a release: `CURRENT`, `DO`, `FILTER`, `FOLLOWING`, `NOTHING`, `OVER`, `PARTITION`, `PRECEDING`, `RANGE`, `ROWS`, `UNBOUNDED`, `WINDOW`. **Neither `GROUPS` nor `EXCLUDE` is in the list**, which is why `PROFILE.md` §5.13 puts both out of scope |
| `lang_UPSERT.html` | gained a *Limitations* section: UPSERT "does not currently work for virtual tables". Virtual tables were already out of scope (`PROFILE.md` step 1 §8) |
| `fileformat2.html` | a new §1.1 *Hot Journals* pushed *The Database Header* from §1.2 to §1.3. `../TEST_INTERFACE.md` §4 and `PROFILE.md` cite it by title and give the number for **this** edition |
| `lang_createtable.html` | one sentence about constant `DEFAULT` expressions reworded |
| `json1.html` | two typographical fixes ("a element" → "an element"). Nothing about the functions changed |
| `c3ref/changes.html`, `c3ref/total_changes.html` | reworked "See also" lists; `total_changes()` gained a paragraph saying it ignores other connections |
| `c3ref/errcode.html` | dropped the sentence that the return value after a *successful* call is undefined, and added the list of error-code-preserving interfaces. `../TEST_INTERFACE.md` §6 still reads it only after a failure |
| `c3ref/column_blob.html` | the memory-allocation-failure paragraph was rewritten. Out-of-memory behaviour is observed nowhere in this benchmark |
| `capi3ref.html` | the aggregate of all of the above, plus `sqlite3_create_window_function()`. **549 KB — do not read it end to end** |
| `about.html`, `pragma.html`, `wal.html` | prose and cross-reference edits with no effect on anything `PROFILE.md` puts in scope |

Two inherited pages you might expect to have changed did **not** change in
prose: `partialindex.html` — so `PROFILE.md` §4.6's reworded `WHERE`-clause
rule, introduced at step 4, is still exactly what the page beside you says —
and `lang_aggfunc.html`, whose functions are the ones `windowfunctions.html`
means by "All of SQLite's aggregate functions".

The navigation table above does not cover the new material or the material
added at steps 2, 3 and 4, so:

| Topic | Page |
| --- | --- |
| Window functions: the `OVER` clause, `PARTITION BY`, the window `ORDER BY`, frame specifications, the `FILTER` clause, the eleven built-in window functions, aggregates used as window functions, and the `WINDOW` clause | `windowfunctions.html` |
| `ALTER TABLE … RENAME COLUMN`, and that `ALTER TABLE` works by modifying the stored schema text | `lang_altertable.html` |
| What `sqlite_master.sql` holds, its normalisation rules, and that it is "modified by subsequent ALTER TABLE statements" | `fileformat2.html` §2.6, *Storage Of The SQL Database Schema* |
| Aggregate functions — the ones that may be used as window functions | `lang_aggfunc.html` |
| UPSERT: `ON CONFLICT`, the conflict target, `DO NOTHING`, `DO UPDATE`, the `excluded.` qualifier | `lang_UPSERT.html` — note the capitalised file name, still the only one in this directory |
| Where the upsert clause may and may not appear on an `INSERT`, the `AS alias` phrase, and the `INSERT … SELECT` parsing ambiguity | `lang_insert.html` |
| The inherited `ON CONFLICT` clause and the five `INSERT OR …` algorithms — a different thing with the same name | `lang_conflict.html` |
| What a partial index's `WHERE` clause may contain, and unique partial indexes | `partialindex.html`; the rule was reworded at step 4 and `PROFILE.md` §4.6 records it |
| The JSON1 functions, JSON path syntax, and the JSON/PATH/VALUE argument rules | `json1.html` |
| Indexes on expressions and their restrictions | `expridx.html`, `lang_createindex.html` |
| The `WITH` clause and ordinary common table expressions | `lang_with.html` |
| A `WITH` clause on `INSERT` / `UPDATE` / `DELETE` | `lang_with.html`; the `with-clause` element of `lang_insert.html`, `lang_update.html`, `lang_delete.html` |
| The `VALUES` clause used as a query, and its restrictions | `lang_select.html`, sections *The VALUES clause*, *The WITH Clause*, *Compound Select Statements*, *The ORDER BY clause*, *The LIMIT clause* |
| The `printf()` SQL function | `lang_corefunc.html` |

The command-line shell page has been `cli.html` since step 3; there is no
`sqlite.html` in this directory.

The syntax diagrams on these pages are images, and the image files are **not**
part of this copy. That matters at this step in two places, and `PROFILE.md`
writes both grammars out in text: the `window-function-invocation`,
`window-defn` and `frame-spec` shapes (§5.1) and the
`ALTER TABLE … RENAME COLUMN` statement form (§5.9). It mattered at step 4 for
the upsert clause too, and §4.1 still carries that one.
