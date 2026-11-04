# Third-Party Notices — iceberg-table-format-multiversion

**Task.** Five-step benchmark that reimplements the Apache Iceberg table format
as a Rust library, carrying the implementation through the iceberg-rust
releases from `0.2.0` to `v0.6.0`; the format specification is taken from
Apache Iceberg `apache-iceberg-1.9.1`.

## 1. Upstream projects

| Project | URL | Pin | Bundled or derived content |
| --- | --- | --- | --- |
| Apache Iceberg | <https://github.com/apache/iceberg> | tag `apache-iceberg-1.9.1`, commit `f40208ae6fb2f33e578c2637d3dea1db18739f31` | `spec.md`, `view-spec.md`, `puffin-spec.md`, `gcm-stream-spec.md`, `LICENSE` and `NOTICE`, copied verbatim into `steps/*/workdir/iceberg-spec/` |
| iceberg-rust | <https://github.com/apache/iceberg-rust> | `0.2.0` commit `adef9bda28ad4c00bd837e0299be230c4789d90c` | Step 1 full `crates/iceberg` source tree (`steps/01-table-format-core/solution/iceberg-0.2.0-source.tar.gz`), including upstream `LICENSE` and `NOTICE` |
| iceberg-rust | <https://github.com/apache/iceberg-rust> | `v0.3.0` commit `79711acc921b4d25c3583bc0ea9628fe64a057e1`; `v0.4.0` commit `d93e5e822ebfa2b2ff654f86102489e55a8dc422`; `v0.5.1` commit `80feb07423d8c81b055c03fb577ad0e95a55df62`; `v0.6.0` commit `dfbab0da0000fe3f16e793415aeef642e529acee` | Exact upstream release-to-release diffs in `steps/02..05/solution/release-*.patch` |

Version chain:

| Step | Start | Target |
| --- | --- | --- |
| 01-table-format-core | scaffold | iceberg-rust `0.2.0` |
| 02-scan-planning | `0.2.0` | iceberg-rust `v0.3.0` |
| 03-metadata-evolution | `v0.3.0` | iceberg-rust `v0.4.0` |
| 04-row-level-deletes | `v0.4.0` | iceberg-rust `v0.5.1` |
| 05-transaction-actions | `v0.5.1` | iceberg-rust `v0.6.0` |

## 2. License files

| SPDX ID | File under `LICENSES/` | Upstream component / versions covered |
| --- | --- | --- |
| Apache-2.0 | `Apache-2.0.txt` | iceberg-rust `0.2.0` through `v0.6.0` — the exact upstream `LICENSE` (11,357 bytes, standard Apache-2.0 text) |
| Apache-2.0 | `Apache-2.0-Apache-Iceberg.txt` | Apache Iceberg `apache-iceberg-1.9.1` — the exact upstream `LICENSE` (15,980 bytes), which reproduces Apache-2.0 and appends the project's third-party attribution block |

## 3. What is bundled or derived

- `steps/01-table-format-core/solution/iceberg-0.2.0-source.tar.gz` — the
  complete `crates/iceberg` source tree at iceberg-rust `0.2.0`, together with
  the upstream `LICENSE` and `NOTICE`.
- `steps/02-scan-planning/solution/release-0.2.0-to-v0.3.0.patch`,
  `steps/03-metadata-evolution/solution/release-v0.3.0-to-v0.4.0.patch`,
  `steps/04-row-level-deletes/solution/release-v0.4.0-to-v0.5.1.patch` and
  `steps/05-transaction-actions/solution/release-v0.5.1-to-v0.6.0.patch` —
  upstream release-to-release source diffs.
- `steps/*/workdir/iceberg-spec/` — the four Apache Iceberg format documents,
  `LICENSE` and `NOTICE` from `apache-iceberg-1.9.1`, copied verbatim.
  `README.md`, `PROFILE.md` and `SHA256SUMS` in that directory are
  task-authored, not upstream.
- Task-authored: `instruction.md`, `tests/`, the step-1 scaffold
  (`Cargo.toml`, `src/lib.rs`), `Cargo.lock`, `TEST_INTERFACE.md`,
  `WORKSPACE_HANDOFF.md` and `timer.sh`.

## 4. Modifications made by the benchmark

- Step 1 installs the contents of upstream `crates/iceberg` at the repository
  root, over the seeded scaffold. The upstream workspace `Cargo.toml` is
  de-workspaced, and a `Cargo.lock` is added because iceberg-rust `0.2.0`
  committed none.
- Steps 2–5 apply the upstream release-to-release diffs with the crate root
  re-based from `crates/iceberg/...` to the de-workspaced `...` layout. The
  `Cargo.toml` and `Cargo.lock` files are task-owned.
- The four specification files are unmodified; no upstream file content is
  otherwise changed.
- `LICENSE` and `NOTICE` are reinstalled from the target upstream tag by each
  step's oracle.

## 5. Third-party dependencies bundled in the task

The Rust crate dependencies are declared in the task-owned `Cargo.lock` files
and prefetched into the image's Cargo registry cache at build time. They are
not vendored in this task directory, and no third-party crate source tree is
stored in the repository.

The Apache Iceberg `LICENSE` file embeds the project's third-party attribution
appendix; those attributions are reproduced in
`LICENSES/Apache-2.0-Apache-Iceberg.txt`.

## 6. Required notices

Apache-2.0 requires the upstream NOTICE to be carried with distributions. Both
NOTICE files below are reproduced verbatim from the pinned upstream trees.

iceberg-rust `0.2.0` and `v0.3.0` (`NOTICE`, 168 bytes):

```
Apache Iceberg Rust
Copyright 2023 The Apache Software Foundation

This product includes software developed at
The Apache Software Foundation (http://www.apache.org/).
```

iceberg-rust `v0.4.0`, `v0.5.1` and `v0.6.0` (`NOTICE`, 173 bytes):

```
Apache Iceberg Rust
Copyright 2023-2024 The Apache Software Foundation

This product includes software developed at
The Apache Software Foundation (http://www.apache.org/).
```

Apache Iceberg `apache-iceberg-1.9.1` (`NOTICE`, 932 bytes):

```
Apache Iceberg
Copyright 2017-2025 The Apache Software Foundation

This product includes software developed at
The Apache Software Foundation (http://www.apache.org/).

--------------------------------------------------------------------------------

This project includes code from Kite, developed at Cloudera, Inc. with
the following copyright notice:

| Copyright 2013 Cloudera Inc.
|
| Licensed under the Apache License, Version 2.0 (the "License");
| you may not use this file except in compliance with the License.
| You may obtain a copy of the License at
|
|   http://www.apache.org/licenses/LICENSE-2.0
|
| Unless required by applicable law or agreed to in writing, software
| distributed under the License is distributed on an "AS IS" BASIS,
| WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
| See the License for the specific language governing permissions and
| limitations under the License.
```

No trademark statement is made in either upstream NOTICE or LICENSE file.

Task-authored benchmark content is licensed separately by the benchmark maintainers.
