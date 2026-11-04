# Step 13 — resolution, environments and index precedence

## 1. Implement this milestone

At this checkpoint `uv` verifies certificates through the platform, keeps a floor under the
Python versions it will look for, and settles a batch of operational commands and option
spellings. `--system-certs` is the flag that selects system certificates, accepted both as a
global flag ahead of the subcommand and on `uv pip` commands, with `--native-tls` retained as
its equivalent. A Python request below the supported floor is no longer invalid input: `uv
python pin` records the pin and warns that no interpreter was found, and `uv venv --python`
fails with that same not-found diagnostic rather than a validity complaint. `uv audit` arrives
with a selectable advisory service and service format. `uv --version` reports uv's target
triple beside the version, `uv cache clear` is an alias of `uv cache clean`, and `uv tool list`
accepts `--outdated`. List-valued options such as `--no-emit-package` take comma-separated
values in a single argument. A version segment above the integer bound is rejected instead of
overflowing. The build backend refuses to build a project whose `classifiers` name one that is
not a real trove classifier, leaving no artefact behind. `UV_INIT_BARE` and
`UV_VENV_RELOCATABLE` configure `uv init` and `uv venv`. An empty workspace-member directory is
skipped by `uv lock` and `uv sync` rather than failing them. The lock-import and
environment-replacement milestone of step 12 must still hold in full — and so must every
milestone before it. This checkpoint re-observes the behaviour fixed by steps 01 through
12 as a precondition on the whole of this step: if any of it has regressed, the step fails
however much of the batch above is implemented. Nothing in the batch above requires giving up anything already built.

## 2. Read these local references

**Normative semantics:** No specification enters at this milestone and almost all of it —
certificate handling, the supported-Python floor, the advisory command, the version report, the
command aliases, the comma-separated option values and the environment variables — is fixed by
`/app/PROFILE.md` alone; the published text that governs the remainder is
`/app/SPECS/specs/pep-0440.rst` with `/app/SPECS/specs/pypa-version-specifiers.rst`, which fix
the version grammar and ordering a release segment must still satisfy but set no upper bound on
one, so the bound itself is the profile's; `/app/SPECS/specs/pypa-core-metadata.rst`, which
fixes the `Classifier` field a built distribution carries, the list of valid classifiers being
external to this corpus; and `/app/SPECS/specs/pypa-pyproject-toml.rst` with
`/app/SPECS/specs/pep-0517.rst`, which fix the `[project]` table a build reads and the hooks
through which a backend produces that metadata.

**Additional files:** `/app/PROFILE.md` (the normative behavioural profile — the command
surface, defaults, diagnostics and exit statuses this milestone adds, in § 1–13),
`/app/TEST_INTERFACE.md` (§ 13), `/app/WORKSPACE_HANDOFF.md`, `/app/SPECS/MAP.md` (which
document enters at which milestone, and which parts of this step nothing published covers) and
`/app/SPECS/changelog/step-13.md` (upstream's own release notes for this step's span — prose,
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
