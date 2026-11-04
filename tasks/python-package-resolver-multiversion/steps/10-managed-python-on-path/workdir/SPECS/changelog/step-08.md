# uv release notes, step 08: releases in the span `0.5.20 -> 0.6.0` (upstream tag `0.6.0`)

Source: astral-sh/uv at tag `0.6.0`, file(s) `CHANGELOG.md`. 12 release entries: 0.6.0, 0.5.31, 0.5.30, 0.5.29, 0.5.28, 0.5.27, 0.5.26, 0.5.25, 0.5.24, 0.5.23, 0.5.22, 0.5.21. Licence: MIT OR Apache-2.0 (astral-sh/uv; see SPECS/licences/uv-LICENSE-MIT and SPECS/licences/uv-LICENSE-APACHE).

Kept sections: Breaking changes, Enhancements, Bug fixes, CLI, Configuration, Error messages, Python, Python versions, Preview, Stabilizations, Deprecations, Compatibility, Security, plus each release's preamble. Dropped: Documentation, Performance, Release, Rust API, Rust libraries, Other changes, Distributions. Kept text is verbatim.

## 0.6.0

There have been 31 releases and 1135 pull requests since [0.5.0](https://github.com/astral-sh/uv/releases/tag/0.5.0), our last release with breaking changes. As before, we've accumulated various changes that improve correctness and user experience, but could break some workflows. This release contains those changes; many have been marked as breaking out of an abundance of caution. We expect most users to be able to upgrade without making changes.

### Breaking changes

- **Create `main.py` instead of `hello.py` in `uv init`** ([#10369](https://github.com/astral-sh/uv/pull/10369))

  Previously, `uv init` created a `hello.py` sample file. Now, `uv init` will create `main.py` instead — which aligns with expectations from user feedback. The `--bare` option can be used to avoid creating the file altogether.

- **Respect `UV_PYTHON` in `uv python install`** ([#11487](https://github.com/astral-sh/uv/pull/11487))

  Previously, `uv python install` did not read this environment variable; now it does. We believe this matches user expectations, however, this will take priority over `.python-version` files which could be considered breaking.

- **Set `UV` to the uv executable path** ([#11326](https://github.com/astral-sh/uv/pull/11326))

  When uv spawns a subprocess, it will now have the `UV` environment variable set to the `uv` binary path. This change is breaking if you are setting the `UV` environment variable yourself, as we will overwrite its value.

  Additionally, this change requires marking the uv Rust entrypoint (`uv::main`) as `unsafe` to avoid unsoundness — this is only relevant if you are invoking uv using Rust. See the [Rust documentation](https://doc.rust-lang.org/std/env/fn.set_var.html#safety) for details about the safety of updating a process' environment.

- **Error on non-existent extras, e.g., in `uv sync`** ([#11426](https://github.com/astral-sh/uv/pull/11426))

  Previously, uv would silently ignore non-existent extras requested on the command-line (e.g., via `uv sync --extra foo`). This is _generally_ correct behavior when resolving requests for package extras, because an extra may be present on one compatible version of a package but not another. However, this flexibility doesn't need to apply to the local project and it's less surprising to error here.

- **Error on missing dependency groups when `--frozen` is provided** ([#11499](https://github.com/astral-sh/uv/pull/11499))

  Previously, uv would not validate that the requested dependency groups were present in the lockfile when the `--frozen` flag was used. Now, an error will be raised if a requested dependency group is not present.

- **Change `-p` to a `--python` alias in `uv pip compile`** ([#11486](https://github.com/astral-sh/uv/pull/11486))

  In `uv pip compile`, `-p` was an alias for `--python-version` while everywhere else in uv's interface it is an alias for `--python`. Additionally, `uv pip compile` did not respect the `UV_PYTHON` environment variable. Now, the semantics of this flag have been updated for parity with the rest of the CLI. 
  
  However, `--python-version` is unique: if we cannot find an interpreter with the given version, we will not fail. Instead, we'll use an alternative interpreter and override its version tags with the requested version during package resolution. This behavior is retained here for backwards compatibility, `--python <version>` / `-p <version>` will not fail if the version cannot be found. However, if a specific interpreter is requested, e.g., with `--python <path>` or `--python pypy`, and cannot be found — uv will exit with an error.
  
  The breaking changes here are that `UV_PYTHON` is respected and `--python <version>` will no longer fail if the version cannot be found.

- **Bump `alpine` default tag to 3.21 for derived Docker images** ([#11157](https://github.com/astral-sh/uv/pull/11157))

  Alpine 3.21 was released in Dec 2024 and is used in the official Alpine-based Python images. Our `uv:python3.x-alpine` images have been using 3.21 since uv v0.5.8. However, now the the `uv:alpine` image will use 3.21 instead of 3.20 and `uv:alpine3.20` will no longer be updated.

- **Use files instead of junctions on Windows** ([#11269](https://github.com/astral-sh/uv/pull/11269))

  Previously, we used junctions for atomic replacement of cache entries on Windows. Now, we use a file with a pointer to the cache entry instead. This resolves various edge-case behaviors with junctions. These files are only intended to be consumed by uv and the cache version has been bumped. We do not think this change will affect workflows.

### Stabilizations

- **`uv publish` is no longer in preview** ([#11032](https://github.com/astral-sh/uv/pull/11032))

  This does not come with any behavior changes. You will no longer see an experimental warning when using `uv publish`. See the linked pull request for a report on the stabilization.

### Enhancements

- Support `--active` for PEP 723 script environments ([#11433](https://github.com/astral-sh/uv/pull/11433))
- Add `revision` to the lockfile to allow backwards-compatible metadata changes ([#11500](https://github.com/astral-sh/uv/pull/11500))

### Bug fixes

- Avoid reading metadata from `.egg-info` files ([#11395](https://github.com/astral-sh/uv/pull/11395))
- Include archive bucket version in archive pointers ([#11306](https://github.com/astral-sh/uv/pull/11306))
- Omit lockfile version when additional fields are dynamic ([#11468](https://github.com/astral-sh/uv/pull/11468))
- Respect executable name in `uvx --from tool@latest` ([#11465](https://github.com/astral-sh/uv/pull/11465))

## 0.5.31

### Enhancements

- Add `uv sync --script` ([#11361](https://github.com/astral-sh/uv/pull/11361))
- Allow PEP 508 requirements in tool requests ([#11337](https://github.com/astral-sh/uv/pull/11337))
- Allow source distributions to produce wheels with `+local` suffixes ([#11429](https://github.com/astral-sh/uv/pull/11429))
- Bring parity to `uvx` and `uv tool install` requests ([#11345](https://github.com/astral-sh/uv/pull/11345))
- Use a stable directory for local, remote, and stdin script virtual environments ([#11347](https://github.com/astral-sh/uv/pull/11347), [#11364](https://github.com/astral-sh/uv/pull/11364))
- Detect infinite recursion in `uv run` ([#11386](https://github.com/astral-sh/uv/pull/11386))

### Python

The managed Python distributions have been updated, including:

- CPython 3.14.0a5, which includes a new [tail calling interpreter](https://docs.python.org/3.14/whatsnew/3.14.html#whatsnew314-tail-call) for a significant performance improvement
- The bundled OpenSSL version was updated from 3.0.15 to 3.0.16 which fixes a [security advisory](https://openssl-library.org/news/secadv/20241016.txt)

See the [`python-build-standalone` release notes](https://github.com/astral-sh/python-build-standalone/releases/tag/20250212) for more details.

### Bug fixes

- Fix cross-drive script installation ([#11167](https://github.com/astral-sh/uv/pull/11167))
- Add indexes in priority order ([#11451](https://github.com/astral-sh/uv/pull/11451))
- Allow `--python <dir>` requests to match existing environments if `sys.executable` is the same file ([#11290](https://github.com/astral-sh/uv/pull/11290))
- Avoid comparing to system site packages in `--dry-run` mode ([#11427](https://github.com/astral-sh/uv/pull/11427))
- Prefer running executables in the environment with `<name>` over `<name>/__main__.py` ([#11431](https://github.com/astral-sh/uv/pull/11431))
- Retry local clones without hardlinks if they fail ([#11421](https://github.com/astral-sh/uv/pull/11421))

## 0.5.30

### Python

The managed PyPy distributions have been updated for PyPy v7.3.18, which includes:

- PyPy3.10, which updates the standard library from Python 3.10.14 to 3.10.19
- PyPy3.11, which adds beta support for Python 3.11.11

See the [PyPy release](https://pypy.org/posts/2025/02/pypy-v7318-release.html) for more details.

### Enhancements

- Add `uv sync --dry-run` ([#11299](https://github.com/astral-sh/uv/pull/11299))
- Ignore `#egg` fragment in HTML Simple API response ([#11340](https://github.com/astral-sh/uv/pull/11340))

### Configuration

- Add `NO_BINARY` and `NO_BINARY_PACKAGE` environment variables ([#11399](https://github.com/astral-sh/uv/pull/11399))

### Bug fixes

- Allow dynamic packages to be overloaded ([#11400](https://github.com/astral-sh/uv/pull/11400))
- Fix credential caching for index roots when URL ends in `simple/` ([#11336](https://github.com/astral-sh/uv/pull/11336))
- Fix marker merging for requirements.txt for psycopg ([#11298](https://github.com/astral-sh/uv/pull/11298))
- Set 777 permissions on locked files ([#11328](https://github.com/astral-sh/uv/pull/11328))
- Support extras in `@` requests for tools ([#11335](https://github.com/astral-sh/uv/pull/11335))
- Upgrade `astral-tokio-tar` to v0.5.1 ([#11359](https://github.com/astral-sh/uv/pull/11359))
- Avoid missing logging for no-op upgrade events ([#11301](https://github.com/astral-sh/uv/pull/11301))
- Use refined specifiers when logging narrowed Python range ([#11334](https://github.com/astral-sh/uv/pull/11334))
- Don't use popup-generating `eprintln` in trampoline warnings ([#11295](https://github.com/astral-sh/uv/pull/11295))
- Patch pkg-config files to be relocatable ([#11291](https://github.com/astral-sh/uv/pull/11291))
- Fix a case of duplicate `torch` packages when using conflicting extras ([#11323](https://github.com/astral-sh/uv/pull/11323))

## 0.5.29

### Enhancements

- Add `--bare` option to `uv init` ([#11192](https://github.com/astral-sh/uv/pull/11192))
- Add support for respecting `VIRTUAL_ENV` in project commands via `--active` ([#11189](https://github.com/astral-sh/uv/pull/11189))
- Allow the project `VIRTUAL_ENV` warning to be silenced with `--no-active` ([#11251](https://github.com/astral-sh/uv/pull/11251))

### Python

The managed Python distributions have been updated, including:

- CPython 3.12.9
- CPython 3.13.2
- pkg-config files are now relocatable

See the [`python-build-standalone` release notes](https://github.com/astral-sh/python-build-standalone/releases/tag/20250205) for more details.

### Bug fixes

- Always use base Python discovery logic for cached environments ([#11254](https://github.com/astral-sh/uv/pull/11254))
- Use a flock to avoid concurrent initialization of project environments ([#11259](https://github.com/astral-sh/uv/pull/11259))
- Fix handling of `--all-groups` and `--no-default-groups` flags ([#11224](https://github.com/astral-sh/uv/pull/11224))

## 0.5.28

### Bug fixes

- Allow discovering virtual environments from the first interpreter found on the `PATH` ([#11218](https://github.com/astral-sh/uv/pull/11218))
- Clear ephemeral overlays when running tools ([#11141](https://github.com/astral-sh/uv/pull/11141))
- Disable SSL in Git commands for `--allow-insecure-host` ([#11210](https://github.com/astral-sh/uv/pull/11210))
- Fix hardlinks in tar unpacking ([#11221](https://github.com/astral-sh/uv/pull/11221))
- Set base executable when returning virtual environment ([#11209](https://github.com/astral-sh/uv/pull/11209))
- Use base Python for cached environments ([#11208](https://github.com/astral-sh/uv/pull/11208))

## 0.5.27

### Enhancements

- Avoid setting permissions during tar extraction ([#11191](https://github.com/astral-sh/uv/pull/11191))
- Remove warnings for missing lower bounds ([#11195](https://github.com/astral-sh/uv/pull/11195))
- Update PubGrub to set-based outdated priority tracking ([#11169](https://github.com/astral-sh/uv/pull/11169))
- Improve error messages for `uv pip install` with `--extra` or `--all-extras` and invalid sources ([#11193](https://github.com/astral-sh/uv/pull/11193))
- Sign Docker images using GitHub attestations ([#8685](https://github.com/astral-sh/uv/pull/8685))

### Preview features

- Don't expand self-referential extras in the build backend ([#11142](https://github.com/astral-sh/uv/pull/11142))

### Bug fixes

- Fix relative paths in bytecode compilation ([#11177](https://github.com/astral-sh/uv/pull/11177))
- Percent-decode URLs in canonical comparisons ([#11088](https://github.com/astral-sh/uv/pull/11088))
- Respect concurrency limits in parallel index fetch ([#11182](https://github.com/astral-sh/uv/pull/11182))
- Use wire JSON schema for conflict items ([#11196](https://github.com/astral-sh/uv/pull/11196))
- Use explicit `_GLibCVersion` tuple in uv-python crate ([#11122](https://github.com/astral-sh/uv/pull/11122))

## 0.5.26

### Enhancements

- Add support for `uvx python` ([#11076](https://github.com/astral-sh/uv/pull/11076))
- Allow `--no-dev --invert` in `uv tree` ([#11068](https://github.com/astral-sh/uv/pull/11068))
- Update `uv python install --reinstall` to reinstall all previous versions ([#11072](https://github.com/astral-sh/uv/pull/11072))
- Consistently write log messages with capitalized first word ([#11111](https://github.com/astral-sh/uv/pull/11111))
- Suggest `--build-backend` when `--backend` is passed to `uv init` ([#10958](https://github.com/astral-sh/uv/pull/10958))
- Improve retry trace message ([#11108](https://github.com/astral-sh/uv/pull/11108))

### Bug fixes

- Ignore non-hash fragments in HTML API responses ([#11107](https://github.com/astral-sh/uv/pull/11107))
- Avoid resolving symbolic links when querying Python interpreters ([#11083](https://github.com/astral-sh/uv/pull/11083))
- Avoid sharing state between universal and non-universal resolves ([#11051](https://github.com/astral-sh/uv/pull/11051))
- Error when `--script` is passing a non-PEP 723 script ([#11118](https://github.com/astral-sh/uv/pull/11118))
- Make metadata deserialization failures non-fatal in the cache ([#11105](https://github.com/astral-sh/uv/pull/11105))
- Mark metadata as dynamic when reading from built wheel cache ([#11046](https://github.com/astral-sh/uv/pull/11046))
- Propagate credentials for `<index>/simple` to `<index>/...` endpoints ([#11074](https://github.com/astral-sh/uv/pull/11074))
- Fix conflicting extra bug during `uv sync` ([#11075](https://github.com/astral-sh/uv/pull/11075))

## 0.5.25

### Enhancements

- Allow installation of manylinux wheels on loongarch64 ([#10927](https://github.com/astral-sh/uv/pull/10927))
- Allow optional `=` for editables in `requirements.txt` ([#10954](https://github.com/astral-sh/uv/pull/10954))
- Add Windows aarch64 to the release binaries ([#10885](https://github.com/astral-sh/uv/pull/10885))

### Bug fixes

- Use spec-compliant (`128+n`) exit codes for `uv run` and `uv tool run` on Unix ([#10781](https://github.com/astral-sh/uv/pull/10781))
- Fix best-interpreter lookups when there is an invalid interpreter in the `PATH` ([#11030](https://github.com/astral-sh/uv/pull/11030))
- Guard against concurrent cache writes on Windows ([#11007](https://github.com/astral-sh/uv/pull/11007))
- Prioritize package preferences with greater package versions ([#10963](https://github.com/astral-sh/uv/pull/10963))
- Reject `--editable` flag on non-directory requirements ([#10994](https://github.com/astral-sh/uv/pull/10994))
- Respect `--no-sources` for `uv pip install` workspace discovery ([#11003](https://github.com/astral-sh/uv/pull/11003))
- Set `JEMALLOC_SYS_WITH_LG_PAGE=16` in ARM Docker builds ([#10943](https://github.com/astral-sh/uv/pull/10943))
- Update `riscv64` Python downloads to allow install on `riscv64gc` ([#10937](https://github.com/astral-sh/uv/pull/10937))
- Fix file persist retries on Windows ([#11008](https://github.com/astral-sh/uv/pull/11008))
- Fix incorrect error message when specifying `tool.uv.sources.(package).workspace` with other options ([#11013](https://github.com/astral-sh/uv/pull/11013))
- Improve SIGINT handling in `uv run` ([#11009](https://github.com/astral-sh/uv/pull/11009))

## 0.5.24

### Enhancements

- Improve determinism of resolution by always setting package priorities ([#10853](https://github.com/astral-sh/uv/pull/10853))
- Upgrade to `cargo-dist` 0.28.0; improves several installer behaviors ([#10884](https://github.com/astral-sh/uv/pull/10884))

### Bug fixes

- Allow fallback to Python download on non-critical discovery errors ([#10908](https://github.com/astral-sh/uv/pull/10908))

### Preview features

- Register managed Python version with the Windows Registry (PEP 514) ([#10634](https://github.com/astral-sh/uv/pull/10634))

## 0.5.23

### Enhancements

- Add `--refresh` to `uv venv` ([#10834](https://github.com/astral-sh/uv/pull/10834))
- Add `--no-default-groups` command-line flag ([#10618](https://github.com/astral-sh/uv/pull/10618))

### Bug fixes

- Sort extras and groups when comparing lockfile requirements ([#10856](https://github.com/astral-sh/uv/pull/10856))
- Include `commit_id` and `requested_revision` in `direct_url.json` ([#10862](https://github.com/astral-sh/uv/pull/10862))
- Invalidate lockfile when static versions change ([#10858](https://github.com/astral-sh/uv/pull/10858))
- Make GitHub fast path errors non-fatal ([#10859](https://github.com/astral-sh/uv/pull/10859))
- Remove warnings for `--frozen` and `--locked` in `uv run --script` ([#10840](https://github.com/astral-sh/uv/pull/10840))
- Resolve `find-links` paths relative to the configuration file ([#10827](https://github.com/astral-sh/uv/pull/10827))
- Respect visitation order for proxy packages ([#10833](https://github.com/astral-sh/uv/pull/10833))
- Treat version mismatch errors as non-fatal in fast paths ([#10860](https://github.com/astral-sh/uv/pull/10860))
- Mark `--locked` and `--upgrade` are conflicting ([#10836](https://github.com/astral-sh/uv/pull/10836))
- Relax error checking around unconditional enabling of conflicting extras ([#10875](https://github.com/astral-sh/uv/pull/10875))

### Error messages

- Error when workspace contains conflicting Python requirements ([#10841](https://github.com/astral-sh/uv/pull/10841))
- Improve uvx error message when uv is missing ([#9745](https://github.com/astral-sh/uv/pull/9745))

## 0.5.22

### Enhancements

- Include version and contact information in GitHub User Agent ([#10785](https://github.com/astral-sh/uv/pull/10785))

### Bug fixes

- Add fallback to build backend when `Requires-Dist` mismatches ([#10797](https://github.com/astral-sh/uv/pull/10797))
- Avoid deserialization error for paths above the root ([#10789](https://github.com/astral-sh/uv/pull/10789))
- Avoid respecting preferences from other indexes ([#10782](https://github.com/astral-sh/uv/pull/10782))
- Disable the distutils setuptools shim during interpreter query ([#10819](https://github.com/astral-sh/uv/pull/10819))
- Omit variant when detecting compatible Python installs ([#10722](https://github.com/astral-sh/uv/pull/10722))
- Remove TOCTOU errors in Git clone ([#10758](https://github.com/astral-sh/uv/pull/10758))
- Validate metadata under GitHub fast path ([#10796](https://github.com/astral-sh/uv/pull/10796))
- Include conflict markers in fork markers ([#10818](https://github.com/astral-sh/uv/pull/10818))

### Error messages

- Add tag incompatibility hints to sync failures ([#10739](https://github.com/astral-sh/uv/pull/10739))
- Improve log when distutils is missing ([#10713](https://github.com/astral-sh/uv/pull/10713))
- Show non-critical Python discovery errors if no other interpreter is found ([#10716](https://github.com/astral-sh/uv/pull/10716))
- Use colors for lock errors ([#10736](https://github.com/astral-sh/uv/pull/10736))

## 0.5.21

### Enhancements

- Avoid building dynamic versions when validating lockfile ([#10703](https://github.com/astral-sh/uv/pull/10703))

### Configuration

- Add `UV_VENV_SEED` environment variable ([#10715](https://github.com/astral-sh/uv/pull/10715))

### Bug fixes

- Avoid attempting to patch macOS dylib for non-macOS installs ([#10721](https://github.com/astral-sh/uv/pull/10721))
- Avoid narrowing `requires-python` marker with disjunctions ([#10704](https://github.com/astral-sh/uv/pull/10704))
- Respect environment variable credentials for indexes outside root ([#10688](https://github.com/astral-sh/uv/pull/10688))
- Respect preferences for explicit index dependencies from `requirements.txt` ([#10690](https://github.com/astral-sh/uv/pull/10690))
- Sort preferences by environment, then index ([#10700](https://github.com/astral-sh/uv/pull/10700))
- Ignore permission errors when looking for user-level configuration file ([#10697](https://github.com/astral-sh/uv/pull/10697))
