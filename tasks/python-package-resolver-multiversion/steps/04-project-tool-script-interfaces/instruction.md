# Step 04 — the project, tool, script and python interfaces

## 1. Implement this milestone

You are adding four interfaces on top of the pip-compatible ones: the project
interface, the tool interface, the script interface and the `uv python`
interface.

The project interface is `uv init`, `uv add`, `uv remove`, `uv lock`,
`uv sync`, `uv run` and `uv tree`. `uv init` scaffolds a project; `uv add` and
`uv remove` edit the project's declared dependencies and update the lockfile,
with and without syncing the environment; `uv lock` writes `uv.lock`, uv's own
lockfile, which every later project command reads; `uv sync` materialises the
project's environment; `uv run` executes a command inside it, and can bring an
extra package along for one invocation; `uv tree` renders the resolved
dependency graph and honours a depth limit.

The tool interface is `uv tool install`, `uv tool run`, `uv tool list`,
`uv tool uninstall` and `uv tool dir`: a package's console scripts are
installed into a directory of their own, become runnable from there, are
listed, and disappear again on uninstall. The script interface reads and writes
an inline metadata block inside a single-file script, so that `uv run` resolves
and runs a script against the dependencies the script itself declares.
`uv python find` and `uv python pin` expose interpreter selection directly, and
`uv venv` reads both the pinned version file and the project's declared
`requires-python`.

Everything the redefined interpreter discovery established at the previous
milestone still holds.

## 2. Read these local references

**Normative semantics:** `/app/SPECS/specs/pep-0723.rst` and `/app/SPECS/specs/pypa-inline-script-metadata.rst` enter at this milestone and fix the inline metadata block a single-file script carries — its delimiters and its `dependencies` and `requires-python` keys — which the script interface reads and writes; `/app/SPECS/specs/pep-0621.rst` and `/app/SPECS/specs/pypa-pyproject-toml.rst` fix the `[project]` table the project interface creates and edits; `/app/SPECS/specs/pypa-entry-points.rst` fixes the console-script groups the tool interface links; `/app/SPECS/specs/pep-0440.rst` fixes the version syntax a pinned-version file and a declared `requires-python` are read as; `/app/SPECS/specs/pypa-virtual-environments.rst` fixes the project environment the project commands operate on; and `uv.lock` is governed by no published specification at all, so its content is fixed by `/app/PROFILE.md`, as are the scaffold, the managed-interpreter and tool directory layouts.

**Additional files:** `/app/PROFILE.md` (the normative behavioural profile — the command surface, defaults, diagnostics and exit statuses this milestone adds, in § 1–4), `/app/TEST_INTERFACE.md` (§ 4), `/app/WORKSPACE_HANDOFF.md`, and `/app/SPECS/MAP.md` with `/app/SPECS/README.md`, which say which milestone each shipped document entered at; `/app/SPECS/changelog/step-04.md` is upstream's own release notes for this span — prose, never contract. Everything under `/app/SPECS/` is verbatim upstream text and must not be edited, trimmed, paraphrased or annotated.

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
