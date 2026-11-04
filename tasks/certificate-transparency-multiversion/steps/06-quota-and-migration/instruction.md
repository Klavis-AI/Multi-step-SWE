# Step 6 — quota and migration (certificate-transparency-go v1.0.19)

## 1. Implement this milestone

`/app` already holds the stack you delivered through step 5 at
certificate-transparency-go v1.0.17 — the codec, the forked parsers,
the RFC 6962 structures, `ctutil`, `loglist`, `dnsclient`, the client, the
utilities and the front end, with proof verification taken from the external
Merkle library and no `merkletree` package of your own. Continue that
implementation in place.

This milestone, certificate-transparency-go v1.0.19, makes resource
consumption explicit. The front end gains a quota surface: `--quota_remote` and
`--quota_intermediate` on `ct_server`, a quota user derived from the request
and another derived from the submitted certificate's intermediates, and that
identity threaded through to every backend call. The point is an error taxonomy
with a resource dimension — refusing because an allowance is exhausted is a
different answer from failing.

`migrillian` arrives: the tool that copies an existing CT log into a backend,
preserving ordering and resuming where it left off rather than restarting. At
this milestone only its command surface is in scope — its fourteen flags,
their types and defaults, and its exit statuses; the transfers themselves are
out of scope (`TEST_INTERFACE.md` §9).

Library changes ride along, three of them breaking in the small.
`LogClient.GetEntryAndProof` completes the read side, and
`(*ct.GetSTHResponse).ToSignedTreeHead()` lets a `get-sth` response convert
itself without a client. `trillian/ctfe.RequestLog` gains an eleventh method,
`IssueSCT`, so every implementation of that exported interface must grow one.
`trillian/util` gains `BuildLogLeaf` and `ExtraDataForChain`, the shared leaf
construction the front end and migrillian both use. And
`scanner.FetcherOptions` **replaces** `Quiet` with `Continuous`: code setting
the old field stops compiling, and `scanlog` loses `--quiet` with it.

`client.CheckLogClient` is unchanged at this release — it gained `BaseURI()` at
v1.0.17, which you have already done.

**Hard prerequisite for step 7:** step 7 removes an escape hatch from key
validation and adds a URL path prefix to the front end, both of which assume a
front end whose configuration and start-up paths are intact. Keep `ct_server`
starting and its flag set clean.

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
| `ct-spec/ARCHITECTURE.md` §2, §2.2 | the backend boundary, and the whole quota surface: what is charged, to whom, in what order |
| `ct-spec/CLI.md` §2.1 | `--quota_remote` and `--quota_intermediate` |
| `ct-spec/CLIENT-API.md` §8.2 | `InstanceOptions.RemoteQuotaUser` and `.CertificateQuotaUser` |
| `ct-spec/CLIENT-API.md` §8.3 | `GetTreeHead`, its `remoteQuota *string` parameter, and why the pointer is the switch; and, in the same section, **which** verified path `ValidateChain` returns — the chain the intermediate charges are built from |
| `ct-spec/CLIENT-API.md` §8.6 | `QuotaUserForCert` and `CertificateQuotaUserPrefix`, exact string format |
| `ct-spec/CLIENT-API.md` §8.5 | `RequestLog` — all eleven methods, and the call order on a successful add-chain |
| `ct-spec/CLIENT-API.md` §8.7 | `BuildLogLeaf` and `ExtraDataForChain` |
| `ct-spec/CLIENT-API.md` §1.2, §3 | `GetEntryAndProof`; `ToSignedTreeHead` |
| `ct-spec/CLIENT-API.md` §5.1 | `FetcherOptions`: `Continuous` in, `Quiet` out |
| `ct-spec/HTTP-API.md` §4.5 | the `get-entry-and-proof` response shape |
| `ct-spec/CLI.md` §4.7 | `migrillian`'s fourteen flags — and that it takes no config file here |
| `ct-spec/CLI.md` §4.1, §3.1 | `scanlog` losing `--quiet`; the `https://` default moves |
| `ct-spec/RELEASE-MAP.md` §6 | the milestone on one screen |

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
  step 7 starts from, so keep it building at every point you might
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
