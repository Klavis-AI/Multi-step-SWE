# Step 7 — The octets crate, HTTP/3 PRIORITY_UPDATE, and HTTP/3 qlogging

## 1. Implement this milestone

The workspace step 06 created gains a fourth member. The byte reader/writer
that has sat in `quiche/src/octets.rs` since the first milestone moves out
of the transport crate entirely and becomes `octets`, a standalone package
with its own manifest, its own version and no dependencies at all, which
`quiche` then consumes through a path dependency like any other crate. Every
codec call site in the transport changes its import; the types and their
behaviour do not.

On top of that, HTTP/3 learns the extensible-priorities signalling channel.
`PRIORITY_UPDATE` (frame type `0xF0700` for request streams, `0xF0701` for
push streams) is parsed off the peer's control stream, validated, and
surfaced to the application as `h3::Event::PriorityUpdate`, with the field
value picked up out of band through `take_last_priority_update`. The event
is edge-triggered and the value is last-write-wins, which is the whole
point: a client that re-prioritises a stream forty times does not hand the
server forty events to process.

HTTP/3 also starts qlogging. Where step 06's qlog crate only ever saw
transport events, this milestone emits `http:*` events beside them —
`stream_type_set` for each control and QPACK stream as it is opened or
discovered, and `frame_created` / `frame_parsed` for every HTTP/3 frame in
both directions — against qlog's own event-draft bump from 0.6 to 0.7.

Underneath, congestion control finishes the trait it started: the per-packet
`on_packet_acked` hook becomes the batched `on_packets_acked`,
`congestion_event` is told how many bytes were lost, HyStart++ moves to
draft-04, and the sender exposes its burst budget as
`Connection::send_quantum()`. Two smaller shape changes ride along:
`connect`/`accept` stop wrapping the connection in `Pin<Box<...>>` and hand
back a plain `Connection`, and `peer_cert()` and `session()` stop cloning
into a `Vec<u8>` and return a borrow of what the connection already holds.

Three things stop compiling at this step and none of them can be worked
around by keeping the old spelling beside the new one.

**The byte codec leaves the crate.**

```rust
// before (0.11.0)                    // after (0.14.0)
use crate::octets;                    use octets;      // a separate package
crate::octets::Octets<'_>             octets::Octets<'_>
crate::octets::BufferTooShortError    octets::BufferTooShortError
```

`quiche/src/octets.rs` is deleted, `octets/` is a workspace member beside
`quiche/`, `apps/` and `qlog/`, and `quiche/Cargo.toml` declares `octets = {
version = "0.1", path = "../octets" }`. A tree that keeps the module inside
`quiche` and adds a thin re-exporting shim crate has not done this step: the
new crate has to be the definition, buildable on its own by a consumer that
has never heard of `quiche`, and it has to carry no dependencies.

**`Connection` is no longer pinned.**

```rust
// before (0.11.0)
pub fn connect(..) -> Result<std::pin::Pin<Box<Connection>>>;
pub fn accept(..)  -> Result<std::pin::Pin<Box<Connection>>>;
// after (0.14.0)
pub fn connect(..) -> Result<Connection>;
pub fn accept(..)  -> Result<Connection>;
```

Every caller that stored `Pin<Box<Connection>>`, and every `h3` call that
took one, now takes `&mut Connection` directly. There is no `Pin` anywhere
in the public transport API.

**Two accessors stop copying.**

```rust
// before (0.11.0)                       // after (0.14.0)
fn peer_cert(&self) -> Option<Vec<u8>>   fn peer_cert(&self) -> Option<&[u8]>
fn session(&self)   -> Option<Vec<u8>>   fn session(&self)   -> Option<&[u8]>
```

The returned slice borrows the connection, so two `peer_cert()` results can
be compared against each other while both are alive, and neither call may
allocate a fresh buffer per invocation.

The HTTP/3 half of the flip is additive rather than breaking: `h3::Event`
grows a `PriorityUpdate` variant, so an exhaustive `match` over `h3::Event`
written against step 06 no longer compiles. It is a bare unit variant, not a
struct-like one: the prioritized element id does not live on the variant at
all. It is the first member of the `(id, event)` tuple `poll()` already
returns for every event, the same place `Headers`'s and `Reset`'s stream ids
come from.

What is newly in scope here:

* `cap-octets-crate` — `octets` is a real, separately buildable package in
  the workspace. `<root>/octets/Cargo.toml` exists and names the package
  `octets`; `cargo metadata --offline --no-deps` at the workspace root
  succeeds and lists `octets` among `workspace_members`; and a crate the
  checker writes into its own scratch directory, declaring `octets = { path =
  "<root>/octets" }` as its only dependency and detached from the workspace,
  compiles offline against it. The lockfile that build produces must name
  exactly two packages — `octets` and the checker's probe — which is what
  fails a tree that kept the module inside `quiche` and made `octets` a
  facade that pulls the transport crate back in.
* `cap-octets-api` — The moved crate is the same codec it was inside
  `quiche`. The probe program from the previous group is given a body that
  exercises the public surface and prints its results, and the checker
  asserts the whole of its stdout byte for byte: the four RFC 9000 Appendix
  A variants (`37`, `15293`, `494878333`, `151288809941952652`) round trip
  through `put_varint`/`get_varint` at their documented 1, 2, 4 and 8 byte
  lengths; `put_u8`/`u16`/`u24`/`u32`/`u64` and `put_bytes` read back
  through the matching getters; `varint_len` and `varint_parse_len` agree
  with the encoding; and a getter aimed past the end of its buffer, and a
  `skip` past the end, both return `Err(BufferTooShortError)` whose
  `Display` is `BufferTooShortError`, rather than panicking. A crate that
  exports the names but not the RFC 9000 encoding fails on the first varint.

  The same probe also exercises the secondary surface the two structs carry
  beside the getters and putters above: `peek_u8` reads without moving the
  cursor, where `get_u8` moves it; `get_bytes_with_u8_length`, `_u16_length`
  and `_varint_length` read a length-prefixed run whose prefix width the
  name says; `slice`/`slice_last` hand back a sub-slice of, and from the end
  of, the underlying buffer without consuming it; `is_empty` answers about
  the *whole* buffer's length, not the cursor's position, so it stays
  `false` after every byte has been read and is only ever `true` on a buffer
  that started at length zero; `to_vec` copies from the current offset
  onward; `OctetsMut::split_at` divides one buffer into two independent ones
  at a byte offset; and `put_varint_with_len` forces an encoding wider than
  the value needs, tagged with the length's own top-bits prefix. Last, and
  the one behavioural rather than signature fact in this group: a call that
  returns `Err(BufferTooShortError)` never moves the cursor first, so a
  short read can be retried against more bytes without having silently
  consumed any of the ones it failed to read.
* `cap-priority-update` — The receive path for `PRIORITY_UPDATE`, driven
  over an in-memory HTTP/3 pair using only the candidate's public API. The
  checker writes a well-formed `0xF0700` frame onto its own control stream
  with `stream_send` and steps the server through five stages: the first
  update for request stream 0 raises exactly one `(0,
  Event::PriorityUpdate)` — a bare unit variant, the `0` being the tuple's
  first member, not a field on the variant; a second update that arrives
  before the value is taken raises no further event but replaces the stored
  value; `take_last_priority_update(0)` yields the **second** field value
  and a repeat call yields `Err(Done)`; a third update after the take arms
  the event again and carries its own value; and an update for element 4 is
  tracked separately from element 0. Last-write-wins and edge-triggering are
  both load bearing — a server that queues one event per frame, or that
  returns the first value rather than the last, fails.
* `cap-priority-update-guards` — The three ways a `PRIORITY_UPDATE` is
  invalid, each on its own fresh pair so that one closed connection cannot
  mask the next. A `0xF0700` frame sent to a **client** is
  `Err(h3::Error::FrameUnexpected)` and the client closes with application
  error `0x105`, which the peer then reports through `peer_error()`. A
  prioritized element id of 2 — a unidirectional stream id, not `% 4 == 0` —
  is `FrameUnexpected` and `0x105` again. A prioritized element id of 4000,
  past the hundred bidirectional streams the peer was granted, is
  `Err(h3::Error::IdError)` and application error `0x108`. The wire error
  code is read off `local_error()` rather than from the Rust variant, so a
  stack that has the right enum but maps it to the wrong HTTP/3 code fails.
* `cap-h3-qlog` — HTTP/3 qlogging, read out of the files the candidate's
  own binaries write. `quiche-server` and `quiche-client` are run with
  `QLOGDIR` pointed at a scratch directory and a single HTTP/3 GET is
  performed; each side's `.sqlog` must then contain, with the `http`
  category: `stream_type_set` for its own control stream as
  `"owner":"local"` on stream 2 for the client and stream 3 for the server,
  `stream_type_set` for the peer's control stream as `"owner":"remote"`,
  `stream_type_set` for both QPACK streams; a `frame_created` settings frame
  on the control stream; on the client a `frame_created` headers frame
  containing `":method"` and `GET` and a `frame_parsed` data frame; and on
  the server a `frame_parsed` headers frame and a `frame_created` headers
  frame containing `":status"`. A stack that logs transport events only —
  which is exactly what step 06 does — produces files with none of these.
* `cap-cc-trait-complete` — The sender's burst budget is public and is the
  congestion controller's, not a constant. `Connection::send_quantum()` on a
  fresh connection equals `max_send_udp_payload_size * 10`: a client
  configured with 1200 reports 12000 and the server of the same pair,
  configured with 1452, reports 14520, before the handshake and again after
  it. A 256 KiB transfer is then driven through the pair; `stream_send`
  returns `Err(Done)` while the send capacity is exhausted and resumes as
  the window opens, every byte arrives in order, the congestion window has
  grown by the end, and `send_quantum()` is unchanged across the whole
  transfer. A stack that returns a fixed 1350-derived number regardless of
  configuration fails on the first assertion; one that returns the current
  congestion window fails on the last.
* `cap-peer-cert-borrowed` — `peer_cert()` and `session()` return borrows
  rather than fresh allocations. After a handshake against a server using
  the checker's fixture certificate, `let der: Option<&[u8]> =
  p.client.peer_cert();` type-checks and the bytes equal the DER the checker
  decoded from that PEM itself; two results of `peer_cert()` are held alive
  at the same time and compared, which a `Option<Vec<u8>>` signature cannot
  express through a `&self` receiver without cloning; the server, which
  asked for no client certificate, reports `None`; and `session()` returns
  the same bytes on two consecutive calls. The group is a statement about
  the signature and the data together, so a stack that leaked a `&'static
  []` to satisfy the type fails the content comparison.

`gate-endtoend` carries no capability of its own: it drives this step's
binaries end to end over loopback, and every gate group must pass. A stack
that implements every capability listed above and cannot complete one real
connection does not satisfy this step at all, however much of the new
capability it holds. The group table in `TEST_INTERFACE.md` is the contract
for it, as it is for the rest.

What must still hold: everything step 06 left working. 3 of this step's
groups re-run earlier milestones unchanged, listed by name in
`/app/WORKSPACE_HANDOFF.md`. They are gate groups as well, so any one of them
failing means the step is not satisfied. They are there because this step's
change plausibly breaks them: a rewrite that gets the new behaviour right
and drops an old one does not satisfy the step at all.

## 2. Read these local references

**Normative semantics:**

* `/app/spec/rfc9000.txt` — 16 (variable-length integer encoding), 17
  (packet formats), 19 (frame types). Section 16 and its Appendix A worked
  values are what the `octets` crate encodes and decodes; moving the module
  into its own package must leave the encoding bit-identical. The packet and
  frame layouts are the ones the reader is used on.
* `/app/spec/rfc9218.txt` — 4 (priority parameters), 7 (the PRIORITY_UPDATE
  frame) and 7.2 (its HTTP/3 form specifically; 7.1 is the HTTP/2 one, and 8
  is about merging client- and server-driven priorities, which this release
  does not implement). This is the document that defines `PRIORITY_UPDATE`,
  its two frame type codepoints `0xF0700` and `0xF0701`, the Prioritized
  Element ID field and the structured-field priority value that follows it. It is also where the rule that the most
  recently received update wins comes from, which is what makes the event
  edge-triggered rather than queued.
* `/app/spec/rfc9114.txt` — 6.2.1 (control streams), 7.2.4 (SETTINGS), 8.1
  (HTTP/3 error codes). `PRIORITY_UPDATE` is a control-stream frame, so the
  control-stream rules decide who may send it and what happens when the
  wrong endpoint does. Section 8.1 fixes the two error codes this step's
  guards produce: `H3_FRAME_UNEXPECTED` is `0x105` and `H3_ID_ERROR` is
  `0x108`.
* `/app/spec/rfc9002.txt` — 7 (congestion control), 7.2 (initial and
  minimum congestion window), 7.7 (pacing). `send_quantum()` exposes the
  burst budget these sections describe, and the initial congestion window of
  ten packets is where its starting value comes from. The congestion-control
  trait rework and HyStart++ draft-04 sit behind the same sections.

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
