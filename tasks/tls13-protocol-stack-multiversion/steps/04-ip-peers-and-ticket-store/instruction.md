# Step 04 — ip peers and ticket store

## Implement this milestone

Bring the workspace at `/app` up to the behaviour of release 0.21.0.

**What changes at this step.** the certificate of a peer named by an IP address is checked against the `iPAddress` entry in its subjectAltName, where the previous release refused every peer named that way before it looked at a certificate at all; a client session store that holds several TLS 1.3 tickets and consumes each one exactly once, per RFC 8446 Appendix C.4; a configurable server ticket count whose default rises from one to four; `Error` loses its `String` payloads and becomes `non_exhaustive`; QUIC connection types; and a certificate whose bytes cannot be parsed is now refused with the `bad_certificate` alert, where the previous release sent `decode_error` when your client was the one parsing it and `access_denied` when your server was — RFC 5246 §7.2.2 defines the first as "a certificate was corrupt", the second as a field out of range or a wrong message length, and `access_denied` for a *valid* certificate refused by access control

**Migration.** A ticket is used exactly once. The number of NewSessionTickets on the wire changes from one to four.

Two items on that list are checked by direct wire probes; the bad_certificate alert change is additionally checked through the conformance runner. The ticket count is read straight off the wire and must be four; it is upstream's library default, not something the example server configures. And the example client is pointed at `127.0.0.1` as the *name* of its peer: it must complete that handshake when the server's certificate carries `127.0.0.1` as an `iPAddress` subjectAltName, and must refuse it when the certificate names only DNS hosts. The rest of the list is deliberately out of scope here and `/app/TEST_INTERFACE.md` §6 says which: nothing counts the tickets your *client* keeps or repeats a resumption to see whether one is reused, nothing configures the server's ticket count, nothing drives a QUIC connection, and the shape of your error type is not looked at.

Everything required at the earlier steps must keep working. The cumulative workspace at `/app` already contains your previous step's tree; this milestone is an edit to it, not a fresh start. *Everything* means everything: the behaviour each earlier step established is read again here, one end-to-end path per milestone, and every out-of-band check those steps introduced is run again -- interoperability with an independent implementation, the handling of malformed `server_name` values and the ability to read a handshake message split across records among them, but that is an illustration and not the list. The list is `TEST_INTERFACE.md`: its section 4 names every observation made outside the conformance runner at this step, and section 6 names what was deliberately left out of scope. A regression in anything re-checked means this step is not satisfied, even though the capability was introduced earlier.

## Read these local references

Normative specifications, copied unmodified into the workspace:

- `/app/docs/specs/rfc8446.txt` — RFC 8446 — TLS 1.3
- `/app/docs/specs/rfc5246.txt` — RFC 5246 — TLS 1.2
- `/app/docs/specs/rfc5077.txt` — RFC 5077 — session resumption without server-side state
- `/app/docs/specs/rfc6066.txt` — RFC 6066 — TLS extension definitions (server_name, status_request)
- `/app/docs/specs/rfc5280.txt` — RFC 5280 — X.509 PKI certificate and CRL profile

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
