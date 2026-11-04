# Step 13 — encryption level alignment

## Implement this milestone

Bring the workspace at `/app` up to the behaviour of release 0.23.45.

**What changes at this step.** the fix for GHSA-2mjx-qc3c-rqvc: TLS 1.3 handshake messages were accepted at the wrong encryption level when they followed a key-changing message inside the same record. The alignment check is extended to cover messages already received but not yet processed

**Migration.** Behaviour that steps 10 through 12 were required to have becomes a defect: a record carrying a handshake message after the one that ends a flight must now be refused.

A record carrying a ClientHello followed by another handshake message must draw a fatal alert. Steps 04–09 refused such a record, the deframer rewrite at step 10 began accepting it, and this step refuses it again — this time with the check covering messages that were already buffered. The refusal does not depend on how the record layer delivered the flight: a ClientHello reassembled from several records, whose closing fragment also carries a complete trailing handshake message, must be refused exactly as a single packed record is, because the check now covers every complete handshake message still held in the deframer and not only the tail of the record last read. The 0.23.31–0.23.45 release window also tightens OCSP handling in two directions at once, and both are in scope: your server must not **emit** an empty `CertificateStatus` (the guard is `if !ocsp.is_empty()`, backed by `PayloadU24<NonEmpty>`), and your client must **reject** one that a peer sends -- an empty OCSP response is a decode error, and the cases that carry it are the two `RejectEmptyOCSPResponse-*` names. Beyond that, adds certificate-compression support; both changes are exercised by the seven `bogo-*-new` conformance groups.

Everything required at the earlier steps must keep working. The cumulative workspace at `/app` already contains your previous step's tree; this milestone is an edit to it, not a fresh start. *Everything* means everything: the behaviour each earlier step established is read again here, one end-to-end path per milestone, and every out-of-band check those steps introduced is run again -- interoperability with an independent implementation, the handling of malformed `server_name` values and the ability to read a handshake message split across records among them, but that is an illustration and not the list. The list is `TEST_INTERFACE.md`: its section 4 names every observation made outside the conformance runner at this step, and section 6 names what was deliberately left out of scope. A regression in anything re-checked means this step is not satisfied, even though the capability was introduced earlier.

## Read these local references

Normative specifications, copied unmodified into the workspace:

- `/app/docs/specs/rfc8446.txt` — RFC 8446 — TLS 1.3
- `/app/docs/specs/rfc9147.txt` — RFC 9147 — DTLS 1.3 (record-layer vocabulary)

Additional files in the workspace:

- `/app/WORKSPACE_HANDOFF.md` — what the previous step left you, and what is expected of what you leave behind.
- `/app/TEST_INTERFACE.md` — the binaries, arguments and behaviour your tree must provide. Read this before you rename anything. Its §6 lists what is out of scope for this step.
- `/app/docs/specs/README.md` — what is in the specification corpus and which document answers which kind of question.

## Meta requirements

- The budget for this step is shown by `bash /app/timer.sh`. The configured
  timeout is authoritative if the two ever disagree.
- `/app` is cumulative and is carried into the next step. Leave it building and
  leave your previous steps' behaviour intact; do not delete work you inherited
  in order to make this step's changes simpler.
- This environment has no network access, and everything the build needs is already installed. The upstream project's source, its releases, mirrors and package registries are unreachable, so a copy cannot be fetched by any route. Searching for, downloading, copying, wrapping or invoking
  an existing implementation of this milestone, or any task-specific answer, is
  prohibited.
- `/tests`, later steps, and any solution material are off limits. Do not read
  them, and do not write anything into `/app` that is meant to be found by
  them.
- Your tree must build and work when driven over real sockets, as a peer
  would drive it, without calling private functions. Anything observable
  that way, a user of this library could see too.
- Check your own work locally before you finish: build the workspace, run the
  examples against each other, and read `/app/TEST_INTERFACE.md` for the exact
  binaries and arguments to use.
