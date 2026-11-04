# Step 06 — crypto provider split

## Implement this milestone

Bring the workspace at `/app` up to the behaviour of release 0.22.0.

**What changes at this step.** a `CryptoProvider` trait with *ring* as the default; certificate revocation lists in the default verifier; **removal of SCT delivery by TLS extension**; `WebPkiVerifier` splits into `WebPkiServerVerifier` and `WebPkiClientVerifier`; verifier traits lose their default implementations; client certificate root hints; the move to pki-types

**Migration.** A capability required at step 01 is withdrawn: SCT delivery by TLS extension is removed, and the implementation must now decline those cases rather than serve them.

The withdrawal is itself in scope: the BoGo shim must *decline* the SCT-by-extension cases, not merely fail to pass them. An implementation that still answers them has not performed the withdrawal.

The boundary of the withdrawal is a runner flag, not a case name. The cases to decline are the ones where the conformance runner passes `-signed-cert-timestamps`, which asks your shim to *attach* an SCT list to a certificate it sends. `-enable-signed-cert-timestamps` and `-expect-signed-cert-timestamps` are the other direction -- they ask your shim to *receive* one -- and they are not withdrawn: your shim must still accept both on its command line and must not decline the cases that carry them. Only two of those cases have `SignedCertificateTimestamp` in the name; the others are named for what else they carry, so the flag is the boundary and the name is not. What the groups at this step check is the withdrawal itself -- the cases the runner drives with `-signed-cert-timestamps` must be reported SKIP -- and the out-of-band observation that your example client no longer offers the extension; no group at this step requires a BoGo case to pass, so declining a case that should run makes no difference here either way.

The withdrawal is visible from the other end of the connection too, and it is in scope there as well. An implementation that no longer delivers SCTs by extension also stops asking for them, so the `signed_certificate_timestamp` extension -- number 18, RFC 6962 3.3.1 -- must be gone from the ClientHello your example client sends. Six connections are made and the extension numbers are read off the wire; 18 must appear in none of them. Nothing else about the hello is read: not the order of the extensions, not the bytes inside any of them.

Two inherited numbers are worth stating, because they are read exactly and neither is written down anywhere else. Your server must send **four** NewSessionTicket messages on a fresh TLS 1.3 connection -- that is what this release does by default and nothing here configures it, so the obligation is not to change it. And it must answer a ClientHello whose `server_name` is not a legal hostname with a **fatal alert** as its first record, rather than with a handshake record; `ab@cd.com` and `.` are the two names used.

Two inherited *behaviours* are re-checked here as well, and both are exact. A ClientHello that offers `pre_shared_key` without `psk_key_exchange_modes` must be refused with a **fatal alert** as the first record -- RFC 8446 4.2.9 makes the second extension mandatory whenever the first is offered and tells a server to abort without it; only the level of the alert is read, never its description. And the `legacy_session_id` your client sends when it is configured for TLS 1.2 only -- the `--protover 1.2` invocation in `TEST_INTERFACE.md` §3 -- must be **empty**: middlebox-compatibility mode (RFC 8446 4.1.2, D.4) is a TLS 1.3 behaviour, and a client configured out of TLS 1.3 has nothing to be compatible with. Neither changes at this release. They are stated because this step rebuilds every verifier and moves every certificate call site, and a handshake-time check is an easy thing to drop on the way.

Everything required at the earlier steps must keep working. The cumulative workspace at `/app` already contains your previous step's tree; this milestone is an edit to it, not a fresh start. *Everything* means everything: the behaviour each earlier step established is read again here, one end-to-end path per milestone, and every out-of-band check those steps introduced is run again -- interoperability with an independent implementation, the handling of malformed `server_name` values and the ability to read a handshake message split across records among them, but that is an illustration and not the list. The list is `TEST_INTERFACE.md`: its section 4 names every observation made outside the conformance runner at this step, and section 6 names what was deliberately left out of scope. A regression in anything re-checked means this step is not satisfied, even though the capability was introduced earlier.

## Read these local references

Normative specifications, copied unmodified into the workspace:

- `/app/docs/specs/rfc8446.txt` — RFC 8446 — TLS 1.3
- `/app/docs/specs/rfc6962.txt` — RFC 6962 — Certificate Transparency (SCT delivery)
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
