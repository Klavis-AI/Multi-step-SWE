# Step 2 — Common table expressions and `VALUES` (SQLite 3.8.3)

## 1. Implement this milestone

`/app` already holds the SQLite-compatible engine you delivered for step 1,
built by `make` into `/app/build/libsqlite3.a` and `/app/build/sqlite3`.
Continue that implementation in place — this step adds SQL surface to it, and
changes nothing else.

Move the engine from SQLite 3.8.0 to **SQLite 3.8.3** by adding exactly the
three capabilities `/app/sqlite-doc/PROFILE.md` §2 puts in scope, plus the
release string:

- **the `WITH` clause and ordinary common table expressions** (§2.1) — on
  `SELECT`, `INSERT`, `UPDATE` and `DELETE`; several comma-separated CTEs in one
  `WITH`, a later one able to read an earlier one; with and without a
  parenthesised column-name list; referenced more than once, including
  self-joined under two aliases; referenced from `FROM`, `IN`, scalar and
  `EXISTS` subqueries; a `WITH` at the beginning of a subquery; a CTE body that
  is any `SELECT` already in scope at step 1, or a `VALUES` clause. A CTE name
  lives only for the statement that defines it, shadows a real table of the same
  name inside that statement, and its column-name list must have exactly as many
  names as its query has columns;
- **the `VALUES` clause used as a query** (§2.2) — as a complete statement, as a
  `FROM` subquery, inside `IN`/scalar/`EXISTS` subqueries, as either operand of
  a compound operator, as an `INSERT` source, and as the body of a common table
  expression. Its rows appear in the order written, and every row-value list has
  the same width;
- **the `printf()` SQL function** (§2.3) — the conversions, flags, width and
  precision listed there, and the five rules `lang_corefunc.html` states about
  a missing or `NULL` format and missing arguments;
- **the release string**: `SQLITE_VERSION`, `sqlite3_libversion()` and the SQL
  function `sqlite_version()` must all report `3.8.3`
  (`/app/TEST_INTERFACE.md` §3.3).

`WITH RECURSIVE` and recursive common table expressions are **out of scope** and
are never exercised — `PROFILE.md` §2.5 lists them with the other exclusions.

Everything step 1 put in scope stays in scope and must keep working exactly as
it did: a sample of step 1's own cases for DDL and DML, the value model,
transactions and durability, the command-line contract and the C library must
keep working at this step.

`PROFILE.md` is **cumulative** — its step 1 section is still there, above the
step 2 section, and still binding.

**Hard prerequisite for step 3:** `make` succeeds and both a `WITH … SELECT`
query and a `VALUES` query return their rows, through `/app/build/sqlite3` *and*
through the static library. Steps 3-5 add JSON1 and expression indexes, UPSERT
and window functions on top of this engine.

---

## 2. Read these local references

`/app/sqlite-doc/` has been replaced with the SQLite **3.8.3** edition of every
page — 71 pages, about 1.5 MB. It is not the same corpus you (or step 1) read:
one page is new and 35 of the 70 you had differ.
`/app/sqlite-doc/README.md` says exactly which, and maps every topic to its
page.

Read these two first — they are short, and they bound everything else:

1. `/app/sqlite-doc/PROFILE.md` — **what is in scope and what is not.** Its
   step 2 section (§2.1-§2.5) is this step's whole assignment; its step 1
   section is what must keep working. Anything in neither its in-scope nor its
   out-of-scope list is out of scope.
2. `/app/TEST_INTERFACE.md` — the plumbing contract. §7 is new; §1, §2 and §4
   are unchanged from step 1, and §3.3 carries the one value that moves.

Then the normative upstream pages, in the order they pay off:

| Read | For |
| --- | --- |
| `lang_with.html` | **new at this release.** The `WITH` clause and common table expressions: the four statements a `WITH` may precede, several CTEs in one clause, the column-name list, "works as if it were a view", "exist only for the duration of a single SQL statement", and *Limitations And Caveats*. Skip its recursive examples — the hierarchy, graph, Mandelbrot and Sudoku queries are all `WITH RECURSIVE` and out of scope |
| `lang_select.html` sections *The VALUES clause* and *The WITH Clause* | the `VALUES` ≡ `SELECT … UNION ALL …` equivalence, and where a `WITH` clause may and may not appear in a compound select |
| `lang_select.html` sections *Compound Select Statements*, *The ORDER BY clause*, *The LIMIT clause* | operand width, and the two restrictions whose violations must be rejected: no `ORDER BY` and no `LIMIT` on a statement whose right-most element is a `VALUES` clause |
| `lang_corefunc.html`, the `printf(FORMAT,...)` row | the five stated rules. `likelihood()` and `unlikely()` on the same page are out of scope |
| `datatype3.html` §§1-3, §§5-6 | unchanged from step 1, and still the page every result depends on. Re-read only if step 1's value model gave you trouble |
| `c3ref/*.html` | unchanged. `printf()` is an SQL function, not a C entry point, so **no new declaration goes into `include/sqlite3.h`** |

The syntax diagrams on these pages are images and the images are **not** part of
this copy, so the grammar of `with-clause` and `cte-table-name` reaches you as
prose: `lang_with.html`'s text and `PROFILE.md` §2.1 carry it. The three pages
`lang_insert.html`, `lang_update.html` and `lang_delete.html` gain nothing for
`WITH` but a diagram label, so do not spend the budget there.

**Do not re-read the whole corpus.** 35 of the 70 inherited pages changed at
this release, but none of those edits *removes* a rule that is in scope, and
`PROFILE.md` §2.4 states that step 1's meaning is unchanged. The edits that do
touch in-scope text are small clarifications — `lang_corefunc.html` corrects the
integer bound quoted for `abs()` and rewords `coalesce()`, and
`lang_altertable.html` loses a sentence — none of which changes a behaviour in
this step's scope. `README.md` names the pages with the largest edits. Re-read a changed page only where your implementation of that
specific area is shaky. `capi3ref.html` is 420 KB of material you already have
one function at a time under `c3ref/`; leave it alone.

Also read `/app/WORKSPACE_HANDOFF.md` — how `/app` travels to the next step,
what you must not delete, and a **checkpoint you can run by hand**. Run it at
the start, before you change anything: it confirms the inherited build, the
CLI, the file header and the C library all still work, and on that first run its
last line still prints the *previous* release string, which is correct.

---

## 3. Meta requirements

- **Budget**: 7,200 seconds. `bash /app/timer.sh` shows the remaining time;
  the configured timeout is authoritative and resets each step. Most of it should go
  into `WITH` and `VALUES`, not into re-reading documentation you already have.
- **Cumulative workspace**: work in `/app`. Everything you leave there is what
  step 3 starts from, so keep it building at every point you might be
  interrupted. Do not delete or move `sqlite-doc/`, `TEST_INTERFACE.md`,
  `WORKSPACE_HANDOFF.md` or `timer.sh`. `build/` is not carried forward; the
  sources are.
- **Network**: This environment has no network access, and everything the
  build needs is already installed. The C toolchain and the whole build are
  local; nothing needs to be fetched. Do **not** search for, download, copy,
  wrap or invoke an online implementation or a task-specific answer.
- **This applies with full force to SQLite itself.** SQLite is public domain and
  its amalgamation, its source tree, its release patches and its test suite are
  all one download away. Vendoring, transcribing, or reconstructing upstream
  SQLite source — in whole or in part, by any route, including from memory — is
  prohibited and defeats the point of the task. The shipped documentation
  describes the *behaviour* you must reproduce; the implementation must be
  yours. This holds for the 3.8.0 → 3.8.3 difference exactly as it held for
  step 1's engine.
- **Off-limits**: do not inspect `/tests`, later step directories, hidden
  fixtures or solution material, and do not weaken or disable any check.
- **Handoff**: leave `/app` in a reproducible state — `make` works offline from
  the sources in the workspace, `make clean` removes `/app/build`, and the
  `WORKSPACE_HANDOFF.md` checkpoint passes.
