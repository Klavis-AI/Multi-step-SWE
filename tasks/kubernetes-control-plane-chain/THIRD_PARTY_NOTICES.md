# Third-Party Notices — kubernetes-control-plane-chain

**Task.** Nine-step benchmark that builds a C11 Kubernetes control plane across
the 1.25 to 1.33 release chain, graded against a corpus recorded from each
release's own control-plane binaries and driven by that release's kubectl.

## 1. Upstream project

| | |
| --- | --- |
| Project | Kubernetes |
| Code URL | <https://github.com/kubernetes/kubernetes> |
| Code pin | tag `v1.33.13`, commit `c592dbba68d0a438f2f49204aeecb1a1390da27b` |
| Documentation URL | <https://github.com/kubernetes/website> |
| Documentation pin | `release-1.25` through `release-1.33` branches (commits below) |

Pinned documentation branches:

| Step | Release | kubernetes/website commit |
| --- | --- | --- |
| 01-baseline-1-25 | 1.25 | `8eb33afd12b1820c90a19c27d7a059006285c7e9` |
| 02-cel-admission-1-26 | 1.26 | `eedf3e0e54d5678b5acc4c9b7a4abd6b75227ad7` |
| 03-aggregated-discovery-1-27 | 1.27 | `f173f35615f0e87a49d963f28908cf25ebc538af` |
| 04-sidecars-jobs-1-28 | 1.28 | `6b57c3094180a6a8093f9de80a85488da77031ed` |
| 05-apf-ga-1-29 | 1.29 | `bbe3056985c0d736a9e806462194c2fa7a793df8` |
| 06-vap-ga-1-30 | 1.30 | `c28dcd8393562ddc6adf89b920ad3c396166a31b` |
| 07-beta-apis-1-31 | 1.31 | `11f92a075a2a6b97448d8bb586ad7eac14871e04` |
| 08-resize-images-1-32 | 1.32 | `a6b9d383dfe5cba5d846d3f05475c6cc25f3d2d7` |
| 09-resize-subresource-1-33 | 1.33 | `b9ab6105d0e3b6824dc368d3dd3b101fb4774575` |

The `k8s.io` API definitions are pinned to the release tags
`kubernetes-1.32.13` and `kubernetes-1.33.13` of `kubernetes/api`,
`kubernetes/apimachinery`, `kubernetes/apiextensions-apiserver` and
`kubernetes/kube-aggregator`. The CEL language definition is
`google/cel-spec` tag `v0.7.1` (commit `ebff24990ecc57209ab9f9b28896fd171848274f`).

## 2. License files

| SPDX ID | File under `LICENSES/` | Upstream component / versions covered |
| --- | --- | --- |
| Apache-2.0 | `Apache-2.0.txt` | Exact Kubernetes `v1.33.13` root `LICENSE` (11,358 bytes). Also the licence of the four `k8s.io` repositories at `kubernetes-1.32.13` / `kubernetes-1.33.13` and of `cel-spec v0.7.1`; their `LICENSE` files are byte-identical to this text |
| CC-BY-4.0 | `CC-BY-4.0.txt` | Exact `kubernetes/website` `LICENSE` (18,657 bytes), covering the Markdown documentation under `environment/docs/kubernetes-docs/` |

## 3. What is bundled or derived

- `environment/docs/kubernetes-docs/<minor>/` — 37 Markdown pages of the
  Kubernetes API concepts, CEL and component reference documentation, copied
  from the `kubernetes/website` `release-<minor>` branches listed above, plus
  that repository's `LICENSE`.
- `environment/docs/kubernetes-proto/<minor>/k8s.io/<repo>/...` — 139
  `generated.proto` files (and the four repository `LICENSE` files) from
  `kubernetes-1.32.13` and `kubernetes-1.33.13`, laid out by Go import path.
- `environment/docs/cel-spec/v0.7.1/` — `langdef.md` and `LICENSE` from
  `google/cel-spec` `v0.7.1`.
- `environment/docs/SOURCES.txt`, `environment/docs/SHA256SUMS` and
  `environment/docs-provenance.txt` — task-authored provenance metadata.
- Task-authored: `instruction.md`, `tests/` and the C11 control-plane sources
  in each `steps/*/solution/` tree.

## 4. Modifications made by the benchmark

- The documentation and `.proto` files are stored byte-for-byte as fetched;
  `SOURCES.txt` records the upstream URL and SHA-256 of every file, and the
  image build verifies them.
- The only changes are path normalisation: each document is placed under
  `kubernetes-docs/<minor>/` and each proto file under its Go import path
  below `kubernetes-proto/<minor>/`.
- No upstream code is included in the step trees; the control-plane
  implementation, tests and instructions are task-authored C.

## 5. Third-party dependencies bundled in the task

The task directory contains only the documentation and protocol files listed
above; no third-party source library is vendored. The Docker image installs
these runtime tools at build time:

| Component | Version | License | Bundled path |
| --- | --- | --- | --- |
| etcd | `v3.5.21` | Apache-2.0 | image `/usr/local/bin/etcd`, `etcdctl` |
| kubectl | `v1.25.16`, `v1.26.15`, `v1.27.16`, `v1.28.15`, `v1.29.15`, `v1.30.14`, `v1.31.14`, `v1.32.13`, `v1.33.13` | Apache-2.0 | image `/usr/local/bin/kubectl-<minor>` and `kubectl` |
| Node.js | `v24.22.0` | MIT, with bundled components under their own licenses | image `/usr/local` |

`LICENSES/Apache-2.0.txt` covers etcd and kubectl. The Node.js distribution's
own `LICENSE` file carries its MIT text and the notices of its bundled
components; those terms travel with the image.

## 6. Required notices

- Apache-2.0: neither `kubernetes/kubernetes` at `v1.33.13` nor
  `kubernetes/website` at the pinned branches ships a `NOTICE` file (both
  paths return 404). The `k8s.io` repositories and `cel-spec` also ship no
  `NOTICE`. No Apache NOTICE text is therefore reproduced.
- CC BY 4.0 attribution for the documentation: the Markdown pages are
  "Kubernetes Documentation" by The Kubernetes Authors, from
  <https://github.com/kubernetes/website>, licensed under Creative Commons
  Attribution 4.0 International. They are redistributed here unmodified.
  The license text is in `LICENSES/CC-BY-4.0.txt`.
- Trademark: Kubernetes is a registered trademark of The Linux Foundation.
  This task is not affiliated with or endorsed by The Linux Foundation or the
  Kubernetes project.

Task-authored benchmark content is licensed separately by the benchmark maintainers.
