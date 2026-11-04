# uv release notes, step 12: releases in the span `0.9.0 -> 0.10.0` (upstream tag `0.10.0`)

Source: astral-sh/uv at tag `0.10.0`, file(s) `CHANGELOG.md`, `changelogs/0.1.x.md`, `changelogs/0.2.x.md`, `changelogs/0.3.x.md`, `changelogs/0.4.x.md`, `changelogs/0.5.x.md`, `changelogs/0.6.x.md`, `changelogs/0.7.x.md`, `changelogs/0.8.x.md`, `changelogs/0.9.x.md`. 31 release entries: 0.10.0, 0.9.30, 0.9.29, 0.9.28, 0.9.27, 0.9.26, 0.9.25, 0.9.24, 0.9.23, 0.9.22, 0.9.21, 0.9.20, 0.9.19, 0.9.18, 0.9.17, 0.9.16, 0.9.15, 0.9.14, 0.9.13, 0.9.12, 0.9.11, 0.9.10, 0.9.9, 0.9.8, 0.9.7, 0.9.6, 0.9.5, 0.9.4, 0.9.3, 0.9.2, 0.9.1. Licence: MIT OR Apache-2.0 (astral-sh/uv; see SPECS/licences/uv-LICENSE-MIT and SPECS/licences/uv-LICENSE-APACHE).

Kept sections: Breaking changes, Enhancements, Bug fixes, CLI, Configuration, Error messages, Python, Python versions, Preview, Stabilizations, Deprecations, Compatibility, Security, plus each release's preamble. Dropped: Documentation, Performance, Release, Rust API, Rust libraries, Other changes, Distributions. Kept text is verbatim.

## 0.10.0

Since we released uv [0.9.0](https://github.com/astral-sh/uv/releases/tag/0.9.0) in October of 2025, we've accumulated various changes that improve correctness and user experience, but could break some workflows. This release contains those changes; many have been marked as breaking out of an abundance of caution. We expect most users to be able to upgrade without making changes.

This release also includes the stabilization of preview features. Python upgrades are now stable, including the `uv python upgrade` command, `uv python install --upgrade`, and automatically upgrading Python patch versions in virtual environments when a new version is installed. The `add-bounds` and `extra-build-dependencies` settings are now stable. Finally, the `uv workspace dir` and `uv workspace list` utilities for writing scripts against workspace members are now stable.

### Breaking changes

- **Require `--clear` to remove existing virtual environments in `uv venv`** ([#17757](https://github.com/astral-sh/uv/pull/17757))

  Previously, `uv venv` would prompt for confirmation before removing an existing virtual environment in interactive contexts, and remove it without confirmation in non-interactive contexts. Now, `uv venv` requires the `--clear` flag to remove an existing virtual environment. A warning for this change was added in [uv 0.8](https://github.com/astral-sh/uv/blob/main/changelogs/0.8.x.md#breaking-changes).

  You can opt out of this behavior by passing the `--clear` flag or setting `UV_VENV_CLEAR=1`.

- **Error if multiple indexes include `default = true`** ([#17011](https://github.com/astral-sh/uv/pull/17011))

  Previously, uv would silently accept multiple indexes with `default = true` and use the first one. Now, uv will error if multiple indexes are marked as the default.

  You cannot opt out of this behavior. Remove `default = true` from all but one index.

- **Error when an `explicit` index is unnamed** ([#17777](https://github.com/astral-sh/uv/pull/17777))

  Explicit indexes can only be used via the `[tool.uv.sources]` table, which requires referencing the index by name. Previously, uv would silently accept unnamed explicit indexes, which could never be referenced. Now, uv will error if an explicit index does not have a name.

  You cannot opt out of this behavior. Add a `name` to the explicit index or remove the entry.

- **Install alternative Python executables using their implementation name** ([#17756](https://github.com/astral-sh/uv/pull/17756), [#17760](https://github.com/astral-sh/uv/pull/17760))

  Previously, `uv python install` would install PyPy, GraalPy, and Pyodide executables with names like `python3.10` into the bin directory. Now, these executables will be named using their implementation name, e.g., `pypy3.10`, `graalpy3.10`, and `pyodide3.12`, to avoid conflicting with CPython installations.

  You cannot opt out of this behavior.

- **Respect global Python version pins in `uv tool run` and `uv tool install`** ([#14112](https://github.com/astral-sh/uv/pull/14112))

  Previously, `uv tool run` and `uv tool install` did not respect the global Python version pin (set via `uv python pin --global`). Now, these commands will use the global Python version when no explicit version is requested.

  For `uv tool install`, if the tool is already installed, the Python version will not change unless `--reinstall` or `--python` is provided. If the tool was previously installed with an explicit `--python` flag, the global pin will not override it.

  You can opt out of this behavior by providing an explicit `--python` flag.

- **Remove Debian Bookworm, Alpine 3.21, and Python 3.8 Docker images** ([#17755](https://github.com/astral-sh/uv/pull/17755))

  The Debian Bookworm and Alpine 3.21 images were replaced by Debian Trixie and Alpine 3.22 as defaults in [uv 0.9](https://github.com/astral-sh/uv/pull/15352). These older images are now removed. Python 3.8 images are also removed, as Python 3.8 is no longer supported in the Trixie or Alpine base images.

  The following image tags are no longer published:
  - `uv:bookworm`, `uv:bookworm-slim`
  - `uv:alpine3.21`
  - `uv:python3.8-*`

  Use `uv:debian` or `uv:trixie` instead of `uv:bookworm`, `uv:alpine` or `uv:alpine3.22` instead of `uv:alpine3.21`, and a newer Python version instead of `uv:python3.8-*`.

- **Drop PPC64 (big endian) builds** ([#17626](https://github.com/astral-sh/uv/pull/17626))

  uv no longer provides pre-built binaries for PPC64 (big endian). This platform appears to be largely unused and is only supported on a single manylinux version. PPC64LE (little endian) builds are unaffected.

  Building uv from source is still supported for this platform.

- **Skip generating `activate.csh` for relocatable virtual environments** ([#17759](https://github.com/astral-sh/uv/pull/17759))

  Previously, `uv venv --relocatable` would generate an `activate.csh` script that contained hardcoded paths, making it incompatible with relocation. Now, the `activate.csh` script is not generated for relocatable virtual environments.

  You cannot opt out of this behavior.

- **Require username when multiple credentials match a URL** ([#16983](https://github.com/astral-sh/uv/pull/16983))

  When using `uv auth login` to store credentials, you can register multiple username and password combinations for the same host. Previously, when uv needed to authenticate and multiple credentials matched the URL (e.g., when retrieving a token with `uv auth token`), uv would pick the first match. Now, uv will error instead.

  You cannot opt out of this behavior. Include the username in the request, e.g., `uv auth token --username foo example.com`.

- **Avoid invalidating the lockfile versions after an `exclude-newer` change** ([#17721](https://github.com/astral-sh/uv/pull/17721))

  Previously, changing the `exclude-newer` setting would cause package versions to be upgraded, ignoring the lockfile entirely. Now, uv will only change package versions if they are no longer within the `exclude-newer` range.

  You can restore the previous behavior by using `--upgrade` or `--upgrade-package` to opt-in to package version changes.

- **Upgrade `uv format` to Ruff 0.15.0** ([#17838](https://github.com/astral-sh/uv/pull/17838))

  `uv format` now uses [Ruff 0.15.0](https://github.com/astral-sh/ruff/releases/tag/0.15.0), which uses the [2026 style guide](https://astral.sh/blog/ruff-v0.15.0#the-ruff-2026-style-guide). See the blog post for details.

  The formatting of code is likely to change. You can opt out of this behavior by requesting an older Ruff version, e.g., `uv format --version 0.14.14`.

- **Update uv crate test features to use `test-` as a prefix** ([#17860](https://github.com/astral-sh/uv/pull/17860))

  This change only affects redistributors of uv. The Cargo features used to gate test dependencies, e.g., `pypi`, have been renamed with a `test-` prefix for clarity, e.g., `test-pypi`.

### Stabilizations

- **`uv python upgrade` and `uv python install --upgrade`** ([#17766](https://github.com/astral-sh/uv/pull/17766))

  When installing Python versions, an [intermediary directory](https://docs.astral.sh/uv/concepts/python-versions/#minor-version-directories) without the patch version attached will be created, and virtual environments will be transparently upgraded to new patch versions.

  See the [Python version documentation](https://docs.astral.sh/uv/concepts/python-versions/#upgrading-python-versions) for more details.

- **`uv add --bounds` and the `add-bounds` configuration option** ([#17660](https://github.com/astral-sh/uv/pull/17660))

  This does not come with any behavior changes. You will no longer see an experimental warning when using `uv add --bounds` or `add-bounds` in configuration.

- **`uv workspace list` and `uv workspace dir`** ([#17768](https://github.com/astral-sh/uv/pull/17768))

  This does not come with any behavior changes. You will no longer see an experimental warning when using these commands.

- **`extra-build-dependencies`** ([#17767](https://github.com/astral-sh/uv/pull/17767))

  This does not come with any behavior changes. You will no longer see an experimental warning when using `extra-build-dependencies` in configuration.

### Enhancements

- Improve ABI tag error message phrasing ([#17878](https://github.com/astral-sh/uv/pull/17878))
- Introduce a 10s connect timeout ([#17733](https://github.com/astral-sh/uv/pull/17733))
- Allow using `pyx.dev` as a target in `uv auth` commands despite `PYX_API_URL` differing ([#17856](https://github.com/astral-sh/uv/pull/17856))

### Bug fixes

- Support all CPython ABI tag suffixes properly  ([#17817](https://github.com/astral-sh/uv/pull/17817))
- Add support for detecting PowerShell on Linux and macOS ([#17870](https://github.com/astral-sh/uv/pull/17870))
- Retry timeout errors for streams ([#17875](https://github.com/astral-sh/uv/pull/17875))

## 0.9.30

Released on 2026-02-04.

### Python

- Add CPython 3.14.3 and 3.13.12 ([#17849](https://github.com/astral-sh/uv/pull/17849))

### Enhancements

- Allow comma-separated values for `--extra` option
  ([#17525](https://github.com/astral-sh/uv/pull/17525))
- Check all files during a dry-run publish instead of stopping at the first failure
  ([#17785](https://github.com/astral-sh/uv/pull/17785))
- Clarify `UV_HTTP_TIMEOUT` error message ([#17493](https://github.com/astral-sh/uv/pull/17493))

### Preview features

- Use relocatable virtual environments by default
  ([#17770](https://github.com/astral-sh/uv/pull/17770))

### Bug fixes

- Fix deadlock on token refresh in `uv publish` when using pyx
  ([#17832](https://github.com/astral-sh/uv/pull/17832))
- Ignore global Python pins when incompatible with project
  ([#15473](https://github.com/astral-sh/uv/pull/15473))

## 0.9.29

Released on 2026-02-03.

### Python

- Update to Pyodide 0.29.3 ([#17730](https://github.com/astral-sh/uv/pull/17730))

### Enhancements

- Add wheel-tag-style aliases for manylinux platform names
  ([#17750](https://github.com/astral-sh/uv/pull/17750))
- Hint on `uv version --bump dev` similar to pre-release bumps
  ([#17796](https://github.com/astral-sh/uv/pull/17796))
- Improve display of RFC 9457 Problem Detail responses in `uv publish` server errors
  ([#17787](https://github.com/astral-sh/uv/pull/17787))
- Improve the wording of publish errors during dry-run
  ([#17782](https://github.com/astral-sh/uv/pull/17782))
- Set backoff to 10 retries ([#17816](https://github.com/astral-sh/uv/pull/17816))
- Add properties to synthentic and project roots in Cyclone DX exports
  ([#17820](https://github.com/astral-sh/uv/pull/17820))
- Identify the invidividual clients in `uv publish` trace logs
  ([#17784](https://github.com/astral-sh/uv/pull/17784))

### Preview features

- Remove special casing for `base` and `default` conda environment names
  ([#17758](https://github.com/astral-sh/uv/pull/17758))

### Bug fixes

- Fix `PYTHONHOME` inheritance when spawning different Python versions
  ([#17821](https://github.com/astral-sh/uv/pull/17821))
- Fix wheel rejections on freethreading+debug builds
  ([#17812](https://github.com/astral-sh/uv/pull/17812))
- Pad with zeros during comparisons in `EqualStar` and `NotEqualStar` operators
  ([#17751](https://github.com/astral-sh/uv/pull/17751))
- Reject unknown field names in conflict declarations
  ([#17727](https://github.com/astral-sh/uv/pull/17727))
- Fix panics in `system-configuration` in sandboxes
  ([#17829](https://github.com/astral-sh/uv/pull/17829))

### Security

- Hide a subset of environment variable values in `--help`
  ([#17745](https://github.com/astral-sh/uv/pull/17745))

## 0.9.28

Released on 2026-01-29.

### Python

- Update CPython to use
  [OpenSSL 3.5.5](https://github.com/openssl/openssl/releases/tag/openssl-3.5.5) which includes
  fixes for high severity CVEs
  ([python-build-standalone#960](https://github.com/astral-sh/python-build-standalone/pull/960))

### Enhancements

- Add support for Pyodide interpreter on Windows
  ([#17658](https://github.com/astral-sh/uv/pull/17658))
- Warn if multiple indexes include `default = true`
  ([#17713](https://github.com/astral-sh/uv/pull/17713))
- Skip uploads when validation reports 'Already uploaded'
  ([#17412](https://github.com/astral-sh/uv/pull/17412))

### Configuration

- Add a reflink alias for the "clone" link mode
  ([#17724](https://github.com/astral-sh/uv/pull/17724))

### Bug fixes

- Ensure `uv.exe` exits when `uvw.exe` or `uvx.exe` is killed
  ([#17500](https://github.com/astral-sh/uv/pull/17500))

## 0.9.27

Released on 2026-01-26.

### Python

- Upgrade Pyodide to 0.29.2 ([#17652](https://github.com/astral-sh/uv/pull/17652))
- Upgrade to GraalPy 25.0.2 ([#17634](https://github.com/astral-sh/uv/pull/17634))

### Enhancements

- Add `-t` shortform for `--target` to `uv pip` subcommands
  ([#17501](https://github.com/astral-sh/uv/pull/17501))
- Add support for ROCm 7.0 and 7.1 accelerator backends
  ([#17681](https://github.com/astral-sh/uv/pull/17681))
- Further improve free-threading ABI incompatibility errors
  ([#17491](https://github.com/astral-sh/uv/pull/17491))
- Implement `uv pip freeze --exclude` flag ([#17045](https://github.com/astral-sh/uv/pull/17045))
- Improve warnings for `--system` and `--no-system` in `uv venv`
  ([#17647](https://github.com/astral-sh/uv/pull/17647))
- Make `uv pip compile` attempt to download a specified `--python-version` if it can.
  ([#17249](https://github.com/astral-sh/uv/pull/17249))
- Support Trusted Publishing with pyx ([#17438](https://github.com/astral-sh/uv/pull/17438))
- Fix JSON schema for `exclude-newer-package` ([#17665](https://github.com/astral-sh/uv/pull/17665))

### Preview features

- Better detection for conflicting packages ([#17623](https://github.com/astral-sh/uv/pull/17623))
- Upgrade based on outdated build versions in `uv python upgrade`
  ([#17653](https://github.com/astral-sh/uv/pull/17653))

### Bug fixes

- Change chocolatey system test to ensure uv uses the right python
  ([#17533](https://github.com/astral-sh/uv/pull/17533))
- Fix infinite loop when `SSL_CERT_FILE` is a directory
  ([#17503](https://github.com/astral-sh/uv/pull/17503))

## 0.9.26

Released on 2026-01-15.

### Python

- Add CPython 3.15.0a5

### Enhancements

- Add a hint to update uv when a managed Python download is not found
  ([#17461](https://github.com/astral-sh/uv/pull/17461))
- Improve cache initialization failure error message
  ([#17469](https://github.com/astral-sh/uv/pull/17469))
- Improve error message for abi3 wheels on free-threaded Python
  ([#17442](https://github.com/astral-sh/uv/pull/17442))
- Add support for `--no-sources-package` ([#14910](https://github.com/astral-sh/uv/pull/14910))

### Preview features

- Add `METADATA.json` and `WHEEL.json` in uv build backend
  ([#15510](https://github.com/astral-sh/uv/pull/15510))
- Add support for GCS request signing ([#17474](https://github.com/astral-sh/uv/pull/17474))
- Adjust the process ulimit to the maximum allowed on startup
  ([#17464](https://github.com/astral-sh/uv/pull/17464))

### Bug fixes

- Lock to avoid concurrent refresh of pyx tokens
  ([#17479](https://github.com/astral-sh/uv/pull/17479))

## 0.9.25

Released on 2026-01-13.

### Python

- Add CPython 3.15.0a4
- Upgrade Tcl/Tk used by CPython to 9.0

### Enhancements

- Add `--compile-bytecode` to `uv python install` and `uv python upgrade` to compile the standard
  library ([#17088](https://github.com/astral-sh/uv/pull/17088))
- Allow disabling `exclude-newer` per package ([#16854](https://github.com/astral-sh/uv/pull/16854))
- Broadcast `WM_SETTINGCHANGE` on `uv tool update-shell`
  ([#17404](https://github.com/astral-sh/uv/pull/17404))

### Preview features

- Detect workspace from `uv run` target ([#17423](https://github.com/astral-sh/uv/pull/17423))

### Bug fixes

- Avoid unwrapping size for file responses ([#17434](https://github.com/astral-sh/uv/pull/17434))
- Use keyring authentication when retrieving `tool@latest` version
  ([#17448](https://github.com/astral-sh/uv/pull/17448))
- Use latest Pyodide version for each python version
  ([#17372](https://github.com/astral-sh/uv/pull/17372))
- Improve trampoline file handle closing ([#17374](https://github.com/astral-sh/uv/pull/17374))
- Fix error message when installing musl python on armv7
  ([#17213](https://github.com/astral-sh/uv/pull/17213))

## 0.9.24

Released on 2026-01-09.

### Bug fixes

- Fix handling of `UV_NO_SYNC=1 uv run ...` ([#17391](https://github.com/astral-sh/uv/pull/17391))
- Rebuild dynamic distribution when version changes with `--no-cache`
  ([#17387](https://github.com/astral-sh/uv/pull/17387))

## 0.9.23

Released on 2026-01-09.

### Enhancements

- Only write portable paths in `RECORD` files ([#17339](https://github.com/astral-sh/uv/pull/17339))
- Support relative paths in `UV_PYTHON_BIN_DIR` and `UV_TOOL_BIN_DIR`
  ([#17367](https://github.com/astral-sh/uv/pull/17367))

### Preview features

- Enable uploads to S3 via pre-signed URLs ([#17349](https://github.com/astral-sh/uv/pull/17349))

### Configuration

- Allow setting proxy variables via global / user configuration
  ([#16918](https://github.com/astral-sh/uv/pull/16918))
- Manually parse and reconcile Boolean environment variables
  ([#17321](https://github.com/astral-sh/uv/pull/17321))

### Bug fixes

- Avoid broken build artifacts on build failure
  ([#17276](https://github.com/astral-sh/uv/pull/17276))
- Fix missing dependencies on synthetic root in SBOM export
  ([#17363](https://github.com/astral-sh/uv/pull/17363))
- Recognize `armv8l` as an alias for `armv7l` in platform tag parsing
  ([#17384](https://github.com/astral-sh/uv/pull/17384))
- Fix redaction of a URL in a middleware trace log
  ([#17346](https://github.com/astral-sh/uv/pull/17346))

## 0.9.22

Released on 2026-01-06.

### Enhancements

- Use a dedicated error message when lockfile can't be found
  ([#17318](https://github.com/astral-sh/uv/pull/17318))

### Bug fixes

- Filter unusable wheels from the lockfile for more architectures
  ([#17317](https://github.com/astral-sh/uv/pull/17317))
- Correctly count retries originating from early middleware errors
  ([#17274](https://github.com/astral-sh/uv/pull/17274))

## 0.9.21

Released on 2025-12-30.

### Bug fixes

- Fix regression where zstd distribution hashes were not considered valid
  ([#17265](https://github.com/astral-sh/uv/pull/17265))

## 0.9.20

Released on 2025-12-29.

This is a re-release of 0.9.19, with internal crate versions incremented to enable publishing to
crates.io.

## 0.9.19

Released on 2025-12-29.

### Python

- Add CPython 3.15.0a3 ([#17165](https://github.com/astral-sh/uv/pull/17165))

### Enhancements

- Allow `uv pip compile` to install missing python interpreters in cases where it would otherwise
  fail ([#17216](https://github.com/astral-sh/uv/pull/17216))
- Avoid creating file contents with `uv init --bare --script`
  ([#17162](https://github.com/astral-sh/uv/pull/17162))
- Respect `--torch-backend` in `uv tool` commands
  ([#17117](https://github.com/astral-sh/uv/pull/17117))
- Support comma-separated values in `--no-binary` and `--only-binary`
  ([#17185](https://github.com/astral-sh/uv/pull/17185))

### Preview features

- Summarize package changes in `uv sync` with JSON output format
  ([#16981](https://github.com/astral-sh/uv/pull/16981))

### Bug fixes

- Avoid enforcing incorrect hash in mixed-hash settings
  ([#17157](https://github.com/astral-sh/uv/pull/17157))
- Fix retry counts in cached client ([#17104](https://github.com/astral-sh/uv/pull/17104))
- Respect `UV_PYTHON_DOWNLOAD_MIRROR` in `uv python list`
  ([#16673](https://github.com/astral-sh/uv/pull/16673))
- Support remote `pylock.toml` files ([#17119](https://github.com/astral-sh/uv/pull/17119))
- Avoid flagging proxied Git URLs as ambiguous authority
  ([#17234](https://github.com/astral-sh/uv/pull/17234))
- Fix dropped support of `-` in pip constraints, overrides, and excludes
  ([#17188](https://github.com/astral-sh/uv/pull/17188))

## 0.9.18

Released on 2025-12-16.

### Enhancements

- Add value hints to command line arguments to improve shell completion accuracy
  ([#17080](https://github.com/astral-sh/uv/pull/17080))
- Improve error handling in `uv publish` ([#17096](https://github.com/astral-sh/uv/pull/17096))
- Improve rendering of multiline error messages
  ([#17132](https://github.com/astral-sh/uv/pull/17132))
- Support redirects in `uv publish` ([#17130](https://github.com/astral-sh/uv/pull/17130))
- Include Docker images with the alpine version, e.g., `python3.x-alpine3.23`
  ([#17100](https://github.com/astral-sh/uv/pull/17100))

### Configuration

- Accept `--torch-backend` in `[tool.uv]` ([#17116](https://github.com/astral-sh/uv/pull/17116))

### Bug fixes

- Avoid panics due to reads on failed requests
  ([#17098](https://github.com/astral-sh/uv/pull/17098))
- Enforce latest-version in `@latest` requests
  ([#17114](https://github.com/astral-sh/uv/pull/17114))
- Explicitly set `EntryType` for file entries in tar
  ([#17043](https://github.com/astral-sh/uv/pull/17043))
- Ignore `pyproject.toml` index username in lockfile comparison
  ([#16995](https://github.com/astral-sh/uv/pull/16995))
- Relax error when using `uv add` with `UV_GIT_LFS` set
  ([#17127](https://github.com/astral-sh/uv/pull/17127))
- Support file locks on ExFAT on macOS ([#17115](https://github.com/astral-sh/uv/pull/17115))
- Change schema for `exclude-newer` into optional string
  ([#17121](https://github.com/astral-sh/uv/pull/17121))

## 0.9.17

Released on 2025-12-09.

### Enhancements

- Add `torch-tensorrt` and `torchao` to the PyTorch list
  ([#17053](https://github.com/astral-sh/uv/pull/17053))
- Add hint for misplaced `--verbose` in `uv tool run`
  ([#17020](https://github.com/astral-sh/uv/pull/17020))
- Add support for relative durations in `exclude-newer` (a.k.a., dependency cooldowns)
  ([#16814](https://github.com/astral-sh/uv/pull/16814))
- Add support for relocatable nushell activation script
  ([#17036](https://github.com/astral-sh/uv/pull/17036))

### Bug fixes

- Respect dropped (but explicit) indexes in dependency groups
  ([#17012](https://github.com/astral-sh/uv/pull/17012))

## 0.9.16

Released on 2025-12-06.

### Python

- Add CPython 3.14.2
- Add CPython 3.13.11

### Enhancements

- Add a 5m default timeout to acquiring file locks to fail faster on deadlock
  ([#16342](https://github.com/astral-sh/uv/pull/16342))
- Add a stub `debug` subcommand to `uv pip` announcing its intentional absence
  ([#16966](https://github.com/astral-sh/uv/pull/16966))
- Add bounds in `uv add --script` ([#16954](https://github.com/astral-sh/uv/pull/16954))
- Add brew specific message for `uv self update`
  ([#16838](https://github.com/astral-sh/uv/pull/16838))
- Error when built wheel is for the wrong platform
  ([#16074](https://github.com/astral-sh/uv/pull/16074))
- Filter wheels from PEP 751 files based on `--no-binary` et al in `uv pip compile`
  ([#16956](https://github.com/astral-sh/uv/pull/16956))
- Support `--target` and `--prefix` in `uv pip list`, `uv pip freeze`, and `uv pip show`
  ([#16955](https://github.com/astral-sh/uv/pull/16955))
- Tweak language for build backend validation errors
  ([#16720](https://github.com/astral-sh/uv/pull/16720))
- Use explicit credentials cache instead of global static
  ([#16768](https://github.com/astral-sh/uv/pull/16768))
- Enable SIMD in HTML parsing ([#17010](https://github.com/astral-sh/uv/pull/17010))

### Preview features

- Fix missing preview warning in `uv workspace metadata`
  ([#16988](https://github.com/astral-sh/uv/pull/16988))
- Add a `uv auth helper --protocol bazel` command
  ([#16886](https://github.com/astral-sh/uv/pull/16886))

### Bug fixes

- Fix Pyston wheel compatibility tags ([#16972](https://github.com/astral-sh/uv/pull/16972))
- Allow redundant entries in `tool.uv.build-backend.module-name` but emit warnings
  ([#16928](https://github.com/astral-sh/uv/pull/16928))
- Fix infinite loop in non-attribute re-treats during HTML parsing
  ([#17010](https://github.com/astral-sh/uv/pull/17010))

## 0.9.15

Released on 2025-12-02.

### Python

- Add CPython 3.14.1
- Add CPython 3.13.10

### Enhancements

- Add ROCm 6.4 to `--torch-backend=auto` ([#16919](https://github.com/astral-sh/uv/pull/16919))
- Add a Windows manifest to uv binaries ([#16894](https://github.com/astral-sh/uv/pull/16894))
- Add LFS toggle to Git sources ([#16143](https://github.com/astral-sh/uv/pull/16143))
- Cache source reads during resolution ([#16888](https://github.com/astral-sh/uv/pull/16888))
- Allow reading requirements from scripts without an extension
  ([#16923](https://github.com/astral-sh/uv/pull/16923))
- Allow reading requirements from scripts with HTTP(S) paths
  ([#16891](https://github.com/astral-sh/uv/pull/16891))

### Configuration

- Add `UV_HIDE_BUILD_OUTPUT` to omit build logs
  ([#16885](https://github.com/astral-sh/uv/pull/16885))

### Bug fixes

- Fix `uv-trampoline-builder` builds from crates.io by moving bundled executables
  ([#16922](https://github.com/astral-sh/uv/pull/16922))
- Respect `NO_COLOR` and always show the command as a header when paging `uv help` output
  ([#16908](https://github.com/astral-sh/uv/pull/16908))
- Use `0o666` permissions for flock files instead of `0o777`
  ([#16845](https://github.com/astral-sh/uv/pull/16845))
- Revert "Bump `astral-tl` to v0.7.10 (#16887)" to narrow down a regression causing hangs in
  metadata retrieval ([#16938](https://github.com/astral-sh/uv/pull/16938))

## 0.9.14

Released on 2025-12-01.

### Bug fixes

- Allow earlier post releases with exclusive ordering
  ([#16881](https://github.com/astral-sh/uv/pull/16881))
- Prefer updating existing `.zshenv` over creating a new one in `tool update-shell`
  ([#16866](https://github.com/astral-sh/uv/pull/16866))
- Respect `-e` flags in `uv add` ([#16882](https://github.com/astral-sh/uv/pull/16882))

### Enhancements

- Attach subcommand to User-Agent string ([#16837](https://github.com/astral-sh/uv/pull/16837))
- Prefer `UV_WORKING_DIR` over `UV_WORKING_DIRECTORY` for consistency
  ([#16884](https://github.com/astral-sh/uv/pull/16884))

## 0.9.13

Released on 2025-11-26.

### Bug fixes

- Revert "Allow `--with-requirements` to load extensionless inline-metadata scripts" to fix reading
  of requirements files from streams ([#16861](https://github.com/astral-sh/uv/pull/16861))
- Validate URL wheel tags against `Requires-Python` and required environments
  ([#16824](https://github.com/astral-sh/uv/pull/16824))

## 0.9.12

Released on 2025-11-24.

### Enhancements

- Allow `--with-requirements` to load extensionless inline-metadata scripts
  ([#16744](https://github.com/astral-sh/uv/pull/16744))
- Collect and upload PEP 740 attestations during `uv publish`
  ([#16731](https://github.com/astral-sh/uv/pull/16731))
- Prevent `uv export` from overwriting `pyproject.toml`
  ([#16745](https://github.com/astral-sh/uv/pull/16745))

## 0.9.11

Released on 2025-11-20.

### Python

- Add CPython 3.15.0a2

See the
[`python-build-standalone` release notes](https://github.com/astral-sh/python-build-standalone/releases/tag/20251120)
for details.

### Enhancements

- Add SBOM support to `uv export` ([#16523](https://github.com/astral-sh/uv/pull/16523))
- Publish to `crates.io` ([#16770](https://github.com/astral-sh/uv/pull/16770))

### Preview features

- Add `uv workspace list --paths` ([#16776](https://github.com/astral-sh/uv/pull/16776))
- Fix the preview warning on `uv workspace dir`
  ([#16775](https://github.com/astral-sh/uv/pull/16775))

### Bug fixes

- Fix `uv init` author serialization via `toml_edit` inline tables
  ([#16778](https://github.com/astral-sh/uv/pull/16778))
- Fix status messages without TTY ([#16785](https://github.com/astral-sh/uv/pull/16785))
- Preserve end-of-line comment whitespace when editing `pyproject.toml`
  ([#16734](https://github.com/astral-sh/uv/pull/16734))
- Disable `always-authenticate` when running under Dependabot
  ([#16773](https://github.com/astral-sh/uv/pull/16773))

## 0.9.10

Released on 2025-11-17.

### Enhancements

- Add support for `SSL_CERT_DIR` ([#16473](https://github.com/astral-sh/uv/pull/16473))
- Enforce UTF‑8-encoded license files during `uv build`
  ([#16699](https://github.com/astral-sh/uv/pull/16699))
- Error when a `project.license-files` glob matches nothing
  ([#16697](https://github.com/astral-sh/uv/pull/16697))
- `pip install --target` (and `sync`) install Python if necessary
  ([#16694](https://github.com/astral-sh/uv/pull/16694))
- Account for `python_downloads_json_url` in pre-release Python version warnings
  ([#16737](https://github.com/astral-sh/uv/pull/16737))
- Support HTTP/HTTPS URLs in `uv python --python-downloads-json-url`
  ([#16542](https://github.com/astral-sh/uv/pull/16542))

### Preview features

- Add support for `--upgrade` in `uv python install`
  ([#16676](https://github.com/astral-sh/uv/pull/16676))
- Fix handling of `python install --default` for pre-release Python versions
  ([#16706](https://github.com/astral-sh/uv/pull/16706))
- Add `uv workspace list` to list workspace members
  ([#16691](https://github.com/astral-sh/uv/pull/16691))

### Bug fixes

- Don't check file URLs for ambiguously parsed credentials
  ([#16759](https://github.com/astral-sh/uv/pull/16759))

## 0.9.9

Released on 2025-11-12.

### Deprecations

- Deprecate use of `--project` in `uv init` ([#16674](https://github.com/astral-sh/uv/pull/16674))

### Enhancements

- Add iOS support to Python interpreter discovery
  ([#16686](https://github.com/astral-sh/uv/pull/16686))
- Reject ambiguously parsed URLs ([#16622](https://github.com/astral-sh/uv/pull/16622))
- Allow explicit values in `uv version --bump`
  ([#16555](https://github.com/astral-sh/uv/pull/16555))
- Warn on use of managed pre-release Python versions when a stable version is available
  ([#16619](https://github.com/astral-sh/uv/pull/16619))
- Allow signing trampolines on Windows by using `.rcdata` to store metadata
  ([#15068](https://github.com/astral-sh/uv/pull/15068))
- Add `--only-emit-workspace` and similar variants to `uv export`
  ([#16681](https://github.com/astral-sh/uv/pull/16681))

### Preview features

- Add `uv workspace dir` command ([#16678](https://github.com/astral-sh/uv/pull/16678))
- Add `uv workspace metadata` command ([#16516](https://github.com/astral-sh/uv/pull/16516))

### Configuration

- Add `UV_NO_DEFAULT_GROUPS` environment variable
  ([#16645](https://github.com/astral-sh/uv/pull/16645))

### Bug fixes

- Remove `torch-model-archiver` and `torch-tb-profiler` from PyTorch backend
  ([#16655](https://github.com/astral-sh/uv/pull/16655))
- Fix Pixi environment detection ([#16585](https://github.com/astral-sh/uv/pull/16585))

## 0.9.8

Released on 2025-11-07.

### Enhancements

- Accept multiple packages in `uv export` ([#16603](https://github.com/astral-sh/uv/pull/16603))
- Accept multiple packages in `uv sync` ([#16543](https://github.com/astral-sh/uv/pull/16543))
- Add a `uv cache size` command ([#16032](https://github.com/astral-sh/uv/pull/16032))
- Add prerelease guidance for build-system resolution failures
  ([#16550](https://github.com/astral-sh/uv/pull/16550))
- Allow Python requests to include `+gil` to require a GIL-enabled interpreter
  ([#16537](https://github.com/astral-sh/uv/pull/16537))
- Avoid pluralizing 'retry' for single value ([#16535](https://github.com/astral-sh/uv/pull/16535))
- Enable first-class dependency exclusions ([#16528](https://github.com/astral-sh/uv/pull/16528))
- Fix inclusive constraints on available package versions in resolver errors
  ([#16629](https://github.com/astral-sh/uv/pull/16629))
- Improve `uv init` error for invalid directory names
  ([#16554](https://github.com/astral-sh/uv/pull/16554))
- Show help on `uv build -h` ([#16632](https://github.com/astral-sh/uv/pull/16632))
- Include the Python variant suffix in "Using Python ..." messages
  ([#16536](https://github.com/astral-sh/uv/pull/16536))
- Log most recently modified file for cache-keys
  ([#16338](https://github.com/astral-sh/uv/pull/16338))
- Update Docker builds to use nightly Rust toolchain with musl v1.2.5
  ([#16584](https://github.com/astral-sh/uv/pull/16584))
- Add GitHub attestations for uv release artifacts
  ([#11357](https://github.com/astral-sh/uv/pull/11357))

### Configuration

- Expose `UV_NO_GROUP` as an environment variable
  ([#16529](https://github.com/astral-sh/uv/pull/16529))
- Add `UV_NO_SOURCES` as an environment variable
  ([#15883](https://github.com/astral-sh/uv/pull/15883))

### Bug fixes

- Allow `--check` and `--locked` to be used together in `uv lock`
  ([#16538](https://github.com/astral-sh/uv/pull/16538))
- Allow for unnormalized names in the METADATA file (#16547)
  ([#16548](https://github.com/astral-sh/uv/pull/16548))
- Fix missing value_type for `default-groups` in schema
  ([#16575](https://github.com/astral-sh/uv/pull/16575))
- Respect multi-GPU outputs in `nvidia-smi` ([#15460](https://github.com/astral-sh/uv/pull/15460))
- Fix DNS lookup errors in Docker containers ([#8450](https://github.com/astral-sh/uv/issues/8450))

## 0.9.7

Released on 2025-10-30.

### Enhancements

- Add Windows x86-32 emulation support to interpreter architecture checks
  ([#13475](https://github.com/astral-sh/uv/pull/13475))
- Improve readability of progress bars ([#16509](https://github.com/astral-sh/uv/pull/16509))

### Bug fixes

- Drop terminal coloring from `uv auth token` output
  ([#16504](https://github.com/astral-sh/uv/pull/16504))
- Don't use UV_LOCKED to enable `--check` flag
  ([#16521](https://github.com/astral-sh/uv/pull/16521))

## 0.9.6

Released on 2025-10-29.

This release contains an upgrade to Astral's fork of `async_zip`, which addresses potential sources
of ZIP parsing differentials between uv and other Python packaging tooling. See
[GHSA-pqhf-p39g-3x64](https://github.com/astral-sh/uv/security/advisories/GHSA-pqhf-p39g-3x64) for
additional details.

### Security

- Address ZIP parsing differentials
  ([GHSA-pqhf-p39g-3x64](https://github.com/astral-sh/uv/security/advisories/GHSA-pqhf-p39g-3x64))

### Python

- Upgrade GraalPy to 25.0.1 ([#16401](https://github.com/astral-sh/uv/pull/16401))

### Enhancements

- Add `--clear` to `uv build` to remove old build artifacts
  ([#16371](https://github.com/astral-sh/uv/pull/16371))
- Add `--no-create-gitignore` to `uv build` ([#16369](https://github.com/astral-sh/uv/pull/16369))
- Do not error when a virtual environment directory cannot be removed due to a busy error
  ([#16394](https://github.com/astral-sh/uv/pull/16394))
- Improve hint on `pip install --system` when externally managed
  ([#16392](https://github.com/astral-sh/uv/pull/16392))
- Running `uv lock --check` with outdated lockfile will print that `--check` was passed, instead of
  `--locked` ([#16322](https://github.com/astral-sh/uv/pull/16322))
- Update `uv init` template for Maturin ([#16449](https://github.com/astral-sh/uv/pull/16449))
- Improve ordering of Python sources in logs ([#16463](https://github.com/astral-sh/uv/pull/16463))
- Restore DockerHub release images and annotations
  ([#16441](https://github.com/astral-sh/uv/pull/16441))

### Bug fixes

- Check for matching Python implementation during `uv python upgrade`
  ([#16420](https://github.com/astral-sh/uv/pull/16420))
- Deterministically order `--find-links` distributions
  ([#16446](https://github.com/astral-sh/uv/pull/16446))
- Don't panic in `uv export --frozen` when the lockfile is outdated
  ([#16407](https://github.com/astral-sh/uv/pull/16407))
- Fix root of `uv tree` when `--package` is used with circular dependencies
  ([#15908](https://github.com/astral-sh/uv/pull/15908))
- Show package list with `pip freeze --quiet` ([#16491](https://github.com/astral-sh/uv/pull/16491))
- Limit `uv auth login pyx.dev` retries to 60s
  ([#16498](https://github.com/astral-sh/uv/pull/16498))
- Add an empty group with `uv add --group ... -r ...`
  ([#16490](https://github.com/astral-sh/uv/pull/16490))

## 0.9.5

Released on 2025-10-21.

This release contains an upgrade to `astral-tokio-tar`, which addresses a vulnerability in tar
extraction on malformed archives with mismatching size information between the ustar header and PAX
extensions. While the `astral-tokio-tar` advisory has been graded as "high" due its potential
broader impact, the _specific_ impact to uv is **low** due to a lack of novel attacker capability.
Specifically, uv only processes tar archives from source distributions, which already possess the
capability for full arbitrary code execution by design, meaning that an attacker gains no additional
capabilities through `astral-tokio-tar`.

Regardless, we take the hypothetical risk of parser differentials very seriously. Out of an
abundance of caution, we have assigned this upgrade an advisory:
https://github.com/astral-sh/uv/security/advisories/GHSA-w476-p2h3-79g9

### Security

- Upgrade `astral-tokio-tar` to 0.5.6 to address a parsing differential
  ([#16387](https://github.com/astral-sh/uv/pull/16387))

### Enhancements

- Add required environment marker example to hint
  ([#16244](https://github.com/astral-sh/uv/pull/16244))
- Fix typo in MissingTopLevel warning ([#16351](https://github.com/astral-sh/uv/pull/16351))
- Improve 403 Forbidden error message to indicate package may not exist
  ([#16353](https://github.com/astral-sh/uv/pull/16353))
- Add a hint on `uv pip install` failure if the `--system` flag is used to select an externally
  managed interpreter ([#16318](https://github.com/astral-sh/uv/pull/16318))

### Bug fixes

- Fix backtick escaping for PowerShell ([#16307](https://github.com/astral-sh/uv/pull/16307))

## 0.9.4

Released on 2025-10-17.

### Enhancements

- Add CUDA 13.0 support ([#16321](https://github.com/astral-sh/uv/pull/16321))
- Add auto-detection for Intel GPU on Windows ([#16280](https://github.com/astral-sh/uv/pull/16280))
- Implement display of RFC 9457 HTTP error contexts
  ([#16199](https://github.com/astral-sh/uv/pull/16199))

### Bug fixes

- Avoid obfuscating pyx tokens in `uv auth token` output
  ([#16345](https://github.com/astral-sh/uv/pull/16345))

## 0.9.3

Released on 2025-10-14.

### Python

- Add CPython 3.15.0a1
- Add CPython 3.13.9

### Enhancements

- Obfuscate secret token values in logs ([#16164](https://github.com/astral-sh/uv/pull/16164))

### Bug fixes

- Fix workspace with relative pathing ([#16296](https://github.com/astral-sh/uv/pull/16296))

## 0.9.2

Released on 2025-10-10.

### Python

- Add CPython 3.9.24.
- Add CPython 3.10.19.
- Add CPython 3.11.14.
- Add CPython 3.12.12.

### Enhancements

- Avoid inferring check URLs for pyx in `uv publish`
  ([#16234](https://github.com/astral-sh/uv/pull/16234))
- Add `uv tool list --show-python` ([#15814](https://github.com/astral-sh/uv/pull/15814))

## 0.9.1

Released on 2025-10-09.

### Enhancements

- Log Python choice in `uv init` ([#16182](https://github.com/astral-sh/uv/pull/16182))
- Fix `pylock.toml` config conflict error messages
  ([#16211](https://github.com/astral-sh/uv/pull/16211))

### Configuration

- Add `UV_UPLOAD_HTTP_TIMEOUT` and respect `UV_HTTP_TIMEOUT` in uploads
  ([#16040](https://github.com/astral-sh/uv/pull/16040))
- Support `UV_WORKING_DIRECTORY` for setting `--directory`
  ([#16125](https://github.com/astral-sh/uv/pull/16125))

### Bug fixes

- Allow missing `Scripts` directory ([#16206](https://github.com/astral-sh/uv/pull/16206))
- Fix handling of Python requests with pre-releases in ranges
  ([#16208](https://github.com/astral-sh/uv/pull/16208))
- Preserve comments on version bump ([#16141](https://github.com/astral-sh/uv/pull/16141))
- Retry all HTTP/2 errors ([#16038](https://github.com/astral-sh/uv/pull/16038))
- Treat deleted Windows registry keys as equivalent to missing ones
  ([#16194](https://github.com/astral-sh/uv/pull/16194))
- Ignore pre-release Python versions when a patch version is requested
  ([#16210](https://github.com/astral-sh/uv/pull/16210))
