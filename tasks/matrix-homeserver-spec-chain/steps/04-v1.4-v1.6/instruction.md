# Threads, spaces and the refined receipt model: releases v1.4 to v1.6

## 1. Implement this milestone

This workspace already holds a Matrix homeserver written in C that serves the
Client-Server API, federates over the Server-Server API, and carries the
end-to-end-encryption support surfaces. Absorb **Matrix specification releases v1.4,
v1.5 and v1.6**, shipped at `/spec/v1.6/`.

The interface is unchanged: `make` produces `/app/bin/dendrite`, started with
`--config`, `--http-bind-address`, `--https-bind-address`, `--tls-cert` and
`--tls-key` as `/app/DENDRITE/` defines. Everything the workspace already does must
keep working.

What this milestone adds to the workspace:

* **Event relationships and threading** — `m.relates_to` with `rel_type`,
  `GET /_matrix/client/v1/rooms/{roomId}/relations/{eventId}` and its `/{relType}`
  and `/{relType}/{eventType}` forms with `from`, `to`, `dir` and `limit`;
  `GET /_matrix/client/v1/rooms/{roomId}/threads` with its `include` and `limit`;
  the `m.thread` and `m.replace` aggregations in an event's
  `unsigned.m.relations` wherever that event is served. A thread reply is an
  ordinary event in the room: it stays in `/messages` and in `/sync`'s timeline
  alongside its root, and it is a *client* that chooses to render it inside the
  thread instead -- these releases give the server no filter for hiding it.
  Replying to a reply that is already in a thread is refused.
* **Per-thread notifications and receipts** — `thread_id` on
  `POST /rooms/{roomId}/receipt/{receiptType}/{eventId}`,
  `unread_thread_notifications` in `/sync`, the `m.read.private` receipt type, and
  `m.fully_read` settable through `/receipts` as well as `/read_markers`.
  What a count counts is the default push rules' business: an ordinary message
  notifies, a state event and an `m.notice` do not, and an event never notifies
  the user who sent it. The partition files a thread's replies under that
  thread's root id and leaves everything else -- the root itself included -- on
  the main timeline. Note that v1.6's receipts page lists "has child events with
  a `rel_type` of `m.thread`" among its "in a thread" criteria, which reads the
  other way; its own worked example is authoritative here, marking roots `A` and
  `B` read through a `main` receipt, and the line is struck from later releases.
* **Spaces**, which release v1.2 published and this milestone is where the
  chain takes up — the `m.space.child` and `m.space.parent` state events,
  `POST /createRoom` with `creation_content.type: m.space`,
  `GET /_matrix/client/v1/rooms/{roomId}/hierarchy` with `suggested_only`, `limit`,
  `max_depth` and `from`, and its federation counterpart
  `GET /_matrix/federation/v1/hierarchy/{roomId}`.
* **Room directory** — the `room_types` filter on `/publicRooms` and `room_type` in
  its results, and a room's `type` carried across an upgrade. This server
  always has an estimate, so every `/publicRooms` response carries
  `total_room_count_estimate`.
* **Media** — the `Cross-Origin-Resource-Policy` header on media responses.
* **Room version 10** in addition to 1 through 9, with the rules its page under
  `/spec/v1.6/rooms/` defines: it rejects non-integer power levels and adds the
  `knock_restricted` join rule. `/capabilities` reports what `/createRoom`
  accepts.
* **Knocking and restricted joins across federation** at those versions, driven
  through `POST /_matrix/client/v3/knock/{roomIdOrAlias}` and
  `POST /_matrix/client/v3/join/{roomIdOrAlias}`.

## 2. Read these local references

**Normative semantics:** `/spec/v1.6/` — that Matrix specification release, as
prose (`client-server-api/`, `server-server-api.md`, `appendices.md`) and as
machine-readable API definitions (`api/client-server/*.yaml`,
`api/server-server/*.yaml`), with one page per room version under `rooms/`. The
earlier releases already in the workspace remain the reference for everything
unchanged, and each release's own changelog says what moved (local copies:
`/opt/docs/matrix-spec/changelog/v1.4.md`, `v1.5.md` and `v1.6.md`,
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
