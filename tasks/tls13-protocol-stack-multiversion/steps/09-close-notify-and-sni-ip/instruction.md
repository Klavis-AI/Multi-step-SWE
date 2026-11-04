# Step 09 — close notify and sni ip

## Implement this milestone

Bring the workspace at `/app` up to the behaviour of release 0.23.5.

**What changes at this step.** a `close_notify` received during the handshake no longer leaves `complete_io()` spinning; `handshake_kind()`; the second phase of `no_std` support; and **a `server_name` carrying an IP literal is parsed and then ignored rather than refused**

**Migration.** An IP literal in server_name is no longer a decoding failure. The extension is accepted and that entry disregarded.

RFC 6066 does not permit an address in `server_name`, but refusing to parse the extension breaks clients that send one anyway. The correct reading is to accept the message and disregard the entry — which is not the same as accepting any name: a syntactically invalid hostname must still be refused, and that rule is in scope at this step alongside the new one.

Both halves of that sentence are in scope, and separately, because they fail separately. **Parsed** is visible in the first record your server sends: an IPv4 literal and an IPv6 literal must each draw a ServerHello where the release you inherited drew a fatal alert. Each address family is its own requirement — they reach the name parser by different routes and an implementation can arrive at one without the other.

**Disregarded** is not visible in the first record at all. A server that accepted the address and then *used* it — dispatching on an address, which is the wrong answer — would send back a ServerHello exactly as a server that threw it away does. RFC 6066 section 3 is what separates them: a server that based its choice on the name has to say so, by returning a `server_name` extension of its own. That acknowledgement is read here. An ordinary hostname must come back acknowledged; an IP literal must come back **unacknowledged**, which is the same answer a hello carrying no `server_name` at all gets. §4 of `TEST_INTERFACE.md` says where it is read from and why that one probe negotiates TLS 1.2 to do it.

Be careful about where the line between refused and accepted actually falls, because it is narrower than "validate the hostname" suggests. The two names required as refusals are `ab@cd.com` and a bare `.`, and both must still draw a fatal alert. That is **not a character whitelist**: measured against this release, `exa_mple.com` and `example.com.` are both accepted, so a validator that rejects every name with a character outside letters, digits and hyphens, or every name ending in a dot, is wrong in the other direction, and nothing here asks for one. The rule that produces all of it is short: take the value as an address if it is one, otherwise as a DNS name, and refuse it only when it is neither.

Three of the four changes named above are **out of scope**, and §6 of `TEST_INTERFACE.md` gives the reasoning for each: the `complete_io()` fix has no signature a peer on a socket can provoke, `handshake_kind()` is an accessor that changes nothing on the wire, and the `no_std` work is a build-time reshape that this step's build line does not newly depend on. Make them — they are the release, and a later step may lean on them — but the requirements here are the `server_name` rule above plus everything required before.

Two of those inherited requirements are easy to lose by accident because they are defaults rather than features, and neither is named in the paragraph below: the **number of NewSessionTicket messages your server sends on a fresh TLS 1.3 connection**, and the fact that the **order of the extensions in your ClientHello varies from connection to connection**. Both came in at earlier steps, both are read again here, and both are properties of the tree you were handed rather than anything this step asks you to change.

Everything required at the earlier steps must keep working. The cumulative workspace at `/app` already contains your previous step's tree; this milestone is an edit to it, not a fresh start. *Everything* means everything: the behaviour each earlier step established is read again here, one end-to-end path per milestone, and every out-of-band check those steps introduced is run again -- interoperability with an independent implementation, the handling of malformed `server_name` values and the ability to read a handshake message split across records among them, but that is an illustration and not the list. The list is `TEST_INTERFACE.md`: its section 4 names every observation made outside the conformance runner at this step, and section 6 names what was deliberately left out of scope. A regression in anything re-checked means this step is not satisfied, even though the capability was introduced earlier.

## Read these local references

Normative specifications, copied unmodified into the workspace:

- `/app/docs/specs/rfc8446.txt` — RFC 8446 — TLS 1.3
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
