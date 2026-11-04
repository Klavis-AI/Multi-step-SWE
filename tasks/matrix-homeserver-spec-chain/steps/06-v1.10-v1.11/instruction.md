# Authenticated media: releases v1.10 and v1.11

## 1. Implement this milestone

This workspace already holds a Matrix homeserver written in C serving the
Client-Server API, federation, the end-to-end-encryption surfaces, threads, spaces,
relations and notifications. Absorb **Matrix specification releases v1.10 and
v1.11**, shipped at `/spec/v1.11/`.

The interface is unchanged: `make` produces `/app/bin/dendrite`, started with
`--config`, `--http-bind-address`, `--https-bind-address`, `--tls-cert` and
`--tls-key` as `/app/DENDRITE/` defines. Everything the workspace already does must
keep working, except where these releases change it.

What these releases add and change:

* **Authenticated media** — the new endpoints
  `GET /_matrix/client/v1/media/config`,
  `/_matrix/client/v1/media/download/{serverName}/{mediaId}`,
  `/_matrix/client/v1/media/download/{serverName}/{mediaId}/{fileName}`,
  `/_matrix/client/v1/media/thumbnail/{serverName}/{mediaId}`, all requiring an
  access token; and the federation endpoints
  `GET /_matrix/federation/v1/media/download/{mediaId}` and
  `/_matrix/federation/v1/media/thumbnail/{mediaId}`, whose responses are
  `multipart/mixed` with a JSON part followed by the content part.
  The release deprecates the unauthenticated `/_matrix/media/v3/download` and
  `/thumbnail` endpoints and says a server should stop serving them by the next
  release; **this server stops now**. They answer `404` with
  `errcode: M_NOT_FOUND` for everything, including content this server holds,
  while the authenticated forms serve it.
* **Asynchronous upload** (release v1.7) is not moved by these releases:
  `POST /_matrix/media/v1/create` still reserves an `mxc://` URI, and
  `PUT /_matrix/media/v3/upload/{serverName}/{mediaId}` fills it once as that
  release defines (a second fill is refused with `409`
  `M_CANNOT_OVERWRITE_MEDIA`); there is no `create` under
  `/_matrix/client/v1/media`. Content uploaded that way is served by the
  authenticated download endpoints like any other.
* **`unsigned.membership`** on events served to a client, as
  [MSC4115] defines it. It is the membership of the user the event is being
  *served to* -- `leave` for a user who has no `m.room.member` event in the
  room -- not the sender's, and this server always includes it on events served
  to a client.
* **Cross-signing** — a user's *first* cross-signing key upload no longer requires
  user-interactive authentication; a later replacement still does.
* **Relations** — recursion on the `/relations` endpoints as release v1.10
  defines it: `recurse` gathers events that relate to the given event
  indirectly, to the depth the release names.
* **Media** — the `animated` parameter on thumbnails. The
  `Cross-Origin-Resource-Policy` header is v1.4 material this chain already
  requires; it must hold on the authenticated media responses this release adds.
* **Account erasure** — `erase` on `POST /account/deactivate`.
* **Server support discovery** — `GET /.well-known/matrix/support`. The
  release lets a deployment publish administrator contacts there and lets one
  that has none answer `404`; **this deployment has none configured**, so the
  endpoint answers `404` with an empty body.
* **Versions** — `GET /_matrix/client/versions` advertises `v1.10` and `v1.11`
  alongside every release this server already listed.

## 2. Read these local references

**Normative semantics:** `/spec/v1.11/` — that Matrix specification release, as
prose (`client-server-api/`, `server-server-api.md`, `appendices.md`) and as
machine-readable API definitions (`api/client-server/*.yaml`,
`api/server-server/*.yaml`), with one page per room version under `rooms/`. The
earlier releases already in the workspace remain the reference for everything
unchanged, and each release's own changelog says what moved (local copies:
`/opt/docs/matrix-spec/changelog/v1.10.md` and `v1.11.md`,
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
