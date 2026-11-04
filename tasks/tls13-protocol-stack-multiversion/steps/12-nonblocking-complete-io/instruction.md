# Step 12 — nonblocking complete io

## Implement this milestone

Bring the workspace at `/app` up to the behaviour of release 0.23.31.

**What changes at this step.** the 0.23 hardening line — the hybrid key-exchange group becoming the default preference; client certificate selection driven by the server's hints; a client-side callback for the server's OCSP response; ClientHello extension permutation carried through a HelloRetryRequest; exporter traffic secrets; and several fixes to session-ticket and session-id handling — ending in the fix for the `complete_io()` failure to make progress under non-blocking I/O that was introduced by the yanked 0.23.30

**Migration.** X25519MLKEM768 becomes the client's first preference and carries a key share. The previous step's rule — offered but not preferred — no longer holds.

`complete_io()` must return as soon as it can make no further progress rather than spinning, including when the underlying transport returns `WouldBlock` with nothing buffered. Make it — it is a real bug for library callers and the tree you hand on carries it — but **it is out of scope here**, and §6 of `TEST_INTERFACE.md` says why in detail: of the examples in the tree you inherit, the ones that call `complete_io()` drive it over a blocking socket, the mio-based ones drive the connection themselves and never enter the loop, and a peer at the far end of a socket cannot make the near end's transport non-blocking. There is no probe that separates the fixed loop from the one you inherited. Do not go looking for requirements there; they are in the two paragraphs below.

**The key exchange is where the new requirements are, and it is two obligations, not one.** The first is the preference order, which the migration note above states: X25519MLKEM768 first in `supported_groups`, and a `key_share` entry for it. The second is that the share is the object `docs/specs/draft-ietf-tls-ecdhe-mlkem.txt` §4.1 defines rather than a slot carrying the right code point. That section fixes its size at **1216 bytes** and its layout as the **1184-byte ML-KEM-768 encapsulation key first, the 32-byte X25519 public value second**, and the bytes are read: the length, and whether the ML-KEM half decodes as an encapsulation key, with the coefficients of its twelve-bit packed encoding all reduced. The halves in the other order are the same 1216 bytes and are wrong; §4.2 of the same document requires a server to run that check on what it receives and abort with `illegal_parameter`. The two obligations fail separately — a name in a list costs nothing to emit, and the share is what a peer has to do arithmetic on — so they are in scope separately. Both are read from the ClientHello, and §4 of `TEST_INTERFACE.md` says so. A third obligation, inherited from the previous step rather than new here, is on the other side of the same group: your **server** must be able to perform X25519MLKEM768, not merely list it, and §4 says how that is read — a hello offering that group alone with an empty `client_shares` must draw a HelloRetryRequest naming it.

**The conformance corpus grows, and this is what the new cases ask for.** They are the other half of this release and the largest single block of new requirements at this step:

- Client certificate selection, driven by what the server asked for rather than by what you happen to hold: the `certificate_authorities` hint and the issuers it names, the signature algorithms the server offered, and for ECDSA the curve and the key's own preferences — including the case where none of the hints match and a default must be chosen anyway, and including the asynchronous form, where the certificate is not available at the moment it is first asked for. Both TLS 1.2 and TLS 1.3.

- A client-side callback for the server's stapled OCSP response, including its verdict: a callback that rejects the response must fail the handshake, and **the case where the server staples nothing at all still reaches the callback** — a client that only invokes it when a response arrived cannot reject an absent staple. Sync and async forms, under TLS 1.2 and TLS 1.3, and under handshakes packed into one record and split across records.

- ClientHello extension permutation carried through a **HelloRetryRequest**: the case sends a permuted hello and then, when the retry comes back, a second permuted hello, and what it reads is that the handshake still completes with both hellos permuted -- the permutation order itself is free.

- Exporter traffic secrets, for both AES-128-GCM-SHA256 and AES-256-GCM-SHA384.

- Session-id and ticket handling, three client-side rules: a server that echoes the TLS 1.3 compatibility session id back must be rejected with `illegal_parameter` — the client generated that id at random, so a server, including one that then negotiates TLS 1.2, is echoing a session it could not have known; a TLS 1.3 `NewSessionTicket` whose ticket field is empty must be rejected too (the field is `opaque ticket<1..2^16-1>`, so zero bytes is a decode error — it is the TLS 1.2 sibling of that case which tolerates an empty ticket, and it is out of scope here); and a TLS 1.2 ticket must not be offered when none was issued.

**Four things required before are defaults rather than features, and none of them is named above.** They are read again here and a regression in any of them means this step is not satisfied: the **number of NewSessionTicket messages your server sends** on a fresh TLS 1.3 connection, the fact that your server's encrypted handshake flight arrives **coalesced into a single record**, the fact that the **order of your ClientHello's extensions varies** from connection to connection, and that a `server_name` carrying an **IP literal is accepted** rather than refused. All four are properties of the tree you were handed, not things this step asks you to change — which is exactly why they are easy to break while changing something else.

Everything required at the earlier steps must keep working. The cumulative workspace at `/app` already contains your previous step's tree; this milestone is an edit to it, not a fresh start. *Everything* means everything: the behaviour each earlier step established is read again here, one end-to-end path per milestone, and every out-of-band check those steps introduced is run again -- interoperability with an independent implementation, the handling of malformed `server_name` values and the ability to read a handshake message split across records among them, but that is an illustration and not the list. The list is `TEST_INTERFACE.md`: its section 4 names every observation made outside the conformance runner at this step, and section 6 names what was deliberately left out of scope. A regression in anything re-checked means this step is not satisfied, even though the capability was introduced earlier.

## Read these local references

Normative specifications, copied unmodified into the workspace:

- `/app/docs/specs/rfc8446.txt` — RFC 8446 — TLS 1.3
- `/app/docs/specs/draft-ietf-tls-ecdhe-mlkem.txt` — draft-ietf-tls-ecdhe-mlkem — X25519MLKEM768 hybrid key exchange

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
