# Step 14 — build systems by default again

## 1. Implement this milestone

At this checkpoint `uv` is a stricter front end over the same commands: the project scaffold is
packaged again, and the resolver's pre-release preference is settled. `uv init` creates a
packaged, src-layout project with a build system, and rejects `--project`, which selects an
existing project rather than a new one. `uv run` discovers the project from the directory of
the script it is given, and a `--project` path that does not exist is an error rather than a
warning. `uv venv --clear` refuses to erase a directory that is not a virtual environment
unless `--force` is given as well. The resolver prefers stable releases and falls back to a
pre-release only where no stable version satisfies the requirement, under `uv pip install` and
under `uv lock` alike. Hash-checking mode is entered by a `--require-hashes` directive inside a
requirements file as well as by the flag, and an MD5-only digest no longer satisfies it. A
`pylock.toml` handed to `uv pip install` or `uv pip sync` is validated before use: its filename
must follow the lock-file naming rule and its `packages` array must be present. `uv lock
--upgrade-group` must name a declared dependency group. `uv pip compile --no-annotate`
suppresses the excluded-package footer as well as the per-requirement annotations, `uv pip`
accepts `--cert`, and `uv publish` skips a distribution whose filename is not normalised. Step 13's
milestone must still hold in full, and all of it is re-checked here: the dependency-audit
surface and its service options, the `cache clean`/`cache clear` aliases, the environment
variables that configure project and environment creation, classifier validation in the bundled
build backend, comma-separated package lists, the oldest interpreter that may be requested, the
system-certificate-store flags, the listing of outdated tools, the upper bound on version
segments, the version report's target triple, and workspace members that contain nothing —
every subsection of `PROFILE.md` § 13.

## 2. Read these local references

**Normative semantics:** No specification enters at this milestone; the published text that
governs it is already shipped — `/app/SPECS/specs/pep-0440.rst` and
`/app/SPECS/specs/pypa-version-specifiers.rst` fix pre-release ordering and exclusion but
permit either answer where a fallback is available, so which answer is taken is fixed by
`/app/PROFILE.md`; `/app/SPECS/specs/pep-0751.rst` and `/app/SPECS/specs/pypa-pylock-toml.rst`
fix the lock file's name and its required `packages` array; `/app/SPECS/specs/pep-0518.rst` and
`/app/SPECS/specs/pypa-pyproject-toml.rst` fix the `[build-system]` table a generated project
now declares; `/app/SPECS/specs/pep-0427.rst`,
`/app/SPECS/specs/pypa-binary-distribution-format.rst` and
`/app/SPECS/specs/pypa-source-distribution-format.rst` fix the archive forms and the filename
grammar a distribution is judged against; and `/app/SPECS/specs/pep-0735.rst` fixes the
declared group an upgrade request must name.

**Additional files:** `/app/PROFILE.md` (the normative behavioural profile — the command
surface, defaults, diagnostics and exit statuses this milestone adds, in § 1–14),
`/app/TEST_INTERFACE.md` (§ 14), `/app/WORKSPACE_HANDOFF.md`, `/app/SPECS/MAP.md` (which
document enters at which milestone) and `/app/SPECS/changelog/step-14.md` (upstream's own
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
