# Third-Party Notices — icechunk-array-store-multiversion

**Task.** Eleven-step benchmark that reimplements Icechunk, a transactional
versioned storage engine for chunked N-dimensional array data, as a Rust
library, CLI and on-disk format, carrying the implementation through eleven
earth-mover/icechunk releases from `icechunk-v0.1.0-alpha.1` to `v2.2.0`.

## 1. Upstream project

| | |
| --- | --- |
| Project | Icechunk |
| URL | <https://github.com/earth-mover/icechunk> |
| Licence | Apache-2.0 from `icechunk-v0.1.0-alpha.8` onwards; the `icechunk-v0.1.0-alpha.1` manifests declare `MIT OR Apache-2.0` |
| Copyright | `Copyright 2024 Earthmover PBC` (appendix of the upstream `LICENSE`) |

Pinned releases, in step order:

| Step | Upstream tag | Commit |
| --- | --- | --- |
| 01-spec1-repository-format | `icechunk-v0.1.0-alpha.1` | `e20f7e8663eedb737c712cdbcceeec21cc3b1b2e` |
| 02-sessions-conflicts-virtual-chunks | `icechunk-v0.1.0-alpha.8` | `2030ff6546d784215dd887dd79375c1fc171ac05` |
| 03-expiration-gc-file-headers | `icechunk-v0.1.0` | `61df56c6ee94d358beb29eb1b5ceefb3a642c9f0` |
| 04-flatbuffers-format | `v0.2.0` | `c1dc9214c4e65f21e5da85fd7b6435bf4041a867` |
| 05-cli-expiration-accounting-stores | `v0.2.12` | `cdc97764904fb2f649277419752dfaa5b96ab7ea` |
| 06-manifest-splitting-format-1-0 | `v1.0.0` | `e94daa76831310ead138b594870061a0ef36bdab` |
| 07-async-api | `v1.1.0` | `1f6804fda42ae00bcd3c919c6ecfc41d45f805bd` |
| 08-backends-reliability-forward-compat | `v1.1.21` | `4cad7551f1be44a04ccb583a79ab8a7354787dca` |
| 09-icechunk-2-table-of-contents | `v2.0.0` | `a75341fc9d14dff55fd4ef7c314d12c15d9f3342` |
| 10-spec-2-1-additive | `v2.1.0` | `83f61cf3ac0cb2d2a4f7827eb90d2a08177b6406` |
| 11-headers-presets-hardening | `v2.2.0` | `6e9b0ba9a183f9fce238f67809578af9dfbf5ca8` |

## 2. License files

| SPDX ID | File under `LICENSES/` | Upstream component / versions covered |
| --- | --- | --- |
| Apache-2.0 | `Apache-2.0.txt` | Exact upstream `LICENSE` (11,344 bytes) as committed from `icechunk-v0.1.0-alpha.8` through `v2.2.0`; its appendix names `Copyright 2024 Earthmover PBC` |
| MIT | `MIT.txt` | SPDX canonical MIT text, covering the `license = "MIT OR Apache-2.0"` declaration in `icechunk/Cargo.toml` at `icechunk-v0.1.0-alpha.1`. No upstream `LICENSE` file, copyright line or notice file exists at that tag, so no copyright holder is asserted for it |

## 3. What is bundled or derived

- `steps/01-spec1-repository-format/solution/icechunk-alpha.1-source.tar.gz` —
  the complete upstream worktree at `icechunk-v0.1.0-alpha.1` (29 Rust source
  files, upstream `Cargo.lock`, `spec/icechunk-spec.md`, docs and project
  files).
- `steps/02..11/solution/release-*.patch` — exact upstream
  `git diff --binary --no-renames` transitions from each pinned tag to the
  next.
- `steps/*/workdir/icechunk-spec/` — documentation copied verbatim from the
  tag each step is anchored to: `spec/icechunk-spec.md` becomes `spec.md`, and
  pages under `docs/docs/` become `guide/`, `design/`, `archive/`,
  `flatbuffers/` and `reference/`. The directory's `README.md`, `PROFILE.md`,
  `NOTICE` and `SHA256SUMS` are task-authored; the upstream `LICENSE` text is
  also stored there.
- Task-authored: `instruction.md`, `tests/`, the step-1 scaffold
  (`icechunk/Cargo.toml`, `src/lib.rs`), `TEST_INTERFACE.md`,
  `WORKSPACE_HANDOFF.md` and `timer.sh`.

## 4. Modifications made by the benchmark

- Step 1 installs the unmodified upstream worktree at
  `icechunk-v0.1.0-alpha.1` over the seeded scaffold, replacing the scaffold's
  `icechunk/` directory and manifests.
- Steps 2–11 apply the upstream release-to-release diffs over the cumulative
  workspace. No upstream source is hand-edited.
- Specification documents are copied verbatim and are not edited, trimmed or
  annotated. Only their paths and file names are normalised.
- `icechunk-v0.1.0-alpha.1` predates the repository's committed `LICENSE`
  file. The Apache-2.0 text stored in each `icechunk-spec/` directory is the
  file the project committed at the next checkpoint; the task-authored `NOTICE`
  in that directory records this.

## 5. Third-party dependencies bundled in the task

The Rust dependency graph is declared in the task-owned `Cargo.lock` files and
prefetched into the image's Cargo registry cache at build time. Crates are not
vendored in this repository, and no third-party crate source tree is stored in
the task directory.

No upstream release in the pin range ships a `NOTICE` file; the root `NOTICE`
path returns 404 for `icechunk-v0.1.0-alpha.8`, `icechunk-v0.1.0` and
`v2.2.0`.

## 6. Required notices

- Apache-2.0: the upstream `LICENSE` appendix carries
  `Copyright 2024 Earthmover PBC`.
- `icechunk-v0.1.0-alpha.1`: the only licence statement is the manifest field
  `license = "MIT OR Apache-2.0"`; the project supplied no `LICENSE` file at
  that tag. The MIT text is reproduced in `LICENSES/MIT.txt` and the
  Apache-2.0 text in `LICENSES/Apache-2.0.txt`.
- No trademark statement is made in the upstream `LICENSE`.

Task-authored benchmark content is licensed separately by the benchmark maintainers.
