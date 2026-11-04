# Step 2 — multiple backends (certificate-transparency-go v1.0.5)

## 1. Implement this milestone

`/app` already holds the RFC 6962 stack you delivered for step 1 at
certificate-transparency-go v1.0 — the `tls` codec, the forked
`asn1` and `x509`, the root package's structures and signature inputs,
`merkletree`, the client, the utilities, and the `trillian/ctfe` front end with
`ct_server`. Continue that implementation in place.

This milestone, certificate-transparency-go v1.0.5, lets one front end
serve logs that live on **different backends**. The configuration file gains a
second legal shape — `LogMultiConfig`, a named set of `LogBackend` entries plus
a set of logs each naming its backend — and the old single-backend shape is
*converted* into it on load, so that exactly one validator sees both. That
validator performs exactly four checks and no others: it rejects an empty backend
name, rejects an empty backend spec, rejects a duplicated backend name, and
rejects a log naming a backend that is not defined. Duplicate-*prefix* checking is
a different rule that arrives at v1.0.10, so at v1.0.5 it must be **absent**:
a group hands the validator a config whose two logs share a prefix and requires it
to **accept** that config, so adding the later check does not satisfy that
requirement (CONFIG.md §4.4).

Alongside it: a new `logid` package, so a log id can no longer be mistaken for
a backend tree id; `jsonclient`'s three request helpers now return the raw
response body as well as the parsed one, and its retry backoff is rewritten
with a multiplier and jitter; `client.RspError` appears; `x509` gains the
structured error surface; `x509util` gains two new files — the "possible PEM"
readers and `CRLToString` — respells its `Oid…` helpers to `OID…` and drops
`OidFromNamedCurve`; `SetUpInstance` takes an `InstanceOptions` struct; and two
new programs, `certcheck` and `crlcheck`, bring the binary count to ten.

**Hard prerequisite for step 3:** step 3 is about what the front end does when
it signs, which means the front end must start from a config file and reach its
signing path. Keep the loader, the validator and `SetUpInstance` working.

A set of **gate** checks re-runs what step 1 already signed off — the `tls`
codec, the `x509` fork, `merkletree`, and the front end brought up from a
step-1 configuration file plus `--log_rpc_server`. If any of them fails, the
step is not satisfied, however much of this milestone you implemented. Beyond
the gate, this milestone's own capability is required, in independent groups:
a change that breaks step 1's work means the step is not satisfied, and new
work must actually be present.

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
| `ct-spec/CONFIG.md` §3 | the two config shapes and the conversion between them |
| `ct-spec/CONFIG.md` §4 | the loader entry points, and which validation rules exist at v1.0.5 |
| `ct-spec/CLIENT-API.md` §5.5 | the `logid` package in full |
| `ct-spec/CLIENT-API.md` §1.1 | the three changed `jsonclient` signatures and the new backoff |
| `ct-spec/CLIENT-API.md` §8.1–§8.2 | `SetUpInstance` and `InstanceOptions` at this release |
| `ct-spec/X509-FORK.md` §5.2 | the structured `x509.Error` surface, and the two other `x509` files that arrive with it |
| `ct-spec/CLIENT-API.md` §5.2 | `x509util` at v1.0.5 — the two "possible PEM" readers, the `Oid…` → `OID…` respellings and the one removal, `CRLToString`, and the `GeneralNames` renderers. Read its release table, not only the signature block |
| `ct-spec/CLI.md` §4.3 | `certcheck` and `crlcheck` |
| `ct-spec/RELEASE-MAP.md` §2 | the whole milestone on one screen |

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
  step 3 starts from, so keep it building at every point you might
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
