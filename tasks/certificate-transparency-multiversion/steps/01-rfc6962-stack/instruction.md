# Step 1 — the RFC 6962 stack (certificate-transparency-go v1.0)

## 1. Implement this milestone

Build the RFC 6962 Certificate Transparency stack in `/app`, from nothing. The
workspace is an empty Go module whose path is
`github.com/google/certificate-transparency-go`; everything below is yours to
write.

This milestone is certificate-transparency-go v1.0 and it has two
halves.

The **library**: a reflective TLS presentation-language codec (`tls/`); a fork
of `encoding/asn1` and `crypto/x509` that parses certificates the standard
library rejects and reports what was wrong without discarding the result
(`asn1/`, `x509/`), including precertificate handling; the RFC 6962 data
structures and their exact signature inputs in the root package; a Merkle tree
verifier with inclusion and consistency proofs (`merkletree/`); an HTTP client
for the eight endpoints (`client/`, `jsonclient/`); and the utilities built on
them (`scanner/`, `preload/`, `fixchain/`, `x509util/`).

The **personality**: `trillian/ctfe`, the front end that validates submitted
chains against configured roots, issues Signed Certificate Timestamps, and
talks to a Merkle-tree backend over gRPC — plus `ct_server`, the program that
configures and runs it. Eight `package main` directories exist at this release
and all eight must build.

**Hard prerequisite for step 2:** every later milestone is a change to this
one. Step 2 adds a second shape of configuration file to the same loader, so
the configuration path must exist and work; steps 3 to 13 extend the codec, the
fork, the client and the front end in place. A tree that does not compile at
the end of this step is what step 2 inherits.

The **gate** is proof of life and nothing else: the whole tree compiles, all
eight commands build, and `ct_server` starts and serves the log its
configuration names. Nothing precedes this step, so the gate has no earlier
milestone to re-run. If the gate does not hold, the step is not satisfied,
however much of the stack you built. Beyond the gate, this milestone is
covered by **behaviour groups**, each one behaviour family, checked on its
own. `TEST_INTERFACE.md` section 7 lists the groups. Breadth across the
codec, the fork, the structures, the proofs, the client and the front end is
expected, not depth in any single corner.

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
| `ct-spec/ARCHITECTURE.md` (all) | how the two halves fit together and where the backend boundary is |
| `ct-spec/TLS-CODEC.md` §1–§4 | the codec, its struct tags and its error contract |
| `ct-spec/TLS-CODEC.md` §5 | every CT structure's exact wire layout, with vectors |
| `ct-spec/rfc/rfc6962.txt` §1–§4 | the normative data structures, hashing rules and endpoints |
| `ct-spec/rfc/rfc5246.txt` §4 | the TLS presentation language the codec implements |
| `ct-spec/X509-FORK.md` §1–§4 | why the fork exists, `NonFatalErrors`, precertificates |
| `ct-spec/MERKLE.md` §1–§4.1 | hashing rules, proof verification, the `merkletree` surface |
| `ct-spec/HTTP-API.md` §1–§6 | paths, wrappers, request and response bodies, admission control, and how backend errors become HTTP statuses |
| `ct-spec/CONFIG.md` §1–§4 | the log config message, its key handlers, where the backend address comes from, and how the file is parsed |
| `ct-spec/CLI.md` preamble, §1–§3 | the flag grammar every program shares (including the logging library's own flags), the program inventory, and the `ct_server` and `ctclient` flag sets |
| `ct-spec/CLIENT-API.md` §1, §3, §5, §8 | the client surface, the root package's signature verifier, and the front-end entry points |
| `ct-spec/GLOSSARY.md` | any term above you do not recognise |

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
  step 2 starts from, so keep it building at every point you might
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
