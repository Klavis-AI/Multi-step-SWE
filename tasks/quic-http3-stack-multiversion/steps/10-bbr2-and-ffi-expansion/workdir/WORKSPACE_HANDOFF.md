# Workspace handoff — step 10

You are continuing the workspace step 09 left behind. Nothing has
been reset, removed or fixed up for you.

## What is already there

From step 09 (quiche 0.18.0): a workspace of `quiche/`, `qlog/`, `octets/` and
`apps/` with the transport library at `quiche/`; one QUIC version (RFC 9000
version 1) and no drafts; receiving-side key update; multi-path connections with
`probe_path`, `send_on_path`, `migrate` and `path_event_next`; connection-ID
issuance and retirement through `new_source_cid`, `active_source_cids`,
`source_cids_left`, `retire_destination_cid`, `source_ids` and `retired_scids`;
Reno, CUBIC and BBR selectable by name, with `"bbr2"` refused; prioritised
stream iteration; a configurable initial congestion window; on-demand
ACK-eliciting packets; transport-level DATAGRAM frames and no HTTP/3 datagram
API; `Stats` carrying thirteen `peer_*` mirrors of the peer's transport
parameters; a C API covering configuration, the handshake, streams and
statistics, but nothing about paths or connection IDs; qlog 0.9.0 emitting
JSON-SEQ with no MTU event.

## What must keep working

This step is scored partly on milestones you already passed. The
inherited groups are:

* `reg-08-probe-and-migrate` — step 08's `cap-probe-and-migrate`, narrowed to its deterministic half: The candidate's own client and server complete a half-megabyte transfer over UDP loopback with `--perform-migration`, the client exits zero and the bytes are identical. Carried because this step renames the connection-ID methods the migration path is built out of, and a rename applied to the library but not to `apps/` breaks exactly this. Step 08's group also required the client to be *observed* holding two UDP sockets at once, which is what distinguishes a migration from a reconnection. That leg is not carried into the gate. It needs a sampler -- the harness polls `/proc/<pid>/fd` against `/proc/net/udp` roughly every 20 ms -- and whether a poll lands inside the overlap is a question about the host's scheduler. Step 08's `cap-probe-and-migrate` still asks it, where a miss costs that group's weight and vetoes nothing.
* `reg-09-key-update` — step 09's `cap-key-update`, narrowed to a single connection pair through one key update: The verifier owns a UDP socket, completes a handshake as the peer, flips the key-phase bit, and requires the candidate to derive the next generation of application keys with the `"quic ku"` label while reusing its header- protection key, to answer under the new generation with its own phase bit flipped, and to reject a second update before the first is acknowledged with `Err(Error::KeyUpdate)`. Carried because it is the step immediately before this one and its machinery sits underneath every short-header packet the new C entry points send.
* `reg-04-datagram-roundtrip` — step 04's `cap-datagram-roundtrip`, re-run unchanged: With `enable_dgram(true, 10, 10)` on both sides, a 256-byte datagram is echoed back byte-for-byte, it never appears as stream data, and both queues drain to `Err(Error::Done)`. Carried because this step adds C predicates over those same two queues and because the transport DATAGRAM API is the one thing step 09 deleted *around* without touching.

They are re-runs, not new work. They are here because this step's
change plausibly breaks them.

