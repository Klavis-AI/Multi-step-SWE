# Third-Party Notices — onion-routing-client-multiversion

Benchmark task that implements an onion-routing (Tor) client in Rust across
twelve cumulative upstream releases, from cells and circuits through bridges
and pluggable transports.

## Upstream project

| | |
|---|---|
| Project | arti (Tor Project's Rust Tor implementation) |
| URL | https://gitlab.torproject.org/tpo/core/arti |
| Pins | `arti-v0.0.0`, `arti-v0.0.1`, `arti-v0.0.2`, `arti-v0.0.3`, `arti-v0.0.4`, `arti-v0.1.0`, `arti-v0.2.0`, `arti-v0.3.0`, `arti-v0.4.0`, `arti-v0.6.0`, `arti-v1.0.0`, `arti-v1.1.0` |
| Bundled/derived | The step-1 workspace is a source skeleton laid out like `arti-v0.0.0` (crate directories, workspace `Cargo.toml`, `Cargo.lock`), with the implementation left to the candidate. Each step's oracle patch (`steps/*/solution/`) is derived from the corresponding upstream release. The step documents (`SPEC_NOTES.md`, `TEST_INTERFACE.md`) are task-authored summaries of Tor protocol behaviour. |

## Version map

| Step | Crate root | Source milestone | Target milestone |
|---|---|---|---|
| 01-cells-circuits-directories | workspace root | `arti-v0.0.0` | `arti-v0.0.0` |
| 02-guards-persistence-isolation | `crates/` | `arti-v0.0.0` | `arti-v0.0.1` |
| 03-reactor-and-events | `crates/` | `arti-v0.0.1` | `arti-v0.0.2` |
| 04-prediction-and-reconfiguration | `crates/` | `arti-v0.0.2` | `arti-v0.0.3` |
| 05-bootstrap-status-and-tls-backends | `crates/` | `arti-v0.0.3` | `arti-v0.0.4` |
| 06-deferred-bootstrap-and-errors | `crates/` | `arti-v0.0.4` | `arti-v0.1.0` |
| 07-dns-isolation-and-dialing | `crates/` | `arti-v0.1.0` | `arti-v0.2.0` |
| 08-clock-skew-and-safe-logging | `crates/` | `arti-v0.2.0` | `arti-v0.3.0` |
| 09-fs-trust-and-dir-tolerance | `crates/` | `arti-v0.3.0` | `arti-v0.4.0` |
| 10-crypto-backends-and-errors | `crates/` | `arti-v0.4.0` | `arti-v0.6.0` |
| 11-channel-padding-and-stability | `crates/` | `arti-v0.6.0` | `arti-v1.0.0` |
| 12-bridges-and-pluggable-transports | `crates/` | `arti-v1.0.0` | `arti-v1.1.0` |

## License table

| SPDX ID | File under `LICENSES/` | Covers |
|---|---|---|
| MIT | `MIT.txt` | arti `arti-v0.0.2` through `arti-v1.0.0` (`MIT OR Apache-2.0`), including the workspace skeleton in `steps/01-cells-circuits-directories/workdir/`; `Copyright 2019-2021, The Tor Project, Inc.` |
| MIT | `MIT-2019-2022.txt` | arti `arti-v1.1.0` (`MIT OR Apache-2.0`); exact upstream `LICENSE-MIT` from tag `arti-v1.1.0`; `Copyright 2019-2022, The Tor Project, Inc.` |
| Apache-2.0 | `Apache-2.0.txt` | arti at every pinned release (`MIT OR Apache-2.0`) |

`MIT.txt` and `Apache-2.0.txt` are copied byte-for-byte from the task tree's
`steps/01-cells-circuits-directories/workdir/LICENSE-MIT` and `LICENSE-APACHE`;
`MIT-2019-2022.txt` is the exact upstream `LICENSE-MIT` at tag `arti-v1.1.0`.
The upstream project licenses its code under `MIT OR Apache-2.0`; the recipient
may use either. The MIT copyright line changed once across the pins: tags
`arti-v0.0.2` through `arti-v1.0.0` carry `Copyright 2019-2021`, and
`arti-v1.1.0` carries `Copyright 2019-2022` (the step 12 patch applies that
change). The `arti-v0.0.0` and `arti-v0.0.1` tags contain no root `LICENSE-MIT`
or `LICENSE-APACHE` file; their `README.md` and crate manifests declare
`MIT OR Apache-2.0`, so those two pins are covered by the same two texts. The
Apache-2.0 file is byte-identical at every tag checked (`arti-v0.0.2`,
`arti-v1.0.0`, `arti-v1.1.0`).

## Modifications made by the benchmark

- The workspace is a skeleton derived from the `arti-v0.0.0` crate layout, not
  a compiled upstream tree: crate manifests are present and implementation
  files are empty. The benchmark supplies its own `SPEC_NOTES.md`,
  `TEST_INTERFACE.md`, `WORKSPACE_HANDOFF.md` and tests.
- The oracle patches in `steps/*/solution/` are benchmark-written transitions
  derived from the upstream releases named in the version map; they are not
  upstream patch files.
- No upstream source file is edited in place; the candidate builds the
  implementation in the workspace.

## Third-party dependencies bundled in the task

No third-party crate source is bundled in this repository. The container image
prefetches the dependency universe from the releases' `Cargo.lock` graphs; the
workspace records the resolved graph in `Cargo.lock` and
`Cargo.lock.release`. Those crates remain under their own upstream licenses, as
declared by their crates.io metadata and license files in the prefetched
sources.

## Required notices

- MIT copyright: `Copyright 2019-2021, The Tor Project, Inc.` in
  `LICENSES/MIT.txt`, and `Copyright 2019-2022, The Tor Project, Inc.` in
  `LICENSES/MIT-2019-2022.txt` for `arti-v1.1.0`.
- Apache-2.0: the license file carries the standard appendix placeholder
  `Copyright [yyyy] [name of copyright owner]`; arti's LICENSE-APACHE does not
  fill it in.
- No `NOTICE` file is present in the upstream tree, so no additional Apache
  NOTICE text applies.

Task-authored benchmark content is licensed separately by the benchmark maintainers.
