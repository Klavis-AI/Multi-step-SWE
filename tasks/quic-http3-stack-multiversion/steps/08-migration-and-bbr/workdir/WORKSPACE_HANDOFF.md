# Workspace handoff — step 08

You are continuing the workspace step 07 left behind. Nothing has
been reset, removed or fixed up for you.

## What is already there

From step 07 (quiche 0.14.0): the workspace has `quiche/`, `qlog/`, `octets/`
and `apps/` as separate crates, HTTP/3 carries PRIORITY_UPDATE, and the
congestion-control trait is complete with Reno and CUBIC selectable by name.
Connections are single-path: one local address, one peer address, decided when
the connection is created and never revisited. `APPLICATION_PROTOCOL` is still
the packed wire-format constant.

## What must keep working

This step is scored partly on milestones you already passed. The
inherited groups are:

* `reg-01-stream-transfer` — step 01's `cap-stream-transfer`, re-run with the separate stdout-length assertion folded into the byte-for-byte body comparison: Inherited from step 01: the candidate's `client` fetches a 512 KiB pseudorandom file from the candidate's `server` over loopback and the bytes match exactly. The path API is additive, so ordinary single-path transfers must be completely unaffected -- and this is the group that catches the usual casualty of a multi-path refactor, which is that every send now goes through path lookup and the one-path case was the last thing anyone retested. The client is given `--http-version HTTP/0.9` rather than left to negotiate. Step 01 scored this observation over the byte-stream protocol, and an inherited group has to re-run the observation it inherited; routing it through HTTP/3 would quietly make it a different group.
* `reg-04-datagram-roundtrip` — step 04's `cap-datagram-roundtrip`, re-run unchanged: Inherited from step 04: DATAGRAM frames still round-trip, with both peers enabling them via `enable_dgram(true, 10, 10)` and a 256-byte payload echoed back byte-for-byte. A datagram is bound to a path and is not retransmitted, so of everything carried forward this is what the multi-path refactor is most likely to break and least likely to break loudly.
* `reg-07-priority-update` — step 07's `cap-priority-update`, re-run unchanged: Inherited from step 07: the HTTP/3 PRIORITY_UPDATE frame and its event, re-run with the same five stages and the same claims as step 07's own group -- one frame raises one event, a second update for an unread element raises none but replaces the stored value, taking it hands back the latest value once, the event then rearms, and a different prioritised element is stored beside the first rather than over it. This group also covers the ALPN change from a second angle: an HTTP/3 layer whose application protocols no longer negotiate never opens a control stream at all, so a careless edit to `set_application_protos` fails here, long before it reaches priorities, as well as in `cap-alpn-list-shape`.

They are re-runs, not new work. They are here because this step's
change plausibly breaks them.

