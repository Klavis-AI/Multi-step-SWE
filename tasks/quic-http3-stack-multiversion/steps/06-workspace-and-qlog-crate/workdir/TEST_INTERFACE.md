# Test interface — step 06, the cargo workspace, the qlog crate and the flag-taking apps

This is the contract between your implementation and the checker. Every
name, path and flag the checker depends on is written down here; nothing
outside this document is assumed about your code. Where the document and
your intuition disagree, the document wins — it is what the tests were
written against.

Generated, together with this step's interface contract, from one source.
The two cannot disagree.

## What this step is

Reshape the repository. Up to now the library has been a single crate with its
root at `src/lib.rs` and a handful of positional-argument programs under
`examples/`. At this milestone the repository becomes a cargo workspace with
three members: `quiche/` holds the transport and HTTP/3 library, `qlog/` is a
separately versioned crate that owns the qlog schema and its streaming
serialiser, and `apps/` is a real binary crate that builds `quiche-client` and
`quiche-server` — programs that take flags instead of having their listen
address, certificate paths and document root compiled in.

The qlog split is not cosmetic. The trace format changes with it: a qlog is no
longer one JSON document written at the end of a connection but a JSON Text
Sequence, one record per line, each prefixed with the record separator `0x1e`,
written as the connection runs. `Connection::set_qlog` keeps its signature and
gains `set_qlog_with_level` beside it, so a caller can ask for `Core`, `Base`
or `Extra` importance.

Three transport capabilities arrive with the reshuffle. The receiver's flow
control window autotunes: it starts at the advertised initial limit and doubles
each time the application drains it inside two round trips, bounded by two new
`Config` setters. `RESET_STREAM` from the peer becomes visible to the
application as `Error::StreamReset(u64)` on the transport and
`h3::Event::Reset(u64)` on HTTP/3, where before a reset stream simply went
quiet. And the ACK frame's ECN-count form — frame type `0x03` — is parsed,
where before it was an unknown frame type and a connection error.

HTTP/3 renames one setting to match the published RFC:
`Config::set_max_header_list_size` becomes `set_max_field_section_size`, and
`h3::Connection::peer_settings_raw()` exposes the peer's SETTINGS exactly as
they arrived, unknown identifiers and all.

## The flip

Something an earlier step established changes meaning here. This is the
part a candidate most often gets half-right: the new behaviour appears,
the old one is left working beside it, and both the new group and the
inherited group pass for the wrong reason — until the step that
requires the old behaviour to be *gone*.

The crate root moves. `quiche/Cargo.toml` is now the library's manifest and
`quiche/src/lib.rs` its root; the workspace manifest at the repository root
declares the members and builds no code of its own:

```toml
# ./Cargo.toml
[workspace]
members = [ "quiche", "qlog", "apps" ]
```

Anything that cargo-builds the repository from the top must therefore find
three packages — `quiche`, `qlog` and `quiche_apps` — and a checker that links
the library links `quiche/`, not `.`. A tree that keeps `src/lib.rs` at the
root and adds a `qlog` directory beside it has not done this step.

`apps/` replaces `examples/`. The two programs it produces are
`target/debug/quiche-client` and `target/debug/quiche-server`, they parse their
command line with docopt, and every setting the old examples had compiled in is
now a flag:

```text
quiche-server --listen <addr> --cert <file> --key <file> --root <dir>
              --index <name> --max-data BYTES --max-stream-data BYTES
              --max-window BYTES --max-stream-window BYTES --idle-timeout MS
              --no-retry --dump-packets PATH ...
quiche-client [options] URL...
              --no-verify -n REQUESTS -H HEADER --wire-version VERSION
              --session-file PATH --max-window BYTES --max-stream-window BYTES ...
```

`--listen` defaults to `127.0.0.1:4433`, which is the address the previous
three milestones had no way to change. A server that still ignores its
arguments and binds a compiled-in port fails this step even if the transport
underneath is perfect.

The qlog serialisation flips from JSON to JSON-SEQ. A file that opens with `{`
and a `"traces"` array is the old format; the new one opens with the byte
`0x1e` followed by a single-line JSON object carrying
`"qlog_format":"JSON-SEQ"`, and every event after it is another `0x1e`-prefixed
line.

`Error` and `h3::Event` both gain a variant, so an exhaustive `match` over
either stops compiling:

```rust
quiche::Error::StreamReset(u64)   // new
quiche::h3::Event::Reset(u64)     // new
```

and the HTTP/3 setting is renamed rather than aliased:

```rust
// before (0.9.0)                     // after (0.11.0)
h3::Config::set_max_header_list_size  h3::Config::set_max_field_section_size
```

Nothing else changes shape here, but one thing the previous milestone changed
*is* compiled against for the first time. See the `h3::Header` element below:
this milestone's harness is the first in the ladder to build headers and read
field lines back, so a tree carried forward with the pre-0.9.0 header types --
which the previous step's own groups never touched, and therefore never failed
-- does not compile past this step's inherited gate target.

## Interface elements the checker depends on

### `the workspace layout`

The repository root holds a virtual workspace manifest and no library source:

```toml
[workspace]
members = [ "quiche", "qlog", "apps" ]
exclude = [ "fuzz", "tools/http3_test" ]
```

Three members, three packages:

| directory | package name | version | what it is |
| --- | --- | --- | --- |
| `quiche/` | `quiche` | 0.11.0 | the transport and HTTP/3 library; root `quiche/src/lib.rs` |
| `qlog/` | `qlog` | 0.6.0 | the qlog schema types and the streaming serialiser |
| `apps/` | `quiche_apps` | — | the `quiche-client` and `quiche-server` binaries |

`quiche` depends on `qlog` by path — `qlog = { version = "0.6", path = "../qlog", optional = true }` — behind an optional `qlog` feature, so the library
still builds with default features and no qlog support. `apps` depends on
`quiche` by path and turns that feature on.

A checker that wants the library links `quiche/`. `cargo build` at the root
builds all three members and puts both binaries in the workspace's shared
`target/debug/`.

### `quiche-server and quiche-client`

`apps/` builds exactly two binaries, `quiche-server` and `quiche-client`, and
they take their configuration from the command line. The flags that matter to
the checker:

```text
quiche-server
  --listen <addr>            IP:port to bind          [default: 127.0.0.1:4433]
  --cert <file>              TLS certificate chain    [default: src/bin/cert.crt]
  --key <file>               TLS private key          [default: src/bin/cert.key]
  --root <dir>               document root            [default: src/bin/root/]
  --index <name>             file served for `/`      [default: index.html]
  --max-data BYTES           connection flow control  [default: 10000000]
  --max-stream-data BYTES    per-stream flow control  [default: 1000000]
  --max-window BYTES         connection max receiver window  [default: 25165824]
  --max-stream-window BYTES  per-stream max receiver window  [default: 16777216]
  --max-streams-bidi N       concurrent streams       [default: 100]
  --max-streams-uni N        concurrent uni streams   [default: 100]
  --idle-timeout MS          idle timeout             [default: 30000]
  --no-retry                 disable stateless retry
  --dump-packets PATH        write incoming packets to a directory

quiche-client [options] URL...
  --no-verify                do not verify the server certificate
  -n, --requests N           send N identical requests [default: 1]
  -H, --header HEADER        add a request header
  --dump-responses PATH      write each response body to a file in PATH
  --http-version VERSION     HTTP/0.9, HTTP/3 or all  [default: all]
  --wire-version VERSION     version to put on the wire [default: babababa]
  --session-file PATH        cache a TLS session for resumption
  --max-data BYTES           connection flow control  [default: 10000000]
  --max-stream-data BYTES    per-stream flow control  [default: 1000000]
  --max-window BYTES         connection max receiver window  [default: 25165824]
  --max-stream-window BYTES  per-stream max receiver window  [default: 16777216]
  --max-streams-bidi N       concurrent streams       [default: 100]
  --max-streams-uni N        concurrent uni streams   [default: 100]
  --idle-timeout MS          idle timeout             [default: 30000]
  --dump-packets PATH        write incoming packets to a directory
```

The seven flow-control, receiver-window, stream-count and idle-timeout flags
above the dividing line are one shared set: both binaries parse them from the
same place and both carry the same defaults, which is why they are written out
identically under each. That matters to a checker rather than being a
presentational choice -- a group at this step reads the server's qlog
`parameters_set` record for `"owner":"remote"`, which is the server
reporting the transport parameters `quiche-client` sent it, and checks them
against these client-side defaults when the client is launched with none of
the corresponding overrides. A client that defaults differently is detected
there, so the defaults are part of the contract and not an implementation
detail.

`--http-version` is on `quiche-server` too, with the same three values and the
same default; `HTTP/3` restricts the ALPN list to the `h3` family and
`HTTP/0.9` to the `hq-interop` family.

Behaviour the checker relies on:

* `quiche-server` binds exactly the address `--listen` names, reads its
  certificate and key from exactly the paths given, and serves files from
  exactly the directory `--root` names. A request for `/` is served the file
  `--index` names. A request for a path that is not in the root gets the
  response body `Not Found!`.
* `quiche-client` writes each response body to stdout and nothing else, and
  exits 0 on success. With `-n N` it sends the request N times and writes N
  bodies. The N requests are in flight together, so the bodies reach stdout in
  completion order and may interleave; `--dump-responses DIR` is how a caller
  recovers each response whole. It writes one file per request into `DIR`,
  named after the last path segment of the URL, with `.2`, `.3` and so on
  appended for the second and later requests — `payload.bin`, `payload.bin.2`,
  `payload.bin.3` — and writes nothing to stdout while it is in use.
* Both take `--dump-packets DIR` and write one raw file per datagram received
  into it, before any header of that datagram is parsed: the file holds the
  bytes exactly as they came off the socket. **The file name is part of the
  contract** — `<n>.pkt`, where `<n>` is a decimal counter rising in arrival
  order from 0 — because the checker orders the directory by parsing that stem
  as a number and reads the datagrams back in the order they arrived. A hex
  stem, a `packet-<n>` prefix, a timestamp, or one file overwritten per
  datagram leaves it with nothing to order, and every flag this group checks
  by parsing dumped bytes (`--no-retry`, `--wire-version`) fails with it.
* Both honour the environment variable `QLOGDIR`; see the next element.

### `the qlog crate and the JSON-SEQ trace format`

`qlog/` is a crate of its own at version 0.6, and the trace it writes is a JSON
Text Sequence. `Connection::set_qlog` and `Connection::set_qlog_with_level` are
both behind the library's optional `qlog` feature:

```rust
#[cfg(feature = "qlog")]
pub fn set_qlog(&mut self, writer: Box<dyn std::io::Write + Send + Sync>,
                title: String, description: String);

#[cfg(feature = "qlog")]
pub fn set_qlog_with_level(&mut self, writer: Box<dyn std::io::Write + Send + Sync>,
                           title: String, description: String,
                           qlog_level: QlogLevel);

pub enum QlogLevel { Core, Base, Extra }
```

`set_qlog` is `set_qlog_with_level(.., QlogLevel::Base)`.

Both binaries wire this to `QLOGDIR`: when that environment variable names a
directory, each connection gets a file in it called `<role>-<scid hex>.sqlog`
with `role` being `client` or `server`. The role prefix is load-bearing: it is
how the checker tells the two traces apart, and it requires exactly one file per
role in the directory.

**The file must be there, and written, while the connection is still live.** The
checker reads the trace after the client's request has completed but before it
stops the server, so the records have to be flushed as the connection runs —
a writer that accumulates the whole trace in memory and writes it out when the
process exits leaves the checker looking at an empty directory. Flushing at
each event, or at each connection state change, both satisfy this; so does
writing the header at connection creation, which is what makes the file exist
at all.

The file is a sequence of records. Each record is the byte `0x1e` (ASCII record
separator), then one line of JSON, then `\n`. The first record is the log
header:

```json
{"qlog_version":"0.3","qlog_format":"JSON-SEQ","title":"...",
 "description":"...",
 "trace":{"vantage_point":{"type":"server"},"title":"...","description":"...",
          "configuration":{"time_offset":0.0}}}
```

Every record after it is one event with three fields — `time`, `name`, `data` —
where `name` is `"<category>:<event>"`. A connection that completes a request
at `QlogLevel::Base` produces events drawn from exactly this set:

| name | when |
| --- | --- |
| `transport:parameters_set` | once for `"owner":"local"` and once for `"owner":"remote"` |
| `transport:packet_sent` | per packet sent |
| `transport:packet_received` | per packet received |
| `transport:data_moved` | per stream read or write |
| `recovery:metrics_updated` | when the congestion or RTT state changes |

The `"owner":"local"` `parameters_set` event carries the local transport
parameters under their RFC 9000 names — `max_idle_timeout`,
`initial_max_data`, `initial_max_stream_data_bidi_local`,
`initial_max_stream_data_bidi_remote`, `initial_max_stream_data_uni`,
`initial_max_streams_bidi`, `initial_max_streams_uni`, `max_udp_payload_size`,
`ack_delay_exponent`, `max_ack_delay`, `active_connection_id_limit`,
`disable_active_migration` — with the values the process is actually using, so
`--max-data 3000000` on `quiche-server` shows up as
`"initial_max_data":3000000`.

A `transport:packet_sent` or `transport:packet_received` event carries the
packet's frames as `data.frames`, an array of objects. Each object names its
frame in a **`frame_type`** field whose value is the frame's RFC 9000 name in
`snake_case` — `padding`, `ping`, `ack`, `crypto`, `stream`,
`max_stream_data`, `handshake_done`, `connection_close`, and so on — and
carries that frame's own fields alongside it under their RFC 9000 names, with
one substitution worth stating because it is not mechanical: the flow-control
limit RFC 9000 calls *Maximum Stream Data* is written **`maximum`**, not `max`
and not `maximum_stream_data`. A MAX_STREAM_DATA frame for stream 0 is
therefore logged as

```json
{"frame_type":"max_stream_data","stream_id":0,"maximum":1048576}
```

and a STREAM frame as
`{"frame_type":"stream","stream_id":0,"offset":0,"length":1200}`.

A group that watches the receive window grow reads the successive `maximum`
values out of the `max_stream_data` objects for one `stream_id`, so a trace
that logs the frames under any key other than `data.frames`, tags them under
anything other than `frame_type`, or names the limit anything other than
`maximum`, is unreadable to that group even though the connection itself
behaved correctly.

### `the qlog crate's Rust API`

The trace format above is what the checker reads off disk. This is what it
*compiles* against: at a later step the checker writes a program of its own,
declaring `qlog = { path = "<root>/qlog" }` as its only dependency, and
imports the crate's items by name. Those names are therefore part of this
step's contract from the moment the crate exists, and they are pinned here
with the shapes that program uses:

```rust
// crate root
pub const QLOG_VERSION: &str = "0.3";

pub struct TraceSeq { /* .. */ }
impl TraceSeq {
    pub fn new(vantage_point: VantagePoint, title: Option<String>,
               description: Option<String>, configuration: Option<Configuration>,
               common_fields: Option<CommonFields>) -> Self;
}

pub struct VantagePoint {
    pub name: Option<String>,
    pub ty: VantagePointType,
    pub flow: Option<VantagePointType>,
}

pub enum VantagePointType { Client, Server, Network, Unknown }

pub struct Configuration {
    pub time_offset: Option<f64>,
    pub original_uris: Option<Vec<String>>,
}

pub struct CommonFields { /* .. */ }

// qlog::streamer
pub struct QlogStreamer { /* .. */ }
impl QlogStreamer {
    pub fn new(qlog_version: String, title: Option<String>,
               description: Option<String>, summary: Option<String>,
               start_time: std::time::Instant, trace: TraceSeq,
               log_level: EventImportance,
               writer: Box<dyn std::io::Write + Send + Sync>) -> Self;
    pub fn start_log(&mut self) -> Result<()>;
    pub fn add_event(&mut self, event: Event) -> Result<()>;
    pub fn finish_log(&mut self) -> Result<()>;
}

// qlog::events
pub struct Event { /* .. */ }
impl Event {
    pub fn with_time(time: f32, data: EventData) -> Self;
}

pub enum EventImportance { Core, Base, Extra }

pub enum EventData { /* one variant per schema event, each wrapping that
                       event's own struct, .. */ }

// qlog::events::quic
pub struct MetricsUpdated {
    pub min_rtt: Option<f32>,
    pub smoothed_rtt: Option<f32>,
    pub latest_rtt: Option<f32>,
    pub rtt_variance: Option<f32>,
    pub pto_count: Option<u16>,
    pub congestion_window: Option<u64>,
    pub bytes_in_flight: Option<u64>,
    pub ssthresh: Option<u64>,
    pub packets_in_flight: Option<u64>,
    pub pacing_rate: Option<u64>,
}
```

`EventImportance` is the crate's own level enum, and the library's `QlogLevel`
above is the wrapper `Connection::set_qlog_with_level` takes: the two map onto
each other one-for-one. `add_event` writes one `Event` and returns
`Err(Error::Done)` -- the crate's `Error` has a `Done` variant for exactly
this -- when the event's importance is not contained in the streamer's
`log_level`, which is how a streamer reports that it filtered an event out.
`EventData` is the enum of event bodies, one variant per schema event;
`qlog::events::quic::MetricsUpdated` is the variant behind this milestone's
`recovery:metrics_updated` record and carries those ten `Option` fields by
those names.

The consumer that matters is a separate program, built outside your workspace
against `qlog/` by path, so a crate that serialises the right JSON but names or
shapes these differently fails that program's build rather than any assertion
in it.

### `the autotuning receive window`

The receiver's flow control window is no longer fixed at the advertised initial
limit. It starts there and grows when the application is draining it faster
than the limit is being extended:

* the receiver extends its limit — `MAX_DATA`, `MAX_STREAM_DATA` — when the
  unused part of the current limit falls below half the current window;
* if the previous extension was less than two round trips ago, the window is
  doubled first, so the new limit is `consumed + 2 * window` instead of
  `consumed + window`;
* the window never exceeds the configured maximum.

Two new `Config` setters supply those maxima, and `apps/` exposes them as
`--max-window` and `--max-stream-window`:

```rust
impl Config {
    /// Sets the maximum connection-level flow control window.
    pub fn set_max_connection_window(&mut self, v: u64);
    /// Sets the maximum per-stream flow control window.
    pub fn set_max_stream_window(&mut self, v: u64);
}
```

Setting the maximum equal to the advertised initial limit pins the window and
restores the previous milestone's behaviour exactly. The connection window is
additionally kept at or above a multiple of the largest stream window, so a
grown stream window is never throttled by an unmoved connection limit.

One thing about the sender's side of the same mechanism changes with it. A
`stream_send` that can place no bytes because the stream has no capacity left
now reports that as `Err(Error::Done)`; the previous milestone returned
`Ok(0)`. `Ok(0)` is reserved for the caller that passed a zero-length buffer.

### `a reset stream is reported, not just stopped`

`Error` gains one variant, carrying the peer's application error code:

```rust
pub enum Error {
    // ...
    /// The specified stream was stopped by the peer.
    StreamStopped(u64),
    /// The specified stream was reset by the peer.
    StreamReset(u64),
    // ...
}
```

`StreamReset` maps to the C error value `-16`. After the peer sends
`RESET_STREAM` for a stream, that stream is offered to the application through
`readable()` one more time — that is how the reset reaches it — and
`Connection::stream_recv` on it returns `Err(Error::StreamReset(code))`
carrying the frame's error code. The reset is delivered once in the sense that
matters: the stream then drops out of `readable()` and does not come back,
however much more is exchanged. The error itself is sticky rather than
one-shot, so a caller that asks the stream again keeps getting the same
`Err(Error::StreamReset(code))` rather than data or `Error::Done`. Reading a
stream the peer has merely finished still returns the data and then `fin`.

HTTP/3 surfaces the same thing as an event:

```rust
pub enum Event {
    Headers { list: Vec<Header>, has_body: bool },
    Data,
    Finished,
    /// Stream was reset. The associated data is the peer's error code.
    Reset(u64),
    Datagram,
    GoAway,
}
```

`h3::Connection::poll` returns `Ok((stream_id, Event::Reset(code)))` for a
stream the peer reset, in place of the `Finished` event it would otherwise have
produced. No group at this step checks the HTTP/3 event; it is described here
because it is part of the milestone and later steps build on it.

### `the ACK frame's ECN counts`

Frame type `0x03` — the ACK frame with an ECN Counts section — is a frame this
milestone parses. RFC 9000 Section 19.3.2 puts three varints after the ACK
ranges, in this order:

```text
ACK Frame {
  Type (i) = 0x02..0x03,
  Largest Acknowledged (i),
  ACK Delay (i),
  ACK Range Count (i),
  First ACK Range (i),
  ACK Range (..) ...,
  [ECN Counts (..)],          -- present if and only if Type == 0x03
}
ECN Counts {
  ECT0 Count (i),
  ECT1 Count (i),
  ECN-CE Count (i),
}
```

The low bit of the frame type is what selects the section: `0x02` has no ECN
counts, `0x03` has all three. A receiver must consume all three varints and go
on to the next frame in the packet. What it then does with the values is out of
scope, and this is worth stating plainly because the obvious guess is wrong:
at this milestone the reference does nothing with them at all. The receive
path destructures the ACK frame without binding the counts, the recovery
module has no ECN state to put them in, and no field of `Stats` reports them.
Consuming the three varints so that the rest of the packet still parses is the
whole of the behaviour this milestone adds, and it is the whole of what the
group can observe -- which is why the group observes it through the frame that
*follows* the counts rather than by reading a counter back out.

Before this milestone `0x03` was not a known frame type, so a packet carrying
one was a connection error: `Error::InvalidFrame`, and a `CONNECTION_CLOSE`
with transport error code `0x07` (`FRAME_ENCODING_ERROR`). That is the
behaviour that changes.

The group also reads the connection's own state after the packet, through two
accessors **by name**, so both have to exist on `Connection` with these shapes:

```rust
pub fn is_closed(&self) -> bool
pub fn is_draining(&self) -> bool
```

`is_draining()` is `true` while the connection is in the draining period that
follows a received `CONNECTION_CLOSE` -- no new application data can be sent or
received, the object cannot yet be dropped, and `is_closed()` is still `false`
-- and `false` on a live connection. A packet that parses into frames the
connection can process must leave both `false`; a stack that keeps the previous
milestone's answer, a connection error, reports one of them `true` and fails
the group. `is_closed()` is inherited from step 01; `is_draining()` has been
part of the `Connection` surface since step 04 and is checked here for the first
time.

### `quiche::Stats`

`Connection::stats()` returns a plain struct with public fields, and this
release widens it: alongside the counters it already had, `Stats` now mirrors
the peer's transport parameters. No group at this step reads `Stats` -- the
ECN section above turns on the absence of an ECN counter -- but the fields are
part of the surface a later step's checker destructures exhaustively, and the
`peer_*` spellings are what it compiles against, so they are written out here
rather than left to be inferred:

```rust
pub struct Stats {
    pub recv: usize,
    pub sent: usize,
    pub lost: usize,
    pub retrans: usize,
    pub rtt: std::time::Duration,
    pub cwnd: usize,
    pub sent_bytes: u64,
    pub recv_bytes: u64,
    pub lost_bytes: u64,
    pub stream_retrans_bytes: u64,
    pub pmtu: usize,
    pub delivery_rate: u64,
    pub peer_max_idle_timeout: u64,
    pub peer_max_udp_payload_size: u64,
    pub peer_initial_max_data: u64,
    pub peer_initial_max_stream_data_bidi_local: u64,
    pub peer_initial_max_stream_data_bidi_remote: u64,
    pub peer_initial_max_stream_data_uni: u64,
    pub peer_initial_max_streams_bidi: u64,
    pub peer_initial_max_streams_uni: u64,
    pub peer_ack_delay_exponent: u64,
    pub peer_max_ack_delay: u64,
    pub peer_disable_active_migration: bool,
    pub peer_active_conn_id_limit: u64,
    pub peer_max_datagram_frame_size: Option<u64>,
}
```

The thirteen `peer_*` fields are the peer's advertised transport parameters --
the ones the peer sends a value for, named from the peer's side, so they report
what the *other* endpoint configured. `peer_disable_active_migration` is a
`bool`, `peer_max_datagram_frame_size` is an `Option<u64>` because DATAGRAM is
negotiated rather than defaulted, and the other eleven are `u64`.

### `HTTP/3 settings: the rename and the raw view`

The SETTINGS identifier `0x6` keeps its wire encoding and its meaning and
changes its Rust name to the one RFC 9114 uses:

```rust
// before (0.9.0)
impl h3::Config { pub fn set_max_header_list_size(&mut self, v: u64); }
// after (0.11.0)
impl h3::Config { pub fn set_max_field_section_size(&mut self, v: u64); }
```

Exceeding a peer's advertised limit still raises `h3::Error::ExcessiveLoad`,
which is HTTP/3 error `0x107`.

Beside it, the peer's SETTINGS frame becomes readable exactly as it arrived:

```rust
impl h3::Connection {
    /// Gets the raw settings from peer including unknown and reserved types.
    ///
    /// The order of settings is the same as received in the SETTINGS frame.
    pub fn peer_settings_raw(&self) -> Option<&[(u64, u64)]>;
}
```

`None` before a SETTINGS frame has been received; afterwards every
`(identifier, value)` pair in receipt order, including identifiers this
implementation does not recognise. Of the identifiers this implementation
recognises by number, `0x6` (`MAX_FIELD_SECTION_SIZE`, above) still takes
effect; `0x1` (`QPACK_MAX_TABLE_CAPACITY`) and `0x7` (`QPACK_BLOCKED_STREAMS`)
are stored and returned like any other identifier but this quiche version has
no QPACK dynamic table implementation, so neither drives any further
observable behaviour through the public API. Unknown identifiers are still
ignored, and the HTTP/2-reserved identifiers `0x0`, `0x2`, `0x3`, `0x4` and
`0x5` are still a connection error (`h3::Error::SettingsError`).

### `quiche::h3::Header, quiche::h3::HeaderRef and quiche::h3::NameValue`

```rust
pub trait NameValue {
    fn name(&self) -> &[u8];
    fn value(&self) -> &[u8];
}

pub struct Header(Vec<u8>, Vec<u8>);      // the fields are private; construct with `new`
impl Header {
    pub fn new(name: &[u8], value: &[u8]) -> Self;
}
impl NameValue for Header { .. }

pub struct HeaderRef<'a>(&'a [u8], &'a [u8]);
impl<'a> HeaderRef<'a> {
    pub fn new(name: &'a [u8], value: &'a [u8]) -> Self;
}
impl<'a> NameValue for HeaderRef<'a> { .. }
```

`Header` and `HeaderRef` derive `Clone`, `Debug` and `PartialEq`; neither
exposes its fields. `Header::new` **copies the bytes it is given, with no
normalisation of any kind** — no lower-casing, no validation. Every name and
value the harness passes is already in the form the wire wants (the
pseudo-headers are lower-case literals), so nothing at this step observes what
your `new` does to casing; what it does observe is that the bytes arrive at the
peer unchanged and in order.

The harness uses all of this: it builds every request and response it sends
with `Header::new(b":method", b"GET")`-style calls, reads the field lines back
through `NameValue`, and collects them into the `Vec<Header>` that
`send_request` and `send_response` take. The trait has to be importable **by
that name** — `use quiche::h3::NameValue;` — because that is the import the
harness writes.

## The groups at this step

One row per group. **gate** groups are all-or-nothing: a failure means the step is not satisfied; **requirement** groups cover this step's own new capability. The rows are grouped by tier for reading; the order they run in, and each one's result, is what `reward_details.json` records.

| group | tier | surface | what it observes |
| --- | --- | --- | --- |
| `candidate-workspace-builds` | gate | T4 | The one check every step shares: `cargo build --offline` (default targets -- libraries and binaries only, not tests or benches) must complete for the workspace at `/app` within the harness's build timeout, before any other group in this table runs. It is the one group every step carries identically, and no step declares it as a capability of its own -- but it is not optional either: one failing gate group, including this one, means the step is not satisfied, however much of the new work is right. |
| `cap-workspace-layout` | requirement | T4 | The shape of the produced tree, read structurally and not searched for identifiers. The repository root must carry a workspace manifest whose `members` list names three directories and whose `exclude` list names `fuzz` and `tools/http3_test` — two real, nested crates that are not workspace members and would otherwise make cargo warn about an unlisted manifest; `quiche/Cargo.toml` must declare a package called `quiche` whose `version` field reads exactly `0.11.0` — the version is parsed out and compared, not skimmed past — with its library root at `quiche/src/lib.rs`; `qlog/Cargo.toml` must declare a separate package called `qlog`; `apps` must declare the package `quiche_apps`; and there must be no `src/lib.rs` at the repository root, because the crate moved rather than being copied. The workspace's shared `target/debug/` must then hold two executables named `quiche-client` and `quiche-server`. That last check is also where the previous three milestones' no-argument `examples/http3-server` stops mattering, and it is worth being exact about how. The example *source* is still in the tree — it moved with the crate, to `quiche/examples/http3-server.rs`, and deleting it is not required. What changed is that no group resolves it by name any more: the T1 groups at this step ask for `quiche-server`, so a tree that still builds the example but does not produce `quiche-server` fails here and at every T1 group, while a tree that produces both is fine. The checker reads the manifests with a TOML parser and checks the files on disk; it does not grep any source file. |
| `cap-apps-cli-flags` | requirement | T1 | Two of the candidate's own processes on loopback UDP, configured entirely from the command line. The checker picks a free ephemeral port, writes a document root under its own scratch directory containing a 24 KiB generated body and a differently named index file, and starts `quiche-server --listen 127.0.0.1:PORT --cert ... --key ... --root ... --index ...`. It then requires that the server bound exactly that port; that `quiche-client --no-verify https://127.0.0.1:PORT/payload.bin` exits 0 having written the 24 KiB body to stdout byte for byte, with no framing and no headers; that the same client with `-n 3 --dump-responses DIR` exits 0 having written nothing at all to stdout and leaving exactly three files in `DIR`, named `payload.bin`, `payload.bin.2` and `payload.bin.3`, each of them the whole 24 KiB body; that `https://127.0.0.1:PORT/` is answered with the contents of the file `--index` named rather than with `index.html`; and that a path outside the root is answered `Not Found!` without killing the server. Every one of those facts is a flag the previous milestone's binaries had no way to accept. The three responses are checked through `--dump-responses` rather than on stdout because the three requests are in flight together and their bodies interleave there. Four more flags are checked the same way, each against its own fresh server/client pair: `--dump-packets DIR` must leave one raw file per incoming datagram in `DIR`, on both the server started above and a client started below, before any header of that datagram is even parsed -- checked by requiring at least one dumped file to still carry the long-header form bit every Initial packet sets. `--no-retry` is checked by parsing the raw long-header bytes `--dump-packets` leaves behind rather than by reading any process's own output: without the flag, a token-less Initial must be met with a genuine, real-version Retry packet (type bits `0b11` at RFC 9000's version-independent header offset); with it, no such packet may appear anywhere in what the client received. `--wire-version` is checked the same way in the other direction: the client's own default (`babababa`) is a reserved "grease" version that always trips version negotiation first, so an explicitly chosen, distinct grease value must be the very first version the server ever receives. `--session-file PATH` is checked by requiring the named file to exist and be non-empty after a connection completes, since a client that never called into that TLS-session-caching path would leave nothing there. `-H`/`--header` is deliberately out of scope at this group and at this step: `build_h3_response` (the reference server's only request-header consumer) reads exactly `:scheme`, `:authority`/`host`, `:path`, `:method` and `priority` and silently drops every other header, so a header this flag adds has no server-observable effect through any channel a group here can reach short of decrypting and QPACK-decoding the request -- `--dump-packets` captures ciphertext, and no process's own stdout/stderr/qlog output echoes request headers back. Building that decode capability for one low-priority CLI flag would be a materially larger verifier than everything else in this group put together. |
| `cap-qlog-jsonseq` | requirement | T4 | The trace file a real connection leaves behind, parsed as JSON Text Sequences. The checker runs a request against the candidate's own server with `QLOGDIR` pointing at an empty directory, then requires: exactly one `server-*.sqlog` and one `client-*.sqlog`; every record in each file prefixed with the byte `0x1e` and parsing as a single JSON object; a first record whose `qlog_version` is `"0.3"`, whose `qlog_format` is `"JSON-SEQ"` and whose `trace.vantage_point.type` is `"server"` on the server side and `"client"` on the client side; every later record carrying exactly the keys `time`, `name` and `data`; and the set of `name` values being exactly `transport:parameters_set`, `transport:packet_sent`, `transport:packet_received`, `transport:data_moved` and `recovery:metrics_updated` — no more, no fewer. It then requires that the `parameters_set` record with `"owner":"local"` carries the four values the server was given on its command line (`--max-data`, `--max-stream-data`, `--max-streams-bidi`, `--idle-timeout`) under their RFC 9000 names, which is what makes this a statement about a trace of the connection that happened and not about a template. The same record is also required to carry the correct value for every other field the local transport parameters expose: `initial_max_stream_data_uni` and `initial_max_streams_uni` (the apps binary feeds these from `--max-stream-data`/its own default, respectively), `max_udp_payload_size` (the 1350-byte constant every quiche-apps binary hardcodes), `ack_delay_exponent`, `max_ack_delay` and `active_connection_id_limit` (quiche's own built-in transport-parameter defaults), and `disable_active_migration` (hardcoded `true` by the apps binary). The `parameters_set` record with `"owner":"remote"` -- the server's report of what quiche-client sent it -- is held to the same standard: since the client in this connection is launched with none of the matching CLI overrides, every one of these fields is checked against quiche-client's own docopt default or the same apps/quiche constant, not merely checked for presence or counted. No substring search is used anywhere. A second connection, this time with the client started under a tiny, pinned `--max-stream-window` (and a matching small `--max-stream-data`) against a body many times that size, requires that the client's own trace show at least ten MAX_STREAM_DATA extensions on the response stream and that no single extension grow the offered window by more than four times the configured ceiling. `--max-stream-window` and `--max-stream-data` are CLI-only plumbing on top of the `Config` receiver-window and stream-window setters -- those setters are already exercised in-memory by `cap-stream-window-autotune` -- so this is the check that these two flags actually reach them, rather than being silently ignored in favour of the built-in defaults. `--max-window`, the connection-level sibling, is parsed by the binaries but is passed by no group at this step, so nothing here observes whether it is wired up. |
| `cap-ecn-ack-frame` | requirement | T2 | The checker builds and encrypts a 1-RTT packet of its own and requires the candidate's server to read it. Packet protection is entirely `quiche_verifier_support`'s: the client's application traffic secret is taken out of the candidate through the standard NSS keylog hook, the AEAD, the nonce and the header-protection mask are the verifier's, and the verifier confirms its keys are right by first decrypting one of the candidate's own 1-RTT packets with them. The payload is two frames: an ACK frame of type `0x03` acknowledging packet 0 with ECN counts `(ECT0, ECT1, CE) = (7, 11, 3)`, followed by a STREAM frame carrying 64 generated bytes on stream 8. Feeding that packet to the server must return `Ok`, and `stream_recv` on stream 8 must then yield those 64 bytes exactly — which is only possible if all three ECN varints were consumed and the parser arrived at the frame after them. The same packet at the previous milestone is `Error::InvalidFrame` and a `FRAME_ENCODING_ERROR` connection close, so the group is also checked in the other direction. The verifier drains every packet the server then wants to send, decrypts each one with its own keys and decodes the frames itself: none of them may be a `CONNECTION_CLOSE`, one of them must carry an ACK range covering the injected packet number, and the server must be neither closed nor draining afterwards -- read through `Connection::is_closed()` and `Connection::is_draining()`, both of which must return `false`. The drain is done by hand rather than by exchanging with the pair, because the injected packet carries a packet number the pair's own client never sent and the resulting ACK is legitimately a protocol violation at that client. |
| `cap-stream-window-autotune` | requirement | T3 | Three in-memory pairs that differ in exactly one setting. All three receivers advertise `initial_max_stream_data_bidi_remote = 1024` and are given a connection-level limit far too large to ever be the thing that bites; they are given `set_max_stream_window(1024)`, `(2048)` and `(8192)` respectively. Each runs eight identical rounds of "offer the sender the whole buffer, exchange, drain the receiver completely", and the checker records how many bytes the sender was allowed to place in each round. The pinned receiver must hand out exactly 1024 bytes in each of the eight rounds — the limit advances by one window each time, which is the previous milestone's behaviour and the floor this group holds the candidate to. For the other two the first round must still be 1024, the allowance must never go down, it must never exceed the maximum that receiver was configured with, it must reach that maximum before the eighth round, and across the whole run it must take only the values on the doubling ladder between the two — {1024, 2048} for the first and {1024, 2048, 4096, 8192} for the second, and no value in between. Raising the maximum must move strictly more data in the same eight rounds, and the 8192 receiver must move at least four times what the pinned one moved. A receiver that ignores `set_max_stream_window` fails the pinned run and the never-exceeds bound; a receiver that never grows the window fails the ladder and the reaches-its-maximum bound; a receiver that grows the window by something other than doubling fails the ladder. The window is deliberately small enough that the initial congestion window can never be the limiter, so every number in the run is a statement about flow control. A fourth, separate run repeats the 8192 case but pins the connection-level ceiling to exactly that same 8192 instead of to the oversized value the first three runs use — the tightest ceiling a single-stream connection could need. It must reach the same {1024, 2048, 4096, 8192} ladder and the same eighth-round maximum as the third run: the connection window, kept at or above the largest stream window in play, must not throttle a stream window that grows up to and stays within it. |
| `cap-stream-reset-error` | requirement | T3 | What a reader learns when the writer resets the stream. The checker has the client send 100 bytes on stream 0 without finishing it, lets the server read them, then has the client reset the stream with application error code `0xbeef` and exchanges again. The reset stream must first be offered to the application as readable — that is the path the reset travels — and `stream_recv` on it must then return `Err(Error::StreamReset(0xbeef))`, the exact variant carrying the exact code. The group also requires that the reset is reported once rather than repeatedly: having handed the reset over, the stream must drop out of `readable()` and must not come back into it however much more is exchanged. Asking the stream again must keep returning the same `Error::StreamReset(0xbeef)` rather than data or `Error::Done`. Finally a second, untouched stream on the same connection must still deliver its data and its `fin`, and neither endpoint may close. At the previous milestone there is no such variant and a reset stream simply goes quiet. |
| `cap-h3-peer-settings-raw` | requirement | T3 | The peer's SETTINGS frame, read back exactly as it arrived. The checker drives the candidate's transport `Connection` directly on the client side — no HTTP/3 connection there — and writes an HTTP/3 control stream by hand onto client uni-directional stream 2: the stream type varint `0x00`, then a SETTINGS frame carrying four pairs in this order, one of them an identifier no implementation recognises: ```text (0x01, 4096) QPACK_MAX_TABLE_CAPACITY (0x1f * 3 + 0x21, 0xdead) a reserved "grease" identifier (0x06, 1024) MAX_FIELD_SECTION_SIZE (0x07, 12) QPACK_BLOCKED_STREAMS ``` The server side is a real `h3::Connection`. After the exchange, `peer_settings_raw()` must return `Some` of exactly those four pairs in exactly that order — the unknown identifier included and not filtered out, the order preserved rather than sorted or canonicalised. The group also requires that polling the server raises no error, because an unrecognised SETTINGS identifier must be ignored rather than rejected, and that `peer_settings_raw()` was `None` before the frame arrived. A "grease" identifier is not the only kind of unrecognised one: the HTTP/2- reserved identifiers (`0x0`, `0x2`, `0x3`, `0x4`, `0x5`) are still a connection error under RFC 9114 Section 7.2.4.1. On a second, independent pair — SETTINGS may only be sent once per control stream — the checker writes a control stream carrying only `(0x02, 1)` and requires the server's poll to raise exactly `h3::Error::SettingsError`. Separately, this milestone renames `set_max_header_list_size` to `set_max_field_section_size` on `h3::Config`, and the rename is not merely cosmetic: on a third pair, built with the server's own `h3::Config` carrying a small `set_max_field_section_size` limit, a request whose header block exceeds that limit must still raise `h3::Error::ExcessiveLoad` when the server polls it, exactly as `set_max_header_list_size` did at the previous milestone. Of the three identifiers this implementation recognises by number (`0x1`/QPACK_MAX_TABLE_CAPACITY, `0x6`/MAX_FIELD_SECTION_SIZE and `0x7`/QPACK_BLOCKED_STREAMS), only `0x6` has an effect reachable through the public API at this milestone: this quiche version has no QPACK dynamic table implementation, so `0x1` and `0x7` are accepted, stored and returned by `peer_settings_raw()` exactly like any other identifier but drive no further observable behaviour. That is why this group's functional-effect case is built around `0x6` specifically rather than a generic "recognised identifiers take effect". |
| `reg-01-h3-get` | gate | T1 | A 64 KiB generated body still crosses the candidate's own HTTP/3 server and client, byte for byte, with the client exiting 0. The programs are different programs now — `quiche-server --listen/--cert/--key/--root` and `quiche-client --no-verify <url>` in place of the no-argument `examples/http3-server` — so this is the group that catches a workspace reshuffle that moved the code but broke the request path. Both are told `--http-version HTTP/3` explicitly, so that a candidate whose default happens to be HTTP/0.9 is still asked the question step 01 asked. |
| `reg-01-flow-control` | gate | T3 | A 64 KiB transfer through a 4096-byte stream window still takes many rounds and still arrives intact. The window is pinned with `set_max_stream_window(4096)` so that the claim is the same claim step 01 made — the receiver extends its limit by one window at a time and the sender respects it — rather than a claim about the autotuner this step adds. The group requires the first `stream_send` to accept exactly 4096 bytes and no more, the capacity to read 0 while those bytes are in flight, a further `stream_send` at that point to place nothing and say so with `Err(Error::Done)`, every subsequent round to be offered no more than one window, the transfer to need at least 16 rounds, and all 65536 bytes to arrive in order. |
| `reg-02-duplicate-push-removed` | gate | T5 | The three bytes `0e 01 ff` — the frame type DUPLICATE_PUSH carried at draft-27, a length of 1 and one payload byte — written by hand onto an HTTP/3 request stream ahead of a real HEADERS frame. The expected outcome is a silent discard: the unknown frame is skipped, the HEADERS frame that follows it on the same stream is delivered with its `:path` intact, and neither endpoint closes the connection. The frame type is still undefined at 0.11.0, so the answer is still "ignore it", and the group is here because this step rewrites the crate's directory layout and its HTTP/3 settings table around the frame parser that has to keep doing that. |
| `gate-endtoend` | gate | T1 | The whole stack, as a user of it would drive it. One `quiche-server` on a free ephemeral port with a certificate, a key and a document root the checker chose, and then two separate `quiche-client` invocations against it: one for a 96 KiB generated body, which must arrive byte for byte on stdout with exit status 0, and one for `/`, which must be answered with the index file. The server must still be running afterwards. The client is not told which HTTP version to use, so this group is satisfied by whichever one the two ends negotiate between themselves. If the workspace does not build both binaries, if the flags are not honoured, if the handshake does not complete or if the stack cannot move a body of that size over the protocol it chose, this group fails and it is the first thing to look at. |

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

`/app` contains everything step 05 left behind, unchanged. The milestone before this one is modelled on quiche 0.9.0; this one is modelled on 0.11.0. Read `WORKSPACE_HANDOFF.md` for what is already there and what must keep working.

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

