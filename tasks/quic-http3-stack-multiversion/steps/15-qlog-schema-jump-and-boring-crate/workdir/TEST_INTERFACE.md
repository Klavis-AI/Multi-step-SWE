# Test interface — step 15, A qlog schema jump, a crypto backend that comes from crates.io, and five smaller additions

This is the contract between your implementation and the checker. Every
name, path and flag the checker depends on is written down here; nothing
outside this document is assumed about your code. Where the document and
your intuition disagree, the document wins — it is what the tests were
written against.

Generated, together with this step's interface contract, from one source.
The two cannot disagree.

## What this step is

One release, two format changes that break every consumer of the logs, a
crypto backend that stops being vendored, and five additions to the connection
and HTTP/3 surfaces.

**The qlog file schema changes, and so does every event name.** The log format
this package has written since milestone 06 was qlog as it stood in the early
drafts: a header record that named its `qlog_format` and `qlog_version`, and
event names spread over seven categories -- `connectivity:`, `security:`,
`transport:`, `recovery:`, `http:`, `qpack:` and `generic:`. The schema is now
URI-identified. The header record declares a `file_schema` of
`urn:ietf:params:qlog:file:sequential`, the trace it carries declares the event
schemas a reader should expect, and the categories collapse: the first four
become `quic:`, `http:` becomes `http3:`, and `generic:` becomes `loglevel:`.

Both halves are wire format. A tool that reads the candidate's logs dispatches
on the header to decide how to parse and on the event name to decide what it is
looking at, so a rename that stops at the Rust type names changes nothing that
matters.

**BoringSSL arrives as a dependency rather than as a submodule.** The
transport crate has carried its own copy of BoringSSL at `quiche/deps/boringssl`
since the first milestone, registered in `.gitmodules`, compiled by the build
script with `cmake`, and selected by a `boringssl-vendored` feature. That
arrangement is gone. The default feature is now `boringssl-boring-crate`, the
crypto comes from the `boring` and `boring-sys` packages as ordinary
dependencies resolved from the registry, and the submodule, the directory and
the `cmake` build dependency all disappear with it. `boringssl-vendored` and
`openssl` are no longer features of the crate.

**Five additions.** `h3::Config::new()` stops leaving the field section limit
unset and defaults it to 32768 octets, so an endpoint that was never configured
now advertises a limit. `dgram_send_buf` and `dgram_recv_buf` move owned
buffers in and out of the DATAGRAM queues instead of copying through a slice.
`stream_readable_len` answers how much of a stream can be read right now.
`stream_closed` answers whether a stream is finished in both the directions
that apply to it. And `Config::set_curves_list` hands a TLS group preference
list to the backend.

## The flip

Something an earlier step established changes meaning here. This is the
part a candidate most often gets half-right: the new behaviour appears,
the old one is left working beside it, and both the new group and the
inherited group pass for the wrong reason — until the step that
requires the old behaviour to be *gone*.

The event names are the format. Renaming the Rust variants without renaming the
serialized strings leaves every reader working and every log unchanged, which
is the one outcome this milestone is not. The rename is also not uniform: three
namespaces collapse into `quic:`, but HTTP/3's events go to `http3:` and the
log-level events go to `loglevel:`, so a search-and-replace that puts
everything under `quic:` is as wrong as one that changes nothing.

The header record changes shape, not just content. It no longer carries
`traces`, because a sequential file has exactly one trace and carries it as
`trace`; it declares `file_schema` rather than a format-and-version pair; and
the trace declares `event_schemas`, which is what makes the collapsed event
names readable at all -- without them a reader cannot tell which revision of
the QUIC event definitions it is looking at.

The crypto backend is a build-system change with a runtime consequence. A
candidate who flips the feature in the transport crate and leaves `apps`
resolving the old vendored path still compiles, because the old feature simply
no longer exists and cargo does not error on a feature that is not requested.
The failure surfaces as two binaries that link different crypto and cannot
complete a handshake with each other.

The field section default is a default and not a constant. Code that writes
32768 into the SETTINGS frame unconditionally passes any check that only looks
at a fresh config, and breaks every application that chose its own limit.

`stream_readable_len` counts *contiguous* bytes. Data that arrived out of order
and sits behind a gap is buffered but not readable, and counting it would hand
a caller a number that the next `stream_recv` cannot honour.

## Interface elements the checker depends on

### `The qlog file schema, and the constants that name it`

The `qlog` crate exports the schema identifiers as public constants, and their
values are fixed by the qlog documents rather than chosen here:

```rust
pub const QLOGFILE_URI:    &str = "urn:ietf:params:qlog:file:contained";
pub const QLOGFILESEQ_URI: &str = "urn:ietf:params:qlog:file:sequential";
pub const SQLOG_EXT:     &str = ".sqlog";
pub const SQLOG_GZ_EXT:  &str = ".sqlog.gz";
pub const SQLOG_ZST_EXT: &str = ".sqlog.zst";
```

and, in `qlog::events`, the two event-schema URIs:

```rust
pub const QUIC_URI:  &str = "urn:ietf:params:qlog:events:quic-12";
pub const HTTP3_URI: &str = "urn:ietf:params:qlog:events:http3-12";
```

The sequential file is a JSON Text Sequence: each record is preceded by an
ASCII record separator (0x1e) and followed by a newline, which is unchanged
from the previous milestone. What changes is the first record, the header.

The header is the serialization of a `QlogSeq`:

```rust
pub struct QlogSeq {
    pub file_schema: String,
    pub serialization_format: String,
    pub title: Option<String>,
    pub description: Option<String>,
    pub trace: TraceSeq,
}
```

and `TraceSeq` gains the field that makes the collapsed event names legible:

```rust
pub struct TraceSeq {
    pub title: Option<String>,
    pub description: Option<String>,
    pub common_fields: Option<CommonFields>,
    pub vantage_point: Option<VantagePoint>,
    pub event_schemas: Vec<String>,
}
```

`TraceSeq::new` takes the five in that order, the last being the event schemas.

For a streamer created with a title, a description, and a trace built with
`vec![QUIC_URI, HTTP3_URI]`, the header record is an object whose keys are
exactly

```
description  file_schema  serialization_format  title  trace
```

with `file_schema` equal to `QLOGFILESEQ_URI`, `serialization_format` equal to
`"JSON-SEQ"`, and `trace.event_schemas` equal to
`["urn:ietf:params:qlog:events:quic-12", "urn:ietf:params:qlog:events:http3-12"]`
in that order. Three things that were there before are not: there is no
`traces` array, because a sequential file carries exactly one trace and carries
it as `trace`; there is no `qlog_format`/`qlog_version` pair, because the
schema URI replaced both; and there is no `configuration` object on the trace.

`QlogStreamer::new` takes seven arguments, in this order:

```rust
QlogStreamer::new(title, description, start_time, trace,
                  log_level, time_precision, writer)
```

`title` and `description` are `Option<String>`, `start_time` an
`std::time::Instant`, `trace` the `TraceSeq` above, `log_level` an
`EventImportance`, `time_precision` an `EventTimePrecision`, and `writer` a
`Box<dyn Write + Send>` (the probe passes a `Box::new(File)`). `start_log`,
`add_event_data_now` and `finish_log` keep their shapes: the last two return
`Result`, and `add_event_data_now` takes an `EventData`.

### `Event names: the namespaces collapse`

Every event's serialized name changes. The rule is not a single prefix
substitution:

| events that were in | are now in |
| --- | --- |
| `connectivity:` | `quic:` |
| `security:` | `quic:` |
| `transport:` | `quic:` |
| `recovery:` | `quic:` |
| `http:` | `http3:` |
| `generic:` | `loglevel:` |

The part after the colon does not change. `connectivity:mtu_updated` becomes
`quic:mtu_updated`, and `http:stream_type_set` becomes `http3:stream_type_set`
rather than `quic:stream_type_set`.

The case emits four events through a `QlogStreamer` and compares the
`name` of each serialized record against a fixed table:

```
connectivity:mtu_updated              ->  quic:mtu_updated
security:key_updated                  ->  quic:key_updated
recovery:congestion_state_updated     ->  quic:congestion_state_updated
http:stream_type_set                  ->  http3:stream_type_set
```

They are drawn from four different old categories on purpose: an
implementation that moved one namespace and left the others passes none of the
rest. The fourth is also the control for the opposite error -- it must land in
`http3:` and not in `quic:`.

Each event record is an object whose keys are exactly `data`, `name` and
`time`, with `time` a JSON number. The case also checks the body of the first
event, so that the name is known to be attached to the event it claims to be:
a `MtuUpdated { old: Some(1200), new: 1452, done: Some(true) }` serializes with
`old` 1200, `new` 1452 and `done` true.

The Rust variant names in `EventData` are already namespaced, and the cases
construct them by those names, so they are part of the surface too. **The module
paths they live at are part of it as well**, because a probe imports them by
path:

```rust
qlog::events::{EventData, EventImportance, HTTP3_URI, QUIC_URI}
qlog::events::quic::{CongestionStateUpdated, KeyType, KeyUpdated, MtuUpdated,
                     SpinBitUpdated}
qlog::events::http3::{StreamType, StreamTypeSet}
qlog::streamer::{EventTimePrecision, QlogStreamer}
qlog::{CommonFields, ReferenceTime, TraceSeq, VantagePoint, VantagePointType,
       QLOGFILESEQ_URI, QLOGFILE_URI, SQLOG_EXT, SQLOG_GZ_EXT, SQLOG_ZST_EXT}
```

The event modules are **`quic` and `http3`**, not `connectivity`, `h3`,
`qpack` or `security`: the release collapses the old per-category modules into
those two, and `EventData`'s variants are named after the module they now live
in. Besides the four `MtuUpdated`, `KeyUpdated`, `CongestionStateUpdated` and
`StreamTypeSet`, `EventData` carries a **fifth** variant the probe uses to prove
the schema runs on events this milestone did not touch: `QuicSpinBitUpdated`,
wrapping `quic::SpinBitUpdated { state: bool }`. The case constructs the
four payloads below exactly as written, so their field names and the shapes
those expressions fix are part of the surface too -- a payload that does not
accept these expressions does not compile against the probe:

```rust
MtuUpdated { old: Some(1200), new: 1452, done: Some(true) }
KeyUpdated { key_type: KeyType::Server1RttSecret,
             old: None, new: None, key_phase: Some(1), trigger: None }
CongestionStateUpdated { old: None,
                         new: "congestion_avoidance".to_string(), trigger: None }
StreamTypeSet { initiator: None, stream_id: 2, stream_type: StreamType::Control,
                stream_type_bytes: None, associated_push_id: None }
```

The integer fields' widths are yours to choose; their `Option`-wrapping is not,
wherever `Some`/`None` appears above, and `CongestionStateUpdated`'s `new` is a
`String`. `ReferenceTime` is constructed with
`ReferenceTime::new_monotonic(None)`; `CommonFields` carries the
`reference_time`; `VantagePoint` must be constructible as
`VantagePoint { ty: VantagePointType::Client, name: None }` (so `VantagePointType`
is public and the struct's fields are too); and the five `SQLOG*`/`*_URI`
constants above are `&str` constants at the crate root. `EventTimePrecision`
lives in `qlog::streamer` and has a `MicroSeconds` variant.

### `BoringSSL from the registry, not from a submodule`

The transport crate's crypto stops being vendored. What that means precisely,
because every clause is checked:

* The package's **default feature set is exactly `["boringssl-boring-crate"]`**.
* `boringssl-vendored` and `openssl` are **not features of the package at
  all** -- not empty, not off by default, absent. `boringssl-boring-crate`,
  `ffi` and `qlog` remain features.
* `boring` and `boring-sys` are **normal dependencies** of the package, in the
  `[dependencies]` table. They are not build dependencies and not optional
  extras reached through some other name.
* `cmake` and `pkg-config` are **not build dependencies** of the package. The
  build script no longer compiles a C library, so it no longer needs either.
* There is **no `.gitmodules`** at the workspace root, and **no
  `quiche/deps/boringssl` directory**. The submodule and the tree it pointed
  at are gone.

The case reads all of this the way cargo does, from

```
cargo metadata --offline --no-deps --format-version 1
```

taking the entry in `packages` whose name is `quiche` and looking at its
`features` and `dependencies` arrays. `--no-deps` keeps the answer about the
workspace rather than about the resolved graph. Nothing is read from the
candidate's source: the two directory checks are checks that a path does not
exist, and the rest is cargo's own description of the manifest.

The environment no longer sets `QUICHE_BSSL_PATH` and there is no prebuilt
BoringSSL in the image, because there is nothing left to point at it. The
`boring-sys` package builds its own copy at dependency-build time.

### `Five additions on Config, Connection and h3::Config`

```rust
// quiche::h3
pub const SETTINGS_MAX_FIELD_SECTION_SIZE_DEFAULT: u64 = 32_768;

impl quiche::Config {
    /// Configures the TLS curve (a.k.a. group) preference list.
    pub fn set_curves_list(&mut self, curves: &str) -> Result<()>;
}

impl quiche::Connection {
    /// Reads the first received DATAGRAM as an owned buffer.
    pub fn dgram_recv_buf(&mut self) -> Result<Vec<u8>>;
    /// Sends an owned buffer in a DATAGRAM frame, without copying it.
    pub fn dgram_send_buf(&mut self, buf: Vec<u8>) -> Result<()>;
    /// The number of contiguous bytes buffered for a stream, up to `max_len`.
    pub fn stream_readable_len(&self, stream_id: u64, max_len: usize) -> usize;
    /// Whether the stream is closed in every direction that applies to it.
    pub fn stream_closed(&self, stream_id: u64) -> bool;
}
```

`Connection` is generic over the buffer factory it was given at milestone 13,
so the two DATAGRAM entry points are written against that factory's buffer
type; with the default factory that type is `Vec<u8>`, which is what the
cases use.

**The field section default.** `h3::Config::new()` sets
`max_field_section_size` to `Some(SETTINGS_MAX_FIELD_SECTION_SIZE_DEFAULT)`
where it used to leave it `None`. The consequence a peer sees is that
SETTINGS_MAX_FIELD_SECTION_SIZE -- identifier `0x06` -- now appears in the
SETTINGS frame of an endpoint nobody configured, carrying 32768. The case
reads it back from the other end of a real HTTP/3 connection with
`peer_settings_raw()`, from both directions, and then sets 4096 explicitly on
one side and checks that value reaches the wire instead.

**The owned DATAGRAM buffers.** `dgram_send_buf` takes ownership and queues the
buffer; `dgram_recv_buf` removes the first queued DATAGRAM and returns it. They
are the same queues the copying pair uses, so a datagram sent with one can be
read with the other. Exact answers: a buffer longer than
`dgram_max_writable_len()` is `Err(Error::BufferTooShort)`, a buffer of exactly
that length is accepted, a connection whose peer never negotiated the extension
answers `Err(Error::InvalidState)` to a send, and an empty receive queue answers
`Err(Error::Done)`.

**`stream_readable_len`.** The count is of *contiguous* bytes at the stream's
current read offset -- the bytes the next `stream_recv` would return -- bounded
above by `max_len`. Data sitting behind a gap is not counted; a stream that does
not exist is 0, not an error; and after a partial `stream_recv` the count is
what is left. The case builds the gap deliberately, by capturing two
writes before they are delivered and handing them to the peer in the wrong
order.

**`stream_closed`.** For a bidirectional stream, true once both directions have
signalled fin. For a unidirectional stream, true once the one direction that
exists has: the send side if the stream was opened locally, the receive side if
it was opened by the peer. A stream that has already been collected is closed;
a stream that was never opened is not.

**`set_curves_list`.** A colon-separated preference list of group names in the
backend's spelling, for example `"X25519MLKEM768:X25519:P-256:P-384"`. It maps
onto `SSL_CTX_set1_groups_list`. A name the backend does not recognise is
`Err(Error::TlsFail)`. The case observes the effect rather than the
return: a client offering only `P-256` and a server accepting only `X25519`
share no group and do not complete a handshake, while a client offering
`P-384:P-256` and a server accepting `P-256` do and then carry stream data.

## The groups at this step

One row per group. **gate** groups are all-or-nothing: a failure means the step is not satisfied; **requirement** groups cover this step's own new capability. The rows are grouped by tier for reading; the order they run in, and each one's result, is what `reward_details.json` records.

| group | tier | surface | what it observes |
| --- | --- | --- | --- |
| `candidate-workspace-builds` | gate | T4 | The one check every step shares: `cargo build --offline` (default targets -- libraries and binaries only, not tests or benches) must complete for the workspace at `/app` within the harness's build timeout, before any other group in this table runs. It is the one group every step carries identically, and no step declares it as a capability of its own -- but it is not optional either: one failing gate group, including this one, means the step is not satisfied, however much of the new work is right. |
| `cap-qlog-file-schema` | requirement | T4 | The header record of a sequential qlog file, against a fixed expectation table. The verifier writes a small crate into scratch -- a `Cargo.toml` with its own empty `[workspace]` table so it is not pulled into the candidate's, and a path dependency on `<candidate root>/qlog` -- and a `main.rs` that builds a `TraceSeq` with `vec![QUIC_URI, HTTP3_URI]`, opens a `QlogStreamer` over a file named with `SQLOG_EXT`, starts the log, writes one event, finishes it, and prints the seven public constants. Building that crate is the first assertion: a `qlog` that does not export those constants, or whose `TraceSeq::new` does not take event schemas, does not compile against it -- and nothing has been read from the candidate's source to find that out. The probe's printed constants are compared against their fixed values. The file it wrote is then parsed as a JSON Text Sequence and checked structurally: it holds exactly two records; the header record's keys are exactly `description`, `file_schema`, `serialization_format`, `title` and `trace`; `file_schema` is `urn:ietf:params:qlog:file:sequential`; `serialization_format` is `JSON-SEQ`; there is no `traces` array anywhere in it; `trace.event_schemas` is the two-element list `["urn:ietf:params:qlog:events:quic-12", "urn:ietf:params:qlog:events:http3-12"]` in that order; and the trace carries no `configuration` object. Structural throughout, against an expectation table, and never a substring search over the file: the absence checks in particular are checks that a key is not in an object, not that a string does not occur in the bytes. |
| `cap-qlog-event-namespace` | requirement | T4 | The serialized `name` of four streamed events, against the rename table in the interface document. A second probe crate, built the same way against `<candidate root>/qlog`, emits `QuicMtuUpdated`, `QuicKeyUpdated`, `QuicCongestionStateUpdated` and `Http3StreamTypeSet` through a `QlogStreamer` and writes them to a `.sqlog` file. The file is parsed as a JSON Text Sequence; it must hold the header plus exactly four event records; each record's keys must be exactly `data`, `name` and `time`, with `time` a JSON number; and each `name` must equal the expected string. The four are drawn from four different old categories -- `connectivity:`, `security:`, `recovery:` and `http:` -- so that moving one namespace and not the others fails. The fourth carries the whole of the second failure mode: it must be `http3:stream_type_set`, so a candidate that rewrote every name to begin with `quic:` fails here. The body of the first record is checked as well as its name -- `old` 1200, `new` 1452, `done` true -- so that a name cannot be produced by something that is not the event it claims to be. |
| `cap-boring-crate-backend` | requirement | T4 | The transport crate's manifest as cargo reports it, and two paths that must not exist. `cargo metadata --offline --no-deps --format-version 1` is run in the candidate's workspace and the `quiche` entry of the `packages` array is read. From it: the `default` feature is exactly the one-element list `["boringssl-boring-crate"]`; `boringssl-vendored` and `openssl` are not keys of the features map at all; `boringssl-boring-crate`, `ffi` and `qlog` still are; `boring` and `boring-sys` each appear in `dependencies` with kind `null`, which is cargo's spelling for a normal dependency; and neither `cmake` nor `pkg-config` appears with kind `build`. Then the two absences: no `.gitmodules` at the workspace root, and no `quiche/deps/boringssl` directory. This is a T4 group -- the emitted artefact is the package layout as cargo describes it -- and it is checked as layout, never as behaviour. The behaviour that follows from getting it wrong is checked elsewhere: two binaries linking different crypto cannot complete a handshake, which is what the proof-of-life gate runs into. |
| `cap-h3-default-field-section` | requirement | T3 | The SETTINGS frame an unconfigured HTTP/3 endpoint now sends, read from the other end of a real connection. Two in-memory HTTP/3 connections are brought up over the `h3` ALPN with the verifier's default `h3::Config` on both sides, settled, and polled until neither has an event left -- polling is what parses the peer's control stream, so it is what fills `peer_settings_raw()`. Each side's view of its peer must then contain exactly one pair whose identifier is `0x06`, carrying 32768. At the previous milestone the pair was absent entirely. The public constant `SETTINGS_MAX_FIELD_SECTION_SIZE_DEFAULT` is checked to equal 32768 beside it, because an application that wants to reason about the limit reaches for the constant and not for the literal. The second half is the control that makes this a default rather than a constant: a server whose `h3::Config` had `set_max_field_section_size(4096)` called on it must put 4096 on the wire. A candidate that writes 32768 unconditionally passes the first half and fails this one. |
| `cap-dgram-buf-api` | requirement | T3 | An owned buffer handed to the DATAGRAM queue and taken back out of it. Two in-memory connections with `enable_dgram(true, 10, 10)` on both configs. The client calls `dgram_send_buf` with an owned 512-byte body; after the datagrams cross, the server's `dgram_recv_buf` returns exactly those bytes, a second call returns `Err(Error::Done)` because an owned read consumes the datagram just as the copying read does, and the server echoes the same buffer back with `dgram_send_buf` so the direction is proven both ways. Then the three exact answers. A buffer one byte longer than `dgram_max_writable_len()` is `Err(Error::BufferTooShort)`; a buffer of exactly that length is `Ok(())`, so the refusal is a boundary rather than a blanket rejection; and on a second pair whose configs never enabled the extension, `dgram_send_buf` is `Err(Error::InvalidState)` and `dgram_recv_buf` is `Err(Error::Done)`. |
| `cap-stream-readable-len` | requirement | T3 | How much of a stream can be read right now, including the case where the answer is zero and the receive buffer is not empty. The case does not use the verifier's ordinary exchange helper, because that one delivers everything in order. It writes 1000 bytes to stream 4, takes the datagrams the client produced without delivering them, writes 800 more, takes those, and then hands the *second* batch to the server first. With the front of the stream missing, `stream_readable_len(4, usize::MAX)` must be 0 and `stream_readable(4)` must be false, even though 800 bytes are buffered. An implementation that sums the receive buffer reports 800 here and passes everything else in the group. Delivering the withheld batch closes the gap, and the count becomes 1800. `stream_readable_len(4, 100)` is then 100 -- the ceiling is a ceiling, not a hint, and it cuts inside the first buffered chunk -- and `stream_readable_len(4, 0)` is 0. A 600-byte `stream_recv` returns the first 600 bytes of the stream in order and leaves the count at 1200. A stream that was never opened is 0 rather than an error. |
| `cap-stream-closed` | requirement | T3 | Whether a stream is finished, in every direction that applies to it. One bidirectional stream walked through both halves of its finish. After the client sends 256 bytes without a fin, neither end reports stream 4 closed. After the client sends its fin and the server reads it, still neither does -- a bidirectional stream needs both directions, and `stream_finished` at the client is still false because it reports only the receiving half. After the server replies with a fin and the client reads it, both ends report closed, which is also true if the stream has been collected by then. Then the two edges. A stream nobody ever opened reports false, which is what distinguishes it from one that finished and was collected. And on locally opened unidirectional streams, where the peer's direction does not exist and must not be waited for: stream 2, written with a fin, is closed; stream 6, written without one, is not. |
| `cap-curves-list` | requirement | T3 | That the TLS group preference list reaches the handshake. The return value alone proves nothing -- a setter that took the string and did nothing would also return `Ok` -- so the observation is a pair of configurations with no group in common. A client offering only `P-256` and a server accepting only `X25519` must not complete a handshake: the case drives the datagrams between them itself, leniently, because a handshake that is meant to fail produces errors on both sides and those errors are the subject rather than a fault, and then asserts that neither end reached the established state. Beside it, the control: a client offering `P-384:P-256` and a server accepting `P-256` do handshake, and then carry 256 bytes on a stream with a fin. Without it, a candidate whose connections never handshake at all would pass the first assertion. The exact-answer negative is the argument itself: `set_curves_list` with a name the backend does not recognise is `Err(Error::TlsFail)`. |
| `reg-04-datagram-roundtrip` | gate | T3 | The DATAGRAM extension negotiated, a 256-byte payload sent by the client and echoed by the server, both directions byte-for-byte, a received DATAGRAM opening no stream, and an empty queue answering `Done`. It is carried here because this step adds a second way to reach the same queues. `dgram_send_buf` and `dgram_recv_buf` are new entry points onto the send and receive queues that `dgram_send` and `dgram_recv` have used since milestone 04, and the obvious way to add them is to rewrite the queues to hold owned buffers and reimplement the copying pair on top. This group is the copying pair, unchanged, observed through the API it has always had. The behaviour this step deliberately changes is not in it: nothing here touches the field section default, the qlog schema, or the event names. |
| `reg-09-key-update` | gate | T2 | A 1-RTT key update driven from the wire and verified with the verifier's own packet protection, which is written from RFC 9001 and does not depend on the candidate. It is carried here because this step replaces the crypto. BoringSSL stops being a vendored submodule compiled by the build script and becomes the `boring` and `boring-sys` packages from the registry, and the key schedule -- the secrets, the HKDF labels, the header protection, the phase bit -- runs through whichever one is linked. A second implementation reading the candidate's packets is the only observation that distinguishes "the new backend is wired up" from "the new backend is wired up and produces the same bytes". The observation is of the wire, not of the backend: nothing in it names a crate, a feature or a build script. |
| `reg-01-h3-get` | gate | T1 | The end-to-end HTTP/3 fetch, run between the candidate's own `quiche-server` and its own `quiche-client`, as two real processes over UDP loopback. The server is started on a loopback port serving a 64 KiB file the verifier wrote. The client fetches it with `--no-verify` and with `--http-version HTTP/3` pinned rather than left to the apps' own negotiation, exits 0 within the group's deadline, and the bytes on its stdout match the file byte-for-byte. Pinning the version is what keeps the inherited observation HTTP/3 and stops it quietly becoming a different group. This is the plan-mandated end-to-end regression, and it is the only group in the package that makes `apps` resolve its own features. The seed's `apps` manifest names no crypto backend at all -- it inherits the transport crate's default -- so the split-backend failure this paragraph describes is not reachable here; what the group does enforce is that the whole workspace still builds against the registry BoringSSL and that the two binaries still talk to each other, which is where a half-converted workspace shows first. Ports are chosen free per case and the driver runs one case at a time, so there is no contention with the other process-spawning groups. (The harness's `serial_t1` guard exists and is not called by any case at this step; the serialisation the driver provides is per-invocation.) |
| `gate-endtoend` | gate | T1 | Proof of life, and nothing else. The candidate's own `quiche-server` is started on a loopback port with `--listen`, `--cert`, `--key`, `--root` and `--no-retry`; its own `quiche-client` fetches one file from it with `--no-verify`; the client exits 0 and the bytes it wrote match the file the server was serving, byte-for-byte. Every other option is the reference's own default. Nothing from this step's milestone appears here on purpose -- no qlog, no SETTINGS, no owned datagram buffers, no curve list. Those are checked where they can be seen. This group is the floor underneath all of them, and it is the group that fails first if replacing the vendored BoringSSL left the workspace unable to build, or left the two binaries linking different crypto. It adds no new requirement of its own. |

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

### BoringSSL is your dependency's to build; there is nothing prebuilt for you

This step is the one exception to what the other fourteen do, so read it even
if you have carried a working `build.rs` this far.

There is **no prebuilt BoringSSL in this image, and `QUICHE_BSSL_PATH` is not
set** — not set to an empty string, not set at all — and this workdir ships no
`.cargo/config.toml` to set it either. Reading it is not a harmless fallback:
a build script that does `env::var("QUICHE_BSSL_PATH").unwrap()` panics, a
panicking build script fails `cargo build`, and a failed build fails the
proof-of-life gate, which means the whole step is not satisfied however good the rest of
your work is.

The reason is that there is nothing left for it to point at. The transport
crate now takes its BoringSSL through the `boring` crate, and `boring-sys`
compiles its own copy at dependency-build time. That is dependency work, not
yours: it is already in the shared `CARGO_TARGET_DIR` described under
*The measurement runs offline* below, and `boring-sys` is pinned in the `Cargo.lock`
that ships in this workdir. Your build script must not try to find, fetch, or
compile a BoringSSL of its own, and must not emit `cargo:rustc-link-search` /
`cargo:rustc-link-lib` lines for one.

If you are carrying a `build.rs` forward from an earlier step, the link lines
it emits for `$QUICHE_BSSL_PATH` are exactly what has to go.

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

`/app` contains everything step 14 left behind, unchanged. The milestone before this one is modelled on quiche 0.26.0; this one is modelled on 0.30.0. Read `WORKSPACE_HANDOFF.md` for what is already there and what must keep working.

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

