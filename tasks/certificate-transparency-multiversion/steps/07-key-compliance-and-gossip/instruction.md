# Step 7 — key compliance and gossip (certificate-transparency-go v1.0.21)

## 1. Implement this milestone

`/app` already holds the stack you delivered through step 6 at
certificate-transparency-go v1.0.19 — the TLS codec, the forked
`asn1` and `x509`, the RFC 6962 structures, `ctutil`, `loglist`, `dnsclient`,
the client with a four-method `CheckLogClient`, the utilities, the front end
with its quota surface, and `migrillian`. Continue that implementation in
place.

This milestone, certificate-transparency-go v1.0.21, takes something away
and cannot be reversed from outside the process.

Remove the non-compliant-key escape hatch. RFC 6962 permits a log key to be
ECDSA on P-256 or RSA of at least 2048 bits, and constructing a signature
verifier over anything else must fail. Until v1.0.19 that failure
could be waived by a command-line flag declared in the **root** package, which
put it on the command line of every binary in the tree. Delete the flag from
all of them. Keep the capability as an exported package variable defaulting to
false, so a program that imports the library can still opt in and no command
line can.

Give the log an address. Add a global URL path prefix for every handler that
does not carry its own, and a per-log override that replaces it — the override
is normalised by trimming slashes from both ends and prepending exactly one,
so that three spellings of the same override are one prefix. The global prefix
is used as given, empty included. Leave `/` and `/metrics` unprefixed. Add the
two advertised merge delays to the configuration as promises, validated but not
enforced.

Introduce mirrors. A mirrored log carries a public key and must not carry a
private one. Withdraw the free function that fetched a tree head and replace
it with an interface, with a log-backed implementation and a mirror-backed one
behind it, and move the remaining explicit parameters of the instance set-up
call inside its options struct.

Teach the parsers Subject Information Access — timestamps and CA
repositories — and RFC 3779 address and AS-number blocks. SIA is supported in
**both** directions and both are required: a certificate template carrying
either list must *emit* the extension, in the exact shape
`TEST_INTERFACE.md` §6 fixes, and a parse must recover the two fields from
it. The two families are deliberately not equally forgiving, and both halves
are in scope: an SIA extension whose body is *empty* is non-fatal, but an SIA
body that does not decode — trailing bytes after its `SEQUENCE` included — is
a fatal parse error, while every RPKI malformation, trailing data included, is
non-fatal and leaves the certificate usable. Add the `gossip/minimal` tree with its two
programs and, most durably, the extension type that carries an STH inside a
certificate; that extension's decoder accepts **exactly** one encoded
structure and rejects a body with anything left over.

Two command lines change beyond the flag removal, and both are in scope.
`migrillian` stops being a single-log tool: its per-log flags give way to a
configuration file plus master-election flags (CLI.md §4.7). `certcheck`
gains one flag (CLI.md §4.3). And `ct_server` changes how it *stops*: on
SIGINT or SIGTERM it now shuts the listener down gracefully and exits **0**
rather than aborting with 1 (CLI.md §2.3).

**Hard prerequisite for step 8:** step 8 is the
largest release in the series and it edits the admission path, the
configuration message and the `ct_server` flag set all at once. It needs the
instance set-up call already reduced to `(ctx, opts)`, the STH getter already
an interface — a frozen-STH implementation joins the two you add here — and
the handler-prefix machinery already in place, because `/healthz` is defined
by *not* being prefixed. Approximations here become contradictions there.

A set of **gate** groups re-runs what earlier steps already signed off,
together with proof that your tree builds and the artifact starts. If any of
them fails, the step is not satisfied, however much of this milestone you
implemented. Beyond the gate, this milestone's new behaviour is required in
independent groups; `TEST_INTERFACE.md` §7 lists them. A change that breaks
earlier work means the step is not satisfied, and new work must actually be
present. The removal is checked by
compilation and by command-line behaviour, not by looking for a file, so a
flag left registered anywhere in the tree is visible.

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
| `ct-spec/CLI.md` §2.1 | the 17 flags, `handler_prefix`, and the note on the removed flag |
| `ct-spec/CLIENT-API.md` §3 | the key-compliance rules and the two shapes the waiver takes |
| `ct-spec/HTTP-API.md` §1.1 | how a path is assembled from the two prefixes |
| `ct-spec/CONFIG.md` §2.1, §2.2 | `override_handler_prefix` and the merge delays |
| `ct-spec/CONFIG.md` §2.5 | `is_mirror` and what it forbids |
| `ct-spec/CLIENT-API.md` §8.1, §8.2 | `SetUpInstance` losing two parameters; `InstanceOptions` |
| `ct-spec/CLIENT-API.md` §8.3 | `STHGetter` replacing `GetTreeHead` |
| `ct-spec/CONFIG.md` §2.4, §4.1, §4.3 | the EKU name table, the half-open `not_after` window, and the per-log validator they move into — with `ValidatedLogConfig` and `LogBackendMap` |
| `ct-spec/X509-FORK.md` §5.3 | Subject Information Access and RFC 3779, and which of the two tolerates broken DER |
| `/app/TEST_INTERFACE.md` §5, §6 | the rest of those two extensions: what `CreateCertificate` must emit for SIA and what a parse may quietly drop, the RFC 3779 wire grammar the fixtures encode (that RFC is not in `ct-spec/rfc/`), the parse verdicts that stay fatal at this release, and what instance set-up must refuse |
| `ct-spec/X509-FORK.md` §5.7 | secp192r1, the named-curve OIDs, `OIDFromNamedCurve`, and why a bare SPKI is still refused |
| `ct-spec/X509-FORK.md` §2.2, §3, §3.1 | `IsFatal`, what lands in `NonFatalErrors`, and how a `PrintableString` with out-of-set characters is recovered |
| `ct-spec/CLIENT-API.md` §6.2 | `x509ext`, `LogSTHInfo` and the two OIDs |
| `ct-spec/TLS-CODEC.md` §1 | what `Unmarshal` does with leftover bytes, and which callers refuse them |
| `ct-spec/CLI.md` §4.3, §4.7, §4.10 | `certcheck`'s new flag; `migrillian`'s config-file rework; `goshawk` and `gosmin` with their types and defaults |
| `ct-spec/CLI.md` §2.3 | the signals `ct_server` handles and the status it exits with |
| `ct-spec/RELEASE-MAP.md` §7 | the milestone on one screen |

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
  step 8 starts from, so keep it building at every point you might
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
