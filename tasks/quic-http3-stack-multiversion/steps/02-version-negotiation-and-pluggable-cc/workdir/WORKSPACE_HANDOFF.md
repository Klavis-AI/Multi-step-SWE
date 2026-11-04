# Workspace handoff — step 02

You are continuing the workspace step 01 left behind. Nothing has
been reset, removed or fixed up for you.

## What is already there

Step 01 left a working single-version draft-23 stack: transport with stream
transfer and flow control, an HTTP/3 layer over the `h3-23` ALPN, a C API
exported from a cdylib, and a Retry path that echoed the original destination
connection ID inline. Congestion control was Reno, wired in directly with no
way to select it. `Stats` reported recv, sent, lost, rtt and cwnd.

Two things changed in the shared `verifier` support crate that every scored
group's test file imports, and neither is optional plumbing: `quiche::connect`
and `quiche::accept` now return `Result<Pin<Box<Connection>>>` rather than step
01's `Result<Box<Connection>>`, and `Config::set_idle_timeout` was renamed
`Config::set_max_idle_timeout` (same `u64` milliseconds argument, same
behaviour). Code written against step 01's names does not compile against this
step's harness or any later one.

## What must keep working

This step is scored partly on milestones you already passed. The
inherited groups are:

* `reg-01-initial-keys` — step 01's `cap-initial-keys`, re-checked in both directions and carried through to a completed handshake: The grader opens the candidate's client Initial with its own draft-23 keys -- salt `c3eef712…` over the client's destination connection ID -- and finds a TLS ClientHello at the head of the CRYPTO stream; it then opens the server's reply, keyed from that same original destination connection ID with the server labels, and finds a ServerHello. The draft-23 handshake then completes. Draft-23 is still a supported version at this milestone, so its key schedule must still be right.
* `reg-01-h3-get` — step 01's `cap-h3-get`, re-run unchanged: The candidate's `http3-client` performs a GET against the candidate's `http3-server` and receives the exact body. The ALPN list grew here, which is exactly what plausibly breaks it.
* `reg-01-stream-transfer` — step 01's `cap-stream-transfer`, re-run unchanged: The candidate's `client` fetches a 512 KiB pseudorandom file from the candidate's `server` over UDP loopback byte for byte. Congestion control became pluggable here, which is exactly what plausibly breaks it.

They are re-runs, not new work. They are here because this step's
change plausibly breaks them.

