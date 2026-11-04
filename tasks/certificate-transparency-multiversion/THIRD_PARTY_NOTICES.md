# Third-Party Notices

Task: `certificate-transparency-multiversion` — build the RFC 6962 Certificate
Transparency stack from nothing, then carry it through thirteen cumulative
upstream releases from certificate-transparency-go v1.0 to v1.3.3.

## Upstream project and pins

| item | value |
| --- | --- |
| Project | certificate-transparency-go |
| URL | https://github.com/google/certificate-transparency-go |
| Base release | v1.0 (2017-09-11), commit `abb79e468b6f3bbd48d1ab0c9e68febf80d52c4d` |
| Final release | v1.3.3 (2026-02-26), commit `e8f93173135c7817ebd7133dab729c4576ce9a21` |
| What is bundled/derived | Step 01's `solution/payload.tar.gz` is the upstream v1.0 source tree (sources, upstream tests, testdata, protos, scripts and upstream `LICENSE`). Steps 02–13 carry `transition.patch`, a unified diff for the exact upstream release-to-release change. Upstream's 2017 `vendor/` tree, `go.mod` and `go.sum` are deliberately excluded from the payload because the benchmark supplies its own build manifest. |

### Release map

Each transition patch is derived from the upstream diff between the two listed
commits.

| step | release | date | commit |
| --- | --- | --- | --- |
| 01-rfc6962-stack | v1.0 | 2017-09-11 | `abb79e468b6f3bbd48d1ab0c9e68febf80d52c4d` |
| 02-multiple-backends | v1.0.5 | 2017-11-24 | `ae40d07cce12f1227c6e658e61c9dddb7646f97b` |
| 03-signing-correctness | v1.0.10 | 2018-02-23 | `1bec4527572c443752ad4f2830bef88be0533236` |
| 04-ctutil-verification | v1.0.13 | 2018-03-21 | `480c3654a70c5383b9543ec784203030aedbd3a5` |
| 05-outsourced-proofs | v1.0.17 | 2018-05-08 | `3c3d22ce946447d047a03228ebb4a41e3e4eb15b` |
| 06-quota-and-migration | v1.0.19 | 2018-05-24 | `8736a411b4ff214ea20687e46c2b67d66ebd83fc` |
| 07-key-compliance-and-gossip | v1.0.21 | 2018-08-17 | `3629d6846518309d22c16fee15d1007262a459d2` |
| 08-admission-inversion | v1.1.0 | 2019-11-14 | `ba4e6e2f6e40512709db1451c58ecb13b1bea13e` |
| 09-flag-lifecycle-and-lax-parsing | v1.1.2 | 2021-09-21 | `9df679d49f8d16130c6c42334430ffc54a9bd074` |
| 10-client-rebuild-and-witness | v1.1.3 | 2022-05-13 | `5162ff69d3bda920f809f3e33dc3049b011ca935` |
| 11-loglist-v1-removal | v1.1.4 | 2022-10-18 | `50ef8506f84ce8b0b1c949d0feae763144e6974c` |
| 12-issuance-chain-storage | v1.2.0 | 2024-05-28 | `0c9c98f339b9304a5ffbff496c2b032883319607` |
| 13-transport-and-tiled-logs | v1.3.3 | 2026-02-26 | `e8f93173135c7817ebd7133dab729c4576ce9a21` |

## License table

| SPDX ID | File under `LICENSES/` | Covers |
| --- | --- | --- |
| Apache-2.0 | `Apache-2.0.txt` | certificate-transparency-go at every listed release. Exact upstream `LICENSE` extracted from `steps/01-rfc6962-stack/solution/payload.tar.gz` (path `LICENSE`). |
| BSD-3-Clause | `BSD-3-Clause.txt` | Go-derived packages in the upstream tree (`asn1/`, `x509/`, and other files with `The Go Authors` headers). Exact Go project `LICENSE`, copyright 2009 The Go Authors. |
| Apache-2.0 | `Prometheus-LICENSE.txt` | `third_party/prometheus/` in the v1.0 payload (`console_libs/prom.lib`, `console_libs/menu.lib`, and its `LICENSE`). Exact upstream third-party license. |

The upstream tree contains no `NOTICE` file at the listed pins. The upstream
`LICENSE` is the Apache-2.0 text without a project-specific copyright line.

## Modifications made by the benchmark

- Step 01 ships the v1.0 source tree as a payload; `solve.sh` extracts it over
  the seeded workspace. The payload omits upstream's `vendor/`, `go.mod` and
  `go.sum`; the benchmark supplies the build manifest.
- Steps 02–13 ship upstream release-to-release diffs as `transition.patch`
  and apply them with `git apply --binary`. The patches are the upstream
  changes, not benchmark rewrites of the source.
- No upstream license file is modified. `third_party/prometheus/LICENSE` is
  preserved verbatim in the payload.

## Third-party dependencies bundled in the task

At v1.0 the payload includes `third_party/prometheus/` with its Apache-2.0
`LICENSE`; the two `console_libs/*.lib` files are Go HTML templates that load
the Prometheus console library assets. They are covered by
`LICENSES/Prometheus-LICENSE.txt`.

The Go module dependencies named by `environment/assets/gomod/*/go.mod` are
fetched into the Docker image's module cache at image build time from the
pinned manifests. They are not vendored in the task tree, and no module source
is redistributed here. Their own upstream licenses apply to those modules; the
cache is not part of the task repository contents.

## Required notices

- certificate-transparency-go source files carry Apache-2.0 headers. Typical
  exact lines in the payload include
  `Copyright 2016 Google Inc. All Rights Reserved.` and
  `Copyright 2014 Google Inc. All Rights Reserved.`; the year varies by file.
- Go-derived files carry BSD-3-Clause headers. Exact lines include
  `Copyright 2009 The Go Authors. All rights reserved.` and
  `Copyright 2011 The Go Authors. All rights reserved.`; the year varies by
  file.
- `third_party/prometheus/LICENSE` is the Apache-2.0 text. Prometheus is a
  project of the Cloud Native Computing Foundation; this task claims no
  endorsement.
- No Apache `NOTICE` file exists at the pins, so no NOTICE text is reproduced.

Task-authored benchmark content is licensed separately by the benchmark maintainers.
