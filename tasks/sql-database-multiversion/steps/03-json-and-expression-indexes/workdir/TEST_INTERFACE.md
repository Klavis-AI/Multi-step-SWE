# TEST_INTERFACE — step 03 of 5 (SQLite 3.9.0)

This is the public contract the hidden verifier holds you to. Everything the
verifier needs — a name, a path, a signature, an output format, an exit status,
a build command — is stated here or in a page of `/app/sqlite-doc/` that
`/app/sqlite-doc/README.md` points at. The verifier may combine these
interfaces in ways this file does not enumerate; it will not use an interface
this file does not name.

Semantics come from the upstream documentation in `/app/sqlite-doc/`, which is
copied verbatim from the official SQLite 3.9.0 documentation archive and
is in the public domain. `/app/sqlite-doc/PROFILE.md` says which parts of that
documentation are in scope for this step. This file describes the *plumbing*:
how the verifier reaches your implementation.

## 1. Workspace layout and build

```text
/app/
  Makefile          the build entry point (yours to edit; contract below)
  include/sqlite3.h the public C header
  src/              library translation units
  shell/            command-line shell translation units
  build/            build output (created by make; never committed)
  sqlite-doc/       upstream documentation, PROFILE.md, README.md
  TEST_INTERFACE.md this file
  WORKSPACE_HANDOFF.md
  timer.sh
```

* The implementation language is **C**, compiled by the verifier with GCC 12 on
  Debian bookworm (`gcc`, `make`, `ar`, `ld` from `binutils`). The default
  standard used by the shipped `Makefile` is `-std=c99`; you may change the
  standard, the optimisation level and the warning flags.
* **`make` run in `/app` with no arguments must succeed and must produce
  exactly these two files:**
  * `/app/build/libsqlite3.a` — a static archive containing the library.
  * `/app/build/sqlite3` — the command-line executable of section 2.
* **`make clean` must succeed and must remove the `/app/build` directory
  itself**, not merely the files inside it.
* The build must work **offline**, from the sources in `/app` alone. No
  network, no package installation, no code generation that needs a tool the
  image does not have. Only a POSIX C toolchain, GNU make and `ar` are
  available at measurement time.
* The verifier allows `make` **15 minutes**, and each case **2 minutes**
  (**4 minutes** for a case that drives the program several times over). These
  are ceilings against a hang, not a performance target — see section 6 — and
  every case is small enough that a straightforward implementation finishes in
  seconds.
* The verifier deletes `/app/build` before building, so nothing you leave there
  is used. Build products are excluded from the workspace snapshot between
  steps; **the sources are what carries forward.**
* You may add, rename and split files under `src/` and `shell/` freely, and you
  may rewrite the `Makefile`, as long as `make` keeps producing those two
  outputs from the sources in the workspace. Internal file and symbol layout is
  out of scope.
* A verifier C program is compiled roughly like this, so both must work:

  ```sh
  cc -I/app/include driver.c /app/build/libsqlite3.a -o driver -lm -lpthread -ldl
  ```

  `libsqlite3.a` must therefore be self-contained apart from the C runtime,
  `libm`, `libpthread` and `libdl`.

## 2. The `sqlite3` command-line program

`/app/build/sqlite3` implements the shell described in
`/app/sqlite-doc/cli.html`. The verifier uses only the following surface.

### 2.1 Invocation

```text
sqlite3 [OPTIONS] [FILENAME [SQL]]
```

* `FILENAME` names the database file to open, creating it if it does not exist.
  A missing `FILENAME`, or the literal `:memory:`, opens a transient in-memory
  database that leaves no file behind.
* **At most one `SQL` argument** may follow `FILENAME`. When it is present it
  is executed — it may hold several `;`-separated statements — and the program
  then exits without reading standard input. The verifier never passes more
  than one.
* With no `SQL` argument, statements are read from **standard input** until end
  of file and executed in order.
* Statements are separated by `;`. A statement read from **standard input**
  that is still unterminated when end of file is reached is not executed. (A
  single `SQL` argument need not end in `;`: it is a complete command line,
  not a stream.) `cli.html` says only what an *interactive* shell does with
  a missing semicolon — it "will give you a continuation prompt" — and says
  nothing either way about a non-interactive stream that simply ends, so this
  is a rule **this contract** imposes rather than a report of any other
  implementation's behaviour. Only one case in the whole benchmark
  observes it, in step 1's command-line group; no later step writes an
  unterminated trailing statement.
* The verifier uses no command-line option outside the three below, and defines
  each here because `cli.html` describes the shell but does not enumerate
  its command-line options. Not every step's cases reach all three:
  * `-init FILE` — read start-up commands from `FILE` instead of from any
    default start-up file. Each line of `FILE` is processed as if it had been
    typed before the input, so a `FILE` of `/dev/null` means "no start-up
    commands at all". **The verifier always passes `-init /dev/null`** unless a
    case is specifically exercising `-init`, so no start-up file in the image
    or the home directory can influence a measured run.
  * `-batch` — force non-interactive behaviour: no prompt, no banner.
  * `-cmd COMMAND` — run the single dot command `COMMAND` after the start-up
    file and before the input is processed. `COMMAND` is one argument.
* An unrecognised option, or a second `SQL` argument, may be rejected; the
  verifier never relies on that.

### 2.2 Dot commands

Only these are used by the verifier, and only in these forms:

| Command | Meaning |
| --- | --- |
| `.mode list` | one output line per result row, columns joined by the separator |
| `.headers on` / `.headers off` | emit, or do not emit, one leading line of column names |
| `.separator STRING` | set the column separator used by `list` mode |
| `.nullvalue STRING` | the text substituted for a NULL result value |
| `.bail on` / `.bail off` | stop, or do not stop, after the first error |
| `.quit` | stop reading input and exit |

Defaults at start-up must be `list` mode, headers off, separator `|`, null
value the empty string, and `.bail off`. Any other dot command may exist or
not; none is in scope.

### 2.3 Output and exit status

* Query results go to **standard output**. Diagnostics go to **standard
  error**. An in-scope case compares standard output byte for byte, so nothing
  else — no banner, no prompt, no timing line — may reach standard output in a
  non-interactive run.
* In `list` mode a row is the column values joined by the separator, followed
  by a newline. A NULL column is rendered as the `.nullvalue` string. With
  `.headers on` the first line of each result set is the column names joined
  the same way.
* Text values are written as their characters, BLOB values as their raw bytes,
  INTEGER values in decimal.
* REAL values are written in decimal with **at most 15 significant digits,
  trailing fractional zeros removed, and always at least one digit after the
  decimal point**, so the text is never mistakable for an INTEGER: `5.0` (not
  `5`), `0.5`, `-3.0`, `2.25`, `0.333333333333333`. A magnitude too large or
  too small for that form may be written in exponential notation; the verifier
  never compares one.
* **Exit status is `0` when every statement executed successfully and non-zero
  when any statement failed.** The particular non-zero value is *out of scope*.
* Error **message wording is out of scope** anywhere in this task. Negative cases
  assert that a statement is rejected and that the observable database state is
  unchanged, never what the message says.

## 3. The C library interface

`/app/include/sqlite3.h` must declare at least the following, with these
spellings and signatures. Each is documented on the page named in the last
column, under `/app/sqlite-doc/`. The verifier compiles ordinary C programs
against this header and links them against `/app/build/libsqlite3.a`.

### 3.1 Types

| Declaration | Documented at |
| --- | --- |
| `typedef struct sqlite3 sqlite3;` (opaque connection handle) | `c3ref/sqlite3.html` |
| `typedef struct sqlite3_stmt sqlite3_stmt;` (opaque prepared statement) | `c3ref/stmt.html` |
| `sqlite3_int64`, `sqlite3_uint64` — 64-bit signed and unsigned integers | `c3ref/int64.html` |
| `typedef void (*sqlite3_destructor_type)(void*);` | `c3ref/c_static.html` |

### 3.2 Functions

| Signature | Documented at |
| --- | --- |
| `int sqlite3_open(const char *filename, sqlite3 **ppDb);` | `c3ref/open.html` |
| `int sqlite3_close(sqlite3 *db);` | `c3ref/close.html` |
| `int sqlite3_exec(sqlite3 *db, const char *sql, int (*callback)(void*,int,char**,char**), void *arg, char **errmsg);` | `c3ref/exec.html` |
| `void sqlite3_free(void *p);` | `c3ref/free.html` |
| `int sqlite3_prepare_v2(sqlite3 *db, const char *zSql, int nByte, sqlite3_stmt **ppStmt, const char **pzTail);` | `c3ref/prepare.html` |
| `int sqlite3_step(sqlite3_stmt *stmt);` | `c3ref/step.html` |
| `int sqlite3_reset(sqlite3_stmt *stmt);` | `c3ref/reset.html` |
| `int sqlite3_finalize(sqlite3_stmt *stmt);` | `c3ref/finalize.html` |
| `int sqlite3_bind_null(sqlite3_stmt*, int);` | `c3ref/bind_blob.html` |
| `int sqlite3_bind_int(sqlite3_stmt*, int, int);` | `c3ref/bind_blob.html` |
| `int sqlite3_bind_int64(sqlite3_stmt*, int, sqlite3_int64);` | `c3ref/bind_blob.html` |
| `int sqlite3_bind_double(sqlite3_stmt*, int, double);` | `c3ref/bind_blob.html` |
| `int sqlite3_bind_text(sqlite3_stmt*, int, const char*, int, void(*)(void*));` | `c3ref/bind_blob.html` |
| `int sqlite3_bind_blob(sqlite3_stmt*, int, const void*, int, void(*)(void*));` | `c3ref/bind_blob.html` |
| `int sqlite3_bind_parameter_index(sqlite3_stmt*, const char *zName);` | `c3ref/bind_parameter_index.html` |
| `int sqlite3_column_count(sqlite3_stmt*);` | `c3ref/column_count.html` |
| `int sqlite3_data_count(sqlite3_stmt*);` | `c3ref/data_count.html` |
| `const char *sqlite3_column_name(sqlite3_stmt*, int N);` | `c3ref/column_name.html` |
| `const char *sqlite3_column_decltype(sqlite3_stmt*, int N);` | `c3ref/column_decltype.html` |
| `int sqlite3_column_type(sqlite3_stmt*, int iCol);` | `c3ref/column_blob.html` |
| `int sqlite3_column_int(sqlite3_stmt*, int iCol);` | `c3ref/column_blob.html` |
| `sqlite3_int64 sqlite3_column_int64(sqlite3_stmt*, int iCol);` | `c3ref/column_blob.html` |
| `double sqlite3_column_double(sqlite3_stmt*, int iCol);` | `c3ref/column_blob.html` |
| `const unsigned char *sqlite3_column_text(sqlite3_stmt*, int iCol);` | `c3ref/column_blob.html` |
| `const void *sqlite3_column_blob(sqlite3_stmt*, int iCol);` | `c3ref/column_blob.html` |
| `int sqlite3_column_bytes(sqlite3_stmt*, int iCol);` | `c3ref/column_blob.html` |
| `const char *sqlite3_errmsg(sqlite3 *db);` | `c3ref/errcode.html` |
| `int sqlite3_errcode(sqlite3 *db);` | `c3ref/errcode.html` |
| `int sqlite3_changes(sqlite3 *db);` | `c3ref/changes.html` |
| `int sqlite3_total_changes(sqlite3 *db);` | `c3ref/total_changes.html` |
| `sqlite3_int64 sqlite3_last_insert_rowid(sqlite3 *db);` | `c3ref/last_insert_rowid.html` |
| `int sqlite3_busy_timeout(sqlite3 *db, int ms);` | `c3ref/busy_timeout.html` |
| `const char *sqlite3_libversion(void);` | `c3ref/libversion.html` |

`sqlite3_prepare_v2` compiles only the **first** statement in `zSql`. Section
2.1 calls `;` a statement *separator*, so to leave no doubt: the `;` that ends
a statement is consumed with that statement, and `*pzTail` is left pointing at
the text **after** that semicolon.

`sqlite3_exec()` given SQL text that holds **no statement** — an empty string,
or one containing only whitespace and/or SQL comments — returns `SQLITE_OK` and
changes nothing. `c3ref/exec.html` says of that case that "no SQL statements
are evaluated and the database is not changed" but does not say what the call
returns; running no statements is not a failure, so it succeeds, and the
verifier relies on that.

The header is compiled by C programs, so every declaration must be visible
without including anything else, and the header must be safe to include from
C89-and-later translation units and idempotent (include guards).

### 3.3 Constants

These macro values are fixed by the documentation and by
`c3ref/c_abort.html`, `c3ref/c_blob.html` and `c3ref/c_static.html`; the
verifier compares against the macro *and* relies on the numeric value:

| Macro | Value | Meaning |
| --- | --- | --- |
| `SQLITE_OK` | 0 | successful result |
| `SQLITE_ERROR` | 1 | generic error |
| `SQLITE_BUSY` | 5 | the database file is locked |
| `SQLITE_CONSTRAINT` | 19 | a constraint violation |
| `SQLITE_MISUSE` | 21 | interface used incorrectly |
| `SQLITE_ROW` | 100 | `sqlite3_step()` has another row ready |
| `SQLITE_DONE` | 101 | `sqlite3_step()` has finished |
| `SQLITE_INTEGER` | 1 | column type code |
| `SQLITE_FLOAT` | 2 | column type code |
| `SQLITE_TEXT` | 3 | column type code |
| `SQLITE_BLOB` | 4 | column type code |
| `SQLITE_NULL` | 5 | column type code |
| `SQLITE_STATIC` | `((sqlite3_destructor_type)0)` | binding keeps the caller's buffer |
| `SQLITE_TRANSIENT` | `((sqlite3_destructor_type)-1)` | binding copies the buffer |
| `SQLITE_VERSION` | `"3.9.0"` | the release this step targets |

`sqlite3_libversion()` must return `"3.9.0"` at this step, and the SQL
function `sqlite_version()` must return the same string. Result codes other
than those listed are out of scope: when a statement fails, the verifier checks
only that the call did not return `SQLITE_OK`/`SQLITE_ROW`/`SQLITE_DONE`,
except where the table above assigns a specific code.

## 4. The database file

A database created by this implementation must begin with the header fields
documented in `/app/sqlite-doc/fileformat2.html`
§1.2, *The Database Header*.
The verifier reads these byte ranges directly out of the file, and **only**
these:

| Offset | Size | Required content |
| --- | --- | --- |
| 0 | 16 | the string `SQLite format 3` followed by a zero byte |
| 16 | 2 | page size in bytes, big-endian; a value of `1` means 65536. Must equal the value reported by `PRAGMA page_size`, must be a power of two, and must be between 512 and 65536 |
| 18 | 1 | file format *write* version: `1` for a rollback journal, `2` for WAL |
| 19 | 1 | file format *read* version: `1` for a rollback journal, `2` for WAL |
| 56 | 4 | text encoding, big-endian; `1` means UTF-8 |
| 60 | 4 | the user version, big-endian, as read and written by `PRAGMA user_version` |

The verifier never asserts the *default* page size — only that offsets 16-17
and `PRAGMA page_size` agree and that the value is legal — because the default
is a free choice and it differs between SQLite releases.

The file size of a non-empty database must be a whole multiple of the page
size, and at least one page. **Nothing below these fields is in scope** — b-tree page layout, cell
formats, the freelist, pointer maps, the record serial-type encoding and the
journal/WAL file formats are all yours to choose. A database file written by
this implementation is never required to be readable by upstream SQLite, and no
verifier fixture is a pre-built database file.

Text is UTF-8 throughout: `PRAGMA encoding` reports `UTF-8`, and offset 56
holds `1`.

## 5. Determinism rules the verifier follows

So that a conforming implementation is not failed by a free choice:

* Every in-scope comparison that can return more than one row uses `ORDER BY`,
  **unless `/app/sqlite-doc/PROFILE.md` fixes the row order itself** — wherever
  it does, it says so where it defines the behaviour, as it does for step 1's
  introspection pragmas. Row order that neither `ORDER BY` nor the profile
  fixes is never asserted.
* Cases avoid REAL values whose decimal rendering is implementation-defined.
  Every floating point value the verifier compares needs no more than 15
  significant decimal digits, so section 2.3's rule fixes its text exactly —
  either because the value is exactly representable in binary64 (`1.0`, `2.5`,
  `-0.25`, `2.25`) or because its shortest 15-significant-digit form is
  unambiguous (`3.14159`, `7.7`). What is never compared is a value whose text
  depends on how many digits an implementation chooses to emit.
* No case depends on wall-clock time, the process locale, environment
  variables other than those the verifier sets, the current working directory,
  file timestamps, `random()`, or `rowid` values the documentation leaves
  unspecified.
* Every in-scope case is run with `-init /dev/null` and a fresh database file in
  a private temporary directory.

## 6. Out of scope

Stated so you do not spend the budget on it:

* **Error message wording**, error message format, and any result code not
  listed in §3.3.
* **Return values that the upstream page for a function does not define.**
  Two in the §3.2 list: `sqlite3_busy_timeout()` is called for its effect only
  — `c3ref/busy_timeout.html` describes the handler and states no return value,
  so the verifier compares none — and `sqlite3_errcode()` is read only after a
  call on that connection has failed, which is the only case
  `c3ref/errcode.html` defines.
* **`EXPLAIN` and `EXPLAIN QUERY PLAN` output.** Both statements must parse and
  run without error, but their result rows are for interactive analysis (see
  `/app/sqlite-doc/eqp.html`) and are never compared. Which index the planner
  chooses, and whether it chooses one at all, is likewise out of scope.
* **Performance, memory use, allocation strategy, and query plan quality.**
  Cases are sized so that any straightforward implementation finishes well
  inside the verifier budget.
* **Internal file and symbol layout**: how many `.c` files you use, what you
  call them, which symbols are static, how the pager and b-tree are organised.
* **On-disk layout below the header fields in §4**, including compatibility
  with real SQLite database files in either direction.
* **Journal and WAL file names and contents.** Only the observable outcome —
  committed data survives, rolled-back data does not, and header bytes 18/19
  reflect the journal mode — is in scope.
* **Dot commands other than those in §2.2**, the interactive prompt, and any
  command-line option not in §2.1.
* **Your own tests.** Nothing in the workspace determines acceptance; the
  verifier supplies its own cases.
* **Thread safety and concurrent access from multiple threads.** The verifier
  is single-threaded per process. Where it uses two *processes* against one
  database file, it does so only as documented in the profile.


## 7. This step

`/app` already holds the implementation you delivered for steps 1 and 2. This
step's overlay replaced only `sqlite-doc/`, `TEST_INTERFACE.md`,
`WORKSPACE_HANDOFF.md` and `timer.sh`; `Makefile`, `include/sqlite3.h`, `src/`
and `shell/` are exactly as you left them, and `make` must keep producing the
same two outputs from them. Section 1's build contract, section 2's
command-line contract and section 4's file contract are unchanged.

Three plumbing facts move at this step, and nothing else in this file does:

* The release string is now `3.9.0`. `SQLITE_VERSION`,
  `sqlite3_libversion()` and the SQL function `sqlite_version()` must all
  report it, as section 3.3 says.
* **The JSON functions must be present in the program and the library that a
  plain `make` produces.** `/app/sqlite-doc/json1.html`'s
  *Compiling the JSON1 Extension* section describes json1 as a loadable extension that upstream's own amalgamation leaves switched off
  unless a compile-time option is given; that is a statement about upstream's
  build, not a contract for this workspace. The verifier calls
  `json_extract(…)` and the other functions of `PROFILE.md` §3.1 directly, from
  ordinary SQL, at both of the boundaries this file already defines — it never
  runs a `.load` dot command, never calls a load-extension entry point, and
  never passes an extra flag to `make`. How you arrange your build to achieve
  that is out of scope.
* The JSON functions are **SQL** functions, not C entry points: **no new
  declaration is required in `include/sqlite3.h`**, and section 3.2's list is
  complete as it stands. An index on an expression needs no new entry point
  either — it is `CREATE INDEX` text like any other.

The new SQL capability itself — the eleven JSON1 functions with their path
syntax, and `CREATE INDEX` over expressions — is reached entirely through the
two boundaries this file already defines: SQL text on standard input to
`/app/build/sqlite3`, and SQL text passed to `sqlite3_prepare_v2()` or
`sqlite3_exec()`. A statement using them is an ordinary statement at both. In
particular:

* a JSON value is an ordinary **TEXT** value: it is stored in an ordinary
  column, bound with `sqlite3_bind_text()`, read back with
  `sqlite3_column_text()`, and reported as `SQLITE_TEXT` by
  `sqlite3_column_type()` — except where `PROFILE.md` §3.1 says a particular
  function returns an INTEGER, a REAL or NULL instead;
* `CREATE INDEX … ON t(expr)` is executed through `sqlite3_exec()` or
  `sqlite3_prepare_v2()`/`sqlite3_step()` like any other DDL statement, and the
  index it creates shows up in `sqlite_master` as section 4's file contract and
  `PROFILE.md` §6 already describe;
* a statement that `PROFILE.md` §3.3 or §3.4 says must be rejected fails the
  same way any other rejected statement does — non-zero exit status from the
  shell, a non-`SQLITE_OK` return from the library, and message wording that is
  not part of the requirement. Where a JSON argument is only discovered to be
  malformed while the statement runs, "rejected" means the statement does not
  produce the row it would otherwise have produced and the call reports
  failure; the verifier
  never asserts *at which* call in a prepare/step sequence the failure
  surfaces.

The verifier's gate for this step is proof of life through **this step's** new
capability at those two boundaries: `json_extract()` returning the value
`PROFILE.md` §3.1 says it holds, and an index on an expression that can be
created and then queried, once through the command-line program and once
through the static library. The gate also carries the sample of steps 1 and 2
that the verifier re-runs.
