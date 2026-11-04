# Step 09 — uv version redefined, and pylock export

## 1. Implement this milestone

At this checkpoint `uv` reads and writes the project's own version and interoperates with the
standard lock file. `uv version` now reports and edits the version in `pyproject.toml` —
`--short`, `--bump` and `--preview` included — while `uv self version` reports the version of
the `uv` binary, and `--version` is no longer accepted by subcommands such as
`uv pip install` and `uv run`. `uv export --format pylock.toml` writes the standard lock and
`uv pip compile -o` writes one too, while `uv pip install` and `uv pip sync` read one; the
filename rule is enforced in both directions, so a TOML input or output that is not a
`pylock.toml` is rejected rather than guessed at. `uv init --build-backend` chooses the backend it scaffolds and implies
`--package`. `uv tool run` requires the executable it runs to be provided by the package it
installed. Around these, `uv python dir` and `uv tool dir` treat an empty environment variable
as unset, `uv python find` and `uv venv` ignore a version file that is not a request they
understand, `uv sync` refuses `--frozen` together with `--no-sources`, and a dependency-group
specifier naming an object the project does not define stops `uv lock` and `uv sync` before a
lockfile is written. The request-validation milestone of step 08 must still hold in full.

## 2. Read these local references

**Normative semantics:** `/app/SPECS/specs/pep-0751.rst` and
`/app/SPECS/specs/pypa-pylock-toml.rst` fix `pylock.toml` — its required fields, the
`wheels`/`sdist`/`directory`/`vcs` package variants, the marker variables it defines, the
filename rule that decides which TOML files are acceptable, and installation from it without
resolution; `/app/SPECS/specs/pep-0440.rst` fixes the version grammar a bumped project version
must still satisfy; `/app/SPECS/specs/pep-0517.rst`,
`/app/SPECS/specs/pypa-source-distribution-format.rst` and
`/app/SPECS/specs/pypa-binary-distribution-format.rst` fix the hooks a scaffolded
`[build-system]` must implement and the two artefact formats a `pylock.toml` package entry
describes; `/app/SPECS/specs/pypa-core-metadata.rst` fixes the metadata a backend must emit;
`/app/SPECS/specs/pypa-entry-points.rst` fixes the `console_scripts` group that decides which
executables a package provides; and `/app/SPECS/specs/pep-0735.rst` fixes the dependency-object
specifier form a group entry may take.

**Additional files:** `/app/PROFILE.md` (the normative behavioural profile — the command
surface, defaults, diagnostics and exit statuses this milestone adds, in § 1–9),
`/app/TEST_INTERFACE.md` (§ 9), `/app/WORKSPACE_HANDOFF.md`, `/app/SPECS/MAP.md` (which
document enters at which milestone) and `/app/SPECS/changelog/step-09.md` (upstream's own
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
