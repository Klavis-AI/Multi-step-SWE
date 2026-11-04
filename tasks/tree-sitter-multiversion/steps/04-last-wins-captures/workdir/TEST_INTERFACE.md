# Test interface

This file is the complete contract between your implementation and the checker.
It does not change between milestones. Everything the checker touches is listed
here; nothing else about your implementation is inspected, so how you structure
your source, which language you write it in, and which libraries you use are
entirely your choice.

## What you must produce

### 1. `/app/build.sh` — an executable build script

The checker runs it as an unprivileged user, from an arbitrary working
directory, with a **1800 second** timeout:

```sh
bash /app/build.sh
```

It must succeed from a clean checkout of your source. `target/` (and any other
build output) is **removed before the checks run**, so the script has to do a real
build every time — it cannot assume a warm cache and it cannot ship a
prebuilt binary.

The checker container has a Rust toolchain, a C/C++ toolchain (`cc`, `clang`),
`node` 22, `python3`, `git` and `make`. This environment has no general network access. The image already holds a
warm crate cache and cargo resolves from it offline, so a build that resolves
normal Rust dependencies will not need the network.

### 2. `/app/bin/tree-sitter` — an executable

`build.sh` must leave an executable at exactly this path. Every check below is
an invocation of this one binary. Nothing else in `/app` is read by the checker.

## How the checker invokes it

Each check runs in a **fresh scratch directory** containing a *parser
repository* staged from the checker's own fixture corpus, plus any extra files
that check writes. Your binary is then invoked one or more times with that
directory as the working directory. A parser repository is:

```text
<scratch>/
  grammar.js              the grammar, in the DSL described in spec/10-grammar-dsl.md
  package.json            usually carries a "tree-sitter" key; a repository
                          without one is configured from src/grammar.json
                          instead (spec/20-cli.md §6.2)
  test/corpus/*.txt       corpus tests, in the format in spec/40-corpus-tests.md
  src/                    created by `generate`
```

Every invocation runs with:

| variable | value |
| --- | --- |
| `NO_COLOR` | `1` |
| `TERM` | `dumb` |
| `CLICOLOR` | `0` |
| `HOME` | a writable scratch home; some checks point it elsewhere |
| `TREE_SITTER_DIR` | `$HOME/.tree-sitter`, unless a check sets it itself |

Both are read from the environment on **every** invocation, and neither may be
hardcoded or cached across runs. A check is free to hand one invocation a
different `HOME` — including a *relative* path, which resolves against that
invocation's working directory — and to set `TREE_SITTER_DIR` independently of
it. Where the tool then reads and writes its configuration, and where it caches
a compiled parser, follows from `spec/20-cli.md` §6.1 and §6.4 applied to the
values actually in the environment.

Your binary may write anywhere under the scratch directory, `$HOME` and
`/tmp`. It must not require anything outside them.

## What is observed

Only these, and only after normalisation:

* the **exit status** of each invocation;
* **stdout** and **stderr**, as text;
* the **existence and contents of specific files** the invocation was asked to
  produce (for example `src/parser.c`, `src/node-types.json`).

### Normalisation applied before any comparison

Applied to all captured output, and (for the same three rules) to the contents
of any file whose text a check compares **exactly**, so none of it can affect
the result of a check. A file compared by *substring* is not normalised — the substring is
matched against the file as written:

* ANSI escape sequences are stripped — you may colour your output freely;
* the `\t<n> ms\t` timing column that `parse` prints on its summary line is
  removed, and no check asserts on any duration, byte rate or parse count;
* trailing whitespace is stripped from every line, and trailing blank lines are
  dropped.

For `tree-sitter test`, the checker reads **only** which test names passed and
which failed. It recognises both `✓ name` and `1. ✓ name` forms, and ignores
everything else the runner prints — headers, separators, summaries, progress.
A line beginning `✓ ` or `✗ ` is therefore a result line: do not use those two
glyphs for anything else, and do not print a name that did not run.

Nothing asserts on wall-clock time, memory, absolute paths, hostnames, process
ids, or the order of anything the specs do not fix an order for.

### Time allowed

Speed is not part of the requirement, but the checker will not wait forever, so
the limits are part of the contract:

| limit | value |
| --- | --- |
| `bash /app/build.sh` | 1800 s |
| any one check's invocations of your binary | 300 s |
| all of a milestone's checks together | 1800 s |

A check that exceeds its own limit is killed and recorded as failed; if the
aggregate budget runs out, every check that had not yet run is recorded as
failed without being run. These are generous — the slowest single operation any
check performs is one `generate` plus one C compile of a small grammar — and an
implementation of ordinary speed will not come near them. They are stated so
that a milestone lost to a timeout is never a surprise.

## Subcommands the checks use

| subcommand | used from milestone |
| --- | --- |
| `--version` | 1 (proof of life; must exit 0) |
| `generate` | 1 |
| `parse` | 1 |
| `test` | 1 |
| `query` | 1 |
| `highlight` | 3 |
| `tags` | 4 |
| `build` | 5 |
| `build-wasm` | 6 — invoked only to confirm it is **not** a subcommand |
| `init-config` | 1 |
| `dump-languages` | 1 |

The milestone column is the first one whose checks invoke the subcommand; a
later milestone may invoke it too. A subcommand absent from this table is still
required if `spec/` specifies it — the table records what the checks reach for,
not what the CLI must provide.

Their required behaviour is specified in `spec/`, which is the authority for
what each one must do. A later milestone may change the behaviour of a
subcommand an earlier one introduced; when it does, the milestone's own
document says so explicitly and the change is deliberate.

## Scope

Each milestone has two parts.

* **The gate** covers the build contract above and the behaviour that earlier
  milestones established. It adds no new requirement of its own, but breaking
  something that already worked means the milestone is not satisfied, however
  much of the new work you finished.
* **The new work** is organised into the milestone's check groups and covers
  only behaviour introduced or changed *by that milestone*. A solution that
  preserves everything and adds nothing satisfies none of the milestone's new
  work.

`/logs/verifier/reward_details.json` records every group and every individual
check with the reason it failed.
