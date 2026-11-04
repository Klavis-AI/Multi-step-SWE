# uv release notes, step 15: releases in the span `0.12.0 -> 0.12.17` (upstream tag `0.12.17`)

Source: astral-sh/uv at tag `0.12.17`, file(s) `CHANGELOG.md`, `changelogs/0.1.x.md`, `changelogs/0.10.x.md`, `changelogs/0.11.x.md`, `changelogs/0.2.x.md`, `changelogs/0.3.x.md`, `changelogs/0.4.x.md`, `changelogs/0.5.x.md`, `changelogs/0.6.x.md`, `changelogs/0.7.x.md`, `changelogs/0.8.x.md`, `changelogs/0.9.x.md`. 17 release entries: 0.12.17, 0.12.16, 0.12.15, 0.12.14, 0.12.13, 0.12.12, 0.12.11, 0.12.10, 0.12.9, 0.12.8, 0.12.7, 0.12.6, 0.12.5, 0.12.4, 0.12.3, 0.12.2, 0.12.1. Licence: MIT OR Apache-2.0 (astral-sh/uv; see SPECS/licences/uv-LICENSE-MIT and SPECS/licences/uv-LICENSE-APACHE).

Kept sections: Breaking changes, Enhancements, Bug fixes, CLI, Configuration, Error messages, Python, Python versions, Preview, Stabilizations, Deprecations, Compatibility, Security, plus each release's preamble. Dropped: Documentation, Performance, Release, Rust API, Rust libraries, Other changes, Distributions. Kept text is verbatim.

## 0.12.17

Released on 2026-09-18.

### Enhancements

- Reject unsupported Git archive paths in lockfiles with a clear error instead of panicking during frozen exports ([#21780](https://github.com/astral-sh/uv/pull/21780))

### Preview features

- Set minimum glibc and musl versions that universal resolutions must support with `minimum-libc-version` ([#21651](https://github.com/astral-sh/uv/pull/21651))
- Reject `pylock.toml` files whose wheel filenames do not match their declared package names or versions ([#20746](https://github.com/astral-sh/uv/pull/20746))
- Keep `uv workspace metadata` read-only unless `--sync` is provided ([#21821](https://github.com/astral-sh/uv/pull/21821))
- Apply `uv check` lock modes when retrieving workspace metadata ([#21821](https://github.com/astral-sh/uv/pull/21821))

### Bug fixes

- Prevent `required-environments` from selecting package versions whose wheels require a newer macOS version than the configured Darwin baseline ([#21825](https://github.com/astral-sh/uv/pull/21825))

## 0.12.16

Released on 2026-09-17.

### Python

- Add Pyodide 314.0.7, 0.29.5, and 0.27.8 ([#21741](https://github.com/astral-sh/uv/pull/21741))

### Enhancements

- Verify downloaded wheels and source distributions against hashes supplied by package indexes ([#21562](https://github.com/astral-sh/uv/pull/21562))
- Allow `build-constraint-dependencies` entries to include hashes for verifying downloaded build dependencies ([#21467](https://github.com/astral-sh/uv/pull/21467))
- Honor Darwin `platform_release` markers in `required-environments` using macOS wheel deployment targets ([#21766](https://github.com/astral-sh/uv/pull/21766))
- Reject unsupported Git URL schemes while parsing lockfiles instead of panicking during frozen exports ([#21779](https://github.com/astral-sh/uv/pull/21779))

### Preview features

- Support `lock-without-metadata` across all dependency types while retaining `package.metadata` for remote URL dependencies to enable offline validation ([#21163](https://github.com/astral-sh/uv/pull/21163))
- Honor configured and command-line index settings, including credentials, in `uv upgrade` ([#21776](https://github.com/astral-sh/uv/pull/21776))
- Allow `uv check` to run in projects that are not managed by uv and outside workspaces ([#21777](https://github.com/astral-sh/uv/pull/21777))
- Respect `--python` and `UV_PYTHON` when selecting the Python version for `uv check` ([#21744](https://github.com/astral-sh/uv/pull/21744))

### Bug fixes

- Redact Azure shared access signatures from displayed and logged URLs ([#21755](https://github.com/astral-sh/uv/pull/21755))
- Check archive sizes from `pylock.toml` before reusing cached distributions ([#21609](https://github.com/astral-sh/uv/pull/21609))
- Keep user-authored local dependency paths relative in lockfiles when backend metadata reports absolute paths ([#20631](https://github.com/astral-sh/uv/pull/20631))
- Use the bundled `uv_build` backend only when its version matches active version pins ([#21742](https://github.com/astral-sh/uv/pull/21742))
- Handle malformed index URLs without panicking when credentials are configured ([#21784](https://github.com/astral-sh/uv/pull/21784))
- Report a configuration error instead of panicking for proxy URLs without a host ([#21781](https://github.com/astral-sh/uv/pull/21781))
- Return a credential-redacted error instead of panicking when a URL cannot be converted to a path ([#21783](https://github.com/astral-sh/uv/pull/21783))

## 0.12.15

Released on 2026-09-15.

### Bug fixes

- Fix regressions in `0.12.14` when installing to symlinked destinations or using `uv pip install --target .` ([#21699](https://github.com/astral-sh/uv/pull/21699))

## 0.12.14

Released on 2026-09-15.

### Enhancements

- Resume interrupted downloads with HTTP Range requests when supported ([#21570](https://github.com/astral-sh/uv/pull/21570))
- Use a consistent format for error rendering ([#17110](https://github.com/astral-sh/uv/pull/17110))
- Render error and warning causes with compact `cause:` labels ([#21599](https://github.com/astral-sh/uv/pull/21599), [#21603](https://github.com/astral-sh/uv/pull/21603))
- Show underlying causes and hints in user warnings ([#21565](https://github.com/astral-sh/uv/pull/21565))
- Show resolver hints for failed `uv tool upgrade` operations ([#21566](https://github.com/astral-sh/uv/pull/21566))

### Preview features

- Export multiple dependency selections from a shared lockfile in one `uv export --batch` invocation with the `batch-export` preview feature ([#21618](https://github.com/astral-sh/uv/pull/21618))

### Bug fixes

- Select releases that satisfy `required-environments` within each resolver fork instead of combining incompatible wheel coverage across forks ([#21672](https://github.com/astral-sh/uv/pull/21672))
- Install packages with paths longer than `MAX_PATH` on Windows systems without long-path support enabled ([#21625](https://github.com/astral-sh/uv/pull/21625))
- Prevent `uv python install` from overwriting valid unmanaged Python symlinks with relative targets on Unix ([#21639](https://github.com/astral-sh/uv/pull/21639))
- Redact credentials and signatures from missing-path-segment URL errors ([#21616](https://github.com/astral-sh/uv/pull/21616))
- Avoid exceeding the configured retry budget when cached HTTP responses fail revalidation ([#21640](https://github.com/astral-sh/uv/pull/21640))
- Prefer `bin/python` over `bin/python3` when discovering interpreters in Unix environments ([#21559](https://github.com/astral-sh/uv/pull/21559))
- Classify package-operation exit codes by their underlying cause: return `1` for expected failures and `2` for recognized operational and internal failures ([#17110](https://github.com/astral-sh/uv/pull/17110))
- Suppress managed-Python fallback warnings under `--quiet` ([#21565](https://github.com/astral-sh/uv/pull/21565))
- Keep failed `uv tool upgrade` errors visible with `-q` while suppressing them with `-qq` ([#21566](https://github.com/astral-sh/uv/pull/21566))

## 0.12.13

Released on 2026-09-10.

### Python

- Add GraalPy 3.13.0 ([#21431](https://github.com/astral-sh/uv/pull/21431))

### Enhancements

- Verify hashes when downloading PEP 658 metadata sidecars ([#21563](https://github.com/astral-sh/uv/pull/21563))

### Preview features

- Respect `ty` exclusions when `uv check` automatically selects members of a virtual workspace ([#21555](https://github.com/astral-sh/uv/pull/21555))

### Bug fixes

- Edit Windows entry-point launcher resources in memory to support Nano Server and reduce antivirus contention ([#18713](https://github.com/astral-sh/uv/pull/18713))
- Prefer `core-metadata` over legacy aliases in JSON index responses ([#21563](https://github.com/astral-sh/uv/pull/21563))

## 0.12.12

Released on 2026-09-09.

The executables in our macOS and Windows release archives and `uv` and `uv_build` wheels are now code-signed. macOS executables are signed with an Apple Developer ID certificate and notarized by Apple. Windows executables have timestamped Authenticode signatures from Azure Artifact Signing. This enables verification of the release publisher and binary integrity, supports publisher-based allowlisting, and should reduce security warnings and antivirus false positives.

### Bug fixes

- Exclude distributions uploaded after the `exclude-newer` cutoff from lockfiles and generated requirement hashes ([#21539](https://github.com/astral-sh/uv/pull/21539))

## 0.12.11

Released on 2026-09-08.

### Preview features

- Generate missing artifact hashes when exporting `pylock.toml` files to ensure they conform to PEP 751 ([#20146](https://github.com/astral-sh/uv/pull/20146))
- Warn when `pylock.toml` artifact hash tables are empty, which will be rejected in a future uv release ([#21462](https://github.com/astral-sh/uv/pull/21462))

### Bug fixes

- Verify source archives against hashes recorded in `uv.lock` before reading their metadata or running their build backends ([#21223](https://github.com/astral-sh/uv/pull/21223))
- Verify supplied hashes for registry requirements pinned with `===` under both `--verify-hashes` and `--require-hashes` ([#21543](https://github.com/astral-sh/uv/pull/21543))
- Apply hashes from public-version pins to matching local versions when no exact local-version hash is provided ([#21544](https://github.com/astral-sh/uv/pull/21544))
- Support PowerShell virtual environment activation from UNC paths, including WSL paths ([#19159](https://github.com/astral-sh/uv/pull/19159))
- Trim surrounding whitespace from entries in `.python-version` and `.python-versions` files ([#21529](https://github.com/astral-sh/uv/pull/21529))
- Suppress `VIRTUAL_ENV` mismatch warnings for `uv add --no-sync`, `uv remove --no-sync`, and `uv add --frozen` ([#21496](https://github.com/astral-sh/uv/pull/21496))
- Warn and continue when `uv python list` cannot query an interpreter ([#21498](https://github.com/astral-sh/uv/pull/21498))

## 0.12.10

Released on 2026-09-04.

### Enhancements

- Attempt to revoke short-lived PyPI trusted-publishing tokens after `uv publish` completes, including when publishing fails ([#21423](https://github.com/astral-sh/uv/pull/21423))

### Preview features

- Omit `exclude-newer-package` settings for packages outside the resolution from `uv.lock` with the `missing-exclude-newer-package-lock` preview feature ([#21455](https://github.com/astral-sh/uv/pull/21455))
- Show terminal dependency cycles in `uv tree --invert` output ([#21404](https://github.com/astral-sh/uv/pull/21404))

### Bug fixes

- Prevent `--locked` from failing when `exclude-newer-package` settings differ only for packages outside the resolution ([#21454](https://github.com/astral-sh/uv/pull/21454))
- Allow `uv lock --check` to reuse a lockfile when an absolute `exclude-newer` cutoff is moved later ([#19571](https://github.com/astral-sh/uv/pull/19571))
- Allow `uv lock --check` to reuse a lockfile when a package-specific `exclude-newer` cutoff is disabled ([#21450](https://github.com/astral-sh/uv/pull/21450))
- Require an explicit `--name` when `uv init` would infer a project name reserved for a Python interpreter ([#21395](https://github.com/astral-sh/uv/pull/21395))
- Write package-specific `exclude-newer` cutoffs to `uv.lock` in a deterministic order ([#21453](https://github.com/astral-sh/uv/pull/21453))

## 0.12.9

Released on 2026-09-01.

### Python

- Add CPython 3.15.0rc2 ([#21413](https://github.com/astral-sh/uv/pull/21413), [#21415](https://github.com/astral-sh/uv/pull/21415))

### Enhancements

- Add `--no-locked` and `--no-frozen` to disable lock modes enabled by `UV_LOCKED` and `UV_FROZEN` for a single invocation ([#21408](https://github.com/astral-sh/uv/pull/21408))
- Report the exact command-line lock-mode flag in warnings and errors ([#21402](https://github.com/astral-sh/uv/pull/21402))

### Bug fixes

- Update `async_http_range_reader` to 0.11.1 to address a potential memory-safety issue when reading metadata ranges from untrusted wheels ([#21401](https://github.com/astral-sh/uv/pull/21401))
- Remove sensitive headers when redirects cross authentication realms, including same-host redirects that change URL schemes ([#21382](https://github.com/astral-sh/uv/pull/21382))
- Redact secrets in signed URLs from retry diagnostics, including nested request errors ([#21381](https://github.com/astral-sh/uv/pull/21381))
- Give `--locked`, `--frozen`, `--check`, and `--check-exists` precedence over conflicting `UV_LOCKED` and `UV_FROZEN` values ([#21396](https://github.com/astral-sh/uv/pull/21396))
- Prevent concurrent uv processes from redundantly extracting the same local or source-built wheel ([#21400](https://github.com/astral-sh/uv/pull/21400))

## 0.12.8

Released on 2026-08-31.

### Enhancements

- Warn about invalid tool directories and continue upgrading valid tools with `uv tool upgrade --all` ([#21368](https://github.com/astral-sh/uv/pull/21368))

### Preview features

- Deduplicate identical files within and across cached wheels with the `content-addressed-cache` preview feature ([#21327](https://github.com/astral-sh/uv/pull/21327))
- Reduce allocations while extracting content-addressed wheels by reusing the hashing buffer across files ([#21340](https://github.com/astral-sh/uv/pull/21340))
- Speed up content-addressed cache cleanup on macOS by reading hard-link counts in bulk ([#21344](https://github.com/astral-sh/uv/pull/21344))

### Bug fixes

- Do not trust hashes from direct URLs discovered only in wheel metadata when installing with `--require-hashes` ([#21348](https://github.com/astral-sh/uv/pull/21348))
- Use a compatible Azure Storage API version for anonymous and authenticated requests, allowing credential retries when public access is disabled ([#21366](https://github.com/astral-sh/uv/pull/21366))
- Redact Azure shared access signature (`sig`) query parameters from displayed URLs ([#21360](https://github.com/astral-sh/uv/pull/21360))
- Treat projects below one-level workspace member globs as standalone instead of aborting workspace discovery ([#21341](https://github.com/astral-sh/uv/pull/21341))

## 0.12.7

Released on 2026-08-27.

### Python

- Replace managed Python installations when upgrading to a newer build of the same version ([#21323](https://github.com/astral-sh/uv/pull/21323))

### Enhancements

- Support Linux `s390x`, `ppc64le`, and `loongarch64` targets for cross-platform dependency resolution ([#21313](https://github.com/astral-sh/uv/pull/21313))
- Retry downloads with configured credentials when Azure Storage denies anonymous access to an endpoint configured via `UV_AZURE_ENDPOINT_URL` ([#21318](https://github.com/astral-sh/uv/pull/21318))

### Preview features

- Use content-based directory hashes to deduplicate extracted wheels in the cache with the `content-addressed-cache` preview feature ([#19693](https://github.com/astral-sh/uv/pull/19693))

### Bug fixes

- Reject source archives with hash mismatches before persisting their extracted contents to the cache ([#21248](https://github.com/astral-sh/uv/pull/21248))

## 0.12.6

Released on 2026-08-25.

### Python

- Update CPython to use OpenSSL 3.5.8 and libffi 3.4.8 [#21295](https://github.com/astral-sh/uv/pull/21295))
### Enhancements

- Report cache-cleaning space savings from filesystem block allocation and avoid double-counting hard links ([#21261](https://github.com/astral-sh/uv/pull/21261))
- Limit warnings about unbounded `uv_build` requirements to source-distribution builds ([#21078](https://github.com/astral-sh/uv/pull/21078))
- Display byte counts below 1 KiB without a fractional part ([#21237](https://github.com/astral-sh/uv/pull/21237))

### Preview features

- Add `uv workspace metadata --sync --exact` to remove packages outside the selected resolution ([#21117](https://github.com/astral-sh/uv/pull/21117))
- Add the `artifact-hash-filtering` preview feature to make `uv pip compile --generate-hashes` honor `--only-binary` and `--no-binary` ([#21235](https://github.com/astral-sh/uv/pull/21235))
- Respect package-specific `exclude-newer` cutoffs when `uv check` selects its `ty` executable ([#21227](https://github.com/astral-sh/uv/pull/21227))
- Preserve virtual-environment hints from `tar-codec` source-distribution errors when the base interpreter is outside a `bin` directory ([#21146](https://github.com/astral-sh/uv/pull/21146))

### Bug fixes

- Allow explicit `uv build` and non-editable first-party workspace packages when `no-build` is enabled ([#21294](https://github.com/astral-sh/uv/pull/21294))
- Reuse configured index credentials during `uv tool upgrade` when the tool receipt references the same index ([#21275](https://github.com/astral-sh/uv/pull/21275))
- Ensure full 40-character Git commit pins resolve to the requested object instead of a SHA-named branch ([#21224](https://github.com/astral-sh/uv/pull/21224))
- Prevent TLS segfaults in riscv64 musl release binaries ([#21158](https://github.com/astral-sh/uv/pull/21158))
- Preserve dependencies selected by recursive extras when markers mix production and extra conditions ([#21181](https://github.com/astral-sh/uv/pull/21181))
- Preserve version constraints from transitively referenced recursive extras ([#21209](https://github.com/astral-sh/uv/pull/21209))
- Resolve repository-relative Git archive dependencies inside the checkout during the initial `uv sync` ([#21264](https://github.com/astral-sh/uv/pull/21264))
- Return an error instead of panicking when a bearer token cannot be encoded as an HTTP header ([#21282](https://github.com/astral-sh/uv/pull/21282))
- Do not misclassify package URLs ending in `.py` as local script paths ([#21144](https://github.com/astral-sh/uv/pull/21144))
- Use directory creation times consistently across libc implementations for directory `cache-keys` entries ([#21137](https://github.com/astral-sh/uv/pull/21137))
- Promote human-readable sizes to the next unit at rounding boundaries ([#21136](https://github.com/astral-sh/uv/pull/21136))

## 0.12.5

Released on 2026-08-14.

### Python

- Add CPython 3.10.21, 3.11.16, and 3.12.14 ([#21138](https://github.com/astral-sh/uv/pull/21138))
- Prefer newer versions and standard variants when selecting between equally prioritized Python interpreters ([#21134](https://github.com/astral-sh/uv/pull/21134))

### Enhancements

- Simplify errors and hints for invalid editable requirements, and redact credentials in requirement URLs ([#21130](https://github.com/astral-sh/uv/pull/21130))

### Preview features

- Allow `--index` and `--default-index` to select configured package indexes by name with the `index-by-name` preview feature ([#17455](https://github.com/astral-sh/uv/pull/17455))
- Include distribution artifact URLs and hashes in CycloneDX SBOM exports by default ([#21131](https://github.com/astral-sh/uv/pull/21131))
- Fall back to logical file sizes when using `cache-physical-space` on filesystems that do not support physical-space accounting ([#21133](https://github.com/astral-sh/uv/pull/21133))

### Bug fixes

- Resolve relative package index paths in PEP 723 scripts against the script directory ([#21097](https://github.com/astral-sh/uv/pull/21097))

## 0.12.4

Released on 2026-08-13.

### Enhancements

- Prefer post-quantum key exchange and enable opt-in TLS diagnostics ([#21054](https://github.com/astral-sh/uv/pull/21054))
- Accept whitespace before versions in noncompliant wildcard comparisons such as `Requires-Python: >= 3.5.*` ([#21012](https://github.com/astral-sh/uv/pull/21012))
- Report a specific error when a PEP 723 closing tag contains trailing whitespace or other content ([#20944](https://github.com/astral-sh/uv/pull/20944))
- Omit source-span carets from diagnostics for empty PEP 508 requirements ([#21094](https://github.com/astral-sh/uv/pull/21094))

### Preview features

- Add `uv check --no-install-project` and respect `UV_NO_INSTALL_PROJECT` to install dependencies without building or installing the project ([#21085](https://github.com/astral-sh/uv/pull/21085))
- Make the ty subprocess invoked by `uv check` honor uv's color and progress settings, including quiet mode ([#21086](https://github.com/astral-sh/uv/pull/21086))

### Bug fixes

- Use windowed `pythonw.exe` launchers for virtual environments created from managed Python minor-version links ([#19235](https://github.com/astral-sh/uv/pull/19235))
- Allow `uv lock` to proceed when `.venv` is an unusable project environment ([#21068](https://github.com/astral-sh/uv/pull/21068))
- Respect `fork-strategy` when ordering forks created from `environments` or existing lockfile `resolution-markers` ([#21000](https://github.com/astral-sh/uv/pull/21000))
- Preserve consecutive wildcard Python minor-version exclusions such as `!=3.11.*, !=3.12.*` in `uv.lock` ([#21045](https://github.com/astral-sh/uv/pull/21045))
- Preserve inline comments on the final item in dependency arrays when `uv add` updates it ([#21008](https://github.com/astral-sh/uv/pull/21008))
- Recover from stale base-interpreter cache metadata when an existing virtual environment exposes a version mismatch ([#21073](https://github.com/astral-sh/uv/pull/21073))
- Prevent interpreter cache reuse across different `PYTHONEXECUTABLE` and `__PYVENV_LAUNCHER__` overrides ([#21075](https://github.com/astral-sh/uv/pull/21075))
- Show standard styling, usage guidance, and line termination for invalid `uv version --bump` values ([#21076](https://github.com/astral-sh/uv/pull/21076))

## 0.12.3

Released on 2026-08-07.

### Python

- Add CPython 3.13.15 ([#20997](https://github.com/astral-sh/uv/pull/20997))

### Preview features

- Add `--output-format` to select automatic, human-readable, or raw-byte output for `uv cache size` ([#20992](https://github.com/astral-sh/uv/pull/20992))
- Preserve JSON output from `uv workspace metadata --quiet` while suppressing diagnostics ([#20991](https://github.com/astral-sh/uv/pull/20991))
- Reduce memory usage for large workspaces by streaming `uv workspace metadata` JSON output ([#20990](https://github.com/astral-sh/uv/pull/20990))

## 0.12.2

Released on 2026-08-05.

### Python

- Add CPython 3.15.0rc1 ([#20948](https://github.com/astral-sh/uv/pull/20948))
- Add CPython 3.14.7 ([#20971](https://github.com/astral-sh/uv/pull/20971))

### Enhancements

- Ensure diagnostic hints end with a newline to prevent malformed terminal output ([#20959](https://github.com/astral-sh/uv/pull/20959))

### Preview features

- Audit one or all installed tools with `uv tool audit` ([#20921](https://github.com/astral-sh/uv/pull/20921))
- Report physically reclaimed disk space during cache cleanup with the `cache-physical-space` preview feature ([#20925](https://github.com/astral-sh/uv/pull/20925))

### Configuration

- Add `UV_RUN_RLIMIT_NOFILE` to set the open-file limit for commands launched by `uv run` ([#20926](https://github.com/astral-sh/uv/pull/20926))

### Bug fixes

- Preserve compatibility with older uv versions when recording artifact sizes in cached wheels and source distributions ([#20963](https://github.com/astral-sh/uv/pull/20963))
- Avoid including workspace-root default dependency groups when syncing or exporting a selected workspace member unless explicitly requested ([#20930](https://github.com/astral-sh/uv/pull/20930))

## 0.12.1

Released on 2026-07-31.

### Enhancements

- Add package-specific pre-release policies with `--prerelease-package` ([#20837](https://github.com/astral-sh/uv/pull/20837))
- Support local HTML files as flat indexes ([#20802](https://github.com/astral-sh/uv/pull/20802))
- Add Xonsh virtual environment activation scripts (`activate.xsh`) ([#19740](https://github.com/astral-sh/uv/pull/19740))
- Preserve filesystem paths passed to `uv add --index` when updating `pyproject.toml` ([#20817](https://github.com/astral-sh/uv/pull/20817))

### Preview features

- Add automatic fixes to `uv check` with `--fix` ([#20793](https://github.com/astral-sh/uv/pull/20793))
- Avoid rejecting unchanged metadata-free lockfiles when workspace dependencies share direct sources ([#20847](https://github.com/astral-sh/uv/pull/20847))
- Honor direct URL constraints when validating metadata-free lockfiles ([#20796](https://github.com/astral-sh/uv/pull/20796))
- Ignore malformed PEP 723 scripts discovered during project checks ([#20784](https://github.com/astral-sh/uv/pull/20784))
- Use ty's native script exclusion in `uv check` ([#20742](https://github.com/astral-sh/uv/pull/20742))

### Bug fixes

- Flush shell startup file updates before `uv tool update-shell` and `uv python update-shell` exit ([#20842](https://github.com/astral-sh/uv/pull/20842))
- Make workspace-root dependency groups available to commands run from workspace members ([#20840](https://github.com/astral-sh/uv/pull/20840))
- Resolve `--find-links` paths in requirements files relative to the containing file ([#20832](https://github.com/astral-sh/uv/pull/20832))
- Respect configured indexes in `uv tool list --outdated` ([#20770](https://github.com/astral-sh/uv/pull/20770))
