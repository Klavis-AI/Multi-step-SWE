# Step 10 — managed interpreters reach PATH

## 1. Implement this milestone

At this checkpoint `uv` manages Python interpreters as installed software, and its exit
statuses are part of the contract. `uv python install` places managed executables where they
shadow the system interpreter on `PATH`, `--bin` and `--no-bin` control that, `uv python upgrade`
moves a managed installation forward, and `uv python pin --rm` removes a pin.
Failure is reported precisely: `uv lock --check` and `uv sync --check` signal an out-of-date
lockfile or environment through the exit status instead of rewriting anything, and a `uv venv`
request for an interpreter that is not present exits with the status reserved for a request
that cannot be satisfied. A dependency without a `[build-system]` is built through the legacy setuptools
backend rather than treated as virtual, so a `uv sync` whose only dependency is such a
path source or workspace member performs that build — and fails with status `1`,
installing nothing, when it cannot be carried out. `uv add` defaults a subdirectory path to a workspace source and takes
`--bounds`, a path source whose editable declarations conflict is refused rather than locked,
and `uv init --lib` scaffolds the default build backend. `uv run --with` layers an empty ephemeral environment over the one holding
those requirements, so a `--with` package's module file does not lie under `sys.prefix`. `uv.toml` rejects fields that belong only to
`pyproject.toml`, `uv pip compile --python-platform linux` selects `manylinux` wheels,
and `uv version` outside a project is an error. The `uv version` and `pylock.toml` milestone
of step 09 must still hold.

## 2. Read these local references

**Normative semantics:** No published specification governs what this milestone adds — nothing
in the shipped text fixes where a managed interpreter is installed, whether it shadows the
system one on `PATH`, how a managed installation is upgraded, which exit status a check or a
missing interpreter produces, how a dependency without a build system is treated, or which
fields a configuration file may carry, so this milestone is fixed by `/app/PROFILE.md` alone;
the specifications already shipped under `/app/SPECS/specs/` continue to govern the inherited
behaviour it builds on — platform tag selection, the build-system table and the virtual
environment among them — and nothing here may contradict them.

**Additional files:** `/app/PROFILE.md` (the normative behavioural profile — the command
surface, defaults, diagnostics and exit statuses this milestone adds, in § 1–10),
`/app/TEST_INTERFACE.md` (§ 10), `/app/WORKSPACE_HANDOFF.md`, `/app/SPECS/MAP.md` (which
document enters at which milestone, and which parts of this step nothing published covers) and
`/app/SPECS/changelog/step-10.md` (upstream's own release notes for this step's span — prose,
never contract). Everything under `/app/SPECS/` is verbatim upstream text under the licences
reproduced beside it: read it, and never edit, trim, paraphrase or annotate it.

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
