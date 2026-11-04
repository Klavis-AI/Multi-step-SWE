# Next-generation authentication: releases v1.14 and v1.15

## 1. Implement this milestone

This workspace already holds a Matrix homeserver written in C serving the
Client-Server API, federation, the end-to-end-encryption surfaces, threads, spaces,
relations, notifications, authenticated media and sliding sync. Absorb **Matrix
specification releases v1.14 and v1.15**, shipped at `/spec/v1.15/`.

The interface is unchanged: `make` produces `/app/bin/dendrite`, started with
`--config`, `--http-bind-address`, `--https-bind-address`, `--tls-cert` and
`--tls-key` as `/app/DENDRITE/` defines. Everything the workspace already does must
keep working, except where these releases change it.

What these releases add and change:

* **Next-generation authentication** — the OAuth 2.0 based API release v1.15
  adds. `GET /_matrix/client/v1/auth_metadata` is the endpoint a client asks for
  the authorization server metadata (RFC 8414) of the issuer a deployment
  delegates authentication to. **This deployment delegates to nobody**: it is
  started with no authorization server configured, so the API is not supported
  here and the endpoint answers `404` with `errcode: M_UNRECOGNIZED` -- which is
  what the release tells a server without it to do, and is the only position
  this ticket asks you to implement. Everything else authenticates exactly as it
  does today, from this server's own access tokens: an unknown token is still
  `M_UNKNOWN_TOKEN` with `soft_logout: false`, and `POST /login` and
  `POST /register` still work.
* **`POST /users/{userId}/report`** — reporting a user. The release lets a server
  conceal whether the reported user exists, and this one does: a report naming a
  user who does not exist, or one on another server, is answered exactly as a
  report of a local user is.
* **`server_name` removed** from `POST /join/{roomIdOrAlias}` and
  `POST /knock/{roomIdOrAlias}`: `via` is now the only way to name a resident
  server. Carrying `server_name` as well must not break a join or a knock this
  server can serve by itself.
* **`GET /_matrix/client/v1/room_summary/{roomIdOrAlias}`** — the summary of a
  room the caller may or may not be in, by room id or by alias, with `via`
  naming servers to ask about a room this one does not hold. It carries at
  least `room_id`, `num_joined_members`, `world_readable` and `guest_can_join`,
  and reports the caller's own `membership` -- `leave` when they are not in the
  room. This server also includes every optional summary field that applies to
  the room: `name` when it has one, `join_rule`, `room_version`, `room_type`
  when the room is typed, `encryption` when it is encrypted, and
  `allowed_room_ids` when its join rule names rooms. The release lets a server
  answer a summary without an access token and
  **this one does**, for a room whose join rule or history visibility already
  makes it public; a room that is neither is `404` for a caller who is not in
  it.
* **`m.topic` content block** on `m.room.topic`, carrying `m.text`
  representations alongside the plain `topic`.
* **Room hierarchy** — `allowed_room_ids`, `encryption` and `room_version` in the
  entries of `GET /_matrix/client/v1/rooms/{roomId}/hierarchy`.
* **`GET /rooms/{roomId}/initialSync`** is no longer deprecated: it is a
  supported endpoint again, and it is a `GET` -- apart from the pre-flight
  `OPTIONS` every endpoint answers, any other method on that path is refused
  the way an unknown route is.
* The newest room version this release publishes, alongside every earlier one, with
  `/capabilities` reporting what `/createRoom` accepts. `GET /_matrix/client/versions`
  keeps advertising every release this server has absorbed: from `v1.1` through
  `v1.12` at least, contiguously, with nothing named after `v1.15`.

## 2. Read these local references

**Normative semantics:** `/spec/v1.15/` — that Matrix specification release, as
prose (`client-server-api/`, `server-server-api.md`, `appendices.md`) and as
machine-readable API definitions (`api/client-server/*.yaml`,
`api/server-server/*.yaml`), with one page per room version under `rooms/`. The
earlier releases already in the workspace remain the reference for everything
unchanged, and each release's own changelog says what moved (local copies:
`/opt/docs/matrix-spec/changelog/v1.14.md` and `v1.15.md`,
and likewise for the earlier releases).

**The interface this server exposes:** `/app/DENDRITE/`, unchanged.

**Additional files:** `/app/WORKSPACE_HANDOFF.md` is the running log this workspace
carries between sessions — read it first and keep it accurate. `/app/selfcheck.sh`
and `/app/selfcheck-federation.sh` start the built server the way it is meant to be
started and walk requests through it; neither asserts anything, so read their output
against the specification.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`; the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the supplied specification,
  documents and handoff files. `make` must keep working from a clean checkout of
  what you leave behind, and the server must still be your own program built from
  source here, a native executable with no interpreter or foreign-language runtime
  behind it. That build must work with every compiled file in the workspace
  discarded first, and a tree carrying fewer than 1,500 lines of C, or a `bin/dendrite`
  that is not a native executable or that links or embeds a foreign-language runtime, is
  not a homeserver this ticket asked for.
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
