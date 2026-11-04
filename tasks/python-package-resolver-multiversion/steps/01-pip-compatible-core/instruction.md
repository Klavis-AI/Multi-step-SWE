# Step 01 — the pip-compatible core

## 1. Implement this milestone

You are building the `uv pip` interface and `uv venv` — uv's pip-compatible
core — in `/app`. At this checkpoint `uv` must resolve a set of requirements
against a Python package index, install the result into a virtual environment,
and report what that environment holds, all through the pip-facing command
surface: `uv pip compile`, `uv pip install`, `uv pip sync`, `uv pip uninstall`
and `uv pip freeze`, together with `uv venv`, which creates the environment
the others act on.

That surface encompasses version identifiers and version specifiers;
requirement strings with extras and environment markers; an index client that
reads both the HTML and the JSON listings a simple index serves, including
per-file `requires-python`, detached metadata and yanked files; a resolver that
chooses one consistent version assignment for a single marker environment and
reports a failure when none exists; wheel selection by compatibility tag with
fallback to a source distribution; source-distribution and editable builds
driven through a build backend in an isolated environment; and installation
that leaves a correct `.dist-info` directory behind. `uv pip compile` must emit a pinned requirements file; `uv pip sync`
must remove what the input does not name.

This is the first milestone, so nothing is inherited — everything established
here is the substrate the later milestones extend rather than replace.

## 2. Read these local references

**Normative semantics:** `/app/SPECS/specs/pep-0440.rst` and `/app/SPECS/specs/pypa-version-specifiers.rst` fix version identifiers, their normalisation, their total ordering and every specifier operator; `/app/SPECS/specs/pep-0508.rst` and `/app/SPECS/specs/pypa-dependency-specifiers.rst` fix requirement strings, extras and the environment-marker language; `/app/SPECS/specs/pypa-name-normalization.rst` fixes how a package name is normalised wherever it is compared or printed; `/app/SPECS/specs/pep-0503.rst`, `/app/SPECS/specs/pep-0691.rst`, `/app/SPECS/specs/pep-0658.rst`, `/app/SPECS/specs/pep-0592.rst`, `/app/SPECS/specs/pypa-simple-repository-api.rst` and `/app/SPECS/specs/pypa-file-yanking.rst` fix what the index client reads — the HTML and JSON listings, per-file `requires-python`, detached metadata and yanked files; `/app/SPECS/specs/pep-0425.rst`, `/app/SPECS/specs/pep-0600.rst`, `/app/SPECS/specs/pep-0656.rst` and `/app/SPECS/specs/pypa-platform-compatibility-tags.rst` fix the compatibility tags a wheel is selected by; `/app/SPECS/specs/pep-0427.rst` and `/app/SPECS/specs/pypa-binary-distribution-format.rst` fix the wheel archive and how it is unpacked; `/app/SPECS/specs/pypa-core-metadata.rst` and `/app/SPECS/specs/pypa-recording-installed-packages.rst` fix the metadata fields and the installed `.dist-info` directory that freezing and uninstalling read; `/app/SPECS/specs/pep-0517.rst`, `/app/SPECS/specs/pep-0518.rst`, `/app/SPECS/specs/pep-0660.rst` and `/app/SPECS/specs/pypa-source-distribution-format.rst` fix the build-backend hooks, the isolated build environment, editable builds and the source distribution they operate on; `/app/SPECS/specs/pep-0621.rst`, `/app/SPECS/specs/pep-0631.rst` and `/app/SPECS/specs/pypa-pyproject-toml.rst` fix the `[project]` table read from `pyproject.toml`; and `/app/SPECS/specs/pypa-virtual-environments.rst` fixes what makes a directory a virtual environment. Three documents enter the shipped set at this milestone without this milestone turning on them: `/app/SPECS/specs/pypa-entry-points.rst`, `/app/SPECS/specs/pypa-direct-url.rst` and `/app/SPECS/specs/pypa-direct-url-data-structure.rst` fix the entry-point format and the direct-URL record, and nothing § 1 of `/app/PROFILE.md` fixes turns on a console script or on `direct_url.json`.

**Additional files:** `/app/PROFILE.md` (the normative behavioural profile — the command surface, defaults, diagnostics and exit statuses this milestone adds, in § 1), `/app/TEST_INTERFACE.md` (§ 1), `/app/WORKSPACE_HANDOFF.md`, and `/app/SPECS/README.md` with `/app/SPECS/MAP.md`, which index every shipped document and say which milestone each one enters at; `/app/SPECS/specs/pypa-index.rst` and the `pypa-section-*.rst` pages beside it are the consolidated index those documents sit in, and `/app/SPECS/changelog/step-01.md` is upstream's own release notes for this span — prose, never contract. Everything under `/app/SPECS/` is verbatim upstream text and must not be edited, trimmed, paraphrased or annotated.

## 3. Meta requirements

- The implementation budget is 14,400 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work cumulatively in `/app`. Preserve the inherited source, manifests,
  lockfile, and the documents under `/app/SPECS/`, `/app/PROFILE.md`,
  `/app/TEST_INTERFACE.md` and `/app/WORKSPACE_HANDOFF.md`. Do not create a
  prose answer or an alternate entry point in place of the implementation.
- `cargo build --bin uv` must succeed from `/app/Cargo.toml`, and the binary it
  produces at `target/debug/uv` is the only thing the checkpoint is read
  through. Leave the workspace in a state where that command works from a cold
  `target/`.
- This environment has no network access, and everything the build needs is
  already installed. The upstream project's source, its releases, mirrors and
  package registries are unreachable, so a copy cannot be fetched by any route.
  Do not search for, download, copy, wrap, or invoke an online implementation or
  task-specific answer; do not inspect `/tests`, later-step directories, or
  solution material.
- Do not weaken, filter, rename, or disable any check or test.
  Run the checks named in `/app/WORKSPACE_HANDOFF.md` and leave a reproducible
  handoff for the next fresh session.
