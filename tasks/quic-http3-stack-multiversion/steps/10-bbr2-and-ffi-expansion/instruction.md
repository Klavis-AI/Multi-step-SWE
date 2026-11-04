# Step 10 — BBRv2, an expanded C surface, and peer transport parameters

## 1. Implement this milestone

Three capabilities arrive at this milestone and one piece of bookkeeping
moves house.

**BBRv2 becomes the fourth congestion-control algorithm.**
`CongestionControlAlgorithm` grows a `BBR2` variant appended after `BBR`,
the name `"bbr2"` becomes selectable through
`Config::set_cc_algorithm_name`, and there is a real algorithm behind it: a
connection configured for BBRv2 carries data to completion, does not close its
congestion window under load it is not losing anything to, and reports sane
path statistics. Step 08 added BBR
and its group asserted that `"bbr2"` was *refused*; that assertion is now
false and this step's group is its inverse.

**The C API grows a path and connection-ID surface.** Everything the Rust
API learned at step 08 for multi-path connections, and everything it learned
at step 09 for issuing and retiring connection IDs, becomes reachable from
C: `quiche_conn_probe_path`, `quiche_conn_migrate`,
`quiche_conn_send_on_path`, `quiche_conn_path_event_next` with an opaque
`quiche_path_event` and its six accessors, `quiche_conn_new_scid`,
`quiche_conn_retire_dcid`, `quiche_conn_active_scids`,
`quiche_conn_scids_left`, `quiche_conn_available_dcids`,
`quiche_conn_source_ids` and `quiche_conn_retired_scids` with their two
iterator types, and `quiche_conn_peer_transport_params`. The header and the
Rust `ffi` module must agree with each other, which is a stricter
requirement than it sounds: at the previous milestone they did not.

**The connection reports reset and stopped stream counts, and how many
PATH_CHALLENGE frames it has received.** `Stats` gains five counters, and
`Config` gains a bound on how many received PATH_CHALLENGE frames a path
will queue for response.

The bookkeeping that moves: the thirteen `peer_*` fields that `Stats` has
carried since step 06 are no longer part of `Stats`. They live in a
`TransportParams` struct reached through a new
`Connection::peer_transport_params()`, which returns `None` until the peer's
parameters have actually been parsed. The C API mirrors the split exactly.

A `MtuUpdated` connectivity event joins the qlog schema, which steps to
0.10.0.

Two hard breaks, no deprecated aliases for either.

**Four connection-ID methods are renamed.** The words "source" and
"destination" contract to `scid` and `dcid`:

| step 09 | step 10 | | --- | --- | | `Connection::new_source_cid` |
`Connection::new_scid` | | `Connection::active_source_cids` |
`Connection::active_scids` | | `Connection::source_cids_left` |
`Connection::scids_left` | | `Connection::retire_destination_cid` |
`Connection::retire_dcid` |

Signatures and behaviour are unchanged; only the names move. The rename
reaches the shipped binaries too -- `quiche-client` and `quiche-server` call
the new names. Exactly four methods are affected: `source_ids()` and
`retired_scids()`, added at step 09, keep their names, and so does
`available_dcids()` from step 08. A step 08-09 solution that keeps the old
spelling stops compiling with no replacement to guess at, which is why the
table above is exhaustive and the two survivors are named explicitly.

**The `peer_*` fields leave `Stats`.** `Stats` at step 09 had twenty-two
public fields: nine counters and thirteen `peer_max_idle_timeout`-style
mirrors of the peer's transport parameters. At this milestone `Stats` has
fourteen: the same nine, plus `reset_stream_count_local`,
`stopped_stream_count_local`, `reset_stream_count_remote`,
`stopped_stream_count_remote` and `path_challenge_rx_count`. Reading
`stats().peer_initial_max_data` no longer compiles. The values are still
available, through `Connection::peer_transport_params() ->
Option<&TransportParams>`, under names with the `peer_` prefix dropped:
`peer_initial_max_data` becomes
`peer_transport_params().unwrap().initial_max_data`. The accessor returns
`None` before the peer's parameters have been parsed, which `Stats` had no
way to express -- it reported zeros, indistinguishable from a peer that
really had asked for zero.

The same split happens in C, and it is not a rename: `quiche_stats` loses
its thirteen `peer_*` members, a new `quiche_transport_params` gains them,
and `bool quiche_conn_peer_transport_params(const quiche_conn *conn,
quiche_transport_params *out)` returns `false` when there is nothing to
report.

**One inconsistency is corrected rather than added to.** At step 09 the C
header declared `quiche_h3_event_type`'s six values as `HEADERS=0, DATA=1,
FINISHED=2, GOAWAY=3, RESET=4, PRIORITY_UPDATE=5`, and the Rust function
behind it returned 4, 5 and 6 for the last three. The header was right and
the implementation was wrong. At this milestone they agree:
`quiche_h3_event_type()` returns 3 for a GoAway, 4 for a Reset and 5 for a
PriorityUpdate. Nothing in the header changes.

Nothing else moves. `PathStats` is field-for-field what step 09 specified,
and so are `SendInfo`, `RecvInfo`, `PathEvent`, the `h3` module's public
surface, `Error`, and every `Config` setter step 09 required. Every step
01-09 group that does not name a renamed method or a `peer_*` field must
still hold untouched.

What is newly in scope here:

* `cap-bbr2` — Three things, and the first two are cheap only if the third
  is also true.

  The **name table**: `set_cc_algorithm_name("bbr2")` returns `Ok(())`, and
  `"reno"`, `"cubic"` and `"bbr"` still do. `"BBR2"`, `"bbr2 "`, `"bbr3"`
  and `""` all return `Err(quiche::Error::CongestionControl)`, so the accept
  list grew by exactly one entry and did not become permissive.

  The **discriminant**: `(Reno as u8, CUBIC as u8, BBR as u8, BBR2 as u8)`
  is `(0, 1, 2, 3)`, asserted as a tuple in one assertion so the message
  carries all four. BBR2 has to be appended after BBR rather than inserted
  among the existing three, because the values cross the C boundary and step
  01's and step 02's C groups pin the earlier ones.

  The **behaviour**: a `pair::Pair` built with
  `set_cc_algorithm(CongestionControlAlgorithm::BBR2)` on both sides
  handshakes, carries a 256 KiB body to completion byte-for-byte, and ends
  with a congestion window no smaller than the one it started the transfer
  with (the assertion is `>=`, not `>`) and `Stats::sent_bytes` at least the
  length of the body. Only ratios and inequalities are asserted, never an
  absolute window size or strict growth: a BBR-family controller's window is
  a function of its bandwidth and RTT estimates, an in-memory pair with no
  propagation delay reads both off the host's scheduling, and pinning a
  number -- or demanding the window rise -- would be pinning this machine.
  The same transfer is run once under `"bbr"` and once under `"bbr2"` and
  both must complete, so a candidate that aliases the new name onto the old
  algorithm still passes this group -- that is deliberate, since no
  observation available to a black-box checker can distinguish the two
  control laws on a lossless in-memory path, and asserting a difference
  would be asserting a machine-dependent accident.
* `cap-ffi-paths` — A C program the checker writes is compiled against
  `/app/quiche/include/quiche.h`, linked against the `libquiche.a` produced
  by `cargo build --features ffi`, and run. The checker builds the
  candidate's library itself with that feature into a scratch target
  directory, so a candidate whose default build omits the FFI is not at a
  disadvantage; a candidate whose `ffi` feature does not exist, or does not
  produce a `staticlib`, cannot satisfy this group. The link line is `-lpthread -ldl -lm -lstdc++`
  and nothing else, so the archive has to be self-contained exactly as step
  01's group required.

  The program then drives the new path and connection-ID surface and prints
  a `key=value` report the checker parses:

  * `quiche_connect`, then `quiche_conn_send` to pull the Initial out, so
  there is a real connection to ask questions of. * `quiche_conn_scids_left`
  and `quiche_conn_active_scids` before and after a `quiche_conn_new_scid`
  call, with the sequence number the call writes through its out-parameter.
  The first issued ID is sequence 1 and `active_scids` rises by one, which
  is the C mirror of the step 08 Rust group. * `quiche_conn_source_ids`
  walked with `quiche_connection_id_iter_next` until it returns `false`,
  reporting how many IDs it yielded and the length of the first one; then
  `quiche_connection_id_iter_free`. The iterator has to be a real iterator
  over the connection's state, not a snapshot of one element. *
  `quiche_conn_peer_transport_params` returning **`false`** on a client that
  has not yet processed the server's flight -- the C mirror of `None`, and
  the observable half of the `Stats` split. A candidate that returns `true`
  with a zeroed struct fails. On a handshaken server it returns `true`, and
  the program then reads the ten leading `uint64_t` members of
  `quiche_transport_params` back and compares them against the seven
  distinctive numbers the peer's `quiche_config_*` calls set plus the three
  RFC 9000 Section 18.2 defaults the peer left alone. **Only those ten are
  read.** The Rust mirror of this struct carries no `#[repr(C)]` at this
  release, so the compiler is free to reorder the `bool` and the three
  members behind it and the C side reads garbage there; the ten leading
  `uint64_t`s line up under the reference layout and would still line up if
  the attribute were added, so they are the part of the struct a candidate
  can be held to. * `quiche_conn_probe_path` on a connection with no spare
  connection IDs returning the C value of `Error::OutOfIdentifiers` (-18),
  and, once IDs have been issued, returning 0 and driving
  `quiche_conn_is_path_validated` for that 4-tuple from 0 to 1.
  `quiche_conn_is_path_validated` on a 4-tuple that is not a path of the
  connection returns `QUICHE_ERR_INVALID_STATE` (-6) rather than crashing or
  reporting a plain `false`: the C function returns `v as c_int` on success
  and the error code otherwise, so -6 is distinguishable from 0. *
  `quiche_conn_retired_scids` reading zero on a fresh connection, rising to
  1 after `quiche_conn_retire_dcid` retires a peer ID, and
  `quiche_conn_retire_dcid` on a sequence number that was never issued
  failing with a code that says why: `QUICHE_ERR_OUT_OF_IDENTIFIERS` (-18)
  on a client holding a single destination ID and no spare to replace it
  with, because the spare is checked before the sequence number is looked
  up, and `QUICHE_ERR_INVALID_STATE` (-6) once a spare exists and the
  sequence number itself is the problem.

  The same program then drives a real in-memory HTTP/3 exchange over the FFI
  -- `quiche_h3_send_request`, `quiche_h3_send_priority_update_for_request`,
  `quiche_h3_send_goaway` and a `quiche_conn_stream_shutdown` with error
  0x100 -- and asks `quiche_h3_event_type` for the integer behind each event
  `quiche_h3_conn_poll` hands back. HEADERS, GOAWAY, RESET and
  PRIORITY_UPDATE must report 0, 3, 4 and 5, the values the header's `enum
  quiche_h3_event_type` declares. At the previous milestone the
  implementation returned 4, 5 and 6 for the last three while the header
  declared 3, 4 and 5, so every C caller that switched on the enum took the
  wrong branch; this half of the group fails on step 09's tree without
  reading a line of anyone's source. On step 09's tree it does not even
  link: ten of the functions the program calls are not in that release's
  header.

  Nothing here greps the header or the Rust source. Every value comes from a
  compiled, linked, executed C program, and a declaration that disagrees
  with the Rust behind it produces either a link error or a wrong number.
* `cap-mtu-updated-qlog` — The `MtuUpdated` event as the candidate's own
  `qlog` crate serialises it. The checker writes a small Rust program
  depending on `/app/qlog` by path, builds it against the candidate's
  workspace, and runs it; the program constructs a `QlogStreamer` over a
  file in a scratch directory with `log_level: EventImportance::Extra`,
  calls `start_log()`, emits two `MtuUpdated` events and one event of a kind
  step 06 already pinned, then `finish_log()`. The checker reads the file
  back and parses it as a JSON Text Sequence, and requires:

  * the first record is the JSON-SEQ header step 06 specified --
  `qlog_version` `"0.3"`, `qlog_format` `"JSON-SEQ"`, no `traces` key -- so
  the new event did not arrive by changing the envelope; * both `MtuUpdated`
  records have `name` exactly `"connectivity:mtu_updated"`, and each
  record's key set is exactly `{data, name, time}`; * the event emitted with
  only `new` set has a `data` object whose key set is exactly `{new}` --
  `old` and `done` are omitted, not serialised as `null`, which is what
  `skip_serializing_none` on the struct means; * the event emitted with all
  three set has `data` keys exactly `{old, new, done}` with the numbers and
  the boolean the program passed in, round-tripped as JSON numbers rather
  than strings; * a second streamer, built at `EventImportance::Core` over a
  second file and fed the same `MtuUpdated`, writes **no** event record for
  it, because the event's importance is `Extra`. The program reports the
  `Err(Error::Done)` it gets back, and the second file holds its header
  record and the one `Core` control event and nothing else. This is what
  separates an implementation that added the schema type from one that also
  registered its importance -- and the control event is what separates it
  from a streamer that silently drops everything.

  No time value and no `reference_time` is asserted anywhere, and no
  substring search is used: every check is against parsed JSON key sets and
  values.
* `cap-peer-transport-params` — `Connection::peer_transport_params()` and
  the disappearance of the `peer_*` fields from `Stats`, on a `pair::Pair`
  whose two `Config`s are given deliberately distinctive, mutually different
  values -- `set_initial_max_data`, `set_initial_max_streams_bidi`,
  `set_max_idle_timeout`, `set_max_recv_udp_payload_size`,
  `set_active_connection_id_limit` and `set_disable_active_migration`, every
  one of them a number (or a boolean) that differs between the client and
  the server and appears nowhere else in the harness.

  The particular numbers are a choice of this group's, not a protocol fact,
  and they are deliberately **not** written down here. Their whole job is to
  be values no candidate could produce except by carrying the peer's
  parameters across the handshake and reading them back out; a contract that
  named them would turn the assertion into something a lookup table could
  satisfy. What the contract pins is the shape -- every value distinct,
  every value different on the two ends, none of them a default -- and the
  accessor's obligation to report the *peer's* number for each one.

  Before any packet is exchanged, `peer_transport_params()` is `None` on
  both peers. That is the assertion the old `Stats` mirror could not
  express, and a candidate that returns `Some(TransportParams::default())`
  from the start fails here rather than anywhere else.

  After the handshake, each peer reads back **the other's** configured
  values, not its own: the client's view of `initial_max_data` is what the
  server was given, and the asymmetry is what catches an implementation that
  wired the accessor to the local parameters. `max_idle_timeout` is in
  milliseconds, matching the setter. `disable_active_migration` crosses as a
  boolean in both directions independently. `max_udp_payload_size` and
  `active_conn_id_limit` are asserted separately from the rest, because a
  struct that carried *some* of the peer's answers and its own configuration
  for the remainder would satisfy one block and not the other.

  The two parameters no `Config` in this group touches --
  `ack_delay_exponent` and `max_ack_delay` -- read as RFC 9000 Section
  18.2's defaults, 3 and 25, on both ends. A peer that accepts a default
  does not send the parameter at all, so an implementation that fills the
  struct only from what appeared on the wire reports zero here and fails.

  `initial_source_connection_id` is `Some` and equals the connection ID the
  peer actually used, which is the one field that ties the struct to this
  connection rather than to a config; `retry_source_connection_id` and
  `max_datagram_frame_size` are `None`, because no Retry was issued and
  neither side enabled DATAGRAM.

  The group also requires that the `Stats` struct returned by `stats()` has
  the `peer_*` fields **gone**: it reads every one of the fourteen fields
  `Stats` is specified to have, in one destructuring-style assertion, so a
  `Stats` that still carries the thirteen mirrors alongside the new counters
  does not compile against this group and a `Stats` missing any of the
  fourteen does not either.
* `cap-scid-renames` — The four renamed methods, driven through the same
  sequence step 08 and step 09 drove the old names through, so the group is
  a rename check with real behaviour underneath it rather than a
  symbol-existence check.

  A `pair::Pair` handshakes with `set_active_connection_id_limit(3)`. Then:
  `scids_left()` is positive and `active_scids()` is 1 on each peer;
  `new_scid(&fresh_id, token, false)` returns `Ok(1)` on the client, because
  sequence 0 came from the handshake; `active_scids()` rises to 2 and
  `scids_left()` falls by one, so the two accessors are views of one piece
  of state and not independent counters; driving the pair makes the server's
  `available_dcids()` rise, which is the observable consequence of the
  NEW_CONNECTION_ID frame arriving; issuing past the peer's limit with
  `retire_if_needed = false` returns `Err(quiche::Error::IdLimit)`; and
  `retire_dcid(0)` on the server, driven until the retirement reaches the
  client, raises the client's `retired_scids()` to 1 and leaves
  `source_ids()` yielding exactly as many IDs as `active_scids()` counts --
  two, the handshake's and the one `new_scid` issued, with the retired one
  gone from the iterator.

  `source_ids()` and `retired_scids()` are read throughout under their step
  09 names, which is the half of the assertion that pins the rename as
  *exactly four methods*: a candidate that renamed everything containing
  "source" to "scid" breaks `source_ids()` and fails here, and a candidate
  that renamed nothing fails to compile against the group at all. The group
  also calls two singular accessors the rename leaves alone,
  `Connection::source_id()` and `Connection::destination_id()` (shapes in
  the interface section): `source_id().into_owned()` must be the ID
  `retired_scid_next()` later hands back, and `destination_id().as_ref()`
  must equal the peer's advertised `initial_source_connection_id`.
* `cap-stream-reset-stop-counts` — The four new `Stats` counters, and
  specifically the asymmetry RFC 9000 Section 3.5 creates between the two
  endpoints of one connection.

  A `pair::Pair` handshakes and the client writes one byte to each of
  streams 0, 2, 4 and 8. All four counters read zero on both peers at that
  point.

  **Reset path.** The client calls `stream_shutdown(0, Shutdown::Write,
  1001)` and the pair is driven until quiet. The client now reads
  `reset_stream_count_local` 1 and `reset_stream_count_remote` 0, while the
  server reads local 0 and remote 1: the same RESET_STREAM frame is counted
  under a different name at each end, which is the whole point of the
  local/remote split. The server then resets stream 0 in reaction and both
  peers read 1/1. The client resets streams 2, 4 and 8; the server resets 4
  and 8; and the final reading is client local 4 / remote 3 and server local
  3 / remote 4 -- the two endpoints disagree, by exactly the number of
  resets each originated. Every reading is taken after the pair has been
  driven, because the counters move when a frame is sent or received, not
  when the API call returns.

  **Stop path**, on a fresh pair. The server calls `stream_shutdown(0,
  Shutdown::Read, 1001)`, which sends STOP_SENDING. After driving, the
  server reads `stopped_stream_count_local` 1 and
  `reset_stream_count_remote` 1, and the client reads
  `stopped_stream_count_remote` 1 and `reset_stream_count_local` 1: the
  client auto-reset the stream because Section 3.5 requires it, so one API
  call moved a counter on each side of each name. Repeating for four streams
  gives server 4/0/0/4 and client 0/4/4/0 across (stopped_local,
  stopped_remote, reset_local, reset_remote). An implementation that counts
  API calls rather than frames, or that counts both directions into one
  field, cannot produce that table.
* `cap-path-challenge-queue` — `Stats::path_challenge_rx_count` and the
  bound `Config::set_path_challenge_recv_max_queue_len` puts on responding.

  On a pair with `set_active_connection_id_limit(4)` and connection IDs
  exchanged, the server's `path_challenge_rx_count` is zero after the
  handshake and after the client has sent STREAM frames on the active path:
  ordinary data does not move it. The client then probes a second local
  address; the server's count reaches 1 and rises by exactly one per probe,
  so the counter is of PATH_CHALLENGE frames and not of packets or of paths.

  The count moves **before** the queue decides anything. With
  `set_path_challenge_recv_max_queue_len(1)` on the server and the same
  4-tuple re-probed three further times, each datagram delivered before the
  server is ever given a chance to answer, four challenges reach a path
  whose queue holds one -- and the count rises by all four. A counter
  incremented where the frame is queued would stop at one. That is the
  assertion that distinguishes a counter incremented on receipt from one
  incremented at the point of queueing.

  What the group deliberately does **not** assert is a *behavioural*
  consequence of the bound. Measured against the reference: with the bound
  at one and four challenges outstanding, the path still validates, because
  the one challenge that was queued still produces a PATH_RESPONSE and one
  response is all validation needs. The discard is genuinely invisible from
  outside the connection; only the counter's disagreement with it is
  visible. The group asserts the path *does* validate, so that a candidate
  who made the discard observable by dropping validation would be caught
  too — read through `Connection::is_path_validated(local, peer)` on the
  probed four-tuple, which must be `Ok(true)` (the interface section states
  its shape: a `Result<bool>`, not the `int` the C wrapper returns).

  `send_on_path` for a 4-tuple that is not a path of this connection returns
  `Err(quiche::Error::InvalidState)` -- checked on a foreign local address
  and on a foreign peer address separately, because the check is on the
  4-tuple and not on either half -- and `send_on_path` on the active path
  with nothing pending returns `Err(quiche::Error::Done)` rather than an
  empty packet. Both are inherited from step 08 and re-read here because the
  queue bound must not have changed them.

`gate-endtoend` carries no capability of its own: it drives this step's
binaries end to end over loopback, and every gate group must pass. A stack
that implements every capability listed above and cannot complete one real
connection does not satisfy this step at all, however much of the new
capability it holds. The group table in `TEST_INTERFACE.md` is the contract
for it, as it is for the rest.

What must still hold: everything step 09 left working. 3 of this step's
groups re-run earlier milestones unchanged, listed by name in
`/app/WORKSPACE_HANDOFF.md`. They are gate groups as well, so any one of them
failing means the step is not satisfied. They are there because this step's
change plausibly breaks them: a rewrite that gets the new behaviour right
and drops an old one does not satisfy the step at all.

## 2. Read these local references

**Normative semantics:**

* `/app/spec/rfc9000.txt` — 18.2 (transport parameter definitions), 3.5
  (solicited state transitions), 19.4 (RESET_STREAM frames), 19.5
  (STOP_SENDING frames), 8.2 (path validation), 8.2.2 (path validation
  responses), 19.17 (PATH_CHALLENGE frames), 5.1.1 (issuing connection IDs),
  5.1.2 (consuming and retiring connection IDs), 9 (connection migration).
  18.2 is the authority for `TransportParams`: which parameters exist, which
  have defaults and what those defaults are, which is what makes a parameter
  the peer never sent readable as a number rather than as an absence. 3.5
  with 19.4 and 19.5 is the reason the four new stream counters are
  asymmetric between the two endpoints -- a STOP_SENDING frame *requests* a
  RESET_STREAM, so one shutdown produces a stopped count on one side and a
  reset count on the other. 8.2.2 is the obligation
  `set_path_challenge_recv_max_queue_len` bounds: an endpoint MUST echo
  every PATH_CHALLENGE it receives, and a queue limit is how an
  implementation refuses to be made to do unbounded work by a peer that
  challenges faster than it can answer. 19.17 is the frame
  `path_challenge_rx_count` counts.

  5.1.1 and 5.1.2 are the behaviour behind the four renamed methods, and the
  reason the rename is only a rename: they draw the line between the
  connection IDs an endpoint issues (its *source* IDs, now `scid`) and the
  ones it uses to address its peer (its *destination* IDs, now `dcid`), and
  that line is what the new names make explicit. Section 9 is what the newly
  exposed C entry points drive.
* `/app/spec/rfc9002.txt` — 7 (congestion control), 7.2 (initial and
  minimum congestion window), 7.3 (congestion control states), 4.7 (probe
  timeout). Background for the fourth congestion-control algorithm. RFC 9002
  specifies the Reno-shaped default, not BBRv2, and that is the point: 7.3's
  slow-start / recovery / congestion-avoidance state machine is the
  behaviour a BBR-family controller deliberately does *not* implement, so
  the only things a group can fairly assert of BBRv2 are the ones 7 and 7.2
  make universal -- the window starts at the initial window, grows while the
  path is not congested, and every byte handed to the transport is
  eventually sent.
* `/app/spec/rfc9114.txt` — 7.2 (frame definitions), 7.2.6 (GOAWAY), 4.1
  (HTTP message framing). The HTTP/3 events the C API reports, and the
  numbering that had to be corrected: the six `quiche_h3_event_type` values
  are the observable outcomes of this section's frames. Read for GOAWAY in
  particular, which is the first of the three values whose returned integer
  moves at this milestone.
* `/app/spec/rfc9221.txt` — 3 (datagram frame types), 4 (behavior and
  usage). Unchanged at this milestone and cited so it stays that way. The
  transport DATAGRAM frame API is what the inherited datagram group
  exercises, and the two new C predicates over its queues are mirrors of
  step 04's Rust ones. Nothing about the frame or its flow-control exemption
  moves here.

**Additional files:**

* `/app/TEST_INTERFACE.md` — the contract between your code and the
  checker: every name, path and flag it depends on, the group table it
  defines, and what is deliberately out of scope. Where it and your intuition
  disagree, it wins.
* `/app/WORKSPACE_HANDOFF.md` — what the previous step left behind and
  which of its milestones remain in scope here.
* `/app/spec/README.md` — which sections of which document are
  load-bearing for this step, and `SHA256SUMS` proving the corpus is
  byte-identical to the IETF originals.
* `/app/spec/appendix-a-vectors.json` — RFC 9001 Appendix A's worked
  example as named byte strings. The checker carries a byte-identical copy of
  the same file in its own harness; no group at this step reads either one, so
  it is a reference for your own plumbing rather than a check that runs.
* `/app/.cargo/config.toml` — `QUICHE_BSSL_PATH` points at the BoringSSL
  prebuilt into the image. Link it; do not fetch or compile one. The
  build must succeed with no network.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Continue cumulatively in `/app`; preserve inherited source, manifests,
  documents, and handoff files.
- This environment has no network access, and everything the build needs is already installed. The upstream project's source, its releases, mirrors and package registries are unreachable, so a copy cannot be fetched by any route. Do not search for, download, copy, wrap, or invoke an
  online implementation or task-specific answer; do not inspect `/tests`,
  later-step directories, or solution material.
- Do not edit the supplied documents (`TEST_INTERFACE.md`, `spec/`) or weaken, filter, rename,
  or disable tests. Run the references' local checks and leave a reproducible
  handoff.
- This milestone is modelled on a real crate. Depending on, vendoring, or
  copying source from `quiche`, `tokio-quiche`, `quinn`, `s2n-quic`, `neqo` or
  any other QUIC or HTTP/3 implementation is a task-specific answer and is out
  of bounds; the tree is built offline and a manifest that names one will not
  resolve. Cryptography, byte-buffer and async-runtime crates are ordinary
  dependencies and are fine.
