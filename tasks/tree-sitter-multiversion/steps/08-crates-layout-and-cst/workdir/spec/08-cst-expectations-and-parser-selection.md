# Milestone 08 — CST expectations, explicit parser selection, and parser-free generation

This milestone is `tree-sitter` release **0.26.2**. It adds to the seven
foundation documents and to the milestone documents before it; everything
those state still holds except where a statement below replaces it.

Five capabilities are introduced, and they are independent of one another:

1. the `:cst` corpus-test attribute, which compares a test's expected block
   against a **third** rendering of the tree (§1, §2);
2. `test --update` writing that rendering back (§3);
3. explicit parser selection for `parse` and `query`, through
   `--grammar-path`, and through `--lib-path` with `--lang-name` (§4);
4. `generate --no-parser`, which produces a grammar's JSON artifacts without
   constructing a parse table (§5);
5. containment-filtered query ranges, `--containing-byte-range` and
   `--containing-row-range` (§6).

## 0. Source layout is not part of the interface

This release reorganises the implementation into separate library units — a
command-line front end, a generator, a loader, a highlighter, a tagger. That
reorganisation is **not graded and must not be attempted as a graded item**.
`TEST_INTERFACE.md` is unchanged on the point: the only entry point is
`/app/build.sh` producing `/app/bin/tree-sitter`, and nothing about where your
source lives is inspected. Restructure your tree if it helps you; leave it
alone if it does not. Nothing in §1–§6 depends on either choice.

## 1. The `:cst` corpus-test attribute

Corpus-test attributes — an attribute on its own line below the test's name,
inside the header — were introduced by an earlier milestone, which also fixed
the rule that an attribute written on the *name* line is part of the name and
not an attribute. This milestone adds one more recognised attribute:

```text
==================
a binding
:cst
==================
let a = 1;
---

0:0  - 0:10   source_file
0:0  - 0:10     binding
0:0  - 0:3        "let"
0:4  - 0:5        name: identifier `a`
0:6  - 0:7        "="
0:8  - 0:9        value: number `1`
0:9  - 0:10       ";"
```

`:cst` changes **what the expected block is compared against, and how**. It
changes nothing else: not where corpus files are found, not how the header or
the divider are recognised, not what `test` prints, not its exit status.

### 1.1 Which rendering

`30-tree-output.md` establishes that this system has two S-expression
renderings and that they are not interchangeable: the one `parse` prints (§1–§4
of that document) and the one the corpus test runner compares against (§5 of
that document). A `:cst` test departs from **both**. Its expected block is not
an S-expression at all; it is the line-per-node, range-prefixed rendering that
`parse --cst` prints, which a previous milestone introduced.

So a corpus file may now hold expectations in either of two dialects, chosen
per test by the presence of the attribute:

| the test | its expected block is |
| --- | --- |
| without `:cst` | the corpus-test S-expression rendering of `30-tree-output.md` §5 |
| with `:cst` | the `parse --cst` rendering |

### 1.2 How it is compared

Without `:cst`, the expectation is normalised before comparison: comments are
removed, every run of whitespace collapses to one space, and field prefixes are
compared only if the expectation contains at least one
(`30-tree-output.md` §5.2, `40-corpus-tests.md` §2.3).

**With `:cst`, none of that applies.** The expected block has leading and
trailing whitespace trimmed from the block as a whole, and is then compared to
the rendered tree **exactly**:

* whitespace is **not** collapsed. Every space inside the block is significant,
  including the padding inside a range prefix and the indentation of a node;
* comments are **not** stripped;
* field prefixes are **always** significant. There is no "expectation contains
  no fields, so fields are ignored" rule for a `:cst` test — an expectation
  that omits a field prefix the tree carries does not match.

Two consequences are worth stating because each is a way to get this wrong:

* a `:cst` test whose expected block holds an **S-expression** does not pass.
  The rendering it is compared against is not an S-expression, so the two
  cannot be equal, and the test fails with `✗` and `test` exits 1. This is the
  opposite of the behaviour before this milestone, where the attribute line was
  not recognised;
* a `:cst` test whose expected block is laid out across several lines "however
  you like" does not pass either. The freedom that `40-corpus-tests.md` §2.3
  grants a non-`:cst` expectation comes from the whitespace collapsing, and
  that collapsing does not happen here.

## 2. The expected block for a `:cst` test

The rendering is the one `parse --cst` already prints; this milestone makes it
reachable from a corpus test and makes one change to it.

The positions in the block are positions in the **test's input**, and one
detail of how that input is recovered from the corpus file is load-bearing here
in a way it never was for an S-expression expectation, which carries no
positions at all. `40-corpus-tests.md` §2.2 takes the input to be the text
between the header and the divider; the **single** newline immediately before
the divider is the separator and is not part of it. So the input of the §1
example is `let a = 1;` with no trailing newline, its last row is row 0, and the
`source_file` line ends at `0:10` — not at `1:0`, which is where the same text
ends when it is a *file* whose last byte is a newline (§4.2). Every other
newline is kept: a blank line written immediately below the header is row 0 of
the input and pushes the first token to row 1, and a *second* blank line before
the divider leaves one newline at the end of the input, so the root then does
end on the following empty row.

### 2.1 The change this milestone makes

A named node's kind is followed by a single space **only when that node has no
children**, because the space separates the kind from the node's quoted text
and only a childless node carries text. Before this milestone the space was
written for every named node, so a named node **with** children ended its line
with a trailing space.

The practical effect is that **no line of a CST block ends in whitespace**. For
the example in §1 the `source_file` and `binding` lines end at their kind.

This matters for a `:cst` expectation in a way it did not matter for
`parse --cst` output. The comparison §1.2 describes happens **inside your
binary**, between the block as it stands in the corpus file and the rendering
your parser produces; no normalisation that a harness later applies to
*captured output* sits between the two, so nothing rescues a block whose lines
do not match byte for byte. An expectation carrying a trailing space therefore
does not match a rendering that has none, and `test` reports it as failing.

### 2.2 The structure of a line

Each visible node occupies one line, parents before children, in source order.
A line is a range prefix, then indentation, then the node:

```text
<start_row>:<start_col><pad>- <end_row>:<end_col><pad><indent><node>
```

* rows and columns are zero-based and columns count bytes, as everywhere else
  (`00-overview.md`, `30-tree-output.md` §2). The end position is exclusive;
* `<indent>` is two spaces per level of depth, and the **root is already at one
  level**, so the root's kind is preceded by two spaces and each further level
  adds two;
* both `<pad>` runs align the prefixes into columns. Let

      w(n) = floor(log10(n)) for n >= 1, and 0 for n = 0

  and let `W` be the largest value of `w(row) + w(len) + 1` over the rows of the
  input, where `len` is that row's length in bytes and `row` its zero-based
  index. Each `<pad>` is `max(1, W - w(row) - w(col))` spaces, computed from the
  row and column it follows. So both pad runs are at least one space, and a
  short position is padded more than a long one;
* `<node>` is, for a named node, an optional `<field>: ` prefix then the node's
  kind, and then — only if the node has no children — a space and the node's
  source text in **backticks**. For an anonymous node it is the node's text in
  **double quotes**. For a missing anonymous token it is `MISSING: "<kind>"`.

Unlike the two S-expression renderings, this one prints **anonymous nodes**:
the `"let"`, `"="` and `";"` lines in §1 have no counterpart in either
S-expression. That is what "concrete syntax tree" refers to here, and it is the
main reason the dialect exists.

Beyond the change in §2.1, this milestone does not alter how a tree containing
an **error** or a **missing** node is rendered — whatever `parse --cst` already
does there, including any marker column it already emits, it keeps doing.
Nothing in this milestone depends on that rendering and nothing is graded on it.

A worked two-node example, for the input `let a = 1;\nlet bb = 22;` — note that
`W` is 2 for this input, so `0:0` is followed by two spaces and `1:11` by one:

```text
0:0  - 1:12   source_file
0:0  - 0:10     binding
0:0  - 0:3        "let"
0:4  - 0:5        name: identifier `a`
0:6  - 0:7        "="
0:8  - 0:9        value: number `1`
0:9  - 0:10       ";"
1:0  - 1:12     binding
1:0  - 1:3        "let"
1:4  - 1:6        name: identifier `bb`
1:7  - 1:8        "="
1:9  - 1:11       value: number `22`
1:11 - 1:12       ";"
```

## 3. `test --update` and a `:cst` test

`--update` behaves as `40-corpus-tests.md` §5 specifies, with one addition: for
a test carrying `:cst`, the block written back is the **CST rendering** of the
tree that was produced, not the S-expression rendering.

The attribute line is part of the header and is preserved, so a rewritten
`:cst` test is still a `:cst` test. The rewritten test therefore **round-trips**:
running `test --update` and then `test` with no flags leaves every test passing,
and the second run rewrites nothing.

As for any other test, the block is preceded by a blank line after the divider,
and only files that contained at least one failing test are rewritten.

## 4. Selecting a parser explicitly

`20-cli.md` §6.3 gives three ways `parse` and `query` find a language: a
`--scope` match, a `file-types` match, and the parser repository in the current
working directory. This milestone adds two options that **bypass all three**.
Both are accepted by `parse` and by `query`.

| option | effect |
| --- | --- |
| `--grammar-path <dir>` | take the parser from the repository at `<dir>` |
| `--lib-path <file>` | load the parser from an already-compiled shared library `<file>` |
| `--lang-name <name>` | the grammar name to look up inside `--lib-path`'s library |

When either is given, the path being parsed is **not** consulted to choose a
language: its extension may be claimed by nothing at all, and no parser
directory need be configured.

### 4.1 `--grammar-path`

`<dir>` is a parser repository, and it is used exactly as the current directory
would be by `20-cli.md` §6.3 step 3 — so it must already have been generated.
`--grammar-path` does not run the generator: a directory holding only a
`grammar.js` has no `src/grammar.json`, the parser's name cannot be read, and
the invocation exits **1** as §6.4 specifies. Generate first, then point at it.

Compilation and caching are unchanged (§6.4): the library is keyed by the
grammar's name, and rebuilt when its sources are newer.

### 4.2 `--lib-path` and `--lang-name`

`--lib-path` names a shared library that already exports a language, so no
compilation happens and no repository is needed — the `build` subcommand an
earlier milestone introduced already produces such a library, at a path of your
choosing with its `-o` option. The symbol to look up cannot
be inferred from the file name, so `--lang-name <name>` supplies it: the
invocation loads `tree_sitter_<name>` from the library. A `--lang-name` the
library does not export is exit **1**.

`--lib-path` requires `--lang-name`; the behaviour when it is omitted is not
specified and nothing depends on it.

The tree that either option produces is the ordinary one — `--grammar-path` and
`--lib-path` change only how the language is found, never what the parse
prints. For the grammar of §1 and a **file** holding `let a = 1;` followed by a
final newline, `parse` prints the `30-tree-output.md` rendering — the root ends
at `[1, 0]` because that newline is a byte of the file, whereas the same text as
a corpus test's input ends at `0:10` (§2):

```text
(source_file [0, 0] - [1, 0]
  (binding [0, 0] - [0, 10]
    name: (identifier [0, 4] - [0, 5])
    value: (number [0, 8] - [0, 9])))
```

## 5. `generate --no-parser`

```text
tree-sitter generate --no-parser
```

Writes the grammar's JSON artifacts and **no parser**:

| path | written by `--no-parser` |
| --- | --- |
| `src/grammar.json` | yes |
| `src/node-types.json` | yes |
| `src/parser.c` | **no** |
| `src/tree_sitter/*.h` | **no** |

It exits **0** on success. Because no `src/parser.c` is produced, a subsequent
`parse` in that directory has no parser to compile and exits 1 (`20-cli.md`
§6.4); this is not an error in the `generate` invocation.

`--no-parser` **skips parse-table construction entirely.** That is observable,
and it is the point of the flag: a grammar whose parse table cannot be built —
an unresolvable conflict between two rules, the kind of grammar that
`10-grammar-dsl.md` §6 makes an ordinary `generate` reject with exit 1 — is
still processed successfully under `--no-parser`, exiting **0** and writing both
JSON files. Only the work that precedes table construction is done, so the
diagnostics that table construction produces cannot arise.

Errors *before* table construction are unaffected and still exit 1 with no
files written: a `grammar.js` that throws, a reference to a rule that does not
exist, and every other rejection in `10-grammar-dsl.md` §9 that concerns the
grammar rather than the table.

## 6. Containment-filtered query ranges

`50-queries.md` §5 specifies `--byte-range START:END`, and an earlier milestone
added `--row-range START:END`. Both are **intersection** filters: a node is
considered when any part of it lies inside the range. This milestone adds a
containment form of each:

| option | a node is considered when |
| --- | --- |
| `--byte-range S:E`, `--row-range S:E` | it **overlaps** the range |
| `--containing-byte-range S:E` | it lies **entirely within** the byte range |
| `--containing-row-range S:E` | it lies **entirely within** the rows |

Both take the same `START:END` form, half-open, with `END` exclusive; for the
row forms a bound `r` means the point `(r, 0)`. A node straddling either bound
is **excluded** by the containing form and included by the plain form — that is
the whole of the difference. Output format and ordering are unchanged.

For the four-line input

```text
let a = 1;
let
bb = 2;
let c = 3;
```

and the query `(binding) @b`, where the second `binding` spans rows 1 and 2
(bytes 11 through 22) and the third occupies row 3 (bytes 23 through 33):

* `--containing-row-range 3:4` reports the row-3 binding alone;
* `--containing-row-range 1:2` reports **nothing** — the only binding that
  overlaps those rows extends past row 2, so it is not contained;
* `--containing-byte-range 23:33` reports the row-3 binding alone;
* `--containing-byte-range 5:20` reports **nothing** — the first binding starts
  before byte 5 and the second ends after byte 20, so although each overlaps
  the range, neither is contained.

A malformed value behaves as for the plain forms, which `50-queries.md` §5
leaves unspecified.
