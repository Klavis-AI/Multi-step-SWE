# Step 5 — window functions and `RENAME COLUMN` (SQLite 3.25.0)

## 1. Implement this milestone

`/app` already holds the SQLite-compatible engine you delivered for steps 1, 2,
3 and 4, built by `make` into `/app/build/libsqlite3.a` and `/app/build/sqlite3`.
Continue that implementation in place — this step adds SQL surface to it, and
changes nothing else.

Move the engine from SQLite 3.24.0 to **SQLite 3.25.0** by adding exactly the
two capabilities `/app/sqlite-doc/PROFILE.md` §5 puts in scope, plus the release
string:

- **Window functions** (§5.1–§5.8) — the `OVER` clause in both its spellings
  (`OVER ( window-defn )` and `OVER window-name`), `PARTITION BY`, the window
  `ORDER BY` with its `ASC`/`DESC`/`COLLATE` ordering terms, `RANGE` and `ROWS`
  frame specifications with all five frame boundaries and the default frame,
  the **eleven built-in window functions** (`row_number`, `rank`, `dense_rank`,
  `percent_rank`, `cume_dist`, `ntile`, `lag`, `lead`, `first_value`,
  `last_value`, `nth_value`), **every step-1 §4 aggregate used as a window
  function**, the `FILTER (WHERE …)` clause, and the `WINDOW` clause that names
  a window definition. A window function may appear **only** in a `SELECT`'s
  result set and in that same `SELECT`'s `ORDER BY` — and in any `SELECT` steps
  1 and 2 put in scope: top-level, a CTE body, a `FROM` subquery, one arm of a
  compound select, and the `SELECT` feeding an `INSERT … SELECT`, upsert clause
  included. §5.11 lists the rejections that are required;
- **`ALTER TABLE … RENAME COLUMN`** (§5.9–§5.10) — `ALTER TABLE t RENAME COLUMN
  old TO new`. The column is known by its new name everywhere afterwards and by
  its old name nowhere; `PRAGMA table_info` reports the new name with every
  other column of that row unchanged; **no table content changes**; every index
  stays attached and in force; the statement takes part in transactions like any
  other. And it **rewrites the schema text**: §5.10 is the one place in this
  benchmark where the `sql` column of `sqlite_master` is compared;
- **the release string**: `SQLITE_VERSION`, `sqlite3_libversion()` and the SQL
  function `sqlite_version()` must all report `3.25.0`
  (`/app/TEST_INTERFACE.md` §3.3).

Three things about this step are easy to get wrong, and all three are written
down:

- **Two grammars are on no shipped page.** `windowfunctions.html` and
  `lang_altertable.html` both describe their syntax with diagrams, and the
  diagrams are images that are not part of this documentation copy. So
  `PROFILE.md` §5.1 writes out `window-function-invocation`, `window-defn` and
  `frame-spec` in text, and §5.9 writes out the `ALTER TABLE … RENAME COLUMN`
  statement form. Read both before you touch the parser. §5.6 likewise carries
  the `WINDOW` clause grammar, because **`lang_select.html` does not document
  the `WINDOW` clause** at this release — the clause is in its syntax diagram
  and nowhere in its prose.
- **Only values the documents fix are compared.** The page orders rows
  *within* a partition through the window `ORDER BY` alone, and calls rows with
  equal `ORDER BY` values **peers**; the order among peers is fixed by nothing.
  `PROFILE.md` §5.8 states what follows, and it is a promise to you rather than
  a requirement on you: wherever a compared value depends on a row's position
  inside a peer group, the query's window `ORDER BY` totally orders the
  partition; where peers are exercised deliberately, only peer-invariant
  values are compared; the output order of every query in scope is fixed by a
  total `ORDER BY` on the outer `SELECT`, never by a window `ORDER BY`; and an
  unaliased window result column's *name* is never compared.
- **`PROFILE.md` §5.13 is a long exclusion list and it is worth the two minutes
  it costs.** `GROUPS` frames and the `EXCLUDE` clause do **not** exist at this
  release — `lang_keywords.html`'s 136-keyword list contains neither word — and
  are out of scope in both directions. So are: `LIMIT`/`OFFSET` on a query that
  carries a window function; `FILTER` without `OVER`; an undefined or duplicated
  window name; `ntile(N)` for non-positive `N` and a negative `lag`/`lead`
  offset; a window function inside another window function's arguments; a window
  `ORDER BY` naming a result alias; user-defined aggregate window functions
  (`sqlite3_create_window_function()`); a frame whose two boundaries are both
  `UNBOUNDED PRECEDING` or both `UNBOUNDED FOLLOWING`; and
  the concatenation order of `group_concat()` over a frame the window `ORDER BY`
  does not totally order. `RENAME COLUMN` has its own five exclusions in the
  same section — the `COLUMN` keyword omitted, a quoted/keyword/non-ASCII column
  name, a trigger or view referencing the column, `ALTER TABLE` on an
  `sqlite_`-prefixed object, and a column name that appears inside a string
  literal in an index expression. None of these fifteen is required either way,
  so spending budget on them has no effect.

Window functions and `RENAME COLUMN` are **SQL syntax, not C entry points**: no
new declaration goes into `include/sqlite3.h`, and `TEST_INTERFACE.md` §3.2's
list is already complete. `sqlite3_create_window_function()`, which
`windowfunctions.html` documents, and `sqlite3_keyword_count()` /
`sqlite3_keyword_name()` / `sqlite3_keyword_check()`, which `lang_keywords.html`
now mentions, are not part of the interface and none of them is ever called.

Everything steps 1, 2, 3 and 4 put in scope stays in scope and must keep working
exactly as it did — this step supersedes nothing. Step 3 §3.5's `PRAGMA
index_list` shape and step 4 §4.6's partial-index `WHERE` rule are both still in
force and are still correct against the pages shipped beside this file. Because
this is the last step, the sample of earlier behaviour that must keep working
is the widest of any step: DDL and DML, the value model, transactions and
durability, the command-line contract, the C library, step 2's `WITH` clause,
`VALUES` queries and `printf()`, step 3's JSON1 functions and expression
indexes, and step 4's UPSERT.

`PROFILE.md` is **cumulative** — its step 1, 2, 3 and 4 sections are still
there, above the step 5 section, and still binding. `make clean` is one of them:
it remains a requirement of the workspace even though, as §5.12 discloses, it is
out of scope at this step.

**The gate for this step** — `TEST_INTERFACE.md` §7 — is proof of life through
this step's new capability at both boundaries: one `row_number() OVER (ORDER BY
…)` that numbers rows in the window's order while the output order is fixed by
the statement's own `ORDER BY`, and one `ALTER TABLE … RENAME COLUMN` whose new
column name a following query uses — once through `/app/build/sqlite3` and once
through the static library. The gate also carries the inherited sample — one
path through each of steps 1, 2, 3 and 4.

---

## 2. Read these local references

`/app/sqlite-doc/` has been replaced with the SQLite **3.25.0** edition of every
page — 76 pages, about 2.2 MB, 27 of them under `c3ref/`. The gap between this
release and the last one is **103 days** with no release in between, so the
inherited set barely moved: of the 75 pages you already had, 40 are
byte-for-byte identical, 21 changed without a word of prose changing — sixteen
only in the syntax-diagram show/hide controls, five in those plus a renamed
anchor, a search-index entry or a moved cross-reference — and **14 differ in
prose**. `/app/sqlite-doc/README.md` names all fourteen and says what changed in
each, and maps every topic to its page.

Read these two first — they are short, and they bound everything else:

1. `/app/sqlite-doc/PROFILE.md` — **what is in scope and what is not.** Read its
   step 5 section, **§5.1 through §5.13, to the end**. §5.1 and §5.9 carry the
   grammars that are on no page, §5.3 the frame boundaries and the default
   frame, §5.4 the eleven built-ins, §5.8 the determinism promise above, §5.10
   the `sqlite_master.sql` rule, §5.11 the required rejections and §5.13 the
   exclusion list. Its step 1, 2, 3 and 4 sections are what must keep working.
   Anything in neither its in-scope nor its out-of-scope list is out of scope.
2. `/app/TEST_INTERFACE.md` — the plumbing contract. **§7 is new**; §1, §2, §4,
   §5 and §6 are unchanged from step 4, and §3.3 carries the one value that
   moves. §7 also says how a windowed `SELECT` and a `RENAME COLUMN` behave at
   the C boundary, and where bound parameters appear in a windowed query (and where they
   never do).

Then the normative upstream pages, in the order they pay off:

| Read | For |
| --- | --- |
| `windowfunctions.html` | **new at this release, and the normative page for §§5.1–5.8.** What a window function is and that the `OVER` clause is what makes one; `PARTITION BY`; that the window `ORDER BY` does *not* fix the query's output order; peers; the frame-spec's three parts, the default frame, the omitted end bound and the five boundaries with the ordering constraint between them; the eleven built-ins one at a time; that all of SQLite's aggregate functions may be used as aggregate window functions; the `FILTER` clause; the `WINDOW` clause and its position. **Its worked examples are normative and your output must match them**, with a total outer `ORDER BY` added where the page omitted one — the `t0` `row_number` example, the three frame examples over `t1`, the six-row ranking example, the `lead`/`lag`/`first_value`/`last_value`/`nth_value` example, the `FILTER` example and the `win1`/`win2` `WINDOW` example. Two columns are carved out and `PROFILE.md` says which and why: the six-row example's `cume_dist` (rounded on the page) and `row_number` (peer-order-dependent) columns, in §5.4, and one example's `group_concat` column, in §5.13. The section on **user-defined** aggregate window functions is out of scope (§5.13) |
| `lang_altertable.html` | **the other page you must re-read.** It now documents `RENAME COLUMN`, and it is the page that says `ALTER TABLE` "works by modifing the SQL text of the schema stored in the sqlite_master table. No changes are made to table content", and that the column name "is changed both within the table definition itself and also within all indexes … that reference the column". Its three `RENAME TO` paragraphs were replaced at this release; `RENAME TO` and `ADD COLUMN` themselves are unchanged from step 1 |
| `fileformat2.html`, *Storage Of The SQL Database Schema* | what `sqlite_master.sql` holds, the five normalisations, and that the text is "as modified by subsequent ALTER TABLE statements" — the second half of §5.10's rule. **Note the section moved**: a new §1.1 *Hot Journals* pushed *The Database Header* down, so cite by title, not by a number you remember from step 1 |
| `lang_aggfunc.html` | unchanged in prose at this release. Its functions are exactly what `windowfunctions.html` means by "All of SQLite's aggregate functions"; step 1 §4's closed list is the set you must support with `OVER` |
| `lang_select.html` | unchanged in substance, and still the page that fixes where a `SELECT`'s clauses go and in what order they are processed — which is what settles `GROUP BY`/`HAVING` against a window function (§5.7). It does **not** document the `WINDOW` clause; `PROFILE.md` §5.6 does |
| `lang_keywords.html` | the keyword list grew from 124 to 136. **All twelve** additions are step-4 or step-5 syntax — ten of this step's own (`CURRENT`, `FILTER`, `FOLLOWING`, `OVER`, `PARTITION`, `PRECEDING`, `RANGE`, `ROWS`, `UNBOUNDED`, `WINDOW`) and `DO` and `NOTHING` for step 4's UPSERT, whose keyword-list entry lagged a release. **Neither `GROUPS` nor `EXCLUDE` is in the list** — that is the evidence behind §5.13's first exclusion. The three `sqlite3_keyword_*` interfaces it mentions are not part of `TEST_INTERFACE.md` §3.2 |
| `pragma.html`, the `table_info` and `integrity_check` entries | unchanged. `PROFILE.md` step 1 §5 fixes the result shapes, which §5.9 leans on for what a rename does and does not change |
| `lang_UPSERT.html`, `partialindex.html`, `expridx.html`, `json1.html` | inherited and unchanged in substance. `lang_UPSERT.html` gained a *Limitations* section about virtual tables, which were already out of scope; `partialindex.html` did **not** change, so §4.6's reworded rule from step 4 still matches the page beside you; `json1.html` has two typographical fixes only |
| `c3ref/*.html` | four pages were reworded — `changes.html`, `total_changes.html`, `errcode.html` and `column_blob.html` — none of it changing what `TEST_INTERFACE.md` §3.2 requires or what §6 puts out of scope. `capi3ref.html` is 549 KB of material you already have one function at a time under `c3ref/`; **do not read it end to end** |

**Do not re-read the whole corpus.** Only fourteen inherited pages differ in
prose and §5.12 lists the three of those changes a reader might mistake for a
change of substance. `README.md` names all fourteen; re-read one only where your
implementation of that specific area is shaky.

Also read `/app/WORKSPACE_HANDOFF.md` — how `/app` is collected, what you must
not delete, and a **checkpoint you can run by hand**. Run it at the start,
before you change anything: it confirms the inherited build, the CLI, the file
header and the C library all still work, and on that first run its last line
still prints the *previous* release string, which is correct.

---

## 3. Meta requirements

- **Budget**: 7,200 seconds. `bash /app/timer.sh` shows the remaining time;
  the configured timeout is authoritative and resets each step. Most of it should go
  into the window-function execution path, not into re-reading documentation you
  already have.
- **Cumulative workspace**: work in `/app`. This is the last step, so nothing
  starts from what you leave — but keep it building at every point you might be
  interrupted, because `make` is run on a copy of `/app` exactly as
  you left it. Do not delete or move `sqlite-doc/`, `TEST_INTERFACE.md`,
  `WORKSPACE_HANDOFF.md` or `timer.sh`. `build/` is not collected; the sources
  are.
- **Network**: This environment has no network access, and everything the
  build needs is already installed. The C toolchain and the whole build are
  local; nothing needs to be fetched. Do **not** search for, download, copy,
  wrap or invoke an online implementation or a task-specific answer.
- **This applies with full force to SQLite itself.** SQLite is public domain and
  its amalgamation, its source tree, its release patches and its test suite are
  all one download away — and the 3.24.0 → 3.25.0 difference, the commit that
  added window functions and `RENAME COLUMN` included, is a published diff.
  Vendoring, transcribing, or reconstructing upstream SQLite source — in whole
  or in part, by any route, including from memory — is prohibited and defeats
  the point of the task. The shipped documentation describes the *behaviour* you
  must reproduce; the implementation must be yours. This holds for window
  functions exactly as it held for steps 1, 2, 3 and 4.
- **Off-limits**: do not inspect `/tests`, other step directories, hidden
  fixtures or solution material, and do not weaken or disable any check.
- **Handoff**: leave `/app` in a reproducible state — `make` works offline from
  the sources in the workspace, `make clean` removes `/app/build`, and the
  `WORKSPACE_HANDOFF.md` checkpoint passes with `3.25.0` on its last line.
