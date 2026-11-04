# Step 11 — Python 3.14 becomes the default

## 1. Implement this milestone

At this checkpoint `uv` is the Python 3.14-era release of the tool: the whole command surface
built so far, with 3.14 as the default stable Python version and free-threaded builds usable
without an explicit opt-in. No 3.14 interpreter is present at checkpoint time and none can be
fetched, so that default-version change is not itself observed: what is in scope is the surface
enumerated below, and `TEST_INTERFACE.md` § 11 records the exclusion. `uv venv` refuses to take over a directory that is not already a
virtual environment, whether that directory is named on the command line or is the working
directory itself. `uv build` on a workspace member given with a trailing slash writes its
artefacts to the workspace root's `dist/`, in both the default and the `--wheel` form. `uv
cache clean` and `uv cache prune` accept `--force`. `uv auth` arrives as the
credential-management command family. `uv export` honours `--editable` in both its requirements
output and its `pylock.toml` output. `uv sync --only-group` rejects a request that also carries
`--extra` or `--all-extras`. A `pyproject.toml` carrying only `[dependency-groups]` and no
`[project]` table is a project, so `uv sync`, `uv run` and `uv export` all work in one. `uv pip
compile` accepts `extras` and `dependency_groups` markers on a requirement instead of failing
to parse them. `uv lock` warns when `pyproject.toml` uses a deprecated `tool.uv` field or an
unknown field inside a `tool.uv` table. iOS platform tags join the tag set. The
managed-interpreter milestone of step 10 must still hold in full.

## 2. Read these local references

**Normative semantics:** `/app/SPECS/specs/pep-0730.rst` fixes the `ios_x_y_arch_sdk` platform
tags that enter the tag set here, and `/app/SPECS/specs/pypa-platform-compatibility-tags.rst`
fixes the tag triple they slot into and the ABI tag a free-threaded interpreter carries;
`/app/SPECS/specs/pypa-dependency-specifiers.rst` and `/app/SPECS/specs/pep-0751.rst` define
the `extras` and `dependency_groups` marker variables a requirement may now be qualified by;
`/app/SPECS/specs/pep-0735.rst` and `/app/SPECS/specs/pypa-dependency-groups.rst` fix the
`[dependency-groups]` table that may now stand alone in a `pyproject.toml`;
`/app/SPECS/specs/pypa-pylock-toml.rst` fixes the directory entry and its `editable` key that
an editable export writes; and `/app/SPECS/specs/pypa-virtual-environments.rst` fixes what
makes a directory a virtual environment, and so what `uv venv` may take over.

**Additional files:** `/app/PROFILE.md` (the normative behavioural profile — the command
surface, defaults, diagnostics and exit statuses this milestone adds, in § 1–11),
`/app/TEST_INTERFACE.md` (§ 11), `/app/WORKSPACE_HANDOFF.md`, `/app/SPECS/MAP.md` (which
document enters at which milestone) and `/app/SPECS/changelog/step-11.md` (upstream's own
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
