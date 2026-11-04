# uv release notes, step 06: releases in the span `0.4.20 -> 0.5.0` (upstream tag `0.5.0`)

Source: astral-sh/uv at tag `0.5.0`, file(s) `CHANGELOG.md`. 11 release entries: 0.5.0, 0.4.30, 0.4.29, 0.4.28, 0.4.27, 0.4.26, 0.4.25, 0.4.24, 0.4.23, 0.4.22, 0.4.21. Licence: MIT OR Apache-2.0 (astral-sh/uv; see SPECS/licences/uv-LICENSE-MIT and SPECS/licences/uv-LICENSE-APACHE).

Kept sections: Breaking changes, Enhancements, Bug fixes, CLI, Configuration, Error messages, Python, Python versions, Preview, Stabilizations, Deprecations, Compatibility, Security, plus each release's preamble. Dropped: Documentation, Performance, Release, Rust API, Rust libraries, Other changes, Distributions. Kept text is verbatim.

## 0.5.0

Since the launch of Python version, project, and tool management capabilities back in August, we've seen extraordinary adoption of uv. We've been iterating rapidly since then: adding new features, fixing bugs, and improving the user experience. Despite moving quickly, stability and compatibility has been a major focus — we've made thirty releases since our last breaking change. Consequently, we've accumulated various changes that improve correctness and user experience, but could break some workflows. This release contains those changes; many have been marked as breaking out of an abundance of caution. We expect most users to be able to upgrade without making changes.

### Breaking

- **Use base executable to set virtualenv Python path** ([#8481](https://github.com/astral-sh/uv/pull/8481))

  Previously, uv canonicalized the path to the Python executable when setting the Python path in created virtual environments. This behavior had several undesirable effects: it would bypass stabilized version directories (as constructed by Homebrew) and it was not consistent with the Python standard library's behavior. Now, uv uses the `sys._base_executable` path.

- **Use XDG (i.e. `~/.local/bin`) instead of the Cargo home directory in the installer** ([#8420](https://github.com/astral-sh/uv/pull/8420))

  Previously, uv's installer used `$CARGO_HOME` or `~/.cargo/bin` for its target install directory. It's been a longstanding complaint that uv uses this directory, as there's no relationship to Cargo. Now, uv will be installed into `$XDG_BIN_HOME`, `$XDG_DATA_HOME/../bin`, or `~/.local/bin` (in that order). Note that `$UV_INSTALL_DIR` can always be used to override the target directory.

- **Discover and respect `.python-version` files in parent directories** ([#6370](https://github.com/astral-sh/uv/pull/6370))

  Previously, uv only read `.python-version` files from the working directory. Now, uv will check parent directories for `.python-version` files; however uv will not search for `.python-version` files beyond project boundaries. The new behavior is better aligned with that of `pyenv` and Rye.

- **Error when disallowed settings are defined in `uv.toml`** ([#8550](https://github.com/astral-sh/uv/pull/8550))

  Some settings can only be defined in the `pyproject.toml`. Previously, uv would ignore these settings when present in the `uv.toml`. Now, uv will error to avoid confusion about why the settings are not respeced.

- **Implement PEP 440-compliant local version semantics** ([#8797](https://github.com/astral-sh/uv/pull/8797))

  Previously, uv's implementation of local versions (e.g. `2.0+cpu`) was not compliant with the specification due to the technical complexity of implementing the local version semantics in the PubGrub algorithm. Thanks to the work of @ericmarkmartin, uv now has a spec-compliant implementation. Namely, uv will now allow a request for `torch==2.1.0` to install `torch@2.1.0+cpu` regardless of whether `torch@2.1.0` (without a local tag) actually exists.

- **Treat the base Conda environment as a system environment** ([#7691](https://github.com/astral-sh/uv/pull/7691))

  Previously, uv would not distinguish between the base and other Conda environments. Now, uv uses `CONDA_DEFAULT_ENV` and the names `base` and `default` to determine if an environment active via `CONDA_PREFIX` is the base environment. If the base environment is active, the `--system` flag must be used to mutate it.

- **Do not allow pre-releases when the `!=` operator is used** ([#7974](https://github.com/astral-sh/uv/pull/7974))

  Previously, uv would use the presence of a pre-release specifier in a version specifier as an opt-in to allow pre-release versions during resolution. The new behavior does not allow pre-releases when an inequals operator is used, e.g., `!= 2.0a1`.

- **Prefer `USERPROFILE` over `FOLDERID_Profile` when selecting a home directory on Windows** ([#8048](https://github.com/astral-sh/uv/pull/8048))

  This change is a side-effect of switching from the `directories` crate to `etcetera` for determining canonical system paths. If `USERPROFILE` is not set, the behavior will be unchanged.

- **Improve interactions between color environment variables and CLI options** ([#8215](https://github.com/astral-sh/uv/pull/8215))

  Previously, uv would respect the `FORCE_COLOR` and `NO_COLOR` environment variables over the `--color` flag. Now, when the `--color` flag is explicitly provided, uv will respect it over the environment variables.

- **Make `allow-insecure-host` a global option** ([#8476](https://github.com/astral-sh/uv/pull/8476))

  Previously, this option was only available in some parts of uv. Now, `--allow-insecure-host` can be provided to any command. For consistency, the `allow-insecure-host` setting has been removed from the `[tool.uv.pip]` configuration in favor of `[tool.uv]`.

- **Only write `.python-version` files during `uv init` for workspace members if the version differs** ([#8897](https://github.com/astral-sh/uv/pull/8897))

  Previously, uv would create a `.python-version` file for workspace members during `uv init`. Now, uv will only do so if the version differs from the `.python-version` file in the workspace root since uv will respect `.python-version` files in parent directories.

### Enhancements

- Add `uv tree --outdated` ([#8893](https://github.com/astral-sh/uv/pull/8893))
- Add armv8l alias for armv7l to support arm 32-bit compatibility mode ([#8881](https://github.com/astral-sh/uv/pull/8881))
- Add support for `pip list --outdated` ([#8872](https://github.com/astral-sh/uv/pull/8872))
- Allow semicolons directly after direct URLs ([#8836](https://github.com/astral-sh/uv/pull/8836))
- Enable support for arbitrary git transports ([#8769](https://github.com/astral-sh/uv/pull/8769))
- Improve Python discovery source messages ([#8890](https://github.com/astral-sh/uv/pull/8890))
- Show dedicated error for trailing `;` on URL and path requirements ([#8835](https://github.com/astral-sh/uv/pull/8835))
- Add progress bar for `uv cache clean` ([#8857](https://github.com/astral-sh/uv/pull/8857))
- Warn on failure to query system configuration file ([#8829](https://github.com/astral-sh/uv/pull/8829))

### Preview features

- Add support for building basic source distributions with the experimental uv build backend ([#8886](https://github.com/astral-sh/uv/pull/8886))

### Bug fixes

- Respect dynamic version updates in `uv lock` ([#8867](https://github.com/astral-sh/uv/pull/8867))
- Respect fork markers in `--resolution-mode=lowest-direct` ([#8839](https://github.com/astral-sh/uv/pull/8839))

## 0.4.30

### Enhancements

- Add support for `.env` and custom env files in `uv run` ([#8811](https://github.com/astral-sh/uv/pull/8811))
- Add support for `--all-packages` in `uv run`, `uv sync`, and `uv export` ([#8742](https://github.com/astral-sh/uv/pull/8742), [#8741](https://github.com/astral-sh/uv/pull/8741), [#8739](https://github.com/astral-sh/uv/pull/8739))
- Allow use of `--frozen` with `--all-packages` in `uv sync` and `uv export` ([#8760](https://github.com/astral-sh/uv/pull/8760))
- Show full error chain on tool upgrade failures ([#8753](https://github.com/astral-sh/uv/pull/8753))
- Add `--check-url` to `uv publish` to check for existing distributions during upload ([#8531](https://github.com/astral-sh/uv/pull/8531))
- Suggest using `--check-url` when `--skip-existing` is used ([#8803](https://github.com/astral-sh/uv/pull/8803))

### Bug fixes

- Allow incompatible `requires-python` for source distributions with static metadata ([#8768](https://github.com/astral-sh/uv/pull/8768))
- Allow managed downloads with `--python-preference system` ([#8808](https://github.com/astral-sh/uv/pull/8808))
- Avoid error for `--group` defined in non-root workspace member ([#8734](https://github.com/astral-sh/uv/pull/8734))
- Avoid showing dependency group annotations on workspace members in tree ([#8730](https://github.com/astral-sh/uv/pull/8730))
- Do not error when the Python bin directory is missing on `uv python uninstall` ([#8725](https://github.com/astral-sh/uv/pull/8725))
- Include member groups when locking workspace ([#8736](https://github.com/astral-sh/uv/pull/8736))
- Fix bug where `python_version < '0'` could appear in a final resolution ([#8759](https://github.com/astral-sh/uv/pull/8759))
- Sanitize filenames during zip extraction ([#8732](https://github.com/astral-sh/uv/pull/8732))
- Switch to RFC 9110 compatible format for exclude newer requests ([#8752](https://github.com/astral-sh/uv/pull/8752))

### Preview features

- Add support for installing versioned Python executables on Windows ([#8663](https://github.com/astral-sh/uv/pull/8663))
- Improve interactions with existing Python executables during install ([#8733](https://github.com/astral-sh/uv/pull/8733))

## 0.4.29

### Enhancements

- Sort errors during display in `uv python install` ([#8684](https://github.com/astral-sh/uv/pull/8684))
- Update resolver to use disjointness checks instead of marker equality ([#8661](https://github.com/astral-sh/uv/pull/8661))
- Add `riscv64` to supported Python platform tags ([#8660](https://github.com/astral-sh/uv/pull/8660))

### Bug fixes

- Fix hard and soft float libc detection for managed Python distributions on ARM ([#8498](https://github.com/astral-sh/uv/pull/8498))
- Handle cycles in `uv pip tree` ([#8689](https://github.com/astral-sh/uv/pull/8689))
- Respect dependency group markers in `uv export` ([#8659](https://github.com/astral-sh/uv/pull/8659))
- Support transitive dependencies in Git workspaces ([#8665](https://github.com/astral-sh/uv/pull/8665))
- Use portable paths for subdirectories in lock URLs ([#8707](https://github.com/astral-sh/uv/pull/8707))
- Update `uv init --virtual` to imply `--no-package` ([#8595](https://github.com/astral-sh/uv/pull/8595))

### Preview

- Install versioned Python executables into the bin directory during `uv python install` (Unix only) ([#8458](https://github.com/astral-sh/uv/pull/8458))

## 0.4.28

### Enhancements

- Add support for requesting free-threaded builds via `+freethreaded` ([#8645](https://github.com/astral-sh/uv/pull/8645))
- Improve trusted publishing error messages ([#8633](https://github.com/astral-sh/uv/pull/8633))
- Remove unneeded `return` from Maturin project template ([#8604](https://github.com/astral-sh/uv/pull/8604))
- Skip Python interpreter discovery for `uv export` ([#8638](https://github.com/astral-sh/uv/pull/8638))
- Hint about missing trusted publishing permission ([#8632](https://github.com/astral-sh/uv/pull/8632))

### Configuration

- Add environment variable to disable progress output ([#8600](https://github.com/astral-sh/uv/pull/8600))

### Bug fixes

- Fork when minimum Python version increases ([#8628](https://github.com/astral-sh/uv/pull/8628))
- Ignore empty groups when validating lock ([#8598](https://github.com/astral-sh/uv/pull/8598))
- Remove duplicate word in error message ([#8589](https://github.com/astral-sh/uv/pull/8589))
- Support cyclic dependencies in `uv tree` ([#8564](https://github.com/astral-sh/uv/pull/8564))
- Update `uv init` to imply `--package` when using `--build-backend` ([#8593](https://github.com/astral-sh/uv/pull/8593))
- Restore use of `dev-dependencies` and `requires-dev` for lockfile compatibility ([#8599](https://github.com/astral-sh/uv/pull/8599))

## 0.4.27

This release includes support for the `[dependency-groups]` table as recently standardized in [PEP 735](https://peps.python.org/pep-0735/). The table allows for declaration of optional dependency groups that are not published as part of the package metadata, unlike `[project.optional-dependencies]`. There are new `--group`, `--only-group`, and `--no-group` options throughout the uv interface.

Previously, uv used a single `tool.uv.dev-dependencies` list for declaration of development dependencies. Now, uv supports declaring development dependencies in a standardized format and allows splitting development dependencies into multiple groups.

For compatibility, and to simplify usage for people that do not need multiple groups, uv special-cases the group named `dev`. The `dev` group is equivalent to `tool.uv.dev-dependencies`. The contents of `tool.uv.dev-dependencies` will merged into the `dev` group in uv's resolver. The `--dev`, `--only-dev`, and `--no-dev` flags remain as aliases for the corresponding `--group` options. Support for `tool.uv.dev-dependencies` remains in this release, but will display warnings in a future release.

uv syncs the `dev` group by default — this matches the exististing behavior for `tool.uv.dev-dependencies`. The default groups can be changed with the `tool.uv.default-groups` setting.

Thank you to Stephen Rosen who authored PEP 735.

### Enhancements

- Support for PEP 735 ([#8272](https://github.com/astral-sh/uv/pull/8272))
- Add support for `--dry-run` mode in `uv lock` ([#7783](https://github.com/astral-sh/uv/pull/7783))
- Don't allow non-string email in authors ([#8520](https://github.com/astral-sh/uv/pull/8520))
- Enforce lockfile schema versions ([#8509](https://github.com/astral-sh/uv/pull/8509))

### Bug fixes

- Always attach URL to network errors ([#8444](https://github.com/astral-sh/uv/pull/8444))
- Fix dangling non-platform dependencies in `uv tree` ([#8532](https://github.com/astral-sh/uv/pull/8532))
- Prefer `lto` over `debug` free-threaded managed Python builds ([#8515](https://github.com/astral-sh/uv/pull/8515))

## 0.4.26

### Enhancements

- Allow static dependency metadata entries for direct URL requirements ([#7846](https://github.com/astral-sh/uv/pull/7846))
- Use reinstall report formatting for `uv python install --reinstall` ([#8487](https://github.com/astral-sh/uv/pull/8487))
- Add support for system-level `uv.toml` configuration ([#7851](https://github.com/astral-sh/uv/pull/7851))

### Bug fixes

- Apply `requires-python` narrowing with upper bounds ([#8403](https://github.com/astral-sh/uv/pull/8403))
- Avoid rewriting `[[tool.uv.index]]` entries when credentials are provided ([#8502](https://github.com/astral-sh/uv/pull/8502))
- Fix `uv add` comment handling for empty arrays ([#8504](https://github.com/astral-sh/uv/pull/8504))
- Replace dashes with underscores in index credential variables ([#8452](https://github.com/astral-sh/uv/pull/8452))
- Respect `--allow-insecure-host` in `uv publish` ([#8440](https://github.com/astral-sh/uv/pull/8440))
- Allow arbitrary `--package` includes in `uv tree` ([#8507](https://github.com/astral-sh/uv/pull/8507))
- Remove existing Python install after successful download in `uv python install` ([#8485](https://github.com/astral-sh/uv/pull/8485))

### Error messages

- Improve error message for cache info serialization ([#8500](https://github.com/astral-sh/uv/pull/8500))
- Suggest `--from` command when executable is available for `uvx` ([#8473](https://github.com/astral-sh/uv/pull/8473))
- Support `--with-editable` in `uv tool install` ([#8472](https://github.com/astral-sh/uv/pull/8472))

## 0.4.25

### Enhancements

- Add support for `uv pip show --files` ([#8369](https://github.com/astral-sh/uv/pull/8369))
- Don't prefetch unreachable packages ([#8246](https://github.com/astral-sh/uv/pull/8246))
- Remove `tool.uv.sources` table if it is empty ([#8365](https://github.com/astral-sh/uv/pull/8365))
- Modify cache versioning to support backwards compatibility ([#8386](https://github.com/astral-sh/uv/pull/8386))

### Configuration

- Add support for `UV_FROZEN` and `UV_LOCKED` ([#8340](https://github.com/astral-sh/uv/pull/8340))

### Bug fixes

- Allow dashes and underscores in custom index names ([#8339](https://github.com/astral-sh/uv/pull/8339))
- Avoid panic when Git dependencies are included in fork markers ([#8388](https://github.com/astral-sh/uv/pull/8388))
- Check existing source by normalized name before `uv add` and `uv remove` ([#8359](https://github.com/astral-sh/uv/pull/8359))
- Fix bug where username from authentication cache could be ignored ([#8345](https://github.com/astral-sh/uv/pull/8345))
- Fix to respect comments positioning in pyproject.toml on change ([#8384](https://github.com/astral-sh/uv/pull/8384))
- Redact index sources in `uv.lock` ([#8333](https://github.com/astral-sh/uv/pull/8333))
- Use correct indentation when project table contains open bracket comment ([#8387](https://github.com/astral-sh/uv/pull/8387))
- Only remove a source from `[tool.uv.sources]` if it is no long being referenced ([#8366](https://github.com/astral-sh/uv/pull/8366))
- Modify `uv pip list` and `uv tree` to print to stdout regardless of `--quiet` flag ([#8392](https://github.com/astral-sh/uv/pull/8392))

### Error messages

- Improve help message for missing `self update` invocations ([#8337](https://github.com/astral-sh/uv/pull/8337))
- Log `.netrc` parsing errors ([#8364](https://github.com/astral-sh/uv/pull/8364))
- Remove trailing newlines in error messages ([#8322](https://github.com/astral-sh/uv/pull/8322))
- Use a dedicated message for incompatible Python versions in wheel ABI tags ([#8363](https://github.com/astral-sh/uv/pull/8363))
- Remove commands available in the top-level from the suggested subcommand error ([#8316](https://github.com/astral-sh/uv/pull/8316))

## 0.4.24

### Bug fixes

- Fix Python executable name in Windows free-threaded Python distributions ([#8310](https://github.com/astral-sh/uv/pull/8310))
- Redact index credentials from lockfile sources ([#8307](https://github.com/astral-sh/uv/pull/8307))
- Respect `UV_INDEX_` rather than `UV_HTTP_BASIC_` as documented ([#8306](https://github.com/astral-sh/uv/pull/8306))
- Improve sources deserialization errors ([#8308](https://github.com/astral-sh/uv/pull/8308))

## 0.4.23

This release introduces a revamped system for defining package indexes, as an alternative to the existing pip-style
`--index-url` and `--extra-index-url` configuration options.

You can now define named indexes in your `pyproject.toml` file using the `[[tool.uv.index]]` table:

```toml
[[tool.uv.index]]
name = "pytorch"
url = "https://download.pytorch.org/whl/cpu"
```

Packages can be pinned to a specific index via `tool.uv.sources`, to ensure that a given package is installed from the
correct index. For example, to ensure that `torch` is *always* installed from the `pytorch` index:

```toml
[tool.uv.sources]
torch = { index = "pytorch" }

[[tool.uv.index]]
name = "pytorch"
url = "https://download.pytorch.org/whl/cpu"
```

Indexes can also be marked as `explicit = true` to prevent packages from being installed from that index
unless explicitly pinned. For example, to ensure that `torch` is installed from the `pytorch` index, but all other
packages are installed from the default index:

```toml
[tool.uv.sources]
torch = { index = "pytorch" }

[[tool.uv.index]]
name = "pytorch"
url = "https://download.pytorch.org/whl/cpu"
explicit = true
```

To define an additional index outside a `pyproject.toml` file, use the `--index` command-line argument
(or the `UV_INDEX` environment variable); to replace the default index (PyPI), use the `--default-index` command-line
argument (or `UV_DEFAULT_INDEX`).

These changes are entirely backwards-compatible with the deprecated `--index-url` and `--extra-index-url` options,
which continue to work as before.

See the [Index](https://docs.astral.sh/uv/configuration/indexes/) documentation for more.

### Enhancements

- Add index URLs when provided via `uv add --index` or `--default-index` ([#7746](https://github.com/astral-sh/uv/pull/7746))
- Add support for named and explicit indexes ([#7481](https://github.com/astral-sh/uv/pull/7481))
- Add templates for popular build backends ([#7857](https://github.com/astral-sh/uv/pull/7857))
- Allow multiple pinned indexes in `tool.uv.sources` ([#7769](https://github.com/astral-sh/uv/pull/7769))
- Allow users to incorporate Git tags into dynamic cache keys ([#8259](https://github.com/astral-sh/uv/pull/8259))
- Pin named indexes in `uv add` ([#7747](https://github.com/astral-sh/uv/pull/7747))
- Respect named `--index` and `--default-index` values in `tool.uv.sources` ([#7910](https://github.com/astral-sh/uv/pull/7910))
- Update to latest PubGrub version ([#8245](https://github.com/astral-sh/uv/pull/8245))
- Enable environment variable authentication for named indexes ([#7741](https://github.com/astral-sh/uv/pull/7741))
- Avoid showing lower-bound warning outside of explicit lock and sync ([#8234](https://github.com/astral-sh/uv/pull/8234))
- Improve logging during lock errors ([#8258](https://github.com/astral-sh/uv/pull/8258))
- Improve styling of `requires-python` warnings ([#8240](https://github.com/astral-sh/uv/pull/8240))
- Show hint in resolution failure on `Forbidden` (`403`) or `Unauthorized` (`401`) ([#8264](https://github.com/astral-sh/uv/pull/8264))
- Update to latest `cargo-dist` version (includes new installer features) ([#8270](https://github.com/astral-sh/uv/pull/8270))
- Warn when patch version in `requires-python` is implicitly `0` ([#7959](https://github.com/astral-sh/uv/pull/7959))
- Add more context on client errors during range requests ([#8285](https://github.com/astral-sh/uv/pull/8285))

### Bug fixes

- Avoid writing duplicate index URLs with `--emit-index-url` ([#8226](https://github.com/astral-sh/uv/pull/8226))
- Fix error leading to out-of-bound panic in `uv-pep508` ([#8282](https://github.com/astral-sh/uv/pull/8282))
- Fix managed distributions of free-threaded Python on Windows ([#8268](https://github.com/astral-sh/uv/pull/8268))
- Fix selection of free-threaded interpreters during default Python discovery ([#8239](https://github.com/astral-sh/uv/pull/8239))
- Ignore sources in build requirements for non-source trees ([#8235](https://github.com/astral-sh/uv/pull/8235))
- Invalid cache when adding lower bound to lockfile ([#8230](https://github.com/astral-sh/uv/pull/8230))
- Respect index priority when storing credentials ([#8256](https://github.com/astral-sh/uv/pull/8256))
- Respect relative paths in `uv build` sources ([#8237](https://github.com/astral-sh/uv/pull/8237))
- Narrow what the pip3.<minor> logic drops from entry points. ([#8273](https://github.com/astral-sh/uv/pull/8273))

## 0.4.22

### Enhancements

- Respect `[tool.uv.sources]` in build requirements ([#7172](https://github.com/astral-sh/uv/pull/7172))

### Preview features

- Add a dedicated `uv publish` error message for missing usernames ([#8045](https://github.com/astral-sh/uv/pull/8045))
- Support interactive input in `uv publish` ([#8158](https://github.com/astral-sh/uv/pull/8158))
- Use raw filenames in `uv publish` ([#8204](https://github.com/astral-sh/uv/pull/8204))

### Bug fixes

- Avoid environment check optimization for `uv pip install --exact` ([#8219](https://github.com/astral-sh/uv/pull/8219))
- Do not use free-threaded interpreters without a free-threaded request ([#8191](https://github.com/astral-sh/uv/pull/8191))
- Don't recommend `--prerelease=allow` during build requirement resolution errors ([#8192](https://github.com/astral-sh/uv/pull/8192))
- Prefer optimized builds for free-threaded Python downloads ([#8196](https://github.com/astral-sh/uv/pull/8196))
- Retain old `python-build-standalone` releases ([#8216](https://github.com/astral-sh/uv/pull/8216))
- Run `uv build` builds in the source distribution bucket ([#8220](https://github.com/astral-sh/uv/pull/8220))

## 0.4.21

### Enhancements

- Add support for managed installations of free-threaded Python ([#8100](https://github.com/astral-sh/uv/pull/8100))
- Add note about `uvx` to `uv tool run` short help ([#7695](https://github.com/astral-sh/uv/pull/7695))
- Enable HTTP/2 requests ([#8049](https://github.com/astral-sh/uv/pull/8049))
- Support `uv tree --no-dev` ([#8109](https://github.com/astral-sh/uv/pull/8109))
- Support PEP 723 metadata with `uv run -` ([#8111](https://github.com/astral-sh/uv/pull/8111))
- Support `pip install --exact` ([#8044](https://github.com/astral-sh/uv/pull/8044))
- Support `uv export --no-header` ([#8096](https://github.com/astral-sh/uv/pull/8096))
- Add Python 3.13 images to Docker publish ([#8105](https://github.com/astral-sh/uv/pull/8105))
- Support remote (`https://`) scripts in `uv run` ([#6375](https://github.com/astral-sh/uv/pull/6375))
- Allow comma value-delimited arguments in `uv run --with` ([#7909](https://github.com/astral-sh/uv/pull/7909))

### Configuration

- Support wildcards in `UV_INSECURE_HOST` ([#8052](https://github.com/astral-sh/uv/pull/8052))

### Bug fixes

- Add prerelease compatibility check to `uv python` CLI ([#8020](https://github.com/astral-sh/uv/pull/8020))
- Avoid deleting a project environment directory if we cannot tell if a `pyvenv.cfg` file exists ([#8012](https://github.com/astral-sh/uv/pull/8012))
- Avoid excluding valid wheels for exact `requires-python` bounds ([#8140](https://github.com/astral-sh/uv/pull/8140))
- Bump `netrc` crate to latest commit ([#8021](https://github.com/astral-sh/uv/pull/8021))
- Fix `uv python pin 3.13t` failure when parsing version for project requires check ([#8056](https://github.com/astral-sh/uv/pull/8056))
- Fix handling of != intersections in `requires-python` ([#7897](https://github.com/astral-sh/uv/pull/7897))
- Remove the newly created tool environment if sync failed ([#8038](https://github.com/astral-sh/uv/pull/8038))
- Respect dynamic extras in `uv lock` and `uv sync` ([#8091](https://github.com/astral-sh/uv/pull/8091))
- Treat resolver failures as fatal in lockfile validation ([#8083](https://github.com/astral-sh/uv/pull/8083))
- Use `git config --get` for author information for improved backwards compatibility ([#8101](https://github.com/astral-sh/uv/pull/8101))
- Use comma-separated values for `UV_FIND_LINKS` ([#8061](https://github.com/astral-sh/uv/pull/8061))
- Use shared resolver state between add and lock to avoid double Git update ([#8146](https://github.com/astral-sh/uv/pull/8146))
- Make `--relocatable` entrypoints robust to symlinking ([#8079](https://github.com/astral-sh/uv/pull/8079))
- Improve compatibility with VSCode PS1 prompt ([#8006](https://github.com/astral-sh/uv/pull/8006))
- Fix "Stream did not contain valid UTF-8" failures in Windows ([#8120](https://github.com/astral-sh/uv/pull/8120))
- Use `--with-requirements` in `uvx` error hint ([#8112](https://github.com/astral-sh/uv/pull/8112))
