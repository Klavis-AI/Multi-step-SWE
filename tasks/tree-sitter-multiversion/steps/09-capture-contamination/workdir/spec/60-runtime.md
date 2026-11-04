# The runtime

The generated `src/parser.c` is a table; on its own it does nothing. The
runtime is the engine that drives it: it holds the parse stack, runs the lexer,
builds and owns trees, recovers from syntax errors, reuses work across edits,
and calls out to an external scanner when the grammar declares one.

This document states the runtime behaviour that is observable through the
command line. Everything here can be reached with `generate`, `parse` and a
`src/scanner.c`.

## 1. Parsing

A parse takes a language and a byte slice and produces a **tree**. Three
properties hold for every parse, and each of them is load-bearing:

1. **A tree is always produced.** There is no "failed to parse" outcome for
   well-formed UTF-8 input. Input the grammar does not describe yields a tree
   containing `ERROR` and `MISSING` nodes (§2), not the absence of a tree.
2. **The root node spans the entire input**, from byte 0 to the last byte,
   whatever errors it contains. Nothing is silently dropped.
3. **Parsing is deterministic.** The same bytes and the same language produce
   byte-identical output, on every run and on every machine.

Trees are immutable. Nothing in this document set exposes a way to mutate one;
an edit produces a new tree (§3).

## 2. Error recovery

When the parser reaches a point where no action applies, it recovers rather
than stopping, and it has exactly two ways to do so.

**Insert a missing token.** If a single token would let the parse continue, the
parser inserts one at the current position. The inserted node is **zero-width**
— its start point equals its end point — and it consumes no input. This is how
`a = 1`, against a grammar whose `assignment` ends in `;`, still yields a
complete `assignment`.

**Wrap unexpected input in an `ERROR` node.** Input that cannot be made to fit
is collected into an `ERROR` node. An `ERROR` may contain well-formed
sub-trees, and characters that are not even valid tokens appear as a further
nested `ERROR` inside it. Adjacent characters that are not valid tokens are
collected into **one** such nested `ERROR`, whose span covers the whole run —
never one such node per character.

The two renderings of these nodes differ, and which one you must produce
depends on the subcommand — see `30-tree-output.md` §3 and §5.1. `parse` exits
**1** for a tree containing either kind of node, even when the printed tree
looks complete.

Recovery must be deterministic: the same input must always recover the same
way. Nothing in the grader tolerates a recovery that varies between runs.

## 3. Incremental reparsing

Given the previous tree, the edit that was applied to the source, and the new
source, the runtime produces a new tree while reusing the sub-trees the edit
did not touch. The observable contract is simple and absolute:

> **An incremental reparse must produce exactly the tree a parse from scratch
> would produce.** Reuse is an optimisation. It is never permitted to change
> the result.

Nothing observes *whether* a node was reused, so an implementation that
re-parses from scratch on every edit is behaviourally correct here — it is only
slower. What is graded is the resulting tree, and it must be the tree a fresh
parse gives.

`parse` exposes edits through a repeatable flag:

```text
-e, --edit "<START> <DELETED> <TEXT>"
```

The value is **one** argument holding three whitespace-separated parts (the
quotes above are not part of it):

| part | meaning |
| --- | --- |
| `<START>` | where the edit begins: either a byte offset, or `row,column` with no space around the comma |
| `<DELETED>` | how many **bytes** to delete at that point |
| `<TEXT>` | the text to insert; everything after the second space, so it may itself contain spaces, and may be empty |

The flag may be given several times. Edits are applied in the order written,
each against the source as the previous edit left it, with a reparse after
each. Only the **final** tree is printed. A value that does not have at least
two whitespace-separated parts, or whose first two parts are not numbers, is
rejected with exit status 1.

> The flag's name is the behaviour of the first milestone; a later milestone
> changes it.

Positions given as `row,column` count rows from 0 and columns in **bytes**, as
everywhere else in this document set.

## 4. External scanners

Some tokens cannot be described by a regular expression: heredocs, indentation,
string interpolation, anything whose end depends on what came before. The
`externals` array in the grammar (`10-grammar-dsl.md` §8) declares such tokens
and hands the job of recognising them to C code you write.

### 4.1 Where the scanner lives

| file | compiled as |
| --- | --- |
| `src/scanner.c` | C99 |
| `src/scanner.cc` | C++ |

Exactly one may be present; `src/scanner.c` is preferred if both are. Nothing
else in `src/` is compiled. The scanner is compiled together with
`src/parser.c` into the same shared library, with `src/` on the include path,
so a scanner includes the generated header as

```c
#include <tree_sitter/parser.h>
```

A C++ scanner must give its five entry points **C linkage** (`extern "C"`),
because the loader resolves them by their unmangled names.

`generate` does **not** check that a scanner exists. A grammar with a non-empty
`externals` and no scanner file generates successfully, exit 0; the failure
appears when a parser is loaded, and that invocation exits 1. The wording of
that failure is not an interface.

### 4.2 The five functions

For a grammar named `<lang>`, the scanner must define all five:

```c
void *tree_sitter_<lang>_external_scanner_create(void);
void  tree_sitter_<lang>_external_scanner_destroy(void *payload);
bool  tree_sitter_<lang>_external_scanner_scan(void *payload, TSLexer *lexer,
                                               const bool *valid_symbols);
unsigned tree_sitter_<lang>_external_scanner_serialize(void *payload, char *buffer);
void  tree_sitter_<lang>_external_scanner_deserialize(void *payload,
                                                      const char *buffer,
                                                      unsigned length);
```

`create` returns an opaque **payload** — the scanner's own state, of whatever
shape it likes — which the runtime passes back to the other four and frees
through `destroy`.

### 4.3 When each is called

`create` is called once, when a language is installed into a parser, and
`destroy` once, when it is replaced or the parser is released. **They are not
called per parse**, so a scanner that parses several files in one process sees
one `create` and one `destroy`, not one pair each.

The other three are called around every attempt to lex an external token:

1. `deserialize` restores the state that was current at the position the lexer
   is about to scan from. When there is no preceding external token — at the
   start of the input, or when the parser is reset — it is called with a
   **length of 0**, and `buffer` may be a null pointer. A scanner must treat
   `length == 0` as "return to your initial state"; it is the only reset signal
   there is.
2. `scan` is called with the lexer positioned at the first character of the
   prospective token.
3. `serialize` is called **only if `scan` returned true** — that is, only after
   a token was actually produced. A `scan` that returns false is not followed
   by a `serialize`.

A single external token therefore costs one `deserialize`, one `scan` and one
`serialize`; a rejected attempt costs a `deserialize` and a `scan`. The runtime
may attempt the same position more than once, so neither `scan` nor
`deserialize` may assume it is called once per position, and **`scan` must not
have side effects outside the payload**.

### 4.4 `scan` and the lexer

```c
struct TSLexer {
  int32_t lookahead;
  TSSymbol result_symbol;
  void (*advance)(TSLexer *, bool skip);
  void (*mark_end)(TSLexer *);
  uint32_t (*get_column)(TSLexer *);
  bool (*is_at_included_range_start)(const TSLexer *);
  bool (*eof)(const TSLexer *);
};
```

| member | contract |
| --- | --- |
| `lookahead` | the code point at the current position; `0` at the end of input |
| `result_symbol` | set before returning true, to the token's index in `externals` |
| `advance(lexer, false)` | consume `lookahead` and include it in the token |
| `advance(lexer, true)` | consume it and treat it as whitespace *before* the token — the token's start moves past it |
| `mark_end(lexer)` | fix the token's end at the current position; lets a scanner look ahead past the end of the token it is returning |
| `get_column(lexer)` | the current column |
| `eof(lexer)` | true at the end of input |

If `mark_end` is never called, the token ends wherever the scanner stopped
advancing.

`valid_symbols` is indexed by a token's **position in the `externals` array**,
and is true for each external token the parser could accept here. A scanner
must consult it: returning a token whose entry is false is a bug, and the usual
shape is to bail out immediately when none of the tokens the scanner knows how
to produce is valid.

`scan` returns true when it produced a token, having set `result_symbol`, and
false otherwise. On false the lexer is rewound to where `scan` began, so a
scanner may advance freely and then decline.

### 4.5 The scanner runs first

Whenever the current parse state permits at least one external token, the
external scanner is called **before** the generated lexer. If it declines, the
position is restored and the generated lexer runs normally. The scanner
therefore takes precedence over every built-in token — including a token whose
text it overlaps — but only in states where one of its tokens is valid.

If neither the scanner nor the generated lexer produces a token, the parser
enters error recovery and lexes again in the recovery state, where a larger set
of external tokens is typically valid. A scanner is consulted during recovery
and must cope with being called at a position that makes no sense to it.

**Zero-width external tokens** are permitted in general, but one produced at
the point of error recovery is discarded rather than accepted. Without that
rule a scanner that returns an empty token during recovery loops forever.

### 4.6 `serialize` and `deserialize`

```c
#define TREE_SITTER_SERIALIZATION_BUFFER_SIZE 1024
```

`serialize` writes the payload's state into `buffer` and returns the number of
bytes written. It may write at most `TREE_SITTER_SERIALIZATION_BUFFER_SIZE`
bytes. Returning 0 is legal and means "no state"; a scanner that keeps none
may simply return 0 and ignore `deserialize`.

`deserialize` receives exactly the bytes a previous `serialize` produced, and
the length it returned.

The serialized bytes are attached to the token the scan produced, and they are
part of what makes a sub-tree reusable: on an incremental reparse, a node whose
external scanner state differs from the state now current is **not** reused,
and is reparsed instead. A scanner that serializes state incompletely does not
produce a visibly wrong tree — it produces a *correct* tree, because the
reparse falls back to real work — but a scanner that serializes state it cannot
restore will produce a wrong one. Serialize everything that affects a future
`scan`, and nothing that does not.

## 5. Loading a parser

How a parser repository is located, compiled into a shared library and cached
is a property of the command line rather than the runtime; it is specified in
`20-cli.md` §6. Two consequences matter here:

* the library is named after the **grammar name**, not the directory, so two
  parser repositories with the same `name` share one cache entry;
* the library is rebuilt when `src/parser.c` or the scanner file is newer than
  it, so editing a scanner and re-running `parse` picks up the change without
  any explicit rebuild step.
