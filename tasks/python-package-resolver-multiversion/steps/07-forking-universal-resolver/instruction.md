# Step 07 — the forking universal resolver

## 1. Implement this milestone

At this checkpoint `uv`'s resolver is universal: one resolution describes every marker
environment at once, and where no single set of versions satisfies them all it forks and
records each branch under the marker that selects it. You reach this through
`uv pip compile --universal`, whose output carries a marker per branch; through `uv lock`,
which writes the fork guards and resolution markers into `uv.lock`; through `uv sync` and
`uv sync --frozen`, which install the branch the running interpreter falls into; and through
`uv tree` and `uv tree --universal`, which render one branch or all of them. Marker handling
becomes algebraic rather than per-environment: equivalent markers normalise to a single form,
`platform_system` and `sys_platform` values are reconciled against each other, and a pair that
cannot both be true prunes the branch it guards, transitively. `--fork-strategy` chooses
between splitting on `requires-python` and keeping the fewest versions, both on the command
line and as a setting. A `[tool.uv] conflicts` declaration is parsed and validated, a
malformed entry is refused, and a request for two declared-conflicting extras fails while
either alone still syncs. The dependency-group and environment-provenance
milestone of step 06 must continue to hold unchanged.

## 2. Read these local references

**Normative semantics:** No published specification governs what this milestone adds — the
published text fixes how a marker is evaluated in one environment, not the algebra of markers
across every environment at once, so forking, marker normalisation and simplification,
cross-variable disjointness, fork strategies, the per-fork guards recorded in the lockfile and
the conflict declaration are fixed by `/app/PROFILE.md` alone; the specifications already
shipped under `/app/SPECS/specs/` continue to govern the inherited behaviour this milestone
builds on, and nothing here may contradict them.

**Additional files:** `/app/PROFILE.md` (the normative behavioural profile — the command
surface, defaults, diagnostics and exit statuses this milestone adds, in § 1–7),
`/app/TEST_INTERFACE.md` (§ 7), `/app/WORKSPACE_HANDOFF.md`, `/app/SPECS/MAP.md` (which
document enters at which milestone, and which parts of this step nothing published covers) and
`/app/SPECS/changelog/step-07.md` (upstream's own release notes for this step's span — prose,
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
