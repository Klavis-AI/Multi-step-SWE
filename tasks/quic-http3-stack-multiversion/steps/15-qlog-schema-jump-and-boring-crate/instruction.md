# Step 15 — A qlog schema jump, a crypto backend that comes from crates.io, and five smaller additions

## 1. Implement this milestone

One release, two format changes that break every consumer of the logs, a
crypto backend that stops being vendored, and five additions to the
connection and HTTP/3 surfaces.

**The qlog file schema changes, and so does every event name.** The log
format this package has written since milestone 06 was qlog as it stood in
the early drafts: a header record that named its `qlog_format` and
`qlog_version`, and event names spread over seven categories --
`connectivity:`, `security:`, `transport:`, `recovery:`, `http:`, `qpack:`
and `generic:`. The schema is now URI-identified. The header record declares
a `file_schema` of `urn:ietf:params:qlog:file:sequential`, the trace it
carries declares the event schemas a reader should expect, and the
categories collapse: the first four become `quic:`, `http:` becomes
`http3:`, and `generic:` becomes `loglevel:`.

Both halves are wire format. A tool that reads the candidate's logs
dispatches on the header to decide how to parse and on the event name to
decide what it is looking at, so a rename that stops at the Rust type names
changes nothing that matters.

**BoringSSL arrives as a dependency rather than as a submodule.** The
transport crate has carried its own copy of BoringSSL at
`quiche/deps/boringssl` since the first milestone, registered in
`.gitmodules`, compiled by the build script with `cmake`, and selected by a
`boringssl-vendored` feature. That arrangement is gone. The default feature
is now `boringssl-boring-crate`, the crypto comes from the `boring` and
`boring-sys` packages as ordinary dependencies resolved from the registry,
and the submodule, the directory and the `cmake` build dependency all
disappear with it. `boringssl-vendored` and `openssl` are no longer features
of the crate.

**Five additions.** `h3::Config::new()` stops leaving the field section
limit unset and defaults it to 32768 octets, so an endpoint that was never
configured now advertises a limit. `dgram_send_buf` and `dgram_recv_buf`
move owned buffers in and out of the DATAGRAM queues instead of copying
through a slice. `stream_readable_len` answers how much of a stream can be
read right now. `stream_closed` answers whether a stream is finished in both
the directions that apply to it. And `Config::set_curves_list` hands a TLS
group preference list to the backend.

The event names are the format. Renaming the Rust variants without renaming
the serialized strings leaves every reader working and every log unchanged,
which is the one outcome this milestone is not. The rename is also not
uniform: three namespaces collapse into `quic:`, but HTTP/3's events go to
`http3:` and the log-level events go to `loglevel:`, so a search-and-replace
that puts everything under `quic:` is as wrong as one that changes nothing.

The header record changes shape, not just content. It no longer carries
`traces`, because a sequential file has exactly one trace and carries it as
`trace`; it declares `file_schema` rather than a format-and-version pair;
and the trace declares `event_schemas`, which is what makes the collapsed
event names readable at all -- without them a reader cannot tell which
revision of the QUIC event definitions it is looking at.

The crypto backend is a build-system change with a runtime consequence. A
candidate who flips the feature in the transport crate and leaves `apps`
resolving the old vendored path still compiles, because the old feature
simply no longer exists and cargo does not error on a feature that is not
requested. The failure surfaces as two binaries that link different crypto
and cannot complete a handshake with each other.

The field section default is a default and not a constant. Code that writes
32768 into the SETTINGS frame unconditionally passes any check that only
looks at a fresh config, and breaks every application that chose its own
limit.

`stream_readable_len` counts *contiguous* bytes. Data that arrived out of
order and sits behind a gap is buffered but not readable, and counting it
would hand a caller a number that the next `stream_recv` cannot honour.

What is newly in scope here:

* `cap-qlog-file-schema` — The header record of a sequential qlog file,
  against a fixed expectation table.

  A small crate is written into scratch -- a `Cargo.toml` with its
  own empty `[workspace]` table so it is not pulled into the candidate's,
  and a path dependency on `<candidate root>/qlog` -- with a `main.rs` that
  builds a `TraceSeq` with `vec![QUIC_URI, HTTP3_URI]`, opens a
  `QlogStreamer` over a file named with `SQLOG_EXT`, starts the log, writes
  one event, finishes it, and prints the seven public constants. Building
  that crate is the first assertion: a `qlog` that does not export those
  constants, or whose `TraceSeq::new` does not take event schemas, does not
  compile against it -- and nothing has been read from the candidate's
  source to find that out.

  The probe's printed constants are compared against their fixed values. The
  file it wrote is then parsed as a JSON Text Sequence and checked
  structurally: it holds exactly two records; the header record's keys are
  exactly `description`, `file_schema`, `serialization_format`, `title` and
  `trace`; `file_schema` is `urn:ietf:params:qlog:file:sequential`;
  `serialization_format` is `JSON-SEQ`; there is no `traces` array anywhere
  in it; `trace.event_schemas` is the two-element list
  `["urn:ietf:params:qlog:events:quic-12",
  "urn:ietf:params:qlog:events:http3-12"]` in that order; and the trace
  carries no `configuration` object.

  Structural throughout, against an expectation table, and never a substring
  search over the file: the absence checks in particular are checks that a
  key is not in an object, not that a string does not occur in the bytes.
* `cap-qlog-event-namespace` — The serialized `name` of four streamed
  events, against the rename table in the interface document.

  A second probe crate, built the same way against `<candidate root>/qlog`,
  emits `QuicMtuUpdated`, `QuicKeyUpdated`, `QuicCongestionStateUpdated` and
  `Http3StreamTypeSet` through a `QlogStreamer` and writes them to a
  `.sqlog` file. The file is parsed as a JSON Text Sequence; it must hold
  the header plus exactly four event records; each record's keys must be
  exactly `data`, `name` and `time`, with `time` a JSON number; and each
  `name` must equal the expected string.

  The four are drawn from four different old categories -- `connectivity:`,
  `security:`, `recovery:` and `http:` -- so that moving one namespace and
  not the others fails. The fourth carries the whole of the second failure
  mode: it must be `http3:stream_type_set`, so a candidate that rewrote
  every name to begin with `quic:` fails here.

  The body of the first record is checked as well as its name -- `old` 1200,
  `new` 1452, `done` true -- so that a name cannot be produced by something
  that is not the event it claims to be.
* `cap-boring-crate-backend` — The transport crate's manifest as cargo
  reports it, and two paths that must not exist.

  `cargo metadata --offline --no-deps --format-version 1` is run in the
  candidate's workspace and the `quiche` entry of the `packages` array is
  read. From it: the `default` feature is exactly the one-element list
  `["boringssl-boring-crate"]`; `boringssl-vendored` and `openssl` are not
  keys of the features map at all; `boringssl-boring-crate`, `ffi` and
  `qlog` still are; `boring` and `boring-sys` each appear in `dependencies`
  with kind `null`, which is cargo's spelling for a normal dependency; and
  neither `cmake` nor `pkg-config` appears with kind `build`.

  Then the two absences: no `.gitmodules` at the workspace root, and no
  `quiche/deps/boringssl` directory.

  This is a T4 group -- the emitted artefact is the package layout as cargo
  describes it -- and it is checked as layout, never as behaviour. The
  behaviour that follows from getting it wrong is checked elsewhere: two
  binaries linking different crypto cannot complete a handshake, which is
  what the proof-of-life gate runs into.
* `cap-h3-default-field-section` — The SETTINGS frame an unconfigured
  HTTP/3 endpoint now sends, read from the other end of a real connection.

  Two in-memory HTTP/3 connections are brought up over the `h3` ALPN with
  a default `h3::Config` on both sides, settled, and polled
  until neither has an event left -- polling is what parses the peer's
  control stream, so it is what fills `peer_settings_raw()`. Each side's
  view of its peer must then contain exactly one pair whose identifier is
  `0x06`, carrying 32768. At the previous milestone the pair was absent
  entirely.

  The public constant `SETTINGS_MAX_FIELD_SECTION_SIZE_DEFAULT` is checked
  to equal 32768 beside it, because an application that wants to reason
  about the limit reaches for the constant and not for the literal.

  The second half is the control that makes this a default rather than a
  constant: a server whose `h3::Config` had
  `set_max_field_section_size(4096)` called on it must put 4096 on the wire.
  A candidate that writes 32768 unconditionally passes the first half and
  fails this one.
* `cap-dgram-buf-api` — An owned buffer handed to the DATAGRAM queue and
  taken back out of it.

  Two in-memory connections with `enable_dgram(true, 10, 10)` on both
  configs. The client calls `dgram_send_buf` with an owned 512-byte body;
  after the datagrams cross, the server's `dgram_recv_buf` returns exactly
  those bytes, a second call returns `Err(Error::Done)` because an owned
  read consumes the datagram just as the copying read does, and the server
  echoes the same buffer back with `dgram_send_buf` so the direction is
  proven both ways.

  Then the three exact answers. A buffer one byte longer than
  `dgram_max_writable_len()` is `Err(Error::BufferTooShort)`; a buffer of
  exactly that length is `Ok(())`, so the refusal is a boundary rather than
  a blanket rejection; and on a second pair whose configs never enabled the
  extension, `dgram_send_buf` is `Err(Error::InvalidState)` and
  `dgram_recv_buf` is `Err(Error::Done)`.
* `cap-stream-readable-len` — How much of a stream can be read right now,
  including the case where the answer is zero and the receive buffer is not
  empty.

  The case does not use the ordinary exchange helper, because
  that one delivers everything in order. It writes 1000 bytes to stream 4,
  takes the datagrams the client produced without delivering them, writes
  800 more, takes those, and then hands the *second* batch to the server
  first. With the front of the stream missing, `stream_readable_len(4,
  usize::MAX)` must be 0 and `stream_readable(4)` must be false, even though
  800 bytes are buffered. An implementation that sums the receive buffer
  reports 800 here and passes everything else in the group.

  Delivering the withheld batch closes the gap, and the count becomes 1800.
  `stream_readable_len(4, 100)` is then 100 -- the ceiling is a ceiling, not
  a hint, and it cuts inside the first buffered chunk -- and
  `stream_readable_len(4, 0)` is 0. A 600-byte `stream_recv` returns the
  first 600 bytes of the stream in order and leaves the count at 1200. A
  stream that was never opened is 0 rather than an error.
* `cap-stream-closed` — Whether a stream is finished, in every direction
  that applies to it.

  One bidirectional stream walked through both halves of its finish. After
  the client sends 256 bytes without a fin, neither end reports stream 4
  closed. After the client sends its fin and the server reads it, still
  neither does -- a bidirectional stream needs both directions, and
  `stream_finished` at the client is still false because it reports only the
  receiving half. After the server replies with a fin and the client reads
  it, both ends report closed, which is also true if the stream has been
  collected by then.

  Then the two edges. A stream nobody ever opened reports false, which is
  what distinguishes it from one that finished and was collected. And on
  locally opened unidirectional streams, where the peer's direction does not
  exist and must not be waited for: stream 2, written with a fin, is closed;
  stream 6, written without one, is not.
* `cap-curves-list` — That the TLS group preference list reaches the
  handshake.

  The return value alone proves nothing -- a setter that took the string and
  did nothing would also return `Ok` -- so the observation is a pair of
  configurations with no group in common. A client offering only `P-256` and
  a server accepting only `X25519` must not complete a handshake: the case
  drives the datagrams between them itself, leniently, because a handshake
  that is meant to fail produces errors on both sides and those errors are
  the subject rather than a fault, and then asserts that neither end reached
  the established state.

  Beside it, the control: a client offering `P-384:P-256` and a server
  accepting `P-256` do handshake, and then carry 256 bytes on a stream with
  a fin. Without it, a candidate whose connections never handshake at all
  would pass the first assertion.

  The exact-answer negative is the argument itself: `set_curves_list` with a
  name the backend does not recognise is `Err(Error::TlsFail)`.

`gate-endtoend` carries no capability of its own: it drives this step's
binaries end to end over loopback, and every gate group must pass. A stack
that implements every capability listed above and cannot complete one real
connection does not satisfy this step at all, however much of the new
capability it holds. The group table in `TEST_INTERFACE.md` is the contract
for it, as it is for the rest.

What must still hold: everything step 14 left working. 3 of this step's
groups re-run earlier milestones unchanged, listed by name in
`/app/WORKSPACE_HANDOFF.md`. They are gate groups as well, so any one of
them failing means the step is not satisfied. They are there because this
step's change plausibly breaks them: a rewrite that gets the new behaviour
right and drops an old one does not satisfy the step at all.

## 2. Read these local references

**Normative semantics:**

* `/app/spec/rfc9114.txt` — 4.2.1 (field compression), 4.2.2 (header size
  constraints), 7.2.4 (SETTINGS), 7.2.4.1 (defined SETTINGS parameters).
  7.2.4.1 is where the number this step starts advertising is defined.
  SETTINGS_MAX_FIELD_SECTION_SIZE is identifier `0x06`, and the section
  states both halves of what it means: it is the size of the *decoded* field
  section an endpoint is willing to accept, and the default when the setting
  is absent is unlimited. Those two sentences are the whole of the change.
  Until this milestone the setting was absent and the peer was told nothing;
  now it is present and the peer is told 32768.

  4.2.2 is the size the setting measures, and it is worth reading because
  the number is not the number of bytes on the wire. A field section's size
  is the sum, over its fields, of the name length plus the value length plus
  32 -- an overhead per field that exists so the limit bounds the receiver's
  memory rather than its input. An implementation that compared the limit
  against the encoded HEADERS payload would be measuring a different
  quantity.

  4.2.1 is why those two are different quantities: the field section is
  QPACK compressed, so the encoded form is smaller than what it decodes to,
  and by an amount the sender chooses. Read it for the direction of the
  inequality.

  7.2.4 is the frame the value travels in, and the two rules that make the
  the case's reading of it sound: SETTINGS is sent once, first, on each
  endpoint's control stream, and a receiver must tolerate identifiers it
  does not recognise. The second is why the case looks for its identifier in
  the list rather than insisting the list is exactly one pair long.
* `/app/spec/rfc9221.txt` — 3 (transport parameter), 4 (datagram frame
  types), 5 (behavior and usage), 5.3 (flow control). Section 3 is the
  negotiation the `InvalidState` answer reports. DATAGRAM support is
  advertised by a transport parameter, and an endpoint must not send a
  DATAGRAM frame to a peer that did not send it. So a send on a connection
  whose peer never advertised the extension is not a failure to queue
  something -- it is a frame the endpoint is forbidden to write, which is
  why the answer is a state error rather than a capacity error.

  Section 4 is the frame, and the reason the length limit is hard. A
  DATAGRAM frame's payload must fit in a single QUIC packet: the frame is
  not fragmented and there is no mechanism to reassemble one. That is why a
  buffer one byte too long is refused outright rather than split, and why
  the accessor that reports the writable length is the boundary the case
  tests on both sides.

  Section 5 is the delivery contract that makes an owned-buffer API
  meaningful at all. DATAGRAMs are unreliable and unordered, they are not
  retransmitted on loss, and they are not stream data -- the last of which
  the regression case checks directly by asserting that a received DATAGRAM
  opens no stream. 5.3 is the sentence that they are not flow controlled,
  which is why the send queue's bound is the implementation's own and not
  the peer's.
* `/app/spec/rfc9000.txt` — 2.2 (sending and receiving data), 2.4
  (operations on streams), 3.1 (sending stream states), 3.2 (receiving
  stream states), 3.4 (bidirectional stream states), 4.1 (data flow
  control). 3.2 is the definition `stream_readable_len` has to respect. A
  receiving stream holds data that arrived, at the offsets it arrived at,
  and the section is explicit that data can arrive out of order: the
  receiver buffers it and delivers it to the application in order. So there
  are two quantities -- what has arrived and what can be read -- and they
  are equal only when nothing is missing. Read it before deciding what the
  count counts.

  2.2 is the same distinction from the application's side, and it carries
  the sentence the accessor exists to serve: an endpoint must not deliver
  stream data out of order. 2.4 is the operations an application is offered
  on a stream, and it is the section that frames a read as "how much is
  available" followed by "take it", which is the pair the new accessor
  completes.

  3.1, 3.2 and 3.4 together are `stream_closed`. 3.1 is the sending half's
  state machine, ending in the states reached after the final frame is sent
  and acknowledged; 3.2 is the receiving half's, ending after the final
  offset is known and read; 3.4 composes the two and states plainly that a
  bidirectional stream is closed only when both halves are. Read 3.4 for the
  asymmetry a unidirectional stream has: only one half exists, so only one
  half can be waited for, and waiting for the other would be waiting
  forever.

  4.1 is the reason the cases can send what they send. Flow control
  is a credit the receiver grants per stream and per connection, and a
  sender that runs out queues rather than fails. The cases stay well inside
  the configured limits so that a short write is never mistaken
  for the behaviour being observed.
* `/app/spec/rfc9001.txt` — 4.1 (interface to TLS), 4.7
  (HelloRetryRequest), 4.8 (TLS errors). 4.1 is the boundary
  `set_curves_list` reaches across. QUIC does not implement the handshake;
  it hands CRYPTO stream bytes to TLS and takes keys back, and the key
  exchange -- which group, with which share -- is entirely TLS's business.
  That is why the setter is a pass-through to the TLS library rather than
  something the transport interprets, and why its error is a TLS error.

  4.7 is the first thing that can happen when the two sides' group
  preferences do not line up, and it is the case the group
  deliberately does not test: if the client supports a group the server
  wants but did not send a share for, the server asks for one and the
  handshake continues, one round trip longer. Read it so that "no group in
  common" is understood as a different situation from "no share in common",
  and so that the configuration the case uses -- disjoint preference lists,
  not merely a missing share -- is understood to be the one that cannot be
  recovered.

  4.8 is what happens then. A TLS alert becomes a QUIC connection error in
  the CRYPTO_ERROR range, and the handshake ends. Read it for the shape of
  the failure the case observes: neither endpoint reaches the
  established state, and the failure is a connection error rather than a
  hang.

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
