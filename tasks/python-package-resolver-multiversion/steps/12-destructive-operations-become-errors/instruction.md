# Step 12 — silently destructive operations become errors

## 1. Implement this milestone

At this checkpoint `uv` makes its destructive operations explicit, validates a project's
declarations before doing any work, and fixes exactly how it writes a standard lock file. `uv
venv` will not replace an existing environment — a named directory or `.venv` — unless
`--clear` is given, and `--relocatable` changes which activation scripts it writes. `uv lock`
refuses a `pyproject.toml` that declares more than one index with `default = true`, an
`explicit` index without a name, or a `tool.uv` conflict entry carrying an unknown field, and
leaves no lockfile behind when it refuses. `uv pip compile --format pylock.toml` applies
`--no-binary` and `--only-binary`, per package and for `:all:`, to the artefacts the lock
records; installing such a file back — `uv pip install` and `uv pip sync` reading a
`pylock.toml` without resolving, honouring its markers and hashes — is inherited, unchanged,
and must still hold. `uv sync` and `uv export` accept comma-separated values for `--extra` and
observe `UV_NO_DEFAULT_GROUPS`. `uv workspace dir` and `uv workspace list`, with and without
`--paths`, report the workspace from anywhere inside it. A global Python version pin that is
incompatible with the project is ignored rather than fatal, by `uv lock` and `uv venv` alike.
The Python 3.14-era milestone of step 11 must still hold in full.

Five things are deliberately **out of scope** here. Reading a `pylock.toml` from a URL rather
than from a path is not required and is checked nowhere. `--clear` is required on `uv venv`
only, not on any other command. The cache commands are unchanged from the milestones that fixed
them, and nothing at this checkpoint depends on them. `uv workspace metadata`, the third
subcommand this release adds under `uv workspace`, is not required either: only `dir` and
`list` are read here. And no diagnostic's wording is required: every refusal this step adds is
read through its exit status and the state it leaves behind — the lockfile not written, the
environment still there — never through its message.

## 2. Read these local references

**Normative semantics:** No specification enters at this milestone; the published text that
governs it is already shipped — `/app/SPECS/specs/pep-0751.rst` and
`/app/SPECS/specs/pypa-pylock-toml.rst` fix a lock file installed without resolution, its
marker, version and hash fields, and the `wheels` and `sdist` entries that artefact filtering
chooses between; `/app/SPECS/specs/pep-0508.rst` fixes how the markers in such a lock are
evaluated for the target environment; `/app/SPECS/specs/pep-0735.rst` and
`/app/SPECS/specs/pypa-dependency-groups.rst` fix the declared groups that default-group
selection turns on and off; and `/app/SPECS/specs/pypa-virtual-environments.rst` fixes what
makes a directory a virtual environment, and so what `uv venv` may replace.

**Additional files:** `/app/PROFILE.md` (the normative behavioural profile — the command
surface, defaults, diagnostics and exit statuses this milestone adds, in § 1–12),
`/app/TEST_INTERFACE.md` (§ 12), `/app/WORKSPACE_HANDOFF.md`, `/app/SPECS/MAP.md` (which
document enters at which milestone) and `/app/SPECS/changelog/step-12.md` (upstream's own
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
