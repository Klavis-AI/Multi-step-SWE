# Step 01 — tls12 and tls13 stack

## Implement this milestone

Bring the workspace at `/app` up to the behaviour of release 0.19.0.

**What changes at this step.** a complete TLS 1.2 and TLS 1.3 implementation for both roles: the record layer and deframer, both handshake state machines, the key schedule including binders and exporters, resumption by session id and by ticket, early data, the extension set including SCT delivery by TLS extension, the BoGo shim, and the mio example client and server

This is the base of the ladder, so every obligation here is new. Both roles must interoperate with a peer that shares no code with them, a syntactically invalid hostname in `server_name` must be refused, a handshake message split across several records must still be read, and the server sends one NewSessionTicket by default.

Where that refusal line falls is part of the obligation, and it is **not a character whitelist**. The two values required as refusals here are `ab@cd.com` and a bare `.`, and both must draw a fatal alert. The same rule has a second side: `exa_mple.com` and `example.com.` are legal names at this release, so an implementation that refuses every name holding a character outside letters, digits and hyphens, or every name ending in a dot, refuses names this release accepts and is wrong in the other direction. Get the `server_name` parse boundary right here, on both sides: the workspace carries forward, and the refusal half of this rule is re-read at every later step.

## Read these local references

Normative specifications, copied unmodified into the workspace. These are the ones this step's milestone is checked against; the corpus holds more than this list, and `docs/specs/README.md` tabulates all of it:

- `/app/docs/specs/rfc8446.txt` — RFC 8446 — TLS 1.3
- `/app/docs/specs/rfc5246.txt` — RFC 5246 — TLS 1.2
- `/app/docs/specs/rfc6066.txt` — RFC 6066 — TLS extension definitions (server_name, status_request)
- `/app/docs/specs/rfc7627.txt` — RFC 7627 — extended master secret / session hash
- `/app/docs/specs/rfc7301.txt` — RFC 7301 — Application-Layer Protocol Negotiation (ALPN)
- `/app/docs/specs/rfc5077.txt` — RFC 5077 — session resumption without server-side state
- `/app/docs/specs/rfc6962.txt` — RFC 6962 — Certificate Transparency (SCT delivery)
- `/app/docs/specs/rfc5280.txt` — RFC 5280 — X.509 PKI certificate and CRL profile

Additional files in the workspace:

- `/app/WORKSPACE_HANDOFF.md` — what is in the workspace you have been given, and what is expected of what you leave behind.
- `/app/TEST_INTERFACE.md` — the binaries, arguments and behaviour your tree must provide. Read this before you rename anything. Its §6 lists what is out of scope for this step.
- `/app/docs/specs/README.md` — what is in the specification corpus and which document answers which kind of question.

## Meta requirements

- The budget for this step is shown by `bash /app/timer.sh`. The configured
  timeout is authoritative if the two ever disagree.
- `/app` is cumulative and is carried into the next step, where this step's
  behaviour is re-checked. Leave it building.
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
