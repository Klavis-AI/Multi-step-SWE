# Workspace handoff — step 09

You are continuing the workspace step 08 left behind. Nothing has
been reset, removed or fixed up for you.

## What is already there

From step 08 (quiche 0.16.0): a workspace of `quiche/`, `qlog/`, `octets/` and
`apps/`; multi-path connections with `probe_path`, `send_on_path`, `migrate`,
`path_event_next` and connection-ID rotation; Reno, CUBIC and BBR selectable by
name; `h3::APPLICATION_PROTOCOL` in its list form. The stack speaks RFC 9000
version 1 *and* drafts 27, 28 and 29, with the Initial salt chosen per version.
Application keys are derived once during the handshake and never rotated.
Readable and writable streams are reported as unordered sets. HTTP/3 carries
datagrams through its own `send_dgram`/`recv_dgram` pair.

## What must keep working

This step is scored partly on milestones you already passed. The
inherited groups are:

* `reg-01-flow-control` — step 01's `cap-flow-control`, re-run unchanged: Inherited from step 01, with the same claims: a 64 KiB transfer through a 4096-byte stream window completes byte-identically, and because a window that small has to be reopened before the next chunk can be written, it takes at least sixteen rounds of read-and-reopen to get there -- which the group counts. This is the inherited group drawn from a step whose surface this step rewrites. Step 09 replaces how readable and writable streams are reported: the sets `readable()` and `writable()` return are now views over a priority-ordered structure that `stream_readable_next` and `stream_writable_next` mutate. A stream's readiness and its flow-control state are now bookkept together, so this is exactly the inherited observation most likely to break, and to break quietly -- a transfer that stalls looks like a slow transfer until the round count is checked.
* `reg-04-datagram-roundtrip` — step 04's `cap-datagram-roundtrip`, re-run unchanged: Inherited from step 04: both peers enable datagrams with `enable_dgram(true, 10, 10)`, a 256-byte payload is sent with `dgram_send`, received with `dgram_recv` and echoed back byte-for-byte. It is here for a reason specific to this step rather than as routine carry: the HTTP/3 datagram API is deleted at this milestone and this is the transport API underneath it, which is not. A candidate that reads "datagrams are removed" and deletes both layers passes `cap-h3-datagram-removed`'s subject fixture and fails here and on that group's control fixture. The two groups are drawn around the same boundary from opposite sides on purpose.
* `reg-08-probe-and-migrate` — step 08's `cap-probe-and-migrate`, narrowed to its deterministic half: Inherited from step 08: the candidate's own `client`, run with `--perform-migration` against the candidate's `server`, retrieves a 512 KiB pseudorandom file byte-identically and exits zero. `--source-port` is left at its default so the second bind takes a different ephemeral port. This is the required end-to-end inherited group. It is drawn from step 08 in particular because a key update is a change to packet protection and migration is the one inherited capability that moves packets between protection contexts mid-connection while the connection stays up. If the responder's key handling has an ordering bug involving paths -- keeping the previous opening key on the wrong path, say -- a single-path transfer will not find it and this will. No socket observation, and that is a deliberate narrowing from step 08's own group. Counting the client's bound UDP ports needs a sampler -- the harness polls `/proc/<pid>/fd` against `/proc/net/udp` roughly every 20 ms -- and whether any poll lands while both sockets are open is a question about the host's scheduler rather than about the candidate. `cap-probe-and-migrate` at step 08 still asks it, where a miss costs that group's weight; a gate case that can miss vetoes the whole step for every candidate, so the gate asks only what is deterministic: the flag is accepted, the connection survives it, the bytes arrive.

They are re-runs, not new work. They are here because this step's
change plausibly breaks them.

