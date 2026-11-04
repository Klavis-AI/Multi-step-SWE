# Step 4 — UPSERT (SQLite 3.24.0)

## 1. Implement this milestone

`/app` already holds the SQLite-compatible engine you delivered for steps 1, 2
and 3, built by `make` into `/app/build/libsqlite3.a` and `/app/build/sqlite3`.
Continue that implementation in place — this step adds SQL surface to it, and
changes nothing else.

Move the engine from SQLite 3.9.0 to **SQLite 3.24.0** by adding exactly the one
capability `/app/sqlite-doc/PROFILE.md` §4 puts in scope, honouring one
inherited rule that upstream reworded at this release, plus the release string:

- **UPSERT** (§4.1–§4.5, §4.7) — the trailing `ON CONFLICT` clause on an
  ordinary `INSERT`, in the two shapes §4.1 writes out:
  `ON CONFLICT [ (target) [WHERE expr] ] DO NOTHING` and
  `ON CONFLICT (target) [WHERE expr] DO UPDATE SET col=expr[, …] [WHERE expr]`.
  The **conflict target** identifies one uniqueness constraint — an
  `INTEGER PRIMARY KEY` or other `PRIMARY KEY`, a `UNIQUE` column or table
  constraint, a `CREATE UNIQUE INDEX`, several columns in either order, an
  index on an expression (step 3 §3.4), or, with its own `WHERE` clause, a
  **partial** unique index. `DO NOTHING` skips the colliding row and inserts
  the rest; without a target it means what `INSERT OR IGNORE` means.
  `DO UPDATE` updates the single conflicting row, reading its **original**
  values through an unqualified, table-qualified or alias-qualified name and
  the row that would have been inserted through `excluded.`, and its trailing
  `WHERE` clause turns the update into a no-op for that row without failing the
  statement. §4.5 says how the clause divides work with the inherited
  `INSERT OR …` algorithms, and §4.7 lists the six families of rejection that
  are required;
- **the reworded partial-index `WHERE` rule** (§4.6) — see the second warning
  below;
- **the release string**: `SQLITE_VERSION`, `sqlite3_libversion()` and the SQL
  function `sqlite_version()` must all report `3.24.0`
  (`/app/TEST_INTERFACE.md` §3.3).

Two things about this step are easy to get wrong, and both are written down:

- **The upsert grammar is on no shipped page.** `lang_UPSERT.html` describes
  UPSERT in prose and three worked examples, but its syntax diagram is an image
  and the images are not part of this documentation copy. The prose never says
  that a conflict target may carry **its own `WHERE` clause**, nor that
  `DO UPDATE SET` may be followed by one. `PROFILE.md` §4.1 writes the whole
  clause out in text; read it before you touch the parser, because the
  target's `WHERE` clause is what makes a partial unique index reachable at
  all. §4.1 also fixes where the `AS alias` phrase sits in the `INSERT` header.
- **`partialindex.html` changed a rule that was in force at step 3.** Its
  `WHERE`-clause restriction now reads "may not contain subqueries, references
  to other tables, **non-deterministic** functions, or bound parameters", and
  the paragraph that also barred `LIKE`, `GLOB`, `MATCH` and `REGEXP` is gone.
  So from this step onward a *deterministic* function call, and `LIKE` and
  `GLOB`, are **legal** in a partial index's `WHERE` clause and must work;
  `random()`, `sqlite_version()`, a subquery, another table's column and a
  bound parameter are still rejected. `PROFILE.md` §4.6 states the new rule and
  says in as many words that it supersedes step 3 §3.4's fifth rejection.

UPSERT is SQL syntax, not a C entry point: **no new declaration goes into
`include/sqlite3.h`**, and `TEST_INTERFACE.md` §3.2's list is already complete.
`sqlite3_set_last_insert_rowid()`, which `c3ref/last_insert_rowid.html` mentions
at this release, is not part of the interface and is never called.

`changes()`, `total_changes()` and `last_insert_rowid()` are **never read after
a statement that carried an upsert clause** — two shipped pages disagree about
what they should say, so it is out of scope either way. That, the ambiguous
`INSERT … SELECT … ON CONFLICT` written without a `WHERE` clause, the keyword
literals `TRUE` and `FALSE`, `ON CONFLICT(rowid)`, more than one `ON CONFLICT`
clause on one statement, and the `REPLACE INTO … ON CONFLICT …` spelling are all
listed in `PROFILE.md` §4.9 with everything else this step leaves out. Read it,
because it is what stops you spending the budget on out-of-scope behaviour.

Everything steps 1, 2 and 3 put in scope stays in scope and must keep working
exactly as it did — with the single exception §4.6 states. A sample of DDL and
DML, the value model, transactions and durability, the command-line contract,
the C library, step 2's `WITH` clause, `VALUES` queries and `printf()`, and step
3's JSON1 functions and expression indexes must keep working at this step.

`PROFILE.md` is **cumulative** — its step 1, 2 and 3 sections are still there,
above the step 4 section, and still binding.

**Hard prerequisite for step 5:** `make` succeeds and an
`INSERT … ON CONFLICT (target) DO UPDATE SET …` updates the row it collides
with, leaves the other rows alone and inserts a non-colliding row — through
`/app/build/sqlite3` *and* through the static library. Step 5 adds window
functions on top of this engine.

---

## 2. Read these local references

`/app/sqlite-doc/` has been replaced with the SQLite **3.24.0** edition of every
page — 75 pages, about 1.8 MB, 27 of them under `c3ref/`. The gap between this
release and the last one is two years and eight months and fifteen minor
releases: one page is new, and 49 of the 74 you had differ in prose.
`/app/sqlite-doc/README.md` says exactly which, and maps every topic to its
page.

Read these two first — they are short, and they bound everything else:

1. `/app/sqlite-doc/PROFILE.md` — **what is in scope and what is not.** Read its
   step 4 section, **§4.1 through §4.9, to the end**: §4.1 carries the grammar
   that is on no page, §4.6 overturns a rule that was in force at step 3, and
   §4.9 is the exclusion list. Its step 1, 2 and 3 sections are what must keep
   working. Anything in neither its in-scope nor its out-of-scope list is out of
   scope.
2. `/app/TEST_INTERFACE.md` — the plumbing contract. **§7 is new**; §1, §2 and
   §4 are unchanged from step 3, and §3.3 carries the one value that moves.

Then the normative upstream pages, in the order they pay off:

| Read | For |
| --- | --- |
| `lang_UPSERT.html` | **new at this release** — and **note the capitalised file name**, the only one in this directory. Nine paragraphs and three worked examples: what UPSERT is, the conflict target and that it is "required for DO UPDATE upserts", the per-row decision in a multi-row insert, `DO NOTHING` ≡ `INSERT OR IGNORE` without a target, the `excluded.` qualifier, that `DO UPDATE` "acts only on the single row", and the trailing `WHERE`. **The three examples — `vocabulary`, `phonebook`, `phonebook2` — are normative and your output must match them.** The syntax diagram is an image and is not here; `PROFILE.md` §4.1 has the grammar |
| `lang_insert.html`, the paragraphs following its syntax diagram | the three UPSERT rules that live on this page rather than on `lang_UPSERT.html`: the `AS alias` phrase, the parsing ambiguity that makes a `WHERE` clause necessary on `INSERT … SELECT … ON CONFLICT …`, and the two sentences forbidding an upsert clause after `DEFAULT VALUES`. Also still the page that fixes what a column outside the column list is populated with, which is what `excluded.` reports for it |
| `lang_conflict.html` | the five `INSERT OR …` algorithms and "If no algorithm is specified anywhere, the ABORT algorithm is used" — the inherited mechanism `PROFILE.md` §4.5 composes with the upsert clause. New at this release is its warning **not to confuse** the `ON CONFLICT` *clause* this page documents with the `ON CONFLICT` phrase inside an UPSERT; they are different things |
| `partialindex.html`, *Creating Partial Indexes* and *Unique Partial Indexes* | the reworded `WHERE`-clause restriction, and the `team_leader` example that is exactly the partial unique index a conflict target identifies by repeating its `WHERE` clause |
| `lang_createindex.html`, `expridx.html` | unchanged in substance. The index-key grammar — "a parenthesized list of table column names and/or expressions" — is what a conflict target may name, because the target names the constraint's key |
| `pragma.html`, the `index_list` entry | unchanged from step 3: five output columns, `partial` among them. `PROFILE.md` §3.5 records the shape |
| `json1.html` | **renumbered its sections** at this release — *Interface Overview* is §3 here and was §2.0 — and now documents fifteen functions where step 3's edition documented eleven. The eleven of `PROFILE.md` §3.1 are still the closed list; the extras are out of scope |
| `c3ref/*.html` | UPSERT is an **SQL** surface, so no new entry point is needed. `c3ref/last_insert_rowid.html` gained a mention of `sqlite3_set_last_insert_rowid()`; it is not in `TEST_INTERFACE.md` §3.2's list and is not required |

**Do not re-read the whole corpus.** 49 of the 74 inherited pages differ in
prose across those fifteen releases, but nothing this step's `PROFILE.md` puts
in scope was removed, and §4.8 states that steps 1, 2 and 3 keep their meaning with
the single exception §4.6 makes. Four inherited edits matter and all four are
called out above and in `README.md`: `partialindex.html`'s reworded rule,
`json1.html`'s renumbering and its four extra functions,
`lang_update.html`/`lang_vacuum.html`'s new syntax (both out of scope, §4.8),
and `c3ref/last_insert_rowid.html`'s new mention. `README.md` names the pages
with the largest edits; re-read one only where your implementation of that
specific area is shaky. `capi3ref.html` is 542 KB of material you already have
one function at a time under `c3ref/`; leave it alone.

The syntax diagrams on these pages are images and the images are **not** part of
this copy. That matters more at this step than at any earlier one, because the
upsert clause's grammar appears on `lang_UPSERT.html` only in the diagram:
`PROFILE.md` §4.1 writes it out in text, and §§4.2–4.7 state the parts in scope
for this step.

Also read `/app/WORKSPACE_HANDOFF.md` — how `/app` travels to the next step,
what you must not delete, and a **checkpoint you can run by hand**. Run it at
the start, before you change anything: it confirms the inherited build, the CLI,
the file header and the C library all still work, and on that first run its last
line still prints the *previous* release string, which is correct.

---

## 3. Meta requirements

- **Budget**: 7,200 seconds. `bash /app/timer.sh` shows the remaining time;
  the configured timeout is authoritative and resets each step. Most of it should go
  into the upsert clause, not into re-reading documentation you already have.
- **Cumulative workspace**: work in `/app`. Everything you leave there is what
  step 5 starts from, so keep it building at every point you might be
  interrupted. Do not delete or move `sqlite-doc/`, `TEST_INTERFACE.md`,
  `WORKSPACE_HANDOFF.md` or `timer.sh`. `build/` is not carried forward; the
  sources are.
- **Network**: This environment has no network access, and everything the
  build needs is already installed. The C toolchain and the whole build are
  local; nothing needs to be fetched. Do **not** search for, download, copy,
  wrap or invoke an online implementation or a task-specific answer.
- **This applies with full force to SQLite itself.** SQLite is public domain and
  its amalgamation, its source tree, its release patches and its test suite are
  all one download away — and the 3.9.0 → 3.24.0 difference, the commit that
  added UPSERT included, is a published diff. Vendoring, transcribing, or
  reconstructing upstream SQLite source — in whole or in part, by any route,
  including from memory — is prohibited and defeats the point of the task. The
  shipped documentation describes the *behaviour* you must reproduce; the
  implementation must be yours. This holds for UPSERT exactly as it held for
  steps 1, 2 and 3.
- **Off-limits**: do not inspect `/tests`, later step directories, hidden
  fixtures or solution material, and do not weaken or disable any check.
- **Handoff**: leave `/app` in a reproducible state — `make` works offline from
  the sources in the workspace, `make clean` removes `/app/build`, and the
  `WORKSPACE_HANDOFF.md` checkpoint passes.
