# tree-sitter 0.19.0 — overview and vocabulary

`tree-sitter` is an incremental parser generator with a companion parsing
runtime. Implementing it means building both halves:

* a **generator** that reads a grammar written in a JavaScript DSL and emits a
  table-driven parser as C source;
* a **runtime** that runs a generated parser over source text, produces a
  concrete syntax tree, re-parses edited text in time proportional to the size
  of the edit rather than the size of the file, and recovers from syntax errors
  instead of abandoning the parse.

Both halves are reached through a single command-line executable, and that
executable is the only thing the grader touches. It is run inside a *parser
repository* and judged on exit statuses, on what it writes to stdout and
stderr, and on the files it leaves on disk. `TEST_INTERFACE.md` is the complete
statement of how it is invoked and what is observed; this document set is the
statement of what it must do.

These seven documents describe release **0.19.0**. Each later milestone ships
its own document describing what that milestone changes; nothing here
anticipates one. Where a rule below looks arbitrary, or looks like an
oversight, it is still the rule: implement it as written.

## The pipeline

```text
grammar.js                 a grammar, written in the DSL of 10-grammar-dsl.md
    |  generate
    v
src/grammar.json           the grammar, normalised to JSON
src/parser.c               a table-driven parser, in C
src/node-types.json        the static shape of every node the grammar can make
src/tree_sitter/parser.h   the header src/parser.c includes
    |  a C compiler
    v
a shared library exporting tree_sitter_<name>()
    |  parse / test / query
    v
a concrete syntax tree
```

The three stages are separable and are observed separately. `generate` is a
pure source-to-source transformation and needs no C compiler. Everything that
consumes a tree — `parse`, `test`, `query` — needs a *loaded* parser, so it
compiles the generated C on demand and caches the result; `20-cli.md` §6 gives
the rules. A grammar that generates cleanly but whose parser will not compile
fails at the second stage, not the first.

## The object model

### Grammar

A grammar is a name plus a set of named **rules**, each rule a tree of
combinators over string literals, regular expressions and references to other
rules. One rule is the **start rule**: the first one listed. Seven optional
top-level settings — `extras`, `conflicts`, `externals`, `inline`,
`supertypes`, `precedences`, `word` — modify how the rules are compiled.
`10-grammar-dsl.md` specifies all of it.

Rules divide into two kinds, and the division is purely lexical:

* a rule whose name begins with `_` is **hidden**;
* every other rule is **visible**.

### Generated parser

The generator partitions the grammar into a **lexical** part and a **syntactic**
part. Every string literal, every regular expression, and everything wrapped in
`token()` becomes a **token** — a terminal recognised by a generated lexer.
Everything else becomes a nonterminal in an LR parse table. Ambiguity in that
table is resolved at generation time by precedence and associativity, or — when
you declare it — deferred to parse time as a **GLR** split. The generator
either produces a table or fails; it never produces a parser that silently
drops an alternative.

### Concrete syntax tree

Parsing yields a tree that spans the whole input, from byte 0 to the last byte,
with no gaps and no overlaps. Every **node** carries:

| | |
| --- | --- |
| a type | a string: a rule name, a literal's text, or `ERROR` |
| a byte range | half-open, `[start_byte, end_byte)` |
| a point range | `[start_point, end_point)`, each a `(row, column)` pair |

Rows and columns are **zero-based**, and a column is a count of **bytes** from
the start of its row, not of characters and not of display columns. A row is
the text between two `\n` bytes; a `\r` immediately before an `\n` belongs to
the row it ends. So in `café = 1;` the four-character identifier `café`
occupies columns 0 through 5, because `é` is two bytes.

"Concrete" is the operative word. Nothing is discarded: punctuation, operators,
keywords and comments are all in the tree, and the byte ranges of a node's
children tile the byte range of the node. This is what makes the tree usable
for editor tooling, and it is why the printed form in `30-tree-output.md`,
which shows only *some* of the nodes, is a view of the tree rather than the
tree itself.

## Vocabulary

These terms are used with exactly these meanings throughout. They are not
interchangeable, and several of the distinctions are the difference between a
tree that matches the grader's expectation and one that does not.

**node** — one element of the syntax tree, as above.

**named node** — a node whose type is a grammar rule name. `(identifier)`,
`(binary_expression)`. Named nodes are the ones that appear in the printed tree
and the ones a query pattern `(identifier)` matches.

**anonymous node** — a node produced by a string literal in the grammar. Its
type is the literal's own text: the `;` in `seq($.expr, ';')` produces a node
of type `;`. Anonymous nodes are in the tree, and a query can match one by
writing the literal in quotes, but they are not printed by `parse`. A node
produced by a *regular expression* that is not bound to a rule name is also
anonymous, with its type set to the pattern's source text.

**hidden node** — a node that is in neither of the above categories and does
not appear in the tree at all. Hidden rules (`_`-prefixed) are structural
helpers: the children of a hidden rule are attached directly to the hidden
rule's parent, as though the rule had been written inline. Hiding is a
statement about the tree, not about the grammar — a hidden rule still
participates in parsing, precedence and conflicts exactly like a visible one.

**visible node** — named or anonymous; that is, present in the tree.

**field** — a name attached to the *edge* from a node to one of its children,
declared with `field('name', rule)`. A field is a property of the position, not
of the child: the same `identifier` rule can be reached as `left:` in one
context and `function:` in another. Fields let a consumer ask for "the
condition of this if-statement" without counting children.

**alias** — a rewriting of a node's type in one particular position, declared
with `alias(rule, name)`. The node keeps its extent and its children and
changes only what it is called. Whether the alias is *named* depends on how the
new name is written: `alias($.inner, $.renamed)` yields a named node
`renamed`; `alias($.inner, 'LITERAL')` yields an anonymous node `LITERAL`.

**extra** — a token permitted to appear *anywhere* in the input without being
mentioned in any rule, listed in the grammar's `extras`. Whitespace and
comments are extras. An extra that produces a visible node appears in the tree,
as a child of whichever node contains it; an extra never affects which
production is chosen.

**supertype** — a hidden rule, listed in the grammar's `supertypes`, that is
recorded in `src/node-types.json` as an abstract parent of its alternatives.
Declaring `_expression` a supertype does not make it appear in the tree; it
changes how the node-type file describes the positions that accept an
expression, which are then described as accepting `_expression` rather than
enumerating every concrete alternative.

**token** — a terminal. Produced by a string literal, by a regular expression,
or by `token(...)`/`token.immediate(...)` around a larger expression. A token is
matched by the lexer in one atomic step and always produces a single leaf node;
structure inside a `token()` is erased.

**external token** — a terminal whose recognition your generated lexer cannot
express, declared in the grammar's `externals` and implemented by a
hand-written C **external scanner** compiled alongside the parser. Indentation
tracking, heredoc delimiters and nested comments are the canonical cases.
`60-runtime.md` §4 specifies the scanner interface.

**MISSING** — the error-recovery outcome in which the parser concludes that a
token the grammar requires is simply absent, and inserts a zero-width node of
that token's type to complete the production. A missing node has
`start_byte == end_byte`.

**ERROR** — the error-recovery outcome in which the parser cannot reconcile the
input with the grammar at all. An `ERROR` node covers the span it could not
interpret and holds, as children, whatever it did manage to parse inside that
span. `ERROR` nodes are named nodes and always appear in the printed tree.

A tree containing an `ERROR` or a `MISSING` node "has an error", and that
condition — not the presence of a diagnostic message — is what `parse`'s exit
status reports.

## The rest of this document set

| document | what it fixes |
| --- | --- |
| `10-grammar-dsl.md` | the grammar language: `grammar()`, every combinator, the regex subset, and how precedence, associativity and declared conflicts resolve ambiguity |
| `20-cli.md` | every subcommand: arguments, flags, exit status, what reaches stdout and stderr, what lands on disk, and how a parser is located and compiled |
| `30-tree-output.md` | the S-expression tree, byte for byte, including position annotations, fields, and `ERROR`/`MISSING` |
| `40-corpus-tests.md` | everything `tree-sitter test` does: the corpus file format, where corpus files are found, highlight tests, and what the runner prints |
| `50-queries.md` | the query language and the exact lines `query` prints |
| `60-runtime.md` | incremental reparse, error recovery, and the external scanner contract |

Read `10-grammar-dsl.md` and `30-tree-output.md` first: between them they fix
the two ends of the pipeline, and every other document depends on both.

Two documents fix formats that are compared **byte for byte** after the
normalisation described in `TEST_INTERFACE.md`: `30-tree-output.md` and
`50-queries.md`. Treat their code blocks as literal.
