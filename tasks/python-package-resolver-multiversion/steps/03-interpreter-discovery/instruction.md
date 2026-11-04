# Step 03 — interpreter discovery redefined

## 1. Implement this milestone

You are redefining interpreter discovery — how `uv` decides which Python a
command acts on — across the whole command surface you have already built. A
`--python` request, also spelled `-p`, on `uv venv`, `uv pip install`,
`uv pip sync`, `uv pip freeze` and `uv pip uninstall` must be satisfied by querying candidate
interpreters and continuing past every one that does not satisfy the request,
instead of stopping at the first interpreter found. A request may name a
version; an implementation such as `cpython` or `pypy`; an implementation and a
version together, in either the `cpython@3.10` or the `pypy3.8` spelling and
without regard to case; or an executable name, a path, an installation
directory or an existing environment directory. The same grammar is accepted
from the environment.

Two outcomes must not occur. A request naming an implementation that does not
exist, and a request that no available interpreter satisfies, must both fail
rather than quietly fall back to some default interpreter.

Separately, both index clients must read the renamed core-metadata attribute
alongside the older spelling, applying the documented precedence between them,
so that detached metadata is still used in place of downloading a distribution.

Everything the pip-compatible input surface established at the previous
milestone still holds.

## 2. Read these local references

**Normative semantics:** `/app/SPECS/specs/pep-0714.rst` enters at this milestone and fixes the renamed core-metadata attribute in both the HTML and the JSON index forms together with its precedence over the older spelling, extending `/app/SPECS/specs/pep-0658.rst` and `/app/SPECS/specs/pypa-simple-repository-api.rst`, which fix the detached metadata file itself; `/app/SPECS/specs/pypa-virtual-environments.rst` fixes what makes a directory a virtual environment, which is the shape `uv venv` writes and what a path request is expected to accept; `/app/SPECS/specs/pypa-platform-compatibility-tags.rst` fixes the tags a discovered interpreter is queried for; and the interpreter request grammar, the search order and the diagnostics around them are governed by no published specification and are fixed by `/app/PROFILE.md`.

**Additional files:** `/app/PROFILE.md` (the normative behavioural profile — the command surface, defaults, diagnostics and exit statuses this milestone adds, in § 1–3), `/app/TEST_INTERFACE.md` (§ 3), `/app/WORKSPACE_HANDOFF.md`, and `/app/SPECS/MAP.md` with `/app/SPECS/README.md`, which say which milestone each shipped document entered at; `/app/SPECS/changelog/step-03.md` is upstream's own release notes for this span — prose, never contract. Everything under `/app/SPECS/` is verbatim upstream text and must not be edited, trimmed, paraphrased or annotated.

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
