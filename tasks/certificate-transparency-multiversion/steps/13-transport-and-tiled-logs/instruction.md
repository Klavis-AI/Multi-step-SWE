# Step 13 — transport and tiled logs (certificate-transparency-go v1.3.3)

## 1. Implement this milestone

`/app` already holds the stack you delivered through step 12 at
certificate-transparency-go v1.2.0 — the TLS codec, the forked
`asn1` and `x509`, the RFC 6962 structures, `ctutil`, `loglist3` as the only
log-list package, the Cobra-based `ctclient`, the submission proxy,
`migrillian`, `internal/witness`, and a front end carrying the whole admission
history down to issuance-chain storage with its cache and its two leaf
layouts. Continue that implementation in place.

This milestone, certificate-transparency-go v1.3.3, is the last in the
series and the broadest. It has no single centre: a dozen changes land at once
across the front end, the parser, the log list, the storage layer and four
command lines, and almost none of them depends on another.

Give the server a second transport. `--tls_certificate` and `--tls_key`
**together** move its own listener onto TLS, minimum version 1.2. Both are
needed: given only one of them the server starts as usual and keeps serving
plain HTTP.
`--trillian_tls_ca_cert_file` does the same for the dial to the backend, using
that file as the CA roots. `--http_idle_timeout` overrides the server's idle
timeout when it is positive and changes nothing when it is negative.

Add a time dimension to admission. A submission is *non-fresh* when the leaf's
`NotBefore` is older than `--non_fresh_submission_age` (default 24h) measured
against the instance's current time. When `--non_fresh_submission_limit` is set
— the form is `"<count>/<duration>"`, and empty disables the mechanism — those
submissions alone pass through a token bucket whose burst is
`--non_fresh_submission_burst`, and one over the limit is refused with 429 and
the exact body the corpus gives. Fresh submissions are never limited. Then
bound the two write endpoints' request bodies with `--max_cert_chain_size`
(default 512000), answering 413 above it, and leave the read endpoints
unbounded.

Publish two more things over HTTP. `/log.v3.json` is per-log and prefixed like
the RFC endpoints, serving the log's key, its log id, its merge delay, its
temporal interval whenever either bound is configured — both keys present, the
one that is not configured empty — and the new `logv3_url` config field; with that field unset the endpoint is a 404 with
the documented message, so it is off by default. And make `get-entries`
conditional about caching: an answer shorter than the range asked for is the
tail of the tree and gets a short max-age, while a full answer keeps the long
one, with the other cacheable endpoints untouched.

In the library, teach the log list about tiled logs — a second kind of log,
addressed by a submission URL and a monitoring URL, sitting beside the ordinary
logs on the same operator with its own parallel family of finders, of which the
URL finder matches either URL. Tighten the parser: the signature algorithm a
certificate carries twice, once inside the signed TBS and once in the outer
unsigned structure, must match byte for byte, and a mismatch is fatal — but the
bare TBS parse is exempt, because it has only one identifier to compare. Add
the PostgreSQL issuance-chain driver behind the storage interface you already
have, with its own connection-string validation message, and split the
leaf-building service into a direct form that holds no storage and an indirect
one that puts a hash in the leaf and the chain in storage. Smaller: an
authorization option on the JSON transport, `--continuous` and its companions
on the preloader, and `--bearer_token` on the hammer.

Finally, take two things away. `migrillian` loses etcd entirely — three flags
gone, and without `--force_master` it refuses to run and says why. And
`internal/witness` is deleted: the whole subtree and its four programs, which
takes the tree from 18 to 14 `package main` directories.

**Hard prerequisite for step (none — this is the last step):** there is none, and that is the
only time it is true in this series. v1.3.3 is the last anchor, nothing
downstream inherits this workspace, and the usual argument for restructuring
early does not apply. What replaces it is the inherited obligation. Five paths
through the earlier work are re-run as gate groups — the TLS presentation
codec, the `loglist3` lookups, the front end's endpoint contract,
issuance-chain storage, and `add-chain` end to end — beside a sixth that
builds and vets the tree. The two non-additive changes of this step are the
ones that can damage them: a parser tightening that must not touch the bare
TBS path, and a deletion the tree no longer compiles **without**. That second
one is worth saying plainly, because it is the one thing in this step's gate
that the tree you were handed does not already satisfy: v1.3.3's
pinned `go.mod` drops the two modules only `internal/witness` imported, so
until that subtree is gone nothing in the workspace builds, no binary is
produced for any group to run, and the step is not satisfied no matter what
else you finish. Do the deletion first. `TEST_INTERFACE.md` §3.5 has the detail.

A set of **gate** groups re-runs what earlier steps already signed off,
together with proof that your tree builds and the artifact starts. If any of
them fails, the step is not satisfied, however much of this milestone you
implemented. Beyond the gate, this milestone's new behaviour is required in
independent groups; `TEST_INTERFACE.md` §7 lists them. A change that breaks
earlier work means the step is not satisfied, and new work must actually be
present. Nothing here is satisfied by
finishing one family perfectly while another is missing, and this is the step
where that matters most: a dozen unrelated changes means breadth is the whole
strategy. Get one observable behaviour working per change, then deepen.

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
| `ct-spec/RELEASE-MAP.md` §13 | the whole milestone on one screen — use it as the checklist |
| `ct-spec/CLI.md` §2.1 | the 31 `ct_server` flags, with types and defaults |
| `ct-spec/CLI.md` §2.3 | what TLS and the idle timeout change about running the server |
| `ct-spec/HTTP-API.md` §6.3 | rate limiting, body size and transport, in one place |
| `ct-spec/HTTP-API.md` §7, §7.1 | `/log.v3.json`, and the bare root path |
| `ct-spec/HTTP-API.md` §4.7 | the two `get-entries` cache lifetimes |
| `ct-spec/CONFIG.md` §2.6, §2.7 | the PostgreSQL driver and `logv3_url` |
| `ct-spec/CLIENT-API.md` §4.3 | `TiledLog`, `TiledLogs` and the finders, field by field |
| `ct-spec/CLIENT-API.md` §8.4 | storage, cache, and the service split at this release |
| `ct-spec/CLIENT-API.md` §1.1 | the authorization option on the JSON transport |
| `ct-spec/CLIENT-API.md` §6.3 | what `internal/witness` was, and that it goes |
| `ct-spec/X509-FORK.md` §5.4 | the two algorithm identifiers, and which parse entry point is exempt |
| `ct-spec/CLI.md` §1 | the 14 programs, and the four that are no longer among them |
| `ct-spec/CLI.md` §4.2, §4.7 | the preloader's continuous mode; migrillian without etcd |
| `ct-spec/RELEASE-MAP.md` §14 | the absences — re-read this before you believe you are finished |

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
  step (none — this is the last step) starts from, so keep it building at every point you might
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
