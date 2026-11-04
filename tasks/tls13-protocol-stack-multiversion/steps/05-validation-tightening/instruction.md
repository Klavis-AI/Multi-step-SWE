# Step 05 — validation tightening

## Implement this milestone

Bring the workspace at `/app` up to the behaviour of release 0.21.6.

**What changes at this step.** the 0.21 maintenance line: validation tightening and interoperability corrections made before the crypto provider split. Three of those corrections are in scope, and all three are visible to a peer. First: a server must **abort** the handshake when a ClientHello offers a `pre_shared_key` extension without a `psk_key_exchange_modes` extension. RFC 8446 4.2.9 requires the client to send the second whenever it sends the first and says servers "MUST abort the handshake if [it] is not present"; the release you inherited answers such a hello with a ServerHello instead. The first record back must be a fatal alert. Second: a client configured to speak **TLS 1.2 only** must send an empty `legacy_session_id`. The 32 random bytes normally in that field are TLS 1.3's middlebox-compatibility mode (RFC 8446 4.1.2 and D.4), and a client that has been configured out of TLS 1.3 has nothing to be compatible with; a client that still offers TLS 1.3 keeps sending them and is unaffected. Third: the interoperability shim gains two runner flags. `-reject-alpn` configures a server that refuses every protocol the client offers, and the refusal must reach the peer as the `no_application_protocol` alert of RFC 7301 3.2 -- the library has sent that alert since the previous step, but the shim had no way to ask for it. `-on-resume-early-write-after-message` tells the shim to hold the early data it is to write until a handshake message of a given type has been received -- the runner passes that type as a **decimal handshake message type** in a separate argument (`2` for ServerHello, `8` for EncryptedExtensions), so the flag takes an argument and does not parse a name -- which means reading whole messages off the runner's pipe rather than whatever one read returns, and writing the queued data from inside the I/O loop rather than before it; the conformance groups that verify this whole-message pipe reading are `bogo-shim-message-reads-async-new` (four asynchronous I/O variants) and `bogo-shim-message-reads-sync-new` (four synchronous variants)

Three obligations you already hold are re-checked here with the values this release has, and each is read off the wire: the server sends **four** NewSessionTicket messages by default on a fresh TLS 1.3 connection; a `server_name` whose HostName is not a syntactically valid DNS hostname -- an IP literal, an address with an `@` in it, a bare dot -- must be **refused with a fatal alert** rather than parsed and honoured or treated as a decode failure that is silently tolerated; and a handshake with a peer that offers both TLS 1.2 and TLS 1.3 must come out at **TLS 1.3**. Separately: the conformance runner advances to a newer BoringSSL pin at this step and its corpus moved with it, so cases that passed under the previous step's runner can fail under this one and "it worked last step" is not evidence. A conformance case this step requires has to pass; declining it, by having the shim exit with the runner's unimplemented status, does not satisfy the step, exactly as failing it would not.

Everything required at the earlier steps must keep working. The cumulative workspace at `/app` already contains your previous step's tree; this milestone is an edit to it, not a fresh start. *Everything* means everything: the behaviour each earlier step established is read again here, one end-to-end path per milestone, and every out-of-band check those steps introduced is run again -- interoperability with an independent implementation, the handling of malformed `server_name` values and the ability to read a handshake message split across records among them, but that is an illustration and not the list. The list is `TEST_INTERFACE.md`: its section 4 names every observation made outside the conformance runner at this step, and section 6 names what was deliberately left out of scope. A regression in anything re-checked means this step is not satisfied, even though the capability was introduced earlier.

## Read these local references

Normative specifications, copied unmodified into the workspace:

- `/app/docs/specs/rfc8446.txt` — RFC 8446 — TLS 1.3
- `/app/docs/specs/rfc7301.txt` — RFC 7301 — Application-Layer Protocol Negotiation (ALPN)
- `/app/docs/specs/rfc5246.txt` — RFC 5246 — TLS 1.2
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
