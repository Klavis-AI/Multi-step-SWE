# Third-Party Notices — evm-state-transition-multiversion

Benchmark task that carries a 2021-era go-ethereum state-transition tool across
seven consensus upgrades, from Berlin to Osaka, keeping every earlier fork
exact.

## Upstream project

| | |
|---|---|
| Project | go-ethereum |
| URL | https://github.com/ethereum/go-ethereum |
| Seed pin | tag `v1.10.0`, commit `56dec25ae26bf749b93c3ea69538fabea60c5768` |
| Solution-bundle versions | `v1.10.7`, `v1.10.13`, `v1.10.23`, `v1.11.5`, `v1.14.0`, `v1.15.0`, `v1.16.2` (one per step, in ladder order) |
| Project | Ethereum Improvement Proposals (EIPs) |
| URL | https://github.com/ethereum/EIPs |
| Pin | commit `dbc6d457cbf90d5f9d55552f3fd91d58b88cc898` (2026-09-18) |

Bundled or derived material:

- `environment/Dockerfile` clones the `v1.10.0` seed into `/app`; `.git` is
  removed and the `github.com/fjl/memsize` module is replaced by the local
  `environment/memsize-stub`.
- Each step's `solution/bundle.tar.gz` is a complete go-ethereum release tree
  at that step's target version. The complete source, including go-ethereum's
  own `COPYING`, `COPYING.LESSER` and vendored dependency licenses, is inside
  the archive.
- Each step's `workdir/docs/fork/*/eip-*.md` is an unmodified EIP text from the
  pinned EIPs commit; `workdir/docs/LICENSES/EIPS-LICENSE.md` is the upstream
  EIPs `LICENSE.md`.
- The step instructions, `TEST_INTERFACE.md`, `WORKSPACE_HANDOFF.md`, build
  scripts, oracles and verifiers are task-authored.

## License table

| SPDX ID | File under `LICENSES/` | Covers |
|---|---|---|
| GPL-3.0 | `GPL-3.0.txt` | go-ethereum's `COPYING` (the `cmd` binaries and GPL-licensed parts), extracted verbatim from `steps/01-london-and-rlp-input/solution/bundle.tar.gz:COPYING` |
| LGPL-3.0 | `LGPL-3.0.txt` | go-ethereum's `COPYING.LESSER` (the library, all code outside `cmd`), extracted verbatim from `steps/01-london-and-rlp-input/solution/bundle.tar.gz:COPYING.LESSER` |
| CC0-1.0 | `CC0-1.0.txt` | EIP texts at commit `dbc6d457` under `steps/*/workdir/docs/fork/` (exact upstream `LICENSE.md`, copied from `steps/01-london-and-rlp-input/workdir/docs/LICENSES/EIPS-LICENSE.md`) |
| Apache-2.0 | `Apache-2.0.txt` | The `log` package's Apache-2.0 notice inside each solution bundle (`log/LICENSE`, a short Apache-2.0 notice); this file supplies the full license text |

go-ethereum's own `COPYING` is GPL-3.0 and `COPYING.LESSER` is LGPL-3.0; the
project's README assigns the library (outside `cmd`) to LGPL-3.0 and the
binaries (`cmd`) to GPL-3.0.

## Modifications made by the benchmark

- The seed tree has its `.git` directory removed and uses a local API-compatible
  stub for `github.com/fjl/memsize` because the upstream module no longer links
  under the pinned Go toolchain. No other seed source is edited.
- Each solution bundle is a complete upstream release tree. The only change is
  the packaging itself (the release contents are shipped as a tarball rather
  than as a git checkout).
- EIP texts are copied without textual changes.
- The step ladder, instructions, interface documents, build scripts, oracles
  and verifiers are task-authored.

## Third-party dependencies bundled in the task

All of the following are inside each step's `solution/bundle.tar.gz` at the
paths shown; the versions follow the go-ethereum release in that bundle and are
recorded by that release's `go.mod`. The license files are already part of the
archive.

| Component | Version | License | Bundled path |
|---|---|---|---|
| go-ethereum | `v1.10.7`–`v1.16.2` per step | GPL-3.0 / LGPL-3.0 | whole tree in `steps/*/solution/bundle.tar.gz`; `COPYING`, `COPYING.LESSER` |
| golang.org/x/crypto/bn256 (Go Authors) | as pinned by each geth release | BSD-3-Clause | `crypto/bn256/LICENSE` |
| cloudflare/bn256 | as pinned by each geth release | BSD-3-Clause | `crypto/bn256/cloudflare/LICENSE` |
| go-ethereum crypto/ecies | as pinned by each geth release | BSD-3-Clause | `crypto/ecies/LICENSE` |
| go-ethereum crypto/secp256k1 (Go port) | as pinned by each geth release | BSD-3-Clause | `crypto/secp256k1/LICENSE` |
| bitcoin-core/secp256k1 | as pinned by each geth release | MIT | `crypto/secp256k1/libsecp256k1/COPYING` |
| log15 (Alan Shreve) — go-ethereum `log` | as pinned by each geth release | Apache-2.0 | `log/LICENSE`; full text at `LICENSES/Apache-2.0.txt` |
| rcrowley/go-metrics | as pinned by each geth release | BSD-2-Clause | `metrics/LICENSE` |
| influxdata/influxdb client | as pinned by each geth release | MIT | `metrics/influxdb/LICENSE` |

The agent image also installs the Go 1.25.1 toolchain (BSD-3-Clause) and warms
the module cache with `github.com/crate-crypto/go-kzg-4844@v1.0.0` and
`github.com/ethereum/c-kzg-4844@v1.0.0`. These are installed in the container
and are not redistributed in the task tree.

## Required notices

- GPL-3.0/LGPL-3.0: go-ethereum source headers carry `Copyright 2014 The
  go-ethereum Authors`. The complete GPL-3.0 and LGPL-3.0 texts are reproduced
  in `LICENSES/` from the project's own `COPYING` and `COPYING.LESSER`.
- Apache-2.0: `log/LICENSE` carries `Copyright 2014 Alan Shreve` and the
  Apache-2.0 short notice; the full license text is in `LICENSES/Apache-2.0.txt`.
- BSD-3-Clause components carry their upstream copyright lines in
  `crypto/bn256/LICENSE`, `crypto/bn256/cloudflare/LICENSE`,
  `crypto/ecies/LICENSE` and `crypto/secp256k1/LICENSE` inside each bundle.
- BSD-2-Clause: `metrics/LICENSE` carries `Copyright 2012 Richard Crowley. All
  rights reserved.`
- MIT: `crypto/secp256k1/libsecp256k1/COPYING` carries `Copyright (c) 2013
  Pieter Wuille`; `metrics/influxdb/LICENSE` carries `Copyright (c) 2015
  Vincent Rischmann`.
- CC0-1.0: the EIPs repository `LICENSE.md` is the CC0 1.0 Universal legal
  code. The task's `workdir/docs/PROVENANCE.md` records that one shipped EIP
  (`eip-155.md`) carries no per-file waiver because it predates the mandatory
  EIP-1 wording; the repository-level CC0 license covers it.
- No upstream component in this task ships an Apache `NOTICE` file, so no
  additional Apache NOTICE text applies.

Task-authored benchmark content is licensed separately by the benchmark maintainers.
