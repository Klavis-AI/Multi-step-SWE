# Step 02 — find-links, hashes, editables, constraints and overrides

## 1. Implement this milestone

You are widening the `uv pip` interface into the full pip-compatible input
surface and finishing `uv venv`. `uv pip compile`, `uv pip install` and
`uv pip sync` must accept requirements files written in pip's option dialect —
nested `-r` includes, constraint files, editable entries, index and
`--find-links` flags and environment-variable expansion in paths — and must
honour the same options given directly on the command line. Distribution policy
becomes a resolver input rather than an install-time filter: `--no-build`,
`--no-binary` and `--only-binary` change which candidates exist at all, and a
request left with nothing installable must fail. Constraint files likewise take
effect during resolution.

Hash-checking mode and the override file are deliberately **out of scope** for
this milestone. `uv` does not accept `--require-hashes` at this release, a
`--hash=` line in a requirements file is parsed and ignored rather than
enforced, and the dependency-override file behaves identically here to the
previous milestone. Nothing in this step's checkpoint depends on either, and
neither may be assumed to work.

The command surface also grows `uv pip show` and `uv pip check`,
machine-readable output from `uv pip list`, `uv cache dir`, and the `uv venv`
options that control the prompt, seed packages and visibility of the system
site-packages. Version comparison has to be exact: local versions and
post-releases must order correctly, the boundary at which a pre-release becomes
an acceptable candidate must be observed by command-line flags, environment
variables and constraint files alike, and markers must evaluate against the
running interpreter's real implementation. Recursive extras must resolve.

Everything the pip-compatible core established at the previous milestone still
holds.

## 2. Read these local references

**Normative semantics:** No specification enters at this milestone — its new command surface, the `requirements.txt` option dialect, `--find-links`, constraint-file semantics and the compiled output's shape are uv's own and are fixed by `/app/PROFILE.md` alone — but inherited documents still govern the semantics this milestone exercises: `/app/SPECS/specs/pep-0440.rst` and `/app/SPECS/specs/pypa-version-specifiers.rst` fix local-version and post-release ordering and the boundary at which a pre-release is an admissible candidate; `/app/SPECS/specs/pep-0508.rst` fixes marker evaluation, including the interpreter-implementation variables, and recursive extras; `/app/SPECS/specs/pep-0660.rst` fixes the editable entries a requirements file may name; and `/app/SPECS/specs/pep-0503.rst` with `/app/SPECS/specs/pypa-simple-repository-api.rst` fix the index pages that `--index-url`, `--extra-index-url` and `--find-links` read, and the wheel-tag grammar the resolver matches candidates against.

**Additional files:** `/app/PROFILE.md` (the normative behavioural profile — the command surface, defaults, diagnostics and exit statuses this milestone adds, in § 1–2), `/app/TEST_INTERFACE.md` (§ 2), `/app/WORKSPACE_HANDOFF.md`, and `/app/SPECS/MAP.md` with `/app/SPECS/README.md`, which say which milestone each shipped document entered at; `/app/SPECS/changelog/step-02.md` is upstream's own release notes for this span — prose, never contract. Everything under `/app/SPECS/` is verbatim upstream text and must not be edited, trimmed, paraphrased or annotated.

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
