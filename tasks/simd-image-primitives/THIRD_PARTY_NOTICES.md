# Third-party notices — simd-image-primitives

An eight-step benchmark in which a candidate optimizes the Simd computer-vision
primitive library one algorithm family at a time, from portable C++ scalar
kernels to SSE4.1/AVX2 implementations, gated on bit-exact output.

## Upstream project

| | |
|---|---|
| Project | Simd (SIMD Library for image processing and machine learning) |
| URL | https://github.com/ermig1979/Simd |
| Pin | tag `v7.2.165`, commit `0dc87019c7030e438df1b750ce7d26f356b23cd1` (`environment/vendor/PIN`) |
| License | MIT |
| Bundled material | No upstream source tree is committed; the per-step `solution/oracle.patch` files contain upstream C++ source under the MIT licence. The agent image clones the pinned tag, verifies the commit, deletes `.git`, and strips eight primitive families. |

### Bundled and derived material

* `environment/Dockerfile` — clones `https://github.com/ermig1979/Simd.git` at
  tag `v7.2.165`, checks `git rev-parse HEAD` against the pinned SHA, removes
  `.git`, and runs `environment/vendor/strip.py` over eight families. The
  resulting tree at `/app/Simd` is part of the built image; it is not committed
  to this repository.
* `environment/vendor/strip.py` — benchmark-authored stripper. Per family it
  deletes every `Simd{Isa}<Family>*.{cpp,h}` file for the ISA prefixes
  `Sse41`, `Avx2`, `Avx512bw`, `Avx512vnni`, `AmxBf16`, `Neon`, `Sve2`, `Hvx`,
  `Vmx` and `Vsx`; removes the family's branches from the `SimdLib.cpp`
  dispatch ladders (both the `#ifdef SIMD_<ISA>_ENABLE` block form and the
  `SIMD_FUNC<n>` macro form); and removes its per-ISA autotest hooks from
  `src/Test/*.cpp`. `SimdBase<Family>*` and the rest of the library are left
  untouched.
* `steps/01-resizer/solution/oracle.patch` through
  `steps/08-background/solution/oracle.patch` — benchmark-generated `git diff`
  patches between successive stripped/restored trees. They add upstream
  implementation files and re-add the dispatch and autotest lines removed by
  `strip.py`. Every restored file carries its upstream MIT header, e.g.
  `Copyright (c) 2011-2025 Yermalayeu Ihar.` The patches touch 26, 18, 20, 10,
  7, 20, 47 and 33 files respectively; all paths are upstream paths under
  `src/Simd/` and `src/Test/`.
* `steps/*/workdir/BENCHMARK.md`, `WORKSPACE_HANDOFF.md`, the step
  instructions, `tests/` harnesses (`test.sh`, `run_test.sh`,
  `parse_report.py`, `score_reports.py`, `envelope_scan.py`, scope data) and
  the copies of `strip.py`/`PIN` under `tests/vendor/` are benchmark-authored
  content.

## License table

| SPDX ID | File under `LICENSES/` | Applies to |
|---|---|---|
| MIT | `MIT.txt` | Simd at tag `v7.2.165` / commit `0dc87019c7030e438df1b750ce7d26f356b23cd1`. Exact upstream `LICENSE` at the pinned commit (SHA-256 `5f86b26a39dfe61259d87fb4d633d1d09217c181b0fe2b420b7d2c2fb5d88ab7`), with `Copyright (c) 2011-2017 Ihar Yermalayeu`. It covers the upstream source carried by the oracle patches and the upstream tree assembled in the image. |

## Modifications made by the benchmark

* The image removes the per-ISA implementations of the eight graded families
  (`resizer`, `blur`, `edges`, `cascade`, `hog`, `warp`, `stats`,
  `background`), their dispatch branches and their autotest hooks. The scalar
  `SimdBase` reference and all out-of-scope families remain as upstream ships
  them.
* `.git` is deleted after the pinned commit is verified. No other file is
  altered at image-build time.
* Each `oracle.patch` restores the upstream files for one family and the
  upstream dispatch/autotest lines; no benchmark-specific change is made to the
  restored upstream code. The patches are generated as diffs between built
  trees, not hand-edited.
* The scope, measurement and scoring harnesses are benchmark-authored.

## Third-party dependencies bundled in the task

No third-party source tree or binaries are committed to the task tree beyond
the upstream source text carried by the `oracle.patch` files. The agent and
verifier images install build tooling from Debian (`cmake`, `ninja-build`,
`g++`, `gcc`, `git`, `python3`, `patch`, and their dependencies) under the
respective Debian package licences; those packages are not redistributed here.
The Simd source tree itself is fetched at image-build time from the pinned
commit. The test framework used for measurement is Simd's own, built from that
tree.

## Required notices

MIT licence copyright line, reproduced in `LICENSES/MIT.txt` from the pinned
upstream `LICENSE`:

```
Copyright (c) 2011-2017 Ihar Yermalayeu
```

Upstream source files restored by the oracle patches carry the per-file header
`Copyright (c) 2011-2025 Yermalayeu Ihar.` and the full MIT permission text; the
year range varies by file. Those headers are retained unmodified.

The MIT licence grants no trademark rights and contains no endorsement clause.

---

Task-authored benchmark content is licensed separately by the benchmark
maintainers.
