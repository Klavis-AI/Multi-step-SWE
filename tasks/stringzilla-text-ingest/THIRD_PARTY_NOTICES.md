# Third-party notices — stringzilla-text-ingest

Optimize one kernel family at a time in StringZilla's multilingual text-ingest
pass — indexing, line and sentence segmentation, case folding,
case-insensitive search, and hashing. Six cumulative steps, each scored per
entry point on the implementation the library installs, gated on byte-identical
output.

## Upstream projects

| | |
|---|---|
| Project | StringZilla |
| URL | https://github.com/ashvardanian/StringZilla |
| Pin | tag `v5.1.2`, commit `7fed3fb4201a34962cd456a4033bebc5ea09e74e` |
| Bundled | Full upstream source tree cloned into `/app/stringzilla` at image build time (`environment/Dockerfile`), minus `.git` and `forkunion/.git`, with the optimized back-ends of the listed kernel families removed |
| Derived | Six `steps/*/solution/oracle.patch` files containing the removed back-ends, restored by the reference solution |
| Submodule | ForkUnion (`forkunion/`), https://github.com/ashvardanian/ForkUnion, commit `7f52520ea00a6bf8e7a1f46348dad345ba361096`, cloned with the parent tree |

`environment/vendor/PIN` records the repository, tag and commit. StringZilla is
header-only, so every build compiles the headers that are in the tree; the
family back-ends are what the task asks the candidate to restore and speed up.

## License table

| SPDX ID | File under `LICENSES/` | Covers |
|---|---|---|
| Apache-2.0 | `LICENSES/Apache-2.0.txt` | StringZilla (`LICENSE`, SHA-256 `c71d239df91726fc519c6eb72d318ec65820627232b2f796219e87dcf35d0ab4`) |
| Apache-2.0 | `LICENSES/Apache-2.0-ForkUnion.txt` | ForkUnion submodule at `7f52520` (`LICENSE`, byte-identical to StringZilla's) |

Both upstream `LICENSE` files are the unmodified Apache License 2.0 text,
including its bracketed "Copyright [yyyy] [name of copyright owner]"
application appendix. Neither repository ships a `NOTICE` file, so no Apache
NOTICE text applies.

## Version-to-family map

| Step | Kernel family stripped from the base tree | Restored by |
|---|---|---|
| `01-utf8-runes` | `utf8_runes` | `steps/01-utf8-runes/solution/oracle.patch` |
| `02-utf8-linebreaks` | `utf8_linebreaks` | `steps/02-utf8-linebreaks/solution/oracle.patch` |
| `03-utf8-sentences` | `utf8_sentences` | `steps/03-utf8-sentences/solution/oracle.patch` |
| `04-utf8-uncased-fold` | `utf8_uncased_fold` | `steps/04-utf8-uncased-fold/solution/oracle.patch` |
| `05-utf8-uncased` | `utf8_uncased` | `steps/05-utf8-uncased/solution/oracle.patch` |
| `06-hash` | `hash` | `steps/06-hash/solution/oracle.patch` |

The base tree also strips `cipher`, `intersect`, `memory`, `sort`,
`utf8_graphemes`, `utf8_norm`, `utf8_tokens` and `utf8_wordbreaks` so that only
the six scored families can be restored; `find`'s back-ends are deliberately
left in place. All steps use the same upstream pin and the same Apache-2.0
licence.

## Modifications made by the benchmark

- `strip.py` deletes each stripped family's backend headers except `serial.h`
  and `tables.h`, removes the `#include` lines that named them, and drops the
  `#if SZ_USE_<TIER>` / `#elif SZ_USE_<TIER>` branches that reference symbols
  the deleted headers defined, keeping the portable `#else` fallback.
- `.git` and `forkunion/.git` are removed after checkout. No other upstream
  source is edited.
- The `steps/*/solution/oracle.patch` files are benchmark-generated patches
  that restore the removed upstream implementations; they are not upstream
  files.
- `environment/vendor/measure.c`, `strip.py` and `PIN` are authored benchmark
  content. `measure.c` is compiled into `/app/tools/measure` with the practice
  profile; the graded profile differs in seed, size and script mix.
- The instructions, verifiers and per-step harnesses are authored benchmark
  content; the starting `/app` tree contains no task-authored StringZilla
  implementation.

## Third-party dependencies bundled in the task

The full upstream tree is present in the image, so it ships with the vendored
submodule even though only the project's own tests use it.

| Component | Version | License | Bundled path |
|---|---|---|---|
| StringZilla | 5.1.2 (`7fed3fb`) | Apache-2.0 | `/app/stringzilla` (whole tree) |
| ForkUnion | commit `7f52520` | Apache-2.0 | `forkunion/` |

The Rust, Python, JavaScript, Java, C# and Swift binding sources are part of
the StringZilla tree; their package manifests name external build/test
dependencies, but no third-party source for them is vendored. `Cargo.lock` is
a lock file, not bundled source.

## Required notices

StringZilla and ForkUnion are licensed under the Apache License, Version 2.0:

> Licensed under the Apache License, Version 2.0 (the "License");
> you may not use this file except in compliance with the License.
> You may obtain a copy of the License at
>
>     http://www.apache.org/licenses/LICENSE-2.0
>
> Unless required by applicable law or agreed to in writing, software
> distributed under the License is distributed on an "AS IS" BASIS,
> WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
> See the License for the specific language governing permissions and
> limitations under the License.

The upstream `LICENSE` files do not name a copyright owner; the StringZilla
headers identify Ash Vardanian as author (`@author Ash Vardanian`). Neither
repository ships a NOTICE file.

The task is not affiliated with or endorsed by the StringZilla or ForkUnion
projects.

Task-authored benchmark content is licensed separately by the benchmark maintainers.
