# Step 03 — deframer hardening

## Implement this milestone

Bring the workspace at `/app` up to the behaviour of release 0.20.7.

**What changes at this step.** 0.20.7 is a patch release on the 0.20 maintenance line, and it is a larger release than a patch number suggests. The record layer stops reading through a **zero-length fragment**: a record whose content type is not `application_data` and whose fragment is empty is refused rather than skipped, so a handshake message split around an empty record is no longer accepted — RFC 8446 §5.1 and RFC 5246 §6.2.1 both forbid sending one. A TLS 1.3 **server can receive 0-RTT early data**, off by default and switched on with `ServerConfig::max_early_data_size`, and can **send data in its first flight** before the client's Finished arrives, under `ServerConfig::send_half_rtt_data`, with the key exporter usable at that point. That setting is a **boolean that defaults to off**. A resumption offer whose obfuscated ticket age is more than **60 seconds** away from the server's own reckoning is refused as stale while one inside that window is accepted, and a client must not use a ticket whose age exceeds the lifetime its issuer advertised on it (RFC 8446 §4.2.11.1; a server may still treat a ticket as valid for a shorter period than it advertised). `ServerName` grows an **IP address** form, so a client pointed at `127.0.0.1` parses the destination and sends a ClientHello where before it could not build the name at all. And there is a year of interoperability corrections behind those

**Migration.** The example programs move out of the `rustls-mio` crate and into a
`rustls-examples` crate, and are built as that crate's **binaries** rather than
as examples. The workspace is built with `-p rustls-examples --bins`, and
`tlsserver-mio` and `tlsclient-mio` are looked for under `target/debug/`;
`/app/TEST_INTERFACE.md` §1 has the exact build line and §2 the shim locations,
and both the new layout and the one you inherited are searched. Make the
move — it is upstream's and the later steps are written against it — but
nothing here is required on it: §6 records that which accepted spec built
the binaries, and where they landed, is out of scope.

Two of the numbers above are BoringSSL's rather than any RFC's, and the conformance runner checks both. The early-data cap your shim configures when the runner asks for early data to be enabled is **14336 bytes** — that is `kMaxEarlyDataAccepted` in BoringSSL, and the cases that overrun the cap by a single byte depend on the value -- do not round it or derive it from the record size. The accepted obfuscated ticket-age skew is **60 seconds** in each direction: a 60-second skew must be accepted and a 61-second one refused.

The zero-length fragment is required as a refusal and nothing more. Whether your server answers such a hello with a fatal alert or simply closes the connection is yours to choose — both are accepted here and at every later step, so do not try to match a particular alert description.

The IP-address change is in scope only as far as it actually goes at this release: a ClientHello is built and sent for an IP destination, where before the name could not be constructed at all. A handshake against one still cannot complete, because the certificate verifier rejects an address-form name as an unsupported name type; verifying a certificate against an IP address arrives at the next step, not this one. No `server_name` extension is sent for an IP peer at this step or any later one, and that disposition is out of scope everywhere.

Two inherited obligations re-checked below are not named in the paragraph that follows. The number of NewSessionTicket messages your server sends on one fresh TLS 1.3 connection is still exactly one here. And a handshake header whose declared length is above the 64 kB bound introduced at the previous step must still be refused rather than buffered — with a fatal alert or by closing the connection, either is accepted. Both are gate checks: a regression in either means this step is not satisfied, regardless of what else is complete.

Everything required at the earlier steps must keep working. The cumulative workspace at `/app` already contains your previous step's tree; this milestone is an edit to it, not a fresh start. *Everything* means everything: the behaviour each earlier step established is read again here, one end-to-end path per milestone, and every out-of-band check those steps introduced is run again -- interoperability with an independent implementation, the handling of malformed `server_name` values and the ability to read a handshake message split across records among them, but that is an illustration and not the list. The list is `TEST_INTERFACE.md`: its section 4 names every observation made outside the conformance runner at this step, and section 6 names what was deliberately left out of scope. A regression in anything re-checked means this step is not satisfied, even though the capability was introduced earlier -- the earlier milestones are re-checked through this step's gate checks, one end-to-end path per milestone, not by re-running the earlier steps' conformance lists.

## Read these local references

Normative specifications, copied unmodified into the workspace:

- `/app/docs/specs/rfc8446.txt` — RFC 8446 — TLS 1.3
- `/app/docs/specs/rfc5246.txt` — RFC 5246 — TLS 1.2
- `/app/docs/specs/rfc6066.txt` — RFC 6066 — TLS extension definitions (server_name, status_request)

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
