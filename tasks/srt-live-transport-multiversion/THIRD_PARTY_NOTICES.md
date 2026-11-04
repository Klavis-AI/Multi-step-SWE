# Third-party notices — srt-live-transport-multiversion

Implement the SRT secure reliable live transport across eleven public release
milestones of Haivision/srt, graded against the pinned SRT and UDT protocol
drafts and the upstream API documentation through the installed public C API.

## Upstream projects

| | |
|---|---|
| Project | Haivision SRT |
| URL | https://github.com/Haivision/srt |
| Releases pinned | v1.2.0, v1.3.0, v1.3.2, v1.3.3, v1.4.0, v1.4.2, v1.4.3, v1.4.4, v1.5.0, v1.5.2, v1.5.7 |
| Bundled source | `steps/01-udt-core-and-srt-extensions/solution/srt-v1.2.0-source.tar.gz` — the complete v1.2.0 release tree |
| Derived source | `steps/<step>/solution/release-v<from>-to-v<to>.patch` — unified diffs that reconstruct each later release from the previous one over the cumulative `/app` tree |
| Bundled documentation | Selected `docs/` files copied byte for byte from the tag named in each step's `workdir/srt-docs/PROVENANCE.md` (v1.3.0 for steps 01-02, then the tag matching each later release) |
| Protocol documents | `steps/01-udt-core-and-srt-extensions/workdir/srt-spec/draft-gg-udt-03.txt` and `draft-sharabayko-srt-01.txt`, copied unmodified |

The release patches are generated from the upstream release trees; `solve.sh`
applies them with `git apply`. Upstream's `docs/` directory first appears in
v1.3.0, so the first two steps use that tag's documents as a superset.

## License table

| SPDX ID | File under `LICENSES/` | Covers |
|---|---|---|
| LGPL-2.1-or-later | `LICENSES/LGPL-2.1-or-later.txt` | SRT v1.2.0 (`COPYING`, SHA-256 `dc626520dcd53a22f727af3ee42c770e56c97a64fe3adb063799d8ab032fe551`); source headers offer "either version 2.1 of the License, or (at your option) any later version" |
| MPL-2.0 | `LICENSES/MPL-2.0.txt` | SRT v1.3.0 and later (`LICENSE`, SHA-256 `fab3dd6bdab226f1c08630b1dd917e11fcb4ec5e1e020e2c16f83a0a13863e85`; identical at v1.5.7) |
| BSD-3-Clause | `LICENSES/BSD-3-Clause-UDT.txt` | UDT4 code embedded in `srtcore/` (Copyright (c) 2001-2011, The Board of Trustees of the University of Illinois) |
| IETF Trust / BCP 78 (in-document notice) | `LICENSES/IETF-Trust-Notice.txt` | `draft-gg-udt-03` (2010) and `draft-sharabayko-srt-01` (2021) |

The relicensing boundary is v1.3.0: v1.2.0 ships `COPYING` (GNU LGPL 2.1) and
every source header grants LGPL-2.1-or-later; from v1.3.0 the tree ships
`LICENSE` (MPL 2.0) and the headers carry the MPL 2.0 notice. The `haicrypt/`
subsystem follows the same boundary.

## Version-to-license map

| Step | Release | Documentation tag | Solution payload | License |
|---|---|---|---|---|
| `01-udt-core-and-srt-extensions` | v1.2.0 | v1.3.0 | full v1.2.0 source tarball | LGPL-2.1-or-later (+ UDT BSD-3-Clause) |
| `02-hsv5-streamid-and-apps` | v1.3.0 | v1.3.0 | v1.2.0 → v1.3.0 patch | MPL-2.0 |
| `03-strict-encryption-and-km-state` | v1.3.2 | v1.3.2 | v1.3.0 → v1.3.2 patch | MPL-2.0 |
| `04-cryspr-and-congctl` | v1.3.3 | v1.3.3 | v1.3.2 → v1.3.3 patch | MPL-2.0 |
| `05-packet-filter-and-fec` | v1.4.0 | v1.4.0 | v1.3.3 → v1.4.0 patch | MPL-2.0 |
| `06-srctime-and-access-control` | v1.4.2 | v1.4.2 | v1.4.0 → v1.4.2 patch | MPL-2.0 |
| `07-declarative-socket-options` | v1.4.3 | v1.4.3 | v1.4.2 → v1.4.3 patch | MPL-2.0 |
| `08-main-backup-bonding-and-timing` | v1.4.4 | v1.4.4 | v1.4.3 → v1.4.4 patch | MPL-2.0 |
| `09-group-api-and-receiver-buffer` | v1.5.0 | v1.5.0 | v1.4.4 → v1.5.0 patch | MPL-2.0 |
| `10-aead-and-sender-buffer` | v1.5.2 | v1.5.2 | v1.5.0 → v1.5.2 patch | MPL-2.0 |
| `11-rexmit-shaping-and-hardening` | v1.5.7 | v1.5.7 | v1.5.2 → v1.5.7 patch | MPL-2.0 |

## Modifications made by the benchmark

- The v1.2.0 source tarball is the upstream release tree, unmodified except
  that it is stored as a tarball.
- Each later release is reconstructed by applying a unified diff between the
  two upstream release trees. The patches are benchmark-generated artefacts;
  they are not files published by upstream.
- The documents under `workdir/srt-docs/` are subsets of the named upstream
  tag's `docs/` directory, copied byte for byte; the `PROVENANCE.md` in each
  step records the upstream SHA-256 prefix of every file. `PROVENANCE.md`
  itself is task-authored.
- The two Internet-Drafts are copied unmodified.
- `PROFILE.md`, `TEST_INTERFACE.md`, `WORKSPACE_HANDOFF.md`, `timer.sh`, the
  instructions, harnesses and verifiers are authored benchmark content. The
  starting `/app` workspace contains no SRT source.
- The build-time `scripts/googletest*.cmake` files in later upstream releases
  can fetch GoogleTest when the upstream test suite is enabled; the task's
  own build does not enable it and no GoogleTest source is bundled.

## Third-party dependencies bundled in the task

| Component | Version | License | Bundled path |
|---|---|---|---|
| UDT4 | 4.11 (as incorporated by SRT) | BSD-3-Clause | `srtcore/*` inside the v1.2.0 tarball and the release patches |
| Internet-Draft `draft-gg-udt-03` | April 12, 2010 | IETF Trust / BCP 78 | `steps/01-udt-core-and-srt-extensions/workdir/srt-spec/draft-gg-udt-03.txt` |
| Internet-Draft `draft-sharabayko-srt-01` | September 2021 | IETF Trust / BCP 78 | `steps/01-udt-core-and-srt-extensions/workdir/srt-spec/draft-sharabayko-srt-01.txt` |
| SRT documentation | per-step tag (v1.3.0 or later) | MPL-2.0 | `steps/<step>/workdir/srt-docs/` |

No other third-party code is bundled. OpenSSL is a build dependency of the
image (`libssl-dev`) and is not copied into the task tree.

## Required notices

SRT v1.2.0 source headers:

> SRT - Secure, Reliable, Transport
> Copyright (c) 2017 Haivision Systems Inc.

SRT v1.3.0 and later source headers:

> SRT - Secure, Reliable, Transport
> Copyright (c) 2018 Haivision Systems Inc.
> This Source Code Form is subject to the terms of the Mozilla Public License,
> v. 2.0.

UDT4:

> Copyright (c) 2001 - 2011, The Board of Trustees of the University of
> Illinois. All rights reserved.

The v1.2.0 `srtcore/core.cpp` header also records "Based on UDT4 SDK version
4.11".

Internet-Drafts carry the IETF Trust copyright notices reproduced verbatim in
`LICENSES/IETF-Trust-Notice.txt`; `draft-sharabayko-srt-01` additionally
states that code components extracted from it must include the Simplified BSD
License text of BCP 78 Section 4.e.

The task is not affiliated with or endorsed by Haivision Systems Inc. or the
SRT project.

Task-authored benchmark content is licensed separately by the benchmark maintainers.
