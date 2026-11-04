# uv release notes, step 09: releases in the span `0.6.0 -> 0.7.0` (upstream tag `0.7.0`)

Source: astral-sh/uv at tag `0.7.0`, file(s) `CHANGELOG.md`, `changelogs/0.1.x.md`, `changelogs/0.2.x.md`, `changelogs/0.3.x.md`, `changelogs/0.4.x.md`, `changelogs/0.5.x.md`, `changelogs/0.6.x.md`. 18 release entries: 0.7.0, 0.6.17, 0.6.16, 0.6.15, 0.6.14, 0.6.13, 0.6.12, 0.6.11, 0.6.10, 0.6.9, 0.6.8, 0.6.7, 0.6.6, 0.6.5, 0.6.4, 0.6.3, 0.6.2, 0.6.1. Licence: MIT OR Apache-2.0 (astral-sh/uv; see SPECS/licences/uv-LICENSE-MIT and SPECS/licences/uv-LICENSE-APACHE).

Kept sections: Breaking changes, Enhancements, Bug fixes, CLI, Configuration, Error messages, Python, Python versions, Preview, Stabilizations, Deprecations, Compatibility, Security, plus each release's preamble. Dropped: Documentation, Performance, Release, Rust API, Rust libraries, Other changes, Distributions. Kept text is verbatim.

## 0.7.0

This release contains various changes that improve correctness and user experience, but could break some workflows; many changes have been marked as breaking out of an abundance of caution. We expect most users to be able to upgrade without making changes.

### Breaking changes

- **Update `uv version` to display and update project versions ([#12349](https://github.com/astral-sh/uv/pull/12349))**

  Previously, `uv version` displayed uv's version. Now, `uv version` will display or update the project's version. This interface was [heavily requested](https://github.com/astral-sh/uv/issues/6298) and, after much consideration, we decided that transitioning the top-level command was the best option.

  Here's a brief example:

  ```console
  $ uv init example
  Initialized project `example` at `./example`
  $ cd example
  $ uv version
  example 0.1.0
  $ uv version --bump major
  example 0.1.0 => 1.0.0
  $ uv version --short
  1.0.0
  ```

  If used outside of a project, uv will fallback to showing its own version still:

  ```console
  $ uv version
  warning: failed to read project: No `pyproject.toml` found in current directory or any parent directory
    running `uv self version` for compatibility with old `uv version` command.
    this fallback will be removed soon, pass `--preview` to make this an error.

  uv 0.7.0 (4433f41c9 2025-04-29)
  ```

  As described in the warning, `--preview` can be used to error instead:

  ```console
  $ uv version --preview
  error: No `pyproject.toml` found in current directory or any parent directory
  ```

  The previous functionality of `uv version` was moved to `uv self version`.

- **Avoid fallback to subsequent indexes on authentication failure ([#12805](https://github.com/astral-sh/uv/pull/12805))**

  When using the `first-index` strategy (the default), uv will stop searching indexes for a package once it is found on a single index. Previously, uv considered a package as "missing" from an index during authentication failures, such as an HTTP 401 or HTTP 403 (normally, missing packages are represented by an HTTP 404). This behavior was motivated by unusual responses from some package indexes, but reduces the safety of uv's index strategy when authentication fails. Now, uv will consider an authentication failure as a stop-point when searching for a package across indexes. The `index.ignore-error-codes` option can be used to recover the existing behavior, e.g.:

  ```toml
  [[tool.uv.index]]
  name = "pytorch"
  url = "https://download.pytorch.org/whl/cpu"
  ignore-error-codes = [401, 403]
  ```

  Since PyTorch's indexes always return a HTTP 403 for missing packages, uv special-cases indexes on the `pytorch.org` domain to ignore that error code by default.

- **Require the command in `uvx <name>` to be available in the Python environment ([#11603](https://github.com/astral-sh/uv/pull/11603))**

  Previously, `uvx` would attempt to execute a command even if it was not provided by a Python package. For example, if we presume `foo` is an empty Python package which provides no command, `uvx foo` would invoke the `foo` command on the `PATH` (if present). Now, uv will error early if the `foo` executable is not provided by the requested Python package. This check is not enforced when `--from` is used, so patterns like `uvx --from foo bash -c "..."` are still valid. uv also still allows `uvx foo` where the `foo` executable is provided by a dependency of `foo` instead of `foo` itself, as this is fairly common for packages which depend on a dedicated package for their command-line interface.

- **Use index URL instead of package URL for keyring credential lookups ([#12651](https://github.com/astral-sh/uv/pull/12651))**

  When determining credentials for querying a package URL, uv previously sent the full URL to the `keyring` command. However, some keyring plugins expect to receive the _index URL_ (which is usually a parent of the package URL). Now, uv requests credentials for the index URL instead. This behavior matches `pip`.

- **Remove `--version` from subcommands ([#13108](https://github.com/astral-sh/uv/pull/13108))**

  Previously, uv allowed the `--version` flag on arbitrary subcommands, e.g., `uv run --version`. However, the `--version` flag is useful for other operations since uv is a package manager. Consequently, we've removed the `--version` flag from subcommands — it is only available as `uv --version`.

- **Omit Python 3.7 downloads from managed versions ([#13022](https://github.com/astral-sh/uv/pull/13022))**

  Python 3.7 is EOL and not formally supported by uv; however, Python 3.7 was previously available for download on a subset of platforms.

- **Reject non-PEP 751 TOML files in install, compile, and export commands ([#13120](https://github.com/astral-sh/uv/pull/13120), [#13119](https://github.com/astral-sh/uv/pull/13119))**

  Previously, uv treated arbitrary `.toml` files passed to commands (e.g., `uv pip install -r foo.toml` or `uv pip compile -o foo.toml`) as `requirements.txt`-formatted files. Now, uv will error instead. If using PEP 751 lockfiles, use the standardized format for custom names instead, e.g., `pylock.foo.toml`.

- **Ignore arbitrary Python requests in version files ([#12909](https://github.com/astral-sh/uv/pull/12909))**

  uv allows arbitrary strings to be used for Python version requests, in which they are treated as an executable name to search for in the `PATH`. However, using this form of request in `.python-version` files is non-standard and conflicts with `pyenv-virtualenv` which writes environment names to `.python-version` files. In this release, uv will now ignore requests that are arbitrary strings when found in `.python-version` files.

- **Error on unknown dependency object specifiers ([12811](https://github.com/astral-sh/uv/pull/12811))**

  The `[dependency-groups]` entries can include "object specifiers", e.g. `set-phasers-to = ...` in:

  ```toml
  [dependency-groups]
  foo = ["pyparsing"]
  bar = [{set-phasers-to = "stun"}]
  ```

  However, the only current spec-compliant object specifier is `include-group`. Previously, uv would ignore unknown object specifiers. Now, uv will error.

- **Make `--frozen` and `--no-sources` conflicting options ([#12671](https://github.com/astral-sh/uv/pull/12671))**

  Using `--no-sources` always requires a new resolution and `--frozen` will always fail when used with it. Now, this conflict is encoded in the CLI options for clarity.

- **Treat empty `UV_PYTHON_INSTALL_DIR` and `UV_TOOL_DIR` as unset ([#12907](https://github.com/astral-sh/uv/pull/12907), [#12905](https://github.com/astral-sh/uv/pull/12905))**

  Previously, these variables were treated as set to the current working directory when set to an empty string. Now, uv will ignore these variables when empty. This matches uv's behavior for other environment variables which configure directories.

### Enhancements

- Disallow mixing requirements across PyTorch indexes ([#13179](https://github.com/astral-sh/uv/pull/13179))
- Add optional managed Python archive download cache ([#12175](https://github.com/astral-sh/uv/pull/12175))
- Add `poetry-core` as a `uv init` build backend option ([#12781](https://github.com/astral-sh/uv/pull/12781))
- Show tag hints when failing to find a compatible wheel in `pylock.toml` ([#13136](https://github.com/astral-sh/uv/pull/13136))
- Report Python versions in `pyvenv.cfg` version mismatch ([#13027](https://github.com/astral-sh/uv/pull/13027))

### Bug fixes

- Avoid erroring on omitted wheel-only packages in `pylock.toml` ([#13132](https://github.com/astral-sh/uv/pull/13132))
- Fix display name for `uvx --version` ([#13109](https://github.com/astral-sh/uv/pull/13109))
- Restore handling of authentication when encountering redirects ([#13050](https://github.com/astral-sh/uv/pull/13050))
- Respect build options (`--no-binary` et al) in `pylock.toml` ([#13134](https://github.com/astral-sh/uv/pull/13134))
- Use `upload-time` rather than `upload_time` in `uv.lock` ([#13176](https://github.com/astral-sh/uv/pull/13176))

## 0.6.17

### Preview features

- Add PyTorch v2.7.0 to GPU backend ([#13072](https://github.com/astral-sh/uv/pull/13072))

### Bug fixes

- Avoid panic for invalid Python versions ([#13077](https://github.com/astral-sh/uv/pull/13077))
- Block scripts from overwriting `python` ([#13051](https://github.com/astral-sh/uv/pull/13051))
- Check distribution names to handle invalid redirects
  ([#12917](https://github.com/astral-sh/uv/pull/12917))
- Check for mismatched package and distribution names on resolver thread
  ([#13088](https://github.com/astral-sh/uv/pull/13088))
- Fix panic with invalid last character in PEP 508 name
  ([#13105](https://github.com/astral-sh/uv/pull/13105))
- Reject `requires-python` even if not listed on the index page
  ([#13086](https://github.com/astral-sh/uv/pull/13086))

## 0.6.16

### Bug fixes

- Revert "Properly handle authentication for 302 redirect URLs"
  ([#13041](https://github.com/astral-sh/uv/pull/13041))

## 0.6.15

This release includes preliminary support for the `pylock.toml` file format, as standardized in
[PEP 751](https://peps.python.org/pep-0751/). `pylock.toml` is an alternative resolution output
format intended to replace `requirements.txt` (e.g., in the context of `uv pip compile`, whereby a
"locked" `requirements.txt` file is generated from a set of input requirements). `pylock.toml` is
standardized and tool-agnostic, such that in the future, `pylock.toml` files generated by uv could
be installed by other tools, and vice versa.

As of this release, `pylock.toml` is supported in the following commands:

- To export a `uv.lock` to the `pylock.toml` format, run: `uv export -o pylock.toml`
- To generate a `pylock.toml` file from a set of requirements, run:
  `uv pip compile -o pylock.toml requirements.in`
- To install from a `pylock.toml` file, run: `uv pip sync pylock.toml` or
  `uv pip install -r pylock.toml`

### Enhancements

- Add PEP 751 support to `uv pip compile` ([#13019](https://github.com/astral-sh/uv/pull/13019))
- Add `uv export` support for PEP 751 ([#12955](https://github.com/astral-sh/uv/pull/12955))
- Accept `requirements.txt` (verbatim) as a format on the CLI
  ([#12957](https://github.com/astral-sh/uv/pull/12957))
- Add `UV_NO_EDITABLE` environment variable to set `--no-editable` on all invocations
  ([#12773](https://github.com/astral-sh/uv/pull/12773))
- Add `pylock.toml` to `uv pip install` and `uv pip sync`
  ([#12992](https://github.com/astral-sh/uv/pull/12992))
- Add a brief sleep before sending `SIGINT` to child processes
  ([#13018](https://github.com/astral-sh/uv/pull/13018))
- Add upload time to `uv.lock` ([#12968](https://github.com/astral-sh/uv/pull/12968))
- Allow updating Git sources by name ([#12897](https://github.com/astral-sh/uv/pull/12897))
- Cache `which git` in `uv init` ([#12893](https://github.com/astral-sh/uv/pull/12893))
- Enable `--dry-run` with `--locked` / `--frozen` for `uv sync`
  ([#12778](https://github.com/astral-sh/uv/pull/12778))
- Infer output type in `uv export` ([#12958](https://github.com/astral-sh/uv/pull/12958))
- Make `uv init` resilient against broken git ([#12895](https://github.com/astral-sh/uv/pull/12895))
- Respect build constraints for `uv run --with` dependencies
  ([#12882](https://github.com/astral-sh/uv/pull/12882))
- Split UV_INDEX on all whitespace ([#12820](https://github.com/astral-sh/uv/pull/12820))
- Support build constraints in `uv tool` and PEP723 scripts.
  ([#12842](https://github.com/astral-sh/uv/pull/12842))
- Use suffix from `uvx` binary when searching for uv binary
  ([#12923](https://github.com/astral-sh/uv/pull/12923))
- Update version formatting to use cyan color ([#12943](https://github.com/astral-sh/uv/pull/12943))
- Add debug logs for version file search ([#12951](https://github.com/astral-sh/uv/pull/12951))
- Fix `SourceNotAllowed` error message during Python discovery
  ([#13012](https://github.com/astral-sh/uv/pull/13012))
- Obfuscate password in credentials debug messages
  ([#12944](https://github.com/astral-sh/uv/pull/12944))
- Obfuscate possible tokens in URL logs ([#12969](https://github.com/astral-sh/uv/pull/12969))
- Validate that PEP 751 entries don't include multiple sources
  ([#12993](https://github.com/astral-sh/uv/pull/12993))

### Preview features

- Build backend: Add reference docs and schema
  ([#12803](https://github.com/astral-sh/uv/pull/12803))

### Bug fixes

- Align supported `config-settings` with example in docs
  ([#12947](https://github.com/astral-sh/uv/pull/12947))
- Ensure virtual environment is compatible with interpreter on sync
  ([#12884](https://github.com/astral-sh/uv/pull/12884))
- Fix `PythonDownloadRequest` parsing for partial keys
  ([#12925](https://github.com/astral-sh/uv/pull/12925))
- Fix pre-release exclusive comparison operator in `uv-pep440`
  ([#12836](https://github.com/astral-sh/uv/pull/12836))
- Forward additional signals to the child process in `uv run`
  ([#13017](https://github.com/astral-sh/uv/pull/13017))
- Omit PEP 751 version for source trees ([#13030](https://github.com/astral-sh/uv/pull/13030))
- Patch `CC` and `CCX` entries in sysconfig for cross-compiled `aarch64` Python distributions
  ([#12239](https://github.com/astral-sh/uv/pull/12239))
- Properly handle authentication for HTTP 302 redirect URLs
  ([#12920](https://github.com/astral-sh/uv/pull/12920))
- Set 4MB stack size for all threads, introduce `UV_STACK_SIZE`
  ([#12839](https://github.com/astral-sh/uv/pull/12839))
- Show PyPy downloads during `uv python list` ([#12915](https://github.com/astral-sh/uv/pull/12915))
- Add `subdirectory` to Direct URL for local directories
  ([#12971](https://github.com/astral-sh/uv/pull/12971))
- Prefer stable releases over pre-releases in `uv python install`
  ([#12194](https://github.com/astral-sh/uv/pull/12194))
- Write requested Python variant to pin file in `uv init`
  ([#12870](https://github.com/astral-sh/uv/pull/12870))

## 0.6.14

### Python versions

The following Python versions have been added:

- CPython 3.13.3
- CPython 3.12.10
- CPython 3.11.12
- CPython 3.10.17
- CPython 3.9.22

See the
[`python-build-standalone` release notes](https://github.com/astral-sh/python-build-standalone/releases/tag/20250409)
for more details.

### Enhancements

- Add `uv-build` and `uv_build` aliases to `uv init --build-backend`
  ([#12776](https://github.com/astral-sh/uv/pull/12776))
- Emit dedicated error message for Conda `environment.yml` files
  ([#12669](https://github.com/astral-sh/uv/pull/12669))

### Preview features

- Build backend: Check module dir exists for sdist build
  ([#12779](https://github.com/astral-sh/uv/pull/12779))
- Build backend: Fix sdist with long directories
  ([#12764](https://github.com/astral-sh/uv/pull/12764))

### Bug fixes

- Error when `tool.uv.sources` is set in system-level configuration file
  ([#12757](https://github.com/astral-sh/uv/pull/12757))
- Split workspace members onto their own lines in `uv init`
  ([#12756](https://github.com/astral-sh/uv/pull/12756))

## 0.6.13

### Enhancements

- Add `--show-version` to `uv python find` ([#12376](https://github.com/astral-sh/uv/pull/12376))
- Remove `--no-config` warning from `uv pip compile` and `uv pip sync`
  ([#12642](https://github.com/astral-sh/uv/pull/12642))
- Skip repeated directories in `PATH` when searching for Python interpreters
  ([#12367](https://github.com/astral-sh/uv/pull/12367))
- Unset `SCRIPT_PATH` in relocatable activation script
  ([#12672](https://github.com/astral-sh/uv/pull/12672))
- Add `UV_PYTHON_DOWNLOADS_JSON_URL` to set custom managed python sources
  ([#10939](https://github.com/astral-sh/uv/pull/10939))
- Reject `pyproject.toml` files in `uv pip compile -o`
  ([#12673](https://github.com/astral-sh/uv/pull/12673))
- Respect the `--offline` flag for Git operations
  ([#12619](https://github.com/astral-sh/uv/pull/12619))

### Bug fixes

- Warn instead of error if CRC appears to be missing
  ([#12722](https://github.com/astral-sh/uv/pull/12722))
- Avoid infinite loop in `uv export` with conflicts
  ([#12726](https://github.com/astral-sh/uv/pull/12726))

## 0.6.12

### Enhancements

- Report the queried executable path in `uv python list`
  ([#12628](https://github.com/astral-sh/uv/pull/12628))
- Improve archive unpack error messages ([#12627](https://github.com/astral-sh/uv/pull/12627))

### Bug fixes

- Respect `authenticate` when using `explicit = true`
  ([#12631](https://github.com/astral-sh/uv/pull/12631))
- Normalize extra and group names in `uv add` and `uv remove`
  ([#12586](https://github.com/astral-sh/uv/pull/12586))
- Enforce CRC-32 checks when unpacking archives
  ([#12623](https://github.com/astral-sh/uv/pull/12623))
- Fix parsing of `python-platform` in settings files
  ([#12592](https://github.com/astral-sh/uv/pull/12592))

## 0.6.11

### Enhancements

- Add dependents ("via ..." comments) in `uv export` command
  ([#12350](https://github.com/astral-sh/uv/pull/12350))
- Bump least-recent non-EOL macOS version to 13.0
  ([#12518](https://github.com/astral-sh/uv/pull/12518))
- Support `--find-links`-style "flat" indexes in `[[tool.uv.index]]`
  ([#12407](https://github.com/astral-sh/uv/pull/12407))
- Distinguish between `-q` and `-qq` ([#12300](https://github.com/astral-sh/uv/pull/12300))

### Configuration

- Support `UV_PROJECT` environment to set project directory.
  ([#12327](https://github.com/astral-sh/uv/pull/12327))

### Bug fixes

- Add a newline after metadata when initializing scripts with other metadata blocks
  ([#12501](https://github.com/astral-sh/uv/pull/12501))
- Avoid writing empty `requires-python` to script blocks
  ([#12517](https://github.com/astral-sh/uv/pull/12517))
- Respect build constraints in `uv sync` ([#12502](https://github.com/astral-sh/uv/pull/12502))
- Respect transitive dependencies in `uv tree --only-group`
  ([#12560](https://github.com/astral-sh/uv/pull/12560))

## 0.6.10

### Enhancements

- Add `uv sync --check` flag ([#12342](https://github.com/astral-sh/uv/pull/12342))
- Add support for Python version requests in `uv python list`
  ([#12375](https://github.com/astral-sh/uv/pull/12375))
- Support `.env` files in `uv tool run` ([#12386](https://github.com/astral-sh/uv/pull/12386))
- Support `python find --script` ([#11891](https://github.com/astral-sh/uv/pull/11891))

### Preview features

- Check all compatible torch indexes when `--torch-backend` is enabled
  ([#12385](https://github.com/astral-sh/uv/pull/12385))

### Bug fixes

- Allow virtual packages with `--no-build` ([#12314](https://github.com/astral-sh/uv/pull/12314))
- Ignore `--find-links` entries for pinned indexes
  ([#12396](https://github.com/astral-sh/uv/pull/12396))
- Omit wheels from lockfile based on `--exclude-newer`
  ([#12299](https://github.com/astral-sh/uv/pull/12299))
- Retain end-of-line comment position when adding dependency
  ([#12360](https://github.com/astral-sh/uv/pull/12360))
- Omit fragment when querying for wheels in Simple HTML API
  ([#12384](https://github.com/astral-sh/uv/pull/12384))
- Error on missing argument in `requirements.txt`
  ([#12354](https://github.com/astral-sh/uv/pull/12354))
- Support modules with different casing in build backend
  ([#12240](https://github.com/astral-sh/uv/pull/12240))
- Add authentication policy support for `pip` commands
  ([#12470](https://github.com/astral-sh/uv/pull/12470))

## 0.6.9

### Enhancements

- Use `keyring --mode creds` when `authenticate = "always"`
  ([#12316](https://github.com/astral-sh/uv/pull/12316))
- Fail with specific error message when no password is present and `authenticate = "always"`
  ([#12313](https://github.com/astral-sh/uv/pull/12313))

### Bug fixes

- Add boolish value parser for `UV_MANAGED_PYTHON` flags
  ([#12345](https://github.com/astral-sh/uv/pull/12345))
- Make deserialization non-fatal when assessing source tree revisions
  ([#12319](https://github.com/astral-sh/uv/pull/12319))
- Use resolver-returned wheel over alternate cached wheel
  ([#12301](https://github.com/astral-sh/uv/pull/12301))

### Preview changes

- Automatically infer the PyTorch index via `--torch-backend=auto`
  ([#12070](https://github.com/astral-sh/uv/pull/12070))

## 0.6.8

### Enhancements

- Add support for enabling all groups by default with `default-groups = "all"`
  ([#12289](https://github.com/astral-sh/uv/pull/12289))
- Add simpler `--managed-python` and `--no-managed-python` flags for toggling Python preferences
  ([#12246](https://github.com/astral-sh/uv/pull/12246))

### Bug fixes

- Allow local version mismatches when validating lockfile
  ([#12285](https://github.com/astral-sh/uv/pull/12285))
- Allow owned string when deserializing `requires-python`
  ([#12278](https://github.com/astral-sh/uv/pull/12278))
- Make cache errors non-fatal in `Planner::build`
  ([#12281](https://github.com/astral-sh/uv/pull/12281))

## 0.6.7

### Python

- Add CPython 3.14.0a6
- Fix regression where extension modules would use wrong `CXX` compiler on Linux
- Enable FTS3 enhanced query syntax for SQLite

See the
[`python-build-standalone` release notes](https://github.com/astral-sh/python-build-standalone/releases/tag/20250317)
for more details.

### Enhancements

- Add support for `-c` constraints in `uv add`
  ([#12209](https://github.com/astral-sh/uv/pull/12209))
- Add support for `--global` default version in `uv python pin`
  ([#12115](https://github.com/astral-sh/uv/pull/12115))
- Always reinstall local source trees passed to `uv pip install`
  ([#12176](https://github.com/astral-sh/uv/pull/12176))
- Render token claims on publish permission error
  ([#12135](https://github.com/astral-sh/uv/pull/12135))
- Add pip-compatible `--group` flag to `uv pip install` and `uv pip compile`
  ([#11686](https://github.com/astral-sh/uv/pull/11686))

### Preview features

- Avoid creating duplicate directory entries in built wheels
  ([#12206](https://github.com/astral-sh/uv/pull/12206))
- Allow overriding module names for editable builds
  ([#12137](https://github.com/astral-sh/uv/pull/12137))

### Bug fixes

- Add `src` to default cache keys ([#12062](https://github.com/astral-sh/uv/pull/12062))
- Discard insufficient fork markers ([#10682](https://github.com/astral-sh/uv/pull/10682))
- Ensure `python pin --global` creates parent directories if missing
  ([#12180](https://github.com/astral-sh/uv/pull/12180))
- Fix GraalPy abi tag parsing and discovery ([#12154](https://github.com/astral-sh/uv/pull/12154))
- Remove extraneous script packages in `uv sync --script`
  ([#12158](https://github.com/astral-sh/uv/pull/12158))
- Remove redundant `activate.bat` output ([#12160](https://github.com/astral-sh/uv/pull/12160))
- Avoid subsequent index hint when no versions are available on the first index
  ([#9332](https://github.com/astral-sh/uv/pull/9332))
- Error on lockfiles with incoherent wheel versions
  ([#12235](https://github.com/astral-sh/uv/pull/12235))

## 0.6.6

### Python

- Add support for dynamic musl Python distributions on x86-64 Linux
  ([#12121](https://github.com/astral-sh/uv/pull/12121))
- Allow the experimental JIT to be enabled at runtime on Python 3.13 and 3.14 on Linux
- Upgrade the build toolchain to LLVM 20, improving performance

See the
[`python-build-standalone` release notes](https://github.com/astral-sh/python-build-standalone/releases/tag/20250311)
for more details.

### Enhancements

- Add `--marker` flag to `uv add` ([#12012](https://github.com/astral-sh/uv/pull/12012))
- Allow overriding module name for uv build backend
  ([#11884](https://github.com/astral-sh/uv/pull/11884))
- Sync latest Python releases ([#12120](https://github.com/astral-sh/uv/pull/12120))
- Use 'Upload' instead of 'Download' in publish reporter
  ([#12029](https://github.com/astral-sh/uv/pull/12029))
- Add `[index].authenticate` allowing authentication to be required on an index
  ([#11896](https://github.com/astral-sh/uv/pull/11896))
- Add support for Windows legacy scripts in `uv tool run`
  ([#12079](https://github.com/astral-sh/uv/pull/12079))
- Propagate conflicting dependency groups when using `include-group`
  ([#12005](https://github.com/astral-sh/uv/pull/12005))
- Show ambiguous requirements when `uv add` failed
  ([#12106](https://github.com/astral-sh/uv/pull/12106))

### Bug fixes

- Avoid considering `PATH` updated when the `export` is commented in the shellrc
  ([#12043](https://github.com/astral-sh/uv/pull/12043))
- Fix `uv publish` retry on network failures ([#12041](https://github.com/astral-sh/uv/pull/12041))
- Use a sized stream in `uv publish` to comply with WSGI PyPI server constraints
  ([#12111](https://github.com/astral-sh/uv/pull/12111))
- Fix `uv python install --reinstall` when the version was not previously installed
  ([#12124](https://github.com/astral-sh/uv/pull/12124))

### Preview features

- Fix `uv_build` invocation ([#12058](https://github.com/astral-sh/uv/pull/12058))

## 0.6.5

### Enhancements

- Allow `--constraints` and `--overrides` in `uvx`
  ([#10207](https://github.com/astral-sh/uv/pull/10207))
- Allow overrides in `satisfies` check for `uv tool run`
  ([#11994](https://github.com/astral-sh/uv/pull/11994))
- Allow users to set `package = true` on `tool.uv.sources`
  ([#12014](https://github.com/astral-sh/uv/pull/12014))
- Add support for Windows legacy scripts via `uv run`
  ([#11888](https://github.com/astral-sh/uv/pull/11888))
- Return error when running uvx with a `.py` script
  ([#11623](https://github.com/astral-sh/uv/pull/11623))
- Warn user on use of `uvx run` ([#11992](https://github.com/astral-sh/uv/pull/11992))

### Configuration

- Add `NO_BUILD` and `NO_BUILD_PACKAGE` environment variables
  ([#11968](https://github.com/astral-sh/uv/pull/11968))

### Bug fixes

- Compare major-minor specifiers when filtering interpreters
  ([#11952](https://github.com/astral-sh/uv/pull/11952))
- Fix system site packages detection default ([#11956](https://github.com/astral-sh/uv/pull/11956))
- Invalidate lockfile when empty dependency groups are added or removed
  ([#12010](https://github.com/astral-sh/uv/pull/12010))
- Remove prepended sys.path ([#11954](https://github.com/astral-sh/uv/pull/11954))
- Fix PyPy Python version label ([#11965](https://github.com/astral-sh/uv/pull/11965))
- Fix error message suggesting `--user` instead of `--username`
  ([#11947](https://github.com/astral-sh/uv/pull/11947))

### Preview

- Move the uv build backend into a separate, minimal `uv_build` package
  ([#11446](https://github.com/astral-sh/uv/pull/11446))

## 0.6.4

### Enhancements

- Upgrade pypy3.10 to v7.3.19 ([#11814](https://github.com/astral-sh/uv/pull/11814))
- Allow configuring log verbosity from the CLI (i.e., `-vvv`)
  ([#11758](https://github.com/astral-sh/uv/pull/11758))
- Warn when duplicate index names found in single file
  ([#11824](https://github.com/astral-sh/uv/pull/11824))

### Bug fixes

- Always store registry index on resolution packages
  ([#11815](https://github.com/astral-sh/uv/pull/11815))
- Avoid error on relative paths in `uv tool uninstall`
  ([#11889](https://github.com/astral-sh/uv/pull/11889))
- Avoid silently dropping errors in directory enumeration
  ([#11890](https://github.com/astral-sh/uv/pull/11890))
- Disable interactive git terminal prompts during fetches
  ([#11744](https://github.com/astral-sh/uv/pull/11744))
- Discover Windows registry (PEP 514) Python versions across 32/64-bit
  ([#11801](https://github.com/astral-sh/uv/pull/11801))
- Don't panic on Ctrl-C in confirm prompt ([#11706](https://github.com/astral-sh/uv/pull/11706))
- Fix non-directory in workspace on Windows ([#11833](https://github.com/astral-sh/uv/pull/11833))
- Make interpreter caching robust to OS upgrades
  ([#11875](https://github.com/astral-sh/uv/pull/11875))
- Respect `include-system-site-packages` in layered environments
  ([#11873](https://github.com/astral-sh/uv/pull/11873))
- Suggest `uv tool update-shell` in PowerShell
  ([#11846](https://github.com/astral-sh/uv/pull/11846))
- Update code page to `65001` before setting environment variables in virtual environments
  ([#11831](https://github.com/astral-sh/uv/pull/11831))
- Use hash instead of full wheel name in wheels bucket
  ([#11738](https://github.com/astral-sh/uv/pull/11738))
- Fix version string truncation while generating cache_key
  ([#11830](https://github.com/astral-sh/uv/pull/11830))
- Explicitly handle ctrl-c in confirmation prompt instead of using a signal handler
  ([#11897](https://github.com/astral-sh/uv/pull/11897))

## 0.6.3

### Enhancements

- Allow quotes around command-line options in `requirement.txt files`
  ([#11644](https://github.com/astral-sh/uv/pull/11644))
- Initialize PEP 723 script in `uv lock --script`
  ([#11717](https://github.com/astral-sh/uv/pull/11717))

### Configuration

- Accept multiple `.env` files in `UV_ENV_FILE`
  ([#11665](https://github.com/astral-sh/uv/pull/11665))

### Bug fixes

- Avoid installing duplicate dependencies across conflicting groups
  ([#11653](https://github.com/astral-sh/uv/pull/11653))
- Check subdirectory existence after cache heal
  ([#11719](https://github.com/astral-sh/uv/pull/11719))
- Include uppercase platforms for Windows wheels
  ([#11681](https://github.com/astral-sh/uv/pull/11681))
- Respect existing PEP 723 script settings in `uv add`
  ([#11716](https://github.com/astral-sh/uv/pull/11716))
- Reuse refined interpreter to create tool environment
  ([#11680](https://github.com/astral-sh/uv/pull/11680))
- Skip removed directories during bytecode compilation
  ([#11633](https://github.com/astral-sh/uv/pull/11633))
- Support conflict markers in `uv export` ([#11643](https://github.com/astral-sh/uv/pull/11643))
- Treat lockfile as outdated if (empty) extras are added
  ([#11702](https://github.com/astral-sh/uv/pull/11702))
- Display path separators as backslashes on Windows
  ([#11667](https://github.com/astral-sh/uv/pull/11667))
- Display the built file name instead of the canonicalized name in `uv build`
  ([#11593](https://github.com/astral-sh/uv/pull/11593))
- Fix message when there are no buildable packages
  ([#11722](https://github.com/astral-sh/uv/pull/11722))
- Re-allow HTTP schemes for Git dependencies ([#11687](https://github.com/astral-sh/uv/pull/11687))

## 0.6.2

### Enhancements

- Add support for constraining build dependencies with `tool.uv.build-constraint-dependencies`
  ([#11585](https://github.com/astral-sh/uv/pull/11585))
- Sort dependency group keys when adding new group
  ([#11591](https://github.com/astral-sh/uv/pull/11591))

### Bug fixes

- Allow use of x86-64 Python on ARM Windows ([#11625](https://github.com/astral-sh/uv/pull/11625))
- Fix an issue where conflict markers could instigate a very large lock file
  ([#11293](https://github.com/astral-sh/uv/pull/11293))
- Fix duplicate packages with multiple conflicting extras declared
  ([#11513](https://github.com/astral-sh/uv/pull/11513))
- Respect color settings for log messages ([#11604](https://github.com/astral-sh/uv/pull/11604))
- Eagerly reject unsupported Git schemes ([#11514](https://github.com/astral-sh/uv/pull/11514))

## 0.6.1

### Enhancements

- Allow users to mark platforms as "required" for wheel coverage
  ([#10067](https://github.com/astral-sh/uv/pull/10067))
- Warn for builds in non-build and workspace root pyproject.toml
  ([#11394](https://github.com/astral-sh/uv/pull/11394))

### Bug fixes

- Add `--all` to `uvx --reinstall` message ([#11535](https://github.com/astral-sh/uv/pull/11535))
- Fallback to `GET` on HTTP 400 when attempting to use range requests for wheel download
  ([#11539](https://github.com/astral-sh/uv/pull/11539))
- Prefer local variants in preference selection
  ([#11546](https://github.com/astral-sh/uv/pull/11546))
- Respect verbatim executable name in `uvx` ([#11524](https://github.com/astral-sh/uv/pull/11524))
