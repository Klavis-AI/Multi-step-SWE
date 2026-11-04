# Milestone 02 — tree-sitter 0.20.6

This milestone carries the tool from release `0.19.0` to release **`0.20.6`**.

Everything the foundation documents (`00-overview.md` through `60-runtime.md`)
and every earlier milestone document still holds, **except** where a section
below says otherwise. Where a section replaces an earlier statement it says so
explicitly and names what it replaces; nothing here silently overrides
anything.

Seven areas change. Four of them are the query engine — the two output
formats byte for byte, a new pattern form, and a query shape the compiler must
now accept. The fifth is the external-scanner half of the runtime, where a
token recognised from its column must survive an edit correctly. The sixth is
how an ABI is selected and what `generate` is allowed not to write. The
seventh is the corpus file format, which gains a suffix on its delimiter lines
and comments in its expected trees.

## 1. Query output, grouped by match

**This replaces the capture line of `50-queries.md` §2**, which carries the
marker note for it. The rest of §2 is unchanged: the path line at column 0,
printed even when there are no matches; the `  pattern: <index>` grouping line
with two spaces of indent; and the ordering rule, which does not move at this
milestone or any other.

A capture whose node begins and ends on the **same row** now prints

```text
in.qlang
  pattern: 0
    capture: 0 - name, start: (0, 4), end: (0, 5), text: `a`
```

Byte for byte: four spaces; `capture: `; the capture's **index**; ` - `; the
capture's name; `, start: ` and the node's start point; `, end: ` and the
node's end point; `, text: ` and the node's text between **backticks**.

Four things are new on that line, and each is graded independently.

### 1.1 The capture index

Every capture **name** in the query file has an index. The names are collected
in the order they first appear, duplicates removed, and numbered from `0`. A
name reused by a later pattern keeps the index it was first given, so indices
identify names, not occurrences.

For the query file

```text
(identifier) @x
(number) @x
(string) @s
```

`@x` is index `0` in both patterns and `@s` is index `1`, even though `@s`
belongs to pattern 2. Index and pattern index are independent numbers that
happen to coincide when every pattern introduces exactly one fresh name.

### 1.2 `end:` is always printed

The start point is now always followed by the end point, for every capture,
not only for one spanning several rows. A point is written `(row, column)`
exactly as `50-queries.md` §2 defines it, with columns counted in bytes.

### 1.3 Text is raw and backtick-quoted

The text field is the node's source text placed between two backtick
characters and **reproduced literally**. Nothing is escaped: a double quote, a
backslash, a tab and a backtick each appear as itself. `50-queries.md` §2's
double-quoted rendering with `\"`, `\\`, `\n` and `\t` escapes is replaced. A
captured string literal `"hi"` prints as

```text
    capture: 0 - s, start: (2, 7), end: (2, 11), text: `"hi"`
```

The text is still never truncated.

### 1.4 A multi-row capture keeps the old shape

In **grouped** output only, a capture whose node spans more than one row still
prints exactly as `50-queries.md` §2 says it does — with **no index** and with
**no text**:

```text
multi.qlang
  pattern: 0
    capture: c, start: (0, 0), end: (3, 2)
  pattern: 1
    capture: 1 - s, start: (2, 2), end: (2, 6), text: `"hi"`
```

Both lines above come from one run over one file. The first capture spans rows
0 to 3, so it carries neither an index nor a text field; the second is
single-row and carries both. So the grouped capture line has **two branches**:
a single-row capture takes the new shape of §1, while a multi-row capture takes
the baseline shape of `50-queries.md` §2 unchanged — `capture: ` followed
directly by the name, with no `<index> - ` before it and no `text:` field after
the end point. This is the only place in either output mode where the capture
index is absent, the asymmetry is deliberate, and it is graded. `--captures`
output does **not** share it — see §2.

The **order** of those two lines is fixed, and it is worth being exact about
why, because `50-queries.md` §2's rationale for its own example reaches the
wrong answer here. §2's rule is right — matches are ordered by **the last node
each match consumed** — but its gloss compares where each match *finishes*, and
by that reading the `@s` match (ending `(2, 6)`) would precede the `@c` match
(ending `(3, 2)`). It does not.

What §2 leaves unsaid is how two such nodes are compared. They are compared by
the point at which a **pre-order** walk of the tree reaches them: a node's
**start**, with a parent reached before any of its children. The `@c` match
consumed one node, the `call`, which the walk reaches at `(0, 0)`; the `@s`
match consumed the `string`, reached at `(2, 2)`; `(0, 0)` is first, so `@c` is
printed first even though it ends last.

The two readings agree whenever the two nodes are **disjoint**, which is the
only case `50-queries.md` §2 illustrates, and they disagree exactly when one
node **contains** the other — where the containing match is always reported
first. This is a clarification of §2, not a change to it: the behaviour is
identical at `0.19.0` and at this release, it is the same in both output modes,
and where the two readings disagree the pre-order one is the one that holds.
§2's closing sentence is untouched and still governs: when two matches complete
at the **same** node their relative order is not fixed, and nothing at this
milestone fixes it.

## 2. Query output with `--captures`

**This replaces the capture line of `50-queries.md` §3**, which carries the
marker note for it. The rest of §3 is unchanged: the path line, the four
spaces of indent, the absence of any grouping line, and the ordering rule.

```text
in.qlang
    pattern:  9, capture: 9 - n, start: (0, 8), end: (0, 9), text: `1`
    pattern: 10, capture: 10 - s, start: (2, 7), end: (2, 11), text: `"hi"`
```

Byte for byte: four spaces; `pattern: `; the pattern's zero-based index
**right-aligned in a field two characters wide**; `, capture: `; the capture's
index; ` - `; the capture's name; `, start: ` and the start point; `, end: `
and the end point; `, text: ` and the backticked raw text.

Three consequences, each of which is a place an implementation diverges:

* pattern index `0` prints as `pattern:  0`, with **two** spaces after the
  colon; index `10` prints as `pattern: 10`, with one. An index needing more
  than two characters simply widens the field;
* the bare `row: <n>` of `50-queries.md` §3 is gone. The position is the same
  `start:` point as in §1, followed by an `end:` point. That document's second
  "difference from §2" bullet no longer holds; its first and third still do;
* the text is printed for **every** capture, multi-row nodes included, and it
  is raw. A newline inside the text is a real newline, so the line runs on to
  further physical lines and ends with the closing backtick:

```text
multi.qlang
    pattern:  0, capture: 0 - c, start: (0, 0), end: (3, 2), text: `foo(
  a,
  "hi"
);`
```

The capture index is the same number, defined the same way, as in §1.1, and
unlike §1.4 it is present on **every** `--captures` line: a multi-row capture
in this mode keeps its `<index> - ` prefix and its `text:` field, and differs
from a single-row one only in that its end point is on a later row and its text
contains real newlines.

## 3. Negated field patterns

`50-queries.md` §1.4 describes a field prefix, `field: <pattern>`, which
constrains a match to a child reached through that field. This milestone adds
the negative form. It is new syntax; the baseline query compiler rejects it.

> **`!field` inside a node pattern constrains the match to nodes that have no
> child under that field.**

It is written as a bare `!` immediately followed by the field name, in the
position a child pattern would occupy. It names no child pattern of its own
and captures nothing — it is a constraint and nothing else.

Given a language in which a binding is `let <name> (: <type>)? = <value> ;`
with fields `name`, `type` and `value`, and the source file

```text
let a = 1;
let b: int = 2;
foo(a, "hi");
let c = d;
```

three forms must work:

| query | matches |
| --- | --- |
| `(binding !type name: (identifier) @untyped)` | `a` and `c` — a negated field may sit beside positive field constraints in the same pattern |
| `(binding !type) @b` | the whole of `let a = 1;` and of `let c = d;` — the node pattern carrying the negation may itself be captured |
| `(source_file (binding !type) @nt)` | the same two — a pattern carrying a negation nests inside another node pattern like any other child pattern |

The binding for `b` is excluded from all three because it has a `type` child.
Ordering within the pattern is insignificant, as `50-queries.md` §1 already
says of whitespace and pattern order.

## 4. A predicate group as a branch of an alternation

`50-queries.md` §1.5 gives alternations, §1.6 gives groups, and §1.6 notes
that grouping is how a predicate is attached to a pattern. This milestone
requires the two to compose.

> **A group carrying a predicate may be a branch of an alternation.** The
> predicate constrains only the branch it appears in; the other branches are
> unconstrained. The alternation is still one pattern.

This holds at the top level of a query file and nested inside a node pattern.
The baseline compiler does not accept the shape, and it must now compile and
match without a diagnostic, exit 0.

On the source file of §3, the query

```text
[((identifier) @x (#eq? @x "a")) (number) @y]
```

reports `a` (row 0), `1`, `2`, and `a` again (row 2, the argument of the
call) — the `#eq?` filters the identifier branch and leaves the number branch
alone. Every capture reported carries pattern index `0`, because an
alternation is one pattern however many branches it has, and the capture
indices are `0` for `@x` and `1` for `@y` by §1.1.

`#match?` behaves the same way in a branch: `[((identifier) @i (#match? @i
"^f")) (string) @s]` reports only the identifiers beginning with `f`, plus
every string. Nested inside a node pattern, `(call [((identifier) @fn (#eq?
@fn "foo")) (string) @arg])` behaves likewise.

Nothing else about predicates changes. The four predicates of
`50-queries.md` §1.9 keep their meanings and their argument counts, comparison
is still over captured text, and an unrecognised predicate is still accepted
and inert.

## 5. External scanners: a column-dependent token and incremental reparse

`60-runtime.md` §3 states the incremental-reparse contract absolutely — a
reparse must produce exactly the tree a parse from scratch would produce — and
§4.6 explains that a node whose external scanner state differs from the state
now current is not reused. §4.4 gives the scanner the lexer's column through
`get_column`. This milestone makes the case where those three meet explicit,
and grades it.

> **When an external scanner consults `get_column` while producing a token,
> that token depends on its column. An edit earlier on the same row moves the
> column, so neither that token nor any sub-tree containing it may be reused
> across the reparse; it must be re-scanned at its new position.**

The dependency is scoped to the **row**. A column-dependent token on a row the
edit did not touch keeps its column, and reusing it is correct. An edit that
inserts or deletes text before such a token on its own row is what invalidates
it.

Nothing about the five-function contract of `60-runtime.md` §4.2, the call
schedule of §4.3, or the serialization rules of §4.6 changes. What changes is
that the reparse now honours §3 in this case.

### 5.1 Worked example

A grammar with one ordinary token rule, `word`, matching `/[^@\s]+/`, with
whitespace in `extras`, and with an `externals` array holding one token that a
hand-written scanner produces only when the lookahead is `@` **and**
`get_column` returns `3`. The file `one.colx` holds `abc@` followed by a
newline, which parses as a `word` and that external token.

```sh
tree-sitter parse one.colx --edit '0 0 x'
```

The edit inserts `x` at byte 0, so the source becomes `xabc@`. The `@` is now
at column 4, the scanner declines it, and the printed tree must be the tree a
fresh parse of `xabc@` gives — the `word` widening to four characters and the
`@` becoming an error:

```text
(source_file [0, 0] - [1, 0]
  (word [0, 0] - [0, 4])
  (ERROR [0, 4] - [0, 5]
    (ERROR [0, 4] - [0, 5])))
```

The tree contains an `ERROR` node, so `parse` exits **1** (`20-cli.md` §3).
Deleting a byte instead — `--edit '0 1 '`, an empty insert — moves the `@` to
column 2 and must invalidate it the same way.

For a two-row file holding `abc@` twice, `--edit '0 0 zz'` invalidates only
row 0: the external token on row 1 is still at column 3 and must still be
recognised, so the tree carries an `ERROR` on row 0 and an intact external
token on row 1.

The flag is `-e, --edit` as `60-runtime.md` §3 specifies, with its value
format unchanged.

## 6. Selecting an ABI, generating without bindings, and the release number

Two options are added to `20-cli.md` §2's flag table and one is withdrawn, and
§6.4 corrects what `--version` prints.

### 6.1 `--abi <version>`

Selects the ABI version the generated parser declares. Accepted values:

| value | declared version |
| --- | --- |
| `13` | 13 |
| `14` | 14 |
| `latest` | 14 — the newest this release emits |

The value lands in the `LANGUAGE_VERSION` preprocessor constant that
`20-cli.md` §2 already describes: `generate --abi 14` writes

```c
#define LANGUAGE_VERSION 14
```

into `src/parser.c`. `--abi latest` writes the same. `generate --abi 13`
writes `13`.

The option is explicit. With no `--abi`, what `generate` declares is what
`20-cli.md` §2 already says it declares; **this milestone does not change the
default**. `generate` exits 0 in each of the three cases and writes its usual
file set.

Those three are the only values this milestone specifies. What `--abi` does
with any **other** value — an ABI this release cannot emit such as `12`, a
number out of range, or a value that is not a number at all — is **out of
scope for this milestone**: it is not fixed here, no foundation section fixes
it either, and nothing grades it. In particular `20-cli.md` §0's row for an
unrecognised flag or option does **not** reach this case: the option `--abi` is
recognised, and §0 says nothing about a recognised option given a value it
rejects. Reject such a value however you find reasonable.

**This replaces `20-cli.md` §2's ABI paragraph**, whose marker note covers both
the spelling of the option and its being the only way to select an ABI. What it
says about the constant, and about the default, still holds; how an ABI is
asked for is §6.3.

### 6.2 `--no-bindings`

Suppresses the language-binding files. `generate --no-bindings` still writes

* `src/parser.c`,
* `src/node-types.json`,
* `src/grammar.json`, subject to the grammar-path condition `20-cli.md` §2
  already states,
* `src/tree_sitter/parser.h`,

and writes **none** of `binding.gyp`, `Cargo.toml`, `bindings/node/binding.cc`,
`bindings/node/index.js`, `bindings/rust/lib.rs` or `bindings/rust/build.rs`.
It exits 0.

**This replaces the marker note on `20-cli.md` §2's *What it writes* table**,
which states that the binding files are written unconditionally with no way to
ask for the parser alone. "Always" against each binding file now means "unless
`--no-bindings` was given"; every other row of the table is unchanged, and the
generated parser is unaffected — it still compiles, loads and parses exactly as
`20-cli.md` §6.4 requires.

### 6.3 `--prev-abi` is withdrawn

`generate` no longer accepts `--prev-abi`. The invocation is rejected, nothing
is generated, and the exit status is the one `20-cli.md` §0 gives an
unrecognised option — read that row rather than assuming a number, because §0
carries a marker note of its own.

ABI 12 can no longer be emitted, and `--abi` of §6.1 is the only way to select
an ABI. One consequence: `20-cli.md` §2's *What it writes* table qualifies
`src/tree_sitter/parser.h` with "unless `--prev-abi`", and that condition can
no longer arise, so the header is written by every successful `generate` —
including one given `--no-bindings`, as §6.2 states.

### 6.4 The release number `--version` prints

**This replaces the release number in `20-cli.md` §1.** That section requires
the `--version` line to contain the literal `0.19.0`, which was the release the
baseline implemented. The requirement is really this:

> **`--version` prints the release number of the release being implemented.**

At this milestone that number is `0.20.6`. Read §1's `0.19.0` as naming the
baseline release rather than as a constant; the shape of the line, and
everything else §1 says, is unchanged. A later milestone moves the number again
and does not need to restate this rule.

## 7. The corpus file format

Two additions, both to `40-corpus-tests.md` §2.

### 7.1 A suffix on the delimiter lines

**This replaces the marker note on `40-corpus-tests.md` §2.1** — that a header
line holds `=` characters and nothing else — and the matching one on §2.2 for
the divider's `-` characters. §2.1's *other* marker note, the one on the test
name being taken verbatim with no annotation syntax, is **not** touched by this
milestone and still holds exactly as written.

> **A delimiter line may carry a suffix: arbitrary text after its run of `=` or
> `-`. The suffix of the file's first header line fixes the suffix for the
> whole file, and every delimiter line in that file — both lines of every
> header and every divider — must repeat it identically.**

A line whose run of `=` or `-` is followed by anything other than that exact
suffix is not a delimiter at all. It is ordinary text and is folded into
whichever test it falls inside, exactly as §2.1 already says of an unrecognised
header.

Everything else in §2.1 and §2.2 is unchanged: three or more `=`, three or more
`-`, the three-line header, the longest-run rule for choosing the divider, and
a file in which no header is recognised contributing a group with no tests
rather than an error.

A file whose first header line carries no suffix has the **empty** suffix, so
every delimiter in it must be bare — which is the format §2.1 and §2.2 already
describe, unchanged. Both forms are valid; what is not valid is mixing them.

```text
==========|||
suffixed test
==========|||

a = 1;

---|||

(source_file
  (assignment
    left: (identifier)
    right: (number)))
```

That file runs one test and it passes. Four ways to get it wrong, each of which
means the delimiter is not recognised and the file contributes fewer tests, or
none:

* the divider written `---` while the headers carry `|||`;
* the divider written `---xxx` while the headers carry `|||`;
* the closing header line written `==========` while the opening one carries
  `|||`;
* a second test in the same file whose headers and divider carry `///`. The
  first test is still recognised, but the second header is not, so its text is
  folded into the first test's expected tree and **the first test fails**.

The first two are the shape a test takes when its divider is never recognised:
it is a test the runner never forms, so it is **not run and not reported** — it
appears in neither the `✓` list nor the `✗` list, and the file's other tests
run and are reported as usual. The last two are the shape an unrecognised
*header* takes: a header the runner does not recognise is not a header, so its
lines are ordinary text too, folded into the test they fall inside — which is
why a test that ran earlier in the file can be made to fail by one. Neither
shape is an error in itself: the run still exits on the ordinary rule of
`40-corpus-tests.md` §3.

The suffix is arbitrary text, not a fixed marker: `========== note` with
`--- note` works as well as `|||` does, provided every delimiter repeats it.

### 7.2 Comments in an expected tree

**This replaces the marker note on `40-corpus-tests.md` §2.3**, which states
that a line beginning with `;` has no special meaning and is compared like any
other text.

> **In an expected tree, a line whose first non-blank character is `;` is a
> comment. It is discarded before the comparison.**

It may appear anywhere in the expectation — before the tree, between its lines,
after it — and it may be indented. The rest of §2.3 is unchanged: whitespace is
still collapsed, the comparison is still against the corpus-test rendering of
`30-tree-output.md` §5, and field prefixes are still compared only if the
expectation contains at least one.

The rule is about the **line**, not the character. A `;` that is not the first
non-blank character of its line is ordinary text and is compared, so

```text
(source_file   ; not a comment
  (assignment))
```

is still an expectation containing the tokens `; not a comment`, and a test
carrying it still fails. Only a whole line is a comment.

```text
==========
commented expectation
==========

a = 1;

---

; one assignment, with its fields
(source_file
  ; the only statement
  (assignment
    left: (identifier)
    right: (number)))
```

That test passes.

## 8. Scope

Nothing else in the specification moves at this milestone. In particular, and
because each is a plausible place to over-correct:

* **match and capture ordering** in both output modes does not change at this
  milestone. It is exactly as `50-queries.md` §2 and §3 describe, read together
  with the clarification in §1.4 of how two matches' last nodes are compared.
  Nothing here grades an order those sections leave undetermined — in
  particular, two matches that complete at the **same** node are explicitly
  unordered by §2, and no check asserts one;
* **overlapping captures** (`50-queries.md` §4) and **quantified captures**
  (§1.7) behave as documented. §4's example produces two matches that complete
  at the same node, so their order falls under §2's undetermined case and is
  **out of scope** here: nothing grades the order in which overlapping captures
  on one node are printed;
* the `parse` tree rendering (`30-tree-output.md`), the grammar DSL
  (`10-grammar-dsl.md`) and every exit status of `20-cli.md` §0 are untouched;
* in the corpus format, only the two things §7 names move. The test **name** is
  still the header's middle line taken verbatim with no annotation syntax
  (`40-corpus-tests.md` §2.1), the corpus-test rendering is still that of
  `30-tree-output.md` §5, and what `test` prints is still `40-corpus-tests.md`
  §3;
* diagnostic **wording** remains outside the interface everywhere.
