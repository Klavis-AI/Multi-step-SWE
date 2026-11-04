# Third-Party Notices

Task: `elf-linker-multiversion` — implement an ELF linker across nine cumulative
milestones, from a static x86-64 linker to a four-architecture linker with LTO,
script-driven layout and debug indexes.

## Upstream project and pins

| item | value |
| --- | --- |
| Project | Wild linker |
| URL | https://github.com/wild-linker/wild |
| Initial release | tag `0.2.0`, commit `727c26f2` (2024-03-18), as recorded in `steps/01-static-x86-64-linker/workdir/spec/00-compatibility-profile.md` |
| Final release | tag `0.10.0` |
| What is bundled/derived | Step 01's `solution/wild-0.2.0.tar.gz` is a source archive of tag `0.2.0` (Cargo workspace, `wild` and `wild_lib` sources, upstream tests and the two upstream license files). Steps 02–09 carry `release-0.2.0-to-0.3.0.patch` through `release-0.9.0-to-0.10.0.patch`, each the exact upstream release-to-release tree diff. The benchmark-authored specification set lives in `steps/*/workdir/spec/`. |

### Release map

| step | advances to |
| --- | --- |
| 01-static-x86-64-linker | `0.2.0` (full source tarball) |
| 02-dynamic-output-and-debug-info | `0.2.0` → `0.3.0` |
| 03-aarch64-tls-and-versioning | `0.3.0` → `0.4.0` |
| 04-script-driven-layout | `0.4.0` → `0.5.0` |
| 05-riscv-crel-and-version-scripts | `0.5.0` → `0.6.0` |
| 06-symver-directives-and-illumos | `0.6.0` → `0.7.0` |
| 07-loongarch-defsym-and-merge | `0.7.0` → `0.8.0` |
| 08-lto-relr-and-relocatable | `0.8.0` → `0.9.0` |
| 09-gdb-index-and-full-sections | `0.9.0` → `0.10.0` |

The Wild project's `LICENSE-APACHE` and `LICENSE-MIT` are byte-identical at
tags `0.2.0` and `0.10.0` (SHA-256
`62c7a1e35f56406896d7aa7ca52d0cc0d272ac022b5d2796e7d6905db8a3636a` and
`23f18e03dc49df91622fe2a76176497404e46ced8a715d9d2b67a7446571cca3`), so the
dual license is unchanged across the whole chain.

## License table

| SPDX ID | File under `LICENSES/` | Covers |
| --- | --- | --- |
| Apache-2.0 | `Apache-2.0.txt` | Wild source at every listed release. Exact upstream `LICENSE-APACHE` from the `0.2.0` payload; one of the two licenses offered by the project. |
| MIT | `MIT.txt` | Wild source at every listed release. Exact upstream `LICENSE-MIT` from the `0.2.0` payload; the other license offered by the project. |
| GFDL-1.3 | `GFDL-1.3.txt` | The GNU `ld` documentation (`ld/ld.texi`, binutils `binutils-2_47`) whose linker-script semantics are restated in the benchmark-authored specification documents. |
| CC-BY-SA-4.0 | `CC-BY-SA-4.0.txt` | The Arm `aaelf64` ABI document, release 2025Q4, whose AArch64 relocation definitions are restated in the benchmark-authored specification documents. |
| CC-BY-SA-4.0 with Arm patent license | `CC-BY-SA-4.0-Arm-Patent-License.txt` | Exact `aaelf64/LICENSE` from the Arm ABI repository at release 2025Q4: the CC BY-SA 4.0 reference and the additional patent grant. |

The Wild project has no `NOTICE` file at the pins. Neither upstream license
file contains a project-specific copyright line; the `0.2.0` source files carry
no per-file copyright header either. The README's dual-license statement is the
project's only licensing statement.

## Modifications made by the benchmark

- `solution/wild-0.2.0.tar.gz` is the upstream tag `0.2.0` tree repacked as a
  source archive. No source file is edited. `solve.sh` extracts it over the
  seeded `/app` workspace; the overlay preserves the benchmark's
  `TEST_INTERFACE.md`, `WORKSPACE_HANDOFF.md` and `spec/` documents, which are
  not part of the upstream tree.
- Steps 02–09 apply the upstream release-to-release diff with
  `git apply -p1 --binary`. The patches are upstream changes; the benchmark
  does not rewrite them.
- The specification set under `steps/*/workdir/spec/` was written for this
  benchmark. Per `spec/LICENSES.md`, the primary sources are variously
  unlicensed, GFDL, CC BY-SA or GPL, so the benchmark restates facts
  (structure layouts, field offsets, relocation numbers, arithmetic and
  constants) rather than reproducing the documents. The `GFDL-1.3.txt` and
  `CC-BY-SA-4.0.txt` files are included for the two primary sources whose terms
  apply; the documents themselves are not copied into the task tree.
- No upstream license file is modified or removed.

## Third-party dependencies bundled in the task

The Dockerfile prefetches the Rust crate graph for all nine steps into the
image's shared `CARGO_HOME` from `environment/assets/prefetch/NN/Cargo.lock`.
Those crates are fetched from crates.io at image build time and are not stored
in the task repository or the solution payloads. The direct dependencies named
by Wild `0.2.0` are:

| name | version | license | bundled path |
| --- | --- | --- | --- |
| `ahash` | 0.8.7 | MIT OR Apache-2.0 | image `CARGO_HOME` only |
| `anyhow` | 1.0.75 | MIT OR Apache-2.0 | image `CARGO_HOME` only |
| `bytemuck` | 1.14.0 | Zlib OR Apache-2.0 OR MIT | image `CARGO_HOME` only |
| `crossbeam-queue` | 0.3.8 | MIT OR Apache-2.0 | image `CARGO_HOME` only |
| `crossbeam-utils` | 0.8.18 | MIT OR Apache-2.0 | image `CARGO_HOME` only |
| `memchr` | 2.7.1 | Unlicense OR MIT | image `CARGO_HOME` only |
| `memmap2` | 0.9.0 | MIT OR Apache-2.0 | image `CARGO_HOME` only |
| `object` | 0.33.0 (`wild_lib`) / 0.32.2 (`wild`) | Apache-2.0 OR MIT | image `CARGO_HOME` only |
| `rayon` | 1.8.0 | MIT OR Apache-2.0 | image `CARGO_HOME` only |
| `smallvec` | 1.13.1 | MIT OR Apache-2.0 | image `CARGO_HOME` only |
| `tracing` | 0.1.40 | MIT | image `CARGO_HOME` only |
| `tracing-subscriber` | 0.3.18 | MIT | image `CARGO_HOME` only |
| `ar` (dev) | 0.9.0 | MIT | image `CARGO_HOME` only |
| `wait-timeout` (dev) | 0.2.0 | MIT OR Apache-2.0 | image `CARGO_HOME` only |

Each dependency's own upstream license governs its use; the crates are not
redistributed in this task tree, so their license files are not reproduced here.
The `Apache-2.0.txt` and `MIT.txt` files in `LICENSES/` cover Wild itself, not
these crates.

## Required notices

- Wild is dual-licensed under Apache-2.0 or MIT, at the user's option. The
  exact texts are `LICENSES/Apache-2.0.txt` and `LICENSES/MIT.txt`. The
  upstream `README.md` states: "Licensed under either of Apache License,
  Version 2.0 or MIT license at your option."
- No project-specific copyright line is asserted by the upstream license
  files. The upstream README names David Lattimore as the project's author and
  sponsor link recipient; no copyright line is fabricated here.
- The Arm `aaelf64` document, release 2025Q4, carries
  `Copyright © 2011, 2013, 2018-2026, Arm Limited and its affiliates. All
  rights reserved.` and is licensed under CC BY-SA 4.0 with the Arm Apache
  patent license; the exact `aaelf64/LICENSE` is
  `LICENSES/CC-BY-SA-4.0-Arm-Patent-License.txt`.
- The GNU `ld` documentation carries
  `Copyright (C) 1991-2026 Free Software Foundation, Inc.` and is licensed
  under the GNU Free Documentation License, Version 1.3 or later.
- The benchmark-authored specification documents restate facts from primary
  sources that are variously unlicensed, GFDL, CC BY-SA or GPL; their
  `spec/LICENSES.md` records the exact editions. Only the GNU `ld`
  documentation (GFDL-1.3) and the Arm `aaelf64` document (CC BY-SA 4.0) are
  represented by license texts here, because the other primary documents are
  not redistributed. No license text is fabricated for any source whose terms
  could not be confirmed.
- ELF, Wild and the names of the referenced architectures are names or
  trademarks of their respective owners. This task claims no endorsement by the
  Wild project, Arm, or the Free Software Foundation.

Task-authored benchmark content is licensed separately by the benchmark maintainers.
