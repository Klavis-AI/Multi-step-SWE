# Workspace handoff — milestone 07

This file describes the *workspace*: what `/app` holds, what has to survive to
the next session, and how someone restoring a checkpoint picks the work up. It
does not describe the product — the product is defined by `/app/spec/` and
`/app/TEST_INTERFACE.md`, and where this file and those disagree, they win.

## What `/app` contains right now

- `spec/` — the specification you implement against. Author-owned, replaced at
  the start of every milestone, and **never edit it**. It grows: each milestone
  adds its own document beside the ones already there, so the copy in front of
  you is the whole contract to date.
- `TEST_INTERFACE.md` — the contract the hidden verifier builds and invokes
  against. Also refreshed every milestone; read it again even if you read the
  previous copy, because what is observed can change.
- `WORKSPACE_HANDOFF.md` — this file.
- `timer.sh` — an advisory countdown for this milestone's budget. Nothing reads
  its output; it is for you.
- Your own source tree — the cumulative implementation you and the sessions
  before you have built, carried forward exactly as the previous session left
  it, together with whatever notes it chose to leave behind.

## Milestones already delivered

Each of these was delivered by an earlier session and **must still hold**. This
milestone is added to them, not substituted for them.

1. **Generator, runtime and CLI** — The grammar DSL and its combinators, rule flattening, inlining and keyword extraction, token regexes compiled to a DFA, LR(1) item sets with state merging, conflict resolution by associativity and by static and dynamic precedence, declared `conflicts` producing GLR splits, per-state lexer generation, the `parser.c` emitter, and the C runtime behind it -- shared subtrees, the GLR stack, error recovery with `MISSING` and `ERROR`, the tree cursor, incremental reparse and external scanners -- plus the query engine and the `generate`, `parse`, `test` and `query` subcommands. `highlight`, `tags` and `build` are explicitly out of scope at milestone 1 (`spec/01` §0) and arrive later.
2. **Query predicates and external-scanner serialization** — Predicate and quantifier handling in the query engine; an external-scanner serialization contract that the runtime enforces across an incremental reparse; lexer and error-recovery corrections; and `node-types.json` generation.
3. **Highlight, tags, and the consolidated runtime** — The highlight and tags layers in their matured form; incremental reparse and changed-range corrections; and the query, node and cursor API completed in the C runtime rather than above it.
4. **Last-wins capture precedence** — When several captures apply to one node, the last matching capture wins instead of all of them applying; the empty string in `extras` is an error; and `token($.rule)` is an error.
5. **Corpus discovery, `build`, and test attributes** — Corpus discovery narrowed to `test/corpus`; the `build` subcommand carrying the compilation that `generate` used to do; and the corpus test attributes `:skip`, `:error`, `:fail-fast`, `:language` and `:platform`.
6. **Reverted single-rule interning, and the query API addition** — Single-element `seq` and `choice` are no longer interned, which changes the generated parse table of every grammar that contains one; invalid supertypes are diagnosed precisely; external scanners may use the logger; and `ts_query_end_byte_for_pattern` is available.

## Preservation rules

1. `/app/build.sh` must exist, be executable, and produce an executable
   `/app/bin/tree-sitter`. That is the only entry point the verifier has. It is
   run unprivileged, with a warm crate cache, and it must not need the network.
2. Do not delete or edit `spec/`, `TEST_INTERFACE.md`, `WORKSPACE_HANDOFF.md`
   or `timer.sh`. The next milestone's overlay replaces them; edits are lost,
   and a deletion can strand the next session without its contract.
3. Everything else under `/app` is yours. Layout, crate names, module structure
   and dependency choices are all your call except where `TEST_INTERFACE.md` or
   `spec/` pins them — the verifier only ever invokes the built binary at its
   documented process boundary.
4. Leave the tree in a state that builds. The next session starts from exactly
   what you leave, with no chance to ask you what you meant.

## The gate

This milestone's verifier carries a hard gate: a small set of representative
paths through the inherited milestones above, plus proof that the binary builds
and runs. The gate earns nothing — it can only zero the milestone. A change
that delivers this milestone's capability while breaking an earlier one fails
the whole thing.

The gate is deliberately much narrower than the earlier milestones' own
verifiers, and those never run again. So re-run the earlier behaviour yourself
before you finish rather than trusting the gate to catch a regression: it is a
tripwire, not a test suite.

## Handing off

Whatever notes you want the next session to have, put them in a file of your
own choosing under `/app` — a `NOTES.md`, comments, anything. It will be
carried forward verbatim. Do not put them in this file; it is overwritten.
