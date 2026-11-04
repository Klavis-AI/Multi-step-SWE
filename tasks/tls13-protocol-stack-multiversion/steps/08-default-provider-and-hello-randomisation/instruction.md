# Step 08 — default provider and hello randomisation

## Implement this milestone

Bring the workspace at `/app` up to the behaviour of release 0.23.0.

**What changes at this step.** aws-lc-rs becomes the default provider; FIPS mode; process-wide provider selection, with the builders refusing to guess when none is installed; the unbuffered API; `no_std` behind a default `std` feature; `Acceptor::accept` returns the alert to send; FFDHE groups for custom providers; `require_ems`; and **randomised ClientHello extension ordering**

**Migration.** The ClientHello stops being byte-stable: extension ordering varies per connection.

Extension ordering is observed over six connections. A fixed order yields exactly one distinct ordering across them; a shuffled one effectively never does. Once randomised the ordering stays randomised, so every later step inherits this.

Two more of the items above are in scope, and both are checked the only way this ladder can check anything -- from outside, over a socket, by driving an example binary. Neither is a test of the shape of your API; each is a test that a program written against it works.

**The unbuffered API ships as a working example server.** An example named `unbuffered-server`, taking a certificate chain and a private key as its two positional arguments, must build and must serve TLS: an independent client connects to it, and the handshake must complete, must come out at **TLS 1.3**, and an HTTP/1 request sent after it must be answered. Upstream ships this example with the release; keeping it building and working is the obligation.

**A refused ClientHello is refused out loud.** `Acceptor::accept` now hands back the alert to send along with the error, so an acceptor that rejects a hello can tell the peer why instead of dropping the connection. The example named `server_acceptor`, started with `--port`, is sent a ClientHello whose `server_name` is not a legal hostname, and the **first record it sends back must be a fatal alert**. Closing the connection without sending a record -- which is all the previous release could do here -- fails. Only the level of the alert is read; which description fits an unparseable name is your choice.

The remaining items above are real parts of the release and are not in scope directly, because nothing they change is visible over a socket: which provider is the default, FIPS mode, what a builder does when no provider is installed, the FFDHE groups a custom provider could add, `require_ems` and the shape of the `std`/`no_std` feature split. §6 of `TEST_INTERFACE.md` says so explicitly, and it says the same about the feature set: §1's build line is written against it, but the previous step's spec is also accepted, so proof of life does not turn on it. Make the change anyway — the next step is written against the result.

Everything required at the earlier steps must keep working. The cumulative workspace at `/app` already contains your previous step's tree; this milestone is an edit to it, not a fresh start. *Everything* means everything: the behaviour each earlier step established is read again here, one end-to-end path per milestone, and every out-of-band check those steps introduced is run again -- interoperability with an independent implementation, the handling of malformed `server_name` values and the ability to read a handshake message split across records among them, but that is an illustration and not the list. The list is `TEST_INTERFACE.md`: its section 4 names every observation made outside the conformance runner at this step, and section 6 names what was deliberately left out of scope. A regression in anything re-checked means this step is not satisfied, even though the capability was introduced earlier.

## Read these local references

Normative specifications, copied unmodified into the workspace:

- `/app/docs/specs/rfc8446.txt` — RFC 8446 — TLS 1.3
- `/app/docs/specs/rfc6066.txt` — RFC 6066 — TLS extension definitions (server_name, status_request)
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
