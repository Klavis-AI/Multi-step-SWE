# Step 2 — Queries and scanner serialization (tree-sitter 0.20.6)

## 1. Implement this milestone

Carry your `tree-sitter` implementation from release `0.19.0` to release
**`0.20.6`**. Seven things change, and they are specified in
`/app/spec/02-queries-and-scanner-serialization.md`:

- **the `query` capture line is rewritten, in both output modes.** This is the
  largest single change here, so do not trade the rest of the milestone for it.
  Four things move at once on every capture line: the line gains a **capture index** before the
  name, `end:` is now **always** printed, the text is **always** printed as raw
  source between **backticks** rather than as an escaped double-quoted string,
  and in `--captures` mode the bare `row: <n>` becomes a `start:` point while
  the pattern index becomes a two-column right-aligned field. One asymmetry
  survives: in grouped output a capture spanning several rows still prints
  with no index and no text. Treat the code blocks in §1 and §2 as literal —
  they are compared byte for byte;
- **negated field patterns.** `!field` inside a node pattern constrains the
  match to nodes with **no** child under that field. It composes with positive
  field constraints, may be carried by a captured pattern, and nests;
- **a predicate group may be a branch of an alternation.** `[((identifier) @x
  (#eq? @x "a")) (number) @y]` must compile and match, with the predicate
  constraining only its own branch. The alternation is still one pattern, so
  every capture it reports carries one pattern index. The meanings of the
  predicates themselves do not change;
- **a column-dependent external token is invalidated by an edit on its row.**
  When a scanner uses `get_column` to decide, the token it produced depends on
  its column; an edit earlier on the same row moves that column, so the token
  and every sub-tree containing it must be re-scanned rather than reused. The
  reparsed tree must be exactly the tree a fresh parse gives — the absolute
  contract `60-runtime.md` §3 already states. The dependency is scoped to the
  row: a column-dependent token on an untouched row is still reusable;
- **the corpus file format gains two things.** A delimiter line — either line
  of a header, or the divider — may carry a **suffix** after its run of `=` or
  `-`; the first header's suffix fixes the file's, and every delimiter must
  repeat it identically or it is not a delimiter at all. And in an **expected
  tree**, a line whose first non-blank character is `;` is a comment and is
  discarded before the comparison. Neither touches how a test is *named*;
- **ABI selection changes.** `generate --prev-abi` is withdrawn, and
  `generate --abi <version>` replaces it, taking `13`, `14` or `latest` and
  setting the `LANGUAGE_VERSION` constant the generated `src/parser.c`
  declares. `latest` is 14 at this release. The default is not changed by this
  milestone;
- **`generate --no-bindings`**, which writes the parser and its header but none
  of the binding files.

Everything milestone 1 established stays required: the generator, the lexer,
the tree, the C runtime, error recovery, incremental reparse, the external
scanner contract and the corpus test runner. The gate re-exercises all of them
— precedence and associativity, GLR conflict resolution, `extras`,
`token.immediate`, aliases, fields and inlining, Unicode character classes,
error recovery, a corpus run, and a query that runs at all — and a failure in
any of them means the milestone is not satisfied.

`highlight` and `tags` are **not** exercised at this milestone: no gate check
and no in-scope requirement invokes either subcommand, and no document here places
a requirement on their output. They first matter at milestone 3. If
`WORKSPACE_HANDOFF.md` reads as though they were already delivered, this
paragraph and `TEST_INTERFACE.md` are the ones to believe.

The hard prerequisite for step 3 is that the query engine still compiles
patterns and reports captures, because step 3 builds further query behaviour
on top of it.

## 2. Read these local references

- `/app/spec/02-queries-and-scanner-serialization.md` — this milestone.
  Normative. Its §1 and §2 are compared byte for byte.
- `/app/spec/00-overview.md` … `60-runtime.md` — the foundation. Still
  normative except where this milestone's document says otherwise; it says so
  explicitly every time, and names what it replaces. `50-queries.md` §2 and §3,
  `20-cli.md` §2 and `40-corpus-tests.md` §2.1–§2.3 are the sections it
  touches — and in §2.1 it moves only one of the two marked statements.
- `/app/spec/01-generator-and-runtime.md` — the previous milestone, still
  required in full.
- `/app/TEST_INTERFACE.md` — the build-and-invocation contract your work must
  meet: what `build.sh` must produce, how the binary is called, and
  exactly what is observed and normalised before comparison. Read it again even
  if you read the previous copy.
- `/app/WORKSPACE_HANDOFF.md` — what `/app` carries between milestones and what
  must still be there when you finish.
- `/app/timer.sh` — the step timer; check it at startup and during long work.

## 3. Meta requirements

- The implementation budget is 14,400 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- The built binary is on a clock too, and it is much shorter than yours. Each
  individual check is given **300 seconds** and the whole set of checks
  **1,800 seconds**, both measured after `build.sh` has already finished.
  No single check here needs anything like that, but a `generate`, `parse`,
  `test` or `query` invocation that takes minutes on a small grammar will be
  killed and is a failure. Keep them fast.
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
  corpus-test and query still behaves as milestone 1 requires. Re-run that
  earlier behaviour yourself — the gate is a tripwire, not a test suite.
