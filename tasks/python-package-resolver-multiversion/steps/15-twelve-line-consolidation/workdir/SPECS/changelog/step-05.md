# uv release notes, step 05: releases in the span `0.3.0 -> 0.4.20` (upstream tag `0.4.20`)

Source: astral-sh/uv at tag `0.4.20`, file(s) `CHANGELOG.md`. 26 release entries: 0.4.20, 0.4.19, 0.4.18, 0.4.17, 0.4.16, 0.4.15, 0.4.14, 0.4.13, 0.4.12, 0.4.11, 0.4.10, 0.4.9, 0.4.8, 0.4.7, 0.4.6, 0.4.5, 0.4.4, 0.4.3, 0.4.2, 0.4.1, 0.4.0, 0.3.5, 0.3.4, 0.3.3, 0.3.2, 0.3.1. Licence: MIT OR Apache-2.0 (astral-sh/uv; see SPECS/licences/uv-LICENSE-MIT and SPECS/licences/uv-LICENSE-APACHE).

Kept sections: Breaking changes, Enhancements, Bug fixes, CLI, Configuration, Error messages, Python, Python versions, Preview, Stabilizations, Deprecations, Compatibility, Security, plus each release's preamble. Dropped: Documentation, Performance, Release, Rust API, Rust libraries, Other changes, Distributions. Kept text is verbatim.

## 0.4.20

### Enhancements

- Add managed downloads for CPython 3.13.0 (final) ([#8010](https://github.com/astral-sh/uv/pull/8010))
- Python 3.13 is the default version for `uv python install` ([#8010](https://github.com/astral-sh/uv/pull/8010))
- Hint at wrong endpoint in `uv publish` failures ([#7872](https://github.com/astral-sh/uv/pull/7872))
- List available scripts when a command is not specified for `uv run` ([#7687](https://github.com/astral-sh/uv/pull/7687))
- Fill in `authors` filed during `uv init` ([#7756](https://github.com/astral-sh/uv/pull/7756))

## 0.4.19

### Enhancements

- Add managed downloads for CPython 3.13.0rc3 and 3.12.7 ([#7880](https://github.com/astral-sh/uv/pull/7880))
- Display the target virtual environment path if non-default ([#7850](https://github.com/astral-sh/uv/pull/7850))
- Preserve case-insensitive sorts in `uv add` ([#7864](https://github.com/astral-sh/uv/pull/7864))
- Respect project upper bounds when filtering wheels on `requires-python` ([#7904](https://github.com/astral-sh/uv/pull/7904))
- Add `--script` to `uv run` to treat an input as PEP 723 regardless of extension ([#7739](https://github.com/astral-sh/uv/pull/7739))
- Improve legibility of build failure errors ([#7854](https://github.com/astral-sh/uv/pull/7854))
- Show interpreter source during Python discovery query errors ([#7928](https://github.com/astral-sh/uv/pull/7928))

### Configuration

- Add `UV_FIND_LINKS` environment variable for `--find-links` ([#7912](https://github.com/astral-sh/uv/pull/7912))
- Ignore empty string values for `UV_PYTHON` environment variable ([#7878](https://github.com/astral-sh/uv/pull/7878))

### Bug fixes

- Allow `py3x-none` tags in newer than Python 3.x ([#7867](https://github.com/astral-sh/uv/pull/7867))
- Allow self-dependencies in the `dev` section ([#7943](https://github.com/astral-sh/uv/pull/7943))
- Always ignore `cp2` wheels in resolution ([#7902](https://github.com/astral-sh/uv/pull/7902))
- Clear the publish progress bar on retry ([#7921](https://github.com/astral-sh/uv/pull/7921))
- Fix parsing of `gnueabi` libc variants in Python version requests ([#7975](https://github.com/astral-sh/uv/pull/7975))
- Simplify supported environments when comparing to lockfile ([#7894](https://github.com/astral-sh/uv/pull/7894))
- Trim commits when reading from Git refs ([#7922](https://github.com/astral-sh/uv/pull/7922))
- Use a higher HTTP read timeout when publishing packages ([#7923](https://github.com/astral-sh/uv/pull/7923))
- Remove the first empty line for `uv tree --package foo` ([#7885](https://github.com/astral-sh/uv/pull/7885))

## 0.4.18

### Enhancements

- Allow multiple source entries for each package in `tool.uv.sources` ([#7745](https://github.com/astral-sh/uv/pull/7745))
- Add `.gitignore` file to `uv build` output directory ([#7835](https://github.com/astral-sh/uv/pull/7835))
- Disable jemalloc on FreeBSD ([#7780](https://github.com/astral-sh/uv/pull/7780))
- Respect `PAGER` env var when paging in `uv help` command ([#5511](https://github.com/astral-sh/uv/pull/5511))
- Support `uv run -m foo` to run a module ([#7754](https://github.com/astral-sh/uv/pull/7754))
- Use a top-level output directory for `uv build` in workspaces ([#7813](https://github.com/astral-sh/uv/pull/7813))
- Update `uv init --package` command to match project name ([#7670](https://github.com/astral-sh/uv/pull/7670))
- Add a custom suggestion for `uv add dotenv` ([#7799](https://github.com/astral-sh/uv/pull/7799))
- Add detailed errors for `tool.uv.sources` deserialization failures ([#7823](https://github.com/astral-sh/uv/pull/7823))
- Improve error message copy for failed builds ([#7849](https://github.com/astral-sh/uv/pull/7849))
- Use `serde-untagged` to improve some untagged enum error messages ([#7822](https://github.com/astral-sh/uv/pull/7822))
- Use build failure hints for `dotenv` errors, rather than in `uv add` ([#7825](https://github.com/astral-sh/uv/pull/7825))

### Configuration

- Add `UV_NO_SYNC` environment variable ([#7752](https://github.com/astral-sh/uv/pull/7752))

### Bug fixes

- Accept `git+` prefix in `tool.uv.sources` ([#7847](https://github.com/astral-sh/uv/pull/7847))
- Allow spaces in path requirements ([#7767](https://github.com/astral-sh/uv/pull/7767))
- Avoid reusing cached downloaded binaries with `--no-binary` ([#7772](https://github.com/astral-sh/uv/pull/7772))
- Correctly trims values during wheel WHEEL file parsing ([#7770](https://github.com/astral-sh/uv/pull/7770))
- Fix `uv tree --invert` for platform dependencies ([#7808](https://github.com/astral-sh/uv/pull/7808))
- Fix encoding mismatch between python child process and uv ([#7757](https://github.com/astral-sh/uv/pull/7757))
- Reject self-dependencies in `uv add` ([#7766](https://github.com/astral-sh/uv/pull/7766))
- Respect `tool.uv.environments` for legacy virtual workspace roots ([#7824](https://github.com/astral-sh/uv/pull/7824))
- Retain empty extras on workspace members ([#7762](https://github.com/astral-sh/uv/pull/7762))
- Use file stem when parsing cached wheel names ([#7773](https://github.com/astral-sh/uv/pull/7773))

## 0.4.17

### Enhancements

- Add `uv build --all` to build all packages in a workspace ([#7724](https://github.com/astral-sh/uv/pull/7724))
- Add support for `uv init --script` ([#7565](https://github.com/astral-sh/uv/pull/7565))
- Add support for upgrading build environment for installed tools (`uv tool upgrade --python`) ([#7605](https://github.com/astral-sh/uv/pull/7605))
- Initialize a Git repository in `uv init` ([#5476](https://github.com/astral-sh/uv/pull/5476))
- Respect `--quiet` flag in `uv build` ([#7674](https://github.com/astral-sh/uv/pull/7674))
- Add context message before listing available tools in `uvx` ([#7641](https://github.com/astral-sh/uv/pull/7641))

### Bug fixes

- Don't create Python bytecode files during interpreter discovery ([#7707](https://github.com/astral-sh/uv/pull/7707))
- Escape glob patterns in workspace member discovery ([#7709](https://github.com/astral-sh/uv/pull/7709))
- Avoid prefetching source distributions with unbounded lower-bound ranges ([#7683](https://github.com/astral-sh/uv/pull/7683))

## 0.4.16

### Enhancements

- Add `uv publish` ([#7475](https://github.com/astral-sh/uv/pull/7475))
- Add a `--project` argument to run a command from a project directory ([#7603](https://github.com/astral-sh/uv/pull/7603))
- Display Python implementation when creating environments ([#7652](https://github.com/astral-sh/uv/pull/7652))
- Implement trusted publishing for `uv publish` ([#7548](https://github.com/astral-sh/uv/pull/7548))
- Respect lockfile preferences for `--with` requirements ([#7627](https://github.com/astral-sh/uv/pull/7627))
- Unhide the `--directory` option ([#7653](https://github.com/astral-sh/uv/pull/7653))
- Allow requesting free-threaded Python interpreters ([#7431](https://github.com/astral-sh/uv/pull/7431))
- Show a dedicated PubGrub hint for `--unsafe-best-match` ([#7645](https://github.com/astral-sh/uv/pull/7645))
- Add resolver error checking for conflicting distributions ([#7595](https://github.com/astral-sh/uv/pull/7595))

### Bug fixes

- Avoid adding double-newlines for CRLF ([#7640](https://github.com/astral-sh/uv/pull/7640))
- Avoid retaining forks when `requires-python` range changes ([#7624](https://github.com/astral-sh/uv/pull/7624))
- Determine if pre-release Python downloads should be allowed using the version specifiers ([#7638](https://github.com/astral-sh/uv/pull/7638))
- Fix `link-mode=clone` for directories on Linux ([#7620](https://github.com/astral-sh/uv/pull/7620))
- Improve Python executable name discovery when using alternative implementations ([#7649](https://github.com/astral-sh/uv/pull/7649))
- Require opt-in to use alternative Python implementations ([#7650](https://github.com/astral-sh/uv/pull/7650))
- Use the first pre-release discovered when only pre-release Python versions are available ([#7666](https://github.com/astral-sh/uv/pull/7666))

### Preview features

- Add build backend scaffolding ([#7662](https://github.com/astral-sh/uv/pull/7662))

## 0.4.15

### Bug fixes

- Revert "Treat invalid platform as more compatible than invalid Python (#7556)" ([#7608](https://github.com/astral-sh/uv/pull/7608))

## 0.4.14

### Breaking

- Move uvx shell completion to `uvx --generate-shell-completion` ([#7511](https://github.com/astral-sh/uv/pull/7511))

### Enhancements

- Adjust messaging for frozen hint on resolution failure during `uv add` ([#7597](https://github.com/astral-sh/uv/pull/7597))
- Provide resolution hints in case of possible local name conflicts ([#7505](https://github.com/astral-sh/uv/pull/7505))
- Improve Docker image release tagging order and display on `ghcr.io` ([#7568](https://github.com/astral-sh/uv/pull/7568))
- Improve deserialization error messages ([#7598](https://github.com/astral-sh/uv/pull/7598))

### Bug fixes

- Allow system environments during project environment validity check ([#7585](https://github.com/astral-sh/uv/pull/7585))
- Avoid validating workspace members when `--no-sources` is provided ([#7599](https://github.com/astral-sh/uv/pull/7599))
- Fix handling of `sys.base_prefix` collision in interpreter identity check during tool installs ([#7596](https://github.com/astral-sh/uv/pull/7596))
- Make `uv cache prune` robust to unreadable rkyv entries ([#7561](https://github.com/astral-sh/uv/pull/7561))
- Revert "Remove duplicate warning for settings discovery errors (#7384)" ([#7594](https://github.com/astral-sh/uv/pull/7594))

## 0.4.13

### Enhancements

- Add `socks` support ([#7503](https://github.com/astral-sh/uv/pull/7503))
- Avoid warning about bad Python interpreter links for empty project environment directories ([#7527](https://github.com/astral-sh/uv/pull/7527))
- Improve invalid environment warning messages ([#7544](https://github.com/astral-sh/uv/pull/7544))
- Use more verbose spelling of "virtualenv" during creation ([#7523](https://github.com/astral-sh/uv/pull/7523))
- Do not use a user-facing warning for "Waiting to acquire lock..." message ([#7502](https://github.com/astral-sh/uv/pull/7502))

### Bug fixes

- Allow Python pre-releases to be used if they are first on the `PATH` ([#7470](https://github.com/astral-sh/uv/pull/7470))
- Avoid deleting the project environment directory if it is not a virtual environment ([#7522](https://github.com/astral-sh/uv/pull/7522))
- Do not error if the `CACHEDIR.TAG` file exists but cannot be written to ([#7550](https://github.com/astral-sh/uv/pull/7550))
- Treat invalid platform as more compatible than invalid Python ([#7556](https://github.com/astral-sh/uv/pull/7556))
- Use portable paths when serializing sources ([#7504](https://github.com/astral-sh/uv/pull/7504))
- Compute resolver hints using the final reduced derivation tree ([#7546](https://github.com/astral-sh/uv/pull/7546))
- Bump the wheel and sdist cache versions ([#7560](https://github.com/astral-sh/uv/pull/7560))
- Heal cache entries with missing source distributions ([#7559](https://github.com/astral-sh/uv/pull/7559))

## 0.4.12

### Enhancements

- Allow users to provide pre-defined metadata for resolution ([#7442](https://github.com/astral-sh/uv/pull/7442))
- Invalidate existing tool environments on Python interpreter mismatch ([#7451](https://github.com/astral-sh/uv/pull/7451))

### Bug fixes

- Avoid fatal error when searching for egg-info with missing directory ([#7498](https://github.com/astral-sh/uv/pull/7498))

## 0.4.11

### Enhancements

- Add `--no-editable` support to `uv sync` and `uv export` ([#7371](https://github.com/astral-sh/uv/pull/7371))
- Add support for `--only-dev` to `uv sync` and `uv export` ([#7367](https://github.com/astral-sh/uv/pull/7367))
- Add support for remaining pip-supported file extensions ([#7387](https://github.com/astral-sh/uv/pull/7387))
- Generate shell completion for `uvx` ([#7388](https://github.com/astral-sh/uv/pull/7388))
- Include `uv export` command in `requirements.txt` output ([#7374](https://github.com/astral-sh/uv/pull/7374))
- Prune unzipped source distributions in `uv cache prune --ci` ([#7446](https://github.com/astral-sh/uv/pull/7446))
- Warn when trying to `uv sync` a package without build configuration ([#7420](https://github.com/astral-sh/uv/pull/7420))
- Support requests for pre-releases in the `--python` option ([#7335](https://github.com/astral-sh/uv/pull/7335))

### Bug fixes

- Avoid erroneous version warning for `.dist-info` directories ([#7444](https://github.com/astral-sh/uv/pull/7444))
- Avoid removing seed packages for `uv venv --seed` environments ([#7410](https://github.com/astral-sh/uv/pull/7410))
- Avoid unnecessary progress bar initializations ([#7412](https://github.com/astral-sh/uv/pull/7412))
- Error when `tool.uv.sources` contains duplicate package names ([#7383](https://github.com/astral-sh/uv/pull/7383))
- Include `--branch` et al when resolving unnamed URLs in `uv add` ([#7447](https://github.com/astral-sh/uv/pull/7447))
- Include `dev-dependencies` in `--no-sources` invocations ([#7408](https://github.com/astral-sh/uv/pull/7408))
- Include the parent interpreter in Python discovery when `--system` is used ([#7440](https://github.com/astral-sh/uv/pull/7440))
- Respect `--no-sources` in PEP 723 scripts ([#7409](https://github.com/astral-sh/uv/pull/7409))
- Respect `pyproject.toml` credentials from user-provided requirements ([#7474](https://github.com/astral-sh/uv/pull/7474))
- Use consistent PyPI cache bucket ([#7443](https://github.com/astral-sh/uv/pull/7443))
- Use unambiguous relative paths in `uv export` ([#7378](https://github.com/astral-sh/uv/pull/7378))

### Error messages

- Hint at missing `project.name` ([#6803](https://github.com/astral-sh/uv/pull/6803))
- Surface dedicated `project.name` error for workspaces ([#7399](https://github.com/astral-sh/uv/pull/7399))
- Remove duplicate warning for settings discovery errors ([#7384](https://github.com/astral-sh/uv/pull/7384))

## 0.4.10

### Enhancements

- Allow `uv tool upgrade --all` to continue on individual upgrade failure ([#7333](https://github.com/astral-sh/uv/pull/7333))
- Support globs as cache keys in `tool.uv.cache-keys` ([#7268](https://github.com/astral-sh/uv/pull/7268))
- Add Python package (`__main__.py`) support to `uv run` ([#7281](https://github.com/astral-sh/uv/pull/7281))
- Add zip application support to `uv run` ([#7289](https://github.com/astral-sh/uv/pull/7289))
- Add `--token` option to `self update` command ([#7279](https://github.com/astral-sh/uv/pull/7279))

### Bug fixes

- Always treat archive-like requirements as local files ([#7364](https://github.com/astral-sh/uv/pull/7364))
- Apply `--no-install` options when constructing resolution ([#7277](https://github.com/astral-sh/uv/pull/7277))
- Avoid clobbering existing `py.typed` files contents in `uv init` ([#7338](https://github.com/astral-sh/uv/pull/7338))
- Avoid enforcing platform compatibility when validating lockfile ([#7305](https://github.com/astral-sh/uv/pull/7305))
- Avoid installing transitive dev dependencies ([#7318](https://github.com/astral-sh/uv/pull/7318))
- Avoid selecting prerelease Python installations without opt-in ([#7300](https://github.com/astral-sh/uv/pull/7300))
- Fix PPC64 page size in binary builds. ([#7298](https://github.com/astral-sh/uv/pull/7298))
- Include pre-release Python versions in `uv python list` ([#7290](https://github.com/astral-sh/uv/pull/7290))
- Make version ID optional for source builds ([#7362](https://github.com/astral-sh/uv/pull/7362))
- Support relative paths in `uv add --script` ([#7301](https://github.com/astral-sh/uv/pull/7301))

### Error messages

- Add dedicated lock errors for wheel-only distributions ([#7307](https://github.com/astral-sh/uv/pull/7307))
- Avoid treating `.whl` sources as source distributions ([#7303](https://github.com/astral-sh/uv/pull/7303))
- Clarify Python requirement source for script incompatibilities ([#7339](https://github.com/astral-sh/uv/pull/7339))

## 0.4.9

### Enhancements

- Add support for managed Python 3.13 ([#7263](https://github.com/astral-sh/uv/pull/7263))
- Upgrade managed CPython versions to latest patch releases ([#7263](https://github.com/astral-sh/uv/pull/7263))
- Allow setting a target version for `uv self update` ([#7252](https://github.com/astral-sh/uv/pull/7252))
- Create `py.typed` files during `uv init --lib` ([#7232](https://github.com/astral-sh/uv/pull/7232))
- Add a dedicated error for packages that fail due to `distutils` deprecation ([#7239](https://github.com/astral-sh/uv/pull/7239))
- Improve error message when requested Python version is unsupported ([#7269](https://github.com/astral-sh/uv/pull/7269))
- Add `uv run --no-sync` ([#7192]((https://github.com/astral-sh/uv/pull/7192))

### Bug fixes

- Avoid updating `pyproject.toml` offsets on non-add edits ([#7262](https://github.com/astral-sh/uv/pull/7262))
- Invalidate cache when `--config-settings` change ([#7139](https://github.com/astral-sh/uv/pull/7139))
- Remove workspace root for single-member workspace with `uv export` ([#7254](https://github.com/astral-sh/uv/pull/7254))

## 0.4.8

### Enhancements

- Add support for dynamic cache keys ([#7136](https://github.com/astral-sh/uv/pull/7136))
- Allow `.dist-info` names with dashes for post releases ([#7208](https://github.com/astral-sh/uv/pull/7208))
- Use type hints in code from `uv init` ([#7225](https://github.com/astral-sh/uv/pull/7225))
- Treat `.tgz` the same as `.tar.gz` ([#7201](https://github.com/astral-sh/uv/pull/7201))
- Direct users towards `uv venv` to create a virtual environment ([#7188](https://github.com/astral-sh/uv/pull/7188))
- Improve error message for uv init already init-ed ([#7198](https://github.com/astral-sh/uv/pull/7198))

### Bug fixes

- Avoid extra newlines in debug logging for source builds ([#7174](https://github.com/astral-sh/uv/pull/7174))
- Prune unreachable packages from `--universal` output ([#7209](https://github.com/astral-sh/uv/pull/7209))
- Respect exclusion when collecting workspace members ([#7175](https://github.com/astral-sh/uv/pull/7175))
- Use path file instead of `sitecustomize.py` ([#7161](https://github.com/astral-sh/uv/pull/7161))
- Replace incorrect `--source` and `--binary` flags with correct `--sdist` and `--wheel` flags in `uv build` ([#7156](https://github.com/astral-sh/uv/pull/7156))

## 0.4.7

### Enhancements

- Add `--no-emit-project` and friends to `uv export` ([#7110](https://github.com/astral-sh/uv/pull/7110))
- Add `--output-file` to `uv export` ([#7109](https://github.com/astral-sh/uv/pull/7109))
- Prune unused source distributions from the cache in `uv cache prune` ([#7112](https://github.com/astral-sh/uv/pull/7112))
- Take intersection of constraint and requirements hashes ([#7108](https://github.com/astral-sh/uv/pull/7108))

### Bug fixes

- Avoid panicking when encountering an invalid Python version during `uv python list` ([#7131](https://github.com/astral-sh/uv/pull/7131))
- Write trailing newline to `.python-version` files ([#7140](https://github.com/astral-sh/uv/pull/7140))

## 0.4.6

### Enhancements

- Accept `--build-constraint` in `uv build` ([#7085](https://github.com/astral-sh/uv/pull/7085))
- Add `--require-hashes` and `--verify-hashes` to `uv build` ([#7094](https://github.com/astral-sh/uv/pull/7094))
- Add `--show-version-specifiers` to `uv tool list` ([#7050](https://github.com/astral-sh/uv/pull/7050))
- Respect hashes in constraints files ([#7093](https://github.com/astral-sh/uv/pull/7093))
- Upgrade installer scripts ([#7092](https://github.com/astral-sh/uv/pull/7092))
- Allow specifying multiple packages in `uv tool upgrade` and `uninstall` ([#7037](https://github.com/astral-sh/uv/pull/7037))
- Sort by implementation in `uv python list` ([#6918](https://github.com/astral-sh/uv/pull/6918))

### Bug fixes

- Invalidate lockfile when member versions change ([#7102](https://github.com/astral-sh/uv/pull/7102))
- Strip fragments from direct source URLs in lockfile ([#7061](https://github.com/astral-sh/uv/pull/7061))
- Support `--no-build` and `--no-binary` in `uv sync` et al ([#7100](https://github.com/astral-sh/uv/pull/7100))
- Use distribution hash over registry hash ([#7060](https://github.com/astral-sh/uv/pull/7060))
- Fix inverted log message ([#7063](https://github.com/astral-sh/uv/pull/7063))
- Adjust Docker `ENTRYPOINT` and `CMD` for inherited images ([#7054](https://github.com/astral-sh/uv/pull/7054))

## 0.4.5

### Enhancements

- Implement `uv build` ([#6895](https://github.com/astral-sh/uv/pull/6895))
- Add `--package` support to `uv build` ([#6990](https://github.com/astral-sh/uv/pull/6990))
- Prune unreachable packages from lockfile ([#6959](https://github.com/astral-sh/uv/pull/6959))
- Prune unreachable wheels from lockfile ([#6961](https://github.com/astral-sh/uv/pull/6961))
- Show build output by default in `uv build` ([#6912](https://github.com/astral-sh/uv/pull/6912))
- Support `uv build --wheel` from source distributions ([#6898](https://github.com/astral-sh/uv/pull/6898))
- Use the root project name for the project virtual environment prompt ([#7021](https://github.com/astral-sh/uv/pull/7021))

### Bug fixes

- Fix handling of inline optional dependencies in `uv add` ([#7023](https://github.com/astral-sh/uv/pull/7023))
- Reflect exit code in `uv tool run` and `uv run` ([#6994](https://github.com/astral-sh/uv/pull/6994))
- Revert `pyproject.toml` modifications on Ctrl-C ([#7024](https://github.com/astral-sh/uv/pull/7024))
- Rollback `pyproject.toml` changes on all errors ([#7022](https://github.com/astral-sh/uv/pull/7022))
- Use correct ordering semantics for narrowing upper-bounded Python requirements ([#7031](https://github.com/astral-sh/uv/pull/7031))
- Fix segfault in Windows trampolines ([#6955](https://github.com/astral-sh/uv/pull/6955))
- Remove unused `__future__.annotations` import in `_virtualenv.py` ([#6996](https://github.com/astral-sh/uv/pull/6996))

## 0.4.4

### Enhancements

- Allow customizing the project environment path with `UV_PROJECT_ENVIRONMENT` ([#6834](https://github.com/astral-sh/uv/pull/6834))
- Warn when `VIRTUAL_ENV` is set but will not be respected in project commands ([#6864](https://github.com/astral-sh/uv/pull/6864))
- Add `--no-hashes` to `uv export` ([#6954](https://github.com/astral-sh/uv/pull/6954))
- Make HTTP headers title case for backward compatibility ([#6887](https://github.com/astral-sh/uv/pull/6887))
- Pin `.python-version` in `uv init` ([#6869](https://github.com/astral-sh/uv/pull/6869))
- Support `file://` URLs for `UV_PYTHON_INSTALL_MIRROR` ([#6950](https://github.com/astral-sh/uv/pull/6950))
- Introduce more docker tags for uv ([#6053](https://github.com/astral-sh/uv/pull/6053))

### Bug fixes

- Avoid canonicalizing the cache directory ([#6949](https://github.com/astral-sh/uv/pull/6949))
- Show all PyPy versions in `uv python list --all-versions` ([#6917](https://github.com/astral-sh/uv/pull/6917))
- Avoid incorrect `requires-python` marker simplifications ([#6268](https://github.com/astral-sh/uv/pull/6268))

## 0.4.3

### Enhancements

- Show build backend output when `--verbose` is provided ([#6903](https://github.com/astral-sh/uv/pull/6903))
- Allow `uv sync --frozen --package` without copying member `pyproject.toml` ([#6943](https://github.com/astral-sh/uv/pull/6943))

### Bug fixes

- Avoid panic with missing temporary directory ([#6929](https://github.com/astral-sh/uv/pull/6929))
- Avoid updating incorrect dependencies for sorted `uv add` ([#6939](https://github.com/astral-sh/uv/pull/6939))
- Use lower-bound semantics for all Python compatibility comparisons ([#6882](https://github.com/astral-sh/uv/pull/6882))

## 0.4.2

### Enhancements

- Adding support for `.pyc` files in `uv run` ([#6886](https://github.com/astral-sh/uv/pull/6886))
- Treat missing `top_level.txt` as non-fatal ([#6881](https://github.com/astral-sh/uv/pull/6881))

### Bug fixes

- Fix `is_disjoint` check for supported environments ([#6902](https://github.com/astral-sh/uv/pull/6902))
- Remove dangling archives in `uv cache clean ${package}` ([#6915](https://github.com/astral-sh/uv/pull/6915))
- Error when discovered Python is incompatible with `--isolated` workspace ([#6885](https://github.com/astral-sh/uv/pull/6885))
- Warn when discovered Python is incompatible with PEP 723 script ([#6884](https://github.com/astral-sh/uv/pull/6884))

## 0.4.1

### Enhancements

- Add `uv export --format requirements-txt` ([#6778](https://github.com/astral-sh/uv/pull/6778))
- Allow `@` references in `uv tool install --from` ([#6842](https://github.com/astral-sh/uv/pull/6842))
- Normalize version specifiers by sorting ([#6333](https://github.com/astral-sh/uv/pull/6333))
- Respect the user's upper-bound in `requires-python` ([#6824](https://github.com/astral-sh/uv/pull/6824))
- Use Windows registry to discover Python on Windows directly ([#6761](https://github.com/astral-sh/uv/pull/6761))
- Hint at `--no-workspace` in `uv init` failures ([#6815](https://github.com/astral-sh/uv/pull/6815))
- Update to last PyPy releases ([#6784](https://github.com/astral-sh/uv/pull/6784))

### Bug fixes

- Avoid deadlocks when multiple uv processes lock resources ([#6790](https://github.com/astral-sh/uv/pull/6790))
- Expand tildes when matching against `PATH` ([#6829](https://github.com/astral-sh/uv/pull/6829))
- Fix `uv init --no-project` alias ([#6837](https://github.com/astral-sh/uv/pull/6837))
- Ignore pre-release segments when discovering via `requires-python` ([#6813](https://github.com/astral-sh/uv/pull/6813))
- Support inline optional tables in `uv add` and `uv remove` ([#6787](https://github.com/astral-sh/uv/pull/6787))
- Update default `hello.py` to pass `ruff format` ([#6811](https://github.com/astral-sh/uv/pull/6811))
- Avoid stripping root for user path display ([#6865](https://github.com/astral-sh/uv/pull/6865))
- Error when user-provided environments are disjoint with Python ([#6841](https://github.com/astral-sh/uv/pull/6841))
- Retain alphabetical sorting for `pyproject.toml` in `uv add` operations ([#6388](https://github.com/astral-sh/uv/pull/6388))))

## 0.4.0

This release adds first-class support for Python projects that are not designed as Python packages (e.g., web applications, data science projects, etc.).

In doing so, it includes some breaking changes around uv's handling of projects. Previously, uv required that all projects could be built into distributable Python packages, and installed them into the virtual environment. Projects created by `uv init` always included a `[build-system]` definition and existing projects that did not define a `[build-system]` would use the legacy setuptools build backend by default.

Most users are not developing libraries that need to be packaged and published to PyPI. Instead, they're building applications using web frameworks, or running collections of Python scripts in the project's root directory. In these cases, requiring a `[build-system]` was confusing and error-prone. In this release, uv changes the default behavior to orient around these common use cases.

In summary, the major changes are:

- uv no longer attempts to package and install projects that do not define a `[build-system]`.
  - While the project itself will not be installed into the virtual environment, its dependencies will still be included.
  - The previous behavior can be recovered by setting `package = true` in the `[tool.uv]` section of your `pyproject.toml`.
- `uv init` no longer creates a `src/` directory or defines a `[build-system]` by default.
  - The previous behavior can be recovered with `uv init --lib` or `uv init --app --package`.
- uv allows and recommends including `[project]` definitions in virtual workspace roots.
  - Previously, the uv required the `[project]` section to be omitted.
- uv allows disabling packaging of projects, even if they define a `[build-system]`, by setting `package = false` in the `[tool.uv]` section of your `pyproject.toml`.

See the latest documentation on [build systems in projects](http://docs.astral.sh/uv/concepts/projects/#build-systems) for more details.

### Enhancements

- Add first-class support for non-packaged projects ([#6585](https://github.com/astral-sh/uv/pull/6585))
- Add `--app` and `--lib` options to `uv init` ([#6689](https://github.com/astral-sh/uv/pull/6689))
- Use `virtual` source label in lockfile for non-packaged dependencies ([#6728](https://github.com/astral-sh/uv/pull/6728))
- Read hash from URL fragment if `--hashes` are omitted ([#6731](https://github.com/astral-sh/uv/pull/6731))
- Support `{package}@{version}` in `uv tool install` ([#6762](https://github.com/astral-sh/uv/pull/6762))
- Publish additional Docker tags without patch version ([#6734](https://github.com/astral-sh/uv/pull/6734))

### Bug fixes

- Accept either strings or structs for hosts ([#6763](https://github.com/astral-sh/uv/pull/6763))
- Avoid including non-excluded members in parent workspaces ([#6735](https://github.com/astral-sh/uv/pull/6735))
- Avoid reading stale `.egg-info` from mutable sources ([#6714](https://github.com/astral-sh/uv/pull/6714))
- Avoid writing invalid PEP 723 scripts on `tool.uv.sources` ([#6706](https://github.com/astral-sh/uv/pull/6706))
- Compare virtual members when invalidating lockfile ([#6754](https://github.com/astral-sh/uv/pull/6754))
- Do not require workspace members to sync with `--frozen` ([#6737](https://github.com/astral-sh/uv/pull/6737))
- Implement deserialization for trusted host ([#6716](https://github.com/astral-sh/uv/pull/6716))
- Avoid showing duplicate paths in `uv python list` ([#6740](https://github.com/astral-sh/uv/pull/6740))
- Raise an error for unclosed script tags in PEP 723 scripts ([#6704](https://github.com/astral-sh/uv/pull/6704))

## 0.3.5

### Enhancements

- Add support for `--allow-insecure-host` (aliased to `--trusted-host`) ([#6591](https://github.com/astral-sh/uv/pull/6591))
- Read requirements from `requires.txt` when available ([#6655](https://github.com/astral-sh/uv/pull/6655))
- Respect `tool.uv.environments` in `pip compile --universal` ([#6663](https://github.com/astral-sh/uv/pull/6663))
- Use relative paths by default in `uv add` ([#6686](https://github.com/astral-sh/uv/pull/6686))
- Improve messages for empty solves and installs ([#6588](https://github.com/astral-sh/uv/pull/6588))

### Bug fixes

- Avoid reusing state across tool upgrades ([#6660](https://github.com/astral-sh/uv/pull/6660))
- Detect musl and error for musl Python builds ([#6643](https://github.com/astral-sh/uv/pull/6643))
- Ignore `send` errors in installer ([#6667](https://github.com/astral-sh/uv/pull/6667))

## 0.3.4

### CLI

- Show `--editable` on the `uv add` CLI ([#6608](https://github.com/astral-sh/uv/pull/6608))
- Add `--refresh` to `tool run` warning for `--with` dependencies ([#6609](https://github.com/astral-sh/uv/pull/6609))

### Bug fixes

- Allow per dependency build isolation for `setup.py`-based projects ([#6517](https://github.com/astral-sh/uv/pull/6517))
- Avoid un-strict syncing by-default for build isolation ([#6606](https://github.com/astral-sh/uv/pull/6606))
- Respect `--no-build-isolation-package` in `uv sync` ([#6605](https://github.com/astral-sh/uv/pull/6605))
- Respect extras and markers on virtual dev dependencies ([#6620](https://github.com/astral-sh/uv/pull/6620))
- Support PEP 723 scripts in GUI files ([#6611](https://github.com/astral-sh/uv/pull/6611))
- Update lockfile after setting minimum bounds in `uv add` ([#6618](https://github.com/astral-sh/uv/pull/6618))
- Use relative paths for `--find-links` and local registries ([#6566](https://github.com/astral-sh/uv/pull/6566))
- Use separate types to represent raw vs. resolver markers ([#6646](https://github.com/astral-sh/uv/pull/6646))
- Parse wheels `WHEEL` and `METADATA` files as email messages ([#6616](https://github.com/astral-sh/uv/pull/6616))
- Support unquoted hrefs in `--find-links` and other HTML sources ([#6622](https://github.com/astral-sh/uv/pull/6622))
- Don't canonicalize paths to user requirements ([#6560](https://github.com/astral-sh/uv/pull/6560))

## 0.3.3

### Enhancements

- Add `uv sync --no-install-project` to skip installation of the project ([#6538](https://github.com/astral-sh/uv/pull/6538))
- Add `uv sync --no-install-workspace` to skip installation of all workspace members ([#6539](https://github.com/astral-sh/uv/pull/6539))
- Add `uv sync --no-install-package` to skip installation of specific packages ([#6540](https://github.com/astral-sh/uv/pull/6540))
- Show previous version in self update message ([#6473](https://github.com/astral-sh/uv/pull/6473))

### CLI

- Add `--no-project` alias for `uv python pin --no-workspace` ([#6514](https://github.com/astral-sh/uv/pull/6514))
- Ignore `.python-version` files in `uv venv` with `--no-config` ([#6513](https://github.com/astral-sh/uv/pull/6513))
- Include virtual environment interpreters in `uv python find` ([#6521](https://github.com/astral-sh/uv/pull/6521))
- Respect `-` as stdin channel for `uv run` ([#6481](https://github.com/astral-sh/uv/pull/6481))
- Revert changes to pyproject.toml when sync fails during `uv add` ([#6526](https://github.com/astral-sh/uv/pull/6526))

### Configuration

- Add `UV_COMPILE_BYTECODE` environment variable ([#6530](https://github.com/astral-sh/uv/pull/6530))

### Bug fixes

- Set `VIRTUAL_ENV` for `uv run` invocations ([#6543](https://github.com/astral-sh/uv/pull/6543))
- Ignore errors in workspace discovery with `--no-project` ([#6554](https://github.com/astral-sh/uv/pull/6554))

## 0.3.2

### Configuration

- Add support for configuring `python-downloads` with `UV_PYTHON_DOWNLOADS` ([#6436](https://github.com/astral-sh/uv/pull/6436))
- Add support for configuring the `python-preference` with `UV_PYTHON_PREFERENCE` ([#6432](https://github.com/astral-sh/uv/pull/6432))
- Deny invalid members in workspace schema ([#6450](https://github.com/astral-sh/uv/pull/6450))

### Bug fixes

- Remove URI type from JSON Schema ([#6449](https://github.com/astral-sh/uv/pull/6449))
- Fix retrieval of credentials for URLs from cache ([#6452](https://github.com/astral-sh/uv/pull/6452))
- Restore `cache` suffix on Windows cache path ([#6482](https://github.com/astral-sh/uv/pull/6482))
- Treat `.pyw` files as scripts in `uv run` on Windows ([#6453](https://github.com/astral-sh/uv/pull/6453))
- Treat invalid extras as `false` in marker evaluation ([#6395](https://github.com/astral-sh/uv/pull/6395))
- Avoid overwriting symlinks in `pip compile` output ([#6487](https://github.com/astral-sh/uv/pull/6487))

## 0.3.1

### Enhancements

- Add `--with-editable` support to `uv run` ([#6262](https://github.com/astral-sh/uv/pull/6262))
- Respect `.python-version` files and `pyproject.toml` in `uv python find` ([#6369](https://github.com/astral-sh/uv/pull/6369))
- Allow manylinux compatibility override via `_manylinux` module ([#6039](https://github.com/astral-sh/uv/pull/6039))

### CLI

- Avoid treating `uv add -r` as `--raw-sources` ([#6287](https://github.com/astral-sh/uv/pull/6287))

### Bug fixes

- Always invoke found interpreter when `uv run python` is used ([#6363](https://github.com/astral-sh/uv/pull/6363))
- Avoid adding extra newline for script with non-empty prelude ([#6366](https://github.com/astral-sh/uv/pull/6366))
- Fix metadata cache instability for lockfile ([#6332](https://github.com/astral-sh/uv/pull/6332))
- Handle Ctrl-C properly in `uvx` invocations ([#6346](https://github.com/astral-sh/uv/pull/6346))
- Ignore workspace discovery errors with `--no-workspace` ([#6328](https://github.com/astral-sh/uv/pull/6328))
- Invalidate `uv.lock` when virtual `dev-dependencies` change ([#6291](https://github.com/astral-sh/uv/pull/6291))
- Make cache robust to removed archives ([#6284](https://github.com/astral-sh/uv/pull/6284))
- Preserve Git username for SSH dependencies ([#6335](https://github.com/astral-sh/uv/pull/6335))
- Respect `--no-build-isolation` in `uv add` ([#6368](https://github.com/astral-sh/uv/pull/6368))
- Respect `.python-version` files in `uv run` outside projects ([#6361](https://github.com/astral-sh/uv/pull/6361))
- Use `sys_executable` for `uv run` invocations ([#6354](https://github.com/astral-sh/uv/pull/6354))
- Use atomic write for `pip compile` output ([#6274](https://github.com/astral-sh/uv/pull/6274))
- Use consistent logic for deserializing short revisions ([#6341](https://github.com/astral-sh/uv/pull/6341))
