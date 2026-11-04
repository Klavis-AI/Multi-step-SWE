# Step 8 — CST expectations and explicit parser selection (tree-sitter 0.26.2)

## 1. Implement this milestone

Carry your `tree-sitter` implementation from release `0.25.0` to release
**`0.26.2`**. Six things change, and they are specified in
`/app/spec/08-cst-expectations-and-parser-selection.md`:

- **the `:cst` corpus-test attribute.** A corpus test carrying `:cst` has its
  expected block compared against the line-per-node, range-prefixed rendering
  that `parse --cst` prints, instead of against an S-expression. This is the
  heart of the milestone. The comparison is
  **exact**: the whitespace collapsing, comment stripping and
  conditional-field rules that a normal corpus expectation enjoys are all
  switched off, so every space inside a range prefix and every field prefix is
  significant. A `:cst` test whose expected block holds an S-expression
  therefore fails — which is the opposite of what happened before, when the
  attribute was not recognised at all. `/app/spec/30-tree-output.md` already
  fixes the **two** S-expression renderings this system had; this is a third
  dialect and it departs from both, chiefly by printing anonymous nodes;
- **one change to that rendering.** A named node's kind is followed by a space
  only when the node has no children, so no line of a CST block ends in
  whitespace. Previously every named node got the space, and a named node with
  children ended its line with a trailing one. A `:cst` expectation is
  compared inside your own binary, against the corpus file as written, so
  nothing normalises that trailing space away and an expectation that carries
  one no longer matches;
- **`test --update` writes the CST rendering** for a test carrying `:cst`,
  preserving the attribute line, so a rewritten `:cst` test is still a `:cst`
  test and the corrected file round-trips: update, then run again, and every
  test passes;
- **explicit parser selection for `parse` and `query`** — `--grammar-path
  <dir>` takes the parser from that repository, and `--lib-path <file>` with
  `--lang-name <name>` loads an already-compiled shared library and looks up
  `tree_sitter_<name>` inside it. Either one bypasses all three of the
  language-resolution routes in `20-cli.md` §6.3, so the extension of the file
  being parsed stops mattering and no parser directory need be configured.
  `--grammar-path` does **not** generate: point it at a repository that has
  already been generated, or the invocation exits 1;
- **`generate --no-parser`**, which writes `src/grammar.json` and
  `src/node-types.json` and no parser at all, and **skips parse-table
  construction entirely** — so a grammar with an unresolvable conflict, which
  an ordinary `generate` rejects, succeeds under `--no-parser` and exits 0.
  Errors that arise before table construction still fail the way they always
  did;
- **containment-filtered query ranges** — `query` gains
  `--containing-byte-range` and `--containing-row-range`. The existing
  `--byte-range` and `--row-range` consider a node when it *overlaps* the
  range; the containing forms consider it only when it lies *entirely within*
  the range, so a node straddling either bound is excluded.

This release also reorganises its own source into separate library units. That
is **out of scope, and you should not spend the budget on it**: as
`/app/TEST_INTERFACE.md` says, only the built binary is ever invoked, and
nothing about where your source lives is inspected. Restructure your tree if it
helps you and leave it alone if it does not.

Everything milestones 1–7 established stays required: the generator and the C
runtime, queries and external-scanner serialization, the highlight and tags
layers, last-wins capture precedence, corpus discovery and `build`, the query
API, and ABI 15 with reserved words and supertypes. The gate re-exercises a
representative slice of them — static and dynamic precedence and GLR in the
parse tree, extras, immediate tokens, aliases, Unicode classes, error
recovery, a corpus run, a query, an external scanner that serializes its state,
last-wins capture precedence through `highlight`, reserved words and supertypes,
and the `:skip` and plain range filters of a corpus run — and a failure in any
of them means the milestone is not satisfied.

## 2. Read these local references

- `/app/spec/08-cst-expectations-and-parser-selection.md` — this milestone.
  Normative.
- `/app/spec/00-overview.md` … `60-runtime.md` — the foundation. Still
  normative except where a milestone document says otherwise; when one does, it
  says so explicitly and names what it replaces.
- `/app/spec/01-*.md` … `07-*.md` — the earlier milestones, still required in
  full. In particular the previous milestone specifies `parse --cst` itself,
  including how a range prefix is padded; this milestone reuses that rendering
  and changes only the one detail described above.
- `/app/TEST_INTERFACE.md` — the build-and-invocation contract your work must
  meet: what `build.sh` must produce, how the binary is called, and
  exactly what is observed and normalised before comparison. Read it again even
  if you read the previous copy.
- `/app/WORKSPACE_HANDOFF.md` — what `/app` carries between milestones and what
  must still be there when you finish.

## 3. Meta requirements

- The implementation budget is 14,400 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
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
  corpus-test and query still behaves as milestones 1–7 require. Write one
  corpus test with `:cst` and one without in the same file and confirm both
  pass. Re-run the earlier behaviour yourself — the gate is a tripwire, not a
  test suite.
