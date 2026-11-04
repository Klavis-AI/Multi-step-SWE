# Workspace handoff — step 11

You are continuing the workspace step 10 left behind. Nothing has
been reset, removed or fixed up for you.

## What is already there

From step 10 (quiche 0.20.0): a workspace of `quiche/`, `qlog/`, `octets/` and
`apps/`; one QUIC version (RFC 9000 version 1); Reno, CUBIC, BBR and BBRv2
selectable by name with discriminants 0, 1, 2, 3; multi-path connections with
`probe_path`, `send_on_path`, `migrate` and `path_event_next`; connection-ID
issuance and retirement under the contracted `scid`/`dcid` spellings;
receiving-side key update; transport-level DATAGRAM frames; `Stats` carrying
reset, stopped and PATH_CHALLENGE counters, with the peer's transport parameters
reached through `Connection::peer_transport_params()` rather than through
`Stats`; `Config::set_path_challenge_recv_max_queue_len`; a C API covering
configuration, the handshake, streams, statistics, paths, connection IDs and the
transport-parameter split; qlog 0.10.0 emitting JSON-SEQ with an `MtuUpdated`
event in the schema but nothing in the transport that emits it; a flat
`recovery/` directory and a single-file `crypto.rs`; a packet-number length
chosen from the packet number's magnitude; a hardcoded 3x anti-amplification
bound; no PMTU discovery of any kind.

## What must keep working

This step is scored partly on milestones you already passed. The
inherited groups are:

* `reg-10-bbr2` — step 10's `cap-bbr2`, narrowed to its deterministic half: The four-name accept list with its rejections -- `"BBR2"`, `"bbr2 "`, `"bbr3"` and `""` each `Err(quiche::Error::CongestionControl)` -- the `(0, 1, 2, 3)` discriminant tuple, a positive congestion window on the handshaken connection, and a 256 KiB transfer under each of the two ways of selecting the controller -- `set_cc_algorithm_name("bbr2")` and `set_cc_algorithm(BBR2)` -- completing byte-for-byte. Carried because this step takes every one of those algorithms apart and reassembles it a directory level down behind a new trait. Nothing in the package endangers BBRv2 more than this milestone does, and nothing yet carried step 10 -- this is its first re-run. Step 10's group also required the congestion window to be no smaller after the transfer than before it. That comparison is not carried into the gate. BBRv2 sizes its window from a bandwidth estimate and a minimum RTT taken off the real clock, and an in-memory pair has no propagation delay for either to be about, so both are readings of how promptly this host scheduled the drive loop; under enough load the pair sits long enough between rounds to enter ProbeRTT, whose window is four packets -- below where it started. `cap-bbr2` at step 10 still makes the comparison, where a miss costs that group's weight.
* `reg-01-stream-transfer` — step 01's `cap-stream-transfer`, re-run with the separate stdout-length assertion folded into the byte-for-byte body comparison: The candidate's own `quiche-server` serves a 512 KiB body, the candidate's own `quiche-client` fetches it with `--http-version HTTP/0.9`, exits zero, and writes the body back byte-for-byte. T1, not T3: step 01 scored this over the process boundary and step 08 re-ran it there, and pinning HTTP/0.9 rather than letting the app negotiate keeps the observation the transport's. Carried because it is the simplest thing in the package that runs every byte through the rebuilt recovery path -- loss detection, the round-trip estimator now in its own file, the pacer now a directory down, and the acknowledgement bookkeeping that had to be re-plumbed to produce `acked_bytes`. If the rebuild broke, a half-megabyte transfer is where it shows. Re-seeded from step 08, the most recent step to carry it, and unchanged from it apart from the body seed.

They are re-runs, not new work. They are here because this step's
change plausibly breaks them.

