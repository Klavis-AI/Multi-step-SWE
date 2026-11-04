# Third-Party Notices

**Task:** `harbor/vortex-columnar-bit-kernels` — optimize the bit-level
kernels under the Vortex columnar file format one at a time, over five
cumulative steps, each rewarded by measured per-function speedup gated on
bit-exact output.

## 1. Upstream project

| | |
| --- | --- |
| Project | Vortex |
| Repository | <https://github.com/spiraldb/vortex> |
| Pinned tag | `0.86.1` |
| Pinned commit | `d1fe2dc46d2b947a2304122d0d0423332fcb77a3` |
| Upstream code licence | Apache-2.0 (`LICENSE`, `LICENSES/Apache-2.0.txt`, SPDX headers) |
| Copyright | Copyright the Vortex contributors |

## 2. What is bundled or derived

The task repository does not contain an upstream source tree. Instead:

- `environment/Dockerfile` clones `https://github.com/spiraldb/vortex` at the
  pinned SHA, verifies `HEAD`, removes `.git`, and runs
  `environment/vendor/strip.py` over the checkout.
- `environment/vendor/strip.py` removes the optimized kernels of seven stages
  (`count_ones`, `pack`, `select`, `simd_compress`, `take`, `bool_filter`,
  `intersect`), leaving the portable kernels as the correctness reference. Five
  of the seven are the task's ladder stages.
- `steps/01-count-ones` through `steps/05-rank-intersect` each contain an
  `oracle.patch` deriving the optimized kernel for that step from upstream
  code at the same pinned commit.
- `steps/*/tests/` and `steps/*/instruction.md` are benchmark-authored.

## 3. Licence files

| SPDX ID | File under `LICENSES/` | Upstream component / versions covered |
| --- | --- | --- |
| Apache-2.0 | `Apache-2.0.txt` | Vortex source at `0.86.1` / `d1fe2dc46d2b947a2304122d0d0423332fcb77a3`, and the derived `oracle.patch` kernels |
| CC-BY-4.0 | `CC-BY-4.0.txt` | Upstream `docs/**`, `**/README.md`, `**/goldenfiles/**`, per upstream `REUSE.toml` |

`LICENSES/Apache-2.0.txt` is the upstream root `LICENSE` at the pinned commit,
with the upstream copyright line (`Copyright the Vortex contributors`) added
above it. It is identical in content to upstream
`LICENSES/Apache-2.0.txt` apart from line wrapping. Every Rust source file in
the tree carries:

```
// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright the Vortex contributors
```

`LICENSES/CC-BY-4.0.txt` is the upstream `LICENSES/CC-BY-4.0.txt`. Upstream's
`REUSE.toml` assigns CC-BY-4.0 to its documentation, README prose, golden test
files and skill/agent text. Those files exist only in the build-time clone of
the upstream tree; they are not part of this repository's own files or of the
`oracle.patch` payloads. They are listed here because the task image's `/app`
workspace carries the clone.

## 4. Modifications made by this benchmark

- Seven upstream optimization stages are removed from the base tree by
  `environment/vendor/strip.py`; the portable implementations and all other
  upstream code remain. See `environment/vendor/PIN` for the exact stage list.
- The five graded stages are restored one at a time by benchmark-authored
  `oracle.patch` files. Those patches are derived from the same pinned upstream
  commit and are licensed under Apache-2.0.
- The benchmark authors the instructions, the verifier harness, `SCOPE.*`,
  `ENVELOPE.*` and the timing/similarity tooling.
- No other upstream source is modified.

## 5. Third-party dependencies bundled in the task

No third-party source is vendored in this task. The Vortex workspace resolves
its Rust crate dependencies from crates.io when the environment and verifier
images are built; their source and licence files are not redistributed here.
`environment/vendor/` contains only the benchmark's `PIN` and `strip.py`.

## 6. Required notices

- `Copyright the Vortex contributors` — the upstream copyright line, carried
  by every SPDX header in the source and by `LICENSES/Apache-2.0.txt`.
- Vortex publishes no `NOTICE` file at the pinned commit, so there is no
  Apache NOTICE text to reproduce.
- The CC-BY-4.0 material listed in section 3 requires attribution to
  `Copyright the Vortex contributors` when it is redistributed; this task does
  not include those files in its own repository.
- No trademark licence is granted by the upstream or by this task.

## 7. Scope of this licence bundle

This bundle covers the Apache-2.0 upstream Vortex code and the derived oracle
patches distributed with this task, and records the CC-BY-4.0 terms for the
upstream documentation present in the build-time clone. Task-authored
benchmark content is licensed separately by the benchmark maintainers.
