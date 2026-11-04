# Milestone 01 — generator and runtime (`tree-sitter` 0.19.0)

This is the first milestone. There is nothing to inherit and nothing to
migrate: everything the seven foundation documents describe is delivered here,
from nothing.

Those seven documents are the specification. **This document does not repeat
them.** It says which of their obligations this milestone owes, what is
deliberately out of scope, and — in §8 — the few acceptance criteria that are
consequences of two documents at once and are therefore easy to read past.
Where this document names a requirement, the foundation section it cites is
what fixes the bytes.

## 0. Scope

Deliver `/app/build.sh` and `/app/bin/tree-sitter` as `TEST_INTERFACE.md`
requires, and with them the whole of the pipeline in `00-overview.md`:

```text
grammar.js  ->  generate  ->  src/  ->  a C compiler  ->  parse / test / query
```

Five entry points whose behaviour the specification fixes in full are used at
this milestone, and they are the five below:

| entry point | fixed by |
| --- | --- |
| `--version` | `20-cli.md` §1 |
| `generate` | `20-cli.md` §2, `10-grammar-dsl.md` |
| `parse` | `20-cli.md` §3, `30-tree-output.md`, `60-runtime.md` |
| `test` | `40-corpus-tests.md` |
| `query` | `50-queries.md` |

Two further subcommands are also invoked at this milestone, and neither needs
more than `20-cli.md` gives it: `init-config`, which writes the configuration
file of §6.1 and exits 0 (§7, §6.1), and `dump-languages`, which writes the
languages it discovered to stdout and exits 0 (§7). Both must exist and both
must exit 0 here — `TEST_INTERFACE.md` records the same milestone for each.
Neither output *layout* is fixed by any document in this set, and no line of
either is compared as a line. What is fixed, and what R6 grades, is the
configuration file `init-config` leaves behind (§6.1) and the presence on
`dump-languages`' stdout of the `scope` a discovered repository declares in its
`package.json` (§6.2).

**Out of scope for this milestone.** `highlight`, `tags` and `build` are listed
in `20-cli.md` and in `TEST_INTERFACE.md` as arriving later; nothing here
exercises them, and an implementation that does not have them yet loses no
points at this milestone. `web-ui` is exercised at no milestone at all, and
`build-wasm` is invoked only once, at milestone 6, to confirm it is not a
subcommand (`TEST_INTERFACE.md`'s milestone table). Everything else `20-cli.md`
§0 says about argument handling still applies to them: an unimplemented
subcommand is an unrecognised one and exits 1.

The obligations below are numbered R1–R7 and each is a scored group. They
partition the work; nothing in the foundation documents is outside them except
the out-of-scope entry points above.

## 1. R1 — the parse table: precedence, associativity, declared conflicts

`generate` must build an LR table from the grammar and resolve its ambiguities
at generation time, by the rules of `10-grammar-dsl.md` §6:

* `prec.left` and `prec.right` fix associativity, and the resulting nesting is
  visible in the tree (§6.1);
* numeric precedence orders two rules that could both apply, including a unary
  form that must bind tighter than the binary forms around it (§6.1);
* an ambiguity the grammar does not resolve is a **generation failure**, exit 1
  (§6.1, §9), and the same grammar with the pair named in `conflicts` generates
  successfully (§6.2);
* `prec.dynamic` chooses between the branches of a GLR split at parse time,
  and the branch that survives is the one in the printed tree (§6.3).

Acceptance is on the printed tree (`30-tree-output.md` §1) and on the exit
status of `generate` — never on the wording of a diagnostic, and never on the
generated table itself, whose shape is not an interface.

## 2. R2 — the generated lexer

`generate` must build a lexer that implements `10-grammar-dsl.md` §5 and §3:

* longest match, with `word` governing which token wins when a keyword and an
  identifier tie (§5.1, §5.2, §5.4);
* `token.immediate` matching only with no preceding `extras`, so the same two
  tokens separated by a space do **not** form the rule (§4, `token.immediate`);
* `extras` admitted anywhere without being mentioned in a rule, appearing in
  the tree when they produce a visible node (§5.3);
* the regular-expression subset of §3, including `\p{...}` classes, with the
  byte columns of `30-tree-output.md` §2 following from a multi-byte match.

## 3. R3 — tree shape and the normalised grammar

The tree the parser produces, and the two JSON artifacts that describe it, must
follow `10-grammar-dsl.md` §7 and §10:

* hidden rules contribute no node and their children attach to the nearest
  visible ancestor; `field` labels an edge; `alias` renames a node in one
  position; `inline` removes a symbol altogether (§7.1–§7.4);
* `supertypes` changes `src/node-types.json` and nothing else — the supertype
  gets an entry carrying `subtypes`, and it does not appear in any tree (§7.5);
* `src/grammar.json` is the normalised record of what the grammar meant, with
  the key set, the rule-object types and the `optional` → `CHOICE`/`BLANK`
  rewriting of §10, and with `extras` defaulting to a single `\s` pattern when
  the grammar omits the key — omitting it and writing `[]` are different
  grammars, and the difference is observable in a parse.

## 4. R4 — the runtime: recovery, corpus tests, incremental reparse, scanners

`60-runtime.md` in full, plus the runner `40-corpus-tests.md` specifies:

* error recovery produces `ERROR` and `MISSING` nodes rather than abandoning
  the parse, and a tree containing either makes `parse` exit 1 whether or not
  the tree was printed (§2; `20-cli.md` §3; `30-tree-output.md` §3);
* `test` discovers the corpus, compares against the **corpus** rendering —
  which is not the `parse` rendering (`30-tree-output.md` §5) — and separates
  passing from failing tests in what it prints (`40-corpus-tests.md` §3), with
  a failure making the run exit non-zero;
* a repository with neither `test/corpus/` nor `corpus/` has no corpus tests to
  run, and `test` there exits 0 — an absent corpus is not a failure
  (`40-corpus-tests.md` §1, §3);
* an edit followed by an incremental reparse yields exactly the tree a fresh
  parse of the edited text yields (§3; `20-cli.md` §3, `--edit`);
* an external scanner is compiled beside the parser and produces its token
  (§4), and a grammar with a non-empty `externals` and no scanner file still
  generates — the failure appears only when a parser is loaded (§4.1;
  `20-cli.md` §6.4).

## 5. R5 — the query engine

`50-queries.md` in full: the pattern language of §1, including the `_` / `(_)`
distinction (§1.1), fields, alternations, the four effective predicates and the
rule that any other predicate compiles and is inert (§1.9), and **both** output
formats byte for byte — grouped by match (§2) and `--captures` (§3), with their
two different orderings and the multi-row capture that carries `end:` instead
of `text:`.

## 6. R6 — generated artifacts and the command-line surface

`20-cli.md` §2, §3, §6 and §7:

* `generate` writes the documented file set, and writes `src/grammar.json`
  **only** when it was given no `grammar-path`; a parser generated with a path
  argument therefore cannot be loaded from its own directory (§2, §6.4);
* `src/parser.c` is self-contained, includes the generated header, defines the
  single entry point `tree_sitter_<name>`, and — for a grammar with
  `externals` — declares the five scanner functions of `60-runtime.md` §4.2
  without defining them (§2);
* `--prev-abi` lowers the emitted ABI version and does not write
  `src/tree_sitter/parser.h` (§2);
* an existing `package.json` is updated in place: a `main` key is added
  pointing at the Node binding directory, and everything already there,
  including the `tree-sitter` configuration key, survives untouched (§2);
* grammar-path resolution goes through `node`, so `./grammar.js` works where a
  bare `grammar.js` does not, and a path whose extension is neither `.js` nor
  `.json` is rejected (§2);
* the exit statuses of §0, including no subcommand, an unrecognised
  subcommand and an unrecognised flag, and `init-config` and `dump-languages`
  succeeding — both are invoked at this milestone, and both exit 0 (§0, §6.1,
  §7; §0 names the two beside the five fully-specified entry points);
* `init-config` writes `config.json` into the configuration directory —
  `$TREE_SITTER_DIR` when that variable is set and `$HOME/.tree-sitter`
  otherwise — and the file it writes carries the default `parser-directories`
  `$HOME/github`, `$HOME/src` and `$HOME/source` (§6.1);
* those configured directories are where a language search looks, and a
  candidate parser is an immediate subdirectory whose name begins
  `tree-sitter-`; `dump-languages` reports what that search found (§6.1, §6.2,
  §7);
* the compiled library is cached at `$TREE_SITTER_DIR/bin/<name>.so`, keyed by
  the **grammar's** name — not by the repository directory it was generated in,
  and not by the `package.json` name around it (§6.4);
* `parse` rejecting a path it cannot use — absent, a directory, an unmatched
  `--scope`, or a negative glob that leaves the set empty (§3).

## 7. R7 — grammar diagnostics

Every condition in `10-grammar-dsl.md` §9 rejects the grammar with exit 1 and
leaves no parser behind, and the regex subset table of §3 is enforced: anchors,
lookaround, backreferences and character-class set operations are rejected
while the supported constructs beside them generate. Only the rejection and the
exit status are graded; the wording of the diagnostic is yours.

## 8. Acceptance criteria that span two documents

These follow from the foundation documents but from no single section of one,
and each is graded:

1. **A `--prev-abi` parser cannot be loaded.** `generate --prev-abi` succeeds
   and writes `src/parser.c` and `src/node-types.json`, but not
   `src/tree_sitter/parser.h` (`20-cli.md` §2). The compile step puts `src/` on
   the include path and finds no header, so the compilation fails and the
   subcommand that needed the parser exits 1 (`20-cli.md` §6.4). Both halves
   are required: the generate must succeed and the subsequent parse must fail.
2. **`generate <path>` breaks the next `parse` in that directory.** No
   `src/grammar.json` is written, the parser's name cannot be discovered, and
   `parse` in that directory exits 1 with no tree (`20-cli.md` §2, §6.4).
3. **`extras: []` is observable in a parse, not only in the JSON.** With the
   key omitted, whitespace is skipped; with `[]`, the same whitespace is
   unexpected input and the tree carries `ERROR` nodes (`10-grammar-dsl.md`
   §5.3, §10).
4. **A missing external scanner is a load-time failure, not a generate-time
   one.** `generate` exits 0; the `parse` that needs the library exits 1
   (`10-grammar-dsl.md` §8, `60-runtime.md` §4.1).
5. **The two S-expression renderings must both be implemented.** A single
   renderer reused for both fails half of R4: `parse` annotates positions and
   prints a missing token as an ordinary zero-width node, while the corpus
   runner omits positions and writes `(MISSING ";")` and `(UNEXPECTED '?')`
   (`30-tree-output.md` §5).
