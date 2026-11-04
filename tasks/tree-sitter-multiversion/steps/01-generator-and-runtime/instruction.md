# Step 1 — generator and runtime

## 1. Implement this milestone

Build `tree-sitter` release **0.19.0**: an incremental parser generator and the
runtime that runs what it generates, reached through one command-line
executable. `/app` is empty apart from the specification, so this milestone is
written from nothing and every later milestone extends it.

In scope for this milestone:

- **The generator.** Read a grammar written in the JavaScript DSL, build an
  LR parse table and a lexer from it, and emit `src/parser.c`,
  `src/node-types.json`, `src/grammar.json`, `src/tree_sitter/parser.h` and the
  binding files. Resolve ambiguity at generation time with precedence and
  associativity; defer it to a GLR split at parse time when the grammar
  declares the conflict; reject a grammar that leaves an ambiguity unresolved.
- **The lexer.** Longest match, token precedence, `token.immediate`, `extras`,
  keyword extraction through `word`, and the finite-automaton subset of the
  regular-expression language — including the constructs that must be
  rejected.
- **The tree.** A concrete syntax tree that tiles the input, with hidden rules,
  fields, aliases, inlining and supertypes shaping what is visible and what is
  recorded in `src/node-types.json`.
- **The runtime.** Compile the generated C on demand, cache the library, parse,
  recover from errors into `ERROR` and `MISSING` nodes instead of abandoning
  the parse, reparse an edited buffer incrementally to exactly the tree a fresh
  parse would give, and call a hand-written external scanner through the
  five-function contract.
- **The corpus test runner.** Discover corpus files — including the case where
  a repository has none, which is a success — compare against the
  corpus S-expression rendering — which is *not* the one `parse` prints — and
  report which tests passed and which failed.
- **The query engine.** The pattern language, its predicates, and both output
  formats byte for byte.
- **The command line.** Every exit status, every documented flag of the
  subcommands below, how a parser is located and compiled, and what each
  subcommand leaves on disk.

Exercised at this milestone: `--version`, `generate`, `parse`, `test` and
`query`, whose behaviour the specification fixes in full. Two further
subcommands are invoked here as well: `init-config` and `dump-languages`. Both
must exist and both must exit 0 — `init-config` writing the configuration file
`20-cli.md` §6.1 describes, with the default `parser-directories` that section
lists, and `dump-languages` listing on stdout the languages found by searching
them. Neither one's output *layout* is fixed, so format both however you like;
what is compared is the configuration file `init-config` leaves on disk and the
fact that a discovered language is named on `dump-languages`' stdout by the
`scope` its `package.json` declares (`20-cli.md` §6.2).
`TEST_INTERFACE.md`'s table records the same milestone for all seven.

Out of scope for this milestone:

- **`highlight`, `tags` and `build`.** `TEST_INTERFACE.md` says which milestone
  first invokes each; none of the three is invoked here, and this step places
  no requirement on them.
- **`build-wasm` and `web-ui`.** Named in the specification for completeness;
  neither is invoked at this milestone, and neither needs behaviour of its
  own. `TEST_INTERFACE.md`'s milestone table records that `build-wasm` gets
  exactly one invocation later, to confirm it is not a subcommand.
- **The wording of any diagnostic.** Error messages are never compared. What is
  fixed is that a bad input is rejected and which exit status says so.
- **Timing, counts and generated-table shape.** The summary line `parse`
  prints, the numbers `-s` and `-t` add, the XML of `-x`, the trace of `-d`,
  the HTML of `-D`, and the internal structure of the parse table are all
  unspecified. Format them however you like.

## 2. Read these local references

**Normative semantics** — all under `/app/spec/`, the specification of release
0.19.0:

- `00-overview.md` — start here; the pipeline, the object model, and the
  vocabulary the other six documents use with exactly those meanings.
- `10-grammar-dsl.md` — the grammar language: `grammar()`, every combinator,
  the supported regular-expression subset, precedence, associativity, declared
  conflicts, tree shaping, `externals`, the rejection list, and the normalised
  grammar JSON.
- `20-cli.md` — every subcommand: arguments, flags, exit status, which stream
  carries what, what lands on disk, and how a parser is found and compiled.
- `30-tree-output.md` — the printed tree, byte for byte, and the **second**
  S-expression rendering the corpus runner uses.
- `40-corpus-tests.md` — the corpus file format, where corpus files live, and
  what `test` prints.
- `50-queries.md` — the query language and the exact lines `query` prints in
  each of its two output modes.
- `60-runtime.md` — incremental reparse, error recovery, and the external
  scanner contract.
- `01-generator-and-runtime.md` — this milestone: what it owes, what is out of
  scope, and the acceptance criteria that follow from two documents at once.

`10-grammar-dsl.md` and `30-tree-output.md` fix the two ends of the pipeline;
read them before the rest. `30-tree-output.md` and `50-queries.md` are compared
byte for byte, so treat their code blocks as literal.

**Additional files:**

- `/app/TEST_INTERFACE.md` — the build-and-invocation contract your work must
  meet: the build script, the binary's path, the environment each
  invocation runs in, what is observed, and the normalisation applied before
  anything is compared. Read it before writing code.
- `/app/WORKSPACE_HANDOFF.md` — what `/app` contains, what must survive, and
  how a fresh session picks the work up.
- `/app/timer.sh` — the step timer; check it at startup and during long work.

## 3. Meta requirements

- The implementation budget for this step is 14,400 seconds. Check
  `bash /app/timer.sh`; the configured timeout is authoritative and resets
  for this step.
- `/app/build.sh` must exist, be executable, build from a clean checkout with
  no warm build directory, and leave an executable at exactly
  `/app/bin/tree-sitter`. It is run unprivileged from an arbitrary working
  directory with a 1800 second timeout. It may not ship a prebuilt binary.
- Work in the cumulative `/app` workspace. Preserve `spec/`,
  `TEST_INTERFACE.md`, `WORKSPACE_HANDOFF.md` and `timer.sh`. Everything else
  under `/app` is yours: language, layout, crate names and dependencies are all
  your choice. Do not create a prose answer or an alternate entry point.
- This environment has no network access, and everything the build needs is already installed. The upstream project's source, its releases, mirrors and package registries are unreachable, so a copy cannot be fetched by any route. Searching for, downloading, copying, wrapping or invoking
  an existing `tree-sitter` implementation, or any task-specific answer, is
  prohibited. The build image holds a warm crate cache, so a normal
  dependency resolution will not need the network.
- Do not inspect `/tests`, later-step directories or solution material, and do
  not edit them or weaken, filter, rename or disable tests.
- Before finishing, leave the tree in a state that builds, and leave a handoff
  note for the next fresh session as `/app/WORKSPACE_HANDOFF.md` describes.
