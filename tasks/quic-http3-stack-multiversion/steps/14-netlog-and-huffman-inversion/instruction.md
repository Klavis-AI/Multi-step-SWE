# Step 14 — A netlog reader, a Huffman coder that changed crates, and a retry-aware accept

## 1. Implement this milestone

Two new workspace members, one piece of existing machinery that moves down a
layer and changes shape on the way, a server-side accept that finally knows
about the Retry it just sent, and three smaller additions on the connection.

**A netlog reader, as a workspace member.** `netlog` is a deserializer for
the line-delimited JSON that Chromium writes when a user exports a network
log. Its first line is a *constants* object that maps every event type,
phase and source type used in that file to an integer, and every subsequent
line is one event that refers to those integers rather than to names. The
crate reads the constants, inverts the maps, and turns a raw record into a
typed event for the QUIC, HTTP/3, HTTP/2 and HTTP families it knows about.

The numbering is not a constant of the format. Two netlogs written by two
Chromium builds may give `QUIC_SESSION` different integers, so a reader that
hard-codes any of them is wrong on the next file it is handed. The case
is built on exactly that property: its fixture uses numbers no Chromium
build ever used.

**A second new member, `qlog-dancer`**, a log-analysis and visualisation
tool built on `netlog` and `qlog`. The workspace goes from nine members to
eleven.

**The Huffman coder changes crates.** The HPACK Huffman code -- the one
QPACK string literals use -- used to live inside the transport crate, under
`h3::qpack`, where nothing outside it could reach. It is now in `octets`,
behind a non-default feature named `huffman_hpack`, as three public entry
points: a decoder on the octet reader, an encoder on the octet writer, and a
free function that answers how long an encoding would be without producing
it. The transport crate becomes a *consumer* of it, depending on `octets`
with that feature turned on.

That is the inversion. The same code, the same table, the same answers --
reachable from outside the workspace for the first time, and no longer
present at the path it used to occupy.

**`accept_with_retry`.** Until now a server that sent a Retry re-derived the
connection IDs by convention: `accept` took the original destination CID and
assumed the Retry's source CID was the same as the new connection's SCID.
The new entry point takes them as data -- a `RetryConnectionIds` carrying
both -- so the server may choose a fresh SCID for the connection that is not
the SCID it put in the Retry. `accept` keeps its old behaviour, exactly.

**Three additions on the connection.** `Connection::pmtu` reports the path
MTU that discovery settled on, and `revalidate_pmtu` asks for it to be
measured again; `Connection::stream_discard` consumes bytes from a stream
without copying them anywhere; and a STOP_SENDING that is lost is now
retransmitted for as long as the peer's sending half is still open.

The Huffman coder is not where it was. Code that reached it through the
transport crate's internals is looking at a path that no longer exists, and
code that wants it now names `octets` and turns a feature on. A workspace
that depends on `octets` without `huffman_hpack` gets an `octets` with no
Huffman in it at all and no error saying so -- the feature is off by
default, and the three entry points are simply absent.

The encoder and the length function are generic over a `const LOWER_CASE:
bool` rather than taking a flag. The parameter is not decoration: with it
`true` the input is lowercased before it is coded, which changes both the
bytes and the length, so the two are the same function only for input that
was already lower case.

`huffman_encoding_len` can fail. It is not a pure size calculation that
always has an answer: input whose Huffman encoding would be **strictly
longer** than the input itself is refused rather than measured, because a
coder that never refuses would hand its caller a "compressed" form that is
larger. Equal length is not refused -- a one-byte input whose code also comes
to one byte is measured and returned like any other, and the literal form is
not forced on it. A caller that treats the return as infallible has no path
for the case the crate exists to signal.

`accept_with_retry` separates two connection IDs that `accept` conflated.
The client validates the pair: it remembers the destination CID it used on
its first Initial and the source CID of the Retry it got back, and it checks
both against the transport parameters the server sends. A server that passes
either one wrongly does not get a connection with a warning -- it gets a
client that closes with a transport parameter error, and the server never
learns why.

`pmtu()` returns an `Option`, and `None` has two different causes that a
caller cannot tell apart: discovery was never enabled, or it is enabled and
has not finished. Both are "no answer yet". It is not an error, and a
connection that carries data perfectly well may answer `None` for its whole
life.

What is newly in scope here:

* `cap-huffman-in-octets` — The Huffman coder at its new address, reached
  from outside the workspace, and checked against the code the specification
  fixes.

  A small crate is written into scratch -- a `Cargo.toml` with its
  own empty `[workspace]` table so it is not pulled into the candidate's,
  and a path dependency on `<candidate root>/octets` with `features =
  ["huffman_hpack"]` -- with a `main.rs` that prints one line per
  observation. Building that crate is the first assertion, and it is the one
  that makes this a T4 group rather than a source inspection: an `octets`
  that has no such feature, or that has the feature but not the three entry
  points, does not compile against it, and nothing has been read from the
  candidate's source to find that out.

  It is built with `cargo build --offline -j 4` and with `CARGO_TARGET_DIR`
  pointed at the target directory the driver already used for the
  candidate's workspace, so the dependency is already compiled and the probe
  build is a link.

  The probe's output is compared against a fixed table, line for line, in
  order. A mismatch is reported as the first differing line with both sides
  quoted, so a failure names the input that was coded wrongly rather than
  saying the output differed.

  ```
  enc|www.example.com|false|f1e3c2e5f23a6ba0ab90f4ff
  len|www.example.com|false|12
  enc|no-cache|false|a8eb10649cbf
  len|no-cache|false|6
  enc|custom-key|false|25a849e95ba97d7f
  len|custom-key|false|8
  enc|custom-value|false|25a849e95bb8e8b4bf
  len|custom-value|false|9
  enc|/index.html|false|60d5485f2bce9a68
  len|/index.html|false|8
  enc|a|false|1f
  len|a|false|1
  enc||false|
  len||false|0
  enc|WWW.EXAMPLE.COM|false|e5cb92f83f21d1af3e05ef6ad1
  len|WWW.EXAMPLE.COM|false|13
  enc|WWW.EXAMPLE.COM|true|f1e3c2e5f23a6ba0ab90f4ff
  len|WWW.EXAMPLE.COM|true|12
  dec|f1e3c2e5f23a6ba0ab90f4ff|www.example.com
  dec|a8eb10649cbf|no-cache
  dec|25a849e95ba97d7f|custom-key
  dec|25a849e95bb8e8b4bf|custom-value
  dec|60d5485f2bce9a68|/index.html
  dec||
  roundtrip|www.example.com|false|www.example.com
  roundtrip|WWW.EXAMPLE.COM|false|WWW.EXAMPLE.COM
  roundtrip|WWW.EXAMPLE.COM|true|www.example.com
  len_unprofitable|00|err
  len_unprofitable|00010203|err
  short_buffer|err
  ```

  `enc` is `put_huffman_encoded::<LOWER_CASE>` into a fresh buffer, rendered
  as hex. `len` is `huffman_encoding_len::<LOWER_CASE>` on the same input,
  and every `len` line is the byte length of the `enc` line above it -- that
  pairing is the point of listing both. `dec` is `get_huffman_decoded` over
  the bytes. The three `roundtrip` lines encode and then decode, and the
  third is the one that shows `LOWER_CASE` is applied before coding rather
  than after: the value that comes back is lower case, so the transformation
  happened on the way in.

  The last three lines are the negatives. `len_unprofitable` asks for the
  encoded length of input whose encoding would not be shorter, and gets an
  error both for a single zero byte and for four ascending bytes.
  `short_buffer` asks the encoder to write `www.example.com` into a
  four-byte buffer and gets an error rather than a truncated write. Each is
  an exact-answer negative: the assertion is that the call failed, not that
  it failed with a particular message.

  The values are the canonical HPACK examples and are fixed by the code
  table. They were re-derived against the reference implementation before
  being written here.
* `cap-netlog` — The netlog reader against a fixed, hand-written fixture,
  parsed into an exact expectation table.

  The fixture is a complete twelve-event netlog in the shape Chromium writes
  them: a constants line ending in a bare comma, an `"events": [` line, ten
  event lines ending in commas, and a final event line that closes the array
  and the document. It records one HTTP/3 session -- session begin,
  transport parameters sent, request headers, a priority update, a packet
  sent, a stream frame received, response headers decoded, data sent,
  settings sent, an unrelated socket event, session end, and a STOP_SENDING
  sent.

  **The event type numbers in the fixture are not Chromium's.** They are 500
  to 504 for the QUIC types, 600 to 604 for the HTTP/3 types and 700 for the
  socket type, and the phase map gives `PHASE_NONE`, `PHASE_BEGIN` and
  `PHASE_END` the values 0, 1 and 2. Nothing outside this file uses those
  numbers. A reader that resolved names from a built-in table instead of
  from the file's own `logEventTypes` and `logEventPhase` maps produces a
  different answer for every line, which is precisely the property being
  checked: the constants are the file's, and the crate exists to read them.

  The probe crate -- again outside the workspace, again a path dependency,
  this time on `<candidate root>/netlog` plus `serde_json` -- reads the
  constants, skips the array-opening line, and then for each record prints

  ```
  <index>|<type>|<phase>|<time>|<source id>|<source type>|<parsed event>
  ```

  where the type and phase are the strings `populate_strings` resolved, the
  time is `time_num` after the caller parsed the decimal string, and the
  last field is a stable rendering of what `parse_event` returned. The
  rendering is written by the probe, not by the crate: it names the variant
  and the handful of fields the case cares about, so that the expectation
  does not depend on anyone's `Debug` output.

  ```
  constants version=1 tick_offset=1700000000000 types=11 phases=3
  0|QUIC_SESSION|PHASE_BEGIN|12|41|25|quic:session host=quic.test port=443 cid=5f6e7d8c
  1|QUIC_SESSION_TRANSPORT_PARAMETERS_SENT|PHASE_NONE|13|41|25|quic:tp-sent initial_source_connection_id 5f6e7d8c
  2|HTTP3_HEADERS_SENT|PHASE_NONE|21|41|25|h3:headers-sent id=0 headers=[:method: GET/:scheme: https/:authority: quic.test/:path: /index.html]
  3|HTTP3_PRIORITY_UPDATE_SENT|PHASE_NONE|22|41|25|h3:priority-update elem=0 field=u=3, i ty=request_stream
  4|QUIC_SESSION_PACKET_SENT|PHASE_NONE|23|41|25|quic:packet-sent level=ENCRYPTION_FORWARD_SECURE pn=7 sent_us=1234567
  5|QUIC_SESSION_STREAM_FRAME_RECEIVED|PHASE_NONE|34|41|25|quic:stream-recv id=0 fin=false off=0 len=1450
  6|HTTP3_HEADERS_DECODED|PHASE_NONE|35|41|25|h3:headers-decoded id=0 headers=[:status: 200/content-type: text/plain]
  7|HTTP3_DATA_SENT|PHASE_NONE|41|41|25|h3:data-sent id=0 len=4096
  8|HTTP3_SETTINGS_SENT|PHASE_NONE|42|41|25|none
  9|SOCKET_ALIVE|PHASE_NONE|43|9|8|none
  10|QUIC_SESSION|PHASE_END|44|41|25|none
  11|QUIC_SESSION_STOP_SENDING_FRAME_SENT|PHASE_NONE|51|41|25|quic:stop-sending id=0 err=3
  records=12
  ```

  Four lines matter most. Lines 0 and 10 are the *same* event
  type in two phases, and only one of them parses into a session event --
  the reader distinguishes them by phase, not by type. Line 8 is a type the
  crate recognises by name and does not model, and line 9 is a type outside
  every family prefix; both must be `None` rather than an error, and a
  reader that treats an unmodelled event as a failure stops at line 8. Line
  11 is the last record in the file, the one whose trailing `]}` the record
  reader has to strip, and it is the line that would be lost by an
  off-by-one at end of input -- which is why the count is printed and
  asserted rather than left implicit.

  The comparison is line for line against the table above, with the first
  difference reported. The whole fixture is fixed, hand-written bytes, so
  there is nothing timing-dependent, network-dependent or clock-dependent in
  it.
* `cap-accept-with-retry` — A full Retry exchange driven through the
  public API, in memory, three times: once correctly and twice with one of
  the two connection IDs wrong.

  Each leg builds a client with `quiche::connect` and takes its first
  Initial without a server on the other side. That packet is parsed with
  `Header::from_slice`, and the server is played by hand: a Retry source
  connection ID is picked that is deliberately **not** the connection's
  eventual SCID, `quiche::retry` is called with the client's SCID, the
  Initial's destination CID, that new SCID, a twenty-byte token and the
  packet's version, and the Retry is handed back to the client.

  The client's response is the first observation. It produces a second
  Initial that carries a non-empty token and whose destination connection ID
  is the Retry source CID -- not the one it started with. Both are asserted.

  The server side is then built with `quiche::accept_with_retry`, given a
  fresh connection SCID and a `RetryConnectionIds` whose two fields the leg
  controls, and the two ends are pumped against each other for up to
  thirty-two rounds.

  | leg | `original_destination_cid` | `retry_source_cid` | expected |
  |---|---|---|---| | positive | the first Initial's DCID | the Retry's SCID
  | both ends established, client `local_error` is `None` | | negative A |
  the first Initial's DCID | a bogus CID | client not established,
  `local_error` is `(is_app = false, 0x08)` | | negative B | a bogus CID |
  the Retry's SCID | client not established, `local_error` is `(is_app =
  false, 0x08)` |

  The two negatives are what make the positive mean something. A server
  implementation that ignored `retry_cids` entirely and echoed its own SCID
  -- which is what `accept` does -- would pass a positive-only version of
  this group whenever the test happened to choose matching IDs, and the
  whole point of the new entry point is that they need not match. What
  catches a conflation here is the negative half: each negative leg
  mismatches a single field of the pair -- a bogus `original_destination_cid`
  in one, a bogus `retry_source_cid` in the other -- so a server that echoes
  its own SCID instead of reading `retry_cids` fails both.

  `0x08` is `TRANSPORT_PARAMETER_ERROR`, and it is required by RFC 9000
  Section 7.3 rather than chosen by this crate. The assertion is on the
  error code and on the `is_app` flag being false; nothing reads a reason
  string.

  **In memory, and on purpose.** This is a T3 group rather than a T1 one
  because the shipped `quiche-server` does not use this entry point -- it
  calls `quiche::accept` -- so there is no command line that would exercise
  it. The public API is the only surface it is reachable from.
* `cap-retransmit-stop-sending` — That a lost STOP_SENDING comes back when
  the peer may still send, and does not when the peer has finished --
  decrypted off the wire, both ways.

  Each leg runs a handshaked pair and then:

  1. the peer opens a stream and sends data on it, with or without a fin
  depending on the leg; 2. this endpoint calls `stream_shutdown` with
  `Shutdown::Read` and an application error code, which queues a
  STOP_SENDING; 3. the datagram carrying it is taken and **withheld** --
  never delivered; 4. six further rounds of ordinary traffic are exchanged
  on a second stream, so the peer acknowledges packets numbered after the
  withheld one; 5. every datagram this endpoint sent is decrypted with 1-RTT
  keys taken from the connection's own keylog, and its frames are decoded.

  Loss is established by the packet reordering threshold of RFC 9002 Section
  6.1 -- later packets acknowledged, the withheld one not -- and not by
  waiting for a timer. Both legs assert that at least one packet was
  declared lost, so a leg that failed to create the loss it was arranging
  fails loudly instead of quietly reporting "no retransmission".

  | leg | the peer's stream when STOP_SENDING is sent | expected |
  |---|---|---| | A | still open, no fin sent | the frame appears again in a
  later packet | | B | already finished, fin delivered | it does not appear
  again |

  The frame is matched exactly -- the STOP_SENDING variant with this stream
  id and this application error code -- not by counting frames or by
  searching for a type byte. The stream id and the error code are this
  group's own choices, held in `const`s inside the case and deliberately not
  named here: what is asserted is that the code handed to `stream_shutdown`
  in step 2 is the code that comes back on the wire, and a contract that
  pinned a number would let an implementation emit *that* number instead of
  carrying the caller's. The packets are the real ones: they are read out of
  the send path, decrypted with the traffic secret from the keylog, and
  parsed into frames, so nothing here depends on an internal counter or a
  log line.

  Leg B is the half that a plausible wrong implementation fails.
  Retransmitting unconditionally satisfies "the frame comes back" and
  violates Section 13.3's condition, which stops the retransmission once the
  receiving part of the stream has reached "Data Recvd". Both legs were
  measured against the previous step's tree as well: there, neither leg
  retransmits, because that tree has no arm for this frame in its lost-frame
  handler at all.
* `cap-retransmit-max-streams` — That a lost MAX_STREAMS_BIDI comes back
  -- decrypted off the wire, like its STOP_SENDING sibling.

  The case runs a handshaked pair and then:

  1. completes client-initiated bidirectional streams, one at a time, until
  a datagram the **server** sends is found to carry a MAX_STREAMS frame for
  bidirectional streams. The loop is bounded and fails loudly if no grant
  ever appears, so a candidate that never regrants stream credit fails here
  rather than skipping the part that matters; 2. **withholds** that datagram
  -- it is never delivered -- and delivers the rest of the same batch; 3.
  exchanges six further rounds of ordinary traffic, so the client
  acknowledges packets numbered after the withheld one; 4. decrypts every
  datagram the server sent afterwards with 1-RTT keys taken from the
  connection's own keylog, and decodes their frames.

  Loss is established by the packet reordering threshold of RFC 9002 Section
  6.1 -- later packets acknowledged, the withheld one not -- and not by
  waiting for a timer. The case asserts that the server declared at least
  one packet lost, so a run that failed to create the loss it was arranging
  fails loudly instead of quietly reporting "no retransmission".

  The keys are the **server's**, read from the client's keylog, because the
  server is the endpoint granting credit here. The frame is matched on its
  variant and its bidirectional flag, not by counting frames or searching
  for a type byte.

  One property of the arrangement carries the whole leg, and it is
  deliberate: **the filler traffic in step 3 runs on a unidirectional
  stream.** Completing more bidirectional streams would give the server a
  fresh reason to send MAX_STREAMS, and the leg would then pass against an
  implementation whose lost-frame handler does nothing at all. With the
  filler on a unidirectional stream, retransmission is the only route by
  which a second bidirectional grant can appear. Measured against the
  previous step's tree as well: there the frame never comes back, because
  that tree has no arm for it in its lost-frame handler.
* `cap-stream-discard` — `stream_discard` as the receive-side sibling of
  `stream_recv`, in four exact legs on an in-memory pair.

  The client sends 4000 bytes on stream 0 with a fin. The server then
  discards in two pieces and asks twice more:

  | call | returns | |---|---| | `stream_discard(0, 1000)` | `Ok((1000,
  false))` | | `stream_discard(0, 3000)` | `Ok((3000, true))` | |
  `stream_discard(0, 1000)` | `Err(Error::Done)` | | `stream_discard(400,
  1000)` | `Err(Error::InvalidStreamState(400))` |

  and `stream_finished(0)` is true between the second call and the third.

  Every leg is an exact value rather than a bound. The first shows the `len`
  argument is a ceiling that is honoured when more is available; the second
  shows `fin` arriving on the call that consumes the last byte and not
  before; the third shows an exhausted stream reporting `Done` rather than a
  zero-length success, which is the difference between a discard loop that
  terminates and one that spins; the fourth shows an unknown stream id being
  refused, with that id echoed back in the error.

  The pair runs a plain ALPN, not HTTP/3, so the only stream in play is the
  one the case opened and nothing else is competing for the connection's
  flow control window. The whole group is a single in-process exchange with
  no timers.
* `cap-pmtu-discovered` — `pmtu()` and `revalidate_pmtu()` as a state
  machine with four observable states, on an in-memory pair.

  | when | `pmtu()` | |---|---| | before the handshake, discovery on |
  `None` | | after handshake and settle, discovery **off** | `None` | |
  after handshake and settle, discovery **on** | `Some(v)`, the same `v` at
  both ends | | immediately after `revalidate_pmtu()` | `None` again | |
  after a further settle | `Some(v)` again |

  and the discovered value satisfies `1200 <= v <= 1452` -- the QUIC minimum
  datagram size from RFC 9000 Section 14, and the maximum this harness's
  pair will send.

  The second row is what stops this group from being satisfied by a stub. An
  accessor hard-wired to return a plausible number passes the third row and
  fails the second; one hard-wired to return `None` passes rows one, two and
  four and fails the third. The two together require the value to actually
  track whether discovery ran.

  The fourth and fifth rows are `revalidate_pmtu`. The `None` immediately
  after the call is checked before anything else is sent, which is the only
  moment at which it is unambiguous: it shows the call restarted the search
  rather than merely scheduling a probe for later. The value that comes back
  afterwards is compared to the one from before, so a revalidation on an
  unchanged path settles where it settled the first time.

  The bound is one-sided at neither end by accident: an exact figure would
  be an assertion about this harness's datagram size rather than about the
  candidate, and a bound that only checked the floor would accept an
  implementation that reported a number larger than anything it could send.
* `cap-eleven-member-workspace` — The workspace layout, read the way cargo
  reads it.

  Two halves. First, each of the eleven expected members has a manifest at
  `<candidate root>/<name>/Cargo.toml` -- the nine carried forward and the
  two new ones, `netlog` and `qlog-dancer`. Second,

  ```
  cargo metadata --offline --no-deps --format-version 1
  ```

  is run at the candidate root and its `packages` array is checked: every
  expected package name is present as a package whose id is a local path,
  and the number of local-path package ids is exactly eleven.

  Counting by path id rather than by matching names is deliberate. `qlog` is
  a substring of `qlog-dancer`, so a substring search over the metadata
  finds the first inside the second and would report a package that is not
  there; and a name-only check would not notice a twelfth member that should
  not exist.

  `--no-deps` is what makes the count a statement about the workspace rather
  than about the dependency graph, and `--offline` is what makes it run at
  all without network access.

  **This is a layout group and is checked as one.** It is structural
  breadth, not the milestone. It asserts nothing about behaviour,
  and the OBSERVATION-TOOLBOX's one exception to the no-source-reading rule
  is exactly this: a manifest may be read as a description of the package's
  shape, never as evidence of what the code does.
* `cap-cc-name-collapse` — The contraction of
  `CongestionControlAlgorithm`, read as the integer each name resolves to
  rather than as the presence of a Rust identifier.

  This release deletes the `BBR` and `BBR2` variants and points both of
  their names at the gcongestion implementation. `FromStr` still accepts
  five names, and three of them now answer the same number:

  | name | discriminant | |---|---| | `reno` | 0 | | `cubic` | 1 | | `bbr` |
  4 | | `bbr2` | 4 | | `bbr2_gcongestion` | 4 |

  and `"BBR"`, `"bbr3"` and `""` are refused with
  `Error::CongestionControl`.

  The enum is `#[repr(C)]` and the C API hands these values across the
  boundary unchanged, so the name-to-integer mapping is the part of this
  change a caller outside Rust can see. The previous milestone answered 2
  for `bbr` and 3 for `bbr2`; a candidate that keeps those variants is
  selecting a controller this release no longer has, and every C caller that
  names either string gets it.

  Compared as integers, through `from_str`, and never by naming a variant. A
  case that wrote `CongestionControlAlgorithm::BBR` would fail to compile
  here rather than fail an assertion, which would cost a candidate every
  group in the file instead of this one. The whole group is three lines of
  table lookup with no connection, no timers and no I/O.

`gate-endtoend` carries no capability of its own: it drives this step's
binaries end to end over loopback, and every gate group must pass. A stack
that implements every capability listed above and cannot complete one real
connection does not satisfy this step at all, however much of the new
capability it holds. The group table in `TEST_INTERFACE.md` is the contract
for it, as it is for the rest.

What must still hold: everything step 13 left working. 3 of this step's
groups re-run earlier milestones unchanged, listed by name in
`/app/WORKSPACE_HANDOFF.md`. They are gate groups as well, so any one of
them failing means the step is not satisfied. They are there because this
step's change plausibly breaks them: a rewrite that gets the new behaviour
right and drops an old one does not satisfy the step at all.

## 2. Read these local references

**Normative semantics:**

* `/app/spec/rfc9000.txt` — 3.5 (solicited state transitions), 7.3
  (authenticating connection IDs), 8.1.2 (address validation using Retry
  packets), 13.3 (retransmission of information), 14.2 (path maximum
  transmission unit), 14.3 (datagram packetization layer PMTU discovery),
  17.2.5 (Retry packet), 19.5 (STOP_SENDING frames). 13.3 is the single most
  load-bearing section for this step, and it is short. It lists, frame
  family by frame family, what an endpoint does when a packet it sent is
  declared lost. Most control frames are re-sent as the *current* state
  rather than the state they carried; connection-close signals are not
  re-sent at all; and STOP_SENDING has a rule of its own, quoted here
  because the case is a direct transcription of it: the request *is
  sent until the receiving part of the stream enters either a "Data Recvd"
  or "Reset Recvd" state*. Read it for the condition, not just the
  permission -- the sentence forbids something as well as requiring
  something, and both halves are checked.

  3.5 is what that condition means. It is the section that defines why one
  endpoint asks another to stop sending at all, what states the request is
  useful in, and what the peer is obliged to do with it. Read it before
  deciding what "the peer has finished" means: it is a property of the
  receiving half of the stream at the endpoint that sent the frame, and it
  is reached by the peer's fin arriving, not by anything the sender does.
  19.5 is the wire format underneath, two variable-length integers, and it
  is the reason a retransmission carries the same stream id and application
  error code as the original rather than a refreshed value.

  8.1.2 and 17.2.5 are the Retry exchange `accept_with_retry` completes.
  8.1.2 describes the round trip -- server refuses to commit state, sends a
  Retry with a token and a new source connection ID, client retries with the
  token, server validates it -- and 17.2.5 is the packet itself, including
  the retry integrity tag that makes it unforgeable. Read 8.1.2 for the
  sentence about the client's subsequent Initial being sent to the Retry's
  source connection ID, which is why a server that changes that ID later has
  to say so.

  7.3 is why saying so is mandatory rather than polite. It requires the
  server to echo, in its transport parameters, both the destination
  connection ID from the client's first Initial and the source connection ID
  of the Retry, and requires the client to close the connection with a
  transport parameter error if either does not match what it observed. That
  requirement, and not any behaviour this crate invents, is what makes a
  wrong `RetryConnectionIds` a `0x08` close.

  14.2 and 14.3 are the vocabulary of `pmtu()`. 14.2 is the path MTU as a
  property of the path rather than of the endpoint, and the 1200-byte floor
  that every QUIC path must clear; 14.3 is DPLPMTUD, the search procedure,
  which is why the answer is a value that was *confirmed* by a probe and its
  acknowledgement rather than a value that was assumed. Read 14.3 for why
  the accessor returns an option: while the search is running there
  genuinely is no discovered PMTU, and the connection works anyway.
* `/app/spec/rfc9002.txt` — 6 (loss detection), 6.1 (acknowledgment-based
  detection), 6.2 (probe timeout). Section 6 is where "the packet was lost"
  acquires a definition. Nothing on the wire says a packet was lost; an
  endpoint *declares* it lost, on its own judgement, and Section 6 is the
  set of rules for that judgement. Read it first, because the STOP_SENDING
  behaviour this step adds is triggered by a declaration and not by an
  event.

  6.1 is the declaration the case relies on. A packet is lost when a
  later packet has been acknowledged and the gap is wide enough -- the
  packet reordering threshold, three packets -- or old enough. The packet
  threshold is the deterministic half of the section: it depends on how many
  later packets the peer acknowledged, which the case controls exactly, and
  not on elapsed time, which it does not. That is why the case establishes
  loss by delivering later packets and withholding one, rather than by
  waiting.

  6.2 is the timer-driven half, and it is here so that it can be recognised
  and avoided. A probe timeout fires on a clock and sends new data to elicit
  an acknowledgement; anything that depends on it is a race. Read 6.2 to see
  what this step's observations are deliberately not built on.
* `/app/spec/rfc9204.txt` — 4.1.1 (prefixed integers), 4.1.2 (string
  literals), 7.2 (static Huffman encoding). 4.1.2 is the reason the Huffman
  coder exists and the reason it is allowed to refuse. A QPACK string
  literal is a length-prefixed byte string preceded by a single bit saying
  whether it is Huffman coded, and *when Huffman encoding is enabled, the
  Huffman table from Appendix B of RFC 7541 is used*. Two things follow, and
  both are in the case. The table is not this crate's invention, so
  its outputs are fixed by the specification rather than chosen -- there is
  exactly one correct encoding of a given input. And the bit exists because
  coding is optional per string: an encoder is expected to compare the two
  forms and pick the shorter, which is exactly the decision
  `huffman_encoding_len` refusing to measure an unprofitable input hands
  back to its caller.

  4.1.1 is the length that precedes the string, a prefixed integer whose
  prefix shares a byte with the Huffman bit. It is worth reading for one
  detail: the length on the wire is the length of the *encoded* form, so an
  encoder has to know how long the encoding will be before it writes it.
  That is why a size-only entry point is part of the surface rather than an
  optimisation.

  7.2 is the security note on using a static table, and it is the reason the
  table is static in the first place: a code computed from the data being
  coded leaks about the data, and a fixed code does not. Read it for why no
  part of this surface is adaptive, and therefore why the same input coded
  twice, in two processes, at two times, produces the same bytes.

  The Huffman table itself is in Appendix B of RFC 7541, which is not in
  this corpus. It is not needed: every value the case checks is
  stated in the interface document as a table, and an implementation that
  ports the code from where it already lives in this repository does not
  need to re-derive it.
* `/app/spec/rfc9114.txt` — 4.1 (HTTP message framing), 7.2.1 (DATA),
  7.2.2 (HEADERS), 7.2.4 (SETTINGS). This one is read for vocabulary rather
  than for behaviour. The netlog fixture the case parses is a
  recording of an HTTP/3 session, and the events in it are named after the
  frames in this section: a headers-sent event, a headers-decoded event, a
  data-sent event with a payload length, a settings-sent event. A reader
  that does not know what those are cannot tell a plausible fixture from an
  implausible one.

  4.1 is the shape of a message as a sequence of frames, and it is why the
  fixture's events appear in the order they do -- the request header
  section, then the response header section, then body. 7.2.2 is the HEADERS
  frame, whose payload is a QPACK-encoded field section, which is where this
  specification meets the previous one: the header lists the netlog carries
  are the decoded form of exactly the string literals Section 4.1.2 of RFC
  9204 describes.

  7.2.1 is DATA, and it is why a data event's interesting field is a length
  rather than a payload. 7.2.4 is SETTINGS, and it is in the fixture as an
  event the parser is expected to recognise by name and then decline to
  model -- which is the ordinary case for a netlog consumer and is asserted
  as such.

  The priority-update event in the fixture carries an opaque field value and
  an element type. The frame it records is an HTTP/3 extension defined in
  RFC 9218 rather than here, and the parser treats its value as the
  uninterpreted string it is, so nothing about this step requires that
  document.

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
