# Step 12 — An HTTP/3 console debugger, a live idle timeout, and unknown transport parameters

## 1. Implement this milestone

One new tool, one new place to set an old value, one new thing the transport
remembers about its peer, and two additions to the HTTP/3 layer.

**A console debugger for HTTP/3.** A new workspace member, `h3i`, is a
client built for taking an HTTP/3 server apart rather than for fetching from
it. It opens a QUIC connection, sends exactly the frames it is told to send
-- including frames a conforming client would never send -- and reports
everything that comes back. It has two modes. Interactively it prompts for
frames. Non-interactively it is given a qlog file of *actions* with
`--qlog-input` and replays them in order: an `http:frame_created` event
carrying a `headers` frame becomes a real HEADERS frame on the stream that
event names, and `"fin_stream": true` closes the stream after it. A run logs
each frame it sends and each frame it receives, and ends by summarising the
connection.

`h3i` is a workspace member and a binary, not a library the transport
depends on: the transport crate does not change because it exists.

**The idle timeout becomes settable on a live connection.** `Config` has
carried `set_max_idle_timeout` since the first milestone, and it still does.
What is new is the same method on `Connection`. It writes the local
transport parameter and then re-encodes the parameter block, so a connection
created from a `Config` that said one thing can be made to advertise another
-- provided it is done immediately after creation, before any packet has
been sent or received. It returns `Result<()>` rather than nothing, because
the re-encoding can fail.

**Unknown transport parameters can be kept instead of discarded.** A QUIC
endpoint must ignore transport parameters it does not recognise, and until
now this stack forgot them the moment it skipped them.
`Config::enable_track_unknown_transport_parameters` turns on a bounded
record: the argument is a number of *bytes*, not a number of parameters, and
each entry charges its value's length plus eight for the identifier. When
the budget will not take another entry the parameter is dropped silently --
failing to remember an unknown parameter is not a connection error. The
record is reachable through the decoded peer parameters, and it is present,
empty, and sized as configured even when the peer sent nothing unrecognised:
the decision to track is the configuration's, not the peer's.

Two public types come with it. One is a single parameter -- an identifier
and the bytes that followed it -- which can say whether its identifier lies
in the reserved space RFC 9000 Section 18.1 sets aside for parameters that
must always be ignored. The other is the bounded record itself, which anyone
may build and push into, and which refuses an entry that does not strictly
fit.

**HTTP/3 grows two things.** A response may now be preceded by informational
(1xx) header sections and followed by a trailer section, both sent through
one new method that takes a flag saying which it is. The rules are
asymmetric and are part of the contract: a server may send either, a client
may send only trailers, and neither may send anything before the stream's
initial header section has gone out. Separately, an HTTP/3 connection can
report how many bytes it has received on the peer's two QPACK streams.

**Nothing is removed and nothing moves.** Every name the previous milestone
exported is still exported, in the same module, with the same signature. The
one structural change is the new workspace member, and it is checked by
running its binary rather than by looking for its directory.

`set_max_idle_timeout` stops being a `Config`-only name. Both methods exist
from this step on and they are not interchangeable: the one on `Config`
shapes every connection the config makes and returns nothing; the one on
`Connection` changes that connection alone, must be called before its first
packet, and returns `Result<()>`. A step that had only ever seen the first
now has to keep them apart, and a candidate that implements the new one by
delegating to the old one will advertise the old value, because the old one
does not re-encode.

The peer's transport parameters gain a field. Anything that constructs a
`TransportParams` by naming every field will not compile until it names the
new one, and its correct value when tracking is off is `None` rather than an
empty record -- the difference between "not asked for" and "asked for and
nothing arrived" is observable and is the point of the field.

What is newly in scope here:

* `cap-h3i-replay` — Three of the candidate's own processes' worth of work
  in one: its `quiche-server` is started on a loopback port with `--listen`,
  `--cert`, `--key`, `--root`, `--no-retry` and `--idle-timeout 6000`, and
  with `QLOGDIR` pointing
  at an empty scratch directory; its `h3i` binary is given a two-record
  JSON-SEQ action file and told to replay it at that port; and the
  **server's** qlog is read to see what arrived.

  The action file is supplied ready-made, not written by the candidate, and is
  the shape the interface document fixes: a `0x1E`-prefixed header record
  naming `qlog_version 0.3`, `qlog_format JSON-SEQ` and a client vantage
  point, then a `0x1E`-prefixed `http:frame_created` record carrying a
  `headers` frame on stream 0 with `:method GET`, `:scheme https`,
  `:authority`, a `:path` naming a file the docroot contains, and
  `"fin_stream": true`.

  `h3i` is invoked as `h3i --qlog-input <file> --no-verify --connect-to
  127.0.0.1:<port> --idle-timeout <ms> <authority>:<port>`, and must exit 0
  within the group's deadline. Exiting 0 is the first half of the assertion
  and is not sufficient on its own: a binary that parsed its arguments and
  connected to nothing would also exit 0, which is why the second half reads
  the server.

  What the server's `.sqlog` must show, matched as qlog event names and
  their payloads rather than as log prose: the HEADERS frame arriving on
  stream 0 with the `:path` the action file named, and a response going back
  on the same stream. Assertions are on the server's qlog because that is an
  emitted artefact with a schema; `h3i`'s own stderr is a `log`-crate format
  that nothing in this step declares, and a group that grepped it would be
  testing a logging convention.

  Three failure modes were seen while proving this shape and each is
  asserted against rather than assumed away: `h3i` absent from the workspace
  member list, so the binary is never built (the group then fails on the
  missing binary, and says so); `--qlog-input` pointed at a file that does
  not exist, which the reference answers with a panic rather than a
  diagnostic, so the file is written before the process starts and its path
  is absolute; and the authority in the replayed headers not matching what
  the server will serve, which `--replay-host-override` exists to fix and
  which is avoided by writing the same authority into both.

  Not asserted, and each for a reason rather than by omission:

  * the interactive prompt mode, the connection summary's wording, the `h3i`
  qlog *output* file, and any frame the action file did not ask for; *
  **`--replay-host-override` itself.** The paragraph above explains what it
  is for; the problem is sidestepped instead, by writing the same
  authority into the replayed headers and into what the server serves. The
  flag must exist and take an argument -- a candidate that omits it from the
  usage string has not built this milestone's `h3i` -- but no group drives
  it, and that is a choice, not an oversight; * **the default value of
  `--idle-timeout`.** It is 5000 ms, and the contract above says so because
  a candidate has to pick a number. Every invocation in this package passes
  the flag explicitly (2000 ms), so the default is never the value in play.
  Proving it would mean running one replay with the flag omitted and waiting
  for the connection to idle out on its own -- five seconds of wall clock,
  per run, to pin a number nothing else depends on. Documented, not
  asserted.
* `cap-live-idle-timeout` — Two in-memory `connect`/`accept` pairs that
  differ in exactly one line, read from the side that can actually see the
  answer.

  Both configurations set `Config::set_max_idle_timeout(30_000)` on **both**
  ends. In the first pair, and only there, the client is then told
  `set_max_idle_timeout(7_000)` on the **connection**, immediately after
  `quiche::connect` and before a single datagram has been produced. Both
  pairs then handshake normally.

  Measured against the reference:

  | | server's view of the client | client's view of the server | | --- |
  --- | --- | | live call made | **7000** | 30000 | | no live call | 30000 |
  30000 |

  Three separate things are being pinned by those four numbers. That the
  server sees 7000 is the feature. That the *client's* view of the *server*
  stays 30000 in the same pair is what stops a candidate from having changed
  a shared default. And that the baseline pair reports 30000 on the same
  code path is what stops the first row from being satisfied by a connection
  that ignores its config.

  The value is read through `Connection::peer_transport_params()`, which the
  previous milestone already provided -- so the group is about the new
  setter and not about a new way of looking.

  The return type is load-bearing and is exercised, not merely mentioned:
  the call site binds the result and propagates it, so a candidate whose
  method returns `()` does not compile. It is not required to fail, and no
  case provokes the error path.

  Deliberately not asserted: that a call made *after* the first packet is
  rejected. The documentation says "must only be called immediately after
  creating a connection", and the reference enforces nothing -- it writes
  the field and re-encodes whenever it is asked. Requiring a rejection would
  require the candidate to be stricter than the reference. Deliberately not
  asserted either: anything about a connection actually timing out. Idle
  timeout expiry is a wall-clock observation and has no place in a case that
  must be deterministic.
* `cap-unknown-tp-tracking-enabled` — One handshake, both answers. The
  **server's** config calls
  `enable_track_unknown_transport_parameters(128)`; the **client's** does
  not. Everything else is the pair's default. After an ordinary handshake,
  read through `Connection::peer_transport_params()`:

  * the server's view of the client is `Some(u)` with `u.capacity == 128`
  and `u.parameters` **empty**; * the client's view of the server is `None`.

  Measured against the reference, exactly: `Some(UnknownTransportParameters
  { capacity: 128, parameters: [] })` and `None`.

  That is the whole group, and it is deliberately small. The record is
  allocated while the peer's parameter block is being decoded, from the
  configuration alone, before any parameter has been examined -- so its
  existence and its declared capacity are observable even though this
  ladder's peer sends nothing unrecognised. `None` and an empty `Some` are
  different answers to different questions, and a candidate that returns
  `Default::default()` for both, or `Some(empty)` for both, fails one half
  or the other.

  A second capacity is not tested here and does not need to be: the
  arithmetic belongs to `cap-unknown-tp-capacity`, where it is exercised
  directly on the public type instead of through a handshake.

  Not observed anywhere in this step: a tracked parameter that actually
  arrived. Both endpoints of the pair are the candidate, the candidate emits
  no unrecognised transport parameter, and the independent support code used
  here has no TLS and so cannot complete a handshake to send one.
  The narrowing is
  real and is recorded rather than papered over with a group that
  hand-builds a `TransportParams` and asserts on its own struct literal.
* `cap-unknown-tp-capacity` — The public accounting type, driven directly.
  No connection, no handshake, nothing timed: `UnknownTransportParameters {
  capacity: 20, parameters: vec![] }` is built directly -- both
  fields are public and the type derives `Default` -- and then pushed into,
  in this order. Measured against the reference:

  | push | charge | admitted? | capacity after | entries after | | --- | ---
  | --- | --- | --- | | `{ id: 31, value: b"abcd" }` | 4 + 8 = 12 | yes, `12
  < 20` | 8 | 1 | | `{ id: 62, value: b"abcd" }` | 12 | **no**, `12 < 8` is
  false | 8 | 1 | | `{ id: 63, value: b"" }` | 0 + 8 = 8 | **no**, `8 < 8`
  is false | 8 | 1 |

  Four claims, and the third row is the one that matters most. The admission
  test is a **strict** `<`, so an entry whose charge exactly equals the
  remaining capacity is refused; a candidate that wrote `<=` passes the
  first two rows and fails the third. The eight bytes charged for the
  identifier are fixed and are charged even for an empty value, which is
  what the third row also shows.

  Both refusals return `Err` -- `BufferTooShort` -- and both leave
  `capacity` and `parameters` exactly as they were. A silent drop at this
  API would be a different contract from the quiet drop the protocol layer
  performs, and the group asserts the state after each refusal rather than
  only the return value.

  The surviving entry is then read back through the `IntoIterator` on
  `&UnknownTransportParameters`, yielding one item whose `id` is 31 and
  whose `value` is `b"abcd"` -- so a candidate that accounted correctly but
  stored nothing, or stored a borrowed slice it could not return, fails
  here.
* `cap-tp-reserved-ids` — `UnknownTransportParameter::is_reserved` over a
  fixed table, and nothing else. RFC 9000 Section 18.1 reserves the
  identifiers `31 * N + 27` for parameters that have no meaning and must
  always be ignored; the predicate is true for exactly those.

  The table, measured against the reference:

  | id | 27 | 28 | 31 | 57 | 58 | 62 | 88 | 89 | 90 | 119 | 120 | 121 | 1000
  | 7963 | | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | ---
  | --- | --- | --- | --- | | reserved | Y | n | n | n | Y | n | n | Y | n |
  n | Y | n | n | Y |

  Five true, nine false, the true ones being 27, 58, 89, 120 and 7963 -- the
  first four members of the family and one well above where an off-by-one in
  a loop would still agree -- and the false ones chosen to sit immediately
  on either side of a true one (28, 57, 88, 90, 119, 121) so that a
  predicate off by one in either direction fails.

  **Identifiers below 27 are deliberately absent, and this is not an
  oversight.** The reference computes `(id - 27) / 31` on a `u64`, which
  underflows for every identifier under 27 and panics outright in the debug
  profile the tree is built in -- `id: 0` was measured taking the whole probe
  down at `quiche/src/lib.rs:8085`. A candidate writing the obvious
  non-underflowing form, `id >= 27 && (id - 27) % 31 == 0`, answers `false`
  there instead. RFC 9000 Section 18.1 defines the reserved set only upward
  from 27 and says nothing about what lies below, so both readings are
  defensible and neither is this milestone's capability. Checking the
  difference would test an arithmetic accident of the reference rather than
  the feature; the table stops at 27 and the two forms agree on every row of
  it.

  This is a pure predicate over a public type, peripheral to the tracking
  machinery that `cap-unknown-tp-tracking-enabled` and
  `cap-unknown-tp-capacity` carry.
* `cap-h3-informational` — An HTTP/3 exchange over an in-memory pair in
  which the response arrives in two header sections, and a client that tries
  the same thing is refused.

  The server answers a GET with `send_response(103, [link: </s.css>],
  fin=false)` -- the informational section, which is the stream's *initial*
  header section and so goes out through the ordinary call -- and then
  `send_additional_headers(&[:status 200, content-length: 5],
  is_trailer_section: false, fin: false)` for the final response, followed
  by a five-byte body. The client polls and must see, in this order and with
  nothing between them:

  1. `Event::Headers` whose list contains `:status` `103`, `more_frames`
  true; 2. `Event::Headers` whose list contains `:status` `200`,
  `more_frames` true; 3. `Event::Data`, carrying the five bytes; 4.
  `Event::Finished`.

  Both header sections are asserted by content, not merely counted: a
  candidate that delivered the same section twice, or coalesced the two into
  one, fails on the `:status` values.

  The refusal leg is the other half and is checked in the same case. Before
  any of the above, the **client** calls `send_additional_headers` on its
  own request stream with `is_trailer_section: false`. It must return
  `FrameUnexpected`: informational responses are a response construct, a
  request cannot contain one, and the reference rejects it before examining
  the stream at all. Measured: `Err(FrameUnexpected)`, on a stream that had
  already had its initial HEADERS sent -- so the refusal is about the
  direction, not about the state.

  A server calling `send_additional_headers` on a stream whose initial
  header section has not been sent is also refused, and that leg belongs to
  `cap-h3-trailers`, which is where the stream-state rules are gathered.

  Nothing is asserted about what the 1xx status code *means*, about
  `Retry-After`, or about Early Hints semantics. The contract is positional:
  two header sections before the body, both delivered, in order.
* `cap-h3-trailers` — A trailer section after a body, and the three ways
  the same method must refuse.

  The positive leg: the server sends `send_response(200, fin=false)`, then a
  body with `fin=false`, then `send_additional_headers(&[x-checksum:
  deadbeef], is_trailer_section: true, fin: true)`. The client polls and
  must see `Event::Headers` (the 200), `Event::Data` (the body, byte for
  byte), `Event::Headers` again -- carrying `x-checksum` with the value the
  server sent, and with `more_frames` **false**, because the trailer section
  closed the stream -- and then `Event::Finished`. The transition of
  `more_frames` from true on the first section to false on the trailer is
  asserted: it is how a receiver knows the trailer was a trailer.

  Three refusals, each `FrameUnexpected`, each measured against the
  reference:

  * **a second trailer section on the same stream.** Once a trailer section
  has gone out the stream is finished with header sections; the repeat call
  returns `FrameUnexpected` rather than writing a second HEADERS frame. *
  **before the initial header section.** A server calling
  `send_additional_headers` on a stream on which it has not yet called
  `send_response` returns `FrameUnexpected`. This is the leg that is about
  stream state rather than direction, which is why it sits here rather than
  in `cap-h3-informational`. * **an unknown stream id.** The same error, not
  a panic and not a different error: a stream the connection has never seen
  is refused the same way as one in the wrong state.

  A client **is** allowed to send trailers, and that asymmetry is the reason
  this group and `cap-h3-informational` exist separately:
  `is_trailer_section: true` is the one combination a client may use, and
  `is_trailer_section: false` is the one it may not.
* `cap-h3-stats` — The two QPACK byte counters, driven by bytes
  written by hand onto the candidate's own QPACK streams -- because an
  ordinary request and response leaves them both at zero and a group that
  read them after a GET would assert `0 == 0` and be satisfied by a
  `Default::default()`.

  **Finding the streams (T2, no assumption).** The client's 1-RTT datagrams
  are decrypted with keys derived from `CLIENT_TRAFFIC_SECRET_0` out of its
  own keylog, their STREAM frames are decoded, and every client-initiated
  unidirectional stream (`stream_id % 4 == 2`) carrying `offset == 0` is
  read for its HTTP/3 stream type byte. The one whose type is `0x02` is the
  QPACK encoder stream and the one whose type is `0x03` is the QPACK decoder
  stream. Measured against the reference: control `2`, encoder `6`, decoder
  `10`, and a GREASE stream `14`. The group reads the type byte rather than
  hard-coding the ids, so a candidate that opens its unidirectional streams
  in a different order still passes -- what it may not do is fail to open
  them.

  **Driving the counters (T3).** Twenty bytes are written on the encoder
  stream and twelve on the decoder stream through the client's transport
  connection, the pair settles, and the server's HTTP/3 connection is polled
  until it reports `Done`. Then:

  | | encoder | decoder | | --- | --- | --- | | `server.stats()` | **20** |
  **12** | | `client.stats()` | 0 | 0 |

  Five more bytes on the encoder stream, settle and poll again, and the
  server reports **25** and **12** -- cumulative on the one that moved,
  untouched on the one that did not.

  The client's own counters staying at zero throughout is what makes the
  reading per-connection and per-direction rather than a connection-wide
  total; the second write is what makes it cumulative rather than a
  high-water mark or a last-read size. Both endpoints must still be open
  afterwards: QPACK instructions on a stack with no dynamic table are
  discarded, and discarding them must not close a critical stream.

  Exact byte counts are safe to assert here precisely because they were
  chosen by hand. Nothing is asserted about bytes the *candidate's* encoder
  wrote, which is an encoder policy and not this step's contract.

`gate-endtoend` carries no capability of its own: it drives this step's
binaries end to end over loopback, and every gate group must pass. A stack
that implements every capability listed above and cannot complete one real
connection does not satisfy this step at all, however much of the new
capability it holds. The group table in `TEST_INTERFACE.md` is the contract
for it, as it is for the rest.

What must still hold: everything step 11 left working. 3 of this step's
groups re-run earlier milestones unchanged, listed by name in
`/app/WORKSPACE_HANDOFF.md`. They are gate groups as well, so any one of them
failing means the step is not satisfied. They are there because this step's
change plausibly breaks them: a rewrite that gets the new behaviour right
and drops an old one does not satisfy the step at all.

## 2. Read these local references

**Normative semantics:**

* `/app/spec/rfc9000.txt` — 18 (transport parameter encoding), 18.1
  (reserved transport parameters), 18.2 (transport parameter definitions),
  7.4 (transport parameters), 7.4.1 (values of transport parameters for
  0-RTT), 10.1 (idle timeout). Section 18 is the encoding this step both
  re-runs and remembers: identifier, length, value, repeated, with the rule
  that **an endpoint MUST ignore transport parameters it does not
  understand**. "Ignore" is what the previous milestones did literally; this
  one keeps a bounded copy of what it is ignoring, which the section permits
  and does not describe.

  18.1 is the reserved space and the arithmetic behind `is_reserved`:
  identifiers of the form `31 * N + 27` are reserved to exercise exactly the
  requirement above and have no semantics ever. A candidate that treats them
  as an error rather than as something to ignore is wrong at the protocol
  level, not merely at this API.

  18.2 is the catalogue the decoder recognises, and therefore the
  definition, by subtraction, of which identifiers are "unknown".

  7.4 is why re-encoding matters. Transport parameters are carried in the
  TLS handshake and are fixed for the life of the connection from the peer's
  point of view -- there is no mechanism to update them later. That is the
  whole reason `Connection::set_max_idle_timeout` is documented as
  "immediately after creating a connection, before any packet is sent or
  received", and the whole reason it has to re-serialise rather than merely
  store.

  10.1 is what the value means once it is there: both endpoints advertise
  one, the effective timeout is the minimum of the two non-zero values, and
  zero means no timeout. Read it for why a connection's *own* setting is
  observable at the **peer** and not by asking the connection that set it.
* `/app/spec/rfc9114.txt` — 4.1 (HTTP message framing), 4.1.1 (request
  cancellation and rejection), 4.1.2 (malformed requests and responses),
  6.2.2 (push streams), 7.2.2 (HEADERS frames). 4.1 is the whole contract of
  `send_additional_headers`. It gives the permitted shape of a message as a
  sequence of header sections: an optional run of informational responses,
  then the final header section, then the body, then an optional trailer
  section -- and it is the reason the method takes a boolean rather than
  being two methods, because both are HEADERS frames in the same position
  language.

  4.1 is also where the asymmetry comes from, and it is worth reading rather
  than taking on trust: informational (1xx) responses are a *response*
  construct, so a request cannot contain one, which is why a client calling
  the method with `is_trailer_section == false` is refused before anything
  else is examined. Trailers belong to both directions.

  7.2.2 is the frame itself: nothing distinguishes an informational, final
  or trailing header section on the wire, so the receiver's ability to tell
  them apart is positional, which is why the case reads them in order.
* `/app/spec/rfc9204.txt` — 4.2 (encoder and decoder streams), 4.3
  (encoder instructions), 4.4 (decoder instructions). The two counters
  `h3::Stats` reports are byte counts on the streams 4.2 defines, so read it
  for what those streams are and when they are opened -- a unidirectional
  stream each way, identified by a leading stream type, at most one of each
  per connection per direction. 4.3 and 4.4 are what travels on them. Note
  what the counters do **not** include: the stream type byte that opens each
  stream is not counted, so an ordinary request and response -- whose field
  lines are all static-table entries, and which owes no decoder instruction
  -- leaves both counters at zero. The group drives them by writing bytes
  onto the candidate's own QPACK streams rather than by reading them after a
  GET.

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
