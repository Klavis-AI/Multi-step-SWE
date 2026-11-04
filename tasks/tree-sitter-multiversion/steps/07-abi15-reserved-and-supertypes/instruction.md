# Step 7 — ABI 15, reserved words and supertype queries (tree-sitter 0.25.0)

## 1. Implement this milestone

Carry your `tree-sitter` implementation from release `0.24.0` to release
**`0.25.0`**. Seven things change, and they are specified in
`/app/spec/07-abi15-reserved-and-supertypes.md`:

- **generated parsers declare ABI 15 — when the repository asks for it.** A
  repository carrying a `tree-sitter.json` (in its own directory or any
  ancestor) is generated at ABI 15; one without is generated at ABI 14, with a
  warning on stdout, and `generate` **no longer migrates** a `package.json`
  `"tree-sitter"` key into a `tree-sitter.json` — that milestone-06 behaviour is
  overturned. `--abi 14` and `--abi 15` choose explicitly. An ABI 15
  `src/parser.c` defines `SUPERTYPE_COUNT` and wires `.supertype_symbols`,
  `.supertype_map_slices`, `.reserved_words`, `.name` and `.metadata` into the
  language struct; every parser this release writes defines `SUPERTYPE_COUNT`,
  an ABI 14 one with the value `0`;
- **reserved words.** The grammar gains a `reserved` option — an object of
  named token sets, `global` applying by default — and a `reserved(set, rule)`
  combinator that swaps the set in force for the extent of a rule. A reserved
  word is excluded from the keyword-extraction token, so `let a = if;` becomes
  an `ERROR`. The sets are recorded under a `reserved` key in
  `src/grammar.json` at either ABI, but the exclusion itself is enforced only
  by an ABI 15 parser;
- **`(supertype/subtype)` query patterns are verified.** The pattern matches a
  subtype node derived through the named supertype, and both halves are
  checked when the query compiles: a subtype the supertype does not have, or a
  left-hand name that is not a declared supertype, is a compile error — exit 1,
  no captures. Against an ABI 14 parser the pattern is not verified and
  matches nothing;
- **`MISSING` nodes are queryable** — `(MISSING)`, `(MISSING ";")` and
  `(MISSING identifier)` — and a captured missing node reports a zero-width
  range with empty text;
- **`test --file-name <name>`** runs only the corpus files whose base name,
  extension included, is exactly `<name>`, at any depth; a name that matches
  nothing runs nothing and exits 0;
- **`parse --cst`** prints a concrete syntax tree — anonymous nodes included —
  in the aligned `row:col - row:col` line format §6 fixes byte for byte;
- **`generate -o <dir>` / `--output <dir>`** writes `parser.c`,
  `node-types.json`, `grammar.json` and `tree_sitter/parser.h` directly into
  `<dir>`, creating it, and writes nothing under `src/`.

Everything milestones 1–6 established stays required: the generator and the C
runtime, queries and external-scanner serialization, highlight and tags,
last-wins capture precedence, corpus discovery and `build`, and the
`tree-sitter.json` configuration and interning behaviour of milestone 6. The
gate re-exercises the inherited tree — precedence, associativity and GLR in the
parse tree, extras, immediate tokens, aliases and fields, Unicode classes,
error recovery, the corpus runner and query captures; query predicates and
quantifiers and external-scanner serialization from milestone 2; the tags
renderer from milestone 3; last-wins capture precedence, through a
`test/highlight` assertion, from milestone 4; `build` and the narrowing of
corpus discovery to `test/corpus` from milestone 5; and, from milestone 6, a
hidden rule whose body is a single string literal keeping its own anonymous
token — and a failure in any of them means the milestone is not satisfied.

## 2. Read these local references

- `/app/spec/07-abi15-reserved-and-supertypes.md` — this milestone. Normative.
- `/app/spec/00-overview.md` … `60-runtime.md` — the foundation. Still
  normative except where this milestone's document says otherwise; it says so
  explicitly every time, and names what it replaces.
- `/app/spec/01-*.md` … `06-*.md` — the earlier milestones, still required in
  full. Milestone 02 §6.1 (`--abi`), 03 §2 (ABI 14 and the wired-table
  convention) and 06 §3 (`tree-sitter.json`) are the ones this milestone
  builds on directly.
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
  `/app/bin/tree-sitter --version` exits 0, a repository with a
  `tree-sitter.json` generates at ABI 15 and one without at ABI 14, a
  `reserved` grammar rejects its reserved word in a parse and in a corpus run,
  a bad `(supertype/subtype)` pattern fails to compile, `(MISSING ";")`
  captures a missing semicolon, `test --file-name` selects one corpus file,
  `parse --cst` prints the aligned rendering, `generate -o` writes into the
  named directory — and a grammar you generate, parse, corpus-test and query
  still behaves as milestones 1–6 require. Re-run that earlier behaviour
  yourself — the gate is a tripwire, not a test suite.
