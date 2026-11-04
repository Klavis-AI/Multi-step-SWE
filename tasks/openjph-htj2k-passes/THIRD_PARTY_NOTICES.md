# Third-Party Notices — openjph-htj2k-passes

Benchmark task that optimizes the OpenJPH HTJ2K codec one coding pass at a
time, with each step rewarded by measured per-function speedup and gated on
bit-exact output.

## Upstream project

| | |
|---|---|
| Project | OpenJPH |
| URL | https://github.com/aous72/OpenJPH |
| Pin | tag `0.32.0`, commit `23c422895ce6c3a156935222e4715ee0b7be952c` |
| Bundled/derived | The environment image clones the full upstream tree at the pin and removes the optimized implementations of the six measured stages (`blk_decode`, `blk_encode`, `rev_wavelet`, `irv_wavelet`, `colour`, `codestream`). The step solution patches (`steps/*/solution/oracle.patch`) are derived from the upstream optimized code. The workload generator, measurement harness, instructions and tests are task-authored. |

## License table

| SPDX ID | File under `LICENSES/` | Covers |
|---|---|---|
| BSD-2-Clause | `BSD-2-Clause.txt` | OpenJPH 0.32.0 (exact upstream `LICENSE`, fetched from the pinned tag) |

The upstream license carries three copyright lines:

```
Copyright (c) 2019, Aous Naman
Copyright (c) 2019, Kakadu Software Pty Ltd, Australia
Copyright (c) 2019, The University of New South Wales, Sydney, Australia
```

## Modifications made by the benchmark

- The environment tree is upstream at the pin with the optimized codec stages
  removed by `environment/vendor/strip.py`; the portable C++ implementations in
  `src/core` remain and define the reference output the measurement divides by.
- The solution patches restore optimized implementations derived from upstream
  OpenJPH and carry the upstream BSD-2-Clause terms. They are benchmark patch
  files, not upstream releases.
- `environment/vendor/make_workload.py`, `environment/vendor/measure.c`,
  `BENCHMARK.md`, the step instructions and the verifier are task-authored.

## Third-party dependencies bundled in the task

None. The task carries the OpenJPH source tree only. The build uses the system
C++ toolchain and CMake from the base image; no third-party library is vendored
in this repository.

## Required notices

- BSD-2-Clause copyright lines from the upstream `LICENSE`, reproduced in
  `LICENSES/BSD-2-Clause.txt`.
- No trademark or attribution notice beyond the copyright lines appears in the
  upstream license file.
- The upstream tree carries no separate `NOTICE` file.

Task-authored benchmark content is licensed separately by the benchmark maintainers.
