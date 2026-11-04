# Workspace handoff — step 07

You are continuing the workspace step 06 left behind. Nothing has
been reset, removed or fixed up for you.

## What is already there

From step 06 (quiche 0.11.0): the tree is already a cargo workspace —
`quiche/` holds the transport and HTTP/3, `qlog/` is a top-level crate emitting
JSON-SEQ, `apps/` builds the flag-taking binaries. That layout does not change
here; `octets/` is added to it.

The binaries are the ones step 06 shipped and the grader drives them exactly as
step 06 did: `quiche-server --listen <addr> --cert <pem> --key <pem> --root
<dir> --no-retry` and `quiche-client --no-verify [--http-version HTTP/3] <url>`,
both under `target/debug/`. `quiche-server` speaks HTTP/3 and HTTP/0.9 on the
same socket and picks by ALPN.

Also inherited and still scored: RFC 9000 version 1 beside drafts 27/28/29 with
a per-version Initial salt and Retry key; `recv(RecvInfo)` /
`send() -> (usize, SendInfo)` with a real pacing `at`; CUBIC with HyStart++;
the DATAGRAM extension; `ConnectionId<'a>`; window-based flow control; and the
qlog crate's transport events. `Header::from_slice` parsing, in particular, is
re-checked here: the fields it exposes are `ty`, `version`, `dcid`, `scid`,
`pkt_num`, `pkt_num_len`, `token`, `versions` and `key_phase`, with `dcid` and
`scid` being `ConnectionId<'a>` rather than `Vec<u8>` and `versions` being
`Option<Vec<u32>>` — the Version Negotiation packet's version list, in wire
order, `Some(...)` there and `None` on every other packet type. The check
compares `versions` against a `Some(vec![...])` literal of the parsed
versions, so the field has to be an `Option` of a vector of integers carrying
the list in wire order -- `u32` is what a QUIC version field is. That field has
been part of `Header` since step 01 and this milestone is where it is first
read back.

## What must keep working

This step is scored partly on milestones you already passed. The
inherited groups are:

* `reg-01-header-parse` — step 01's `cap-header-parse`, re-run unchanged: Inherited from step 01: `Header::from_slice` reads a long header out of a fixed byte corpus the grader carries. An Initial, a Retry and a Version Negotiation packet, all at QUIC version 1, are parsed with the connection-id length the server would use; the type, version, connection ids, token and version list come back exactly as the corpus encodes them, the Retry's token is the bytes before its 16-byte integrity tag, and two truncations of the same packet return `Err(Error::BufferTooShort)`. The codec being parsed with now lives in another crate; the bytes it reads must not have moved with it.
* `reg-01-h3-get` — step 01's `cap-h3-get`, re-run unchanged: Inherited from step 01: a 64 KiB body travels from the candidate's HTTP/3 server to the candidate's HTTP/3 client over UDP loopback, byte for byte, as two real processes. The invocation is this milestone's — `quiche-server --listen/--cert/--key/--root/--no-retry` and `quiche-client --no-verify --http-version HTTP/3` — but the observation is step 01's. HTTP/3 grew a frame type, an event variant and a whole qlog surface at this step; none of that may cost it the ability to serve a file.
* `reg-06-qlog-jsonseq` — step 06's `cap-qlog-jsonseq`, narrowed to the trace envelope -- the file, its header record, and an unchanged qlog_version and qlog_format: Inherited from step 06: the qlog crate still writes a JSON-SEQ trace whose header record names `qlog_version` `"0.3"` and `qlog_format` `"JSON-SEQ"`, carries no `traces` array, and is followed by one record per transport event, including all five of `recovery:metrics_updated`, `transport:data_moved`, `transport:packet_received`, `transport:packet_sent` and `transport:parameters_set` for a completed transfer. The invocation is step 06's own -- no `--http-version` flag, so ALPN never selects HTTP/3 and the new `http:*` events this milestone adds never enter the trace -- because the point of this group is not the new HTTP/3 qlog surface `cap-h3-qlog` already covers, it is that the shared JSON-SEQ writer those `http:*` events now also run through has not changed shape or dropped an event underneath them. Step 06's own group asserted this *exact* set of five names; that assertion cannot be carried forward as written, because a step-07 stack correctly adding `http:*` events to the same file would then have a strictly larger name set and fail it for doing its job. This group checks presence instead: each of the five names must still appear somewhere in the trace, which step 06's exactness already implied and step 07 must not have broken.

They are re-runs, not new work. They are here because this step's
change plausibly breaks them.

