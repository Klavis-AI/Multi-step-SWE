# Milestone 03 — tree-sitter 0.20.9

This milestone carries the tool from release `0.20.6` to release **`0.20.9`**.

Everything the foundation documents (`00-overview.md` through `60-runtime.md`)
and every earlier milestone document still holds, **except** where a section
below says otherwise. Where a section changes an earlier statement it says so
explicitly and names what it replaces; nothing here silently overrides
anything.

Six areas change.

## 1. The `#any-of?` and `#not-any-of?` query predicates

`50-queries.md` §1.9 lists four predicates that affect matching and then states
that **any other** predicate is accepted by the compiler and has no effect.
From this release that is no longer true of two names. Two further predicates
affect matching:

| predicate | arguments | holds when |
| --- | --- | --- |
| `#any-of?` | a capture, then string literals | the captured text equals one of the literals |
| `#not-any-of?` | as `#any-of?` | the negation |

```text
((identifier) @name (#any-of? @name "alpha" "gamma"))
((identifier) @name (#not-any-of? @name "alpha" "gamma"))
```

Against the source

```text
let alpha;
const beta;
var gamma;
```

and a grammar whose `identifier` rule captures the names, the first pattern
reports `alpha` and `gamma` and the second reports `beta`.

Comparison is over the **text** of the captured node, taken from the source,
exactly as for `#eq?` (`50-queries.md` §1.9). The comparison is equality
against each literal in turn, not a regular-expression match.

These predicates constrain matching in **both** output modes: the grouped
default and `--captures`. A pattern whose `#any-of?` matches nothing
contributes no matches, and a file with no matches still prints its path line
(`50-queries.md` §2).

### 1.1 Argument validation

Unlike an unrecognised predicate, these two are **checked**, and a violation is
a compile error — exit 1, with no output on stdout:

* the **first** argument must be a capture. `(#any-of? "alpha" "gamma")` is
  rejected;
* every argument **after** the first must be a string literal. A second capture,
  as in `(#any-of? @name @name)`, is rejected.

Both rules apply to `#not-any-of?` in the same way. The diagnostic must **name
the offending predicate** — the text `#any-of?` or `#not-any-of?` must appear
in it — but its wording is otherwise not an interface, as everywhere else
(`20-cli.md` §0).

`50-queries.md` §1.9's rule is otherwise untouched: every predicate name that
is not one of the six now recognised is still accepted, still has no effect on
matching, and still produces no diagnostic and exit 0.

## 2. Generated parsers use ABI 14

`20-cli.md` §2 states that `src/parser.c` declares its ABI version as a
preprocessor constant and that the default is 13. **The default is now 14.**
Generating with no ABI option writes

```c
#define LANGUAGE_VERSION 14
```

An ABI 14 parser carries one table the previous version did not. `src/parser.c`
defines a table of **primary state ids** — one entry per parse state, giving
the state that determines which tokens are valid — and the language struct
refers to it:

| what | how it appears in `src/parser.c` |
| --- | --- |
| the table | a definition whose name is `ts_primary_state_ids` |
| the reference to it | a field initialiser written `.primary_state_ids =` |

Both must be present in a parser generated at the default ABI. The table's
element type, its length and its contents are not compared; that it is defined
and wired into the language struct is.

Everything else about `generate`'s output is unchanged: the file list of
`20-cli.md` §2 still applies, `src/parser.c` is still self-contained C
defining exactly one external `tree_sitter_<name>()`, the parser must still
load and parse (`20-cli.md` §6.4), and a corpus must still run against it
(`40-corpus-tests.md`).

## 3. `generate` can compile the parser it just wrote

`20-cli.md` §2 says `generate` "needs no C compiler and loads no parser". That
remains the default, but `generate` gains options that compile:

| flag / option | effect |
| --- | --- |
| `-b`, `--build` | after writing the parser, compile it into a shared library |
| `-0`, `--debug-build` | with `--build`, compile the library in debug mode |
| `--libdir <path>` | the directory the compiled library is written to |

The library is named after the **grammar's name**, as everywhere else
(`20-cli.md` §6.4):

* `generate --build --libdir DIR` exits 0, writes `src/parser.c` as usual, and
  leaves `DIR/<name>.so`;
* `generate --build --debug-build --libdir DIR` exits 0 and leaves
  `DIR/<name>.debug.so`. The plain `DIR/<name>.so` is **not** written — the
  debug build replaces it, it does not accompany it;
* `generate --build` with no `--libdir` exits 0 and compiles, but into the
  tool's own default library location rather than anywhere in the current
  directory. No directory named `libs` appears. Which default location is used
  is not compared;
* `generate --libdir DIR` with no `--build` compiles **nothing**. It exits 0
  and writes `src/parser.c`, and `DIR/<name>.so` does not exist. `--libdir`
  only says where a library would go; `--build` is what asks for one.

`DIR` is created if it does not exist. Compilation follows `20-cli.md` §6.4 —
`src/parser.c` plus a scanner file if present, `src/` on the include path, a
position-independent library exporting `tree_sitter_<name>`.

### 3.1 `--no-minimize` is removed

`20-cli.md` §2 lists `--no-minimize`. This release does not have it.
`generate --no-minimize` is an unrecognised option and is therefore exit 1
(`20-cli.md` §0), and because `generate` did not run, **`src/parser.c` is not
written**. Parse-table minimisation is no longer optional and there is no
option that turns it off.

## 4. `query` gains two invocation controls

`50-queries.md` lists `query`'s flags. Two more are recognised.

### 4.1 `--row-range <START:END>`

```text
tree-sitter query --row-range 1:3 q.scm in.txt
```

Limits matching to a range of **rows**, written as two integers separated by a
colon, exactly like `--byte-range` (`50-queries.md` §5). The range is
**half-open**: `START` is included and `END` is not. Rows are counted from 0,
as everywhere in this document set.

On the three-line source of §1, with the query `(identifier) @name`:

| range | captures reported |
| --- | --- |
| `0:1` | `alpha` |
| `0:2` | `alpha`, `beta` |
| `1:2` | `beta` |
| `1:3` | `beta`, `gamma` |

A range that lies entirely beyond the last row of the input — `9:10` on a
three-row file — reports no captures and is **not** an error: the path line is
printed and the exit status is 0.

The behaviour for a malformed value is not specified, as for `--byte-range`.

### 4.2 `-q`, `--quiet`

Suppresses the match and capture listing. The **path line is still printed**
for each path (`50-queries.md` §2), and the exit status is unaffected —
`--quiet` hides output, it does not change what the query did. It applies in
both output modes.

## 5. `parse` output and input encoding

### 5.1 `-x`, `--xml` replaces the S-expression

`20-cli.md` §3 describes `-x`, `--xml` as *additionally* printing an XML
rendering of the tree. From this release it **replaces** it: with `--xml`,
stdout carries the XML rendering of the tree and does **not** carry the
S-expression rendering of `30-tree-output.md`. Without `--xml`, stdout carries
the S-expression rendering exactly as before.

The XML's own markup remains unspecified in content, as `20-cli.md` §3 already
says — nothing compares element names, attributes, indentation or whitespace.
What is fixed is which of the two renderings appears on stdout for a given
invocation. The exit status is unchanged by `--xml` (`20-cli.md` §3).

### 5.2 `--encoding <encoding>`

```text
tree-sitter parse --encoding utf8  in.txt
tree-sitter parse --encoding utf16 in16.txt
```

Names the encoding of the input files rather than letting it be detected. Two
values are recognised, `utf8` and `utf16`. `--encoding utf8` on a UTF-8 file
exits 0 and prints the same tree the invocation would print without the
option.

### 5.3 UTF-16 input and its byte-order mark

A file that begins with a UTF-16 **byte-order mark** is decoded as UTF-16
without any option being given. Parsing such a file exits 0 and prints the
tree of the decoded text, where before it would have been read as bytes and
produced an `ERROR` tree and exit 1.

`--encoding utf16` on the same file produces the **same tree** as BOM
detection does.

Positions follow the rule that holds everywhere else: rows count from 0 and
**columns count bytes** (`30-tree-output.md`). UTF-16 columns are therefore
twice the character count, and the two-byte BOM occupies columns 0 and 1 of
row 0, so the first node of a UTF-16LE file whose text begins immediately
after the BOM starts at column 2. For a file decoding to

```text
let alpha;
const beta;
```

the root spans `[0, 2] - [2, 0]`, its first `keyword` spans `[0, 2] - [0, 8]`
and the `identifier` after it spans `[0, 10] - [0, 20]`.

## 6. Capture-application options on `test` and `highlight`

Three options are recognised, and each is accepted and exits 0:

| invocation | option |
| --- | --- |
| `tree-sitter test --apply-all-captures` | apply every capture a highlight query produces |
| `tree-sitter highlight --apply-all-captures` | the same, for `highlight` |
| `tree-sitter highlight --check` | check that highlight captures conform to the standard capture names |

`--apply-all-captures` states which captures a highlighter applies when several
cover the same node: **all** of them, which is the behaviour
`50-queries.md` §4 already fixes for overlapping captures. Passing it does not
change what a run produces; a run that gives it must still succeed.

`test --apply-all-captures` runs the corpus exactly as `test` does without it
(`40-corpus-tests.md`), reporting the same tests passed and the same tests
failed, and exits 0 when they all pass. It also runs the highlight tests of
`40-corpus-tests.md` §7 unchanged when the repository has a `test/highlight/`
directory: the same files are checked, against the same `highlights` query, and
the same assertions pass and fail.

**This milestone is where `highlight` must work.** `spec/01` §0 puts
`highlight` out of scope for the first milestone and says an implementation that
does not have it "loses no points at this milestone". That is no longer true
here. `highlight --apply-all-captures` and `highlight --check` are scored, and a
subcommand that exits 1 for being unrecognised (`20-cli.md` §0) does **not**
satisfy them: both are recognised options that exit 0. A solution that leaves
`highlight` unimplemented loses this section's group.

`highlight --apply-all-captures` and `highlight --check` otherwise follow
`highlight`'s exit-status rule in full (`20-cli.md` §4): in particular, when no
language can be found for a path — the case in a freshly generated parser
repository, since `highlight` has no current-directory fallback (`20-cli.md`
§4 and §6.3) — the run reports that on stderr and still exits **0**. Neither
option changes that.

> **Clarification on `highlight` availability:** `highlight` may not exist at
> the start of this milestone. When no language can be found for a path, the
> expected exit-status rule is: exit 0 (per `20-cli.md` §4 and §6.3). This is
> the same rule the two options above follow, and it applies whether or not
> `highlight` is otherwise fully implemented.
