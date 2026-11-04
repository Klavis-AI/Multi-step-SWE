# Step 6 — The interning revert and the grammar configuration file (tree-sitter 0.24.0)

## 1. Implement this milestone

Carry your `tree-sitter` implementation from release `0.22.6` to release
**`0.24.0`**. Five things change, and they are specified in
`/app/spec/06-interning-revert-and-query-api.md`:

- **a hidden rule whose whole body is a single string literal keeps its own
  token.** Until now such a rule was folded into its referrer and the literal
  vanished; from this release the literal becomes an ordinary anonymous node
  type named by its own text, so it is recorded in `src/node-types.json` and
  can be matched by a quoted query pattern — nested under its parent, at the
  top level of a query, and beside a named sibling. This is the heart of the
  milestone. Mind the boundary: a hidden rule
  wrapping a single **regular expression** is still folded away and still
  contributes no node type, so a query naming it is still a compile error. The
  printed tree does not change either way, because anonymous nodes are not
  printed;
- **a parser repository configures itself through `tree-sitter.json`.** The
  `"tree-sitter"` key of `package.json` moves into a new file at the
  repository root, carrying `grammars`, `metadata` and `bindings`, and
  `generate` performs that migration in place — writing the new file and
  removing the key, leaving the rest of `package.json` alone. A repository
  with a `tree-sitter.json` and no `package.json` is complete, and `generate`
  no longer creates one;
- **`generate` stops writing language bindings.** No `binding.gyp`, no
  `Cargo.toml`, no `bindings/` tree, and no `"main"` key added to
  `package.json`. It writes `src/` and `tree-sitter.json`, and that is all.
  This replaces part of `20-cli.md` §2, so read §3.3 of the milestone document
  before you touch your generator's output stage;
- **`generate` warns about single-element `seq` and `choice` rules.** A rule
  whose body is a `seq(...)` or `choice(...)` holding exactly one element, and
  that element a string literal or a regular expression, gets a diagnostic on
  stderr naming the rule. It is a warning: generation still succeeds and the
  exit status is still 0. A single element that is a *rule reference* is not
  reported;
- **five changes to the command-line surface** — `test` loses `-f`/`--filter`
  and gains `--overview-only`, `build` loses `--internal-build` and gains
  `-0`/`--debug`, `parse` gains `-r`/`--rebuild`, the `build-wasm` subcommand
  is removed, and `parse --no-ranges` prints the tree with every position
  annotation omitted and nothing else about the rendering changed.

Everything milestones 1–5 established stays required: the generator and the C
runtime, queries and external-scanner serialization, the highlight and tags
layers, last-wins capture precedence, corpus discovery under `test/corpus/`,
the `build` subcommand and the corpus-test attributes. The gate re-exercises a
broad slice of it — precedence and associativity, the GLR split, `extras`,
`token.immediate`, aliases and fields, Unicode classes, error recovery,
corpus runs and their attributes, query compilation and capture output,
regex-by-name test selection, the highlight and tags layers, external-scanner
serialization, and the `build` subcommand — and a failure in any of them means
the milestone is not satisfied.

Two obligations are worth calling out because this milestone touches the code
around them and a regression there is easy to miss: the printed `parse`
rendering with its position annotations must be unchanged when `--no-ranges`
is *not* given, and a corpus run must still discover, execute and report its
tests exactly as before.

## 2. Read these local references

- `/app/spec/06-interning-revert-and-query-api.md` — this milestone.
  Normative.
- `/app/spec/00-overview.md` … `60-runtime.md` — the foundation. Still
  normative except where this milestone's document says otherwise; it says so
  explicitly every time, and names what it replaces.
- `/app/spec/01-*.md` … `05-*.md` — the earlier milestones, still required in
  full.
- `/app/TEST_INTERFACE.md` — the build-and-invocation contract your work must
  meet: what `build.sh` must produce, how the binary is called, and
  exactly what is observed and normalised before comparison. Read it again
  even if you read the previous copy.
- `/app/WORKSPACE_HANDOFF.md` — what `/app` carries between milestones and
  what must still be there when you finish.

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
  corpus-test and query still behaves as milestones 1–5 require. Re-run that
  earlier behaviour yourself — the gate is a tripwire, not a test suite.
