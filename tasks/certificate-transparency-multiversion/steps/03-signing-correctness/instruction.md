# Step 3 — signing correctness (certificate-transparency-go v1.0.10)

## 1. Implement this milestone

`/app` already holds the stack you delivered through step 2 at
certificate-transparency-go v1.0.5 — the codec, the forked parsers,
the RFC 6962 structures, the Merkle verifier, the client, the utilities, and
the `trillian/ctfe` front end with its two-shaped configuration loader.
Continue that implementation in place.

This milestone, certificate-transparency-go v1.0.10, is a correctness
milestone rather than a feature: it is the fix for the v1.0.7 advisory about
signature caching in the front end. The rule it establishes is that **no two
distinct signing inputs may share a signature**, and that whatever makes
signing cheaper is invisible in the output — no response reveals whether a
signature was cached or computed afresh.

That means every byte that enters a signature input must also determine what
may be reused: the timestamp, the entry type, the entry itself, the extensions,
and for a tree head the size and root hash. A submission that differs anywhere
must produce a signature that verifies only over its own input.

The configuration validator also learns to refuse duplicate prefixes
explicitly, at load time, instead of failing later when two logs try to claim
the same HTTP path.

**Hard prerequisite for step 4:** step 4 makes verification an importable
library — `ctutil` verifies SCTs that somebody else issued. It can only be
written against a signature construction that is exactly right, so this step's
property and the signature inputs behind it must hold.

A set of **gate** groups re-runs what earlier steps already signed off,
together with proof that your tree builds and the artifact starts. If any of
them fails, the step is not satisfied, however much of this milestone you
implemented. Beyond the gate, this milestone's new behaviour is required in
independent groups; `TEST_INTERFACE.md` §7 lists them. A change that breaks
earlier work means the step is not satisfied, and new work must actually be
present. Breadth is expected, not depth, and a tree that regressed does not
satisfy the step at all, however clever the cache.

---

## 2. Read these local references

Everything you need is already in `/app`. Nothing here requires the network.

Two contract files first, in this order:

1. **`/app/TEST_INTERFACE.md`** — the interface contract: the module path, the
   build command, the import paths and symbols drivers link against, the
   programs they run, the formats crossing the boundary, and what a group
   checks. Read sections 1 to 3 before you write anything, section 7 for the
   group list, and section 10 last.
2. **`/app/WORKSPACE_HANDOFF.md`** — what `/app` is, what is overlay-owned,
   what carries into the next step, and the checkpoint to run before you stop.

Then the corpus in `/app/ct-spec/`, which is the only description of the
behaviour you are implementing. `/app/ct-spec/README.md` indexes all of it; the
pages and sections that matter at this milestone are:

| Read | For |
| --- | --- |
| `ct-spec/TLS-CODEC.md` §5.3, §5.5 | the exact bytes of each signature input — the whole milestone |
| `ct-spec/ARCHITECTURE.md` §5 | where signing sits in the add-chain lifecycle |
| `ct-spec/rfc/rfc6962.txt` §3.2, §3.5 | the normative definitions of the two signed structures |
| `ct-spec/CONFIG.md` §4 | the duplicate-prefix check and its diagnostic |
| `ct-spec/RELEASE-MAP.md` §3 | the milestone on one screen |

`/app/ct-spec/PROFILE.md` is written for this step specifically: what this
milestone adds over the previous one, in the product's own vocabulary, with a
reading route through the corpus. Read it before the pages above.

Finally, `/app/WORKSPACE_HANDOFF.md` tells you what state to leave `/app` in
and gives you a copy-pasteable checkpoint; run it before your time runs out.

---

## 3. Meta requirements

- **Budget**: 7,200 seconds. `bash /app/timer.sh` shows the remaining time;
  the configured timeout is authoritative and resets each step. The corpus in
  `/app/ct-spec/` is 12 pages and about 0.8 MB — read
  the two contract files and the sections named above, not the whole set.
- **Cumulative workspace**: work in `/app`. Everything you leave there is what
  step 4 starts from, so keep it building at every point you might
  be interrupted — the snapshot is taken when the clock stops, not when you
  decide you are finished. Do not delete or move `ct-spec/`,
  `TEST_INTERFACE.md`, `WORKSPACE_HANDOFF.md`, `timer.sh`, `go.mod` or
  `go.sum`: those six are overlay-owned and are refreshed every step.
  Everything else under `/app` is yours and is preserved. Compiled binaries are
  not carried forward; the sources are.
- **Network**: This environment has no network access, and everything the build needs is already installed. The upstream project's source, its releases, mirrors and package registries are unreachable, so a copy cannot be fetched by any route. Do **not** search for, download, copy, wrap or
  invoke an online implementation or a task-specific answer. Checking itself is
  offline and the dependency set is already pinned in `/app/go.mod`, so there
  is nothing you need to fetch in order to build.
- **This applies with full force to certificate-transparency-go itself.**
  `github.com/google/certificate-transparency-go` is Apache-2.0 licensed and is
  one `git clone` away, as are its forks, its vendored copies inside other
  projects, and the Go module proxy's zip of every version of it. Vendoring,
  transcribing or reconstructing upstream source — in whole or in part, by any
  route, **including from memory** — is prohibited and defeats the point of the
  task. The shipped documentation in `/app/ct-spec/` describes the *behaviour*
  you must reproduce, down to the wire formats and the exact bytes where they
  matter; the implementation must be yours.
- **Off-limits**: do not inspect `/tests`, later step directories, fixtures
  outside the workspace or solution material, and do not weaken or disable
  any check.
- **Handoff**: leave `/app` in a reproducible state — `go build ./...` succeeds
  offline from the sources in the workspace, and the `WORKSPACE_HANDOFF.md`
  checkpoint passes.
