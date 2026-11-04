# Third-Party Notices — bitmagic-block-kernels

A six-step benchmark ladder in which a candidate restores BitMagic's hand-written
AVX2 block kernels (boolean block algebra, bit-to-gap conversion, block shift,
XOR similarity, change counting, and index scatter) in a tree whose optimized
kernel bodies have been removed.

## Upstream project

| | |
|---|---|
| Project | BitMagic |
| Repository | https://github.com/tlk00/BitMagic |
| Pin | commit `ccbd94fa55ba7946fb8bbac72a59a816917573a8` |
| Upstream license | Apache-2.0, with an additional request to credit BitMagic (reproduced below) |

The task repository does not vendor the BitMagic source tree. The agent image
and every verifier image clone the pinned commit at build time and remove the
optimized kernels with `environment/vendor/strip.py`.

### Bundled and derived material

* `steps/*/solution/oracle.patch` — six patches derived from upstream
  `src/bmavx2.h` and related backend headers. They restore the optimized AVX2
  kernel bodies and their dispatch wiring for the six ladder stages.
* `environment/vendor/` and each step's `tests/vendor/` — benchmark-authored
  stripping and verification tooling (`strip.py`, `ALL_GROUPS`, and the step
  test harnesses).
* No upstream test fixtures are committed to the task repository.

## Licenses

| SPDX ID | File under `LICENSES/` | Applies to |
|---|---|---|
| Apache-2.0 | `Apache-2.0.txt` | BitMagic at the pin. The canonical Apache License 2.0 text. |

## Modifications made by the benchmark

* The starting tree is the upstream pin with the optimized bodies of the ten
  ladder kernel families removed from every backend header. The portable C++
  bodies in `src/bmfunc.h` remain; the tree still builds and passes upstream's
  stress suite.
* Verifier images rebuild the identically stripped base and measure against it;
  the oracle patches restore the upstream optimized code.
* No behavioural change is made to the remaining upstream code.

## Third-party dependencies bundled in the task

None beyond the upstream project itself. The benchmark-authored stripping and
test harnesses are task-authored content.

## Required notices

Upstream `LICENSE` file, reproduced verbatim:

```
BitMagic Library License

Copyright(c) 2002-2019 Anatoliy Kuznetsov(anatoliy_kuznetsov at yahoo.com)

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

For more information please visit:  http://bitmagic.io

**Important!** We ask you to explicitly mention BitMagic project in any derived work or our published 
materials. Proper BitMagic reference on your product/project page is a REQUIREMENT 
for using the Library.
```

BitMagic and the BitMagic logo are marks of their respective owner.

---

Task-authored benchmark content is licensed separately by the benchmark maintainers.
