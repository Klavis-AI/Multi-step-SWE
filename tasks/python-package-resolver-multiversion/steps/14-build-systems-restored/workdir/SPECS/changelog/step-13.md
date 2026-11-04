# uv release notes, step 13: releases in the span `0.10.0 -> 0.11.0` (upstream tag `0.11.0`)

Source: astral-sh/uv at tag `0.11.0`, file(s) `CHANGELOG.md`, `changelogs/0.1.x.md`, `changelogs/0.2.x.md`, `changelogs/0.3.x.md`, `changelogs/0.4.x.md`, `changelogs/0.5.x.md`, `changelogs/0.6.x.md`, `changelogs/0.7.x.md`, `changelogs/0.8.x.md`, `changelogs/0.9.x.md`. 13 release entries: 0.11.0, 0.10.12, 0.10.11, 0.10.10, 0.10.9, 0.10.8, 0.10.7, 0.10.6, 0.10.5, 0.10.4, 0.10.3, 0.10.2, 0.10.1. Licence: MIT OR Apache-2.0 (astral-sh/uv; see SPECS/licences/uv-LICENSE-MIT and SPECS/licences/uv-LICENSE-APACHE).

Kept sections: Breaking changes, Enhancements, Bug fixes, CLI, Configuration, Error messages, Python, Python versions, Preview, Stabilizations, Deprecations, Compatibility, Security, plus each release's preamble. Dropped: Documentation, Performance, Release, Rust API, Rust libraries, Other changes, Distributions. Kept text is verbatim.

## 0.11.0

Released on 2026-03-23.

### Breaking changes

This release includes changes to the networking stack used by uv. While we think that breakage will be rare, it is possible that these changes will result in the rejection of certificates previously trusted by uv so we have marked the change as breaking out of an abundance of caution.

The changes are largely driven by the upgrade of reqwest, which powers uv's HTTP clients, to [v0.13](https://seanmonstar.com/blog/reqwest-v013-rustls-default/) which included some breaking changes to TLS certificate verification.

The following changes are included:

- [`rustls-platform-verifier`](https://github.com/rustls/rustls-platform-verifier) is used instead of [`rustls-native-certs`](https://github.com/rustls/rustls-native-certs) and [`webpki`](https://github.com/rustls/webpki) for certificate verification

    **This change should have no effect unless you are using the `native-tls` option to enable reading system certificates.**

    `rustls-platform-verifier` delegates to the system for certificate validation (e.g., `Security.framework` on macOS) instead of eagerly loading certificates from the system and verifying them via `webpki`. The effects of this change will vary based on the operating system. In general, uv's certificate validation should now be more consistent with browsers and other native applications. However, this is the most likely cause of breaking changes in this release. Some previously failing certificate chains may succeed, and some previously accepted certificate chains may fail. In either case, we expect the validation to be more correct and welcome reports of regressions.

     In particular, because more responsibility for validating the certificate is transferred to your system's security library, some features like [CA constraints](https://support.apple.com/en-us/103255) or [revocation of certificates](https://en.wikipedia.org/wiki/Certificate_revocation) via OCSP and CRLs may now be used.

    This change should improve performance when using system certificate on macOS, as uv no longer needs to load all certificates from the keychain at startup.

- [`aws-lc`](https://github.com/aws/aws-lc) is used instead of `ring` for a cryptography backend

    There should not be breaking changes from this change. We expect this to expand support for certificate signature algorithms.

- `--native-tls` is deprecated in favor of a new `--system-certs` flag

    The `--native-tls` flag is still usable and has identical behavior to `--system-certs.`

    This change was made to reduce confusion about the TLS implementation uv uses. uv always uses `rustls` not `native-tls`.

- Building uv on x86-64 and i686 Windows requires NASM

    NASM is required by `aws-lc`. If not found on the system, a prebuilt blob provided by `aws-lc-sys` will be used.

    If you are not building uv from source, this change has no effect.

    See the [CONTRIBUTING](https://github.com/astral-sh/uv/blob/b6854d77bfd0cb78157fecaf8b30126c6f16bc11/CONTRIBUTING.md#setup) guide for details.

- Empty `SSL_CERT_FILE` values are ignored (for consistency with `SSL_CERT_DIR`)

See [#18550](https://github.com/astral-sh/uv/pull/18550) for details.

### Python

- Enable frame pointers for improved profiling on Linux x86-64 and aarch64

See the [python-build-standalone release notes](https://github.com/astral-sh/python-build-standalone/releases/20260320) for details.

### Enhancements

- Treat 'Dynamic' values as case-insensitive ([#18669](https://github.com/astral-sh/uv/pull/18669))
- Use a dedicated error for invalid cache control headers ([#18657](https://github.com/astral-sh/uv/pull/18657))
- Enable checksum verification in the generated installer script ([#18625](https://github.com/astral-sh/uv/pull/18625))

### Preview features

- Add `--service-format` and `--service-url` to `uv audit` ([#18571](https://github.com/astral-sh/uv/pull/18571))

### Bug fixes

- Find the dynamic linker on the file system when sniffing binaries fails ([#18457](https://github.com/astral-sh/uv/pull/18457))
- Fix export of conflicting workspace members with dependencies ([#18666](https://github.com/astral-sh/uv/pull/18666))
- Respect installed settings in `uv tool list --outdated` ([#18586](https://github.com/astral-sh/uv/pull/18586))
- Treat paths originating as PEP 508 URLs which contain expanded variables as relative ([#18680](https://github.com/astral-sh/uv/pull/18680))
- Fix `uv export` for workspace member packages with conflicts ([#18635](https://github.com/astral-sh/uv/pull/18635))
- Continue to alternative authentication providers when the pyx store has no token ([#18425](https://github.com/astral-sh/uv/pull/18425))
- Use redacted URLs for log messages in cached client ([#18599](https://github.com/astral-sh/uv/pull/18599))

## 0.10.12

Released on 2026-03-19.

### Python

- Add pypy 3.11.15 ([#18468](https://github.com/astral-sh/uv/pull/18468))
- Add support for using Python 3.6 interpreters ([#18454](https://github.com/astral-sh/uv/pull/18454))

### Enhancements

- Include uv's target triple in version report ([#18520](https://github.com/astral-sh/uv/pull/18520))
- Allow comma separated values in `--no-emit-package` ([#18565](https://github.com/astral-sh/uv/pull/18565))

### Preview features

- Show `uv audit` in the CLI help ([#18540](https://github.com/astral-sh/uv/pull/18540))

### Bug fixes

- Improve reporting of managed interpreter symlinks in `uv python list` ([#18459](https://github.com/astral-sh/uv/pull/18459))
- Preserve end-of-line comments on previous entries when removing dependencies ([#18557](https://github.com/astral-sh/uv/pull/18557))
- Treat abi3 wheel Python version as a lower bound ([#18536](https://github.com/astral-sh/uv/pull/18536))
- Detect hard-float support on aarch64 kernels running armv7 userspace ([#18530](https://github.com/astral-sh/uv/pull/18530))

## 0.10.11

Released on 2026-03-16.

### Enhancements

- Fetch Ruff release metadata from an Astral mirror ([#18358](https://github.com/astral-sh/uv/pull/18358))
- Use PEP 639 license metadata for uv itself ([#16477](https://github.com/astral-sh/uv/pull/16477))

### Bug fixes

- Allow `--project` to refer to a `pyproject.toml` directly and reduce to a warning on other files ([#18513](https://github.com/astral-sh/uv/pull/18513))
- Disable `SYSTEM_VERSION_COMPAT` when querying interpreters on macOS ([#18452](https://github.com/astral-sh/uv/pull/18452))
- Enforce available distributions for supported environments ([#18451](https://github.com/astral-sh/uv/pull/18451))
- Fix `uv sync --active` recreating active environments when `UV_PYTHON_INSTALL_DIR` is relative ([#18398](https://github.com/astral-sh/uv/pull/18398))

## 0.10.10

Released on 2026-03-13.

### Python

- Add CPython 3.15.0a7 ([#18403](https://github.com/astral-sh/uv/pull/18403))

### Enhancements

- Add `--outdated` flag to `uv tool list` ([#18318](https://github.com/astral-sh/uv/pull/18318))
- Add riscv64 musl target to build-release-binaries workflow ([#18228](https://github.com/astral-sh/uv/pull/18228))
- Fetch Ruff from an Astral mirror ([#18286](https://github.com/astral-sh/uv/pull/18286))
- Improve error handling for platform detection in Python downloads ([#18453](https://github.com/astral-sh/uv/pull/18453))
- Warn if `--project` directory does not exist ([#17714](https://github.com/astral-sh/uv/pull/17714))
- Warn when workspace member scripts are skipped due to missing build system ([#18389](https://github.com/astral-sh/uv/pull/18389))
- Update build backend versions used in `uv init` ([#18417](https://github.com/astral-sh/uv/pull/18417))
- Log explicit config file path in verbose output ([#18353](https://github.com/astral-sh/uv/pull/18353))
- Make `uv cache clear` an alias of `uv cache clean` ([#18420](https://github.com/astral-sh/uv/pull/18420))
- Reject invalid classifiers, warn on license classifiers in `uv_build` ([#18419](https://github.com/astral-sh/uv/pull/18419))

### Preview features

- Add links to `uv audit` output ([#18392](https://github.com/astral-sh/uv/pull/18392))
- Output/report formatting for `uv audit` ([#18193](https://github.com/astral-sh/uv/pull/18193))
- Switch to batched OSV queries for `uv audit` ([#18394](https://github.com/astral-sh/uv/pull/18394))

### Bug fixes

- Avoid sharing version metadata across indexes ([#18373](https://github.com/astral-sh/uv/pull/18373))
- Bump zlib-rs to 0.6.2 to fix panic on decompression of large wheels on Windows ([#18362](https://github.com/astral-sh/uv/pull/18362))
- Filter out unsupported environment wheels ([#18445](https://github.com/astral-sh/uv/pull/18445))
- Preserve absolute/relative paths in lockfiles ([#18176](https://github.com/astral-sh/uv/pull/18176))
- Recreate Python environments under `uv tool install --force` ([#18399](https://github.com/astral-sh/uv/pull/18399))
- Respect timestamp and other cache keys in cached environments ([#18396](https://github.com/astral-sh/uv/pull/18396))
- Simplify selected extra markers in `uv export` ([#18433](https://github.com/astral-sh/uv/pull/18433))
- Send pyx mint-token requests with a proper `Content-Type` ([#18334](https://github.com/astral-sh/uv/pull/18334))
- Fix Windows operating system and version reporting ([#18383](https://github.com/astral-sh/uv/pull/18383))

## 0.10.9

Released on 2026-03-06.

### Enhancements

- Add `fbgemm-gpu`, `fbgemm-gpu-genai`, `torchrec`, and `torchtune` to the PyTorch list ([#18338](https://github.com/astral-sh/uv/pull/18338))
- Add torchcodec to PyTorch List ([#18336](https://github.com/astral-sh/uv/pull/18336))
- Log the duration we took before erroring ([#18231](https://github.com/astral-sh/uv/pull/18231))
- Warn when using `uv_build` settings without `uv_build` ([#15750](https://github.com/astral-sh/uv/pull/15750))
- Add fallback to `/usr/lib/os-release` on Linux system lookup failure ([#18349](https://github.com/astral-sh/uv/pull/18349))
- Use `cargo auditable` to include SBOM in uv builds ([#18276](https://github.com/astral-sh/uv/pull/18276))

### Configuration

- Add an environment variable for `UV_VENV_RELOCATABLE` ([#18331](https://github.com/astral-sh/uv/pull/18331))

### Bug fixes

- Continue on trampoline job assignment failures ([#18291](https://github.com/astral-sh/uv/pull/18291))
- Handle the hard link limit gracefully instead of failing ([#17699](https://github.com/astral-sh/uv/pull/17699))
- Respect build constraints for workspace members ([#18350](https://github.com/astral-sh/uv/pull/18350))
- Revalidate editables and other dependencies in scripts ([#18328](https://github.com/astral-sh/uv/pull/18328))
- Support Python 3.13+ on Android ([#18301](https://github.com/astral-sh/uv/pull/18301))
- Support `cp3-none-any` ([#17064](https://github.com/astral-sh/uv/pull/17064))
- Skip tool environments with broken links to Python on Windows ([#17176](https://github.com/astral-sh/uv/pull/17176))

## 0.10.8

Released on 2026-03-03.

### Python

- Add CPython 3.10.20
- Add CPython 3.11.15
- Add CPython 3.12.13

### Enhancements

- Add Docker images based on Docker Hardened Images ([#18247](https://github.com/astral-sh/uv/pull/18247))
- Add resolver hint when `--exclude-newer` filters out all versions of a package ([#18217](https://github.com/astral-sh/uv/pull/18217))
- Configure a real retry minimum delay of 1s ([#18201](https://github.com/astral-sh/uv/pull/18201))
- Expand `uv_build` direct build compatibility ([#17902](https://github.com/astral-sh/uv/pull/17902))
- Fetch CPython from an Astral mirror by default ([#18207](https://github.com/astral-sh/uv/pull/18207))
- Download uv releases from an Astral mirror in installers by default ([#18191](https://github.com/astral-sh/uv/pull/18191))
- Add SBOM attestations to Docker images ([#18252](https://github.com/astral-sh/uv/pull/18252))
- Improve hint for installing meson-python when missing as build backend ([#15826](https://github.com/astral-sh/uv/pull/15826))

### Configuration

- Add `UV_INIT_BARE` environment variable for `uv init` ([#18210](https://github.com/astral-sh/uv/pull/18210))

### Bug fixes

- Prevent `uv tool upgrade` from installing excluded dependencies ([#18022](https://github.com/astral-sh/uv/pull/18022))
- Promote authentication policy when saving tool receipts ([#18246](https://github.com/astral-sh/uv/pull/18246))
- Respect exclusions in scripts ([#18269](https://github.com/astral-sh/uv/pull/18269))
- Retain default-branch Git SHAs in `pylock.toml` files ([#18227](https://github.com/astral-sh/uv/pull/18227))
- Skip installed Python check for URL dependencies ([#18211](https://github.com/astral-sh/uv/pull/18211))
- Respect constraints during `--upgrade` ([#18226](https://github.com/astral-sh/uv/pull/18226))
- Fix `uv tree` orphaned roots and premature deduplication ([#17212](https://github.com/astral-sh/uv/pull/17212))

## 0.10.7

Released on 2026-02-27.

### Bug fixes

- Fix handling of junctions in Windows Containers on Windows ([#18192](https://github.com/astral-sh/uv/pull/18192))

### Enhancements

- Activate logging for middleware retries ([#18200](https://github.com/astral-sh/uv/pull/18200))
- Upload uv releases to a mirror ([#18159](https://github.com/astral-sh/uv/pull/18159))

## 0.10.6

Released on 2026-02-24.

### Bug fixes

- Apply lockfile marker normalization for fork markers ([#18116](https://github.com/astral-sh/uv/pull/18116))
- Fix Python version selection for scripts with a `requires-python` conflicting with `.python-version` ([#18097](https://github.com/astral-sh/uv/pull/18097))
- Preserve file permissions when using reflinks on Linux ([#18187](https://github.com/astral-sh/uv/pull/18187))

## 0.10.5

Released on 2026-02-23.

### Enhancements

- Add hint when named index is found in a parent config file ([#18087](https://github.com/astral-sh/uv/pull/18087))
- Add warning for `uv lock --frozen` ([#17859](https://github.com/astral-sh/uv/pull/17859))
- Attempt to use reflinks by default on Linux ([#18117](https://github.com/astral-sh/uv/pull/18117))
- Fallback to hardlinks after reflink failure before copying ([#18104](https://github.com/astral-sh/uv/pull/18104))
- Filter `pylock.toml` wheels by tags and `requires-python` ([#18081](https://github.com/astral-sh/uv/pull/18081))
- Validate wheel filenames are normalized during `uv publish` ([#17783](https://github.com/astral-sh/uv/pull/17783))
- Fix message when `exclude-newer` invalidates the lock file ([#18100](https://github.com/astral-sh/uv/pull/18100))
- Change the missing files log level to debug ([#18075](https://github.com/astral-sh/uv/pull/18075))

### Bug fixes

- Fix `--no-emit-workspace` with `--all-packages` on single-member workspaces ([#18098](https://github.com/astral-sh/uv/pull/18098))
- Fix `UV_NO_DEFAULT_GROUPS` rejecting truthy values like `1` ([#18057](https://github.com/astral-sh/uv/pull/18057))
- Fix iOS detection ([#17973](https://github.com/astral-sh/uv/pull/17973))
- Propagate project-level conflicts to package extras ([#18096](https://github.com/astral-sh/uv/pull/18096))
- Use a global build concurrency semaphore ([#18054](https://github.com/astral-sh/uv/pull/18054))

## 0.10.4

Released on 2026-02-17.

### Enhancements

- Remove duplicate references to the affected paths when showing `uv python` errors ([#18008](https://github.com/astral-sh/uv/pull/18008))
- Skip discovery of workspace members that contain only git-ignored files, including in sub-directories ([#18051](https://github.com/astral-sh/uv/pull/18051))

### Bug fixes

- Don't panic when initialising a package at the filesystem root (e.g. `uv init / --name foo`) ([#17983](https://github.com/astral-sh/uv/pull/17983))
- Fix permissions on `wheel` and `sdist` files produced by the `uv_build` build backend ([#18020](https://github.com/astral-sh/uv/pull/18020))
- Revert locked file change to fix locked files on NFS mounts ([#18071](https://github.com/astral-sh/uv/pull/18071))

## 0.10.3

Released on 2026-02-16.

### Python

- Add CPython 3.15.0a6

### Enhancements

- Don't open file locks for writing ([#17956](https://github.com/astral-sh/uv/pull/17956))
- Make Windows trampoline error messages consistent with uv proper ([#17969](https://github.com/astral-sh/uv/pull/17969))
- Log which preview features are enabled ([#17968](https://github.com/astral-sh/uv/pull/17968))

### Preview features

- Add support for ruff version constraints and `exclude-newer` in `uv format` ([#17651](https://github.com/astral-sh/uv/pull/17651))
- Fix script path handling when `target-workspace-discovery` is enabled ([#17965](https://github.com/astral-sh/uv/pull/17965))
- Use version constraints to select the default ruff version used by `uv format` ([#17977](https://github.com/astral-sh/uv/pull/17977))

### Bug fixes

- Avoid matching managed Python versions by prefixes, e.g. don't match CPython 3.10 when `cpython-3.1` is specified ([#17972](https://github.com/astral-sh/uv/pull/17972))
- Fix handling of `--allow-existing` with minor version links on Windows ([#17978](https://github.com/astral-sh/uv/pull/17978))
- Fix panic when encountering unmanaged workspace members ([#17974](https://github.com/astral-sh/uv/pull/17974))
- Improve accuracy of request timing ([#18007](https://github.com/astral-sh/uv/pull/18007))
- Reject `u64::MAX` in version segments to prevent overflow ([#17985](https://github.com/astral-sh/uv/pull/17985))

## 0.10.2

Released on 2026-02-10.

### Enhancements

- Deprecate unexpected ZIP compression methods ([#17946](https://github.com/astral-sh/uv/pull/17946))

### Bug fixes

- Fix `cargo-install` failing due to missing `uv-test` dependency ([#17954](https://github.com/astral-sh/uv/pull/17954))

## 0.10.1

Released on 2026-02-10.

### Enhancements

- Don't panic on metadata read errors ([#17904](https://github.com/astral-sh/uv/pull/17904))
- Skip empty workspace members instead of failing ([#17901](https://github.com/astral-sh/uv/pull/17901))
- Don't fail creating a read-only `sdist-vX/.git` if it already exists ([#17825](https://github.com/astral-sh/uv/pull/17825))
