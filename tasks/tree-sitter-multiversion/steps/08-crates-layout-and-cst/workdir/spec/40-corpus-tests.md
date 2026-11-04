# Corpus tests and `tree-sitter test`

```text
tree-sitter test [FLAGS] [OPTIONS]
```

| flag / option | effect |
| --- | --- |
| `-f`, `--filter <filter>` | run only tests whose name contains this substring |
| `-u`, `--update` | rewrite failing tests' expected output to match the actual tree |
| `-d`, `--debug` | write a parse trace to stderr |
| `-D`, `--debug-graph` | write an HTML parse log to `log.html` |

`test` takes no path argument. It always operates on the parser repository in
the current working directory, using the parser found there
(`20-cli.md` §6.3). It does three things, in order: it runs the corpus tests
(§1–§5), it validates the repository's query files (§6), and it runs the
highlight tests (§7). Any of the three can make it exit 1.

## 1. Where corpus files live

The corpus directory is chosen by this rule, in this order:

1. `test/corpus/`, if it exists;
2. otherwise `corpus/`, at the repository root.

The first that exists is used **and the other is ignored**. Adding
`test/corpus/` to a repository that has a top-level `corpus/` silently stops
the top-level one from running.

If neither directory exists, `test` runs no corpus tests, prints nothing for
them, and exits 0.

Within the corpus directory:

* every regular file is a corpus file. **No extension is required** — `.txt`
  is the convention, but a file with no extension is read the same way;
* files and directories whose names begin with `.` are skipped;
* subdirectories are searched recursively, to any depth.

> The order in which files and directories are visited is **not specified**. It
> is whatever order the filesystem reports, and it is not sorted. Do not depend
> on it, and do not write a test that depends on it.

## 2. The corpus file format

A corpus file is a sequence of tests. Each test is a header, an input, a
divider, and an expected tree:

```text
================
Simple assignment
================

a = 1;

---

(source_file
  (assignment
    left: (identifier)
    right: (number)))

================
A second test
================

b = 2;

---

(source_file
  (assignment))
```

### 2.1 The header

Three lines:

```text
===…===
<name>
===…===
```

* the first and third lines consist of **three or more** `=` characters and
  nothing else. Two is not enough: a line of `==` is ordinary text and is
  folded into whichever test it falls inside;
* the middle line is the test's name;
* the name **may not contain `=`**. A header whose middle line contains one is
  not recognised as a header at all, and the file is then read as though that
  header were not there. A file in which no header is recognised contributes a
  group with no tests, and that is not an error.

> That a header line holds `=` characters and nothing else is
> the behaviour of the first milestone; a later milestone changes it.

The **test name is the header's middle line taken verbatim**, with leading and
trailing whitespace and `=` characters trimmed away. There is no attribute,
marker or annotation syntax: a name is only ever a name, and every character
that survives trimming is part of it. A header line such as

```text
===
My test :something
===
```

names a test `My test :something` and nothing else happens as a result.

> This is the behaviour of the first milestone; a later milestone changes it.

The two run-of-`=` lines need not be the same length as each other.

### 2.2 The input and the divider

Everything between the header and the divider is the input, verbatim, with one
exception: **the single newline immediately before the divider line is not part
of the input.** It belongs to the file's layout, not to the source being
parsed. Every other newline is kept, including leading blank lines and any
further trailing blank lines beyond that one.

So the input of

```text
===
one line
===
a = 1;
---
```

is `a = 1;` with no trailing newline, and the input of

```text
===
one line and a blank
===
a = 1;

---
```

is `a = 1;\n`. This is invisible in the default expected-tree dialect, which
prints no positions, and becomes observable as soon as a dialect prints the
root node's end position.

The divider is a line consisting of **three or more** `-` characters and
nothing else. A test body may contain more than one such line — a source
language may well use `---` itself — so the rule is:

> **The divider is the line with the longest run of `-`.** If several lines tie
> for the longest, the last of them is the divider.

So in

```text
===
divider test
===
a = 1;
---
b = 2;
-----
(source_file …)
```

the `-----` is the divider and the input is the three lines `a = 1;`, `---`,
`b = 2;` — which, for most grammars, means the expected tree has to contain
the `ERROR` that the `---` produces.

> That a divider line holds `-` characters and nothing else is
> the behaviour of the first milestone; a later milestone changes it.

### 2.3 The expected tree

Everything after the divider, up to the next header or the end of the file, is
the expected tree. It is compared, after both sides are collapsed so that every
run of whitespace becomes a single space, against the **corpus-test rendering**
of the actual tree — which is **not** the rendering `parse` prints. See
`30-tree-output.md` §5; the essentials are:

* no position annotations;
* named nodes only;
* a missing token as `(MISSING ";")`;
* an unexpected character as `(UNEXPECTED '$')`;
* field prefixes are compared **only if the expectation contains at least one**
  (`30-tree-output.md` §5.2).

Because whitespace is collapsed, the expectation may be laid out on one line or
across many, indented however you like. Only the tokens matter.

A line whose first non-blank character is `;` has **no** special meaning here.
It is not a comment: it is folded into the expectation like any other text and
compared, so a test whose expected tree carries one fails.

> This is the behaviour of the first milestone; a later milestone changes it.

## 3. What `test` prints

To stdout, a tree of groups mirroring the corpus directory:

```text
  basic:
    ✓ Simple assignment
    ✗ Deliberately wrong
```

* one line per directory and one per file, each ending in `:`. A file's group
  name is its name with any extension removed;
* each level is indented two more spaces than its parent, starting at two;
* each test is a line at its file's level plus two, beginning `✓ ` (U+2713) for
  a pass or `✗ ` (U+2717) for a failure, then the test's name;
* groups are printed even when they contain no test that ran — an empty group,
  or one whose every test the filter excluded, still prints its name line.

The names are written with terminal colour. Colour is stripped before anything
is compared, so `✓ name` and `✗ name` are what is read, and the grader reads
**only** which names passed and which failed.

### Failures

After the group tree, when at least one test failed:

```text

1 failure:

expected / actual

  1. Deliberately wrong:
    (source_file (assignment (identifier) (number)))
```

A blank line; then `1 failure:` or `<n> failures:`; then a key line naming
which colour is the expectation and which is the actual tree; then, for each
failure, a blank line, a line `  <n>. <name>:`, and a line holding the two
trees merged into a single word-level diff, coloured to show which side each
differing run came from.

Because the colour is what distinguishes expected from actual, the merged line
carries no information once colour is stripped. Nothing depends on it, and you
may render a failure however you like — what is graded is that the test appears
with `✗` and that the exit status is 1.

### Exit status

| | |
| --- | --- |
| every test that ran passed | `0` |
| at least one test failed | `1` |
| the filter matched no test | `0` |
| no corpus directory exists | `0` |

## 4. `--filter`

`-f <filter>` runs only those tests whose **name contains** `<filter>` as a
substring. It is a plain substring test, not a pattern. Excluded tests are not
run and not printed; their groups still are.

A filter that matches nothing is not an error: no test runs, nothing fails, and
the exit status is 0.

## 5. `--update`

`-u` rewrites the expected tree of every test that failed, replacing it with
the corpus-test rendering of the tree that was actually produced, and exits
**0**. A test that was corrected is printed with `✓`, so after colour
stripping an updated run is indistinguishable from a passing one. After the
group tree it prints a blank line, `1 update:` or `<n> updates:`, a blank line,
and a numbered list of the corrected test names.

The rewritten file is normalised: every test is written as an 80-character rule
of `=`, the name, another 80-character rule of `=`, the input, an 80-character
rule of `-`, a blank line, the expected tree, and a blank line before the next
test.

```text
================================================================================
missing semicolon
================================================================================
a = 1
--------------------------------------------------------------------------------

(source_file
  (assignment
    (identifier)
    (number)
    (MISSING ";")))

```

Only files that contained **at least one failing test** are rewritten. A corpus
file whose every test passed is left exactly as it was, byte for byte — `-u`
does not reformat it. Within a file that is rewritten, every test is written in
the normalised form above, including the ones that passed.

> This is the behaviour of the first milestone; a later milestone changes it.

## 6. Query files are checked too

Before reporting its exit status, `test` validates the query files in the
repository's `queries/` directory: every `.scm` file there is compiled against
the repository's parser. A query that fails to compile is reported, and `test`
exits **1** — even when every corpus test passed, and even when a filter meant
no corpus test ran at all.

This makes `test` the only subcommand whose exit status depends on a file the
invocation did not name. A repository with a broken highlight query cannot have
a green test run.

## 7. Highlight tests

If a `test/highlight/` directory exists, `test` exercises it after the corpus
tests and the query check. Each file in it is **source code in the repository's
own language**, carrying its expectations inside its own comments.

Two things must be in place for the language to resolve, and neither is
optional:

* the repository's `package.json` `"tree-sitter"` entry must declare the file
  extension under `file-types`, because these files are matched by name rather
  than by being in the working directory;
* a highlight query must exist and be named by that same entry (conventionally
  `queries/highlights.scm`).

### 7.1 Assertions

An assertion is a **comment** — any node whose type contains `comment` — that
contains an arrow followed by a highlight name.

| arrow | asserts at |
| --- | --- |
| `<-` | the column where the **comment token** starts |
| `^` | the column of the **caret itself** |

The highlight name is the first run of `[A-Za-z0-9_.-]` characters after the
arrow. One comment carries one assertion.

An assertion applies to the line **above** the comment, not to the comment's
own line. Several assertion comments may be stacked: each consecutive line of
assertion comments walks the target one line further up, so in

```text
a = 1;
// <- variable
//  ^ number
```

both assertions are about `a = 1;` — the first about column 0, the second about
column 4.

An assertion comment on the **first line** of the file is ignored, since there
is no line above it.

### 7.2 Which name wins

A node may be captured by more than one pattern in the highlight query. When
that happens, the name from the **earliest** pattern in the file is the one
that applies. Given

```text
(identifier) @variable
(call_expression function: (identifier) @function)
```

the `foo` in `foo(x);` highlights as `variable`, because `(identifier)` comes
first; reversing the two lines makes it `function`.

> This is the behaviour of the first milestone; a later milestone changes it.

### 7.3 What is printed

The group header is a line reading `syntax highlighting:` with no indentation.
Each file then produces one line:

```text
syntax highlighting:
  ✓ a.x (2 assertions)
```

Two spaces, `✓`, the file's **base name**, a space, and the number of
assertions checked in parentheses — `(1 assertions)` included, the word is not
pluralised. A file with a failing assertion prints `✗` and the base name
instead, followed by an indented detail line whose wording is not an interface:

```text
syntax highlighting:
  ✗ a.x
```

A failed assertion makes `test` exit **1**, exactly as a failed corpus test
does. Checking stops at the first failure within a file.
