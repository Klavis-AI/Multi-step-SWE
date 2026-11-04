# Third-Party Notices — evm-fork-chain

Benchmark task that builds a C11 Ethereum EVM across fourteen hard-fork steps,
carrying go-ethereum's `t8n`/`t9n`/`b11r` tooling interface from Frontier to
Osaka and grading it against the official Ethereum test fixtures.

## Upstream project

| | |
|---|---|
| Project | go-ethereum (EVM tooling documentation and command-line test fixtures) |
| URL | https://github.com/ethereum/go-ethereum |
| Pin | tag `v1.16.6`, commit `386c3de6c45f3e185279e6760a17f88fb98dc81a` (reported by the captured binary as `evm-geth version 1.16.6-stable-386c3de6`) |
| Project | Ethereum Improvement Proposals (EIPs) |
| URL | https://github.com/ethereum/EIPs |
| Pin | commit `068e50ad2afa94c3bd967ce584867fb27d371c01` |
| Project | blst (BLS12-381 library) |
| URL | https://github.com/supranational/blst |
| Pin | commit `de54cd4684a3adba193a6c50ca5861c8c32c3b8a` (2026-08-18) |
| Project | Ethereum Yellow Paper |
| URL | https://github.com/ethereum/yellowpaper |
| Pin | Shanghai version `efc5f9a` (2025-02-04); PDF sha256 `d67c009c5d5a0542c8288aee3c4d470b3e2dbe6e664f72f00011543608477fa9` |
| Project | ethereum/tests and execution-spec-tests (fixture corpora) |
| URL | https://github.com/ethereum/tests and https://github.com/ethereum/execution-spec-tests |
| Pin | no commit is recorded in the task tree; every corpus case names its upstream path in its `src` field (`ethtests/…`, `legacytests/…`, `fixtures/fixtures_stable/…`) |

Bundled or derived material:

- `environment/app/SPEC/evm-tool-README.md` is a byte-for-byte copy of
  go-ethereum `cmd/evm/README.md` at `v1.16.6`.
- `environment/app/SPEC/evm-tool-testdata/` (directories `1`–`33` and
  `statetest.json`) matches go-ethereum `cmd/evm/testdata/` at `v1.16.6`;
  sampled files are byte-identical.
- `environment/app/SPEC/evm-tool-usage.txt` is the command usage captured from
  `evm-geth` 1.16.6.
- `environment/app/SPEC/ethereum-yellowpaper.pdf` and
  `environment/docs/yellowpaper/` hold the Yellow Paper.
- Each step's `workdir/SPEC/eips/eip-*.md` is an unmodified EIP text from the
  pinned EIPs commit.
- Each step's `solution/evm/vendor/blst/` is a vendored subset of blst at the
  pinned commit; `VENDORED.txt` records the subset and the build recipe.
- Each step's `tests/corpus/data/` holds the fixture inputs derived from the
  upstream Ethereum test corpora, indexed by `tests/corpus/cases.jsonl`.
- The C EVM implementation, step instructions, runners and verifiers are
  task-authored.

## License table

| SPDX ID | File under `LICENSES/` | Covers |
|---|---|---|
| Apache-2.0 | `Apache-2.0.txt` | blst at `de54cd4`, vendored under `steps/*/solution/evm/vendor/blst/` (exact upstream `LICENSE`) |
| CC0-1.0 | `CC0-1.0.txt` | EIPs at `068e50ad`, copied under `steps/*/workdir/SPEC/eips/` (exact upstream `LICENSE.md`) |
| CC-BY-SA-4.0 | `CC-BY-SA-4.0.txt` | Ethereum Yellow Paper, Shanghai version `efc5f9a`, under `environment/app/SPEC/` and `environment/docs/yellowpaper/` (exact upstream `LICENCE.md`) |
| GPL-3.0 | `GPL-3.0.txt` | go-ethereum `cmd/evm` README, usage text and testdata at `v1.16.6` (`environment/app/SPEC/`) |
| MIT | `MIT-ethereum-tests.txt` | Fixtures whose `src` begins `ethtests/` or `legacytests/`, from ethereum/tests (exact upstream `LICENSE`) |
| MIT | `MIT-execution-spec-tests.txt` | Fixtures whose `src` begins `fixtures/fixtures_stable/`, from execution-spec-tests (exact upstream `LICENSE`) |

go-ethereum licenses its `cmd` directory under GPL-3.0 and the rest of the
library under LGPL-3.0. Only `cmd/evm` documentation and testdata are copied
into this task; no go-ethereum library source is bundled, so the LGPL-3.0 text
is not included.

## Modifications made by the benchmark

- blst is vendored as a subset: the public C headers, `src/`, and the ELF
  assembly are kept; language bindings, test vectors, documentation images,
  non-ELF object formats and the Perl assembly generators are dropped. The
  retained files are verbatim copies, as recorded in
  `steps/01-frontier/solution/evm/vendor/blst/VENDORED.txt`.
- The Yellow Paper PDF is converted to plain text with `pdftotext -layout` for
  offline search; the PDF is shipped unmodified.
- The upstream Ethereum fixtures are converted into the task's per-case
  directory layout with a `cases.jsonl` index; the underlying transactions,
  pre-state, environment and expected results are taken from the upstream
  fixture JSON.
- EIP texts and go-ethereum documentation/testdata are copied without textual
  changes.
- The C EVM, the step ladder, the build system, the task instructions, the
  corpus runner and the verifiers are task-authored.

## Third-party dependencies bundled in the task

| Component | Version | License | Bundled path |
|---|---|---|---|
| blst | commit `de54cd4` | Apache-2.0 | `steps/*/solution/evm/vendor/blst/` (`LICENSE`, `VENDORED.txt`) |
| go-ethereum `cmd/evm` | `v1.16.6` | GPL-3.0 | `environment/app/SPEC/evm-tool-README.md`, `evm-tool-usage.txt`, `evm-tool-testdata/` |
| ethereum/tests | unpinned in tree | MIT | `steps/*/tests/corpus/data/` and `cases.jsonl` |
| execution-spec-tests | unpinned in tree | MIT | `steps/*/tests/corpus/data/` and `cases.jsonl` |
| Ethereum Yellow Paper | Shanghai `efc5f9a` | CC-BY-SA-4.0 | `environment/app/SPEC/ethereum-yellowpaper.pdf`, `environment/docs/yellowpaper/` |

The container image additionally installs Debian `libssl`, `libsecp256k1` and
`libgmp` development packages and Node.js; these are runtime/build dependencies
of the environment and are not redistributed in the task tree.

## Required notices

- Apache-2.0: the upstream blst `LICENSE` is the unmodified Apache License 2.0
  text and carries no project-specific copyright line. `VENDORED.txt` records
  the upstream repository and commit.
- GPL-3.0: go-ethereum source headers carry `Copyright 2014 The go-ethereum
  Authors`. The go-ethereum `COPYING` is the GNU General Public License v3.
- CC0-1.0: EIPs carry the EIP-1 copyright waiver, "Copyright and related rights
  waived via CC0"; the repository `LICENSE.md` is the CC0 1.0 Universal legal
  code. CC0 imposes no attribution condition.
- CC-BY-SA-4.0: the Yellow Paper is "Ethereum: A Secure Decentralised
  Generalised Transaction Ledger", Shanghai version `efc5f9a` (2025-02-04),
  originally by Gavin Wood, currently maintained by Andrew Ashikhmin and
  contributors, at https://github.com/ethereum/yellowpaper. It is licensed
  under CC BY-SA 4.0. The benchmark's plain-text conversion is a modification
  for offline search and is distributed under the same license.
- MIT: `Copyright 2014-2025 Ethereum Foundation` (ethereum/tests) and
  `Copyright 2025 Ethereum Community` (execution-spec-tests).
- No upstream component in this task ships an Apache `NOTICE` file, so no
  additional Apache NOTICE text applies.

Task-authored benchmark content is licensed separately by the benchmark maintainers.
