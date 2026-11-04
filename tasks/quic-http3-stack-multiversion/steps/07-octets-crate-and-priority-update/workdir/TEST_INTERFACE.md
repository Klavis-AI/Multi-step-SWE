# Test interface — step 07, The octets crate, HTTP/3 PRIORITY_UPDATE, and HTTP/3 qlogging

This is the contract between your implementation and the checker. Every
name, path and flag the checker depends on is written down here; nothing
outside this document is assumed about your code. Where the document and
your intuition disagree, the document wins — it is what the tests were
written against.

Generated, together with this step's interface contract, from one source.
The two cannot disagree.

## What this step is

The workspace step 06 created gains a fourth member. The byte reader/writer
that has sat in `quiche/src/octets.rs` since the first milestone moves out of
the transport crate entirely and becomes `octets`, a standalone package with
its own manifest, its own version and no dependencies at all, which `quiche`
then consumes through a path dependency like any other crate. Every codec call
site in the transport changes its import; the types and their behaviour do not.

On top of that, HTTP/3 learns the extensible-priorities signalling channel.
`PRIORITY_UPDATE` (frame type `0xF0700` for request streams, `0xF0701` for
push streams) is parsed off the peer's control stream, validated, and surfaced
to the application as `h3::Event::PriorityUpdate`, with the field value picked
up out of band through `take_last_priority_update`. The event is
edge-triggered and the value is last-write-wins, which is the whole point: a
client that re-prioritises a stream forty times does not hand the server forty
events to process.

HTTP/3 also starts qlogging. Where step 06's qlog crate only ever saw
transport events, this milestone emits `http:*` events beside them —
`stream_type_set` for each control and QPACK stream as it is opened or
discovered, and `frame_created` / `frame_parsed` for every HTTP/3 frame in
both directions — against qlog's own event-draft bump from 0.6 to 0.7.

Underneath, congestion control finishes the trait it started: the per-packet
`on_packet_acked` hook becomes the batched `on_packets_acked`, `congestion_event`
is told how many bytes were lost, HyStart++ moves to draft-04, and the sender
exposes its burst budget as `Connection::send_quantum()`. Two smaller shape
changes ride along: `connect`/`accept` stop wrapping the connection in
`Pin<Box<...>>` and hand back a plain `Connection`, and `peer_cert()` and
`session()` stop cloning into a `Vec<u8>` and return a borrow of what the
connection already holds.

## The flip

Something an earlier step established changes meaning here. This is the
part a candidate most often gets half-right: the new behaviour appears,
the old one is left working beside it, and both the new group and the
inherited group pass for the wrong reason — until the step that
requires the old behaviour to be *gone*.

Three things stop compiling at this step and none of them can be worked around
by keeping the old spelling beside the new one.

**The byte codec leaves the crate.**

```rust
// before (0.11.0)                    // after (0.14.0)
use crate::octets;                    use octets;      // a separate package
crate::octets::Octets<'_>             octets::Octets<'_>
crate::octets::BufferTooShortError    octets::BufferTooShortError
```

`quiche/src/octets.rs` is deleted, `octets/` is a workspace member beside
`quiche/`, `apps/` and `qlog/`, and `quiche/Cargo.toml` declares
`octets = { version = "0.1", path = "../octets" }`. A tree that keeps the
module inside `quiche` and adds a thin re-exporting shim crate has not done
this step: the new crate has to be the definition, buildable on its own by a
consumer that has never heard of `quiche`, and it has to carry no dependencies.

**`Connection` is no longer pinned.**

```rust
// before (0.11.0)
pub fn connect(..) -> Result<std::pin::Pin<Box<Connection>>>;
pub fn accept(..)  -> Result<std::pin::Pin<Box<Connection>>>;
// after (0.14.0)
pub fn connect(..) -> Result<Connection>;
pub fn accept(..)  -> Result<Connection>;
```

Every caller that stored `Pin<Box<Connection>>`, and every `h3` call that took
one, now takes `&mut Connection` directly. There is no `Pin` anywhere in the
public transport API.

**Two accessors stop copying.**

```rust
// before (0.11.0)                       // after (0.14.0)
fn peer_cert(&self) -> Option<Vec<u8>>   fn peer_cert(&self) -> Option<&[u8]>
fn session(&self)   -> Option<Vec<u8>>   fn session(&self)   -> Option<&[u8]>
```

The returned slice borrows the connection, so two `peer_cert()` results can be
compared against each other while both are alive, and neither call may allocate
a fresh buffer per invocation.

The HTTP/3 half of the flip is additive rather than breaking: `h3::Event` grows
a `PriorityUpdate` variant, so an exhaustive `match` over `h3::Event` written
against step 06 no longer compiles. It is a bare unit variant, not a
struct-like one: the prioritized element id does not live on the variant at
all. It is the first member of the `(id, event)` tuple `poll()` already
returns for every event, the same place `Headers`'s and `Reset`'s stream ids
come from.

## Interface elements the checker depends on

### the `octets` crate

`octets` is a package of its own at the workspace root, beside `quiche`,
`apps` and `qlog`:

```toml
# <root>/Cargo.toml
[workspace]
members = [ "apps", "octets", "qlog", "quiche" ]

# <root>/octets/Cargo.toml
[package]
name = "octets"
version = "0.1.0"
edition = "2018"
# ... and no [dependencies] section at all

# <root>/quiche/Cargo.toml
[dependencies]
octets = { version = "0.1", path = "../octets" }
```

`cargo metadata --no-deps` run at the workspace root lists `octets` among the
workspace members. A third-party crate that declares
`octets = { path = "<root>/octets" }` and nothing else builds offline, and the
lockfile cargo produces for it names exactly two packages: `octets` and the
consumer itself. That last clause is the one that rules out a "crate" that is
really a facade over `quiche`.

`quiche` does not re-export the module: there is no `quiche::octets` at this
milestone. Code that wants the reader/writer depends on the crate.

That absence is documented but not independently probed: confirming it would
mean compiling a throwaway crate against the *whole* `quiche` package rather
than the dependency-free `octets` one the two groups above build against, and
`quiche` pulls in a BoringSSL build no other group here pays for a second
time. `cap-octets-crate`'s lock-file check rules out the nearby defect of
`octets` itself being a facade over `quiche` -- that shim would have to depend
on `quiche` to work, which the lock file would show -- but it says nothing
about `quiche` separately keeping a `pub use octets` of its own alongside a
genuinely standalone crate; that narrower fact is out of scope here for the
cost reason above.

### `octets::Octets`, `octets::OctetsMut` and the free functions

Moving the module must not change it. The public surface is:

```rust
pub struct BufferTooShortError;          // Display is "BufferTooShortError"
pub type Result<T> = std::result::Result<T, BufferTooShortError>;

pub struct Octets<'a>    { /* .. */ }    // read-only cursor
pub struct OctetsMut<'a> { /* .. */ }    // read/write cursor

impl<'a> Octets<'a> {
    pub fn with_slice(buf: &'a [u8]) -> Self;
    pub fn get_u8(&mut self) -> Result<u8>;
    pub fn get_u16(&mut self) -> Result<u16>;
    pub fn get_u24(&mut self) -> Result<u32>;
    pub fn get_u32(&mut self) -> Result<u32>;
    pub fn get_u64(&mut self) -> Result<u64>;
    pub fn get_varint(&mut self) -> Result<u64>;
    pub fn get_bytes(&mut self, len: usize) -> Result<Octets>;
    pub fn skip(&mut self, skip: usize) -> Result<()>;
    pub fn cap(&self) -> usize;
    pub fn off(&self) -> usize;
    // .. peek_u8, get_bytes_with_{u8,u16,varint}_length, slice, slice_last,
    //    buf, len, is_empty, to_vec
}

impl<'a> OctetsMut<'a> {
    pub fn with_slice(buf: &'a mut [u8]) -> Self;
    pub fn put_u8(&mut self, v: u8) -> Result<&mut [u8]>;
    pub fn put_u16(&mut self, v: u16) -> Result<&mut [u8]>;
    pub fn put_u24(&mut self, v: u32) -> Result<&mut [u8]>;
    pub fn put_u32(&mut self, v: u32) -> Result<&mut [u8]>;
    pub fn put_u64(&mut self, v: u64) -> Result<&mut [u8]>;
    pub fn put_varint(&mut self, v: u64) -> Result<&mut [u8]>;
    pub fn put_varint_with_len(&mut self, v: u64, len: usize) -> Result<&mut [u8]>;
    pub fn put_bytes(&mut self, v: &[u8]) -> Result<()>;
    pub fn split_at(&mut self, off: usize) -> Result<(OctetsMut, OctetsMut)>;
    // .. and every getter `Octets` has
}

pub fn varint_len(v: u64) -> usize;        // 1, 2, 4 or 8
pub fn varint_parse_len(first: u8) -> usize;
```

`BufferTooShortError` keeps the impls the module already carries, and the probe
leans on all three: `Debug` (its `{:?}` prints `BufferTooShortError`), `Display`
(its `{}` prints the same, as written above), and `std::error::Error`, because
the probe coerces a value to `&dyn std::error::Error`. A type that has the
first two but not the third does not compile against the harness. `cap()` is
the bytes still available to read from the cursor, not the buffer's total
length: the probe drains a buffer to the end and requires `cap()` to read `0`.

The encoding is RFC 9000 Section 16: the two most significant bits of the
first byte give the length, and the reference values from Appendix A round
trip — `151288809941952652` as eight bytes, `494878333` as four, `15293` as
two, `37` as one. Every getter and putter that would run off the end of the
buffer returns `Err(BufferTooShortError)` rather than panicking, and a failed
call does not advance the cursor.

### HTTP/3 PRIORITY_UPDATE and `h3::Event::PriorityUpdate`

```rust
pub enum Event {
    Headers { list: Vec<Header>, has_body: bool },
    Data,
    Finished,
    Datagram,
    GoAway,
    Reset(u64),
    PriorityUpdate,                                    // new here, unit
}

impl h3::Connection {
    pub fn poll(&mut self, conn: &mut Connection) -> Result<(u64, Event)>;

    pub fn take_last_priority_update(
        &mut self, prioritized_element_id: u64,
    ) -> Result<Vec<u8>>;
}
```

`PriorityUpdate` carries no payload of its own. `poll()` returns the same
`(id, event)` tuple it returns for every other event, and for this one the id
in that tuple *is* the prioritized element id: `poll()` gives back
`(prioritized_element_id, Event::PriorityUpdate)`, not a struct field. That id
is what gets passed to `take_last_priority_update` below.

The frame is the one RFC 9218 defines, carried on the control stream:

```
PRIORITY_UPDATE_REQUEST = 0xF0700     PRIORITY_UPDATE_PUSH = 0xF0701
payload: varint Prioritized Element ID, then the priority field value
         as ASCII to the end of the frame
```

Behaviour a server must show, all of it observable through `poll()`:

* A well-formed `PRIORITY_UPDATE` for request stream `id` raises
  `(id, Event::PriorityUpdate)` once — `id` is the tuple's first member, as
  described above, not a field of the variant.
* The event is **edge-triggered**. A second update for the same element that
  arrives while the first has not been taken raises no second event; it
  replaces the stored value. Once the value has been taken, a further update
  arms the event again.
* `take_last_priority_update(id)` returns the most recent field value as raw
  bytes and clears it; a second call with nothing pending returns
  `Err(h3::Error::Done)`.
* Receiving `PRIORITY_UPDATE` as a **client** is
  `Err(h3::Error::FrameUnexpected)`, and the connection is closed with
  application error `0x105` (`H3_FRAME_UNEXPECTED`).
* A prioritized element id that is not a client-initiated bidirectional stream
  id (not `% 4 == 0`) is `H3_FRAME_UNEXPECTED` (`0x105`).
* An id beyond the peer's stream limit is `H3_ID_ERROR` (`0x108`).

**One deliberate deviation from RFC 9218.** Section 7.2 says a `0xF0700` whose
element id is not a request stream MUST be a connection error of type
`H3_ID_ERROR`. This milestone raises `H3_FRAME_UNEXPECTED` (`0x105`) for the
not-`% 4 == 0` case above, and keeps `H3_ID_ERROR` for the stream-limit case
alone. This document governs over the RFC: a stack that follows Section 7.2 to
the letter fails `cap-priority-update-guards`.

Each of the three rejections above closes the connection carrying the
application error code named beside it, readable off the *transport*
`Connection` (not `h3::Connection`) through `local_error()`/`peer_error()`,
both carried over unchanged from an earlier milestone:

```rust
impl Connection {
    pub fn local_error(&self) -> Option<&ConnectionError>;  // this endpoint's own close
    pub fn peer_error(&self) -> Option<&ConnectionError>;   // what it learned the peer sent
}

pub struct ConnectionError {
    pub is_app: bool,     // true for an application-level (h3) close, as above
    pub error_code: u64,  // the code named above: 0x105 or 0x108 here
    pub reason: Vec<u8>,  // not exercised at this step
}
```

Both accessors return `None` on a connection that has not closed.

`h3::Priority::new(urgency, incremental)` exists as the structured form used
when sending a response, but no public API sends a `PRIORITY_UPDATE` frame at
this milestone; the receive path is the whole of the new capability.

`PRIORITY_UPDATE_PUSH` (`0xF0701`) is out of scope at this milestone and no
group exercises it. The Oracle parses it and applies the same control-stream
and prioritized-element-id guards as the request variant, but then does
nothing further with it -- `quiche/src/h3/mod.rs` marks the branch `// TODO:
we only implement this if we implement server push`, and server push itself
has no public API in this release: nothing can create a push stream to be
prioritized. Testing the stub's non-crash would pin an implementation
accident of an admittedly-unfinished code path, not a capability of this
release.

### `HTTP/3 qlog events`

When qlogging is enabled on the transport connection, the HTTP/3 layer writes
its own events into the same stream, with the `http` category. The checker runs
both binaries with `QLOGDIR` set and reads the `.sqlog` files they leave
behind; the shapes it looks for are:

* `http:stream_type_set` — one per control and QPACK stream, carrying
  `stream_id`, `owner` (`"local"` or `"remote"`) and `stream_type`
  (`"control"`, `"qpack_encode"`, `"qpack_decode"`). The local control stream
  is stream 2 on a client and stream 3 on a server, and each endpoint logs
  both its own three streams as `local` and the peer's three as `remote`.
* `http:frame_created` — a frame this endpoint wrote, with `stream_id` and a
  `frame` object whose `frame_type` is `"settings"`, `"headers"`, `"data"`,
  `"goaway"` and so on. A `headers` frame carries its header list, so the
  client's request shows `":method"` / `"GET"` and the server's response shows
  `":status"`.
* `http:frame_parsed` — the same, for a frame this endpoint read.

The qlog crate's event definitions move from draft 0.6 to draft 0.7 to carry
them. Transport events are unchanged and still present in the same file.

That "0.6 to 0.7" is the `qlog` crate's own `Cargo.toml` package version (it
reads `0.6.0` at the previous milestone's tag and `0.7.0` at this one), not a
field written into any emitted `.sqlog` file -- the JSON-SEQ header's own
`qlog_version` stays `"0.3"`, unchanged since step 06, because that number
names the streaming envelope format, not the event-schema draft. No group
asserts the crate's own manifest version: it is packaging metadata a
candidate could leave unbumped without changing any observable behaviour this
step's groups can see, unlike the `http:*` event shapes above, which are
checked directly.

### `Connection` by value, and `send_quantum()`

```rust
pub fn connect(server_name: Option<&str>, scid: &ConnectionId,
               to: SocketAddr, config: &mut Config) -> Result<Connection>;
pub fn accept(scid: &ConnectionId, odcid: Option<&ConnectionId>,
              from: SocketAddr, config: &mut Config) -> Result<Connection>;

impl Connection {
    pub fn send_quantum(&mut self) -> usize;
    pub fn peer_cert(&self) -> Option<&[u8]>;
    pub fn session(&self) -> Option<&[u8]>;
}

impl h3::Connection {
    pub fn with_transport(conn: &mut Connection, config: &h3::Config)
        -> Result<h3::Connection>;
}
```

`send_quantum()` is the sender's burst budget in bytes — how much the pacer
will allow on the wire back to back. On a fresh connection it is
`max_send_udp_payload_size * 10`, the initial congestion window in packets, so
a `Config` left at the default 1350 reports 13500 and one set to 1200 reports
12000. It is a property of the sender's configuration and its congestion
state, not of the peer's, and it does not change merely because the congestion
window grew during a transfer.

Congestion control's internal trait is completed in the same release —
`on_init`, the batched `on_packets_acked(&[Acked], ..)` in place of the
per-packet `on_packet_acked`, and a `congestion_event` that is told the number
of bytes lost — and HyStart++ moves to draft-04. None of that is public API;
what is public is that a bulk transfer still grows the congestion window and
still delivers its bytes in order.

### `the workspace binaries`

Unchanged from step 06 and still driven headlessly:

* `target/debug/quiche-server` — `--listen <addr>`, `--cert <pem>`,
  `--key <pem>`, `--root <dir>`, `--no-retry`. Serves HTTP/0.9 and HTTP/3 on
  the same socket, chosen by ALPN.
* `target/debug/quiche-client` — `--no-verify`, `--http-version HTTP/3|HTTP/0.9`
  (default: negotiate), one positional URL; the body goes to stdout.

Both honour `QLOGDIR`: if it is set and names an existing directory, each
connection writes `<role>-<scid hex>.sqlog` into it, with `role` being `client`
or `server` — the same name step 06's contract fixes, and the name the
inherited tree already writes. The role prefix is what the checker matches on:
it looks for a name starting with `client` and one starting with `server`, and
a file named after the connection ID alone is not found.

The records have to be **flushed while the connection runs**. The checker reads
the trace as soon as the request it drove has completed, and the server
process is not given a chance to shut down cleanly first, so a writer that
holds the whole trace in memory and emits it on exit is read as an empty
directory — every assertion in `cap-h3-qlog` then fails on an empty log rather
than on a missing event. The checker never writes into the candidate's tree —
certificates, the document root and the qlog directory are all staged under
its own scratch space and passed by flag or environment.

## The groups at this step

One row per group. **gate** groups are all-or-nothing: a failure means the step is not satisfied; **requirement** groups cover this step's own new capability. The rows are grouped by tier for reading; the order they run in, and each one's result, is what `reward_details.json` records.

| group | tier | surface | what it observes |
| --- | --- | --- | --- |
| `candidate-workspace-builds` | gate | T4 | The one check every step shares: `cargo build --offline` (default targets -- libraries and binaries only, not tests or benches) must complete for the workspace at `/app` within the harness's build timeout, before any other group in this table runs. It is the one group every step carries identically, and no step declares it as a capability of its own -- but it is not optional either: one failing gate group, including this one, means the step is not satisfied, however much of the new work is right. |
| `cap-octets-crate` | requirement | T4 | `octets` is a real, separately buildable package in the workspace. `<root>/octets/Cargo.toml` exists and names the package `octets`; `cargo metadata --offline --no-deps` at the workspace root succeeds and lists `octets` among `workspace_members`; and a crate the checker writes into its own scratch directory, declaring `octets = { path = "<root>/octets" }` as its only dependency and detached from the workspace, compiles offline against it. The lockfile that build produces must name exactly two packages — `octets` and the checker's probe — which is what fails a tree that kept the module inside `quiche` and made `octets` a facade that pulls the transport crate back in. |
| `cap-octets-api` | requirement | T4 | The moved crate is the same codec it was inside `quiche`. The probe program from the previous group is given a body that exercises the public surface and prints its results, and the checker asserts the whole of its stdout byte for byte: the four RFC 9000 Appendix A variants (`37`, `15293`, `494878333`, `151288809941952652`) round trip through `put_varint`/`get_varint` at their documented 1, 2, 4 and 8 byte lengths; `put_u8`/`u16`/`u24`/`u32`/`u64` and `put_bytes` read back through the matching getters; `varint_len` and `varint_parse_len` agree with the encoding; and a getter aimed past the end of its buffer, and a `skip` past the end, both return `Err(BufferTooShortError)` whose `Display` is `BufferTooShortError`, rather than panicking. A crate that exports the names but not the RFC 9000 encoding fails on the first varint. The same probe also exercises the secondary surface the two structs carry beside the getters and putters above: `peek_u8` reads without moving the cursor, where `get_u8` moves it; `get_bytes_with_u8_length`, `_u16_length` and `_varint_length` read a length-prefixed run whose prefix width the name says; `slice`/`slice_last` hand back a sub-slice of, and from the end of, the underlying buffer without consuming it; `is_empty` answers about the *whole* buffer's length, not the cursor's position, so it stays `false` after every byte has been read and is only ever `true` on a buffer that started at length zero; `to_vec` copies from the current offset onward; `OctetsMut::split_at` divides one buffer into two independent ones at a byte offset; and `put_varint_with_len` forces an encoding wider than the value needs, tagged with the length's own top-bits prefix. Last, and the one behavioural rather than signature fact in this group: a call that returns `Err(BufferTooShortError)` never moves the cursor first, so a short read can be retried against more bytes without having silently consumed any of the ones it failed to read. |
| `cap-priority-update` | requirement | T3 | The receive path for `PRIORITY_UPDATE`, driven over an in-memory HTTP/3 pair using only the candidate's public API. The checker writes a well-formed `0xF0700` frame onto its own control stream with `stream_send` and steps the server through five stages: the first update for request stream 0 raises exactly one `(0, Event::PriorityUpdate)` — a bare unit variant, the `0` being the tuple's first member, not a field on the variant; a second update that arrives before the value is taken raises no further event but replaces the stored value; `take_last_priority_update(0)` yields the **second** field value and a repeat call yields `Err(Done)`; a third update after the take arms the event again and carries its own value; and an update for element 4 is tracked separately from element 0. Last-write-wins and edge-triggering are both load bearing — a server that queues one event per frame, or that returns the first value rather than the last, fails. |
| `cap-priority-update-guards` | requirement | T3 | The three ways a `PRIORITY_UPDATE` is invalid, each on its own fresh pair so that one closed connection cannot mask the next. A `0xF0700` frame sent to a **client** is `Err(h3::Error::FrameUnexpected)` and the client closes with application error `0x105`, which the peer then reports through `peer_error()`. A prioritized element id of 2 — a unidirectional stream id, not `% 4 == 0` — is `FrameUnexpected` and `0x105` again. A prioritized element id of 4000, past the hundred bidirectional streams the peer was granted, is `Err(h3::Error::IdError)` and application error `0x108`. The wire error code is read off `local_error()` rather than from the Rust variant, so a stack that has the right enum but maps it to the wrong HTTP/3 code fails. |
| `cap-h3-qlog` | requirement | T4 | HTTP/3 qlogging, read out of the files the candidate's own binaries write. `quiche-server` and `quiche-client` are run with `QLOGDIR` pointed at a scratch directory and a single HTTP/3 GET is performed; each side's `.sqlog` must then contain, with the `http` category: `stream_type_set` for its own control stream as `"owner":"local"` on stream 2 for the client and stream 3 for the server, `stream_type_set` for the peer's control stream as `"owner":"remote"`, `stream_type_set` for both QPACK streams; a `frame_created` settings frame on the control stream; on the client a `frame_created` headers frame containing `":method"` and `GET` and a `frame_parsed` data frame; and on the server a `frame_parsed` headers frame and a `frame_created` headers frame containing `":status"`. A stack that logs transport events only — which is exactly what step 06 does — produces files with none of these. |
| `cap-cc-trait-complete` | requirement | T3 | The sender's burst budget is public and is the congestion controller's, not a constant. `Connection::send_quantum()` on a fresh connection equals `max_send_udp_payload_size * 10`: a client configured with 1200 reports 12000 and the server of the same pair, configured with 1452, reports 14520, before the handshake and again after it. A 256 KiB transfer is then driven through the pair; `stream_send` returns `Err(Done)` while the send capacity is exhausted and resumes as the window opens, every byte arrives in order, the congestion window has grown by the end, and `send_quantum()` is unchanged across the whole transfer. A stack that returns a fixed 1350-derived number regardless of configuration fails on the first assertion; one that returns the current congestion window fails on the last. |
| `cap-peer-cert-borrowed` | requirement | T3 | `peer_cert()` and `session()` return borrows rather than fresh allocations. After a handshake against a server using the checker's fixture certificate, `let der: Option<&[u8]> = p.client.peer_cert();` type-checks and the bytes equal the DER the checker decoded from that PEM itself; two results of `peer_cert()` are held alive at the same time and compared, which a `Option<Vec<u8>>` signature cannot express through a `&self` receiver without cloning; the server, which asked for no client certificate, reports `None`; and `session()` returns the same bytes on two consecutive calls. The group is a statement about the signature and the data together, so a stack that leaked a `&'static []` to satisfy the type fails the content comparison. |
| `reg-01-header-parse` | gate | T5 | Inherited from step 01: `Header::from_slice` reads a long header out of a fixed byte corpus the checker carries. An Initial, a Retry and a Version Negotiation packet, all at QUIC version 1, are parsed with the connection-id length the server would use; the type, version, connection ids, token and version list come back exactly as the corpus encodes them, the Retry's token is the bytes before its 16-byte integrity tag, and two truncations of the same packet return `Err(Error::BufferTooShort)`. The version list is read through the `versions` field by name and compared against a `Some(vec![...])` literal holding the parsed versions in wire order, so the field must be an `Option<Vec<u32>>` -- the shape `Header` has had since step 01, stated in the handoff above and in step 01's own interface section. The codec being parsed with now lives in another crate; the bytes it reads must not have moved with it. |
| `reg-01-h3-get` | gate | T1 | Inherited from step 01: a 64 KiB body travels from the candidate's HTTP/3 server to the candidate's HTTP/3 client over UDP loopback, byte for byte, as two real processes. The invocation is this milestone's — `quiche-server --listen/--cert/--key/--root/--no-retry` and `quiche-client --no-verify --http-version HTTP/3` — but the observation is step 01's. HTTP/3 grew a frame type, an event variant and a whole qlog surface at this step; none of that may cost it the ability to serve a file. |
| `reg-06-qlog-jsonseq` | gate | T4 | Inherited from step 06: the qlog crate still writes a JSON-SEQ trace whose header record names `qlog_version` `"0.3"` and `qlog_format` `"JSON-SEQ"`, carries no `traces` array, and is followed by one record per transport event, including all five of `recovery:metrics_updated`, `transport:data_moved`, `transport:packet_received`, `transport:packet_sent` and `transport:parameters_set` for a completed transfer. The invocation is step 06's own -- no `--http-version` flag, so ALPN never selects HTTP/3 and the new `http:*` events this milestone adds never enter the trace -- because the point of this group is not the new HTTP/3 qlog surface `cap-h3-qlog` already covers, it is that the shared JSON-SEQ writer those `http:*` events now also run through has not changed shape or dropped an event underneath them. Step 06's own group asserted this *exact* set of five names; that assertion cannot be carried forward as written, because a step-07 stack correctly adding `http:*` events to the same file would then have a strictly larger name set and fail it for doing its job. This group checks presence instead: each of the five names must still appear somewhere in the trace, which step 06's exactness already implied and step 07 must not have broken. |
| `gate-endtoend` | gate | T1 | The stack works at all: `quiche-server` starts on a free loopback port with a staged certificate and document root, `quiche-client` completes a handshake against it and retrieves a 2 KiB file byte for byte, and the client exits zero. If this fails, every other T1 observation at this step is noise. |

Surfaces: **T1** your client and server as processes over UDP
loopback; **T2** a QUIC endpoint the checker implements itself, reading
and writing real packets; **T3** your library linked into the checker's
own crate, public API only; **T4** a file or layout your code produces;
**T5** an input with exactly one correct answer.

## How this step is checked

The **gate** covers proof of life plus the milestones you already have: your
workspace builds, your binaries complete one end-to-end exchange, and the
earlier capabilities this step disturbs still work. Gate groups add no new
requirement of their own, but a failure in one means the step is not
satisfied, however much of the new work is right.

The **requirement** groups cover this step's own new capability, and every one
of them is in scope. Your step's interface document lists every group with its
tier.

Every case within a group must pass, and there is no separate requirement for
style or for extra work.

Three families of group, named in the report so you can tell them apart:

| prefix | tier | what it checks |
|---|---|---|
| `cap-*` | requirement | the capability this step introduces |
| `reg-NN-*` | gate | a capability step NN introduced, still working |
| `gate-endtoend` | gate | one end-to-end exchange through your own binaries |
| `candidate-workspace-builds` | gate | your workspace builds at all, before any other group runs |

`reg-NN-*` groups are not decoration: every step in this task rewrites
something an earlier step built, and the `reg-` groups are chosen from the
surfaces this step disturbs. (A step with no earlier milestone carries none of
them: this section describes the family, and the group table above is what
says whether a given step has any.) A rewrite that quietly drops an older
behaviour does not fail just one group; it means the step is not satisfied.

The corollary is worth stating plainly: a do-nothing continuation satisfies
none of the new work. A workspace carried forward untouched passes every gate
group — it has not broken anything — and passes no requirement group, because
it implements none of this step's capability.

### What the checker is allowed to look at

Behaviour, and only behaviour:

* your library, linked into a test harness that lives outside `/app` and calls
  your public API;
* your client and server binaries -- whichever names this step's own interface
  section gives them -- run as real processes talking to each other over UDP
  loopback;
* your binaries talking to a QUIC endpoint the checker implements itself, which
  derives its own keys and reads the bytes on the wire;
* files your code emits — qlog, netlog, a generated C header, the workspace
  layout;
* exact answers to inputs designed to have exactly one right answer.

It never **checks** anything by reading your source: no group greps your tree
for an identifier, counts your modules, or checks a log line. One qualification
belongs beside that: anything the checker's own crate *imports or names* has to
exist under that name, because it is a Rust program that compiles against your
crate. Those names are this step's interface section, and where a group builds
a probe of its own, the names that probe imports are written down there too.
Inside your own modules, name things whatever you like. Two plumbing
steps outside the checked path do read source text -- the image picks which
pinned BoringSSL to link by looking at `src/tls.rs`, and where this
milestone's release changed an API's shape the harness picks which dialect of
its own test shim to compile by looking for the new spelling in `src/`. Both
read live code only -- `//` comments are stripped before either check, and the
dialect probe, where this step has one, drops `/* ... */` blocks as well -- and
both key on spellings this step's own documents already tell you to change, so a
tree you have converted is never misread.
Name things whatever you like, put them wherever the manifest says, and write
in whatever style you prefer.

### Reading the report

`reward_details.json` names every group, its tier, whether it
passed, and for a failing group the first line of why. When the gate fails it
says which gate group failed and what it saw — a step reported as unsatisfied
without naming the group responsible would be indistinguishable from a
candidate that did nothing. When a group fails for a reason that is not about
your code — a build that did not finish inside its budget, a harness that
failed to link — it is reported as such rather than silently treated as a
failure.

## The build environment

Written once and generated into every step's copy of this document, so that the
fifteen copies cannot drift apart. Anything in this section that is *not* true
of your step is called out in the step's own section above, never by silently
contradicting it here.

### Toolchain

Rust **1.88** (`rustc 1.88`, `cargo 1.88`), Debian bookworm, x86-64. Also
installed: `clang`, `cmake`, `ninja`, `perl`, `go`, `pkg-config`, `git`,
`python3`, `jq`, `ripgrep`, `xxd`, `iproute2`, `libev`, `uuid-dev`.

### BoringSSL is prebuilt; your build script must link it, not build it

Your workspace does **not** contain BoringSSL and must not try to fetch or
compile it. A prebuilt static BoringSSL is installed in the image, and the
environment variable **`QUICHE_BSSL_PATH`** points at it:

```
$QUICHE_BSSL_PATH/
├── build/
│   ├── libcrypto.a
│   ├── libssl.a
│   ├── crypto/libcrypto.a     (hard link, for the older expected layout)
│   └── ssl/libssl.a           (hard link)
├── libcrypto.a                (copy, for the newest expected layout)
├── libssl.a
├── include/openssl/...
└── COMMIT                     the BoringSSL commit this was built from
```

All three layouts are populated, so a build script that looks in any of them
finds the archives. The archives are built with
`-DCMAKE_POSITION_INDEPENDENT_CODE=ON`, so they link into a `cdylib`.

`QUICHE_BSSL_PATH` is set in the environment in the checker image, and in the
implementation container your step's `.cargo/config.toml` is what sets it —
keep that file, or a build script that reads the variable has nothing to read
and nothing to link. Read it in your build script and emit the
`cargo:rustc-link-search` / `cargo:rustc-link-lib` lines yourself — nothing
does that for you.

The prebuilt BoringSSL that `QUICHE_BSSL_PATH` points at is the one your step
is built against. The measurement runs offline, so it is also the only one available:
you never need to know its commit hash, only to link against that path.

### The measurement runs offline

At measurement time the container has **no network**. `CARGO_NET_OFFLINE=true` is
set and the registry cache holds the crates this milestone's build can reach:
every crate in the `Cargo.lock` that ships in this workdir, the versions this
milestone's own manifests resolve to, the previous release's graph, and the
checker's own harness crate's graph. Nothing else is available. Consequences:

* **Keep `Cargo.lock`.** Do not delete it and do not run `cargo update`. It is
  the contract between your manifest and the offline cache, and a version it
  pins is reachable offline even where the requirement alone would not reach
  it.
* **A dependency you add that is outside that set will not resolve**, here or
  at measurement time, and the build failure is reported as a build failure. If you
  need something, check `Cargo.lock` first.
* **Your resolution is pinned to this milestone's set, here as well as at
  measurement time.** One image serves all fifteen milestones and its registry
  cache is the union of every milestone's crates, so the set above is not what
  the cache holds -- it is what you are allowed to resolve. The step's
  `setup.sh` points cargo's `crates-io` at a single directory source built
  from this milestone's own locks, before your first command runs. A
  dependency outside it therefore fails here too, with the checker's own
  message:

      no matching package named `NAME` found
      location searched: directory source `/opt/vendor/NN`

  A local build that resolves is a build that works here as well. Do not delete or edit
  `$CARGO_HOME/config.toml`; that file is the pin.
* Cargo resolves the *whole* workspace graph, dev-dependencies included, before
  it compiles anything — so even a dev-dependency you invent breaks the build.
* `CARGO_TARGET_DIR` is set to a shared directory whose dependency graph is
  already compiled. Your build pays for your code only.

### What the checker builds and runs

```
cd /app && cargo build --offline          # libraries and binaries
cd /app && cargo build --offline --examples   # best effort; never fatal
cd /app/<app package> && cargo build --offline   # best effort; never fatal
```

Your own tests are **not** compiled by the checker, and a broken test of yours
costs you nothing. Correspondingly, passing your own tests is not required:
the checker links your crate from its own throw-away harness crate outside
`/app`, and runs your binaries as processes.

### Where your crate has to be

The checker locates the transport crate by looking for a `Cargo.toml` whose
package `name` is `quiche`, first at `/app/quiche` and then at `/app`, and uses
the first of the two that exists. Every group that merely needs *the transport
crate* -- to build it, to read its manifest, to look inside its `src/` or its
`include/` -- goes through that search, so either arrangement satisfies all of
them.

The exceptions are the groups for which the layout is the thing being checked,
and there are two kinds. The step that *introduces* the workspace conversion
checks the move itself, against the paths that conversion is defined to
produce. From that step onward, a group that checks **workspace membership**
names the member directories it expects -- `quiche` among them -- because which
crates are members, and where they sit, is that milestone's subject matter
rather than an incidental path. A group of either kind says so in its own
interface section. Outside those groups the layout is never checked.

### The binaries

Several groups run your client and server as real processes over UDP loopback.

**This step's own interface section names the exact binaries it expects, and
that section governs.** Build them with a plain `cargo build` (or let the
best-effort `--examples` pass pick them up). Where they live does not matter:
`examples/`, `tools/apps/` or their own package are all fine. The checker
searches these directories, in this order, for each name before it moves to the
next name:

```
$CARGO_TARGET_DIR/debug/
$CARGO_TARGET_DIR/debug/examples/
$CARGO_TARGET_DIR/release/
$CARGO_TARGET_DIR/release/examples/
```

and then, in case a candidate ignored `CARGO_TARGET_DIR` or a workspace member
carries a target directory of its own, the same four in the same order under
`<workspace root>/target/`, `<workspace root>/quiche/target/` and
`<workspace root>/apps/target/`.

What *does* matter is the name, and it is not a free choice. Each binary the
checker needs is resolved from a short list of accepted names -- this step's own
interface section gives them, most-preferred first -- and the search is
name-major: the first name on that list is looked for across every directory
before the second is tried anywhere. A binary that is correct but named
something outside the list is invisible to the group that needs it, and the
group fails with no diagnostic.

Two consequences worth stating plainly, because both have bitten:

* A single release can ship more than one client, and they are not
  interchangeable — an HTTP/3 group needs the HTTP/3-capable binary
  specifically. Naming a general-purpose client something an HTTP/3 group
  resolves would hand that group a client that cannot speak HTTP/3.
* Upstream renamed these binaries across the fifteen releases. Do not carry a
  name forward from an earlier step because it worked there; read this step's
  interface section.

## Where this step starts

`/app` contains everything step 06 left behind, unchanged. The milestone before this one is modelled on quiche 0.11.0; this one is modelled on 0.14.0. Read `WORKSPACE_HANDOFF.md` for what is already there and what must keep working.

## Out of scope

Stated so you do not spend the budget on them.

* **Interoperability with other QUIC implementations.** No second stack is
  installed and there is no network at measurement time.
* **Performance.** Throughput, CPU, allocation counts and congestion-control
  *efficiency* are never measured. Congestion controllers are checked on
  observable state machine behaviour, not on how fast they go.
* **Anything under `fuzz/`**, and your own test suite: the checker does not
  compile either.
* **Documentation, comments, formatting, clippy.**
* **Platform support other than x86-64 Linux**, and every Windows, macOS, iOS
  or Android code path.
* **The `boringssl-boring-crate` feature** at the releases where it is
  optional. Its crate version is yanked from crates.io and cannot be resolved
  offline; the default feature set is what gets built.

