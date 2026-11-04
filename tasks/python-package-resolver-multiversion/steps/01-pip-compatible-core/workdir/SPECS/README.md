# SPECS — reference set for step 01 (uv `0.1.0`)

This directory is the complete, self-contained reference set for this step.
It contains only normative specifications and upstream's own release notes;
it contains no tests, fixtures or expected outputs. Every copied document is
verbatim; provenance (source URL, pinned commit, SHA-256, licence) is in
`specs/MANIFEST.tsv` and `specs/PROVENANCE.json`. `MAP.md` gives the
step-to-document map for steps 01..01; this README is generated from it.

## This step's milestone

Upstream span: `0.1.0` (initial release).

The pip-compatible core: PEP 440 versions and specifiers, PEP 508 requirements and markers, a PEP 503/691 index client with PEP 658 metadata, a PubGrub resolver over one marker environment, wheel and sdist installation with tag selection, a PEP 517/518 build frontend, `uv pip compile`/`install`/`sync`/`uninstall`/`freeze`, `uv venv`.

## Documents introduced at this step

These are the specifications a candidate could not have needed before this
step; the reason is quoted from `MAP.md`.

`pep-0440` `pep-0508` `pypa-version-specifiers` `pypa-dependency-specifiers` `pypa-name-normalization` (resolution is a total function of these); `pep-0503` `pep-0691` `pep-0658` `pep-0592` `pypa-simple-repository-api` `pypa-file-yanking` (the index client parses HTML and JSON, `data-requires-python`, `.metadata`, yanked files at 0.1.0); `pep-0427` `pep-0425` `pep-0600` `pep-0656` `pypa-binary-distribution-format` `pypa-platform-compatibility-tags` `pypa-recording-installed-packages` `pypa-entry-points` `pypa-core-metadata` (wheel selection by tag order, `RECORD` validation, `.dist-info`, console scripts, `musllinux` tags all present at 0.1.0); `pep-0517` `pep-0518` `pypa-source-distribution-format` (sdists are built through the hooks in an isolated environment); `pep-0660` (deviation from the seed, which lists editables at step 2: the 0.1.0 README states editable installs of local paths are supported, `build_editable` is called in 6 source files at 0.1.0, and editable installs of local paths are observable at this tag); `pep-0621` `pep-0631` `pypa-pyproject-toml` (`uv pip compile pyproject.toml --extra` reads `[project].dependencies`/`optional-dependencies` at 0.1.0); `pypa-direct-url` `pypa-direct-url-data-structure` (`direct_url.json` is written and read for URL requirements at 0.1.0); `pypa-virtual-environments` (`uv venv` writes `pyvenv.cfg`); `pypa-index` `pypa-section-distribution-metadata` `pypa-section-installation-metadata` `pypa-section-distribution-formats` `pypa-section-package-indices` `pypa-section-python-description-formats` `pypa-section-reproducible-environments` (the consolidated index the seed pins).

- `specs/pep-0440.rst` — Version identifiers: epoch/release/pre/post/dev/local grammar, normalisation, the total ordering, and the specifier operators (`==`, `!=`, `~=`, `<=`, `>=`, `<`, `>`, `===`, trailing `.*`), plus pre-release handling.
- `specs/pep-0508.rst` — Dependency specifiers: name, extras, version specifier, URL, and the environment-marker language (variables, version vs string comparison).
- `specs/pypa-version-specifiers.rst` — Living edition of PEP 440 with post-PEP amendments; the normative text for versions and specifiers today.
- `specs/pypa-dependency-specifiers.rst` — Living edition of PEP 508, including the `extras` and `dependency_groups` marker variables added for lock files.
- `specs/pypa-name-normalization.rst` — Package-name validity and normalisation (`re.sub(r"[-_.]+", "-", name).lower()`).
- `specs/pep-0503.rst` — Simple repository API in HTML form: project list, name normalisation, `data-requires-python`, hash fragments.
- `specs/pep-0691.rst` — Simple repository API in JSON form (`application/vnd.pypi.simple.v1+json`), content negotiation, `files[]` fields.
- `specs/pep-0658.rst` — `.metadata` files served next to a distribution and the `data-dist-info-metadata` attribute with its hash.
- `specs/pep-0592.rst` — Yanked releases: the `data-yanked` attribute and when a yanked file may be selected.
- `specs/pypa-simple-repository-api.rst` — Living edition of PEP 503/691/658/714/592/700: HTML and JSON index pages, `requires-python`, `core-metadata`, `yanked`, `upload-time`, hashes.
- `specs/pypa-file-yanking.rst` — Living edition of PEP 592.
- `specs/pep-0427.rst` — The wheel format: filename grammar, `.dist-info` contents, `RECORD`, `WHEEL`, `data/` subdirectories, script generation.
- `specs/pep-0425.rst` — Compatibility tags: the `python-abi-platform` triple and how a tag set is matched.
- `specs/pep-0600.rst` — `manylinux_x_y` platform tags derived from the glibc version.
- `specs/pep-0656.rst` — `musllinux_x_y` platform tags derived from the musl version.
- `specs/pypa-binary-distribution-format.rst` — Living edition of PEP 427: the wheel archive, filename escaping, `RECORD`, installation procedure, script wrappers.
- `specs/pypa-platform-compatibility-tags.rst` — Living edition of PEP 425/600/656/730: tag grammar, ABI tags derived from `sys.abiflags`, `manylinux`/`musllinux`/`macosx`/`ios` platform tags.
- `specs/pypa-recording-installed-packages.rst` — The installed `.dist-info` directory: `METADATA`, `RECORD`, `INSTALLER`, `direct_url.json`, `REQUESTED`; what uninstall and freeze read.
- `specs/pypa-entry-points.rst` — `entry_points.txt` and the `console_scripts` / `gui_scripts` groups from which script wrappers are generated.
- `specs/pypa-core-metadata.rst` — `METADATA` / `PKG-INFO` fields and versions (`Requires-Dist`, `Requires-Python`, `Provides-Extra`, `Classifier`, ...).
- `specs/pep-0517.rst` — Build-backend hook protocol: `build_wheel`, `build_sdist`, `get_requires_for_build_*`, `prepare_metadata_for_build_wheel`, and the isolated build environment.
- `specs/pep-0518.rst` — `[build-system]` table in `pyproject.toml`: `requires` and the build environment.
- `specs/pypa-source-distribution-format.rst` — The sdist: `{name}-{version}.tar.gz` naming, `PKG-INFO`, and the archive rules.
- `specs/pep-0660.rst` — Editable installs through `build_editable` and the related hooks.
- `specs/pep-0621.rst` — `[project]` metadata in `pyproject.toml`: `name`, `version`, `requires-python`, `dependencies`, `optional-dependencies`, `dynamic`.
- `specs/pep-0631.rst` — The dependency-specification form (`dependencies` / `optional-dependencies` as PEP 508 strings) that PEP 621 adopted.
- `specs/pypa-pyproject-toml.rst` — Living edition of PEP 621/518: the `[project]` and `[build-system]` tables.
- `specs/pypa-direct-url.rst` — When an installer records `direct_url.json` (URL, path, VCS, editable requirements).
- `specs/pypa-direct-url-data-structure.rst` — The JSON shape of `direct_url.json`.
- `specs/pypa-virtual-environments.rst` — What a virtual environment is: `pyvenv.cfg`, its `home` key, and how a directory is recognised as one.
- `specs/pypa-index.rst` — The consolidated PyPA specifications index (toctree of the six section pages below).
- `specs/pypa-section-distribution-metadata.rst` — Index section: distribution metadata specifications.
- `specs/pypa-section-installation-metadata.rst` — Index section: installation metadata specifications.
- `specs/pypa-section-distribution-formats.rst` — Index section: distribution format specifications.
- `specs/pypa-section-package-indices.rst` — Index section: package index specifications.
- `specs/pypa-section-python-description-formats.rst` — Index section: Python description formats.
- `specs/pypa-section-reproducible-environments.rst` — Index section: reproducible environments (lock files).

## Inherited documents this milestone turns on

None.

## Not governed by any shipped specification

The parts of this step's capability that no PEP or PyPA page defines. These
are uv's own behaviour; the benchmark's compatibility profile fixes them, and
where a specification speaks the profile may not contradict it.

The `uv pip compile` output format (pip-tools annotation convention), the no-solution report, the cache layout, the resolver's candidate-preference policy beyond "highest satisfying version", and the requirements.txt dialect (pip's, not a PyPA specification).

## Full document index (introduced + inherited)

| file | first shipped at step | governs |
|---|---|---|
| `specs/pep-0425.rst` | 01 | Compatibility tags: the `python-abi-platform` triple and how a tag set is matched. |
| `specs/pep-0427.rst` | 01 | The wheel format: filename grammar, `.dist-info` contents, `RECORD`, `WHEEL`, `data/` subdirectories, script generation. |
| `specs/pep-0440.rst` | 01 | Version identifiers: epoch/release/pre/post/dev/local grammar, normalisation, the total ordering, and the specifier operators (`==`, `!=`, `~=`, `<=`, `>=`, `<`, `>`, `===`, trailing `.*`), plus pre-release handling. |
| `specs/pep-0503.rst` | 01 | Simple repository API in HTML form: project list, name normalisation, `data-requires-python`, hash fragments. |
| `specs/pep-0508.rst` | 01 | Dependency specifiers: name, extras, version specifier, URL, and the environment-marker language (variables, version vs string comparison). |
| `specs/pep-0517.rst` | 01 | Build-backend hook protocol: `build_wheel`, `build_sdist`, `get_requires_for_build_*`, `prepare_metadata_for_build_wheel`, and the isolated build environment. |
| `specs/pep-0518.rst` | 01 | `[build-system]` table in `pyproject.toml`: `requires` and the build environment. |
| `specs/pep-0592.rst` | 01 | Yanked releases: the `data-yanked` attribute and when a yanked file may be selected. |
| `specs/pep-0600.rst` | 01 | `manylinux_x_y` platform tags derived from the glibc version. |
| `specs/pep-0621.rst` | 01 | `[project]` metadata in `pyproject.toml`: `name`, `version`, `requires-python`, `dependencies`, `optional-dependencies`, `dynamic`. |
| `specs/pep-0631.rst` | 01 | The dependency-specification form (`dependencies` / `optional-dependencies` as PEP 508 strings) that PEP 621 adopted. |
| `specs/pep-0656.rst` | 01 | `musllinux_x_y` platform tags derived from the musl version. |
| `specs/pep-0658.rst` | 01 | `.metadata` files served next to a distribution and the `data-dist-info-metadata` attribute with its hash. |
| `specs/pep-0660.rst` | 01 | Editable installs through `build_editable` and the related hooks. |
| `specs/pep-0691.rst` | 01 | Simple repository API in JSON form (`application/vnd.pypi.simple.v1+json`), content negotiation, `files[]` fields. |
| `specs/pypa-binary-distribution-format.rst` | 01 | Living edition of PEP 427: the wheel archive, filename escaping, `RECORD`, installation procedure, script wrappers. |
| `specs/pypa-core-metadata.rst` | 01 | `METADATA` / `PKG-INFO` fields and versions (`Requires-Dist`, `Requires-Python`, `Provides-Extra`, `Classifier`, ...). |
| `specs/pypa-dependency-specifiers.rst` | 01 | Living edition of PEP 508, including the `extras` and `dependency_groups` marker variables added for lock files. |
| `specs/pypa-direct-url-data-structure.rst` | 01 | The JSON shape of `direct_url.json`. |
| `specs/pypa-direct-url.rst` | 01 | When an installer records `direct_url.json` (URL, path, VCS, editable requirements). |
| `specs/pypa-entry-points.rst` | 01 | `entry_points.txt` and the `console_scripts` / `gui_scripts` groups from which script wrappers are generated. |
| `specs/pypa-file-yanking.rst` | 01 | Living edition of PEP 592. |
| `specs/pypa-index.rst` | 01 | The consolidated PyPA specifications index (toctree of the six section pages below). |
| `specs/pypa-name-normalization.rst` | 01 | Package-name validity and normalisation (`re.sub(r"[-_.]+", "-", name).lower()`). |
| `specs/pypa-platform-compatibility-tags.rst` | 01 | Living edition of PEP 425/600/656/730: tag grammar, ABI tags derived from `sys.abiflags`, `manylinux`/`musllinux`/`macosx`/`ios` platform tags. |
| `specs/pypa-pyproject-toml.rst` | 01 | Living edition of PEP 621/518: the `[project]` and `[build-system]` tables. |
| `specs/pypa-recording-installed-packages.rst` | 01 | The installed `.dist-info` directory: `METADATA`, `RECORD`, `INSTALLER`, `direct_url.json`, `REQUESTED`; what uninstall and freeze read. |
| `specs/pypa-section-distribution-formats.rst` | 01 | Index section: distribution format specifications. |
| `specs/pypa-section-distribution-metadata.rst` | 01 | Index section: distribution metadata specifications. |
| `specs/pypa-section-installation-metadata.rst` | 01 | Index section: installation metadata specifications. |
| `specs/pypa-section-package-indices.rst` | 01 | Index section: package index specifications. |
| `specs/pypa-section-python-description-formats.rst` | 01 | Index section: Python description formats. |
| `specs/pypa-section-reproducible-environments.rst` | 01 | Index section: reproducible environments (lock files). |
| `specs/pypa-simple-repository-api.rst` | 01 | Living edition of PEP 503/691/658/714/592/700: HTML and JSON index pages, `requires-python`, `core-metadata`, `yanked`, `upload-time`, hashes. |
| `specs/pypa-source-distribution-format.rst` | 01 | The sdist: `{name}-{version}.tar.gz` naming, `PKG-INFO`, and the archive rules. |
| `specs/pypa-version-specifiers.rst` | 01 | Living edition of PEP 440 with post-PEP amendments; the normative text for versions and specifiers today. |
| `specs/pypa-virtual-environments.rst` | 01 | What a virtual environment is: `pyvenv.cfg`, its `home` key, and how a directory is recognised as one. |

## Release notes

Upstream's own changelog entries, one file per step, covering every release in
that step's span. `### Breaking changes` sections are the load-bearing prose:
upstream bumps the minor version exactly when behaviour breaks. Sections that
are pure documentation, CI, release engineering or performance are omitted;
kept text is verbatim.

- `changelog/step-01.md` — step 01, up to `0.1.0` (this step)

## Licences and notices

- `licences/NOTICE-pypa-packaging-specifications.txt`
- `licences/NOTICE-uv-changelog.txt`
- `licences/uv-LICENSE-APACHE`
- `licences/uv-LICENSE-MIT`

PEPs are public domain or CC0-1.0 (each states which; see the manifest). The
PyPA specification pages are CC-BY-SA-3.0 (Python Packaging User Guide). The
uv release notes and licence texts are MIT OR Apache-2.0 (Astral Software Inc.).
