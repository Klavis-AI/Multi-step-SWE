# Step 4 — ctutil verification (certificate-transparency-go v1.0.13)

## 1. Implement this milestone

`/app` already holds the stack you delivered through step 3 at
certificate-transparency-go v1.0.10 — the codec, the forked parsers,
the RFC 6962 structures, the `merkletree` verifier, the client, the utilities
and the front end. Continue that implementation in place.

This milestone, certificate-transparency-go v1.0.13, turns verification
into a library. The new `ctutil` package lets anyone holding a certificate, an
SCT and a log's public key decide for themselves whether that SCT is genuine
and whether the log included the entry: `LeafHash` and `LeafHashB64` compute
the pre-image and its hash, `VerifySCT` checks the signature, `ContainsSCT`
looks inside a certificate, and `LogInfo` with `VerifyInclusion`,
`VerifyInclusionLatest` and `VerifyInclusionAt` checks inclusion against a tree
head.

Two details are load-bearing. An SCT that travelled *inside* the certificate
covers the precertificate form, so the leaf must be rebuilt with the
**SCT-list** extension removed (`x509.RemoveSCTList`, X509-FORK.md §4.2 — the
certificate you are handed is the *issued* one and has no poison extension to
remove) and the issuer key hash filled in; an SCT delivered beside the
certificate covers whatever `chain[0]` actually is — the certificate itself
when that is an ordinary issued certificate, the *precertificate* entry when
`chain[0]` is a precertificate, a question the code settles by asking the
certificate rather than by trusting the flag. Asking for `embedded` when the
certificate does not actually carry that SCT is an error, not a wrong hash.
And every inclusion check returns **-1** with its error on failure, never 0,
because 0 is a valid leaf index.

Arriving with it: `loglist`, which parses the published v1 catalogue of known
logs; `dnsclient`, which answers the read-side queries from DNS TXT records;
`client.CheckLogClient`, the three-method read-only interface; and the programs
`sctcheck`, `sctscan` and `findlog`, bringing the binary count to thirteen.

**Hard prerequisite for step 5:** step 5 deletes `merkletree` and obtains proof
verification from an external library, rewriting every internal consumer —
`ctutil` among them. The `ctutil` surface must therefore be right *now*,
because step 5 changes what is underneath it without changing it.

A set of **gate** groups re-runs what earlier steps already signed off,
together with proof that your tree builds and the artifact starts. If any of
them fails, the step is not satisfied, however much of this milestone you
implemented. Beyond the gate, this milestone's new behaviour is required in
independent groups; `TEST_INTERFACE.md` §7 lists them. A change that breaks
earlier work means the step is not satisfied, and new work must actually be
present.

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
| `ct-spec/CLIENT-API.md` §2 | the whole `ctutil` surface, the `embedded` flag and the -1 convention |
| `ct-spec/X509-FORK.md` §4.2, §4.3 | the precertificate rebuild that `embedded` depends on, and **which** extension each direction removes |
| `ct-spec/MERKLE.md` §1–§2 | hashing rules and inclusion proof verification |
| `ct-spec/CLIENT-API.md` §4.1 | log list schema v1, field by field |
| `ct-spec/CLIENT-API.md` §6.1 | `dnsclient`: `New`'s key handling, and the four query shapes — written to the spec, but not run here (TEST_INTERFACE.md §9) |
| `ct-spec/CLIENT-API.md` §1.3 | `CheckLogClient` at this release |
| `ct-spec/CLI.md` §4.4, §4.5 | `sctcheck`, `sctscan` and `findlog` |
| `ct-spec/RELEASE-MAP.md` §4 | the milestone on one screen |

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
  step 5 starts from, so keep it building at every point you might
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
