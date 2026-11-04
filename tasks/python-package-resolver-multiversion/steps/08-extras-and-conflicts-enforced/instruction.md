# Step 08 — extras and conflicts become errors

## 1. Implement this milestone

At this checkpoint `uv` validates the request you make instead of quietly ignoring part of it.
An extra or a dependency group the project does not declare is an error from `uv sync`,
including under `--frozen`, and extras or groups declared to conflict are refused rather than
resolved together. The project commands fill out around that: `uv sync` accepts `--dry-run`,
`--active` and `--no-default-groups`, and `uv tree` accepts `--no-dev` together with
`--invert`. Interpreter selection becomes uniform across the surface — `uv python install` and
the `uv pip` commands honour `UV_PYTHON` ahead of a `.python-version` file, `-p` is an alias
for `--python` on `uv pip compile`, and `uv tool run python` reaches an interpreter without
naming a package. `uv run` becomes a well-behaved parent process: it exports `UV` to the
program it spawns, and a child killed by a signal is reported through `uv`'s own exit status
rather than swallowed. `uv init` writes a sample module into a new project by default and
`--bare` suppresses the whole scaffold. `uv publish` leaves preview. `UV_NO_BINARY` and
`UV_NO_BINARY_PACKAGE` join the build-option surface that `uv sync` reads from the
environment. The universal forking resolver of step 07 must still behave exactly as it did.

## 2. Read these local references

**Normative semantics:** No published specification governs what this milestone adds — the
shipped text says which extras and dependency groups a project declares, but nothing in it
says that requesting an undeclared one is an error, how conflicting extras and groups are
enforced, which interpreter request wins, what a spawned process is told, or what a
signal-terminated child does to an exit status, so this milestone is fixed by `/app/PROFILE.md`
alone; the specifications already shipped under `/app/SPECS/specs/` continue to govern the
inherited behaviour these diagnostics are checked against, and nothing here may contradict
them.

**Additional files:** `/app/PROFILE.md` (the normative behavioural profile — the command
surface, defaults, diagnostics and exit statuses this milestone adds, in § 1–8),
`/app/TEST_INTERFACE.md` (§§ 5 and 8), `/app/WORKSPACE_HANDOFF.md`, `/app/SPECS/MAP.md` (which
document enters at which milestone, and which parts of this step nothing published covers) and
`/app/SPECS/changelog/step-08.md` (upstream's own release notes for this step's span — prose,
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
