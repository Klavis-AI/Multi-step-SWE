# Step 8 — admission inversion (certificate-transparency-go v1.1.0)

## 1. Implement this milestone

`/app` already holds the stack you delivered through step 7 at
certificate-transparency-go v1.0.21 — the TLS codec, the forked
`asn1` and `x509`, the RFC 6962 structures, `ctutil`, the log-list and client
packages, the utilities, and the front end with quota, a handler prefix,
per-log paths, gossip and key validation with no escape hatch. Continue that
implementation in place.

This milestone, certificate-transparency-go v1.1.0, is the largest in the
series and the only one where the *same configuration file* produces the
*opposite* admission decision.

Invert the expiry rule. Up to v1.0.21 `reject_expired` was wired
into path building as "do not disable time checks", so it rejected an expired
intermediate as readily as an expired leaf, and nothing could express the
opposite. At v1.1.0 it becomes an explicit **leaf-only** test that runs
*before* path building — reject if the leaf's `NotAfter` is in the past, and
be silent about a certificate that is not yet valid — after which path
building always runs with time checks disabled and an explicit current time.
The comparison is strict: a leaf whose `NotAfter` is exactly the current time
has not expired. Add `reject_unexpired` as its mirror, reading the same
predicate the other way, so that at the decision level the two rules partition
every certificate between them and a log with both set admits nothing; and
refuse to start when both are configured, for that reason. Build the
validation options through a constructor that takes the current time rather
than reading a clock.

Add the rest of the release around it: `reject_extensions`, rejecting by
dotted-decimal OID and failing at start-up on an OID it cannot parse;
`frozen_sth`, a signed STH validated against the log's own key at start-up and
then served by `get-sth` forever while writes keep working; `/healthz`
answering `200` with the body exactly `ok`, unconditionally, at the root of
the HTTP endpoint; `--mask_internal_errors`; and the withdrawal of the
defaults for `--metrics_endpoint` and `--log_rpc_server`, so a process with no
backend configured must say so instead of dialling `localhost:8090`.

In the parsers, add the `lax` tag and the three named relaxations it enables,
and make the `ParseCertificate` family retry with it, returning a usable
certificate together with a `NonFatalErrors` carrying the strict error it
recorded. Bring `ctpolicy/`, `submission/`, `schedule/`, `loglist2/` and
`watcher/` into being, and rename `certcheck`'s flags — one of which reverses
its polarity and its default at the same time.

**Hard prerequisite for step 9:** step 9 lives inside
the surface you build here. It defaults one `ct_server` flag true and then
deletes it, adds another, filters by EKU on top of this admission path, and
accepts a binary-encoded configuration through the same parser. An admission
path that is not restructured now, or a `ct_server` whose flag set is
approximate, makes that step unreachable. The `lax` tag must also exist: the
next milestone widens what parses, and widening nothing is not a change.

A set of **gate** groups re-runs what earlier steps already signed off,
together with proof that your tree builds and the artifact starts. If any of
them fails, the step is not satisfied, however much of this milestone you
implemented. Beyond the gate, this milestone's new behaviour is required in
independent groups; `TEST_INTERFACE.md` §7 lists them. A change that breaks
earlier work means the step is not satisfied, and new work must actually be
present. A milestone-sized change
that is *half* applied leaves the inherited behaviour intact while the new
behaviour is missing, so apply the inversion completely before polishing
anything.

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
| `ct-spec/HTTP-API.md` §5.2 | the inversion itself — the one section to read twice |
| `ct-spec/HTTP-API.md` §5.3 | `reject_extensions` and its messages |
| `ct-spec/HTTP-API.md` §7 | `/healthz`, `/`, `/metrics` and CORS |
| `ct-spec/HTTP-API.md` §1, §1.1 | the path grammar, prefix normalisation, and the three paths a handler prefix never moves |
| `ct-spec/HTTP-API.md` §2, §2.1 | the wrapper's status/body table, the error-body frame, and what `--mask_internal_errors` changes about a 500 |
| `ct-spec/HTTP-API.md` §3.1 | `add-chain`'s error rows, and the one message v1.1.0 prefixes with the log's own identity |
| `ct-spec/CONFIG.md` §2.4, §2.5 | the new config fields and their start-up validation |
| `ct-spec/CLI.md` §2.1 | the 18 `ct_server` flags and the two withdrawn defaults |
| `ct-spec/CLI.md` §4.3 | the `certcheck` rename table |
| `ct-spec/CLI.md` §1, §4.9 | the 18 programs, and the submission server and hammer |
| `ct-spec/X509-FORK.md` §2.1, §2.2 | the `lax` tag and exactly three relaxations |
| `ct-spec/X509-FORK.md` §3.2 | the `ParseCertificate` family's retry and `NonFatalErrors` |
| `ct-spec/CLIENT-API.md` §1.1 | `RspError` moving to `jsonclient`, the alias left behind, and `Options.UserAgent` |
| `ct-spec/CLIENT-API.md` §8.1, §8.2 | `SetUpInstance` returning `*Instance`; `InstanceOptions` |
| `ct-spec/CLIENT-API.md` §8.6, §8.7 | how a distinguished name renders; what goes into a queued leaf |
| `ct-spec/CLIENT-API.md` §7, §4.2 | `ctpolicy`, `submission`, `schedule`, `loglist2` |
| `ct-spec/RELEASE-MAP.md` §8 | the milestone on one screen |

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
  step 9 starts from, so keep it building at every point you might
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
