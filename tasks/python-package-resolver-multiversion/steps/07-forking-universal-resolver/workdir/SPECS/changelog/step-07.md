# uv release notes, step 07: releases in the span `0.5.0 -> 0.5.20` (upstream tag `0.5.20`)

Source: astral-sh/uv at tag `0.5.20`, file(s) `CHANGELOG.md`. 20 release entries: 0.5.20, 0.5.19, 0.5.18, 0.5.17, 0.5.16, 0.5.15, 0.5.14, 0.5.13, 0.5.12, 0.5.11, 0.5.10, 0.5.9, 0.5.8, 0.5.7, 0.5.6, 0.5.5, 0.5.4, 0.5.3, 0.5.2, 0.5.1. Licence: MIT OR Apache-2.0 (astral-sh/uv; see SPECS/licences/uv-LICENSE-MIT and SPECS/licences/uv-LICENSE-APACHE).

Kept sections: Breaking changes, Enhancements, Bug fixes, CLI, Configuration, Error messages, Python, Python versions, Preview, Stabilizations, Deprecations, Compatibility, Security, plus each release's preamble. Dropped: Documentation, Performance, Release, Rust API, Rust libraries, Other changes, Distributions. Kept text is verbatim.

## 0.5.20

### Bug fixes

- Avoid failing when deserializing unknown tags ([#10655](https://github.com/astral-sh/uv/pull/10655))

## 0.5.19

### Enhancements

- Filter wheels from lockfile based on architecture ([#10584](https://github.com/astral-sh/uv/pull/10584))
- Omit dynamic versions from the lockfile ([#10622](https://github.com/astral-sh/uv/pull/10622))
- Add support for `pip freeze --path` ([#10488](https://github.com/astral-sh/uv/pull/10488))
- Reduce verbosity of inline-metadata message when using `uv run <script.py>` ([#10588](https://github.com/astral-sh/uv/pull/10588))
- Add opt-in Git LFS support ([#10335](https://github.com/astral-sh/uv/pull/10335))
- Recommend `--native-tls` on SSL errors ([#10605](https://github.com/astral-sh/uv/pull/10605))
- Show expected and available ABI tags in resolver errors ([#10527](https://github.com/astral-sh/uv/pull/10527))
- Show target Python version in error messages ([#10582](https://github.com/astral-sh/uv/pull/10582))
- Add `--output-format=json` support to `uv python list` ([#10596](https://github.com/astral-sh/uv/pull/10596))

### Python

The managed Python distributions have been updated, including:

- Python 3.14 support on Windows
- Python 3.14.0a4 support
- 64-bit RISC-V Linux support
- Bundled `libedit` updated from 20210910-3.1 -> 20240808-3.1
- Bundled `tcl/tk` updated from 8.6.12 -> 8.6.14 (for all Python versions on Unix, only for Python 3.14 on Windows)

See the [`python-build-standalone` release notes](https://github.com/astral-sh/python-build-standalone/releases/tag/20250115) for more details.

### Bug fixes

- Avoid reading symlinks during `uv python install` on Windows ([#10639](https://github.com/astral-sh/uv/pull/10639))
- Correct Pyston tag format ([#10580](https://github.com/astral-sh/uv/pull/10580))
- Provide `pyproject.toml` path for parse errors in `uv venv` ([#10553](https://github.com/astral-sh/uv/pull/10553))
- Don't treat `setuptools` and `wheel` as seed packages in uv sync on Python 3.12 ([#10572](https://github.com/astral-sh/uv/pull/10572))
- Fix git-tag cache-key reader in case of slashes (#10467) ([#10500](https://github.com/astral-sh/uv/pull/10500))
- Include build tag in rendered wheel filenames ([#10599](https://github.com/astral-sh/uv/pull/10599))
- Patch embedded install path for Python dylib on macOS during `python install` ([#10629](https://github.com/astral-sh/uv/pull/10629))
- Read cached registry distributions when `--config-settings` are present ([#10578](https://github.com/astral-sh/uv/pull/10578))
- Show resolver hints for packages with markers ([#10607](https://github.com/astral-sh/uv/pull/10607))

### Preview features

- Make build backend type annotations more generic ([#10549](https://github.com/astral-sh/uv/pull/10549))

## 0.5.18

### Bug fixes

- Avoid forking for identical markers ([#10490](https://github.com/astral-sh/uv/pull/10490))
- Avoid panic in `uv remove` when only comments exist ([#10484](https://github.com/astral-sh/uv/pull/10484))
- Revert "improve shell compatibility of venv activate scripts (#10397)" ([#10497](https://github.com/astral-sh/uv/pull/10497))

## 0.5.17

This release includes support for generating lockfiles from scripts based on inline metadata, as defined in PEP 723.

By default, scripts remain unlocked, and must be locked explicitly with `uv lock --script /path/to/script.py`, which
will generate a lockfile adjacent to the script (e.g., `script.py.lock`). Once generated, the lockfile will be
respected (and updated, if necessary) across `uv run --script`, `uv add --script`, and `uv remove --script` invocations.

This release also includes support for `uv export --script` and `uv tree --script`. Both commands support PEP 723
scripts with and without accompanying lockfiles.

### Enhancements

- Add support for locking PEP 723 scripts ([#10135](https://github.com/astral-sh/uv/pull/10135))
- Respect PEP 723 script lockfiles in `uv run` ([#10136](https://github.com/astral-sh/uv/pull/10136))
- Update PEP 723 lockfile in `uv add --script` ([#10145](https://github.com/astral-sh/uv/pull/10145))
- Update PEP 723 lockfile in `uv remove --script` ([#10162](https://github.com/astral-sh/uv/pull/10162))
- Add `--script` support to `uv export` for PEP 723 scripts ([#10160](https://github.com/astral-sh/uv/pull/10160))
- Add `--script` support to `uv tree` for PEP 723 scripts ([#10159](https://github.com/astral-sh/uv/pull/10159))
- Add `ls` alias to `uv {tool, python, pip} list` ([#10240](https://github.com/astral-sh/uv/pull/10240))
- Allow reading `--with-requirements` from stdin in `uv add` and `uv run` ([#10447](https://github.com/astral-sh/uv/pull/10447))
- Warn-and-ignore for unsupported `requirements.txt` options ([#10420](https://github.com/astral-sh/uv/pull/10420))

### Preview features

- Add remaining Python type annotations to build backend ([#10434](https://github.com/astral-sh/uv/pull/10434))

### Bug fixes

- Fix `UV_FIND_LINKS` delimiter to split on commas ([#10477](https://github.com/astral-sh/uv/pull/10477))
- Improve `uv tool list` output when tool environment is broken ([#10409](https://github.com/astral-sh/uv/pull/10409))
- Only track markers for compatible versions ([#10457](https://github.com/astral-sh/uv/pull/10457))
- Respect `requires-python` when installing tools ([#10401](https://github.com/astral-sh/uv/pull/10401))
- Visit proxy packages eagerly ([#10441](https://github.com/astral-sh/uv/pull/10441))
- Improve shell compatibility of `venv` activate scripts ([#10397](https://github.com/astral-sh/uv/pull/10397))
- Read publish username from URL ([#10469](https://github.com/astral-sh/uv/pull/10469))

## 0.5.16

### Enhancements

- Accept full requirements in `uv remove` ([#10338](https://github.com/astral-sh/uv/pull/10338))

### Bug fixes

- Avoid enforcing project-level required version for `uv self` ([#10374](https://github.com/astral-sh/uv/pull/10374))
- Fix Ruff linting warnings from generated template files for extension modules ([#10371](https://github.com/astral-sh/uv/pull/10371))

## 0.5.15

### Python

The managed Python distributions have been updated, including:

- Python 3.14.0a3 support on macOS and Linux
- Performance improvements
- Fixes to SQLite feature detection

See the [`python-build-standalone` release notes](https://github.com/astral-sh/python-build-standalone/releases/tag/20250106) for more details.

### Enhancements

- Respect `FORCE_COLOR` environment variable ([#10315](https://github.com/astral-sh/uv/pull/10315))

### Bug fixes

- Avoid downgrading packages when `--upgrade` is provided ([#10097](https://github.com/astral-sh/uv/pull/10097))
- Extract supported architectures from wheel tags ([#10179](https://github.com/astral-sh/uv/pull/10179))
- Redact new index credentials in `uv add` ([#10329](https://github.com/astral-sh/uv/pull/10329))

## 0.5.14

### Enhancements

- Add `--exact` flag to `uv run` ([#10198](https://github.com/astral-sh/uv/pull/10198))
- Add `--outdated` support to `uv pip tree` ([#10199](https://github.com/astral-sh/uv/pull/10199))
- Add a required version setting to uv ([#10248](https://github.com/astral-sh/uv/pull/10248))
- Add loongarch64 to supported Python platform tags ([#10223](https://github.com/astral-sh/uv/pull/10223))
- Add manylinux2014 aliases for `--python-platform` ([#10217](https://github.com/astral-sh/uv/pull/10217))
- Add support for Python interpreters on ARMv5TE platforms ([#10234](https://github.com/astral-sh/uv/pull/10234))
- Add support for optional `--description` in `uv init` ([#10209](https://github.com/astral-sh/uv/pull/10209))
- Ignore empty or missing hrefs in Simple HTML ([#10276](https://github.com/astral-sh/uv/pull/10276))
- Patch pkgconfig files after Python install ([#10189](https://github.com/astral-sh/uv/pull/10189))

### Bug fixes

- Accept directories with space names in `uv init` ([#10246](https://github.com/astral-sh/uv/pull/10246))
- Avoid forking on version in non-universal resolutions ([#10274](https://github.com/astral-sh/uv/pull/10274))
- Avoid stripping query parameters from URLs ([#10253](https://github.com/astral-sh/uv/pull/10253))
- Consider workspace dependencies to be 'direct' ([#10197](https://github.com/astral-sh/uv/pull/10197))
- Detect cyclic dependencies during builds ([#10258](https://github.com/astral-sh/uv/pull/10258))
- Guard against self-deletion in `uv venv` and `uv tool` ([#10206](https://github.com/astral-sh/uv/pull/10206))
- Respect static metadata for already-installed distributions ([#10242](https://github.com/astral-sh/uv/pull/10242))

## 0.5.13

### Bug fixes

- Avoid enforcing URL check on initial publish ([#10182](https://github.com/astral-sh/uv/pull/10182))
- Fix incorrect mismatched constraints reference ([#10184](https://github.com/astral-sh/uv/pull/10184))
- Revert "Update `reqwest` (#10178)" ([#10187](https://github.com/astral-sh/uv/pull/10187))

## 0.5.12

### Enhancements

- Support `uv export` for non-project workspaces ([#10144](https://github.com/astral-sh/uv/pull/10144))
- Set glibc versions for standalone installers ([#10142](https://github.com/astral-sh/uv/pull/10142))
- Allow environment variables to be included in cache keys ([#10170](https://github.com/astral-sh/uv/pull/10170))

### Preview features

- Include extras in `uv-build` `Requires-Dist` metadata ([#10110](https://github.com/astral-sh/uv/pull/10110))
- Use `shutil.which` for the build backend ([#10028](https://github.com/astral-sh/uv/pull/10028))

### Bug fixes

- Always write slash paths to RECORD file ([#10164](https://github.com/astral-sh/uv/pull/10164))
- Add support for subdirectories in direct URLs in `uv.lock` ([#10068](https://github.com/astral-sh/uv/pull/10068))
- Avoid duplicating backslashes in sysconfig parser ([#10063](https://github.com/astral-sh/uv/pull/10063))
- Avoid erroring when subdirectories are provided in `uv add` ([#10095](https://github.com/astral-sh/uv/pull/10095))
- Backtrack to non-local versions when wheels are missing platform support ([#10046](https://github.com/astral-sh/uv/pull/10046))
- Fix mirror script to handle newer metadata format ([#10050](https://github.com/astral-sh/uv/pull/10050))
- Preserve sort when deciding on requirement placement ([#10078](https://github.com/astral-sh/uv/pull/10078))
- Remove redundant alias in `uv init` CLI ([#10124](https://github.com/astral-sh/uv/pull/10124))
- Respect sources credentials in non-project workspaces ([#10125](https://github.com/astral-sh/uv/pull/10125))
- Show non-project dependencies in `uv tree` ([#10149](https://github.com/astral-sh/uv/pull/10149))
- Strip fragment when storing direct URL ([#10093](https://github.com/astral-sh/uv/pull/10093))
- Include hashes for local source archives ([#10080](https://github.com/astral-sh/uv/pull/10080))

## 0.5.11

### Enhancements

- Normalize `platform_system` to `sys_platform` ([#9949](https://github.com/astral-sh/uv/pull/9949))
- Improve retry mechanisms on Windows for `copy_atomic` and `write_atomic` ([#10026](https://github.com/astral-sh/uv/pull/10026))
- Add nuance to prefetch logging ([#9984](https://github.com/astral-sh/uv/pull/9984))
- Update to [`python-build-standalone 20241219`](https://github.com/astral-sh/python-build-standalone/releases/tag/20241219)

### Preview features

- Build backend: Preserve executable bits for scripts in distributions ([#10027](https://github.com/astral-sh/uv/pull/10027))
- Build backend: Handle case where `metadata_directory` already contains `dist-info` directory ([#10005](https://github.com/astral-sh/uv/pull/10005))

### Bug fixes

- Allow `--script` to be provided with `uv run -` ([#10035](https://github.com/astral-sh/uv/pull/10035))
- Allow `uv run` arguments when reading from `stdin` ([#10034](https://github.com/astral-sh/uv/pull/10034))
- Prefer higher Python lower-bounds when forking ([#10007](https://github.com/astral-sh/uv/pull/10007))
- Remove references to deprecated `first-match` ([#10036](https://github.com/astral-sh/uv/pull/10036))

## 0.5.10

### Enhancements

- Improve backtracking behavior when packages conflict repeatedly ([#9843](https://github.com/astral-sh/uv/pull/9843))
- Patch Python `sysconfig` values such as `AR` at `ar` install time ([#9905](https://github.com/astral-sh/uv/pull/9905))
- Patch Python `sysconfig` values such as `clang` to `cc` at install time ([#9916](https://github.com/astral-sh/uv/pull/9916))
- Skip `--native-tls` in `pip compile` header ([#9913](https://github.com/astral-sh/uv/pull/9913))
- Add resolver error hint for no-binary and no-build failures ([#9948](https://github.com/astral-sh/uv/pull/9948))
- Improve build error messages ([#9660](https://github.com/astral-sh/uv/pull/9660))
- Reduce redundant Python version incompatibilities in resolver error message ([#9957](https://github.com/astral-sh/uv/pull/9957))
- Reduce redundant enumeration of all package versions in some resolver errors ([#9885](https://github.com/astral-sh/uv/pull/9885))
- Improve display of ranges when pre-releases are not allowed ([#9944](https://github.com/astral-sh/uv/pull/9944))
- Improve error messages for `uv remove` ([#9959](https://github.com/astral-sh/uv/pull/9959))
- Improve phrasing for single term incompatibilities ([#9953](https://github.com/astral-sh/uv/pull/9953))
- Improve styling of `uv remove` dependency hints ([#9960](https://github.com/astral-sh/uv/pull/9960))
- Omit trailing zeros on Python requirements inferred from versions ([#9952](https://github.com/astral-sh/uv/pull/9952))
- Show a concise error message for missing `version` field ([#9912](https://github.com/astral-sh/uv/pull/9912))
- Use the build options value to improve hints for no wheel / source distribution errors ([#9950](https://github.com/astral-sh/uv/pull/9950))

### Bug fixes

- Allow multiple disjoint URLs in overrides ([#9893](https://github.com/astral-sh/uv/pull/9893))
- Include explicit indexes in publish index choice ([#9932](https://github.com/astral-sh/uv/pull/9932))
- Fix Python interpreter detection for 32-bit operating systems on 64-bit hosts ([#9970](https://github.com/astral-sh/uv/pull/9970))

### Preview features

- Build backend: Fix pre-PEP 639 license files ([#9965](https://github.com/astral-sh/uv/pull/9965))

## 0.5.9

### Enhancements

- Fork version selection based on `requires-python` requirements ([#9827](https://github.com/astral-sh/uv/pull/9827))
- Patch `sysconfig` data at install time ([#9857](https://github.com/astral-sh/uv/pull/9857))
- Remove `-isysroot` when patching sysconfig ([#9860](https://github.com/astral-sh/uv/pull/9860))

### Configuration

- Introduce a `--fork-strategy` preference mode ([#9868](https://github.com/astral-sh/uv/pull/9868))
- Add support for `UV_OFFLINE` ([#9795](https://github.com/astral-sh/uv/pull/9795))

### Bug fixes

- Avoid `panic!()` when current directory does not exist ([#9876](https://github.com/astral-sh/uv/pull/9876))
- Avoid reusing interpreter metadata when running under Rosetta ([#9846](https://github.com/astral-sh/uv/pull/9846))
- Avoid trailing slash when deserializing from lockfile ([#9848](https://github.com/astral-sh/uv/pull/9848))
- Fix bug in terms when collapsing unavailable versions in resolver errors ([#9877](https://github.com/astral-sh/uv/pull/9877))
- Fix suggestion to use `uv help python` on invalid install requests ([#9820](https://github.com/astral-sh/uv/pull/9820))
- Skip root when assessing prefix viability ([#9823](https://github.com/astral-sh/uv/pull/9823))
- Avoid spurious 'Upgraded tool environment' in `uv tool upgrade` ([#9870](https://github.com/astral-sh/uv/pull/9870))

### Preview features

- Build backend: Allow underscores in entrypoints ([#9825](https://github.com/astral-sh/uv/pull/9825))

## 0.5.8

**This release does not include the `powerpc64le-unknown-linux-musl` target due to a build issue. See [#9793](https://github.com/astral-sh/uv/issues/9793) for details. If this change affects you, please file an issue with your use-case.**

### Enhancements

- Omit empty resolution markers in lockfile  ([#9738](https://github.com/astral-sh/uv/pull/9738))
- Add `--install-dir` to to `uv python install` and `uninstall` commands ([#7920](https://github.com/astral-sh/uv/pull/7920))
- Add `--show-urls` and `--only-downloads` to `uv python list` ([#8062](https://github.com/astral-sh/uv/pull/8062))
- Add `uv python list --all-arches` ([#9782](https://github.com/astral-sh/uv/pull/9782))
- Add `uv run --gui-script` flag for running Python scripts with `pythonw.exe` ([#9152](https://github.com/astral-sh/uv/pull/9152))
- Allow `--gui-script` on Unix ([#9787](https://github.com/astral-sh/uv/pull/9787))
- Allow download of Python distribution variants optimized for newer x86_64 microarchitectures ([#9781](https://github.com/astral-sh/uv/pull/9781))
- Allow execution of `pyw` files on Unix ([#9759](https://github.com/astral-sh/uv/pull/9759))
- Allow users to specify URLs in `project.dependencies` and `tool.uv.sources` ([#9718](https://github.com/astral-sh/uv/pull/9718))
- Encode mutually-incompatible pairs of markers ([#9444](https://github.com/astral-sh/uv/pull/9444))
- Improve the error message when a Python install request is not valid ([#9783](https://github.com/astral-sh/uv/pull/9783))
- Preserve directory-level standalone build symlinks ([#9723](https://github.com/astral-sh/uv/pull/9723))
- Add support for `uv publish --index <name>` ([#9694](https://github.com/astral-sh/uv/pull/9694))
- Reframe `--locked` and `--frozen` as `--check` operations for `uv lock` ([#9662](https://github.com/astral-sh/uv/pull/9662))
- Rename Python install scratch directory from `.cache` -> `.temp` ([#9756](https://github.com/astral-sh/uv/pull/9756))
- Enable `uv tool uninstall uv` on Windows ([#8963](https://github.com/astral-sh/uv/pull/8963))
- Improve self-dependency hint to make shadowing clear ([#9716](https://github.com/astral-sh/uv/pull/9716))
- Refactor unavailable metadata to shrink the resolver ([#9769](https://github.com/astral-sh/uv/pull/9769))
- Show 'depends on itself' for proxy packages ([#9717](https://github.com/astral-sh/uv/pull/9717))
- Show a dedicated error for missing subdirectories ([#9761](https://github.com/astral-sh/uv/pull/9761))
- Show a dedicated hint for missing `git+` prefixes ([#9789](https://github.com/astral-sh/uv/pull/9789))

### Bug fixes

- Avoid enforcing non-conflicts in `uv export` ([#9751](https://github.com/astral-sh/uv/pull/9751))
- Don't drop comments between items in TOML tables ([#9784](https://github.com/astral-sh/uv/pull/9784))
- Don't fail with `--no-build` when static metadata is available ([#9785](https://github.com/astral-sh/uv/pull/9785))
- Don't filter non-patch registry version ([#9736](https://github.com/astral-sh/uv/pull/9736))
- Don't read metadata from stale `.egg-info` files ([#9760](https://github.com/astral-sh/uv/pull/9760))
- Enforce correctness of self-dependencies ([#9705](https://github.com/astral-sh/uv/pull/9705))
- Fix projects's typo in resolver error messages ([#9708](https://github.com/astral-sh/uv/pull/9708))
- Ignore `.` prefixed directories during managed Python installation discovery ([#9786](https://github.com/astral-sh/uv/pull/9786))
- Improve handling of invalid virtual environments during interpreter discovery ([#8086](https://github.com/astral-sh/uv/pull/8086))
- Normalize relative paths when `--project` is specified ([#9709](https://github.com/astral-sh/uv/pull/9709))
- Respect self-constraints on recursive extras ([#9714](https://github.com/astral-sh/uv/pull/9714))
- Respect user settings for tracing coloring ([#9733](https://github.com/astral-sh/uv/pull/9733))
- Retry on tar extraction errors ([#9753](https://github.com/astral-sh/uv/pull/9753))
- Add conflict markers to the lock file ([#9370](https://github.com/astral-sh/uv/pull/9370))
- De-duplicate resolution markers ([#9780](https://github.com/astral-sh/uv/pull/9780))
- Avoid 403 error hint for PyTorch URLs ([#9750](https://github.com/astral-sh/uv/pull/9750))
- Avoid treating non-existent `--find-links` as relative URLs ([#9720](https://github.com/astral-sh/uv/pull/9720))
- Omit Windows Store `python3.13.exe` et al ([#9679](https://github.com/astral-sh/uv/pull/9679))
- Replace executables with broken symlinks during `uv python install` ([#9706](https://github.com/astral-sh/uv/pull/9706))

## 0.5.7

### Enhancements

- Ignore dynamic version in source dist ([#9549](https://github.com/astral-sh/uv/pull/9549))
- Improve build frontend error handling ([#9611](https://github.com/astral-sh/uv/pull/9611))
- Un-hide `uv build --no-build-logs` option ([#9642](https://github.com/astral-sh/uv/pull/9642))
- Flag version mismatch between sdist and wheel during `uv build` ([#9633](https://github.com/astral-sh/uv/pull/9633))
- Improve message when updater receipt is for a different uv executable ([#9487](https://github.com/astral-sh/uv/pull/9487))
- Add environment variable to disable writing installer metadata files ([#8877](https://github.com/astral-sh/uv/pull/8877))
- Add managed downloads for the latest CPython releases: `3.9.21`, `3.10.16`, `3.11.11`, `3.12.8`, and `3.13.1` ([#9696](https://github.com/astral-sh/uv/pull/9696))

### Preview features

- Build backend: Add hint on import with preview disabled ([#9691](https://github.com/astral-sh/uv/pull/9691))
- Build backend: Add direct builds to the resolver and installer ([#9621](https://github.com/astral-sh/uv/pull/9621))
- Build backend: Add integration test for scripts ([#9635](https://github.com/astral-sh/uv/pull/9635))
- Build backend: Add template to `uv init` ([#9661](https://github.com/astral-sh/uv/pull/9661))
- Build backend: Add `--list` option ([#9610](https://github.com/astral-sh/uv/pull/9610))

### Bug fixes

- Create missing parent directories for output file of `uv export` / `uv pip compile` ([#9648](https://github.com/astral-sh/uv/pull/9648))
- Fix missing display of non-freethreaded Python 3.13 in `python list` ([#9669](https://github.com/astral-sh/uv/pull/9669))
- Implement `Ord` and `PartialOrd` without origin for `Requirement` ([#9624](https://github.com/astral-sh/uv/pull/9624))
- Include more sources to avoid lowest bound warning ([#9644](https://github.com/astral-sh/uv/pull/9644))
- Respect build tag priority in `uv.lock` ([#9677](https://github.com/astral-sh/uv/pull/9677))

## 0.5.6

### Enhancements

- Add `--dry-run` to `uv pip uninstall` ([#9557](https://github.com/astral-sh/uv/pull/9557))
- Allow `--constraints` and `--overrides` in `uv tool install` ([#9547](https://github.com/astral-sh/uv/pull/9547))
- Display removed Python executables on uninstall ([#9459](https://github.com/astral-sh/uv/pull/9459))
- Warn when keyring has no password for `uv publish` ([#8827](https://github.com/astral-sh/uv/pull/8827))
- Add suggested action when `.python-version` pin is incompatible with the project ([#9590](https://github.com/astral-sh/uv/pull/9590))
- Improve error messages for mismatches in `tool.uv.sources` ([#9482](https://github.com/astral-sh/uv/pull/9482))
- Use constraints in trace rather than irrelevant `requires-python` ([#9529](https://github.com/astral-sh/uv/pull/9529))

### Preview features

- Add `uv python install --default` ([#8650](https://github.com/astral-sh/uv/pull/8650))
- Fix Python executable installation when multiple patch versions are requested ([#9607](https://github.com/astral-sh/uv/pull/9607))
- Build backend: Revamp `include` / `exclude` ([#9525](https://github.com/astral-sh/uv/pull/9525))
- Build backend: Add fast path  ([#9556](https://github.com/astral-sh/uv/pull/9556))
- Build backend: Add functions to collect file list ([#9602](https://github.com/astral-sh/uv/pull/9602))
- Build backend: Default excludes ([#9552](https://github.com/astral-sh/uv/pull/9552))
- Build backend: Refactoring before list ([#9558](https://github.com/astral-sh/uv/pull/9558))
- Build backend: Warn when visiting over 10k files  ([#9523](https://github.com/astral-sh/uv/pull/9523))

### Configuration

- Make `check-url` available in configuration files ([#9032](https://github.com/astral-sh/uv/pull/9032))

### Bug fixes

- Allow `file:` URLs to include package names ([#9493](https://github.com/astral-sh/uv/pull/9493))
- Avoid using IDs across PubGrub states ([#9538](https://github.com/astral-sh/uv/pull/9538))
- Consistently enforce requested-vs.-built metadata when retrieving wheels ([#9484](https://github.com/astral-sh/uv/pull/9484))
- Do not show empty version specifier in `uv tool list` ([#9605](https://github.com/astral-sh/uv/pull/9605))
- Include Git member information when getting metadata from cache ([#9388](https://github.com/astral-sh/uv/pull/9388))
- Include base installation directory in uv run PATH ([#9585](https://github.com/astral-sh/uv/pull/9585))
- Insert backslash when appending to system drive ([#9488](https://github.com/astral-sh/uv/pull/9488))
- Normalize paths when lowering Git dependencies ([#9595](https://github.com/astral-sh/uv/pull/9595))
- Omit origin when comparing requirements ([#9570](https://github.com/astral-sh/uv/pull/9570))
- Override `manylinux_compatible` with `--python-platform` ([#9526](https://github.com/astral-sh/uv/pull/9526))
- Pass extra when evaluating lockfile markers ([#9539](https://github.com/astral-sh/uv/pull/9539))
- Propagate markers for recursive extras in resolver ([#9509](https://github.com/astral-sh/uv/pull/9509))
- Respect path dependencies within Git dependencies ([#9594](https://github.com/astral-sh/uv/pull/9594))
- Support recursive extras with marker in `pip compile -r pyproject.toml` ([#9535](https://github.com/astral-sh/uv/pull/9535))
- Don't emit unpinned warning for proxy packages ([#9497](https://github.com/astral-sh/uv/pull/9497))
- Fix `--refresh-package` flag mentioned as `--refresh-dependency` ([#9486](https://github.com/astral-sh/uv/pull/9486))
- Handle Windows AV/EDR file locks during script installations ([#9543](https://github.com/astral-sh/uv/pull/9543))
- Re-enable conflicting extra/group tests and fix regression from #9540 ([#9582](https://github.com/astral-sh/uv/pull/9582))

## 0.5.5

### Enhancements

- Add aliases for build backend requests ([#9294](https://github.com/astral-sh/uv/pull/9294))
- Avoid displaying empty paths ([#9312](https://github.com/astral-sh/uv/pull/9312))
- Allow constraints in `uv tool upgrade` ([#9375](https://github.com/astral-sh/uv/pull/9375))
- Remove conflict between `--no-sync` and `--frozen` in `uv run` ([#9400](https://github.com/astral-sh/uv/pull/9400))
- Respect dependency sources in overrides and constraints ([#9455](https://github.com/astral-sh/uv/pull/9455))
- Show an interpreter-focused message for `--target` and `--prefix` ([#9373](https://github.com/astral-sh/uv/pull/9373))
- Add `--no-extra` flag and setting ([#9387](https://github.com/astral-sh/uv/pull/9387))
- Add `uv export --prune` ([#9389](https://github.com/astral-sh/uv/pull/9389))
- Add dedicated error message for musl install attempts ([#9430](https://github.com/astral-sh/uv/pull/9430))
- Add various grammar changes to conflict error messages ([#9369](https://github.com/astral-sh/uv/pull/9369))
- Annotate default groups in conflict error messages ([#9368](https://github.com/astral-sh/uv/pull/9368))
- Report marker diagnostics during parsing, rather than evaluation ([#9338](https://github.com/astral-sh/uv/pull/9338))
- Use consistent formatting for build system errors ([#9340](https://github.com/astral-sh/uv/pull/9340))
- Use rich diagnostics for build failures ([#9335](https://github.com/astral-sh/uv/pull/9335))

### Preview features

- Improve build backend excludes ([#9281](https://github.com/astral-sh/uv/pull/9281))
- Include PEP 639 `license-files` metadata during `uv publish` ([#9442](https://github.com/astral-sh/uv/pull/9442))

### Bug fixes

- Allow dependency groups to include the containing package ([#9385](https://github.com/astral-sh/uv/pull/9385))
- Allow syncing to empty virtual environment directories ([#9427](https://github.com/astral-sh/uv/pull/9427))
- Allow system Python discovery with `--target` and `--prefix` ([#9371](https://github.com/astral-sh/uv/pull/9371))
- Don't warn when `--output-file` is empty ([#9417](https://github.com/astral-sh/uv/pull/9417))
- Fix Python interpreter discovery on non-glibc hosts ([#9005](https://github.com/astral-sh/uv/pull/9005))
- Fix `tool.uv.dependency-metadata.[].version` schema ([#9468](https://github.com/astral-sh/uv/pull/9468))
- Only respect preferences across the same indexes ([#9302](https://github.com/astral-sh/uv/pull/9302))
- Re-compile when `--compile` is passed to an install operation ([#9378](https://github.com/astral-sh/uv/pull/9378))
- Remove `--upgrade`, `--no-upgrade`, and `--upgrade-package` from `uv tool upgrade` ([#9318](https://github.com/astral-sh/uv/pull/9318))
- Remove dev dependencies in `--all-groups --no-dev` ([#9300](https://github.com/astral-sh/uv/pull/9300))
- Surface extras and group conflicts in `uv export` ([#9365](https://github.com/astral-sh/uv/pull/9365))
- Treat deprecated aliases as equivalent in marker algebra ([#9342](https://github.com/astral-sh/uv/pull/9342))
- Treat less compatible tags as lower priority in resolver ([#9339](https://github.com/astral-sh/uv/pull/9339))

## 0.5.4

### Enhancements

- Accept either singular or plural values for CLI requirements ([#9196](https://github.com/astral-sh/uv/pull/9196))
- Add `--all-groups` to `uv sync`, `uv run`, `uv export`, and `uv tree` ([#8892](https://github.com/astral-sh/uv/pull/8892))
- Add a progress bar to `uv tree --outdated` and `uv pip list --outdated` ([#9284](https://github.com/astral-sh/uv/pull/9284))
- Add retries for Python downloads ([#9274](https://github.com/astral-sh/uv/pull/9274))
- Use exponential backoff for publish retries ([#9276](https://github.com/astral-sh/uv/pull/9276))
- Add manylinux target triples up to glibc 2.40 ([#9234](https://github.com/astral-sh/uv/pull/9234))

### Bug fixes

- Avoid validating extra and group sources in `build-system.requires` ([#9273](https://github.com/astral-sh/uv/pull/9273))
- Catch retries with wrapped `reqwest` errors ([#9253](https://github.com/astral-sh/uv/pull/9253))
- Sort hashes in `uv export` output ([#9237](https://github.com/astral-sh/uv/pull/9237))
- Strip `--index` and `--default-index` from command header ([#9288](https://github.com/astral-sh/uv/pull/9288))

### Error messages

- Add dedicated warning for empty stdin ([#9256](https://github.com/astral-sh/uv/pull/9256))

## 0.5.3

This release includes support for conflicting optional dependencies and dependency groups in the uv resolver, including the ability to specify dependency sources (like index assignment) on a per-extra or per-group basis.

For example, you can now select CPU-only vs. GPU-enabled PyTorch builds at runtime by defining conflicting extras in a `pyproject.toml`, and assigning different extras to different PyTorch indexes:

```toml
[project]
name = "project"
version = "0.1.0"
requires-python = ">=3.12.0"

[project.optional-dependencies]
# Include `torch` whenever `--extra cpu` or `--extra gpu` is provided.
cpu = ["torch>=2.5.1"]
gpu = ["torch>=2.5.1"]

[tool.uv]
# But allow `cpu` and `gpu` to choose conflicting versions of `torch`.
conflicts = [[{ extra = "cpu" }, { extra = "gpu" }]]

[tool.uv.sources]
torch = [
  # With `--extra cpu`, pull PyTorch from the CPU-only index.
  { index = "pytorch-cpu", extra = "cpu", marker = "platform_system != 'Darwin'" },
  # With `--extra gpu`, pull PyTorch from the GPU-enabled index.
  { index = "pytorch-gpu", extra = "gpu" },
]

[[tool.uv.index]]
name = "pytorch-cpu"
url = "https://download.pytorch.org/whl/cpu"
explicit = true

[[tool.uv.index]]
name = "pytorch-gpu"
url = "https://download.pytorch.org/whl/cu124"
explicit = true
```

See the [PyTorch](https://docs.astral.sh/uv/guides/integration/pytorch/) documentation for more.

### Enhancements

- Allow conflicting extras in explicit index assignments ([#9160](https://github.com/astral-sh/uv/pull/9160))
- Support overrides and constraints in PEP 723 scripts ([#9162](https://github.com/astral-sh/uv/pull/9162))
- Update `uv tool install --force` to imply `--reinstall-package <name>` ([#9074](https://github.com/astral-sh/uv/pull/9074))
- Turn `--verify-hashes` on by default ([#9170](https://github.com/astral-sh/uv/pull/9170))

### Bug fixes

- Allow apostrophe in virtual environment name ([#8984](https://github.com/astral-sh/uv/pull/8984))
- Automatically retry body errors when processing response ([#9213](https://github.com/astral-sh/uv/pull/9213))
- Detect nested workspace inside the current workspace and members with identical names ([#9094](https://github.com/astral-sh/uv/pull/9094))
- Only install the specified project with `--frozen --package` in legacy non-`[project]` workspaces ([#9215](https://github.com/astral-sh/uv/pull/9215))
- Respect `[[tool.uv.index]]` in PEP 723 scripts ([#9208](https://github.com/astral-sh/uv/pull/9208))
- Show derivation markers for resolutions with project name ([#9136](https://github.com/astral-sh/uv/pull/9136))
- Sort distributions when computing hash ([#9185](https://github.com/astral-sh/uv/pull/9185))
- Include trampolines in source distributions on Windows ([#9172](https://github.com/astral-sh/uv/pull/9172))

### Error messages

- Add a dedicated error for `include = "dev"` with `tool.uv.dev-dependencies` ([#9173](https://github.com/astral-sh/uv/pull/9173))
- Avoid showing disjoint marker error with `true` ([#9169](https://github.com/astral-sh/uv/pull/9169))
- Improve error message when `git` is not found ([#9206](https://github.com/astral-sh/uv/pull/9206))
- Include extras and dependency groups in derivation chains ([#9113](https://github.com/astral-sh/uv/pull/9113))
- Include version constraints in derivation chains ([#9112](https://github.com/astral-sh/uv/pull/9112))

## 0.5.2

### Enhancements

- Hide `--no-system` from `uv pip tree` CLI ([#9040](https://github.com/astral-sh/uv/pull/9040))
- Allow configuration of Python and PyPy install mirrors in `uv.toml` ([#8695](https://github.com/astral-sh/uv/pull/8695))
- Allow passing Python download mirrors to `uv python install` ([#8695](https://github.com/astral-sh/uv/pull/8695))
- Add support for specifying conflicting extras and dependency groups ([#8976](https://github.com/astral-sh/uv/pull/8976), [#9096](https://github.com/astral-sh/uv/pull/9096))
- Consistent colon usage in build failure errors ([#8994](https://github.com/astral-sh/uv/pull/8994))
- Show full derivation chain when encountering build failures ([#9108](https://github.com/astral-sh/uv/pull/9108))
- Show link we failed on parsing index pages ([#9118](https://github.com/astral-sh/uv/pull/9118))
- Remove duplicate log when searching for interpreters ([#9092](https://github.com/astral-sh/uv/pull/9092))
- Update uv development status classifier to "Stable" on PyPI ([#8943](https://github.com/astral-sh/uv/pull/8943))
- Use rich diagnostic formatting for early build failures ([#9041](https://github.com/astral-sh/uv/pull/9041))
- Use rich diagnostic formatting for install failures ([#9043](https://github.com/astral-sh/uv/pull/9043))

### Bug fixes

- Allow `--no-build` to validate lock ([#9024](https://github.com/astral-sh/uv/pull/9024))
- Allow default indexes to be marked as explicit ([#8990](https://github.com/astral-sh/uv/pull/8990))
- Avoid creating `.venv` in `uv add --frozen` and `uv add --no-sync` ([#8980](https://github.com/astral-sh/uv/pull/8980))
- Avoid duplicating first-entry comments in `uv add` ([#9109](https://github.com/astral-sh/uv/pull/9109))
- Defer reporting of build failures in resolver ([#9098](https://github.com/astral-sh/uv/pull/9098))
- Fix references to `--resolution-strategy` in error message output ([#8971](https://github.com/astral-sh/uv/pull/8971))
- Ignore virtual environments in parent directories when choosing Python version for new projects ([#9075](https://github.com/astral-sh/uv/pull/9075))
- Forward SIGTERM to child processes in `uv run` ([#8933](https://github.com/astral-sh/uv/pull/8933))
- Prefer Python executable names that match the request over default names ([#9066](https://github.com/astral-sh/uv/pull/9066))
- Prefer compatible to incompatible distributions when packages exist on multiple indexes ([#8961](https://github.com/astral-sh/uv/pull/8961))
- Publish: Ignore non-matching files ([#8986](https://github.com/astral-sh/uv/pull/8986))
- Revert `uv.lock` changes when `uv add` fails ([#9030](https://github.com/astral-sh/uv/pull/9030))
- Show file extensions on available commands when not `.exe` ([#9099](https://github.com/astral-sh/uv/pull/9099))
- Sort by name, then specifiers in `uv add` ([#9097](https://github.com/astral-sh/uv/pull/9097))
- Split after specifiers in `--with` requirements ([#9089](https://github.com/astral-sh/uv/pull/9089))
- Support multiple extras in universal pip compile output ([#8960](https://github.com/astral-sh/uv/pull/8960))

### Preview features

- Build backend: Add tests for source tree -> source dist -> wheel conversions ([#9091](https://github.com/astral-sh/uv/pull/9091))
- Build backend: Switch to custom `glob-walkdir` implementation ([#9013](https://github.com/astral-sh/uv/pull/9013))
- Build backend: Add minimal wheel settings ([#9085](https://github.com/astral-sh/uv/pull/9085))

## 0.5.1

### Enhancements

- Allow installation of manylinux wheels on `riscv64` ([#8934](https://github.com/astral-sh/uv/pull/8934))

### Bug fixes

- Build source distributions at top-level of cache ([#8905](https://github.com/astral-sh/uv/pull/8905))
- Allow non-registry dependencies in `uv pip list --outdated` ([#8939](https://github.com/astral-sh/uv/pull/8939))
- Compute superset of existing and required hashes when healing cache ([#8955](https://github.com/astral-sh/uv/pull/8955))
- Enable uv to replace and delete itself on Windows ([#8914](https://github.com/astral-sh/uv/pull/8914))
- Remove source distribution filename from cache ([#8907](https://github.com/astral-sh/uv/pull/8907))
- Respect `--index-url` in `uv pip list` ([#8942](https://github.com/astral-sh/uv/pull/8942))
- Respect comma-separated extras in `--with` ([#8946](https://github.com/astral-sh/uv/pull/8946))
