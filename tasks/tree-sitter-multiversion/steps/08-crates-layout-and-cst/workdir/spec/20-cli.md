# The command-line interface

One executable, several subcommands:

```text
tree-sitter <SUBCOMMAND> [flags] [options] [arguments]
```

Everything the grader does, it does by running this executable. Exit statuses
are compared exactly, so read §0 before anything else.

## 0. Exit status

| situation | status |
| --- | --- |
| the subcommand did what it was asked | `0` |
| the subcommand rejected its input, or the work failed | `1` |
| no subcommand given, or an unrecognised subcommand | `1` |
| an unrecognised flag or option | `1` |

A subcommand that succeeds exits 0; a subcommand that fails exits 1; and asking
for a subcommand this release does not have is a failure of the second kind, so
it exits 1 rather than being silently ignored.

> The status for the last two rows — a **usage** error, as opposed to a
> subcommand that ran and failed — is the behaviour of the first milestone; a
> later milestone changes it. The first two rows do not change.

Two exceptions to "failure means 1" are specified where they arise, and they
are both cases where the tool decides there was nothing to do rather than that
something went wrong: `highlight` and `tags` exit **0** when no language can be
found for a path (§4, §5), and `test` exits 0 when a filter selects no tests
(§3).

Diagnostics go to **stderr**; results go to **stdout**. The *wording* of any
diagnostic is not an interface — nothing depends on the text of an error
message, and you may phrase them however you like. What is fixed is the exit
status, and which stream carries the result.

## 1. `--version`

```text
tree-sitter --version
tree-sitter -V
```

Writes one line to stdout naming the tool and its release, and exits 0:

```text
tree-sitter 0.19.0
```

The line must contain the release number `0.19.0`. Anything the line carries
after the release number is build information and is not part of the interface.

> **Baseline note (v0.19.0):** the version string above reflects the v0.19.0
> baseline. Later milestones update the release number; each milestone's spec
> says which version string that milestone targets. The interface rule — that
> the output must contain the release number — applies at every milestone.

With no subcommand at all, the executable writes usage information to stderr
and exits 1.

## 2. `generate`

```text
tree-sitter generate [FLAGS] [OPTIONS] [grammar-path]
```

Reads a grammar and writes a parser. Needs no C compiler and loads no parser.

| flag / option | effect |
| --- | --- |
| `--log` | write the table-construction log to stderr |
| `--no-minimize` | skip parse-table minimisation |
| `--prev-abi` | emit a parser for the previous ABI version (see below) |
| `--report-states-for-rule <rule-name>` | write the generated states for one rule to stdout |
| `<grammar-path>` | the grammar file; defaults to `grammar.js` in the current directory |

### What it reads

With no `grammar-path`, `generate` reads `grammar.js` from the current
directory. With a `grammar-path`, it reads that file, which must end in `.js`
or `.json`; any other extension is rejected with exit 1.

A `.js` grammar is evaluated by an external `node` process. A `.json` grammar
is the grammar JSON of `10-grammar-dsl.md` §10 and is read directly.

### What it writes

| path | written |
| --- | --- |
| `src/parser.c` | always |
| `src/node-types.json` | always |
| `src/grammar.json` | **only when no `grammar-path` argument was given** |
| `src/tree_sitter/parser.h` | unless `--prev-abi`, which leaves `src/tree_sitter/` created but empty |
| `binding.gyp` | always |
| `bindings/node/binding.cc` | always |
| `bindings/node/index.js` | always |
| `bindings/rust/lib.rs` | always |
| `bindings/rust/build.rs` | always |
| `Cargo.toml` | always |
| `package.json` | created if absent; otherwise updated in place |

`src/` and `src/tree_sitter/` are created if they do not exist. Existing files
are overwritten.

> That `binding.gyp`, `bindings/` and `Cargo.toml` are written unconditionally,
> with no way to ask for the parser alone, is
> the behaviour of the first milestone; a later milestone changes it.

> **`src/grammar.json` is conditional and the condition is easy to get
> backwards.** Running `tree-sitter generate ./grammar.js` produces a working
> `src/parser.c` and **no** `src/grammar.json`. Since `src/grammar.json` is how
> a parser's name is later discovered (§6), a parser generated that way cannot
> be loaded from its own directory, and a subsequent `parse` in the same
> directory fails with exit 1. Generating with no argument is the normal path.

The update to `package.json` adds `"main": "bindings/node"` and leaves
everything else, including the `"tree-sitter"` configuration key, untouched.
When no `package.json` exists, one is created.

### ABI version

`src/parser.c` declares the parser's ABI version as a preprocessor constant
named `LANGUAGE_VERSION`:

```c
#define LANGUAGE_VERSION 13
```

By default the value is **13**. With `--prev-abi` it is **12**, and
`src/tree_sitter/parser.h` is then not written, on the assumption that the
consumer supplies the older header itself.

> Both the spelling `--prev-abi` and its being the only way to select an ABI
> are the behaviour of the first milestone; a later milestone changes them.

`src/parser.c` must be self-contained C: it includes
`<tree_sitter/parser.h>` — angle brackets, resolved through the include path,
not a quoted relative include — and nothing else that is not part of the C
standard library, and it defines exactly one external function,
`tree_sitter_<name>()`, returning the language. When the grammar declares
`externals`, it additionally *declares* the five external-scanner functions of
`60-runtime.md` §4 without defining them.

### Failure

Every rejection listed in `10-grammar-dsl.md` §9 exits 1 and leaves no parser:
if `generate` fails, `src/parser.c` is not written. A `grammar.js` that throws,
or that cannot be found, is likewise exit 1.

> One consequence of grammar evaluation happening in `node`: a bare relative
> path is resolved as a module specifier, not as a file. `tree-sitter generate
> grammar.js` fails, while `tree-sitter generate ./grammar.js` succeeds. Both
> are exit 0 or 1 in the ordinary way; only the resolution differs.

## 3. `parse`

```text
tree-sitter parse [FLAGS] [OPTIONS] [--] [paths]...
```

Parses each path and prints its tree. The tree format is
`30-tree-output.md`; it is the only part of `parse`'s stdout that is compared.

| flag / option | effect |
| --- | --- |
| `-q`, `--quiet` | suppress the tree; the exit status still reports errors |
| `-t`, `--time` | print a per-file timing line |
| `-s`, `--stat` | print an aggregate parse-count line after the trees |
| `-d`, `--debug` | write a parse trace to stderr |
| `-D`, `--debug-graph` | write an HTML parse log to `log.html` in the current directory |
| `-x`, `--xml` | additionally print an XML rendering of the tree |
| `-e`, `--edit <edits>...` | apply edits and reparse (below) |
| `--timeout <timeout>` | abandon a parse that exceeds this many microseconds |
| `--scope <scope>` | select the language by scope instead of by file name |
| `--paths <paths-file>` | read paths from a file, one per line |
| `<paths>...` | files to parse |

### Exit status

`0` when every path parsed without producing an `ERROR` or a `MISSING` node.
`1` when any tree contains one — that is what a "failed parse" means here, and
it is reported the same way whether or not `--quiet` suppressed the tree.

`1` also when: no path was given and none was matched; a path cannot be read,
including when it is a directory; `--scope` names a scope that is not
configured; or no language can be found for a path (§6).

### Paths

A path argument may be a literal path or a glob. A path argument beginning with
`!` is a *negative* glob and removes matches contributed by the others. If the
resulting set is empty, that is exit 1, not a silent success.

Trees are printed in the order the paths were resolved.

### What is **not** an interface

`parse` writes a trailing summary line after the trees, carrying the file name,
a duration and, when the parse failed, the position of the first error. **None
of it is part of the interface** — not the duration, not the column widths, not
the presence or absence of the line, not the error position it names. The same
applies to the aggregate line `-s` prints, which carries parse counts, and to
the line `-t` prints, which carries a duration. Report the failure through the
exit status and the tree; put timing wherever you like.

`-q` suppresses the S-expression tree, but **not** the trailing summary line:
a clean parse under `-q` prints nothing at all, while a parse that found an
error still prints its summary line and exits 1. Since the summary line is not
compared (§3, "What is **not** an interface"), what `-q` reliably guarantees is
the absence of the tree.

The XML rendering `-x` adds, the trace `-d` writes and the HTML log `-D`
writes are likewise unspecified in content. `-D`'s only fixed obligation is
that it writes its log to `log.html` in the current directory rather than to
stdout.

### `--edit`

Each `--edit` value is an edit to apply before parsing. The value is **one**
argument, holding three whitespace-separated parts:

```text
<START> <REMOVED_LENGTH> <NEW_TEXT>
```

`<START>` is either a byte offset or a `row,column` pair. `<REMOVED_LENGTH>` is
a count of bytes to delete at that position, and the remainder of the value is
the text to insert there. Edits are applied in order to the in-memory source,
each one followed by an incremental reparse, and the tree printed is the tree
after the last edit. Nothing is written back to the file.

This is how incremental reparsing is made observable from the command line: the
tree after an edit must equal the tree a fresh parse of the edited text would
produce (`60-runtime.md` §3).

> The flag's name is the behaviour of the first milestone; a later milestone
> changes it (`60-runtime.md` §3 carries the same note).

## 4. `highlight`

```text
tree-sitter highlight [FLAGS] [OPTIONS] [paths]...
```

Prints each file with syntax highlighting, driven by the `highlights` query of
the file's language configuration (§6).

| flag / option | effect |
| --- | --- |
| `-h`, `--html` | emit an HTML document instead of terminal output |
| `-q`, `--quiet` | print nothing |
| `-t`, `--time` | print timing information |
| `--scope <scope>` | select the language by scope |
| `--paths <paths-file>` | read paths from a file, one per line |
| `--help` | usage information (long form; at this baseline `-h` is `--html`) |

Terminal output is the file's own text with colour applied using terminal
escape sequences. Escape sequences are stripped before any comparison, so what
survives normalisation is the source text, unchanged, including its line
breaks. Which colour is chosen for which capture is a matter of theme and is
not an interface.

HTML output is a complete document: a doctype, a `<head>` carrying a title and
a stylesheet, and a `<body>` containing a `<table>` with one row per source
line, each row holding a line-number cell and a line cell, with highlighted
spans inside the line cell and HTML-special characters escaped. The document's
exact markup is not compared.

The theme comes from the `theme` key of the configuration file (§6.1), and at
this release that file is the only source of one — there is no flag that
supplies a theme on the command line. That is why the mapping from capture name
to colour is not observable here: nothing an invocation can control determines
it.

> This is the behaviour of the first milestone; a later milestone changes it.

Exit status is `0` on success. **When no language can be found for a path,
`highlight` reports that on stderr and still exits 0.** Compare `parse`, which
exits 1 in the same situation; the difference is deliberate and is graded.

## 5. `tags`

```text
tree-sitter tags [FLAGS] [OPTIONS] [paths]...
```

Prints the definitions and references a file contains, driven by the `tags`
query of the file's language configuration (§6). The flags are `-q/--quiet`,
`-t/--time`, `--scope` and `--paths`, with the same meanings as for
`highlight`.

One line per tag, on stdout:

```text
a         	 | variable	def (0, 0) - (0, 1) `a = 1 + 2 * b;`
foo       	 | call    	ref (1, 0) - (1, 3) `foo(x, "hi");`
```

Field by field: the tag's name, left-aligned in a 10-character column; a tab;
` | `; the tag's kind, left-aligned in an 8-character column; a tab; `def` for
a definition or `ref` for a reference; a space; the point range of the *name*
node, in the `(row, column) - (row, column)` form; a space; and the source line
the name appears on, in backticks.

The kind comes from the capture that produced the tag: a capture named
`definition.<kind>` yields `def` and kind `<kind>`; a capture named
`reference.<kind>` yields `ref` and kind `<kind>`. The name is the text of the
tag's `@name` capture.

A name longer than 10 characters, or a kind longer than 8, is not truncated —
the column simply widens. When a tag's node spans several lines, the backticked
field holds only the line the name is on.

Exit status is `0` on success, and — as for `highlight` — **`0` when no
language can be found for a path.**

## 6. Finding and building a parser

`parse`, `test` and `query` need a loaded parser; `generate` does not. This
section is the whole of how one is found and made runnable, and it is
observable: a check that runs `parse` in a scratch directory depends on every
step of it.

### 6.1 Configuration

The tool keeps its configuration in a directory, `$TREE_SITTER_DIR` when that
variable is set and `$HOME/.tree-sitter` otherwise. The file
`config.json` in that directory holds:

| key | meaning |
| --- | --- |
| `parser-directories` | a list of directories to search for parsers |
| `theme` | the colour theme `highlight` uses |

When the file is absent, the defaults apply: `parser-directories` is
`$HOME/github`, `$HOME/src`, `$HOME/source`.

`tree-sitter init-config` writes a configuration file containing those
defaults, to `$TREE_SITTER_DIR/config.json`, and exits 0. The value written is
the environment's `HOME` with the suffix appended, unresolved — not
canonicalised to an absolute path, and not the literal text `$HOME`. With the
invocation's `HOME` set to `./h`, the list written is
`["./h/github", "./h/src", "./h/source"]`.

### 6.2 Parser repositories and their configuration

A **parser repository** is a directory containing a grammar and its generated
`src/`. Each configured parser directory is searched for immediate
subdirectories whose names begin with `tree-sitter-`, and each one found is a
candidate parser.

A repository describes itself through the `"tree-sitter"` key of its
`package.json`, an array of configurations:

| key | meaning |
| --- | --- |
| `path` | the parser's directory, relative to the repository root; defaults to `.` |
| `scope` | the scope name `--scope` matches, conventionally `source.<lang>` |
| `file-types` | file extensions this language claims |
| `content-regex` | a pattern that must match the file's content |
| `first-line-regex` | a pattern that must match the file's first line |
| `injection-regex` | a pattern matching language names in embedded-code markers |
| `highlights` | the highlight query file, relative to the repository root |
| `injections` | the injection query file |
| `locals` | the local-scope query file |
| `tags` | the tag query file |

When a repository has no such configuration but does have `src/grammar.json`, a
minimal configuration is synthesised for it, with no scope and no file types.
The query paths, when absent, default to the corresponding file in a `queries/`
directory.

### 6.3 Selecting a language for a path

`parse`, `test` and `query` try, in order:

1. the language whose `scope` equals the `--scope` value, when that option is
   given. An unmatched scope is exit 1, not a fall-through.
2. the language whose `file-types` claim the path's extension, subject to
   `content-regex` and `first-line-regex` if configured.
3. the parser in the **current working directory**, if the current directory is
   itself a parser repository.

Step 3 is what lets a check `cd` into a scratch parser repository and parse a
file whose extension nothing claims. `highlight` and `tags` do **not** have
step 3: they consider only configured and discovered languages, so running
`highlight` inside a freshly generated parser repository finds no language —
and exits 0 (§4).

If no step succeeds, `parse` and `query` exit 1.

### 6.4 Compiling

A parser is loaded from a shared library, compiled on demand from the
repository's generated sources:

* the sources are `src/parser.c`, plus `src/scanner.c` if it exists, else
  `src/scanner.cc` if it exists. `src/scanner.c` is compiled as C99;
  `src/scanner.cc` as C++;
* `src/` is on the include path, so `#include <tree_sitter/parser.h>` resolves
  to the generated header;
* the result is a position-independent shared library that exports
  `tree_sitter_<name>`, where `<name>` is the grammar's name;
* the parser's name is read from `src/grammar.json`. A repository without that
  file cannot be loaded, and the attempt is exit 1;
* the library is written to `bin/<name>.<so|dll>` inside the configuration
  directory — `$TREE_SITTER_DIR/bin/<name>.so` — and reused on later runs;
* it is rebuilt when any of its sources is newer than it, and otherwise not.

> The cache is keyed by the **grammar's name**, not by the repository's path.
> Two repositories whose grammars share a name share one cached library, and
> the newer sources win by modification time.

A failure to compile, or a library that does not export the expected symbol, is
exit 1 for whichever subcommand needed the parser.

## 7. The other subcommands

`test` is specified in `40-corpus-tests.md`; `query` in `50-queries.md`.

`dump-languages` writes the languages it discovered to stdout and exits 0.
`init-config` is described in §6.1. `build-wasm` and `web-ui` exist; nothing
in this document set depends on either, and neither is exercised before
milestone 6 — at milestone 6 `build-wasm` is invoked once, only to confirm it
is not a subcommand (`TEST_INTERFACE.md`'s milestone table), and `web-ui` is
never invoked at all.

Anything else is an unrecognised subcommand and exits with the usage status of
§0 — `1` at this release, and see the marker note there.
