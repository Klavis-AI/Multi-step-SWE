# Step 3 — JSON1 and indexes on expressions (SQLite 3.9.0)

## 1. Implement this milestone

`/app` already holds the SQLite-compatible engine you delivered for steps 1 and
2, built by `make` into `/app/build/libsqlite3.a` and `/app/build/sqlite3`.
Continue that implementation in place — this step adds SQL surface to it, and
changes nothing else.

Move the engine from SQLite 3.8.3 to **SQLite 3.9.0** by adding exactly the two
capabilities `/app/sqlite-doc/PROFILE.md` §3 puts in scope, plus the release
string:

- **the eleven JSON1 scalar functions** (§3.1–§3.3) — `json`, `json_array`,
  `json_array_length`, `json_extract`, `json_insert`, `json_object`,
  `json_remove`, `json_replace`, `json_set`, `json_type` and `json_valid`, with
  the JSON path syntax (`$`, `.label`, `[index]`), the *JSON arguments*,
  *PATH arguments* and *VALUE arguments* rules that apply to all of them, and
  the four families of rejection §3.3 lists. A JSON document is an ordinary
  TEXT value, so these functions work in a `SELECT` list, a `WHERE` or
  `ORDER BY` term, an `INSERT`/`UPDATE` value, a common table expression and a
  `VALUES` clause, exactly like step 1's scalar functions. **§3.1 fixes how a
  produced document is written** — key order, minification, the decimal text of
  a number and string escaping — and every JSON result must match its expected
  text exactly, so read that bullet before you write the serialiser;
- **indexes on expressions** (§3.4) — `CREATE INDEX … ON t(expr)` with one
  expression, several, or expressions mixed with column names; `ASC`/`DESC` and
  `COLLATE` after an indexed expression; `UNIQUE`; `IF NOT EXISTS`; a partial
  index `WHERE` clause; `DROP INDEX`; the index appearing in `sqlite_master`
  with its `CREATE INDEX` text. Such an index changes **no answer** — it is
  maintained by `INSERT`, `UPDATE` and `DELETE`, survives rollback and
  `VACUUM`, and `PRAGMA integrity_check` still reports `ok`. §3.4 also lists
  five rejections that are required, including a non-deterministic function in an
  index key and a function call or bound parameter in a partial index's `WHERE`
  clause;
- **the release string**: `SQLITE_VERSION`, `sqlite3_libversion()` and the SQL
  function `sqlite_version()` must all report `3.9.0`
  (`/app/TEST_INTERFACE.md` §3.3).

Two things about this step are easy to get wrong, and both are written down:

- **The JSON functions must be there after a plain `make`.** `json1.html`
  describes json1 as a loadable extension that upstream's own amalgamation
  leaves switched off; that is a statement about upstream's build, not about
  this workspace. The functions must be callable from ordinary SQL, through
  both `/app/build/sqlite3` and `/app/build/libsqlite3.a`, without running
  `.load`, calling a load-extension entry point or passing an extra flag to
  `make`. `/app/TEST_INTERFACE.md` §7 states this.
- **`PRAGMA index_list` returns five columns at this release**, not the three
  `PROFILE.md` §5 fixed at step 1. `pragma.html` now enumerates them and
  `PROFILE.md` §3.5 supersedes §5's row from this step onward. §5's row says so
  as well, but only if you read that far.

`json_each()` and `json_tree()` — the two table-valued functions — are **out of
scope**, and so are `json_quote()`, `json_group_array()` and
`json_group_object()`, which do not exist at this release at all.
`PROFILE.md` §3.7 lists those and every other exclusion, including several
places where `json1.html` says more than this step requires; read it, because
it is what stops you spending the budget on out-of-scope behaviour.

Everything steps 1 and 2 put in scope stays in scope and must keep working
exactly as it did: a sample of DDL and DML, the value model, transactions and
durability, the command-line contract, the C library, and step 2's `WITH`
clause, `VALUES` queries and `printf()` must keep working at this step.

`PROFILE.md` is **cumulative** — its step 1 and step 2 sections are still there,
above the step 3 section, and still binding.

**Hard prerequisite for step 4:** `make` succeeds, `json_extract()` returns the
value a document holds, and an index on an expression can be created and then
queried — through `/app/build/sqlite3` *and* through the static library. Steps
4-5 add UPSERT and window functions on top of this engine.

---

## 2. Read these local references

`/app/sqlite-doc/` has been replaced with the SQLite **3.9.0** edition of every
page — 74 pages, about 1.7 MB, 27 of them under `c3ref/`. It is not the same
corpus you (or step 2) read: two pages are new for this step's features, two
more are new for other reasons, and 46 of the 70 you had differ.
`/app/sqlite-doc/README.md` says exactly which, and maps every topic to its
page.

Read these two first — they are short, and they bound everything else:

1. `/app/sqlite-doc/PROFILE.md` — **what is in scope and what is not.** Read its
   step 3 section, **§3.1 through §3.7, to the end**; §3.5 and §3.7 both carry
   requirements you cannot get anywhere else, and §3.5 in particular corrects a
   result shape that step 1's section still shows in its older form. Its step 1
   and step 2 sections are what must keep working. Anything in neither its
   in-scope nor its out-of-scope list is out of scope.
2. `/app/TEST_INTERFACE.md` — the plumbing contract. **§7 is new and is the one
   section that decides how you build**; §1, §2 and §4 are unchanged from step
   2, and §3.3 carries the one value that moves.

Then the normative upstream pages, in the order they pay off:

| Read | For |
| --- | --- |
| `json1.html` (33 KB) | **new at this release.** The eleven scalar functions, in nine sections (`json_insert`, `json_replace` and `json_set` share one), **with their worked examples — the examples are normative and your output must match them**; the *JSON arguments*, *PATH arguments* and *VALUE arguments* rules; and *Interface Overview*, which is why a JSON value is ordinary TEXT. Its own section numbers repeat in this edition (two 3.7s, two 3.9s, no 3.8), so refer to a section by title. Skip *The json_each() and json_tree() table-valued functions* — out of scope |
| `expridx.html` (9 KB) | **new at this release.** Indexes on expressions: the `account_change` worked example, *How To Use Indexes On Expressions*, and *Restrictions* — the four rules `PROFILE.md` §3.4 requires as rejections. The remarks about which expressions the planner can match are about speed, not results, and are never compared |
| `lang_createindex.html` | the index-key grammar in prose — "a parenthesized list of table column names and/or expressions", `ASC`/`DESC` and `COLLATE` after "each column name or expression", `UNIQUE`, `IF NOT EXISTS` — and the one-paragraph restatement of `expridx.html`'s restrictions, new at this release |
| `partialindex.html` | what a partial index's `WHERE` clause may **not** contain: subqueries, other tables' columns, functions, bound parameters, and therefore `LIKE`/`GLOB`/`MATCH`/`REGEXP`. Note the asymmetry — a function call is now allowed in the index **key** and still forbidden in the `WHERE` clause |
| `pragma.html`, the `index_list` and `index_info` entries | `index_list` now enumerates five output columns. `PROFILE.md` §3.5 records the shape; `index_info` is unchanged at three |
| `fileformat2.html` *Storage Of The SQL Database Schema* and *Internal Schema Objects* | unchanged from step 2, and still what fixes `sqlite_master` and the `sqlite_autoindex_TABLE_N` names |
| `c3ref/*.html` | unchanged. JSON1 is an **SQL** surface, not a C one, so **no new declaration goes into `include/sqlite3.h`** and no new entry point is needed |

**Do not re-read the whole corpus.** 46 of the 70 inherited pages changed at
this release, but nothing this step's `PROFILE.md` puts in scope was removed,
and §3.6 states that steps 1 and 2 keep their meaning. Only two inherited edits
matter and both are called out above and in `README.md`: `pragma.html`'s
`index_list` columns, and `datatype3.html` renaming the NONE type affinity to
BLOB (a rename only — the affinity rules did not change, and nothing in scope
compares an affinity's name). `README.md` names the pages with the largest
edits; re-read one only where your implementation of that specific area is
shaky. `capi3ref.html` is 450 KB of material you already have one function at a
time under `c3ref/`; leave it alone.

The syntax diagrams on these pages are images and the images are **not** part of
this copy, so grammar reaches you as prose: `lang_createindex.html`'s text,
`json1.html`'s function signatures and worked examples, and `PROFILE.md` §§3.1
and §3.4 carry it.

Also read `/app/WORKSPACE_HANDOFF.md` — how `/app` travels to the next step,
what you must not delete, and a **checkpoint you can run by hand**. Run it at
the start, before you change anything: it confirms the inherited build, the CLI,
the file header and the C library all still work, and on that first run its last
line still prints the *previous* release string, which is correct.

---

## 3. Meta requirements

- **Budget**: 7,200 seconds. `bash /app/timer.sh` shows the remaining time;
  the configured timeout is authoritative and resets each step. Most of it should go
  into the JSON1 functions and expression indexes, not into re-reading
  documentation you already have.
- **Cumulative workspace**: work in `/app`. Everything you leave there is what
  step 4 starts from, so keep it building at every point you might be
  interrupted. Do not delete or move `sqlite-doc/`, `TEST_INTERFACE.md`,
  `WORKSPACE_HANDOFF.md` or `timer.sh`. `build/` is not carried forward; the
  sources are.
- **Network**: This environment has no network access, and everything the
  build needs is already installed. The C toolchain and the whole build are
  local; nothing needs to be fetched. Do **not** search for, download, copy,
  wrap or invoke an online implementation or a task-specific answer.
- **This applies with full force to SQLite itself.** SQLite is public domain and
  its amalgamation, its source tree, its release patches and its test suite are
  all one download away — and so, specifically, is `json1.c`. Vendoring,
  transcribing, or reconstructing upstream SQLite source — in whole or in part,
  by any route, including from memory — is prohibited and defeats the point of
  the task. The shipped documentation describes the *behaviour* you must
  reproduce; the implementation must be yours. This holds for the 3.8.3 → 3.9.0
  difference exactly as it held for steps 1 and 2.
- **Off-limits**: do not inspect `/tests`, later step directories, hidden
  fixtures or solution material, and do not weaken or disable any check.
- **Handoff**: leave `/app` in a reproducible state — `make` works offline from
  the sources in the workspace, `make clean` removes `/app/build`, and the
  `WORKSPACE_HANDOFF.md` checkpoint passes.
