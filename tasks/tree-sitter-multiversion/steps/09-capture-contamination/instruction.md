# Step 9 — Capture isolation and stricter generation (tree-sitter 0.27.0)

## 1. Implement this milestone

Carry your `tree-sitter` implementation from release `0.26.2` to release
**`0.27.0`**. Six things change, and they are specified in
`/app/spec/09-capture-isolation-and-strict-generation.md`:

- **a quantified capture stops at a break in the run it is matching.** This is
  the heart of the milestone.
  Group consecutive siblings into maximal runs that matched the **same** thing:
  each run is one match, a quantified branch's run holds every node in it, and
  a sibling that matches something else — a different branch of an alternation,
  or nothing in the pattern at all — ends the match in progress and starts a
  new one. So `[ (inc)+ (comment) ] @cap` over three `inc` nodes followed by a
  `comment` is **two** matches, not one match of four captures; and
  `(source_file (inc)+ @arg)` over two `inc`, a `comment`, then two more `inc`
  is **two** matches of two captures each, not one match holding the first run
  with the second silently dropped. The number of matches is the number of
  runs. The number of captures a match reports does not change, nor the capture
  line format, nor the order — only where one match ends and the next begins;
- **a supertype whose rule is a single terminal is rejected.** A rule listed in
  `supertypes` whose body resolves to one token — a regular expression, a
  string literal, or a `token(...)` around either — makes `generate` exit 1 and
  write no `src/parser.c`, and the diagnostic must contain the **name the
  grammar author gave that rule**; nothing else about the message is checked. A
  supertype over a choice of other rules is unaffected and must still generate;
- **`test --update` rewrites every corpus file.** Until now only a file holding
  at least one failing test was rewritten and a wholly passing file was left
  byte for byte. Now every file is rewritten and every test in it is
  normalised. Mind what is preserved: the two rule-of-`=` header lines and the
  rule-of-`-` divider keep the width they were written at, and the input and
  the blank lines around it are kept verbatim. Only the block after the divider
  is replaced — by a blank line and the corpus rendering of the tree actually
  produced, one node per line — and the blank line that separates one test from
  the next is preserved, not replaced;
- **generated parsers declare ABI 15 by default.** Milestone 07 made
  `tree-sitter.json` the route to ABI 15; from this release a repository that
  carries **no** `tree-sitter.json` gets `#define LANGUAGE_VERSION 15` as well,
  where it previously got 14;
- **`generate --abi` rejects a version outside the supported range.** Both a
  version below the range and one far above it exit **1** and leave no
  `src/parser.c`. Neither may silently produce a parser, and neither may abort
  the process with any status other than 1;
- **`parse --cst --no-ranges` is laid out flush left.** Dropping the range
  prefix drops the padding that followed it too: the root node starts at column
  0, each level is indented two spaces from its parent, and a childless named
  node keeps its quoted text on its own line after a single space, exactly as
  it does when ranges are printed.

Everything milestones 1–8 established stays required: the generator and the C
runtime, queries and external-scanner serialization, the highlight and tags
layers, last-wins capture precedence, corpus discovery and `build`, the query
API, ABI 15 with reserved words and supertypes, and the `:cst` corpus-test
attribute with explicit parser selection. The gate re-exercises a
representative slice of them — static and dynamic precedence and GLR in the
parse tree, extras, immediate tokens, aliases and inlining, Unicode classes,
error recovery, a corpus run, query compilation and `#any-of?`, the `:skip`
attribute, `build` producing a shared object, the `parse --cst` rendering, a
valid supertype still generating, an external scanner that serializes its state
across two tokens, `highlight` and `tags` over a configured parser directory,
last-wins capture precedence through a `test/highlight/` assertion, and the
`:cst` corpus attribute — and a failure in any of them means the milestone is
not satisfied.

## 2. Read these local references

- `/app/spec/09-capture-isolation-and-strict-generation.md` — this milestone.
  Normative.
- `/app/spec/00-overview.md` … `60-runtime.md` — the foundation. Still
  normative except where a milestone document says otherwise; when one does, it
  says so explicitly and names what it replaces. `50-queries.md` §1.5 and §1.7
  give the alternation and quantifier syntax this milestone's §1 builds on, and
  `40-corpus-tests.md` §5 is the `--update` rule that §3 replaces.
- `/app/spec/01-*.md` … `08-*.md` — the earlier milestones, still required in
  full. In particular an earlier milestone specifies the query capture line
  format this milestone's examples are written in, another specifies
  `parse --no-ranges`, and another the `parse --cst` rendering and its range
  prefix; this milestone changes only how the two combine.
- `/app/TEST_INTERFACE.md` — the build-and-invocation contract your work must
  meet: what `build.sh` must produce, how the binary is called, and
  exactly what is observed and normalised before comparison. Read it again even
  if you read the previous copy.
- `/app/WORKSPACE_HANDOFF.md` — what `/app` carries between milestones and what
  must still be there when you finish.

## 3. Meta requirements

- The implementation budget is 14,400 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- The built binary's runtime budget is separate from yours and is not generous:
  each check is killed after **300 seconds** of wall clock and the whole
  battery after **1800 seconds**. A check that is killed is a failure, so a
  `generate`, `parse`, `test` or `query` that is correct but pathologically
  slow does not pass.
- Work in the cumulative `/app` workspace and preserve the supplied documents
  and handoff files. `/app/build.sh` must stay executable and must produce an
  executable `/app/bin/tree-sitter`; that binary is the only entry point, it
  is built unprivileged with a warm crate cache, and the build must not need the
  network.
- Do not create a prose answer or an alternate entry point, and do not edit
  `/app/spec/`, `/app/TEST_INTERFACE.md` or `/app/WORKSPACE_HANDOFF.md`.
- This environment has no network access, and everything the build needs is already installed. The upstream project's source, its releases, mirrors and package registries are unreachable, so a copy cannot be fetched by any route. Do not search for, download, copy, wrap or invoke
  an existing implementation of this tool or any task-specific answer; do not
  inspect `/tests`, later-step directories, or solution material.
- Do not weaken, filter, rename or disable any check. Only your built binary is
  run, at its documented process boundary; your own tests are never run.
- Local checks before you finish: `bash /app/build.sh` succeeds,
  `/app/bin/tree-sitter --version` exits 0, and a grammar you generate, parse,
  corpus-test and query still behaves as milestones 1–8 require. Write a query
  whose single pattern is an alternation with one quantified branch, run it
  over a file that interleaves the two branches, and count the match groups.
  Do the same for a bare quantifier over children split into two runs by a
  sibling it does not match, and confirm you get one match per run.
  Run `test --update` twice over a corpus file whose every test already passes
  and confirm the first run rewrites it and the second leaves it alone. Re-run
  the earlier behaviour yourself — the gate is a tripwire, not a test suite.
