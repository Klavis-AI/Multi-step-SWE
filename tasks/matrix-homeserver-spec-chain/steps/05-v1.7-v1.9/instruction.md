# Relations, notifications and reports: releases v1.7 to v1.9

## 1. Implement this milestone

This workspace already holds a Matrix homeserver written in C serving the
Client-Server API, federation, the end-to-end-encryption surfaces, threads and
spaces. Absorb **Matrix specification releases v1.7, v1.8 and v1.9**, shipped at
`/spec/v1.9/`.

The interface is unchanged: `make` produces `/app/bin/dendrite`, started with
`--config`, `--http-bind-address`, `--https-bind-address`, `--tls-cert` and
`--tls-key` as `/app/DENDRITE/` defines. Everything the workspace already does must
keep working.

What this milestone adds to the workspace:

* **Notifications** — `GET /notifications` and the `/pushrules/` tree were
  published in release v1.1 and this milestone is where the chain takes them
  up; the mention rules named below are what v1.7 adds to them.
  `GET /notifications` with its `only`, `from` and `limit`
  parameters, returning each notification with its room, event, actions, read state
  and timestamp; the default push rules the specification lists, including
  `.m.rule.is_user_mention` and `.m.rule.is_room_mention` driven by an event's
  `m.mentions`, which take the place of matching the sender's display name in
  `content.body`; and the `/pushrules/` tree -- the whole rule set, and each
  rule under its scope and kind with `GET`, `PUT` and `DELETE` plus the
  `enabled` and `actions` sub-resources. A rule identifier that would make the
  path ambiguous -- one containing a `/`, or an empty one -- is refused, and a
  refused rule is not created.
* **Event reports** — `POST /rooms/{roomId}/report/{eventId}` with its `score`
  and `reason`, refused for a room the reporter is not in, for an event that
  does not exist, and for a `score` or `reason` of the wrong type.
* **Presence** — `currently_active` and `last_active_ago` in
  `GET /presence/{userId}/status` and in `/sync`'s presence section. Release
  v1.1 published both; this milestone is where the chain takes them up.
* **Room version 11** in addition to 1 through 10, with the rules its page under
  `/spec/v1.9/rooms/` defines: a redaction's target moves into `content`,
  `m.room.create` no longer carries `creator` (the create event's `sender` is
  the creator), and the redaction algorithm's content allow-list changes --
  `m.room.member` additionally keeps `third_party_invite.signed`,
  `m.room.power_levels` additionally keeps `invite`, and `m.room.redaction`
  becomes a type of its own keeping `redacts`. It inherits version 10's
  integer-only power levels. `/capabilities` reports what `/createRoom` accepts.
* **Login** — `POST /login/get_token` and the `m.login.token` type it feeds and
  the `m.get_login_token` capability, which release v1.7 adds; and
  `POST /refresh` rotating a refresh token with `expires_in_ms` on the login
  response, which release v1.3 published and this milestone takes up. The releases call
  `POST /login/get_token` optional; this server offers it, behind
  user-interactive authentication, and the three places a client can look --
  `GET /login`'s flows, `GET /capabilities` and the endpoint itself -- all have
  to say so. A minted login token works once.

## 2. Read these local references

**Normative semantics:** `/spec/v1.9/` — that Matrix specification release, as
prose (`client-server-api/`, `server-server-api.md`, `appendices.md`) and as
machine-readable API definitions (`api/client-server/*.yaml`,
`api/server-server/*.yaml`), with one page per room version under `rooms/`. The
earlier releases already in the workspace remain the reference for everything
unchanged, and each release's own changelog says what moved (local copies:
`/opt/docs/matrix-spec/changelog/v1.7.md`, `v1.8.md` and `v1.9.md`,
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
