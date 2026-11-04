# Third-Party Notices — go-compiler-version-chain

Benchmark task that builds a Go toolchain in C and carries it from the Go 1.0
language through Go 1.25, one release train per step.

## Upstream project

| | |
|---|---|
| Project | Go (`golang/go`) |
| URL | https://github.com/golang/go |
| Pin | tag `go1.25.1` |
| Project | Go website (`golang/website`) — Go Modules Reference |
| URL | https://github.com/golang/website |
| Pin | commit `e007c7cd238526bfd5b680cbff3d3d0c25f51020` (2025-06-10) |
| Bundled/derived | `environment/docs/go/cmd-go/` holds `alldocs.go` from `src/cmd/go/alldocs.go` at `go1.25.1` and `cmd-go.txt`, a plain-text rendering made by removing the `//` comment markers. `environment/docs/go/ref-mod/mod.md` holds `_content/ref/mod.md` from `golang/website` at the pin. Each directory carries the upstream `LICENSE`. The C compiler and runtime ladder, instructions and verifiers are task-authored. |

## License table

| SPDX ID | File under `LICENSES/` | Covers |
|---|---|---|
| BSD-3-Clause | `BSD-3-Clause.txt` | Go `go1.25.1` and `golang/website` at `e007c7cd` — exact upstream `LICENSE` (copy at `environment/docs/go/cmd-go/LICENSE`; the `ref-mod/LICENSE` copy is byte-identical) |

The docs manifest at `environment/docs/SOURCES.txt` records the sha256 of each
file, including both `LICENSE` copies.

## Modifications made by the benchmark

- `cmd-go.txt` is produced from `alldocs.go` by stripping the leading `//`
  comment markers and dropping the trailing `package main` line; the text is
  otherwise unchanged.
- `alldocs.go` and `mod.md` are copied verbatim.
- The C Go compiler, the release ladder, the step instructions and the
  verifiers are task-authored.

## Third-party dependencies bundled in the task

None. The only upstream content in the task tree is the Go documentation and
its license. The container image installs the upstream Go 1.25.1
`linux-amd64` toolchain (BSD-3-Clause) to build and run the authored C
toolchain; that toolchain is image-only and is not redistributed in the task
tree.

## Required notices

- BSD-3-Clause: the upstream `LICENSE` carries `Copyright 2009 The Go
  Authors.` and the three BSD-3-Clause conditions, including the clause that
  neither the name of Google LLC nor the names of its contributors may be used
  to endorse or promote products derived from the software without specific
  prior written permission.
- The Go project ships no `NOTICE` file with this material, so no additional
  NOTICE text applies.
- The Go name and the Go gopher are associated with the Go project; the
  BSD-3-Clause license grants no trademark rights.

Task-authored benchmark content is licensed separately by the benchmark maintainers.
