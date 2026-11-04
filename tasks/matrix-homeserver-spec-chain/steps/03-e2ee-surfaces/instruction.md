# End-to-end-encryption support surfaces

## 1. Implement this milestone

This workspace already holds a Matrix homeserver written in C that serves the
Client-Server API and federates with other homeservers over the Server-Server
API. Add the **end-to-end-encryption support surfaces of Matrix specification
release v1.3**, shipped at `/spec/v1.3/`.

The server never sees plaintext and never performs any encryption: it stores and
serves the key material clients give it, and routes messages between devices.
That is the whole of this milestone.

The interface is unchanged. `make`, run with no arguments in `/app`, produces the
monolith binary at `/app/bin/dendrite`, started with `--config`,
`--http-bind-address`, `--https-bind-address`, `--tls-cert` and `--tls-key` as
`/app/DENDRITE/` defines. Everything the workspace already does — the
Client-Server API and federation — must keep working exactly as it does now.

New, from the "End-to-End Encryption" and "Send-to-Device messaging" modules:

* **Device keys** — `POST /keys/upload` for a device's identity keys and its
  one-time keys, and `POST /keys/query` for a user's devices, including the
  device display name. A device that uploads different identity keys replaces
  what it had, and `/keys/query` serves the newest upload; re-uploading exactly
  what is already stored changes nothing and must not make the user's device
  list look as though it moved.
* **One-time and fallback keys** — `POST /keys/claim` serving each one-time key
  exactly once and falling back to the uploaded fallback key when they run out;
  `device_one_time_keys_count` and `device_unused_fallback_key_types` in
  `/sync`. The release's own rule for these maps is that "if an algorithm is
  not listed, the count for that algorithm is zero", so naming an algorithm at
  zero and leaving it out are the same answer; what has to be right is the
  count for an algorithm the client has uploaded keys in.
* **Device-list tracking** — `GET /keys/changes`, and `device_lists.changed` and
  `device_lists.left` in `/sync` for the users a client shares a room with.
* **Cross-signing** — `POST /keys/device_signing/upload` behind user-interactive
  authentication for the master, self-signing and user-signing keys, and
  `POST /keys/signatures/upload` with its `failures` object; the keys then
  appear in `/keys/query` as `master_keys`, `self_signing_keys` and
  `user_signing_keys`.
* **Server-side key backups** — the `/room_keys/version` lifecycle and
  `/room_keys/keys` at all three granularities, with the key-replacement rules
  the specification states, the `count` and `etag` behaviour, and
  `M_WRONG_ROOM_KEYS_VERSION`.
* **Send-to-device messaging** — `PUT /sendToDevice/{eventType}/{txnId}` with
  transaction-id replay, delivery to a named device or to `*`, and the
  `to_device` section of `/sync` delivering each message exactly once.
* **Over federation** — `POST /_matrix/federation/v1/user/keys/query` and
  `/user/keys/claim` answering for this server's users, the same two called on
  another server for its users, and the `m.device_list_update` and
  `m.signing_key_update` EDUs in both directions.

## 2. Read these local references

**Normative semantics:** `/spec/v1.3/` — the Matrix specification release
v1.3, as prose and as machine-readable API definitions. The earlier releases
already in the workspace remain the reference for everything unchanged.

**The interface this server exposes:** `/app/DENDRITE/`, unchanged.

**Additional files:** `/app/WORKSPACE_HANDOFF.md` is the running log this
workspace carries between sessions — read it first and keep it accurate.
`/app/selfcheck.sh` and `/app/selfcheck-federation.sh` start the built server the
way it is meant to be started and walk requests through it; neither asserts
anything.

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
