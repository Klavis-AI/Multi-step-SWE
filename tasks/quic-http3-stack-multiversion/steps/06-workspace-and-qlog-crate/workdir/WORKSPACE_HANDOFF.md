# Workspace handoff — step 06

You are continuing the workspace step 05 left behind. Nothing has
been reset, removed or fixed up for you.

## What is already there

From step 05 (quiche 0.9.0): QUIC version 1 as `PROTOCOL_VERSION` with drafts
27, 28 and 29 still negotiable and a version-dependent Initial salt; `recv`
taking a `RecvInfo` and `send` returning `(usize, SendInfo)` with a pacing
deadline in `SendInfo.at`; `quiche::connect`/`accept` carrying a `SocketAddr`;
CUBIC with HyStart++; the DATAGRAM extension; `ConnectionId<'a>`; client-side
session export and import behind `is_resumed()`; and HTTP/3 on the plain `h3`
ALPN token.

None of those signatures change here. `Connection::recv`, `Connection::send`,
`quiche::connect` and `quiche::accept` keep the shapes 0.9.0 gave them, and the
`Config` setter era — `set_max_idle_timeout(u64)`,
`set_max_recv_udp_payload_size(usize)`, `set_max_send_udp_payload_size(usize)`
— is unchanged. What changes is where the code lives, what the binaries are
called, what they accept on the command line, and the four behavioural
additions named above.

The example programs of the previous three milestones are gone. Nothing needs
to keep answering on port 4433 with no arguments, and nothing needs to keep
reading `examples/cert.crt` out of its working directory.

## What must keep working

This step is scored partly on milestones you already passed. The
inherited groups are:

* `reg-01-h3-get` — step 01's `cap-h3-get`, re-run against this milestone's binaries: A 64 KiB generated body still crosses the candidate's own HTTP/3 server and client, byte for byte, with the client exiting 0. The programs are different programs now — `quiche-server --listen/--cert/--key/--root` and `quiche-client --no-verify <url>` in place of the no-argument `examples/http3-server` — so this is the group that catches a workspace reshuffle that moved the code but broke the request path. Both are told `--http-version HTTP/3` explicitly, so that a candidate whose default happens to be HTTP/0.9 is still asked the question step 01 asked.
* `reg-01-flow-control` — step 01's `cap-flow-control`, re-run unchanged: A 64 KiB transfer through a 4096-byte stream window still takes many rounds and still arrives intact. The window is pinned with `set_max_stream_window(4096)` so that the claim is the same claim step 01 made — the receiver extends its limit by one window at a time and the sender respects it — rather than a claim about the autotuner this step adds. The group requires the first `stream_send` to accept exactly 4096 bytes and no more, the capacity to read 0 while those bytes are in flight, a further `stream_send` at that point to place nothing and say so with `Err(Error::Done)`, every subsequent round to be offered no more than one window, the transfer to need at least 16 rounds, and all 65536 bytes to arrive in order.
* `reg-02-duplicate-push-removed` — step 02's `cap-duplicate-push-removed`, as an exact-answer negative: The three bytes `0e 01 ff` — the frame type DUPLICATE_PUSH carried at draft-27, a length of 1 and one payload byte — written by hand onto an HTTP/3 request stream ahead of a real HEADERS frame. The expected outcome is a silent discard: the unknown frame is skipped, the HEADERS frame that follows it on the same stream is delivered with its `:path` intact, and neither endpoint closes the connection. The frame type is still undefined at 0.11.0, so the answer is still "ignore it", and the group is here because this step rewrites the crate's directory layout and its HTTP/3 settings table around the frame parser that has to keep doing that.

They are re-runs, not new work. They are here because this step's
change plausibly breaks them.

