# Step 11 — post quantum key share

## Implement this milestone

Bring the workspace at `/app` up to the behaviour of release 0.23.22.

**What changes at this step.** X25519MLKEM768 supported by default **at low negotiation priority**, in both roles; and, as out-of-scope background this release also carries, a `prefer-post-quantum` feature that reorders the default key-exchange groups, `rustls-post-quantum` folded into the core, and the SSL 2 version constant corrected to `0x0002`

**Migration.** The key-share list grows a hybrid group, and both of your roles must be able to perform it and not merely name it.

Five things are read off the wire at this step. The hybrid group is offered but is neither the first preference nor given a key share. Your **server**, offered that group alone with no key share at all, answers with a HelloRetryRequest naming it rather than refusing the hello — a group it lists but cannot perform is caught here. Your **client**, asked by HelloRetryRequest to use the group it offered without a share, sends the 1216 bytes that group's definition fixes. The server's default NewSessionTicket count falls from four to two. And the encrypted handshake flight arrives as a single record rather than one per message.

Only the last is inherited onward: step 12 makes the hybrid group the preference with a share of its own, which contradicts the first rule and makes the third unaskable, and it changes the ticket count again.

Of the four headline items above, **one is in scope**: the hybrid group in the default order, and the two roles being able to perform it. The `prefer-post-quantum` feature is real but is not on this step's build line, so the ordering it produces is never built here; the crate fold is a change to where code lives and nothing reads your module paths; and no case or probe in this package sends an SSL 2 version, so that constant is never read back. Implement all four — the next step is written against the result — but §6 of `TEST_INTERFACE.md` says which of them a requirement can turn on. RFC 8447 is in the reading list for the supported-groups and key-share registries, not for the SSL 2 constant.

Everything required at the earlier steps must keep working. The cumulative workspace at `/app` already contains your previous step's tree; this milestone is an edit to it, not a fresh start. *Everything* means everything: the behaviour each earlier step established is read again here, one end-to-end path per milestone, and every out-of-band check those steps introduced is run again -- interoperability with an independent implementation, the handling of malformed `server_name` values and the ability to read a handshake message split across records among them, but that is an illustration and not the list. The list is `TEST_INTERFACE.md`: its section 4 names every observation made outside the conformance runner at this step, and section 6 names what was deliberately left out of scope. A regression in anything re-checked means this step is not satisfied, even though the capability was introduced earlier.

## Read these local references

Normative specifications, copied unmodified into the workspace:

- `/app/docs/specs/rfc8446.txt` — RFC 8446 — TLS 1.3
- `/app/docs/specs/draft-ietf-tls-ecdhe-mlkem.txt` — draft-ietf-tls-ecdhe-mlkem — X25519MLKEM768 hybrid key exchange
- `/app/docs/specs/rfc8447.txt` — RFC 8447 — IANA registry updates for TLS

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
