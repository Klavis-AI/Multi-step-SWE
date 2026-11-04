# The query language

A **query** is a set of patterns matched against a syntax tree. Each pattern
describes a shape of nodes; each **capture** in a pattern names a node the
match should report. Queries are the mechanism behind highlighting and tagging,
and they are exercised directly through `tree-sitter query`.

```text
tree-sitter query [FLAGS] [OPTIONS] <query-path> [paths]...
```

| flag / option | effect |
| --- | --- |
| `-c`, `--captures` | report captures individually rather than grouped by match |
| `--byte-range <START:END>` | limit matching to a byte range |
| `--test` | additionally check captures against assertions embedded in the source |
| `--scope <scope>` | select the language by scope |
| `--paths <paths-file>` | read paths from a file, one per line |
| `<query-path>` | the query file |
| `<paths>...` | files to search |

`--test` compares the captures against assertion comments in the source file.
The assertion syntax is not fixed by this document set, and a source file
containing no assertions passes trivially, so `--test` on an ordinary file
behaves exactly as without it.

Exit status is `0` on success and `1` when the query fails to compile, when no
path is given, when a path cannot be read, or when no language can be found.
The wording of a compilation diagnostic is not an interface.

## 1. Query syntax

A query file is a sequence of patterns. Whitespace separates tokens and is
otherwise insignificant. `;` begins a comment that runs to the end of the line.

**Consecutive top-level patterns are separate patterns**, each with its own
index, whether or not they are on separate lines. `"=" @a ";" @b` is two
patterns, not one.

### 1.1 Node patterns

| written | matches |
| --- | --- |
| `(identifier)` | a **named** node whose type is `identifier` |
| `";"` | an **anonymous** node whose type is `;` |
| `(_)` | any **named** node |
| `_` | **any** node, named or anonymous |

The distinction between `_` and `(_)` is easy to miss and is graded. Against
the tree of `a = 1 + 2 * b;`, the pattern `(assignment _ @x)` captures four
nodes — `a`, `=`, `1 + 2 * b` and `;` — while `(assignment (_) @y)` captures
two, `a` and `1 + 2 * b`.

### 1.2 Nesting

Nesting inside a node pattern constrains **immediate children**, not
descendants:

```text
(assignment (identifier) @i)          ; matches: identifier is a child
(source_file (identifier) @i)         ; rejected: it never is
```

The second is not a pattern that matches nothing — it is a **compile error**,
exit 1. A pattern that the grammar makes structurally impossible is rejected
rather than accepted and left inert. To reach deeper, nest further:

```text
(assignment right: (binary_expression (number) @n))
```

Only named and anonymous nodes participate; hidden nodes are not in the tree
and cannot be named in a pattern.

### 1.3 Captures

`@name` after any pattern captures the node that pattern matched. A capture may
be attached to a nested pattern, to the outermost pattern, or to several
patterns in one query, and one node may carry more than one capture:

```text
(assignment) @whole
(identifier) @a @b
```

The second reports `a` and `b` for every identifier. Capture names may repeat
across patterns; they do not have to be unique.

### 1.4 Fields

A child pattern may be prefixed with a field name and a colon:

```text
(assignment left: (identifier) @lhs right: (_) @rhs)
```

The prefix constrains the match to a child reached through that field. A field
prefix works on any child pattern, including `(_)`, and a wildcard parent may
carry a field: `(_ left: (identifier) @L)`.

> Do not write such a pattern in practice at this release. A wildcard parent
> combined with a field constraint is pathologically slow — a single tiny file
> can take minutes — while the equivalent pattern with a concrete parent type
> is instant. The behaviour is correct; the cost is not. Nothing in this
> document set uses one.

### 1.5 Alternations

Square brackets match any one of the enclosed patterns:

```text
[(number) (identifier)] @lit
["=" ";"] @punct
```

An alternation may carry a capture, a quantifier, or both, and may be nested
inside a node pattern like any other child pattern.

### 1.6 Groups

Parentheses around a *sequence* of patterns group them into one pattern that
must all match, as siblings:

```text
((identifier) @a "=" @b)
```

This is one pattern with two captures, not two patterns. Grouping is also how a
predicate is attached to a pattern (§1.9).

### 1.7 Quantifiers

A child pattern may be suffixed with `?`, `*` or `+`, before any capture:

| suffix | meaning |
| --- | --- |
| `?` | zero or one |
| `*` | zero or more |
| `+` | one or more |

A `?` whose pattern is absent still yields a match — the match simply has no
capture from it.

A quantifier matches a **contiguous run of siblings**. The run begins at the
first sibling that matches the quantified pattern and ends at the first
sibling that does not — and every sibling counts, including anonymous tokens
and nodes that arrived as `extras`. Each node of the run contributes its own
capture to the same match, so the grouped output of §2 prints one `capture:`
line per node of the run, in order.

Against the three-line input

```text
a = 1;
b = 2;
c = 3;
```

the pattern `(source_file (assignment)+ @s)` produces **one** match carrying
**three** captures, one per assignment. Put something that is not an
`assignment` between the second and the third — a comment, say — and the run
ends there: the match then carries two captures, and the assignment after the
interruption is not reported at all.

> This is the behaviour of the first milestone; a later milestone changes it.

The same rule explains a pattern that looks as though it should match several
nodes but matches one. In `foo(x, y);` the three identifiers are separated by
the anonymous `(`, `,` and `)` tokens, so `(call_expression (identifier)+ @i)`
captures `foo` alone.

In the per-capture output of `--captures`, a node of the run may be printed
more than once; the number of repetitions is not fixed by this document, and
nothing may depend on it.

### 1.8 Anchors

A `.` constrains adjacency between **named** siblings — anonymous nodes between
them are ignored.

| written | meaning |
| --- | --- |
| `(p . (a) @x)` | `a` is the **first** named child of `p` |
| `(p (a) @x .)` | `a` is the **last** named child of `p` |
| `(p (a) @x . (b) @y)` | `b` is the named sibling immediately after `a` |
| `(p . "t" @x)` | `"t"` is the **first** child of `p`, named or not |
| `(p "t" @x .)` | `"t"` is the **last** child of `p`, named or not |

The anchor is not restricted to **named** patterns: a quoted pattern is legal
wherever a pattern is (§1.1), and a `.` beside one constrains that anonymous
node's position among **all** of its parent's children — it must be the
parent's first child, or its last, and not merely the first or last named one.

Against `foo(x, "hi");`, `(call_expression . (identifier) @first)` captures
`foo`, and `(call_expression (_) @last .)` captures `"hi"`.

### 1.9 Predicates

A predicate is written inside a group, alongside the pattern it constrains:

```text
((identifier) @id (#eq? @id "foo"))
((identifier) @m  (#match? @m "^[ab]$"))
((assignment left: (identifier) @l right: (_) @r) (#eq? @l @r))
```

Four predicates affect which nodes match:

| predicate | arguments | holds when |
| --- | --- | --- |
| `#eq?` | a capture and a string | the captured text equals the string |
| `#eq?` | two captures | the two captured texts are equal |
| `#not-eq?` | as `#eq?` | the negation |
| `#match?` | a capture and a string | the captured text matches the string as a regular expression |
| `#not-match?` | as `#match?` | the negation |

`#match?` takes an unanchored regular expression, so `#match? @m "b"` holds for
any text containing a `b`; anchor it with `^` and `$` to require the whole
text. Unlike grammar patterns, a predicate's regular expression is an ordinary
one and may use anchors.

Predicate comparison is over the **text** of the captured node, taken from the
source.

**Any other predicate is accepted by the compiler and has no effect on
matching.** A pattern carrying `(#frobnicate? @i "x")` matches exactly as it
would without it. This is true both of names ending in `?` and of names ending
in `!`. No diagnostic is produced and the exit status is 0.

> This is the behaviour of the first milestone; a later milestone changes it.

Passing the wrong **number** of arguments to one of the four predicates above
is a compile error, exit 1. Passing an unrecognised predicate any number of
arguments is not.

### 1.10 Compile errors

Exit 1, with a diagnostic whose wording is not an interface:

* a node type that the grammar does not have;
* a pattern the grammar makes structurally impossible (§1.2);
* unbalanced brackets or any other syntax error;
* the wrong number of arguments to `#eq?`, `#not-eq?`, `#match?` or
  `#not-match?`;
* a capture name referenced by a predicate that the pattern does not define.

A query that fails to compile writes **nothing to stdout**: the report §2 and
§3 describe is never begun, not even its path line. The diagnostic goes to
stderr, as `20-cli.md` §0 says of every diagnostic.

## 2. Output — grouped by match (the default)

For each path, `query` writes the path on a line of its own, then one block per
match. The path line appears even when there are no matches.

```text
a.x
  pattern: 2
    capture: op, start: (0, 2), text: "="
  pattern: 0
    capture: lhs, start: (0, 0), text: "a"
    capture: rhs, start: (0, 4), text: "1 + 2 * b"
  pattern: 1
    capture: fn, start: (1, 0), text: "foo"
```

Byte for byte:

* the path, exactly as it was given on the command line, at column 0;
* for each match, **two** spaces, then `pattern: `, then the pattern's
  zero-based index;
* for each capture of that match, **four** spaces, then `capture: `, the
  capture's name, `, start: `, the capture node's start point, and then one of
  two endings:

  | condition | ending |
  | --- | --- |
  | the node starts and ends on the same row | `, text: ` followed by the node's text |
  | the node spans more than one row | `, end: ` followed by the node's end point |

A **point** is written `(row, column)` — an opening parenthesis, the row, a
comma, a space, the column, a closing parenthesis. Rows and columns are
zero-based and columns count bytes, as in `30-tree-output.md` §2.

The **text** field is the node's source text rendered as a double-quoted
string with escapes: a literal `"` becomes `\"`, a newline becomes `\n`, a
backslash becomes `\\`, and a tab becomes `\t`. So a captured string literal
`"hi"` prints as `text: "\"hi\""`. The text is never truncated.

A multi-row capture therefore prints no text at all. For the input

```text
a = 1 +
  2;
```

the same query prints

```text
    capture: rhs, start: (0, 4), end: (1, 3)
```

> This is the behaviour of the first milestone; a later milestone changes it.

### Ordering

Matches are reported in the order in which they **complete**, where a match
completes when the tree walk reaches the last node that match consumed. The
walk visits nodes in **pre-order** — each node before its children, and
children left to right.

In the example above, pattern 2 (`"=" @op`) is reported before pattern 0 even
though pattern 0's match begins earlier: pattern 2's last node is the `=`
token and pattern 0's is the whole right-hand side, and the walk reaches the
`=` first.

Pre-order is not the same as ordering by end position, and the difference shows
whenever one match's last node **contains** another's. The containing match is
reported **first**, even though it extends further right. For

```text
1+2
```

with the patterns `(sum) @outer` and `(number) @num`, the `sum` spans the whole
input and each `number` sits inside it, so the output is

```text
  pattern: 0
    capture: outer, start: (0, 0), text: "1+2"
  pattern: 1
    capture: num, start: (0, 0), text: "1"
  pattern: 1
    capture: num, start: (0, 2), text: "2"
```

— the outer match first, then the inner ones left to right. Swapping the two
patterns in the query does not change this: the order follows the tree, not the
query.

When two matches complete at the same node, their relative order is not fixed
by this document.

## 3. Output — `--captures`

`-c` reports one line per capture, ordered by the position of the captured
node, with no per-match grouping:

```text
a.x
    pattern: 0, capture: lhs, row: 0, text: "a"
    pattern: 2, capture: op, row: 0, text: "="
    pattern: 0, capture: rhs, row: 0, text: "1 + 2 * b"
    pattern: 1, capture: fn, row: 1, text: "foo"
```

Byte for byte: the path at column 0, then for each capture **four** spaces,
`pattern: `, the pattern index, `, capture: `, the capture name, `, row: `, the
**start row** of the captured node as a bare integer, `, text: `, and the
node's text escaped as in §2.

Three differences from §2 are worth stating explicitly, because each one is a
place an implementation naturally diverges:

* there is **no `pattern:` grouping line** — the pattern index is part of every
  capture line;
* the position is a bare **row number**, not a point, and there is no column;
* the text is **always** printed, whether or not the node spans several rows.
  A capture spanning two rows prints `text: "1 +\n  2"`, with the newline
  escaped.

> This is the behaviour of the first milestone; a later milestone changes it.

### Ordering

Captures are reported in the order the captured nodes appear in the source. A
capture from a later pattern can therefore precede one from an earlier pattern.
When two captures are at the same position, their relative order is not fixed
by this document.

## 4. Overlapping captures

Two patterns may capture the same node, and when they do, **both captures are
reported**. There is no precedence, priority or suppression rule between them:
neither pattern order, nor pattern specificity, nor the order of the captures
in the file suppresses the other.

For the query

```text
(identifier) @variable
(call_expression function: (identifier) @function)
```

the identifier `foo` in `foo(x, "hi");` is reported twice — once as
`@variable` and once as `@function` — in both output modes.

## 5. `--byte-range`

`--byte-range START:END` limits matching to the given half-open byte range,
written as two integers separated by a colon. Matches outside the range are not
reported. The behaviour for a malformed value is not specified.
