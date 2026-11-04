# Milestone 05 — tree-sitter 0.22.6

This milestone carries the tool from release `0.21.0` to release **`0.22.6`**.

Everything the foundation documents (`00-overview.md` through `60-runtime.md`)
and every earlier milestone document still holds, **except** where a section
below says otherwise. Where a section changes an earlier statement it says so
explicitly and names what it replaces; nothing here silently overrides
anything.

Five areas change.

## 1. Corpus discovery narrows to `test/corpus`

`40-corpus-tests.md` §1 chooses the corpus directory in two steps: `test/corpus/`
if it exists, otherwise the top-level `corpus/`. **The second step is removed.**

The rule is now one step:

> The corpus directory is `test/corpus/`. If it does not exist, `test` runs no
> corpus tests.

A directory named `corpus/` at the repository root has no meaning to `test`
from this release. It is not searched, its tests are neither run nor printed,
and its presence changes nothing about the exit status.

Everything else in `40-corpus-tests.md` §1 is unchanged: inside `test/corpus/`
every regular file is a corpus file whatever its extension, dot-names are
skipped, subdirectories are searched to any depth, and the visiting order is
still unspecified.

The consequences that were already true of a repository with neither directory
now apply to a repository that has only a top-level `corpus/`:

| repository | tests run | exit status |
| --- | --- | --- |
| has `test/corpus/` | the tests under it | as `40-corpus-tests.md` §3 |
| has only `corpus/`, holding tests that would pass | **none** | `0` |
| has only `corpus/`, holding tests that would fail | **none** | `0` |
| has a `test/corpus` that is a **regular file**, not a directory | **none** | `0` |
| has neither | none | `0` |

The third and fourth rows are the ones worth stating twice. A corpus that would
fail, placed at the top level, does not fail the run — it is not seen at all.
And `test/corpus` is required to be a *directory*: a plain file of that name is
not a corpus file and does not make the repository's tests discoverable, so
such a repository behaves exactly like one with no corpus at all.

A repository may still keep a top-level `corpus/` on disk; it is simply inert.

## 2. The `build` subcommand

`20-cli.md` §7 ends "Anything else is an unrecognised subcommand and exits 1".
One more subcommand exists from this release:

```text
tree-sitter build [OPTIONS] [path]
```

`build` compiles a parser repository's generated sources into a shared library
and writes that library to a path you choose. It is the compilation step of
`20-cli.md` §6.4 made explicit and given an output path of its own, instead of
happening implicitly on the way to a `parse`, `test` or `query`.

| option | effect |
| --- | --- |
| `-o`, `--output <path>` | write the library here; relative paths resolve against the current directory |
| `--reuse-allocator` | build the parser so it allocates through the runtime library's allocator rather than its own |
| `<path>` | the parser directory to compile; defaults to `.` |

### What it compiles

Exactly what `20-cli.md` §6.4 compiles when a parser is loaded: `src/parser.c`,
plus `src/scanner.c` (C99) or `src/scanner.cc` (C++) if one is present, with
`src/` on the include path. The result is a position-independent shared library
exporting `tree_sitter_<name>`, where `<name>` is the grammar's name as read
from `src/grammar.json`.

The library `build` writes is therefore a real loadable parser, not a stub: it
contains the symbol `tree_sitter_<name>` and is usable as a dynamic library by
anything that links against it.

`build` needs the repository to have been generated already. It does not run
`generate` itself.

### Where it writes

With `--output`, to that path.

Without `--output`, into the **current directory**, under a name derived from
the last component of `<path>` as it resolves against the current directory:

* if that component begins with `tree-sitter-`, the name is the rest of it —
  `tree-sitter-demo` gives `demo`;
* otherwise the name is `parser`;

with the platform's dynamic-library extension appended — `.so` on Linux,
`.dylib` on macOS, `.dll` on Windows. So, run from a directory that contains
`tree-sitter-demo/`:

```text
tree-sitter build tree-sitter-demo        writes ./demo.so
tree-sitter build -o libitem.so tree-sitter-demo   writes ./libitem.so
```

`<path>` resolves against the current directory first, so a `<path>` of `.` —
which is also what `<path>` defaults to when it is omitted — names the current
directory itself, and the name comes from the current directory's own name.
From a directory that does not begin with `tree-sitter-`, there is no prefix
to strip, and the name is therefore the fallback `parser`:

```text
tree-sitter build .                       writes ./parser.so
tree-sitter build                         writes ./parser.so
```

From a directory whose own name begins with `tree-sitter-`, the same two
commands name the library after that directory instead — `build .` run from
`tree-sitter-demo/` writes `./demo.so`. No check at this milestone stages that
case, so the rule above is what is graded.

Running `tree-sitter build -o lib.so .` from inside a generated parser
repository compiles that repository and writes `lib.so` beside its `src/`.

`--reuse-allocator` changes how the parser is compiled, not what `build`
produces: the output is still a loadable shared library exporting
`tree_sitter_<name>`, at the same place, and the run still exits 0.

### Exit status

`build` exits **0** when the library was written.

When it cannot write one — the compilation failed, or the directory it was
pointed at is not a parser repository — it exits with some **non-zero** status,
and no library is written.

`20-cli.md` §0's second row already says as much ("the subcommand rejected its
input, or the work failed" is `1`), so 1 is the status to reach for. Which
non-zero status a given failure actually produces is nevertheless **not** fixed
here and is not fixed by `20-cli.md` either: nothing in this document pins it.
The only property that is graded is the one stated above — the status is not
`0` and no library is written — so report the failure however you like within
those two bounds. (For the record, the releases this milestone is drawn from
unwind rather than diagnosing on this path and report `101`; that value is an
implementation accident, not a requirement, and nothing asserts it.)

## 3. Corpus test attributes

`40-corpus-tests.md` §2.1 fixes the test name as the header's middle line
verbatim, and states that "there is no attribute, marker or annotation syntax:
a name is only ever a name". **That is what this section replaces.** From this
release a test header may carry attributes.

### 3.1 The syntax

The header is still three lines' worth of structure — a rule of `=`, the name,
a rule of `=` — but the middle part may now be **several** lines:

```text
==================
the test's name
:skip
==================
```

* every line of the middle part that is one of the attributes below is an
  attribute, not part of the name;
* the **first** attribute line ends the name. Every line after it that is not
  an attribute is discarded, not appended to the name;
* the name is the lines before the first attribute, trimmed of trailing
  whitespace, exactly as `40-corpus-tests.md` §2.1 already describes;
* a header with no attribute line behaves exactly as before.

An attribute must be **on a line of its own**. A name line that happens to
contain `:skip` — `my test :skip` — is a name containing that text and nothing
more; it is not an attribute, and the test runs normally. This has always been
true and stays true.

### 3.2 The attributes

| attribute | effect |
| --- | --- |
| `:skip` | the test is not run |
| `:error` | the test asserts that its input **fails to parse** |
| `:fail-fast` | if this test fails, the whole run stops |
| `:platform(<os>)` | the test runs only on that operating system |
| `:language(<name>)` | the test is parsed with the named language |

A test may carry more than one. When both `:skip` and `:error` are present,
`:skip` wins.

### 3.3 What the runner prints, and the exit status

`40-corpus-tests.md` §3 fixes that a test that ran is printed with `✓` or `✗`,
and that the grader reads only which names passed and which failed. A test that
an attribute prevented from being run, or whose outcome an attribute redefines,
is printed with **neither** marker: its line carries the test's name and no `✓`
and no `✗`. It is therefore neither in the passed set nor in the failed set.

That applies to `:skip`, to a `:platform` that does not match, and to `:error`
in both its outcomes.

The exit-status table of `40-corpus-tests.md` §3 is unchanged and is driven by
the ordinary tests only: a run whose every ordinary test passed exits 0 however
many tests were skipped, platform-excluded, or marked `:error`.

### 3.4 `:skip`

The test is not parsed. Its input is never given to the parser, so an input
that could not possibly parse is still not a failure, and its expected tree is
not compared against anything.

```text
==================
skipped one
:skip
==================
@@@ never valid @@@
---
(source_file)
```

A corpus file holding that test and one ordinary passing test reports the
ordinary test as passed, reports nothing as failed, and exits 0.

### 3.5 `:error`

The test asserts that its input **produces a tree with an error** — an `ERROR`
or a `MISSING` node, the same condition `20-cli.md` §3 uses for `parse`'s exit
status. The input is parsed; the expected tree written after the divider is
**not** compared against anything, and may be anything.

Neither outcome contributes to the failed set, and neither changes the exit
status: an `:error` test whose input parses cleanly — that is, one whose
assertion does not hold — is reported like any other `:error` test and the run
still exits 0 if every ordinary test passed. `:error` replaces the tree
comparison; it does not add a new way for a run to fail.

### 3.6 `:fail-fast`

When a test carrying `:fail-fast` **fails**, the run stops there. The tests
after it in the file are not run and not printed, so they are in neither the
passed nor the failed set. The failing test itself is reported with `✗`, and
the run exits 1 as any failure does.

A `:fail-fast` test that passes stops nothing; the run continues normally.

```text
==================
first ok
==================
a;
---
(source_file (item (id)))

==================
stops the run
:fail-fast
==================
b;
---
(source_file)

==================
never reached
==================
c;
---
(source_file (item (id)))
```

Here `first ok` passes, `stops the run` fails, `never reached` does not run,
and `test` exits 1.

### 3.7 `:platform(<os>)`

The test runs only when `<os>` names the operating system the tool is running
on. The names are the ones the host platform is known by: `linux`, `macos`,
`windows`, and so on. On any other platform the test is not run and is reported
with neither marker.

The attribute may be repeated; the test runs if **any** of the named platforms
is the current one.

### 3.8 `:language(<name>)`

Selects the language the test's input is parsed with, by name, from among the
languages the repository provides. `<name>` is the grammar's name as read from
`src/grammar.json` — the same `<name>` §2 uses — and not the repository
directory's name, the package name or the scope.

A `:language(<name>)` naming a language the run cannot find is a **failure of
the run**, not a failing test: `test` exits **1**, and the named test appears
in neither the passed nor the failed set. Tests already reported before that
point keep their reported outcome.

## 4. `generate` exposes the allocator and array headers

`20-cli.md` §2 "What it writes" lists `src/tree_sitter/parser.h` as the only
header `generate` emits. From this release it writes **three**, always, into
the same directory:

| path | contents |
| --- | --- |
| `src/tree_sitter/parser.h` | as before |
| `src/tree_sitter/alloc.h` | the runtime's allocation interface |
| `src/tree_sitter/array.h` | the runtime's growable-array interface |

The rest of §2's file list is unchanged.

These exist so that an external scanner (`60-runtime.md` §4) can use the same
allocator and the same array type as the runtime instead of reimplementing
them. Because `src/` is on the include path when a scanner is compiled
(`20-cli.md` §6.4), a scanner reaches them as

```c
#include "tree_sitter/alloc.h"
#include "tree_sitter/array.h"
```

and a scanner that does so must compile, link and run exactly as one that
includes only `tree_sitter/parser.h`.

### 4.1 `alloc.h`

Defines the four allocation entry points the runtime itself uses:

```c
ts_malloc(size)
ts_calloc(count, size)
ts_realloc(pointer, size)
ts_free(pointer)
```

They have the semantics of the C standard library functions of the same names
without the `ts_` prefix. A scanner that allocates its payload with `ts_malloc`
and releases it with `ts_free` is using the interface as intended.

### 4.2 `array.h`

Defines a growable array as a macro over an element type, together with the
operations on it:

```c
Array(T)                       the type of an array of T
array_init(&a)                 initialise an empty array
array_push(&a, element)        append one element
array_get(&a, i)               a pointer to the element at i
array_back(&a) / array_front(&a)
array_pop(&a)                  remove and yield the last element
array_clear(&a)                set the size to zero, keeping the storage
array_reserve(&a, n)           ensure capacity for n elements
array_delete(&a)               release the storage
```

An `Array(T)` value carries at least its `size` and its `contents`, and
`array_push` grows the storage as needed.

## 5. Regular-expression preprocessing is removed

`10-grammar-dsl.md` §3 lists `{n}`, `{n,}` and `{n,m}` among the supported
constructs and says nothing about a `{` that forms none of them. Earlier
releases rewrote such a brace into a literal before compiling the pattern.
That rewriting step is gone: a pattern is now compiled exactly as written,
and the brace is read as what it is.

> In a regular expression, `{` always begins a repetition quantifier. It must
> be followed by a decimal count — `{n}`, `{n,}` or `{n,m}` — and a closing
> `}`. A `{` that does not introduce one of those is an error.

The three valid forms themselves are **untouched** by this section. `{n}`,
`{n,}` and `{n,m}` mean exactly what `10-grammar-dsl.md` §3 already says they
mean, and a pattern using one still compiles and still matches what it always
matched — `/x[0-9a-f]{2,3}/` accepts `xab` and `xabc` as it did before.
Removing the preprocessing step narrows what a brace may be; it does not narrow
what a quantifier does.

A grammar containing such a pattern is rejected: `generate` writes a diagnostic
and exits **1**, leaving no parser, exactly as for every other rejection in
`10-grammar-dsl.md` §9, to which this case is added. The wording of the
diagnostic is not an interface, but it **names the rule** whose pattern was
rejected.

```text
braced_token: $ => /u{[0-9a-fA-F]+}/     rejected: {[0-9a-fA-F]+} is not a count
braced_token: $ => /a{gh}z/              rejected: {gh} is not a count
```

To match a literal brace, escape it. `\{` and `\}` are ordinary escaped
metacharacters (`10-grammar-dsl.md` §3) and are unaffected by this section:

```text
braced_token: $ => /u\{[0-9a-fA-F]+\}/   accepted
```

That grammar generates cleanly and parses `u{2F} u{ab}` as two `braced_token`
nodes, as it always has.
