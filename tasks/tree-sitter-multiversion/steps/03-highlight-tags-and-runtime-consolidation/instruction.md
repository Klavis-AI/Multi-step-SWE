# Milestone 03 — advance `tree-sitter` to release 0.20.9

You are continuing work on an incremental parser generator with a GLR-capable
C runtime: `tree-sitter`. The previous milestone brought it to release
`0.20.6`. This milestone takes it to release **`0.20.9`**.

Your workspace already holds everything that defines the target:

| where | what |
| --- | --- |
| `workdir/TEST_INTERFACE.md` | the complete contract between your binary and the checker — read it first |
| `workdir/spec/00-overview.md` … `60-runtime.md` | the foundation specification: the grammar DSL, the command line, tree output, the corpus format, the query language and the runtime |
| `workdir/spec/01-*.md` … `03-*.md` | one document per milestone to date, each describing only what that milestone changes |
| your source tree | the implementation as the previous milestone left it |

`workdir/spec/03-highlight-tags-and-runtime-consolidation.md` is this
milestone's document. It is the only one that describes new work; everything
else describes behaviour you are expected to **keep**.

## What to build

`bash /app/build.sh` must leave an executable at `/app/bin/tree-sitter`, and
that binary must satisfy the whole specification — the foundation, every
earlier milestone, and this one.

This milestone changes six things. Each is stated as behaviour in the
milestone document, with the exact invocations and the exact outcomes:

1. **`#any-of?` and `#not-any-of?` query predicates** that actually constrain
   matching, including the argument validation that makes a malformed one a
   compile error rather than a silently ignored predicate.
2. **ABI 14 generated parsers** — the default language version, and the
   primary-state table the generated parser now carries.
3. **`generate --build`, `--debug-build` and `--libdir`**, which let
   `generate` compile the parser it just wrote, and the removal of
   `--no-minimize`.
4. **`query --row-range` and `query --quiet`.**
5. **`parse --xml`, `parse --encoding`, and UTF-16 byte-order-mark detection.**
6. **Capture-application options** on `test` and `highlight`, and
   `highlight --check`.

This environment has no network access, and everything the build needs is already
installed. The upstream project's source, its releases, mirrors and package
registries are unreachable, so a copy cannot be fetched by any route.
Searching for, downloading, copying, wrapping or invoking an existing
`tree-sitter` implementation, or any task-specific answer, is prohibited.
The build image holds a warm crate cache, so a normal dependency resolution
will not need the network.

## What is in scope

* The **gate** re-checks everything the foundation and the earlier milestones
  already established: grammar compilation, precedence and associativity, GLR
  conflict resolution, extras, `token.immediate`, aliases and fields, Unicode
  character classes, error recovery, the corpus test runner, query captures,
  query compile errors, and the external-scanner serialization contract. All of
  it stays required. **A change that breaks something that already worked means
  the milestone is not satisfied**, however much of the new work is finished.
* The six areas above are the new work for this milestone. All six are required,
  so preserving the previous milestone is necessary but never sufficient.

Nothing inspects your source. Only the exit status, stdout, stderr and the
files an invocation was asked to produce are read, after the normalisation
`TEST_INTERFACE.md` describes. Diagnostic wording is never compared — where a
diagnostic matters, the specification says which construct it must name and
nothing more.
