# Step 10 — deframer rewrite and early data

## Implement this milestone

Bring the workspace at `/app` up to the behaviour of release 0.23.13.

**What changes at this step.** the message deframer rewritten; early data accepted only up to `max_early_data_size` even when it is read while still arriving; the aws-lc-rs ticketer adopting RFC 5077; a `custom-provider` feature; and the BoGo runner becoming a workspace binary crate

**Migration.** Record buffering and the point at which early data is counted both change wholesale.

The rewritten record buffering is in scope through the interoperability suite, which is where it is observable at all.

The early-data cap is checked twice over, and one of the two is a probe you can run yourself. Your example server is started a second time with `--max-early-data 2048` on its command line — §3 of `TEST_INTERFACE.md` prints the exact line — and three things are then read from an ordinary client. Under the plain command line, every NewSessionTicket your server sends must advertise `max_early_data_size` **0**: early data is off unless it is switched on, and a server that reads it unconditionally accepts replayable application data from anyone holding a ticket. Under the flag, every ticket must advertise **exactly 2048** — the number is read back and compared, so it is the flag's argument and not a constant you may pick — and a resuming client that puts early data in its first flight must have it **accepted**. Both halves are one requirement together, because either on its own is passable without the capability.

Two consequences worth stating plainly. The flag has to exist: a server that does not understand `--max-early-data` does not start, and the check reports that it did not. And what happens when a client sends *more* than the cap is not what that probe asks, and no case in scope at this step overruns a cap either — §6 of `TEST_INTERFACE.md` says what that leaves out of scope.

The certificate-handling, key-update, resumption, robustness, and TLS 1.3 core BoGo suites also gain additional coverage at this release; all five must pass.

Everything required at the earlier steps must keep working. The cumulative workspace at `/app` already contains your previous step's tree; this milestone is an edit to it, not a fresh start. *Everything* means everything: the behaviour each earlier step established is read again here, one end-to-end path per milestone, and every out-of-band check those steps introduced is run again -- interoperability with an independent implementation, the handling of malformed `server_name` values and the ability to read a handshake message split across records among them, but that is an illustration and not the list. The list is `TEST_INTERFACE.md`: its section 4 names every observation made outside the conformance runner at this step, and section 6 names what was deliberately left out of scope. A regression in anything re-checked means this step is not satisfied, even though the capability was introduced earlier.

## Read these local references

Normative specifications, copied unmodified into the workspace:

- `/app/docs/specs/rfc8446.txt` — RFC 8446 — TLS 1.3
- `/app/docs/specs/rfc5077.txt` — RFC 5077 — session resumption without server-side state
- `/app/docs/specs/rfc8449.txt` — RFC 8449 — record size limit

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
