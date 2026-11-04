# Third-Party Notices — git-format-protocol-chain

Benchmark task that implements git's format and protocol chain in PHP across
eight steps, graded with git's own `t/` suite, bidirectional interop against
pinned real git builds, and fsck parity on a repository corpus.

## Upstream project

| | |
|---|---|
| Project | Git |
| URL | https://github.com/git/git |
| Pins | `v2.20.1` (sha256 `9d2e91e2faa2ea61ba0a70201d023b36f54d846314591a002c610ea2ab81c3e9`), `v2.39.5` (sha256 `c58da92c378df4a986ca33266897a7397e86c22ee266a284d8c2432c39066b59`), `v2.51.0` (sha256 `60a7c2251cc2e588d5cd87bae567260617c6de0c22dca9cdbfc4c7d2b8990b62`) |
| Source | https://mirrors.edge.kernel.org/pub/software/scm/git/git-<version>.tar.xz |
| Bundled/derived | No git source is present in the task tree. The environment and verifier images build the three pinned git releases from their upstream release tarballs; the `v2.51.0` build tree is kept at `/opt/git-suite` as git's own test suite. The PHP implementation, step instructions, corpora and verifiers are task-authored. |

## License table

| SPDX ID | File under `LICENSES/` | Covers |
|---|---|---|
| GPL-2.0 | `GPL-2.0.txt` | Git `v2.20.1`, `v2.39.5` and `v2.51.0`, including the `v2.51.0` test suite at `/opt/git-suite` (exact upstream `COPYING` from `git/git` at `v2.51.0`, including Git's preamble that only version 2 of the GPL applies) |

Git's `COPYING` states that "the only valid version of the GPL as far as this
project is concerned is _this_ particular version of the license (ie v2, not
v2.2 or v3.x or whatever), unless explicitly otherwise stated." The suites and
peer builds are therefore GPL-2.0, not GPL-2.0-or-later.

## Modifications made by the benchmark

- Git sources are built from the unmodified upstream `.tar.xz` releases with
  `NO_GETTEXT=1 NO_PERL_CPAN_FALLBACKS=1`; no source file is patched.
- The `v2.51.0` build tree is pruned after building to `t/`, `templates/`,
  `Documentation/` and `GIT-BUILD-OPTIONS`, which is the subset the test suite
  needs. The `v2.20.1` and `v2.39.5` trees are installed to `/opt/gitpeer/`
  and removed from the build stage.
- The PHP git implementation, the step ladder, the instructions, the
  differential corpora and the verifiers are task-authored.

## Third-party dependencies bundled in the task

None. No third-party source or binary is shipped inside the task tree. The
container images carry the following image-only components:

| Component | Version | License | Image path |
|---|---|---|---|
| Git (peer implementation) | 2.20.1 | GPL-2.0 | `/opt/gitpeer/v2.20.1/bin/git` |
| Git (peer implementation) | 2.39.5 | GPL-2.0 | `/opt/gitpeer/v2.39.5/bin/git` |
| Git (peer implementation and test suite) | 2.51.0 | GPL-2.0 | `/opt/gitpeer/v2.51.0/bin/git`, `/opt/git-suite/` |

## Required notices

- GPL-2.0: Git's `COPYING` is the GNU General Public License version 2 text
  with Git's preamble; the full text is reproduced in `LICENSES/GPL-2.0.txt`.
- Git's `README.md` records that Git "was originally written by Linus Torvalds
  with help of a group of hackers around the net." Copyright in Git is held by
  its contributors; individual source files carry their own copyright lines.
- Git ships no `NOTICE` file, so no additional NOTICE text applies.
- The complete corresponding source for the GPL-2.0 components is the upstream
  `git-<version>.tar.xz` at the kernel.org URL above, identified by the sha256
  values in the pin table.

Task-authored benchmark content is licensed separately by the benchmark maintainers.
