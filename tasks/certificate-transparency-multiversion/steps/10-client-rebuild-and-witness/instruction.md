# Step 10 — client rebuild and witness (certificate-transparency-go v1.1.3)

## 1. Implement this milestone

`/app` already holds the stack you delivered through step 9 at
certificate-transparency-go v1.1.2 — the TLS codec, the forked
`asn1` and `x509` with their `lax` relaxations, the RFC 6962 structures,
`ctutil`, the v1 and v2 log-list packages, the client and utility programs,
and the front end with the inverted admission rule, frozen STHs, `/healthz`,
EKU filtering, get-entries alignment and binary-encoded configuration.
Continue that implementation in place.

This milestone, certificate-transparency-go v1.1.3, replaces an interface
that is not a wire format: the command-line grammar of `ctclient`.

Rebuild it. Up to v1.1.2 the tool is a single `flag`-package `main`
— global flags first, one positional command word last. At v1.1.3 that
file goes and the tool becomes a root command whose subcommands live in
`client/ctclient/cmd/`, with `main.go` reduced to a shim that runs it. The
command word comes first and its own flags come after it. Five connection
flags become persistent and bind on either side of it. Canonical command names
become hyphenated and every old spelling survives as a declared alias. `size`
and `prev_size` retype from Int64 to Uint64 and their default moves from `-1`
to `0`. No subcommand accepts a positional argument. The logging library's
flags are folded into the new flag set so they keep working. An unknown
command, an unknown flag or a surplus positional each produce a diagnostic on
stderr and exit `255`, not 1. Give every command the usage line, short
description, aliases and flags the reference lists, because the generated help
text is the interface a caller reads the tree through.

Move `PEMCertPool` from `trillian/ctfe` to `x509util` without changing it, and
retype everything that holds one: the validation options' root pool, the
constructor that builds them, and the v2 log-list root map. Nothing may remain
under the old name. Delete `ingestor/` and `watcher/`; nothing imports them.

Add the rest of the release around that. Bring `loglist3/` into being with the
full v3 surface — a list version and publication timestamp, operator contact
details, per-log previous-operator history, the six-way state union with a
final tree head for a read-only log, the finder family, the selection helpers,
and construction from both plain and detached-signature JSON. Build
`internal/witness/`: a service that holds the newest tree head it has seen for
each configured log, verifies a caller's newer one for consistency against it,
stores and co-signs what it can reconcile, refuses what it cannot, and serves
all of that over the paths the reference fixes, together with the four
programs that run and feed it. Move HTTP 429 onto the same retrying arm as 503
in `PostAndParseWithRetry`, honouring `Retry-After` the same way. Give
`HammerCTLog` a leading context and stop the loop when it is cancelled. Add
`is_readonly` to the log configuration as field 19, so that a log so
configured never registers the two write handlers — `add-chain` and
`add-pre-chain` answer 404, not 405 and not 403 — while every read endpoint
serves as before.

**Hard prerequisite for step 11:** step 11 deletes the
v1 log-list package outright and migrates its four dependants straight to v3,
skipping v2 entirely, and changes `ctclient`'s persistent log-list default to
the v3 constant. A v3 package that is missing, partial, or not a drop-in for
the finders the v1 callers use leaves that step with nothing to migrate to,
and a `ctclient` still built on the `flag` package has no persistent flag to
change. Both must be complete now.

A set of **gate** groups re-runs what earlier steps already signed off,
together with proof that your tree builds and the artifact starts. If any of
them fails, the step is not satisfied, however much of this milestone you
implemented. Beyond the gate, this milestone's new behaviour is required in
independent groups; `TEST_INTERFACE.md` §7 lists them. A change that breaks
earlier work means the step is not satisfied, and new work must actually be
present. The pieces of this
milestone are independent of one another as well, so breadth is required: the
command tree, the type move and the v3 parser are all needed, not one of the
three done perfectly while the others are absent.

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
| `ct-spec/CLI.md` §3.1 and §3.2 | both halves of the `ctclient` grammar — the break between them is the milestone |
| `ct-spec/CLI.md` §1 | the program inventory this milestone joins |
| `ct-spec/CLI.md` §4.10 | the witness programs and their flag sets and defaults |
| `ct-spec/CLI.md` §4.8 | `HammerCTLog`'s new leading context parameter |
| `ct-spec/CLIENT-API.md` §5.3 | `PEMCertPool`'s surface and where it now lives |
| `ct-spec/CLIENT-API.md` §8.3 | the validation options and constructor that retype with it |
| `ct-spec/CLIENT-API.md` §4.3, §4.2 | the v3 log-list schema, and the v2 neighbour it is defined against |
| `ct-spec/CLIENT-API.md` §4 | the shared log-list surface all three packages carry |
| `ct-spec/CLIENT-API.md` §6.3 | the witness: its paths, its request and response types, its verifier |
| `ct-spec/CLIENT-API.md` §1.1 | the retry state machine, and 429's new arm |
| `ct-spec/HTTP-API.md` §6.2 | read-only against mirror against frozen, and what each returns |
| `ct-spec/CONFIG.md` §1, §1.1 | `is_readonly` as field 19, and why the field number is itself observable |
| `ct-spec/CONFIG.md` §2.5 | serving mode, field by field |
| `ct-spec/MERKLE.md` §4.2 | which proof library is in force at this release |
| `ct-spec/RELEASE-MAP.md` §10, §14 | the milestone on one screen, and the directory listing that proves it |

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
  step 11 starts from, so keep it building at every point you might
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
