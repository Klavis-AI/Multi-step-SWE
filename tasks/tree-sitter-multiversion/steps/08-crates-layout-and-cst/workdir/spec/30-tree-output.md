# The printed syntax tree

`tree-sitter parse` prints each tree as an indented S-expression on stdout.
This document fixes that text exactly. It is compared **byte for byte** after
the normalisation in `TEST_INTERFACE.md` — ANSI sequences stripped, trailing
whitespace on each line stripped, trailing blank lines dropped — so every space
and every bracket below is load-bearing.

There are **two** S-expression renderings in this system, and they are not the
same. The one `parse` prints is specified in §1–§4. The one the corpus test
runner uses is specified in §5. Implementing one and reusing it for the other
produces a tool that fails half its checks; the differences are small, deliberate
and individually graded.

## 1. The `parse` rendering

For the grammar

```js
rules: {
  source_file: $ => repeat($._statement),
  _statement:  $ => choice($.assignment, $.call_expression),
  assignment:  $ => seq(field('left', $.identifier), '=',
                        field('right', $._expression), ';'),
  call_expression: $ => seq(field('function', $.identifier), '(',
                        optional(seq($._expression, repeat(seq(',', $._expression)))), ')', ';'),
  _expression: $ => choice($.identifier, $.number, $.binary_expression,
                        alias($.string_literal, $.string)),
  identifier:  $ => /[a-zA-Z_]\w*/,
  number:      $ => /\d+/,
  comment:     $ => token(seq('//', /.*/)),
}
```

with `extras: $ => [/\s/, $.comment]`, the input

```text
a = 1 + 2 * b;
foo(x, "hi");
// c
```

prints exactly

```text
(source_file [0, 0] - [3, 0]
  (assignment [0, 0] - [0, 14]
    left: (identifier [0, 0] - [0, 1])
    right: (binary_expression [0, 4] - [0, 13]
      (number [0, 4] - [0, 5])
      (binary_expression [0, 8] - [0, 13]
        (number [0, 8] - [0, 9])
        (identifier [0, 12] - [0, 13]))))
  (call_expression [1, 0] - [1, 13]
    function: (identifier [1, 0] - [1, 3])
    (identifier [1, 4] - [1, 5])
    (string [1, 7] - [1, 11]))
  (comment [2, 0] - [2, 4]))
```

Read off the rules:

1. **Only named nodes are printed.** Anonymous nodes — the `=`, the `;`, the
   `(`, the `,`, the `)` — are in the tree but not in this text. Hidden nodes
   are not in the tree at all. So `_statement` and `_expression` never appear,
   and their children are printed as children of the nearest visible ancestor.
2. **One node per line.** A node opens with `(`, its type, a space, and its
   position annotation.
3. **Indentation is two spaces per level of printed depth.** Depth counts
   printed nodes only: `binary_expression`'s children sit two spaces deeper
   than it, regardless of the hidden `_expression` between them.
4. **Closing parentheses are not on lines of their own.** Every `)` that closes
   at the end of a subtree is appended to the last child's line, which is why
   the innermost line ends in four of them.
5. **A field name precedes the opening parenthesis**, as `name: `, with a colon
   and one space. A child with no field has no prefix. Aliased nodes print
   under the alias: `(string …)`, not `(string_literal …)`.
6. **A leaf prints on one line**, opening and closing on the same line.
7. **The whole block ends with a newline** and the root's closing parenthesis
   is the last non-newline character.

Extras are printed like any other node, at their structural position: the
`comment` is a child of `source_file` because that is the node whose extent
contains it. Whitespace extras produce no node and so leave no line.

## 2. The position annotation

```text
[start_row, start_column] - [end_row, end_column]
```

A space on each side of the hyphen; a comma and a space inside each bracket;
one space between the node's type and the opening bracket.

Rows and columns are **zero-based**, and a column counts **bytes** from the
start of its row. In a grammar whose word token is `/\p{L}+/`, the input
`café = 1;` gives that token `[0, 0] - [0, 5]` — five columns for four
characters, because `é` is two bytes. Nothing in this annotation is a character
count or a display width.

The end position is **exclusive**: a node ending at `[0, 5]` does not include
the byte at column 5.

The root node spans the whole input. That has a consequence worth stating,
because it is easy to get wrong: a file ending in a newline has a root whose
end position is the start of the *following*, empty row. The three-line example
above has a root of `[0, 0] - [3, 0]`, not `[2, 4]`. A file with no trailing
newline ends where its text ends: `a = 1;` with no newline gives a root of
`[0, 0] - [0, 6]`.

Empty and blank inputs still produce a tree. An empty file gives

```text
(source_file [0, 0] - [0, 0])
```

and a file containing only two newlines gives

```text
(source_file [2, 0] - [2, 0])
```

— a zero-width root positioned after the whitespace, because the whitespace is
an extra and the start rule matched nothing. Both exit 0.

## 3. Errors in the `parse` rendering

`ERROR` is a named node and prints like one, with its position and its
children:

```text
(source_file [0, 0] - [0, 7]
  (assignment [0, 0] - [0, 7]
    left: (identifier [0, 0] - [0, 1])
    (ERROR [0, 4] - [0, 5])
    right: (identifier [0, 6] - [0, 7])))
```

Two things about errors are specific to this rendering and are the most common
place an implementation diverges:

* **An `ERROR` may contain a nested `ERROR`.** Where the recovery swallowed
  characters that are not even valid tokens, their node is an `ERROR` too, so
  `a = 1 $ ;` prints

  ```text
  (source_file [0, 0] - [0, 9]
    (assignment [0, 0] - [0, 9]
      left: (identifier [0, 0] - [0, 1])
      right: (number [0, 4] - [0, 5])
      (ERROR [0, 6] - [0, 7]
        (ERROR [0, 6] - [0, 7]))))
  ```

  Adjacent uninterpretable characters are swallowed together: a run of them
  becomes **one** nested `ERROR` spanning the whole run, not one node per
  character. Parsing `let a = 1 ; ?? let b = 2 ;` against a grammar in which a
  statement ends in `;` therefore prints the tree below, in which the two
  characters are one nested `ERROR` and not two sibling nodes:

  ```text
  (source_file [0, 0] - [1, 0]
    (statement [0, 0] - [0, 11]
      name: (identifier [0, 4] - [0, 5])
      (number [0, 8] - [0, 9]))
    (ERROR [0, 12] - [0, 14]
      (ERROR [0, 12] - [0, 14]))
    (statement [0, 15] - [0, 26]
      name: (identifier [0, 19] - [0, 20])
      (number [0, 23] - [0, 24])))
  ```

* **There is no `MISSING` marker in this rendering.** A missing node is printed
  as an ordinary node of the type that was missing, with a zero-width position
  — and since only named nodes are printed, a missing *anonymous* token leaves
  no line at all. Parsing `a = 1` against the grammar above, where the `;` is
  missing, prints

  ```text
  (source_file [0, 0] - [0, 5]
    (assignment [0, 0] - [0, 5]
      left: (identifier [0, 0] - [0, 1])
      right: (number [0, 4] - [0, 5])))
  ```

  which is indistinguishable from a complete parse *in the printed text*, while
  `parse` still exits **1** because the tree has an error. Do not infer the
  exit status from the printed tree, and do not add a marker to make them
  agree.

## 4. What `parse` prints besides the tree

Nothing that is compared. `parse` also writes a trailing summary line carrying
a file name, a duration and, on failure, a short S-expression naming the first
error site — which may mention `MISSING` even though the tree above it does
not; `-s` adds a line of parse counts; `-t` adds a duration. None of that is
part of the interface — see `20-cli.md` §3 — and a check reads only the
S-expression block.

When several paths are given, each tree is a separate block, in path order,
with no separator between them beyond the newline that ends each block.

## 5. The corpus-test rendering

The corpus test runner (`40-corpus-tests.md`) compares against a **different**
rendering of the same tree. Same input, same tree, different text:

| | `parse` | corpus runner |
| --- | --- | --- |
| position annotations | yes | **no** |
| named nodes only | yes | yes |
| anonymous nodes | omitted | omitted |
| field prefixes | always printed | printed, but see §5.1 |
| a missing token | an ordinary zero-width node | `(MISSING <type>)` |
| an unexpected character inside an `ERROR` | a nested `(ERROR)` | `(UNEXPECTED '<char>')` |

The corpus rendering is the `parse` rendering with the position annotations
removed and those last two rows changed. Indentation is still two spaces per
level, closing parentheses are still appended rather than given their own
lines, and field prefixes are still `name: `.

The same four inputs, rendered for the corpus runner:

```text
a = 1
  (source_file
    (assignment
      (identifier)
      (number)
      (MISSING ";")))

a = ; b
  (source_file
    (assignment
      (identifier)
      (ERROR)
      (identifier)
      (MISSING ";")))

a = 1 $ ;
  (source_file
    (assignment
      (identifier)
      (number)
      (ERROR
        (UNEXPECTED '$'))))

a = 1; // c
  (source_file
    (assignment
      (identifier)
      (number))
    (comment))
```

(The two-space offset above is this document's indentation, not part of the
rendering.)

### 5.1 `MISSING` and `UNEXPECTED`

`(MISSING <type>)` names the type of the token that was inserted. When that
token is an **anonymous** one, the type is written as a double-quoted string,
because the type *is* the literal's text: `(MISSING ";")`. This is the only
place where an anonymous node's type reaches either rendering.

A missing *named* token would be written bare, as `(MISSING identifier)`. That
form is stated here for completeness only: no input was found for which this
release's recovery inserts a named token rather than producing an `ERROR`, so
nothing in this document set depends on the bare form. Where a named token is
absent, expect `(ERROR)`:

```text
grammar:  source_file: $ => seq('a', $.num)
input:    a
tree:     (ERROR)
```

`(UNEXPECTED '<char>')` names the character that could not be interpreted, in
single quotes. It is this rendering's form of §3's nested `ERROR`, so the same
rule about runs applies: adjacent uninterpretable characters give **one**
`(UNEXPECTED '<char>')`, not one per character, and the character it names is
the first of the run. For the input `let a = 1 ; ?? let b = 2 ;` the runner
writes

```text
(source_file
  (statement
    (identifier)
    (number))
  (ERROR
    (UNEXPECTED '?'))
  (statement
    (identifier)
    (number)))
```

### 5.2 Fields are conditionally significant

The runner decides whether fields matter by looking at the **expected** text:

* if the expected S-expression contains at least one field prefix — anything
  matching a space, a word, a colon, a space and an opening parenthesis — then
  fields are compared, and a wrong field name is a failure;
* if it contains none, the field prefixes are stripped from the actual tree
  before comparison, so a tree full of fields matches a field-free
  expectation.

Both of these are observable and both are graded. A corpus expectation may
therefore be written with fields or without, and it is only ever wrong to write
*some* of them — a partly-fielded expectation is compared with fields, and the
positions where they were omitted will not match.

`40-corpus-tests.md` specifies where these expectations live and what the
runner prints.
