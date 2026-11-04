# Workspace handoff — step 04

You are continuing the workspace step 03 left behind. Nothing has
been reset, removed or fixed up for you.

## What is already there

From step 03 (quiche 0.5.0): draft-29 transport with CUBIC congestion control, the
h3-29 ALPN, and a stable stream API. The packet header still uses the draft-29 long
header format, and the Error enum still has C-style discriminants at this milestone.

## What must keep working

This step is scored partly on milestones you already passed. The
inherited groups are:

* `reg-01-h3-get` — step 01's `cap-h3-get`, re-run unchanged: Inherited from step 01: the candidate's `http3-client` performs a GET against the candidate's `http3-server` and retrieves the exact body. At step 04 the ALPN is still `h3-29` from step 03, and the HTTP/3 layer is unchanged; this group confirms no regression from adding DATAGRAM support to the transport. HTTP/3 SETTINGS-frame hardening (rejecting a missing or malformed SETTINGS frame at the start of the control stream) is not independently scored at this step. A working SETTINGS exchange is already this group's own precondition — any regression there fails this gate outright — so no separate group is added for it, and this step's theme is the transport layer (DATAGRAM, the Error enum, `ConnectionId`), not HTTP/3-layer robustness.
* `reg-01-flow-control` — step 01's `cap-flow-control`, re-run unchanged: Inherited from step 01: with `initial_max_stream_data_bidi_remote = 4096`, the sender's `stream_capacity()` limits the transfer correctly and reopens after the receiver drains. The Error enum change must not break the `FlowControl` variant, and the new DATAGRAM path must not interfere with stream flow control.

They are re-runs, not new work. They are here because this step's
change plausibly breaks them.

