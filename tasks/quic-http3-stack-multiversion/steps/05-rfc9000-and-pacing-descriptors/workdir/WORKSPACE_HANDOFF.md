# Workspace handoff — step 05

You are continuing the workspace step 04 left behind. Nothing has
been reset, removed or fixed up for you.

## What is already there

From step 04 (quiche 0.7.0): drafts 27, 28 and 29 on the wire with a
version-dependent Initial salt, CUBIC, the DATAGRAM extension with both frame
forms and bounded queues, `ConnectionId<'a>` as a public type, and an `Error`
enum whose `StreamStopped` variant already carries a `u64`. HTTP/3 already has
`Event::Datagram` and `Event::GoAway`, `send_dgram`/`recv_dgram` and
`send_goaway`; those arrived with the DATAGRAM work and are not this step's
business.

The four example binaries are the positional-argument programs the last three
milestones have shipped: `server` and `http3-server` take no arguments at all
and `client` and `http3-client` take exactly one URL. That contract does not
change here.

## What must keep working

This step is scored partly on milestones you already passed. The
inherited groups are:

* `reg-01-h3-get` — step 01's `cap-h3-get`, re-run unchanged: Inherited from step 01: the candidate's `http3-client` performs a GET against the candidate's `http3-server` over UDP loopback and receives a 64 KiB body byte for byte. The invocation is this milestone's — the server takes no arguments and the body is staged into `examples/root/` under the crate root — but the observation is step 01's. Both hot-path signatures changed underneath HTTP/3 at this step, and the ALPN list grew a token; a stack that got either wrong serves nothing.
* `reg-01-flow-control` — step 01's `cap-flow-control`, re-run unchanged: Inherited from step 01: with `initial_max_stream_data_bidi_remote` set to 4096, `stream_capacity` falls to zero after 4096 bytes are in flight and `stream_send` accepts no more; once the receiver has drained them and the resulting MAX_STREAM_DATA has crossed, capacity reopens and the remainder of a 64 KiB transfer arrives intact and in order. The pacer now sits between `stream_send` and the wire, so a send path that honours `SendInfo.at` by withholding packets it should have written breaks this.
* `reg-04-error-enum` — step 04's `cap-error-enum`, re-run unchanged: Inherited from step 04: a stream that the peer has stopped with a STOP_SENDING frame carrying error code `0x123` makes the next `stream_send` on it return `Err(Error::StreamStopped(0x123))`, and the code is read out of the variant by pattern match rather than through an accessor. `Error` grows a second payload-carrying variant at this step; the one step 04 added must still carry its own payload and must still be the variant that this condition produces.

They are re-runs, not new work. They are here because this step's
change plausibly breaks them.

