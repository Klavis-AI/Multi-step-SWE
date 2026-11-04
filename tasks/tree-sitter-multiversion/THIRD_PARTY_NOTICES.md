# Third-Party Notices

**Task:** `harbor/tree-sitter-multiversion` — build an incremental parser
generator and its C runtime, then carry it through nine real releases of
tree-sitter from v0.19.0 to v0.27.0.

## 1. Upstream project

| | |
| --- | --- |
| Project | tree-sitter |
| Repository | <https://github.com/tree-sitter/tree-sitter> |
| Upstream licence | MIT (root `LICENSE`, `lib/LICENSE`, per-crate `LICENSE`) |
| Copyright | Copyright (c) 2018 Max Brunsfeld (see the per-version table) |

Pinned releases, in step order:

| Step | Upstream tag | CLI version | Runtime (`lib`) version |
| --- | --- | --- | --- |
| 01-generator-and-runtime | `v0.19.0` | 0.19.0 | 0.17.1 |
| 02-queries-and-scanner-serialization | `v0.20.6` | 0.20.6 | 0.20.6 |
| 03-highlight-tags-and-runtime-consolidation | `v0.20.9` | 0.20.9 | 0.20.10 |
| 04-last-wins-captures | `v0.21.0` | 0.21.0 | 0.20.10 |
| 05-corpus-discovery-build-and-attributes | `v0.22.6` | 0.22.6 | 0.22.6 |
| 06-interning-revert-and-query-api | `v0.24.0` | 0.24.0 | 0.24.0 |
| 07-abi15-reserved-and-supertypes | `v0.25.0` | 0.25.0 | 0.25.0 |
| 08-crates-layout-and-cst | `v0.26.2` | 0.26.2 | 0.26.2 |
| 09-capture-contamination | `v0.27.0` | 0.27.0 | 0.27.0 |

## 2. What is bundled or derived

- `steps/*/solution/release.tar.gz` — the complete upstream source tree at
  each pinned tag (417 files in the v0.19.0 archive), including the CLI, the C
  runtime, the generated parser tables, the upstream test fixtures and the
  upstream build files.
- `steps/*/tests/prewarm/<tag>/` — benchmark-generated manifest stubs
  (`Cargo.toml`, `Cargo.lock` and empty module skeletons) used to warm the
  Cargo registry cache. They are derived from the upstream manifests, not from
  upstream source files.
- `steps/*/workdir/spec/` — benchmark-authored interface specifications.
- Benchmark-authored: `instruction.md`, `tests/`, `TEST_INTERFACE.md`,
  `WORKSPACE_HANDOFF.md`, `timer.sh`, and the generated `/app/build.sh`.

The upstream tag trees are staged wholesale rather than patched, because the
v0.25.0 to v0.26.2 release deleted `cli/` and created `crates/`.

## 3. Licence files

| SPDX ID | File under `LICENSES/` | Upstream component / versions covered |
| --- | --- | --- |
| MIT | `MIT.txt` | tree-sitter CLI, generator, runtime and repository files, `v0.19.0` through `v0.27.0` |
| ICU | `Unicode-ICU.txt` | `lib/src/unicode/` (ICU subset) in every pinned release |
| MIT (musl libc) | `MIT-musl.txt` | `lib/src/wasm-stdlib/libc/` in `v0.27.0` |

`LICENSES/MIT.txt` is the root `LICENSE` of `v0.27.0`, copied byte-for-byte
from `steps/09-capture-contamination/solution/release.tar.gz`. The MIT text is
identical in every release; only the copyright year range in the notice line
changes:

| Pinned tag | Copyright line in root `LICENSE` |
| --- | --- |
| `v0.19.0` | `Copyright (c) 2018 Max Brunsfeld` |
| `v0.20.6` | `Copyright (c) 2018-2021 Max Brunsfeld` |
| `v0.20.9`, `v0.21.0` | `Copyright (c) 2018-2023 Max Brunsfeld` |
| `v0.22.6`, `v0.24.0`, `v0.25.0` | `Copyright (c) 2018-2024 Max Brunsfeld` |
| `v0.26.2`, `v0.27.0` | `Copyright (c) 2018 Max Brunsfeld` (also `lib/LICENSE`) |

`LICENSES/Unicode-ICU.txt` is `lib/src/unicode/LICENSE` from the
`v0.27.0` archive; the file is byte-identical in all nine pinned releases. It
is the ICU copyright and permission notice (Copyright © 1991-2019 Unicode,
Inc.) together with the ICU third-party terms. The files it covers are the
ICU subset in `lib/src/unicode/`, taken from ICU commit
`552b01f61127d30d6589aa4bf99468224979b661`.

`LICENSES/MIT-musl.txt` is `lib/src/wasm-stdlib/libc/LICENSE` from the
`v0.27.0` archive, the musl libc MIT licence (Copyright © 2005-2020 Rich
Felker, et al.). `lib/src/wasm-stdlib/libc/` is present only in `v0.27.0`; the
`v0.26.2` archive still uses `lib/src/wasm/wasm-stdlib.h` and does not bundle
the musl sources.

## 4. Modifications made by this benchmark

- Each step's `/app` tree is replaced with the complete upstream tag tree; no
  upstream source file is edited. Benchmark-owned paths (`spec`,
  `TEST_INTERFACE.md`, `WORKSPACE_HANDOFF.md`, `timer.sh`) are preserved across
  the replacement.
- A generated `/app/build.sh` is added to satisfy the task interface
  (`cargo build --release --locked` into `/app/bin/tree-sitter`).
- The per-step `prewarm/` manifest stubs are benchmark-generated build
  metadata, not upstream source.
- The nine upstream tag trees are otherwise unmodified.

## 5. Third-party dependencies bundled in the task

| Component | Version | Licence | Bundled path |
| --- | --- | --- | --- |
| ICU Unicode headers subset | ICU commit `552b01f61127d30d6589aa4bf99468224979b661` | ICU licence (Unicode terms plus ICU third-party terms) | `lib/src/unicode/` inside every `solution/release.tar.gz` |
| musl libc subset | as vendored upstream in tree-sitter `v0.27.0` | MIT | `lib/src/wasm-stdlib/libc/` inside `steps/09-capture-contamination/solution/release.tar.gz` |

Rust crate dependencies are resolved from crates.io by the verifier build and
are not vendored in this task. The `tests/prewarm/` manifests only name them;
they do not contain their source or licence files.

## 6. Required notices

- `Copyright (c) 2018 Max Brunsfeld` (and the year-range variants listed in
  section 3) for tree-sitter.
- `Copyright © 1991-2019 Unicode, Inc. All rights reserved.` for the ICU
  subset, together with the additional ICU third-party notices reproduced in
  `LICENSES/Unicode-ICU.txt`.
- `Copyright © 2005-2020 Rich Felker, et al.` for the musl libc subset, plus
  the contributor list and third-party notices reproduced in
  `LICENSES/MIT-musl.txt`.
- tree-sitter publishes no upstream `NOTICE` file at any pinned tag, so there
  is no Apache NOTICE text to reproduce.
- No trademark licence is granted by the upstream or by this task.

## 7. Scope of this licence bundle

This bundle covers the upstream tree-sitter material, the ICU subset and the
musl libc subset redistributed with this task. Task-authored benchmark content
is licensed separately by the benchmark maintainers.
