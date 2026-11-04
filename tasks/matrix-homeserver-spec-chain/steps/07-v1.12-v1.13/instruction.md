# Sliding sync, reporting and media dispositions: releases v1.12 and v1.13

## 1. Implement this milestone

This workspace already holds a Matrix homeserver written in C serving the
Client-Server API, federation, the end-to-end-encryption surfaces, threads, spaces,
relations, notifications and authenticated media. Absorb **Matrix specification
releases v1.12 and v1.13**, shipped at `/spec/v1.13/`, and add the **simplified
sliding sync** endpoint.

The interface is unchanged: `make` produces `/app/bin/dendrite`, started with
`--config`, `--http-bind-address`, `--https-bind-address`, `--tls-cert` and
`--tls-key` as `/app/DENDRITE/` defines. Everything the workspace already does must
keep working.

What these releases add and change:

* **Marking rooms unread** — the `m.marked_unread` room account-data type.
  Marking a room unread leaves its fully-read marker alone. `m.fully_read` is
  set through `POST /rooms/{roomId}/read_markers` and never written directly:
  a `PUT` of it through the account-data API is refused.
* **Joining** — the `via` query parameter on `POST /join/{roomIdOrAlias}`, with
  `server_name` deprecated but still accepted.
* **Reporting** — `POST /rooms/{roomId}/report`, and `reason` optional on
  `POST /rooms/{roomId}/report/{eventId}`.
* **Media** — the `Content-Type` and `Content-Disposition` rules on downloads.
  The release gives an allow-list of types a server *may* serve inline and says
  everything else is an `attachment`; **this server serves the allow-listed
  types inline** and everything else as an attachment. The header carries the
  file name the download was asked for: the one in the path when the request
  names one, and otherwise the one the content was uploaded with.
* **One-time keys** — the order in which `POST /keys/claim` issues them.
* **Simplified sliding sync** — `POST /_matrix/client/unstable/org.matrix.simplified_msc3575/sync`
  (a `POST`; apart from the pre-flight `OPTIONS` every endpoint answers, any
  other method on that path is refused the way an unrecognised route is)
  as [MSC4186] defines it: `lists` with `ranges`, `required_state` (including
  the `*` wildcards) and `timeline_limit`, which truncates the timeline and says
  so with `limited`; a list's `filters` — `is_encrypted`, `is_invite`, and
  `room_types` with `not_room_types`, where a `null` entry means a room with no
  type and `not_room_types` takes priority; `room_subscriptions`; `extensions`
  (`to_device`, `e2ee`, `account_data`, `receipts`, `typing`); the opaque `pos`
  continuation token and the `timeout` query parameter that long-polls against
  it, returning at once when there is something to report and when the timeout
  is zero or absent -- "at once" is inside five seconds, and inside three for
  a `timeout` of zero, while a request with nothing to report holds the
  connection and comes back no sooner than 0.4 s after it arrived; and `conn_id`
  separating one client's connections. A room the user is invited to carries its
  stripped state and no timeline, and becomes
  an ordinary joined room once the invitation is accepted. The endpoint is
  advertised in `GET /_matrix/client/versions`. Clients that use `GET /sync` keep
  working unchanged.

## 2. Read these local references

**Normative semantics:** `/spec/v1.13/` — that Matrix specification release, as
prose (`client-server-api/`, `server-server-api.md`, `appendices.md`) and as
machine-readable API definitions (`api/client-server/*.yaml`,
`api/server-server/*.yaml`), with one page per room version under `rooms/`. The
earlier releases already in the workspace remain the reference for everything
unchanged, and each release's own changelog says what moved (local copies:
`/opt/docs/matrix-spec/changelog/v1.12.md` and `v1.13.md`,
and likewise for the earlier releases).

Simplified sliding sync is not part of a numbered release: it is
[MSC4186](https://github.com/matrix-org/matrix-spec-proposals/blob/main/proposals/4186-simplified-sliding-sync.md)
(local copy: `/opt/docs/matrix-spec-proposals/4186-simplified-sliding-sync.md`),
served under the unstable prefix above. The proposal's text has kept changing;
it is the reference for the semantics, and this server speaks this wire shape:

- `pos` and `timeout` are URL query parameters, not body fields. A `pos` the
  server does not recognise is `400` with `errcode` `M_UNKNOWN_POS`.
- In a list, `ranges` is an array of inclusive `[start, end]` pairs and
  `required_state` an array of `[event type, state key]` pairs.
- The response carries `pos`, `lists` (each list with its `count`), `rooms`
  (room id to room) and, when one is enabled, `extensions`.
- A room carries `initial`, `name`, `required_state` and `timeline` (arrays of
  events), and `limited` when its timeline was truncated. An invited room
  carries its stripped state as `stripped_state`, and neither `required_state`
  nor a timeline.
- An extension is requested as `"extensions": {"<name>": {"enabled": true}}` and
  is absent from the response unless enabled. In the response, `to_device` has
  `next_batch` and `events`; `e2ee` has `device_one_time_keys_count`,
  `device_unused_fallback_key_types`, and `device_lists` when devices changed;
  `account_data` has `global` (an array of events) and `rooms` (room id to an
  array of events); `receipts` and `typing` each have `rooms` (room id to one
  `m.receipt` or `m.typing` event).

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
