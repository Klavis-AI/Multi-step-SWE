# Step 1 — The core engine (SQLite 3.8.0)

## 1. Implement this milestone

Build a SQLite-compatible relational database in C, from an empty scaffold, in
`/app`. `make` must produce a static library `/app/build/libsqlite3.a` and a
command-line program `/app/build/sqlite3`, declared by `/app/include/sqlite3.h`.
The engine must store data in a file whose header matches the SQLite 3 database
header, and must implement the SQL surface that `/app/sqlite-doc/PROFILE.md`
puts in scope for this step: `CREATE`/`DROP TABLE` and `INDEX` (including
partial and `DESC` indexes), `ALTER TABLE … RENAME TO`/`ADD COLUMN`,
`INSERT`/`REPLACE`/`UPDATE`/`DELETE` with every `OR …` conflict form, `SELECT`
with joins, `GROUP BY`/`HAVING`, compound selects and `ORDER BY`/`LIMIT`, the
five storage classes with type affinity and NULL semantics, the core scalar and
aggregate functions, transactions and savepoints, the `sqlite_master` catalog,
the rollback-journal and WAL journal modes, and the listed `PRAGMA` statements.
The same behaviour must be reachable two ways: through the `sqlite3` program
and through the C library functions in `/app/TEST_INTERFACE.md` §3.

**Hard prerequisite for step 2:** `make` succeeds, and a database created by
`/app/build/sqlite3` can be written by one process and read back by the next —
both through the CLI and through the C library. Steps 2-5 add CTEs and
`VALUES`, JSON1 and expression indexes, UPSERT, and window functions **on top of
this engine**, so what you write here is the substrate for the rest of the task.

---

## 2. Read these local references

Read these two first — they are short, and they bound everything else:

1. `/app/sqlite-doc/PROFILE.md` — **what is in scope and what is not.** It is
   the task's boundary: anything in neither its in-scope nor its out-of-scope
   list is out of scope. It also fixes the result-set shapes of the
   introspection pragmas, which `pragma.html` does not state.
2. `/app/TEST_INTERFACE.md` — the plumbing contract: the `make` outputs, the
   CLI invocation grammar and output format, the exact C declarations
   `sqlite3.h` must carry, and the database-header bytes that are checked.

Then the normative upstream pages, in the order they pay off. Every `.html`
file under `/app/sqlite-doc/` is verbatim SQLite 3.8.0 documentation;
`/app/sqlite-doc/README.md` maps every topic to its page.

| Read | For |
| --- | --- |
| `datatype3.html` §§1-3, then §§5-6 | storage classes, type affinity, and how comparison applies it — the single highest-leverage page; almost every result depends on it. §5 covers sorting, grouping and compound selects, §6 the `BINARY`/`NOCASE`/`RTRIM` collations |
| `lang_select.html` §§1-4 | `FROM`/join processing, `WHERE`, `GROUP BY`/`HAVING`, compound selects, `ORDER BY`/`LIMIT` |
| `lang_expr.html` | operators and their precedence, `CAST`, `CASE`, `IN`, `LIKE`/`GLOB`, parameters |
| `lang_createtable.html` | column and table constraints, `INTEGER PRIMARY KEY` as the rowid, `CREATE TABLE … AS SELECT` |
| `nulls.html` | three-valued logic, which decides many otherwise-plausible answers |
| `fileformat2.html` **§1.2** | the database header fields — this is the only part of the 82 KB page you need for the header; **§2.5** describes `sqlite_master` and the normalisation applied to the `sql` column |
| `lang_corefunc.html`, `lang_aggfunc.html` | the exact behaviour of the functions `PROFILE.md` §4 lists |
| `pragma.html` | only the pragmas `PROFILE.md` §5 lists; the page is 8k words and most of it is out of scope |
| `c3ref/*.html` | one page per C function, named per row in `TEST_INTERFACE.md` §3.2. Prefer these to `capi3ref.html`, which is the same material combined into 420 KB |
| `lang_transaction.html`, `lang_savepoint.html`, `lang_conflict.html`, `wal.html` | transactions, savepoints, the five conflict algorithms, WAL |

`atomiccommit.html`, `lockingv3.html` and `optoverview.html` are background:
useful if you get stuck on durability or planning, never required.

Also read `/app/WORKSPACE_HANDOFF.md` — how `/app` travels to the next step,
what you must not delete, and a **checkpoint you can run by hand** to confirm
the build, the CLI, the file header and the C library all work end to end. Run
it before your time runs out.

---

## 3. Meta requirements

- **Budget**: 7,200 seconds. `bash /app/timer.sh` shows the remaining time;
  the configured timeout is authoritative and resets each step. The corpus in
  `/app/sqlite-doc/` is 70 pages and about 1.4 MB — read the two contract files
  and the sections named above, not the whole set.
- **Cumulative workspace**: work in `/app`. Everything you leave there is what
  step 2 starts from, so keep it building at every point you might be
  interrupted. Do not delete or move `sqlite-doc/`, `TEST_INTERFACE.md`,
  `WORKSPACE_HANDOFF.md` or `timer.sh`. `build/` is not carried forward; the
  sources are.
- **Network**: This environment has no network access, and everything the
  build needs is already installed. The C toolchain and the whole build are
  local; nothing needs to be fetched. Do **not** search for, download, copy,
  wrap or invoke an online implementation or a task-specific answer.
- **This applies with full force to SQLite itself.** SQLite is public domain
  and its amalgamation, its source tree and its test suite are all one download
  away. Vendoring, transcribing, or reconstructing upstream SQLite source — in
  whole or in part, by any route, including from memory — is prohibited and
  defeats the point of the task. The shipped documentation describes the
  *behaviour* you must reproduce; the implementation must be yours.
- **Off-limits**: do not inspect `/tests`, later step directories, hidden
  fixtures or solution material, and do not weaken or disable any check.
- **Handoff**: leave `/app` in a reproducible state — `make` works offline from
  the sources in the workspace, and the `WORKSPACE_HANDOFF.md` checkpoint
  passes.
