# uv release notes, step 03: releases in the span `0.1.24 -> 0.2.0` (upstream tag `0.2.0`)

Source: astral-sh/uv at tag `0.2.0`, file(s) `CHANGELOG.md`. 22 release entries: 0.2.0, 0.1.45, 0.1.44, 0.1.43, 0.1.42, 0.1.41, 0.1.40, 0.1.39, 0.1.38, 0.1.37, 0.1.36, 0.1.35, 0.1.34, 0.1.33, 0.1.32, 0.1.31, 0.1.30, 0.1.29, 0.1.28, 0.1.27, 0.1.26, 0.1.25. Licence: MIT OR Apache-2.0 (astral-sh/uv; see SPECS/licences/uv-LICENSE-MIT and SPECS/licences/uv-LICENSE-APACHE).

Kept sections: Breaking changes, Enhancements, Bug fixes, CLI, Configuration, Error messages, Python, Python versions, Preview, Stabilizations, Deprecations, Compatibility, Security, plus each release's preamble. Dropped: Documentation, Performance, Release, Rust API, Rust libraries, Other changes, Distributions. Kept text is verbatim.

## 0.2.0

Starting with this release, uv will use the **minor** version tag to indicate breaking changes.

### Breaking

In this release, discovery of Python interpreters has changed. These changes should have a limited effect in most
use-cases, however, it has been marked as a breaking change because the interpreter used by uv could change in
some edge cases.

When multiple Python interpreters are installed, uv makes an attempt to find the exact version you requested.
Previously, uv would stop at the first Python interpreter it discovered — if the interpreter did not satisfy
the requested version, uv would fail. Now, uv will query multiple Python interpreters until it finds the
requested version, skipping interpreters that are broken or do not satisfy the request.

Additionally, uv now allows requests for interpreter implementations such as `pypy` and `cpython`. For example,
the request `--python cpython` will ignore a `python` executable that's implemented by `pypy`. These requests may
also include a version, e.g., `--python pypy@3.10`. By default, uv will accept _any_ interpreter implementation.

In summary, the following Python interpreter requests are now allowed:

- A Python version without an implementation name, e.g., `3.10`
- A path to a directory containing a Python installation, e.g., `./foo/.venv`
- A path to a Python executable, e.g., `~/bin/python`
- A Python implementation without a version, e.g., `pypy` or `cpython`
- A Python implementation name and version, e.g., `pypy3.8` or `pypy@3.8`
- The name of a Python executable (for lookup in the `PATH`), e.g., `foopython3`

Previously, interpreter requests that were not versions or paths were always treated as executable
names.

To align the user expecations, uv now respects the interpreter that starts it. For example, `python -m uv ...` will
now prefer the `python` interpreter that was used to start uv instead of searching for a virtual environment.

### Enhancements

- Rewrite Python interpreter discovery ([#3266](https://github.com/astral-sh/uv/pull/3266))
- Add support for requesting `pypy` interpreters by implementation name ([#3706](https://github.com/astral-sh/uv/pull/3706))
- Discover and prefer the parent interpreter when invoked with `python -m uv` [#3736](https://github.com/astral-sh/uv/pull/3736)
- Add PEP 714 support for HTML API client ([#3697](https://github.com/astral-sh/uv/pull/3697))
- Add PEP 714 support for JSON API client ([#3698](https://github.com/astral-sh/uv/pull/3698))
- Write relative paths with unnamed requirement syntax ([#3682](https://github.com/astral-sh/uv/pull/3682))
- Allow relative Python executable paths in Windows trampoline ([#3717](https://github.com/astral-sh/uv/pull/3717))

### CLI

- Allow `--constraint` files in `pip sync` ([#3741](https://github.com/astral-sh/uv/pull/3741))
- Allow `--config-file` to be passed before or after command name ([#3730](https://github.com/astral-sh/uv/pull/3730))
- Make `--offline` a global argument ([#3729](https://github.com/astral-sh/uv/pull/3729))

### Bug fixes

- Evaluate arbitrary markers to `false` ([#3681](https://github.com/astral-sh/uv/pull/3681))
- Improve `DirWithoutEntrypoint` error message ([#3690](https://github.com/astral-sh/uv/pull/3690))
- Improve display of root package in range errors ([#3711](https://github.com/astral-sh/uv/pull/3711))
- Propagate URL errors in verbatim parsing ([#3720](https://github.com/astral-sh/uv/pull/3720))
- Report yanked packages in `--dry-run` ([#3740](https://github.com/astral-sh/uv/pull/3740))

## 0.1.45

### Enhancements

- Parse and store extras on editable requirements ([#3629](https://github.com/astral-sh/uv/pull/3629))
- Allow local versions in wheel filenames ([#3596](https://github.com/astral-sh/uv/pull/3596))
- Create lib64 symlink for 64-bit, non-macOS, POSIX environments ([#3584](https://github.com/astral-sh/uv/pull/3584))

### Configuration

- Add `UV_CONCURRENT_INSTALLS` variable in favor of `RAYON_NUM_THREADS` ([#3646](https://github.com/astral-sh/uv/pull/3646))
- Add serialization and deserialization for `--find-links` ([#3619](https://github.com/astral-sh/uv/pull/3619))
- Apply combination logic to merge CLI and persistent configuration ([#3618](https://github.com/astral-sh/uv/pull/3618))

### Bug fixes

- Reduce sensitivity of unknown option error to discard Python 2 interpreters ([#3580](https://github.com/astral-sh/uv/pull/3580))
- Respect installed packages in `uv run` ([#3603](https://github.com/astral-sh/uv/pull/3603))
- Separate cache construction from initialization ([#3607](https://github.com/astral-sh/uv/pull/3607))
- Add missing `"directory"` branch in source match ([#3608](https://github.com/astral-sh/uv/pull/3608))
- Fix source annotation in pip compile `annotation-style=line` output ([#3637](https://github.com/astral-sh/uv/pull/3637))
- Run cargo update to pull in h2 ([#3638](https://github.com/astral-sh/uv/pull/3638))
- URL-decode hashes in HTML fragments ([#3655](https://github.com/astral-sh/uv/pull/3655))
- Always print JSON output with `--format` json ([#3671](https://github.com/astral-sh/uv/pull/3671))

## 0.1.44



## 0.1.43

### Enhancements

- Annotate sources of requirements in `pip compile` output ([#3269](https://github.com/astral-sh/uv/pull/3269))
- Track origin for `setup.py` files and friends ([#3481](https://github.com/astral-sh/uv/pull/3481))

### Configuration

- Consolidate concurrency limits and expose as environment variables ([#3493](https://github.com/astral-sh/uv/pull/3493))

### Bug fixes

- Avoid keyword arguments for PEP 517 build hooks ([#3517](https://github.com/astral-sh/uv/pull/3517))
- Apply advisory locks when building source distributions ([#3525](https://github.com/astral-sh/uv/pull/3525))
- Avoid attempting to build editables when fetching metadata ([#3563](https://github.com/astral-sh/uv/pull/3563))
- Clone individual files on windows ReFS ([#3551](https://github.com/astral-sh/uv/pull/3551))
- Filter irrelevant requirements from source annotations ([#3479](https://github.com/astral-sh/uv/pull/3479))
- Make cache clearing robust to directories without read permissions ([#3524](https://github.com/astral-sh/uv/pull/3524))
- Respect constraints on editable dependencies ([#3554](https://github.com/astral-sh/uv/pull/3554))
- Skip Python 2 versions when locating Python ([#3476](https://github.com/astral-sh/uv/pull/3476))
- Make `--isolated` a global argument ([#3558](https://github.com/astral-sh/uv/pull/3558))
- Allow unknown `pyproject.toml` fields ([#3511](https://github.com/astral-sh/uv/pull/3511))
- Change error value detection for glibc ([#3487](https://github.com/astral-sh/uv/pull/3487))

## 0.1.42

This release includes stabilized support for persistent configuration in uv.

uv will now read project configuration from a `pyproject.toml` or `uv.toml` file in the current
directory or any parent directory, along with user configuration at `~/.config/uv/uv.toml`
(or `$XDG_CONFIG_HOME/uv/uv.toml`) on macOS and Linux, and `%APPDATA%\uv\uv.toml` on Windows.

See: [Persistent Configuration](https://github.com/astral-sh/uv?tab=readme-ov-file#persistent-configuration) for more.

### Enhancements

- Respect `MACOSX_DEPLOYMENT_TARGET` in `--python-platform` ([#3470](https://github.com/astral-sh/uv/pull/3470))

### Configuration

- Add documentation for persistent configuration ([#3467](https://github.com/astral-sh/uv/pull/3467))
- Add JSON Schema export to SchemaStore ([#3461](https://github.com/astral-sh/uv/pull/3461))
- Merge user and workspace settings ([#3462](https://github.com/astral-sh/uv/pull/3462))

### Bug fixes

- Use Metadata10 to parse PKG-INFO of legacy editable ([#3450](https://github.com/astral-sh/uv/pull/3450))
- Apply normcase to line from easy-install.pth ([#3451](https://github.com/astral-sh/uv/pull/3451))
- Upgrade `async_http_range_reader` to v0.8.0 to respect redirects in range requests ([#3460](https://github.com/astral-sh/uv/pull/3460))
- Use last non-EOL version for `--python-platform` macOS ([#3469](https://github.com/astral-sh/uv/pull/3469))

## 0.1.41

### Bug fixes

- Remove unconstrained version error from requirements ([#3443](https://github.com/astral-sh/uv/pull/3443))

## 0.1.40

### Enhancements

- Add `--allow-existing` to overwrite existing virtualenv ([#2548](https://github.com/astral-sh/uv/pull/2548))
- Respect and enable uninstalls of legacy editables (`.egg-link`) ([#3415](https://github.com/astral-sh/uv/pull/3415))
- Respect and enable uninstalls of existing `.egg-info` packages ([#3380](https://github.com/astral-sh/uv/pull/3380))

### CLI

- Accept `--no-upgrade`, `--no-refresh`, etc. on the CLI ([#3328](https://github.com/astral-sh/uv/pull/3328))

### Configuration

- Expose `UV_NO_BUILD_ISOLATION` as environment variable ([#3318](https://github.com/astral-sh/uv/pull/3318))
- Expose `UV_PYTHON` as an environment variable ([#3284](https://github.com/astral-sh/uv/pull/3284))
- Expose `UV_LINK_MODE` as environment variable ([#3315](https://github.com/astral-sh/uv/pull/3315))
- Add `UV_CUSTOM_COMPILE_COMMAND` to environment variable docs ([#3382](https://github.com/astral-sh/uv/pull/3382))

### Bug fixes

- Ignore 401 HTTP responses with multiple indexes ([#3292](https://github.com/astral-sh/uv/pull/3292))
- Avoid panic for file URLs ([#3306](https://github.com/astral-sh/uv/pull/3306))
- Quote version parse errors consistently ([#3325](https://github.com/astral-sh/uv/pull/3325))
- Detect current environment when `uv` is invoked from within a virtualenv ([#3379](https://github.com/astral-sh/uv/pull/3379))
- Unset target when creating virtual environments ([#3362](https://github.com/astral-sh/uv/pull/3362))
- Update activation scripts from virtualenv ([#3376](https://github.com/astral-sh/uv/pull/3376))
- Use canonical URLs in satisfaction check ([#3373](https://github.com/astral-sh/uv/pull/3373))

## 0.1.39

### Enhancements

- Add `--target` support to `sync` and `install` ([#3257](https://github.com/astral-sh/uv/pull/3257))
- Implement `--index-strategy unsafe-best-match` ([#3138](https://github.com/astral-sh/uv/pull/3138))

### Bug fixes

- Fix `platform_machine` tag for `--python-platform` on macOS ARM ([#3267](https://github.com/astral-sh/uv/pull/3267))

## 0.1.38

### Enhancements

- Add alternate manylinux targets to `--python-platform` ([#3229](https://github.com/astral-sh/uv/pull/3229))
- An enum and backticks for lookahead error ([#3216](https://github.com/astral-sh/uv/pull/3216))
- Upgrade macOS target to `12.0` ([#3228](https://github.com/astral-sh/uv/pull/3228))
- Add keyring logs for URL and host fetches ([#3212](https://github.com/astral-sh/uv/pull/3212))
- Combine unresolvable error dependency clauses with the same root ([#3225](https://github.com/astral-sh/uv/pull/3225))

### CLI

- Gave a better name to the `--color` placeholder ([#3226](https://github.com/astral-sh/uv/pull/3226))
- Warn when an unsupported Python version is encountered ([#3250](https://github.com/astral-sh/uv/pull/3250))

### Configuration

- Use directory instead of file when searching for `uv.toml` file ([#3203](https://github.com/astral-sh/uv/pull/3203))

### Bug fixes

- Avoid removing quites from requirements markers ([#3214](https://github.com/astral-sh/uv/pull/3214))
- Avoid adding extras when expanding constraints ([#3232](https://github.com/astral-sh/uv/pull/3232))
- Reinstall package when editable label is removed ([#3219](https://github.com/astral-sh/uv/pull/3219))

## 0.1.37

### Enhancements

- Change default HTTP read timeout to 30s ([#3182](https://github.com/astral-sh/uv/pull/3182))
- Add `--python-platform` to `sync` and `install` commands ([#3154](https://github.com/astral-sh/uv/pull/3154))
- Add ticks around error messages more consistently ([#3004](https://github.com/astral-sh/uv/pull/3004))
- Fix Docker publish permissions in release pipeline ([#3195](https://github.com/astral-sh/uv/pull/3195))
- Improve tracing for keyring provider ([#3207](https://github.com/astral-sh/uv/pull/3207))

### Bug fixes

- Fix fetch of credentials when cache is seeded with username ([#3206](https://github.com/astral-sh/uv/pull/3206))

## 0.1.36

### Enhancements

- Add support for embedded Python on Windows ([#3161](https://github.com/astral-sh/uv/pull/3161))
- Add Docker image publishing to release pipeline ([#3155](https://github.com/astral-sh/uv/pull/3155))

### Configuration

- Add `UV_CONSTRAINT` environment variable to provide value for `--constraint` ([#3162](https://github.com/astral-sh/uv/pull/3162))

### Bug fixes

- Avoid waiting for metadata for `--no-deps` editables ([#3188](https://github.com/astral-sh/uv/pull/3188))
- Fix `venvlauncher.exe` reference in venv creation ([#3160](https://github.com/astral-sh/uv/pull/3160))
- Fix authentication for URLs with a shared realm ([#3130](https://github.com/astral-sh/uv/pull/3130))
- Restrict observed requirements to direct when `--no-deps` is specified ([#3191](https://github.com/astral-sh/uv/pull/3191))

## 0.1.35

### Enhancements

- Add a `--python-platform` argument to enable resolving against a target platform ([#3111](https://github.com/astral-sh/uv/pull/3111))
- Enforce HTTP timeouts on a per-read (rather than per-request) basis ([#3144](https://github.com/astral-sh/uv/pull/3144))

### Bug fixes

- Avoid preferring constrained over unconstrained packages ([#3148](https://github.com/astral-sh/uv/pull/3148))
- Allow `UV_SYSTEM_PYTHON=1` in addition to `UV_SYSTEM_PYTHON=true` ([#3136](https://github.com/astral-sh/uv/pull/3136))

## 0.1.34

### CLI

- Allow `--python` and `--system` on `pip compile` ([#3115](https://github.com/astral-sh/uv/pull/3115))
- Remove `Option<bool>` for `--no-cache` ([#3129](https://github.com/astral-sh/uv/pull/3129))
- Rename `--compile` to `--compile-bytecode` ([#3102](https://github.com/astral-sh/uv/pull/3102))
- Accept `0`, `1`, and similar values for Boolean environment variables ([#3113](https://github.com/astral-sh/uv/pull/3113))

### Configuration

- Add `UV_REQUIRE_HASHES` environment variable ([#3125](https://github.com/astral-sh/uv/pull/3125))
- Add negation flags to the CLI ([#3050](https://github.com/astral-sh/uv/pull/3050))

### Bug fixes

- Avoid fetching unnecessary extra versions during resolution ([#3100](https://github.com/astral-sh/uv/pull/3100))
- Avoid deprioritizing recursive editables ([#3133](https://github.com/astral-sh/uv/pull/3133))
- Avoid treating localhost URLs as local file paths ([#3132](https://github.com/astral-sh/uv/pull/3132))
- Hide password in the index printed via `--emit-index-annotation` ([#3112](https://github.com/astral-sh/uv/pull/3112))
- Restore seeding of authentication cache from index URLs ([#3124](https://github.com/astral-sh/uv/pull/3124))

## 0.1.33

### Breaking changes

Using the keyring requires a username to be provided on index URLs now. Previously, the username `oauth2accesstoken`
was assumed. This will affect Google Artifact Registry users using `--keyring-provider subprocess` and an index URL
without a username. The suggested fix is to add the required username to index URLs, e.g., `https://oauth2accesstoken@<url>`.

See [#2976](https://github.com/astral-sh/uv/pull/2976#discussion_r1566521453) for details.

### Enhancements

- Allow passing a virtual environment path to `uv pip --python` ([#3064](https://github.com/astral-sh/uv/pull/3064))
- Add compatibility argument for `pip list --outdated` ([#3055](https://github.com/astral-sh/uv/pull/3055))

### CLI

- Enable auto-wrapping of `--help` output ([#3058](https://github.com/astral-sh/uv/pull/3058))
- Show `--require-hashes` CLI argument in help ([#3093](https://github.com/astral-sh/uv/pull/3093))

### Bug fixes

- Fix HTTP authentication when the password includes percent encoded characters (e.g. with Google Artifact Registry) ([#2822](https://github.com/astral-sh/uv/issues/2822))
- Use usernames from URLs when looking for credentials in netrc files and the keyring [#2563](https://github.com/astral-sh/uv/issues/2563))
- Skip `HEAD` requests for indexes that return 403 (e.g. PyPICloud) ([#3070](https://github.com/astral-sh/uv/pull/3070))
- Use kebab-case consistently ([#3080](https://github.com/astral-sh/uv/pull/3080))
- Show package name in no version for direct dependency error ([#3056](https://github.com/astral-sh/uv/pull/3056))
- Avoid erroring when encountering `.tar.bz2` source distributions ([#3069](https://github.com/astral-sh/uv/pull/3069))

## 0.1.32

### Enhancements

- Add a `--require-hashes` command-line setting ([#2824](https://github.com/astral-sh/uv/pull/2824))
- Add hash-checking support to `install` and `sync` ([#2945](https://github.com/astral-sh/uv/pull/2945))
- Add support for URL requirements in `--generate-hashes` ([#2952](https://github.com/astral-sh/uv/pull/2952))
- Allow unnamed requirements for overrides ([#2999](https://github.com/astral-sh/uv/pull/2999))
- Enforce and backtrack on invalid versions in source metadata ([#2954](https://github.com/astral-sh/uv/pull/2954))
- Fall back to distributions without hashes in resolver ([#2949](https://github.com/astral-sh/uv/pull/2949))
- Implement `--emit-index-annotation` to annotate source index for each package ([#2926](https://github.com/astral-sh/uv/pull/2926))
- Log hard-link failures ([#3015](https://github.com/astral-sh/uv/pull/3015))
- Support free-threaded Python ([#2805](https://github.com/astral-sh/uv/pull/2805))
- Support unnamed requirements in `--require-hashes` ([#2993](https://github.com/astral-sh/uv/pull/2993))
- Respect link mode for builds, in `uv pip compile` and for `uv venv` seed packages ([#3016](https://github.com/astral-sh/uv/pull/3016))
- Force color for build error messages ([#3032](https://github.com/astral-sh/uv/pull/3032))
- Surface invalid metadata as hints in error reports ([#2850](https://github.com/astral-sh/uv/pull/2850))

### Configuration

- Add `UV_BREAK_SYSTEM_PACKAGES` environment variable ([#2995](https://github.com/astral-sh/uv/pull/2995))

### CLI

- Remove some restrictions in argument groups ([#3001](https://github.com/astral-sh/uv/pull/3001))

### Bug fixes

- Add `--find-links` source distributions to the registry cache ([#2986](https://github.com/astral-sh/uv/pull/2986))
- Allow comments after all `requirements.txt` entries ([#3018](https://github.com/astral-sh/uv/pull/3018))
- Avoid cache invalidation on credentials renewal ([#3010](https://github.com/astral-sh/uv/pull/3010))
- Avoid calling `normalize_path` with relative paths that extend beyond the current directory ([#3013](https://github.com/astral-sh/uv/pull/3013))
- Deduplicate symbolic links between `purelib` and `platlib` ([#3002](https://github.com/astral-sh/uv/pull/3002))
- Remove unused `--output-file` from `pip install` ([#2975](https://github.com/astral-sh/uv/pull/2975))
- Strip query string when parsing filename from HTML index ([#2961](https://github.com/astral-sh/uv/pull/2961))
- Update hashes without `--upgrade` if not present ([#2966](https://github.com/astral-sh/uv/pull/2966))

## 0.1.31

### Bug fixes

- Ignore direct URL distributions in prefetcher ([#2943](https://github.com/astral-sh/uv/pull/2943))

## 0.1.30

### Enhancements

- Show resolution diagnostics after `pip install` ([#2829](https://github.com/astral-sh/uv/pull/2829))

### Bug fixes

- Backtrack on distributions with invalid metadata ([#2834](https://github.com/astral-sh/uv/pull/2834))
- Include LICENSE files in source distribution ([#2855](https://github.com/astral-sh/uv/pull/2855))
- Respect `--no-build` and `--no-binary` in `--find-links` ([#2826](https://github.com/astral-sh/uv/pull/2826))
- Respect cached local `--find-links` in install plan ([#2907](https://github.com/astral-sh/uv/pull/2907))
- Avoid panic with multiple confirmation handlers ([#2903](https://github.com/astral-sh/uv/pull/2903))
- Use scheme parsing to determine absolute vs. relative URLs ([#2904](https://github.com/astral-sh/uv/pull/2904))
- Remove additional 'because' in resolution failure messages ([#2849](https://github.com/astral-sh/uv/pull/2849))
- Use `miette` when printing `pip sync` resolution failures ([#2848](https://github.com/astral-sh/uv/pull/2848))

## 0.1.29

### Enhancements

- Allow conflicting Git URLs that refer to the same commit SHA ([#2769](https://github.com/astral-sh/uv/pull/2769))
- Allow package lookups across multiple indexes via explicit opt-in (`--index-strategy unsafe-any-match`) ([#2815](https://github.com/astral-sh/uv/pull/2815))
- Allow no-op `--no-compile` flag on CLI ([#2816](https://github.com/astral-sh/uv/pull/2816))
- Upgrade `rs-async-zip` to support data descriptors ([#2809](https://github.com/astral-sh/uv/pull/2809))

### Bug fixes

- Avoid unused extras check in `pip install` for source trees ([#2811](https://github.com/astral-sh/uv/pull/2811))
- Deduplicate editables during install commands ([#2820](https://github.com/astral-sh/uv/pull/2820))
- Fix windows lock race: lock exclusive after all try lock errors ([#2800](https://github.com/astral-sh/uv/pull/2800))
- Preserve `.git` suffixes and casing in Git dependencies ([#2789](https://github.com/astral-sh/uv/pull/2789))
- Respect Git tags and branches that look like short commits ([#2795](https://github.com/astral-sh/uv/pull/2795))
- Enable virtualenv creation on Windows with cpython-x86 ([#2707](https://github.com/astral-sh/uv/pull/2707))

## 0.1.28

### Enhancements

- Recursively resolve direct URL references upfront ([#2684](https://github.com/astral-sh/uv/pull/2684))

### Bug fixes

- Detect Fish via `FISH_VERSION` ([#2781](https://github.com/astral-sh/uv/pull/2781))
- Exclude installed distributions with multiple versions from consideration in the resolver ([#2779](https://github.com/astral-sh/uv/pull/2779))
- Resolve non-determistic behavior in preferences due to site-packages ordering ([#2780](https://github.com/astral-sh/uv/pull/2780))
- Use canonical URL to key redirect map ([#2764](https://github.com/astral-sh/uv/pull/2764))
- Use distribution database and index for all pre-resolution phases ([#2766](https://github.com/astral-sh/uv/pull/2766))
- Fix `uv self update` on Linux ([#2783](https://github.com/astral-sh/uv/pull/2783))

## 0.1.27

### Enhancements

- Add `--exclude-editable` support to `pip-freeze` ([#2740](https://github.com/astral-sh/uv/pull/2740))
- Add `pyproject.toml` et al to list of prompted packages ([#2746](https://github.com/astral-sh/uv/pull/2746))
- Consider installed packages during resolution ([#2596](https://github.com/astral-sh/uv/pull/2596))
- Recursively allow URL requirements for local dependencies ([#2702](https://github.com/astral-sh/uv/pull/2702))

### Configuration

- Add `UV_RESOLUTION` environment variable for `--resolution` ([#2720](https://github.com/astral-sh/uv/pull/2720))

### Bug fixes

- Respect overrides in all direct-dependency iterators ([#2742](https://github.com/astral-sh/uv/pull/2742))
- Respect subdirectories when reading static metadata ([#2728](https://github.com/astral-sh/uv/pull/2728))

## 0.1.26

### Bug fixes

- Bump simple cache version ([#2712](https://github.com/astral-sh/uv/pull/2712))

## 0.1.25

### Breaking changes

- Limit overrides and constraints to `requirements.txt` format ([#2632](https://github.com/astral-sh/uv/pull/2632))

### Enhancements

- Accept `setup.py` and `setup.cfg` files in compile ([#2634](https://github.com/astral-sh/uv/pull/2634))
- Add `--no-binary` and `--only-binary` support to `requirements.txt` ([#2680](https://github.com/astral-sh/uv/pull/2680))
- Allow prereleases, locals, and URLs in non-editable path requirements ([#2671](https://github.com/astral-sh/uv/pull/2671))
- Use PEP 517 to extract dynamic `pyproject.toml` metadata ([#2633](https://github.com/astral-sh/uv/pull/2633))
- Add `Editable project location` and `Required-by` to `pip show` ([#2589](https://github.com/astral-sh/uv/pull/2589))
- Avoid `prepare_metadata_for_build_wheel` calls for Hatch packages with dynamic dependencies ([#2645](https://github.com/astral-sh/uv/pull/2645))
- Fall back to PEP 517 hooks for non-compliant PEP 621 metadata ([#2662](https://github.com/astral-sh/uv/pull/2662))
- Support `file://localhost/` schemes ([#2657](https://github.com/astral-sh/uv/pull/2657))
- Use normal resolver in `pip sync` ([#2696](https://github.com/astral-sh/uv/pull/2696))

### CLI

- Disallow `pyproject.toml` from `pip uninstall -r` ([#2663](https://github.com/astral-sh/uv/pull/2663))
- Unhide `--emit-index-url` and `--emit-find-links` ([#2691](https://github.com/astral-sh/uv/pull/2691))
- Use dense formatting for requirement version specifiers in diagnostics ([#2601](https://github.com/astral-sh/uv/pull/2601))

### Bug fixes

- Don't error on multiple matching index URLs ([#2627](https://github.com/astral-sh/uv/pull/2627))
- Extract local versions from direct URL requirements ([#2624](https://github.com/astral-sh/uv/pull/2624))
- Respect `--no-index` with `--find-links` in `pip sync` ([#2692](https://github.com/astral-sh/uv/pull/2692))
- Use `Scripts` folder for virtualenv activation prompt ([#2690](https://github.com/astral-sh/uv/pull/2690))
