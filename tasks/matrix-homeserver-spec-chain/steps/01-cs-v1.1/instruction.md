# A Matrix homeserver in C: Client-Server API v1.1

## 1. Implement this milestone

Build a Matrix homeserver in this workspace that serves the **Client-Server API of
Matrix specification release v1.1**, shipped at `/spec/v1.1/`, over HTTP.

The homeserver is written in C and exposes **Dendrite's monolith interface**
(matrix-org/dendrite v0.13.8), whose definition is shipped at `/app/DENDRITE/`:

* `make`, run with no arguments in `/app`, produces the monolith binary at
  `/app/bin/dendrite` — the path `go build -o bin/ ./cmd/...` produces in
  `DENDRITE/docs-manual/1_build.md`.
* The binary is started the way `DENDRITE/docs-manual/5_starting_dendrite.md` and
  `DENDRITE/cmd-dendrite-main.go` / `DENDRITE/setup-flags.go` define:

  ```
  bin/dendrite --config <path> --http-bind-address <host:port> \
               --https-bind-address <host:port> \
               --tls-cert <path> --tls-key <path>
  ```

  It must bind the client-server listener on `--http-bind-address` and stay in the
  foreground until `SIGTERM`.
* The configuration file is a Dendrite `version: 2` YAML document
  (`DENDRITE/dendrite-sample.yaml`). Unknown keys are ignored. These keys are
  honoured: `global.server_name`, `global.private_key`,
  `global.key_validity_period`, `global.database.connection_string` (a `file:` URI
  naming the SQLite database; the same URI is repeated in each component's own
  `database.connection_string`, which is how `dendrite-sample.yaml` configures
  SQLite), `global.disable_federation`,
  `global.presence.enable_inbound` / `enable_outbound`,
  `client_api.registration_disabled`, `client_api.guests_disabled`,
  `client_api.registration_shared_secret`, `client_api.rate_limiting.enabled`,
  `media_api.base_path`, `media_api.max_file_size_bytes`,
  `federation_api.disable_tls_validation`, `logging`.
* Registration is open exactly when `client_api.registration_disabled` is
  `false`: this server has no separate confirmation flag in front of it.
* `global.private_key` names a PEM file of type `MATRIX PRIVATE KEY` carrying a
  `Key-ID: ed25519:<id>` header and the base64 of the 32-byte Ed25519 seed, as
  `DENDRITE/docs-manual/3_signingkey.md` specifies. The server has to load the
  key file it is pointed at — a `MATRIX PRIVATE KEY` document it cannot read is
  not a configuration this ticket asked for — and keeps that key in memory for
  the rest of its life. Event signatures are not a requirement at this step.

Everything a client can observe is defined by the specification, not by this
ticket. Serve the `v3` paths the v1.1 release documents (the `r0` aliases are
optional), and implement the modules it defines:

* **Registration and login** — user-interactive authentication with the
  `m.login.dummy` and `m.login.password` types, `/register`, `/register/available`,
  `/login`, `/logout`, `/logout/all`, `/account/password`, `/account/deactivate`,
  `/account/whoami`, bearer access tokens, and devices (`/devices`).
* **Profiles** — `GET`/`PUT /profile/{userId}/displayname` and
  `/profile/{userId}/avatar_url`, the whole-profile `GET /profile/{userId}`,
  and their propagation into the `m.room.member` events of the rooms the user
  has joined.
* **Rooms** — `/createRoom` with `room_version`, `preset`, `visibility`, `name`,
  `topic`, `room_alias_name`, `initial_state`, `invite`, `is_direct`,
  `creation_content` and `power_level_content_override`; room state
  (`/rooms/{roomId}/state`, `/state/{eventType}/{stateKey}`); membership
  (`/invite`, `/join`, `/leave`, `/forget`, `/kick`, `/ban`, `/unban`,
  `/join/{roomIdOrAlias}`); `/members`, `/joined_members`, `/joined_rooms`;
  event authorisation and power levels; room aliases and the room directory
  (`/directory/room/{roomAlias}`, `/rooms/{roomId}/aliases`,
  `/directory/list/room/{roomId}`, `/publicRooms`); `/rooms/{roomId}/upgrade`.
  An invitation that names a third-party identifier it cannot back — an
  `invite_3pid` entry missing a required field, or an `m.room.member` whose
  content carries a `third_party_invite` with no matching
  `m.room.third_party_invite` state event — is refused; this server has no
  identity service.
* **Room membership rules** — `m.room.join_rules` with the `public` and
  `invite` rules these room versions define, set by `preset` at creation and
  readable and writable as room state like any other state event; and
  `m.room.topic`, likewise, carrying the `topic` `/createRoom` was given.
* **Room history visibility** — `m.room.history_visibility` with its four
  settings (`world_readable`, `shared`, `invited`, `joined`), deciding which of
  a room's events each endpoint that serves history will show to which user.
  The setting in force for an event is the one that was in the room's state *at
  that event*, so changing it is never retroactive; a user who has left keeps
  what they could see up to their departure.
* **Guest access** — `m.room.guest_access` as room state, settable and
  readable through the standard state-event endpoints: `can_join` decides
  whether a guest account may be in the room at all and, like every other state
  event, it does not relax `m.room.join_rules` for anyone else. What is required
  here is the state mechanics: setting `forbidden` and `can_join`, reading both
  back, refusing the change from a member without the state-default power, and
  `can_join` not relaxing `m.room.join_rules` for non-guest users. Guest
  accounts are the module the release marks optional, and the release lets a
  homeserver refuse them for its own users; this deployment does, through
  `client_api.guests_disabled`, and a `POST /register?kind=guest` here is
  refused. A guest registering successfully or a guest user joining a room is
  never required.
* **Events** — `/rooms/{roomId}/send/{eventType}/{txnId}` with transaction
  identifiers, `/rooms/{roomId}/event/{eventId}`, `/rooms/{roomId}/messages` and
  `/rooms/{roomId}/context/{eventId}` with their pagination tokens, and
  `/rooms/{roomId}/redact/{eventId}/{txnId}` with the redaction algorithm.
* **Syncing** — `/sync`, initial and incremental, the older
  `/rooms/{roomId}/initialSync` and `/initialSync`, with `since`, `full_state`,
  `set_presence` and `timeout` (a request with a timeout must block until something
  arrives or the timeout expires, while other requests continue to be served), and
  the `rooms.join` / `rooms.invite` / `rooms.leave` sections the specification
  defines.
* **Filtering** — `/user/{userId}/filter`, filters given inline in the `filter`
  query parameter, and their effect on `/sync` and `/rooms/{roomId}/messages`.
* **Instant messaging support modules** — client config
  (`/user/{userId}/account_data/{type}` and its per-room form), room tagging
  (`/user/{userId}/rooms/{roomId}/tags`), typing notifications, receipts
  (`/rooms/{roomId}/receipt/{receiptType}/{eventId}`), fully-read markers
  (`/rooms/{roomId}/read_markers`), and presence
  (`/presence/{userId}/status`, and `set_presence` on `/sync`).
* **Content repository** — `/_matrix/media/v3/upload`, `/download/…`,
  `/thumbnail/…` with the `crop` and `scale` methods, and `/config`.
* **Capabilities and versions** — `/_matrix/client/versions` advertising `v1.1`, and
  `/capabilities` reporting `m.room_versions` consistently with what `/createRoom`
  accepts.

**Room versions 1, 2, 3, 4, 5 and 6** are all supported, each with the event
format, event-identifier format, redaction algorithm and authorisation rules its
page under `/spec/v1.1/rooms/` defines.

A request for a path this server does not serve, or for a version of the API it
does not publish, is answered the way the release says an unknown endpoint is:
`404` with `M_UNRECOGNIZED`.

Responses use the specification's standard error object; use the error codes its
endpoint definitions name and the common codes it defines (`M_FORBIDDEN`,
`M_UNKNOWN_TOKEN`, `M_MISSING_TOKEN`, `M_NOT_JSON`, `M_NOT_FOUND`,
`M_UNRECOGNIZED`, `M_USER_IN_USE`, `M_INVALID_USERNAME`, `M_USER_DEACTIVATED`,
`M_ROOM_IN_USE`, `M_UNSUPPORTED_ROOM_VERSION`, `M_TOO_LARGE`,
`M_GUEST_ACCESS_FORBIDDEN`). Every response carries
`Access-Control-Allow-Origin: *`, every endpoint answers an `OPTIONS` request,
and -- as that section requires -- an `OPTIONS` request performs none of the
endpoint's own logic. JSON responses carry `Content-Type: application/json`.

The container is Debian with an ordinary C toolchain: `gcc`, `g++`, `make`,
`pkg-config`, `gdb` and `valgrind`, the development headers for OpenSSL
(`libssl-dev`), SQLite (`libsqlite3-dev`), libyaml, libcurl, zlib and libmagic,
and `curl`, `jq`, `sqlite3`, `openssl`, `xxd`, `git` and `python3` beside them.
None of it is Matrix-specific -- there is no Matrix library for C in any of
these packages.

`make` builds the server from source in this workspace, and the resulting
`bin/dendrite` is a native executable with no interpreter or foreign-language
runtime behind it. The server must build from source with every compiled file in
the workspace discarded first, and a tree carrying fewer than 1,500 lines of C,
or a `bin/dendrite` that is not a native executable or that links or embeds a
foreign-language runtime, is not a homeserver this ticket asked for.

All of it survives a restart: stop the process with `SIGTERM`, start it again with
the same configuration, and accounts, access tokens, devices, rooms, room state,
message history, account data and uploaded media are all still there.

## 2. Read these local references

**Normative semantics:** `/spec/v1.1/` — the Matrix specification release v1.1,
as prose (`client-server-api/`, `appendices.md`) and as machine-readable API
definitions (`api/client-server/*.yaml`), with one page per room version under
`rooms/`.

**The interface this server exposes:** `/app/DENDRITE/` — Dendrite v0.13.8's sample
configuration (`dendrite-sample.yaml`), its manual installation documentation
(`docs-manual/`), and the source files that define its command-line flags
(`cmd-dendrite-main.go`, `setup-flags.go`).

**Additional files:** `/app/WORKSPACE_HANDOFF.md` is the running log this workspace
carries between sessions — keep it accurate. `/app/selfcheck.sh` starts the built
server the way it is meant to be started and walks a few requests through it; it
asserts nothing, so read its output against the specification.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`; the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the documents and handoff
  files in it. The specification releases are read-only reference material under
  `/spec/`, outside the workspace: they are not yours to change and are not part
  of what you hand on. `make` must keep working from a clean checkout of what you
  leave behind.
- Do not search for, download, copy, wrap, or invoke an online
  implementation or task-specific answer; work only from this workspace, its
  documentation and the local references above, and do not search the system
  for other implementations or answers. The homeserver must be your own program
  built from source in this workspace: do not install, vendor, wrap or execute
  an existing homeserver.
- The server runs under a ceiling: each `bin/dendrite` process is capped at
  **4 GiB of address space** and **900 seconds of CPU time**, and is killed
  together with its whole process group when its run ends. Exceeding either
  limit is a failure of the server, not of the machine.
- A fresh build starts from clean; yours does not. This workspace is
  cumulative and long-lived, and the headers in it keep changing as the server
  grows, so a build that does not record header dependencies will, sooner or
  later, link translation units that disagree about a struct's layout -- a
  binary that misbehaves rather than one that fails to build. Make the build
  you write track them, and say so in the handoff.
- Leave the workspace as sources. Before you finish, run `make clean` (or
  otherwise delete `bin/` and every object file) and remove the databases, media
  stores, logs and other run-time files your own testing produced. What you
  leave behind must rebuild from source with a plain `make`, reusing nothing you
  compiled -- and a workspace that still holds build output or a
  running server's state is not a reproducible handoff.
- Run the local checks named above and leave a reproducible handoff for the next
  fresh session.
