# Third-Party Notices

## Task

`ise-cpp98-to-cpp26` — migrate the ISE C++98 server framework to C++26 in
eleven steps, preserving its recorded behaviour byte for byte at every step.

## Upstream project

| Field | Value |
| --- | --- |
| Project | ISE (Iris Server Engine) |
| URL | <https://github.com/haoxingeng/ise> |
| Pin | commit `9a785fec10e2eaa42210d27606bbd23d05e9e95d` |
| Bundled path | `environment/tree/` — the pinned source tree (`ise/`, `examples/`, `build/`, `doc/`, `README`) |
| Upstream license | Apache-2.0 (`environment/tree/LICENSE`) |

## License files

| SPDX ID | File under `LICENSES/` | Covers |
| --- | --- | --- |
| Apache-2.0 | `Apache-2.0.txt` | ISE at commit `9a785fe`. Exact copy of `environment/tree/LICENSE`. |

The upstream `LICENSE` is the standard Apache License 2.0 text; its appendix
retains the `Copyright [yyyy] [name of copyright owner]` template rather than a
filled-in copyright line. The individual source files carry the project's
copyright and license header:

> `Copyright 2013 HaoXinGeng (haoxingeng@gmail.com)`
> `All rights reserved.`
> Licensed under the Apache License, Version 2.0.

## Modifications by the benchmark

- The retained tree is re-encoded from GBK to UTF-8. For most files encoding is
  the only difference; two files also carry the removal edits listed next.
- Removed upstream files and modules not needed by the task:
  `ise/ext/` (the MySQL DBI, cipher and XML extension modules),
  `ise/main/ise_database.{h,cpp}`, the corresponding `ise_database.h` include
  in `ise/main/ise.h`, the three `ise/ext/...` entries in
  `build/linux/CMakeLists.txt`, `build/windows/`, and the
  `@release/ise-1.0.0.tar.gz` release archive. `environment/Dockerfile`
  describes the tree as `re-encoded to UTF-8 and trimmed as recorded in the
  authoring notes`. Upstream's `ise/ext/dbi/mysql/depend/` also vendored MySQL
  AB client headers under GPL-2.0; that whole tree is removed and none of it is
  redistributed in the task.
- Migration steps under `steps/` add benchmark-authored instructions, tests,
  workdirs and solutions; no retained upstream source file is otherwise edited.

## Third-party build dependencies

The build requires Boost (found with `find_package(Boost REQUIRED)` in
`environment/tree/build/linux/CMakeLists.txt`) and CMake. Both are host build
dependencies installed by the image; neither is vendored or redistributed in the
task tree. The retained task tree contains no third-party source code or data.

## Required notices

- ISE: `Copyright 2013 HaoXinGeng (haoxingeng@gmail.com)`, Apache-2.0, per the
  file headers. The upstream `LICENSE` file itself contains only the standard
  Apache-2.0 template copyright placeholder.
- The pinned tree contains no upstream `NOTICE` file.

Task-authored benchmark content is licensed separately by the benchmark maintainers.
