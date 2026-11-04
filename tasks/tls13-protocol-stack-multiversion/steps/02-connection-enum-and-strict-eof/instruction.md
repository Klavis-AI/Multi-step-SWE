# Step 02 — connection enum and strict eof

## Implement this milestone

Bring the workspace at `/app` up to the behaviour of release 0.20.0.

**What changes at this step.** `Connection` becomes an enum over the two roles; an unclean TCP closure is reported as `ErrorKind::UnexpectedEof` instead of a clean end of stream; PEM parsing moves out of the crate; certificate verifiers receive the end-entity certificate and the intermediates as separate arguments; `SupportedCipherSuite` splits by protocol version; buffers are bounded at 64 kB; `TLSError` is renamed `Error`

**Migration.** A truncated connection is an error, not a clean EOF. Callers that treated end-of-stream as success must now distinguish the two.

**What is new in scope here.** The 64 kB bound is externally observable, and it is in scope: a handshake message whose header declares a length above the bound must be refused -- with a fatal alert, or by closing the connection -- instead of being buffered while the sender waits for an answer. Either refusal is accepted; leaving the peer hanging is not. Beyond that, this step's new requirements are conformance coverage driven by the interoperability runner: early data, key updates, resumption, and TLS 1.3 core cases. The remaining headlines above are an API and error-reporting reshape with no signature on the wire, and are in scope only through the inherited obligations they must not break.

**What is out of scope here, despite the step's name.** The `ErrorKind::UnexpectedEof` change has no observable signature in this workspace. It is raised inside the `Reader` the connection hands out, and the example clients here return as soon as a TCP read yields zero bytes, so they never reach it. Measured against a server that answers and then closes the socket with no `close_notify`: the example client exits `0` and prints the bytes that had already arrived, both at this release and at the one you inherit. Make the change anyway -- the migration note above is real for library callers -- but no group requires it, and `/app/TEST_INTERFACE.md` §6 says so too.

One inherited obligation re-checked below is not named in the paragraph that follows: the number of NewSessionTicket messages your server sends on one fresh TLS 1.3 connection. At this release it is exactly one, the library default you inherited, and it is required.

Everything required at the earlier steps must keep working. The cumulative workspace at `/app` already contains your previous step's tree; this milestone is an edit to it, not a fresh start. *Everything* means everything: the whole conformance corpus your earlier steps were measured on is run again here, and so is every out-of-band check they introduced -- interoperability with an independent implementation, the handling of malformed `server_name` values and the ability to read a handshake message split across records among them, but that is an illustration and not the list. The list is `TEST_INTERFACE.md`: its section 4 names every observation made outside the conformance runner at this step, and section 6 names what was deliberately left out of scope. A regression in anything re-checked means this step is not satisfied, even though the capability was introduced earlier -- in the runner's corpus that means the case lists `TEST_INTERFACE.md` §2 names for this step; the rest of the corpus is run and its results logged, but it is out of scope.

## Read these local references

Normative specifications, copied unmodified into the workspace:

- `/app/docs/specs/rfc8446.txt` — RFC 8446 — TLS 1.3
- `/app/docs/specs/rfc5246.txt` — RFC 5246 — TLS 1.2

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
