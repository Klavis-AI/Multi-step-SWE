# Step 07 — provider ecosystem settle

## Implement this milestone

Bring the workspace at `/app` up to the behaviour of release 0.22.2.

**What changes at this step.** the provider split settling against its ecosystem -- `rustls-pki-types` 1, `webpki-roots` 0.26, `rustls-webpki` 0.102 (floor moving to 0.102.1), `rustls-pemfile` 2 and `rustls-native-certs` 0.7, versions the tree you inherit already carries -- and, inside that, one change a peer can see. A server that asks for a client certificate may name the issuers whose certificates it would accept. At TLS 1.3 that list is the `certificate_authorities` **extension** of `CertificateRequest` (RFC 8446 4.2.4) and it is optional: the release you inherited pushes the extension unconditionally, carrying an empty list when it has no issuer to name, and this release **leaves the extension out altogether** when the list is empty. A non-empty list is unchanged and still carried. At TLS 1.2 the same information is a mandatory *field* of the same message (RFC 5246 7.4.4), so nothing moves there -- an empty list is a zero-length field before and after. Separately, the interoperability shim gains the runner flag that configures that list, `-use-client-ca-list`, which it has until now declined as unimplemented: it takes `<NULL>` or `<EMPTY>` for an empty list, or a comma-separated list of hex-encoded DER distinguished names, and whatever it is given has to reach the wire

**Migration.** An empty client-certificate issuer hint list is no longer encoded as an empty `certificate_authorities` extension at TLS 1.3 -- the extension is left out of the `CertificateRequest` instead.

Both ends of that are read off a socket, through the interoperability shim, which is the only server in this workspace that can be asked for a client certificate at all. Given one issuer to hint at, the `CertificateRequest` must name exactly that one and no other, at TLS 1.2 and at TLS 1.3. Given an explicitly empty list it must name nobody -- which at TLS 1.3 means the extension is absent and at TLS 1.2 means a zero-length field. And a server asked for a client certificate with **no hints configured at all** is read as well; that one needs no new flag, because it is a configuration every release on this ladder can be driven into, and the answer it gives is precisely what changed.

Nothing here reads your manifests. The dependency versions above are the release's own, and no check inspects `Cargo.toml`, `Cargo.lock` or `cargo metadata` for them. What is measured is that the build line in `TEST_INTERFACE.md` section 1 still succeeds and that the behaviour above reaches the wire.

Three obligations you already hold are re-checked here with the values this release has, and each is read off the wire. Your server sends **four** NewSessionTicket messages on a fresh TLS 1.3 connection -- that is this release's default and nothing here configures it, so the obligation is not to change it. A ClientHello whose `server_name` is not a syntactically legal DNS hostname must be **refused with a fatal alert as the first record back**, rather than answered with a ServerHello or met with silence; `ab@cd.com` and `.` are the two names sent, and only the level of the alert is read, never its description. And a handshake with a peer that offers both TLS 1.2 and TLS 1.3 must come out at **TLS 1.3**.

Everything required at the earlier steps must keep working. The cumulative workspace at `/app` already contains your previous step's tree; this milestone is an edit to it, not a fresh start. *Everything* means everything: the behaviour each earlier step established is read again here, one end-to-end path per milestone, and every out-of-band check those steps introduced is run again -- interoperability with an independent implementation, the handling of malformed `server_name` values and the ability to read a handshake message split across records among them, but that is an illustration and not the list. The list is `TEST_INTERFACE.md`: its section 4 names every observation made outside the conformance runner at this step, and section 6 names what was deliberately left out of scope. A regression in anything re-checked means this step is not satisfied, even though the capability was introduced earlier.

## Read these local references

Normative specifications, copied unmodified into the workspace:

- `/app/docs/specs/rfc8446.txt` — RFC 8446 — TLS 1.3
- `/app/docs/specs/rfc5246.txt` — RFC 5246 — TLS 1.2
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
