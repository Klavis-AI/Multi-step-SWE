# Third-Party Notices

**Task:** `harbor/zcash-consensus-node-multiversion` — implement a Zcash
consensus node in Rust across fourteen cumulative upstream Zebra releases,
from v1.0.0-alpha.0 through v1.0.0-beta.6.

## 1. Upstream project

| | |
| --- | --- |
| Project | Zebra (Zcash Foundation) |
| Repository | <https://github.com/ZcashFoundation/zebra> |
| Upstream licence | MIT OR Apache-2.0 (`LICENSE-MIT`, `LICENSE-APACHE`) |
| Copyright | Copyright (c) 2019 Zcash Foundation |

Pinned releases, in step order:

| Step | Upstream tag | Commit |
| --- | --- | --- |
| 01-seeded-baseline | `v1.0.0-alpha.0` | `85c7eea444a72b737dbd0920f07f01f5aa2a9b42` |
| 02-zip215-batched-verification | `v1.0.0-alpha.6` | `24bf952e982bde28eb384b211659159d46150f63` |
| 03-v5-sapling-orchard-types | `v1.0.0-alpha.8` | `500dc2e5110a14f7ef4949f974252142c4c07f9f` |
| 04-orchard-data-nullifier-state | `v1.0.0-alpha.10` | `9aafa79fa357d76ab000f011558d6bb222a71d5e` |
| 05-history-tree-zip211-redpallas | `v1.0.0-alpha.12` | `d30b95d0fa9867c58c398bc7c0b94939029d489c` |
| 06-zip244-txid-zip239-relay | `v1.0.0-alpha.13` | `12c60b5868834ba494fbb990bc9a7e705a2427cf` |
| 07-contextual-validation-trees | `v1.0.0-alpha.14` | `564b902f94f44ffaee1059f87d463fec026d2961` |
| 08-zip209-flyclient-authdigest | `v1.0.0-alpha.15` | `13651d432d230959b6c7db043e82a6cc083df641` |
| 09-mempool-admission | `v1.0.0-alpha.17` | `e7cec3efc26908cdf44dc5f76216995d8d27ec34` |
| 10-mempool-storage-expiry | `v1.0.0-alpha.19` | `d2a5af0ea573854e1e5cb45f1dca3dc0fac79b47` |
| 11-zip401-eviction-conn-limits | `v1.0.0-beta.0` | `0db35fbee0937e156ef96a2dda963ca786274163` |
| 12-halo2-zip155-zip212-subsidy | `v1.0.0-beta.1` | `26b3a50e01c15cbed4c46bbef2887a7b19ecb5b1` |
| 13-anchors-joinsplit-parameters | `v1.0.0-beta.3` | `a5713da0af3038efbcb048d46cf317460e68df47` |
| 14-rpc-tree-persistence-inventory | `v1.0.0-beta.6` | `53bf4c6ba516a502ce39b5cb3616017b70c58c6e` |

Normative specification corpus:

| Artifact | Source | Pin |
| --- | --- | --- |
| `specs/zips/zip-*.rst` | <https://github.com/zcash/zips> | commit `0fae783d424039a09759a69ff993317ded72b97d` (2026-09-15) |
| `specs/nu5.pdf` | <https://zips.z.cash/protocol/nu5.pdf> | version `v2026.7.0-191-g0fae78 [NU5]`, dated 2026-09-15 |
| `specs/nu5.txt` | mechanically derived from `nu5.pdf` with `pdftotext -layout` | same edition |

## 2. What is bundled or derived

- `steps/01-seeded-baseline/workdir/` — the upstream v1.0.0-alpha.0 tree with a
  subset of the wire-format core withheld for the exercise.
- `steps/01-seeded-baseline/solution/zebra-v1.0.0-alpha.0-wire-core.tar.gz` —
  the withheld upstream core files, restoring the tree to the tag.
- `steps/02-...` through `steps/14-.../solution/release-*-to-*.patch` — exact
  upstream release-to-release source diffs through v1.0.0-beta.6, plus the
  pinned `Cargo.lock` and the upstream `LICENSE-APACHE` / `LICENSE-MIT`.
- `steps/*/workdir/specs/` — the upstream `zcash/zips` corpus and the NU5
  protocol specification, reproduced verbatim (`nu5.txt` is a mechanical text
  extraction of `nu5.pdf`).
- Upstream documentation retained in the tree: `book/`, `design/`,
  `README.md`, `CONTRIBUTING.md` and the crate-level documentation.
- Benchmark-authored: `instruction.md`, `tests/`, `specs/PROVENANCE.md`, the
  `specs/README` material, `TEST_INTERFACE.md`, `WORKSPACE_HANDOFF.md`,
  `timer.sh`.

## 3. Licence files

| SPDX ID | File under `LICENSES/` | Upstream component / versions covered |
| --- | --- | --- |
| MIT | `MIT.txt` | Zebra source, tests and docs (`book/`, `design/`) at `v1.0.0-alpha.0` through `v1.0.0-beta.6` |
| Apache-2.0 | `Apache-2.0.txt` | same |
| MIT | `MIT-ZIPs.txt` | `zcash/zips` ZIP sources and the NU5 protocol specification at commit `0fae783d424039a09759a69ff993317ded72b97d` |

`MIT.txt` and `Apache-2.0.txt` were copied byte-for-byte from
`steps/01-seeded-baseline/solution/`; the same bytes are published upstream at
both `v1.0.0-alpha.0` and `v1.0.0-beta.6`. `MIT-ZIPs.txt` is the `COPYING.rst`
shipped in `steps/01-seeded-baseline/workdir/specs/zips/`, byte-identical to
the file at the pinned `zcash/zips` commit.

The Zebra `README.md` states:

> Zebra is distributed under the terms of both the MIT license and the Apache
> License (Version 2.0).
>
> See LICENSE-APACHE and LICENSE-MIT.

## 4. Modifications made by this benchmark

- The initial workdir withholds a subset of the upstream wire-format core; the
  step 01 Oracle restores it from the upstream tag. Later steps apply exact
  upstream release-to-release patches.
- `Cargo.lock` and the two upstream licence files are pinned to each tag.
- `specs/nu5.txt` is a mechanical `pdftotext -layout` extraction of the
  verbatim `nu5.pdf`; the PDF remains the authority. `specs/PROVENANCE.md`
  records the hashes and pins.
- The benchmark authors the instructions, verifier tests, `specs/README`
  material and `specs/PROVENANCE.md`. Upstream source and documentation files
  are otherwise unmodified.

## 5. Third-party dependencies bundled in the task

| Component | Version | Licence | Bundled path |
| --- | --- | --- | --- |
| Zcash Improvement Proposals (`zip-*.rst`) | commit `0fae783d424039a09759a69ff993317ded72b97d` | MIT | `steps/*/workdir/specs/zips/` |
| Zcash Protocol Specification, NU5 edition (`nu5.pdf`, `nu5.txt`) | `v2026.7.0-191-g0fae78 [NU5]` | MIT | `steps/*/workdir/specs/` |

Rust crate dependencies are resolved from crates.io by the verifier build and
are not vendored in this task.

## 6. Required notices

- `Copyright (c) 2019 Zcash Foundation` for Zebra, carried by the upstream
  `LICENSE-MIT` and `LICENSE-APACHE`.
- `Copyright (c) 2016-2020 The Zcash Core developers` for the ZIP corpus and
  the protocol specification.
- Zebra publishes no upstream `NOTICE` file at any pinned tag, so there is no
  Apache NOTICE text to reproduce.
- No trademark licence is granted by the upstream or by this task; Zcash,
  Zebra and the names of their contributors remain the property of their
  holders.

## 7. Scope of this licence bundle

This bundle covers the upstream Zebra material, the `zcash/zips` corpus and
the NU5 protocol specification redistributed with this task. Task-authored
benchmark content is licensed separately by the benchmark maintainers.
