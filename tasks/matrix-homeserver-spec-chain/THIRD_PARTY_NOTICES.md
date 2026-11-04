# Third-Party Notices — matrix-homeserver-spec-chain

**Task.** Eight-step benchmark that carries a Matrix homeserver written in C
from Client-Server API v1.1 to v1.15, adding federation, end-to-end-encryption
surfaces, threads and spaces, authenticated media, sliding sync and
next-generation auth.

## 1. Upstream projects

| | |
| --- | --- |
| Project | Matrix specification |
| URL | <https://github.com/matrix-org/matrix-spec> |
| Bundled releases | tags `v1.1`, `v1.2`, `v1.3`, `v1.6`, `v1.9`, `v1.11`, `v1.13`, `v1.15` |
| Project | Matrix specification proposals |
| URL | <https://github.com/matrix-org/matrix-spec-proposals> |
| Bundled document | `proposals/4186-simplified-sliding-sync.md` at commit `ffc54bc05c827edfb9791cfd7c154837c8487204` |
| Project | Dendrite (interface reference only) |
| URL | <https://github.com/matrix-org/dendrite> |
| Bundled excerpts | tag `v0.13.8`, commit `763c79f1421200c3fd74401fd68298bc00ecefbc` |

Pinned specification releases, in step order:

| Step | Release | matrix-spec commit |
| --- | --- | --- |
| 01-cs-v1.1 | `v1.1` | `43ce9127aeb4400f76e07b64953f15a6899a35bc` |
| 02-federation | `v1.2` | `fb2aa688db46cb55bcda1a4ff52b74a9e5c039b2` |
| 03-e2ee-surfaces | `v1.3` | `b05149b64e1d771573ec9b443c6a120b3b33c3d1` |
| 04-v1.4-v1.6 | `v1.6` | `029e9888359c02f52e5a61a328af94108a4b638b` |
| 05-v1.7-v1.9 | `v1.9` | `0c099e3b796028c495ad7b6cb3bd544200a102f2` |
| 06-v1.10-v1.11 | `v1.11` | `1fc8f8856fe47849f90344cfa91601c984627acb` |
| 07-v1.12-v1.13 | `v1.13` | `bc995e66d64b1376d0923333fce503dbf30a26c8` |
| 08-v1.14-v1.15 | `v1.15` | `40065811a143cd3175f5ccfeff87e926c6f0e452` |

The fifteen release changelogs under `environment/docs/matrix-spec/changelog/`
and the matrix-spec `LICENSE` were fetched at the `v1.15` commit
`40065811a143cd3175f5ccfeff87e926c6f0e452`. MSC4186 and the
matrix-spec-proposals `LICENSE` were fetched at
`ffc54bc05c827edfb9791cfd7c154837c8487204`.

## 2. License file

| SPDX ID | File under `LICENSES/` | Upstream component / versions covered |
| --- | --- | --- |
| Apache-2.0 | `Apache-2.0.txt` | Exact Matrix specification `LICENSE` (10,174 bytes, ending at "END OF TERMS AND CONDITIONS"). The `LICENSE` files of matrix-spec-proposals at the MSC pin and of Dendrite `v0.13.8` are byte-identical to it, so one file covers all three upstream projects |

## 3. What is bundled or derived

- `steps/01..08/workdir/SPEC/<release>/` — the specification snapshot for
  each step, copied from the corresponding matrix-spec tag: the repository's
  `content/` and `data/` material relevant to the Client-Server and
  Server-Server APIs, plus `LICENSE`. File counts per snapshot are 363
  (`v1.1`), 379 (`v1.2`), 386 (`v1.3`), 404 (`v1.6`), 424 (`v1.9`),
  433 (`v1.11`), 436 (`v1.13`) and 449 (`v1.15`).
- `steps/01-cs-v1.1/workdir/DENDRITE/` — Dendrite `v0.13.8` excerpts that
  define the monolith's external interface: `LICENSE`, `dendrite-sample.yaml`,
  `cmd-dendrite-main.go`, `setup-flags.go` and the five pages under
  `docs-manual/`.
- `environment/docs/matrix-spec/changelog/v1.1.md` through `v1.15.md` and
  `environment/docs/matrix-spec/LICENSE` — the release changelogs and licence
  for the agent, copied verbatim at the `v1.15` commit.
- `environment/docs/matrix-spec-proposals/4186-simplified-sliding-sync.md`
  and its `LICENSE` — MSC4186, copied verbatim at the proposal pin.
- `environment/docs-provenance.txt` and `environment/docs/SHA256SUMS` —
  task-authored provenance metadata recording the source URL and SHA-256 of
  every document.
- Task-authored: `instruction.md`, `tests/`, each step's `workdir/setup.sh`,
  `selfcheck.sh` and `WORKSPACE_HANDOFF.md`, and the C homeserver sources in
  each `solution/` tree.

## 4. Modifications made by the benchmark

- Each `SPEC/<release>` directory flattens the upstream tag's `content/` and
  `data/` trees into one root. Included files are byte-for-byte identical to
  the tagged upstream files; only their paths change (for example,
  `content/client-server-api/_index.md` becomes
  `client-server-api/_index.md`, and `data/api/...` becomes `api/...`).
- The snapshots omit files that are not part of the homeserver's normative
  reference: the repository's site scaffolding, the application-service,
  identity-service and push-gateway API pages, the changelog and proposal
  indexes, and a few OpenAPI/schema files not referenced by the retained
  Client-Server and Server-Server documents. The release changelogs are
  supplied separately under `/opt/docs`.
- Dendrite files are renamed to make their role explicit:
  `cmd/dendrite/main.go` → `cmd-dendrite-main.go`, `setup/flags.go` →
  `setup-flags.go`, and `docs/installation/manual/<n>_<name>.md` →
  `docs-manual/<n>_<name>.md`. Contents are unchanged.
- The changelogs, MSC4186 and the three `LICENSE` files are stored verbatim;
  `SHA256SUMS` records their SHA-256 values.
- No upstream implementation is included in the step trees. The homeserver,
  tests and instructions are task-authored, and the solution may not vendor or
  wrap an existing homeserver.

## 5. Third-party dependencies bundled in the task

The task directory contains the specification snapshots, MSC4186, the Dendrite
interface excerpts and the documentation files listed above; no third-party
source library is vendored. The Docker image is built from `debian:bookworm-slim`
with ordinary C development packages (OpenSSL, SQLite, libyaml, libcurl, zlib,
libmagic) and installs Node.js 24.22.0 for the agent harness. Those are
image-level components under their own distribution licenses, not files stored
in this task directory.

## 6. Required notices

- Apache-2.0: matrix-spec at `v1.15`, matrix-spec-proposals at the MSC pin and
  Dendrite at `v0.13.8` all return 404 for `NOTICE`. No Apache NOTICE text is
  therefore reproduced.
- Copyright lines: the matrix-spec and matrix-spec-proposals `LICENSE` files
  carry no copyright line. The two Dendrite Go excerpts carry their upstream
  headers verbatim: `Copyright 2017 Vector Creations Ltd` in
  `cmd-dendrite-main.go` and `Copyright 2020 The Matrix.org Foundation C.I.C.`
  in `setup-flags.go`.
- MSC4186 is a Matrix specification proposal published under the same
  Apache-2.0 repository licence.
- No trademark statement is made in the bundled `LICENSE` file or in the
  copied specification, proposal and Dendrite documents.

Task-authored benchmark content is licensed separately by the benchmark maintainers.
