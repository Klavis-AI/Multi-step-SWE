# Step 15 — consolidation of the 0.12 line

## 1. Implement this milestone

At this checkpoint `uv` is the settled `0.12` line: the whole command surface, with the edges
of the previous milestone's strictness resolved and the report a failed resolution prints
rebuilt. Failures are classified by exit status, so an expected failure and a recognised
operational or internal one are told apart — a requested distribution whose name does not match
the requirement it was requested as, and a `--locked` run with no lockfile to check, are
expected failures. `uv pip install --find-links` accepts a local HTML page as a flat index, and
a hash carried in one of its links is checked against the file it names. `uv init` refuses to
infer a project name that is reserved for a Python interpreter or implementation. `uv export
--frozen` rejects a `uv.lock` whose git source carries an unsupported archive path or URL
scheme. `--locked` and `--frozen` given on the command line take precedence over `UV_LOCKED`
and `UV_FROZEN`, and `--no-locked` turns the environment variable back off. A `requires-python`
value is parsed with whitespace before a wildcard comparison, entries in `.python-version` are
trimmed before they are read as a request, and consecutive wildcard exclusions survive a round
trip through `uv.lock`. A `pylock.toml` whose wheel filename disagrees with the package name or
version declared beside it is rejected. `uv workspace metadata` reads the workspace without
writing a lockfile. The strictness and pre-release-preference milestone of step 14 must still
hold in full.

## 2. Read these local references

**Normative semantics:** No specification enters at this milestone, and the failure report and
the exit-status classification are fixed by `/app/PROFILE.md` alone; the published text that
governs the rest is already shipped — `/app/SPECS/specs/pep-0751.rst` and
`/app/SPECS/specs/pypa-pylock-toml.rst` fix a wheel entry's recorded file name and the package
name and version it is recorded beside; `/app/SPECS/specs/pep-0440.rst` and
`/app/SPECS/specs/pypa-version-specifiers.rst` fix the specifier grammar, the trailing `.*`
form and the whitespace that is ignored around an operator and a version;
`/app/SPECS/specs/pep-0508.rst` and `/app/SPECS/specs/pypa-name-normalization.rst` fix what a
valid, normalised package name is; and `/app/SPECS/specs/pep-0503.rst` with
`/app/SPECS/specs/pypa-simple-repository-api.rst` fix the HTML page an index serves, its
anchors and their `#<hashname>=<hashvalue>` fragments.

**Additional files:** `/app/PROFILE.md` (the normative behavioural profile — the command
surface, defaults, diagnostics and exit statuses this milestone adds, in § 1–15),
`/app/TEST_INTERFACE.md` (§ 15), `/app/WORKSPACE_HANDOFF.md`, `/app/SPECS/MAP.md` (which
document enters at which milestone, and which parts of this step nothing published covers) and
`/app/SPECS/changelog/step-15.md` (upstream's own release notes for this step's span — prose,
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
