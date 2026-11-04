# Step 05 — packaging becomes opt-out

## 1. Implement this milestone

You are inverting the project interface's packaging default and completing that
interface with workspaces, dependency sources and two new commands.

A project is packaged only if it opts in. A `pyproject.toml` carrying no
`[build-system]` table is neither built nor installed into its own environment:
`uv sync` installs the project's dependencies and nothing else, and `uv run`
executes against that environment without the project itself being importable.
`tool.uv.package = false` switches packaging off for a project that does
declare a build system, and its console scripts stop appearing. `uv init` no
longer creates a `src/` layout or a build system by default; `--lib` and
`--package` restore both, and `--script` writes a single-file script with an
inline metadata block instead of a project.

A `[project]` table is now allowed in a workspace root that is itself virtual,
and members resolve against the shared lockfile and environment whether or not
they are packaged. `[tool.uv.sources]` redirects a dependency to a path or a
workspace member, and accepts several entries for one dependency. Two
commands join the project interface: `uv build`, which writes distributions
into `dist/` and can be limited to a wheel, and `uv export`, which writes the
locked resolution as a requirements file. `uv run -m` runs a module,
`UV_PROJECT_ENVIRONMENT` relocates the project environment, and `uv sync` gains
`--no-editable` and `--only-dev`.

Everything the project, tool, script and python interfaces established at the
previous milestone still holds.

## 2. Read these local references

**Normative semantics:** No specification enters at this milestone — the packaging flip and `tool.uv.package`, workspaces, `[tool.uv.sources]`, the export command's grammar and the project-environment location are uv's own and are fixed by `/app/PROFILE.md` alone — but inherited documents govern what the milestone switches on: `/app/SPECS/specs/pep-0518.rst` and `/app/SPECS/specs/pypa-pyproject-toml.rst` fix the `[build-system]` table whose presence or absence is now the packaging switch; `/app/SPECS/specs/pep-0621.rst` fixes the `[project]` table a virtual workspace root may still carry; `/app/SPECS/specs/pep-0517.rst` fixes the build-backend hooks the build command drives; and `/app/SPECS/specs/pypa-binary-distribution-format.rst` with `/app/SPECS/specs/pypa-source-distribution-format.rst` fix the wheel and the source distribution it writes out.

**Additional files:** `/app/PROFILE.md` (the normative behavioural profile — the command surface, defaults, diagnostics and exit statuses this milestone adds, in § 1–5), `/app/TEST_INTERFACE.md` (§ 5), `/app/WORKSPACE_HANDOFF.md`, and `/app/SPECS/MAP.md` with `/app/SPECS/README.md`, which say which milestone each shipped document entered at; `/app/SPECS/changelog/step-05.md` is upstream's own release notes for this span — prose, never contract. Everything under `/app/SPECS/` is verbatim upstream text and must not be edited, trimmed, paraphrased or annotated.

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
