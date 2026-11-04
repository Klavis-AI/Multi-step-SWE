# Step 5 — outsourced proofs (certificate-transparency-go v1.0.17)

## 1. Implement this milestone

`/app` already holds the stack you delivered through step 4 at
certificate-transparency-go v1.0.13 — the codec, the forked parsers,
the RFC 6962 structures, your own `merkletree` verifier, `ctutil`, `loglist`,
`dnsclient`, the client, the utilities and the front end. Continue that
implementation in place.

This milestone, certificate-transparency-go v1.0.17, is **subtractive**.
The product stops carrying its own Merkle proof verifier: the `merkletree`
package is **deleted**, and proof verification is obtained from the backend
project's Merkle library instead. Every consumer inside the product —
`ctclient`, `ctutil`'s `LogInfo`, the integration tooling — is rewritten to
call it. The hashing rules are unchanged, because they are RFC 6962's; what
changes is who owns the code.

Deleting a capability is as much a milestone as adding one, and it is observed
the same way: by what is no longer reachable. A tree that still contains a
`merkletree` package has not performed this milestone, however good that
package is. Once it is removed, a probe that imports the path must fail to
build — alongside a control that must still build, so deleting the wrong
thing does not help.

Two additions come with it: `LeafHashForLeaf` in the root package, and
`scanner/fetcher.go`, which factors the batch-walking loop out of the scanner
so other tools can reuse it.

**The deletion is the headline, not the whole release**, and the rest of it is
equally in scope. v1.0.17 also moves the subjectAltName syntax check
out of the forked parser, completes the `x509.VerifyOptions` disable-switch set
and tightens leaf-EKU matching, puts the whole `ct_server` mux behind a
permissive CORS wrapper and narrows its catch-all to the exact path `/`,
teaches `certcheck --validate` to read a multi-certificate file as a chain,
adds tracing flags to `ct_server` and `--end_index` to `scanlog`, withdraws
`ctfe.LogEntry`, and puts `BaseURI()` on the clients. Every one of those has a
requirement group of its own. The table in section 2 says where each is
written down, and `TEST_INTERFACE.md` section 10 lists them together.

**Hard prerequisite for step 6:** step 6 extends the front end with an explicit
quota surface and begins the log-migration tool, both of which sit on top of a
tree that compiles cleanly after this removal. Finish the removal — do not
leave a half-deleted package or a dangling import.

A set of **gate** groups re-runs what earlier steps already signed off,
together with proof that your tree builds and the artifact starts. If any of
them fails, the step is not satisfied, however much of this milestone you
implemented. Beyond the gate, this milestone's new behaviour is required in
independent groups; `TEST_INTERFACE.md` §7 lists them. A change that breaks
earlier work means the step is not satisfied, and new work must actually be
present. Note the asymmetry this step
creates: the vectors in `ct-spec/MERKLE.md` must still verify afterwards, so
repoint your consumers before you delete, not after.

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
| `ct-spec/MERKLE.md` §4 | the withdrawal, and which import path is in force at this release |
| `ct-spec/MERKLE.md` §1–§3 | the hashing and proof rules that do **not** change |
| `ct-spec/CLIENT-API.md` §3 | `LeafHashForLeaf`, new in the root package |
| `ct-spec/CLIENT-API.md` §5.1 | the `Fetcher` surface extracted from the scanner |
| `ct-spec/CLIENT-API.md` §2.1 | `LogInfo`, unchanged in signature, changed underneath |
| `ct-spec/RELEASE-MAP.md` §5, §14 | the milestone, and the structural facts a group can check |
| `ct-spec/X509-FORK.md` §3.1 | the **SAN relaxation** at this release — a malformed `dnsName` or `rfc822Name` stops failing the parse and is stored verbatim |
| `ct-spec/X509-FORK.md` §5.6 | the six `VerifyOptions` disable switches, and the leaf-EKU matching rule that tightens with them |
| `ct-spec/HTTP-API.md` §7 | the permissive **CORS wrapper** now in front of the whole `ct_server` mux, and the narrowing of the catch-all to the exact path `/` |
| `ct-spec/HTTP-API.md` §5.1 | where the CTFE stops hand-editing certificates and sets the §5.6 switches instead |
| `ct-spec/CLI.md` §4.3 | what `certcheck --validate` validates — a multi-certificate file is read as a chain when neither pool flag is given |
| `ct-spec/CLI.md` §2.1, §4.1, §3.1 | `ct_server`'s three new tracing flags; `scanlog --end_index`; `ctclient consistency` |
| `ct-spec/CLIENT-API.md` §8.3 | `ctfe.LogEntry` and `ctfe.NewLogEntry`, withdrawn here |
| `ct-spec/CLIENT-API.md` §1.1, §1.3, §6.1 | `BaseURI()` on the clients, and `CheckLogClient` gaining it as its **first** method |

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
  step 6 starts from, so keep it building at every point you might
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
