# Federation: the Server-Server API

## 1. Implement this milestone

This workspace already holds a Matrix homeserver written in C that serves the
Client-Server API over HTTP. Add the **Server-Server API of Matrix specification
release v1.2**, shipped at `/spec/v1.2/server-server-api.md` with its
machine-readable definitions under `/spec/v1.2/api/server-server/`, so that
this homeserver and another, independent homeserver share rooms.

The interface is unchanged. `make`, run with no arguments in `/app`, produces the
monolith binary at `/app/bin/dendrite`, and it is started the way
`/app/DENDRITE/docs-manual/5_starting_dendrite.md` and
`/app/DENDRITE/setup-flags.go` define:

```
bin/dendrite --config <path> --http-bind-address <host:port> \
             --https-bind-address <host:port> \
             --tls-cert <path> --tls-key <path>
```

The federation API is served on the **HTTPS** listener, at the port the server's
own `global.server_name` names. The configuration keys honoured are the ones the
workspace already honours, plus `federation_api.disable_tls_validation`, which
makes the federation client accept the peer's certificate.

Everything the Client-Server API already does must keep working exactly as it
does now: the repository's own behaviour is the specification for that part, and
nothing in it may regress.

New, from the Server-Server API:

* **Server keys** — `/_matrix/key/v2/server` publishing this server's
  `verify_keys` and a `valid_until_ts` in the future, signed by itself; the
  `/_matrix/key/v2/query` notary forms, in both their `GET` and `POST` shapes;
  and fetching another server's keys before its signatures are checked.
* **Request authentication** — the `X-Matrix` `Authorization` scheme in both
  directions, over the signed JSON object the specification defines, with an
  unsigned or badly signed request refused.
* **Transactions** — `PUT /_matrix/federation/v1/send/{txnId}` carrying PDUs and
  EDUs, with per-event results, content-hash and signature checking, the
  authorisation rules applied against room state, and transaction replay
  returning the same result. The EDUs `m.typing`, `m.receipt` and `m.presence`
  cross in both directions and reach clients through `/sync`.
* **Room membership over federation** — `make_join`/`send_join` (both the v1
  and the v2 endpoint), `make_leave`/`send_leave`, `invite`, and
  `make_knock`/`send_knock`, so that a user on either server can join, be
  invited to, knock on, leave, be kicked from and be banned from a room that
  lives on the other, driven through the ordinary Client-Server endpoints
  (`/join/{roomIdOrAlias}`, `/knock/{roomIdOrAlias}`,
  `/rooms/{roomId}/{invite,leave,kick,ban,unban}`).
* **Room history and state over federation** — `/event/{eventId}`,
  `/state/{roomId}`, `/state_ids/{roomId}`, `/event_auth/{roomId}/{eventId}`,
  `/backfill/{roomId}` and `/get_missing_events/{roomId}`, so a server that
  joins a room late can page back through history it never received live.
* **State resolution** — two servers that have been talking to a room
  independently produce a fork, and the state at any event is what the release's
  state-resolution algorithm for that room version says it is: version 1's
  algorithm for room version 1, and the second version of the algorithm -- the
  full auth-difference, reverse topological power ordering and mainline ordering
  it specifies -- for every room version from 2 on. An event whose signature or
  authorisation fails is rejected outright: it never becomes part of the room's
  state and is never served to clients. A **content-hash** mismatch is not a
  rejection -- the release keeps the event and serves the redacted copy the
  receiver computed for itself, so it survives with the unverifiable parts of
  its content gone.
* **Queries** — `/query/profile`, `/query/directory` and `/user/devices/{userId}`,
  so `GET /_matrix/client/v3/profile/{userId}` answers for a user on another
  server and `GET /_matrix/client/v3/directory/room/{roomAlias}` resolves an
  alias on another server.
* **Event signing and identifiers** — events this server creates carry the
  content hash and the signature over the redacted event that the specification
  defines, and the event identifier each room version specifies, so another
  server can verify them.
* **Room versions 7, 8 and 9** in addition to 1 through 6, each with the
  authorisation rules and join rules its page under `/spec/v1.2/rooms/`
  defines: knocking at version 7 and restricted join rules at version 8.
  `/capabilities` reports what
  `/createRoom` accepts.

## 2. Read these local references

**Normative semantics:** `/spec/v1.2/` — the Matrix specification release
v1.2, as prose (`server-server-api.md`, `client-server-api/`, `appendices.md`)
and as machine-readable API definitions (`api/server-server/*.yaml`,
`api/client-server/*.yaml`), with one page per room version under `rooms/`.
The earlier release already in the workspace under `/spec/v1.1/` remains the
reference for everything that is unchanged.

**The interface this server exposes:** `/app/DENDRITE/`, unchanged.

**Additional files:** `/app/WORKSPACE_HANDOFF.md` is the running log this
workspace carries between sessions — read it first and keep it accurate.
`/app/selfcheck.sh` starts the built server the way it is meant to be started and
walks a few requests through it; `/app/selfcheck-federation.sh` starts **two**
copies of it with different server names and walks a federation handshake
between them. Neither asserts anything: read the output against the
specification.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the documents and handoff
  files in it. The specification releases are read-only reference material under
  `/spec/`, outside the workspace. `make` must keep working from a clean checkout
  of what you leave behind, and the server must still be your own program built
  from source here, a native executable with no interpreter or foreign-language
  runtime behind it. That build must work with every compiled file in the
  workspace discarded first, and a tree carrying fewer than 1,500 lines of C, or a
  `bin/dendrite` that is not a native executable or that links or embeds a
  foreign-language runtime, is not a homeserver this ticket asked for.
- Every response carries `Access-Control-Allow-Origin: *`, every endpoint
  answers an `OPTIONS` request without performing the endpoint's own logic,
  and JSON responses carry `Content-Type: application/json` -- as they have
  since the first release this server spoke.
- Do not search for, download, copy, wrap, or invoke an online
  implementation or task-specific answer; work only from this workspace, its
  documentation and the local references above, and do not search the system
  for other implementations or answers. Do not install, vendor, wrap or execute
  an existing homeserver.
- The server runs under a ceiling: each `bin/dendrite` process is capped at
  **4 GiB of address space** and **900 seconds of CPU time**, and is killed
  together with its whole process group when its run ends. Exceeding either
  limit is a failure of the server, not of the machine.
- A fresh build starts from clean; yours does not. This milestone changes
  shared headers, so a `make` that only recompiles the files you edited can link
  translation units that disagree about a struct's layout -- a binary that
  misbehaves rather than one that fails to build. Check that the build records
  header dependencies before you trust an incremental result, or `make clean`
  first.
- Leave the workspace as sources. Before you finish, run `make clean` (or
  otherwise delete `bin/` and every object file) and remove the databases, media
  stores, logs and other run-time files your own testing produced. What you
  leave behind must rebuild from source with a plain `make`, reusing nothing you
  compiled -- and a workspace that still holds build output or a
  running server's state is not a reproducible handoff.
- Run the local checks named above and leave a reproducible handoff for the next
  fresh session.
