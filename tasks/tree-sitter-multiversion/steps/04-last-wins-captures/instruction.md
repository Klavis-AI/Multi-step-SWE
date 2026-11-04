# Step 4 — Last-wins capture precedence (tree-sitter 0.21.0)

## 1. Implement this milestone

Carry your `tree-sitter` implementation from release `0.20.9` to release
**`0.21.0`**. Five things change, and they are specified in
`/app/spec/04-last-wins-captures.md`:

- **capture precedence in the highlighter reverses.** When several patterns of
  a `highlights` query capture the same node, the highlighter now takes the
  name from the pattern that appears **last** in the query file, not the first.
  This is the heart of the milestone. It is a
  rule about position in the file — not specificity, not match order, not
  capture index. The query engine itself is unchanged: `tree-sitter query`
  still reports every capture of every match;
- **two new `generate` validation errors** — an empty string literal in the
  `extras` array, and more than one argument to `token` or `token.immediate`.
  Both are rejected the way every other condition in `10-grammar-dsl.md` §9 is:
  a diagnostic on stderr, exit 1, and no parser left behind;
- **corpus tests can be selected by regular expression** — `tree-sitter test`
  gains `--include <pattern>` and `--exclude <pattern>`, which are regular
  expressions and not substrings. `-f`/`--filter` survives unchanged, and a
  selection that matches nothing still exits 0. §3.1 also changes one line of
  what `test` prints: a corpus **file**'s group name line now appears only
  when a test in that file ran;
- **three changes to the command-line surface** — neither `test` nor
  `highlight` accepts `--apply-all-captures` any more, `parse`'s repeatable edit flag is renamed from
  `-e, --edit` to `--edits`, and an argument the tool cannot recognise now
  exits **2** instead of 1. Read §4.3 before you touch your argument parser:
  status 2 means the invocation was never dispatched, and a subcommand that ran
  and then failed still exits 1;
- **`--config-path <path>`**, accepted by `parse`, `query`, `test`, `highlight`
  and `tags`, names a configuration file to read instead of the one in
  `$TREE_SITTER_DIR`. Because an invocation can now supply a `theme` this way,
  §5.1 also settles something the foundation left open: which colour a
  highlighted region is rendered in. That is the second way the rule in the
  first bullet is observed.

Everything milestones 1–3 established stays required: the generator and the C
runtime, queries and external-scanner serialization, and the highlight and tags
layers. The gate re-exercises all of them — precedence and GLR in the parse
tree, corpus runs, error recovery, the generated artefact set and its ABI, and
the highlight-test machinery — and a failure in any of them means the milestone
is not satisfied.

The hard prerequisite for step 5 is that `tree-sitter test` still discovers and
runs a corpus normally, because step 5 changes how that discovery works.

## 2. Read these local references

- `/app/spec/04-last-wins-captures.md` — this milestone. Normative.
- `/app/spec/00-overview.md` … `60-runtime.md` — the foundation. Still
  normative except where this milestone's document says otherwise; it says so
  explicitly every time, and names what it replaces.
- `/app/spec/01-*.md`, `02-*.md`, `03-*.md` — the earlier milestones, still
  required in full.
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
  corpus-test and query still behaves as milestones 1–3 require. Re-run that
  earlier behaviour yourself — the gate is a tripwire, not a test suite.
