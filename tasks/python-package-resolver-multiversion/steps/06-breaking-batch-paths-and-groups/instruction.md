# Step 06 — a breaking batch over paths and groups

## 1. Implement this milestone

At this checkpoint `uv` is a project manager whose dependency groups, interpreters and
configuration are all explicit rather than inferred. The project interface gains dependency
groups: a `[dependency-groups]` table is read from `pyproject.toml`, `uv sync` accepts
`--group`, `--only-group` and `--no-group`, and `tool.uv.default-groups` decides what a bare
`uv sync` installs. `uv venv` now takes the environment's base interpreter from
`sys._base_executable`, so the `home` key it writes into `pyvenv.cfg` and the interpreter a
later `uv run` reaches agree. A `.python-version` file is found in a parent directory as well
as the working directory, and `uv init` leaves a new workspace member to inherit the root's
rather than writing its own. `uv.toml` becomes strict: a setting only `pyproject.toml` may
carry is refused instead of silently ignored. `uv publish` arrives, installed executables move
to `$XDG_BIN_HOME` or `~/.local/bin`, and the surrounding surfaces move with them —
`uv export --no-header`, `uv lock --dry-run`, `uv run -` reading an inline-metadata script from
standard input, `uv run --with` taking a comma-separated list, `uv pip install --exact`,
`uv pip list --outdated`, `uv pip show --files`, the global `--allow-insecure-host`,
`--exclude-newer` and `UV_FROZEN`. Resolution changes too: a local version survives into the
compiled output, the installed environment and `uv.lock`, and `!=` against a pre-release no
longer admits one. Everything the packaging-and-workspaces milestone of step 05 established
must still hold.

## 2. Read these local references

**Normative semantics:** `/app/SPECS/specs/pep-0735.rst` and
`/app/SPECS/specs/pypa-dependency-groups.rst` fix the `[dependency-groups]` table — legal group
names, `include-group`, and the fact that a file may carry it without a `[project]` table;
`/app/SPECS/specs/pep-0440.rst` and `/app/SPECS/specs/pypa-version-specifiers.rst` fix how a
local version is normalised, compared and matched by a specifier, and how `!=` and the
pre-release rules interact; `/app/SPECS/specs/pypa-virtual-environments.rst` fixes `pyvenv.cfg`
and the meaning of its `home` key; `/app/SPECS/specs/pypa-simple-repository-api.rst` fixes the
`upload-time` field that a date cutoff filters on; `/app/SPECS/specs/pep-0723.rst` fixes the
inline script block, which is now also read from standard input; and
`/app/SPECS/specs/pep-0621.rst` fixes the `[project]` table whose `requires-python` an
interpreter request is checked against.

**Additional files:** `/app/PROFILE.md` (the normative behavioural profile — the command
surface, defaults, diagnostics and exit statuses this milestone adds, in § 1–6),
`/app/TEST_INTERFACE.md` (§ 6), `/app/WORKSPACE_HANDOFF.md`, `/app/SPECS/MAP.md` (which
document enters at which milestone) and `/app/SPECS/changelog/step-06.md` (upstream's own
release notes for this step's span — prose, never contract). Everything under `/app/SPECS/` is
verbatim upstream text under the licences reproduced beside it: read it, and never edit, trim,
paraphrase or annotate it.

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
