# Third-Party Notices — uvg266-encoder-strategies

A six-step benchmark ladder in which a candidate optimizes the uvg266 VVC
encoder's strategy families one family at a time — transforms, block cost,
intra prediction, motion compensation, quantisation and loop filtering — with
each step gated on a byte-identical bitstream and rewarded by measured
per-function speedup.

## Upstream project

| | |
|---|---|
| Project | uvg266 (VVC encoder) |
| Repository | https://github.com/ultravideo/uvg266 |
| Pin | commit `48dfc4932c6ba2ac741a07137b9ce2961cf95105` |
| Upstream license | BSD-3-Clause (`LICENSE`; `Copyright (c) 2021, Tampere University, ITU/ISO/IEC, project contributors`) |

The task repository does not vendor the uvg266 source tree. The agent image and
every verifier image clone the pinned commit at build time, initialize the
`greatest` submodule, and remove the optimized strategy families with
`environment/vendor/strip.py`.

### Bundled and derived material

* `steps/*/solution/oracle.patch` — six patches derived from upstream
  `src/strategies/{avx2,sse2,sse41,sse42,altivec}/` sources. They restore the
  optimized strategy twins and the registry/build wiring for the six ladder
  families (`dct`, `picture`, `intra`, `ipol`, `quant`, `sao`). The added code
  retains the upstream BSD-3-Clause headers.
* `environment/vendor/bench-practice.c`, `make_clip.py`, `strip.py`, `PIN` —
  benchmark-authored measurement and tree-stripping tooling.
* `environment/Dockerfile`, `timer.sh`, `task.toml` and the step
  `instruction.md` / `tests/` content — task-authored benchmark material.
* The built image contains the full upstream tree at the pin, including its
  `src/threadwrapper/` library and the `greatest` unit-test submodule.
* No upstream source tree is committed to the task repository.

## Licenses

| SPDX ID | File under `LICENSES/` | Applies to |
|---|---|---|
| BSD-3-Clause | `BSD-3-Clause.txt` | uvg266 at the pin. Verbatim upstream `LICENSE`. |
| ISC | `ISC-greatest.txt` | `greatest` 1.0.0 submodule at commit `60e25ce7658ced49f03b52ac14c553cf6ea33de4`; exact license block from `greatest.h`. |
| ISC | `ISC-threadwrapper.txt` | `src/threadwrapper/LICENSE` in the upstream tree, Copyright 2019 Tampere University. |

## Modifications made by the benchmark

* `strip.py` removes each named strategy family from every architecture
  implementation (`avx2`, `sse2`, `sse41`, `sse42`, `altivec`): the
  `<family>-<isa>.c/.h` files, the dispatcher includes and registration blocks
  in `src/strategies/strategies-<family>.c`, the `src/CMakeLists.txt` entries,
  family-only tables, and any ISA directory left empty.
* The starting tree is the upstream pin with `transforms`, `blockcost`,
  `intra`, `motion`, `quant`, `loopfilter` (the `alf` and `sao` families) and
  `depquant` stripped. The generic C implementations in
  `src/strategies/generic/` remain as the correctness reference and speed
  denominator.
* Verifier images rebuild the identically stripped base and measure against it;
  the oracle patches restore the upstream optimized implementations.
* No behavioural change is made to the remaining upstream code: the stripped
  encoder still produces a byte-identical bitstream.

## Third-party dependencies bundled in the task

The task repository contains no third-party code beyond the derived uvg266
patches. The upstream tree cloned and built at image-build time contains:

| Name | Version | License | Bundled path |
|---|---|---|---|
| greatest | 1.0.0, commit `60e25ce7658ced49f03b52ac14c553cf6ea33de4` | ISC | `greatest/` in the built image tree; initialized by `git submodule update --init --quiet greatest` from https://github.com/ultravideo/greatest.git and used by the upstream test suite |
| threadwrapper | vendored at the pin | ISC | `src/threadwrapper/` in the upstream tree |

The system toolchain and CMake packages installed by the Dockerfile are build
dependencies from Debian and are not redistributed in the task tree.

## Required notices

The upstream `LICENSE` carries:

```
Copyright (c) 2021, Tampere University, ITU/ISO/IEC, project contributors
```

The upstream `LICENSE.EXT.greatest` states:

```
Kvazaar uses "Greatest" C unit testing library, licensed under ISC license.
A fork of the library is included in the git project as a submodule. The
original can be found at https://github.com/silentbicycle/greatest .
```

The `greatest` license carries:

```
Copyright (c) 2011-2015 Scott Vokes <vokes.s@gmail.com>
```

The `src/threadwrapper/` license carries:

```
Copyright 2019 Tampere University
```

The upstream project ships no separate `NOTICE` file at this pin, and its
`LICENSE` contains no trademark statement.

---

Task-authored benchmark content is licensed separately by the benchmark maintainers.
