# Milestone 05 — advance `tree-sitter` to release 0.22.6

You are continuing work on an incremental parser generator with a GLR-capable
C runtime: `tree-sitter`. The previous milestone brought it to release
`0.21.0`. This milestone takes it to release **`0.22.6`**.

Your workspace already holds everything that defines the target:

| where | what |
| --- | --- |
| `workdir/TEST_INTERFACE.md` | the complete contract between your binary and the checker — read it first |
| `workdir/spec/00-overview.md` … `60-runtime.md` | the foundation specification: the grammar DSL, the command line, tree output, the corpus format, the query language and the runtime |
| `workdir/spec/01-*.md` … `05-*.md` | one document per milestone to date, each describing only what that milestone changes |
| your source tree | the implementation as the previous milestone left it |

`workdir/spec/05-corpus-discovery-build-and-attributes.md` is this milestone's
document. It is the only one that describes new work; everything else describes
behaviour you are expected to **keep**.

## What to build

`bash /app/build.sh` must leave an executable at `/app/bin/tree-sitter`, and
that binary must satisfy the whole specification — the foundation, every
earlier milestone, and this one.

This milestone changes five things. Each is stated as behaviour in the
milestone document, with the exact invocations and the exact outcomes:

1. **Corpus discovery narrows to `test/corpus`.** The fallback to a top-level
   `corpus/` directory is removed, so a repository that has only `corpus/` runs
   no tests at all and still exits 0.
2. **A `build` subcommand**, which compiles a parser repository into a shared
   library at a path you choose, with `--output` and `--reuse-allocator` and a
   default output name derived from the directory it was pointed at.
3. **Corpus test attributes** — `:skip`, `:error`, `:fail-fast`,
   `:platform(...)` and `:language(...)` on their own lines inside a test
   header, each changing whether and how that test runs. Where an attribute
   prevents a test from running or redefines its outcome, that test is printed
   with neither marker and so is in neither the passed nor the failed set; a
   test an attribute still lets run — a matching `:platform`, a passing or
   failing `:fail-fast` — passes or fails exactly as an ordinary test does.
   The milestone document is the authority on which case each attribute is.
4. **`generate` writes `src/tree_sitter/alloc.h` and
   `src/tree_sitter/array.h`** alongside `parser.h`, so an external scanner can
   allocate through the runtime's allocator and use the runtime's array type.
5. **Regular-expression preprocessing is removed.** A `{` in a pattern is
   always a repetition quantifier and must be a valid one; a grammar whose
   pattern uses a brace any other way is now rejected, with a diagnostic naming
   the rule.

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
  character classes, error recovery, the corpus test runner and its preference
  for `test/corpus`, and the query capture listing. All of it stays required.
  **A change that breaks something that already worked means the milestone is
  not satisfied**, however much of the new work is finished.
* The five areas above are the new work for this milestone. All five are
  required, so preserving the previous milestone is necessary but never
  sufficient.

Nothing inspects your source. Only the exit status, stdout, stderr and the
files an invocation was asked to produce are read, after the normalisation
`TEST_INTERFACE.md` describes. Diagnostic wording is never compared — where a
diagnostic matters, the specification says which construct it must name and
nothing more.
