# Test interface — step 10, BBRv2, an expanded C surface, and peer transport parameters

This is the contract between your implementation and the checker. Every
name, path and flag the checker depends on is written down here; nothing
outside this document is assumed about your code. Where the document and
your intuition disagree, the document wins — it is what the tests were
written against.

Generated, together with this step's interface contract, from one source.
The two cannot disagree.

## What this step is

Three capabilities arrive at this milestone and one piece of bookkeeping moves
house.

**BBRv2 becomes the fourth congestion-control algorithm.**
`CongestionControlAlgorithm` grows a `BBR2` variant appended after `BBR`, the
name `"bbr2"` becomes selectable through `Config::set_cc_algorithm_name`, and
there is a real algorithm behind it: a connection configured for BBRv2 carries
data, grows its congestion window out of the initial one, and reports sane path
statistics. Step 08 added BBR and its group asserted that `"bbr2"` was
*refused*; that assertion is now false and this step's group is its inverse.

**The C API grows a path and connection-ID surface.** Everything the Rust API
learned at step 08 for multi-path connections, and everything it learned at
step 09 for issuing and retiring connection IDs, becomes reachable from C:
`quiche_conn_probe_path`, `quiche_conn_migrate`, `quiche_conn_send_on_path`,
`quiche_conn_path_event_next` with an opaque `quiche_path_event` and its six
accessors, `quiche_conn_new_scid`, `quiche_conn_retire_dcid`,
`quiche_conn_active_scids`, `quiche_conn_scids_left`,
`quiche_conn_available_dcids`, `quiche_conn_source_ids` and
`quiche_conn_retired_scids` with their two iterator types, and
`quiche_conn_peer_transport_params`. The header and the Rust `ffi` module must
agree with each other, which is a stricter requirement than it sounds: at the
previous milestone they did not.

**The connection reports reset and stopped stream counts, and how many
PATH_CHALLENGE frames it has received.** `Stats` gains five counters, and
`Config` gains a bound on how many received PATH_CHALLENGE frames a path will
queue for response.

The bookkeeping that moves: the thirteen `peer_*` fields that `Stats` has
carried since step 06 are no longer part of `Stats`. They live in a
`TransportParams` struct reached through a new `Connection::peer_transport_params()`,
which returns `None` until the peer's parameters have actually been parsed. The
C API mirrors the split exactly.

A `MtuUpdated` connectivity event joins the qlog schema, which steps to 0.10.0.

## The flip

Something an earlier step established changes meaning here. This is the
part a candidate most often gets half-right: the new behaviour appears,
the old one is left working beside it, and both the new group and the
inherited group pass for the wrong reason — until the step that
requires the old behaviour to be *gone*.

Two hard breaks, no deprecated aliases for either.

**Four connection-ID methods are renamed.** The words "source" and
"destination" contract to `scid` and `dcid`:

| step 09 | step 10 |
| --- | --- |
| `Connection::new_source_cid` | `Connection::new_scid` |
| `Connection::active_source_cids` | `Connection::active_scids` |
| `Connection::source_cids_left` | `Connection::scids_left` |
| `Connection::retire_destination_cid` | `Connection::retire_dcid` |

Signatures and behaviour are unchanged; only the names move. The rename reaches
the shipped binaries too -- `quiche-client` and `quiche-server` call the new
names. Exactly four methods are affected: `source_ids()` and `retired_scids()`,
added at step 09, keep their names, and so does `available_dcids()` from step 08.
A step 08-09 solution that keeps the old spelling stops compiling with no
replacement to guess at, which is why the table above is exhaustive and the two
survivors are named explicitly.

**The `peer_*` fields leave `Stats`.** `Stats` at step 09 had twenty-two public
fields: nine counters and thirteen `peer_max_idle_timeout`-style mirrors of the
peer's transport parameters. At this milestone `Stats` has fourteen: the same
nine, plus `reset_stream_count_local`, `stopped_stream_count_local`,
`reset_stream_count_remote`, `stopped_stream_count_remote` and
`path_challenge_rx_count`. Reading `stats().peer_initial_max_data` no longer
compiles. The values are still available, through
`Connection::peer_transport_params() -> Option<&TransportParams>`, under names
with the `peer_` prefix dropped: `peer_initial_max_data` becomes
`peer_transport_params().unwrap().initial_max_data`. The accessor returns `None`
before the peer's parameters have been parsed, which `Stats` had no way to
express -- it reported zeros, indistinguishable from a peer that really had
asked for zero.

The same split happens in C, and it is not a rename: `quiche_stats` loses its
thirteen `peer_*` members, a new `quiche_transport_params` gains them, and
`bool quiche_conn_peer_transport_params(const quiche_conn *conn,
quiche_transport_params *out)` returns `false` when there is nothing to report.

**One inconsistency is corrected rather than added to.** At step 09 the C header
declared `quiche_h3_event_type`'s six values as `HEADERS=0, DATA=1, FINISHED=2,
GOAWAY=3, RESET=4, PRIORITY_UPDATE=5`, and the Rust function behind it returned
4, 5 and 6 for the last three. The header was right and the implementation was
wrong. At this milestone they agree: `quiche_h3_event_type()` returns 3 for a
GoAway, 4 for a Reset and 5 for a PriorityUpdate. Nothing in the header changes.

Nothing else moves. `PathStats` is field-for-field what step 09 specified, and
so are `SendInfo`, `RecvInfo`, `PathEvent`, the `h3` module's public surface,
`Error`, and every `Config` setter step 09 required. Every step 01-09 group that
does not name a renamed method or a `peer_*` field must still pass untouched.

## Interface elements the checker depends on

### `CongestionControlAlgorithm::BBR2`

The enum gains a fourth variant, **appended**:

```rust
#[repr(C)]
pub enum CongestionControlAlgorithm {
    Reno  = 0,
    CUBIC = 1,
    BBR   = 2,
    BBR2  = 3,
}
```

The discriminants are load-bearing in both directions. `#[repr(C)]` and the
explicit values are what let the C API pass an algorithm as an integer, so
`BBR2 as i32 == 3` is part of the contract, and inserting the new variant
anywhere but the end -- after `BBR`, which step 08 pinned at 2 -- breaks the C
side of every earlier step.

`CongestionControlAlgorithm::from_str` (and therefore
`Config::set_cc_algorithm_name`) accepts exactly four names, all lowercase and
exact: `"reno"`, `"cubic"`, `"bbr"`, `"bbr2"`. Anything else is
`Err(Error::CongestionControl)`, including `"BBR2"`, `"bbr2 "` and `""`. Step 08
required `"bbr2"` to be refused; that requirement is withdrawn here and
replaced by its inverse.

There is an algorithm behind the variant, not just a name. A connection
configured with `set_cc_algorithm(CongestionControlAlgorithm::BBR2)` or
`set_cc_algorithm_name("bbr2")` completes a handshake, carries a 256 KiB
transfer to completion byte-for-byte, and reports the bytes it sent through
`Stats::sent_bytes`. The window is asserted `>=`-wise, not `>`-wise: growth is
what a real controller does and is what the group is about, but demanding it
strictly would be pinning the host's scheduler, since a BBR-family window is a
function of bandwidth and RTT estimates that an in-memory pair reads off the
machine. An implementation that adds the variant and maps it onto nothing, or
panics, or closes the window under load it is not losing anything to, fails.

### `Connection ID methods, renamed`

Four methods change name at this milestone and nothing else about them changes.
Their signatures, their return types, their error behaviour and their effect on
the wire are exactly what step 08 and step 09 specified:

```rust
impl Connection {
    pub fn new_scid(&mut self, scid: &ConnectionId, reset_token: u128,
                    retire_if_needed: bool) -> Result<u64>;
    pub fn active_scids(&self) -> usize;
    pub fn scids_left(&self) -> usize;
    pub fn retire_dcid(&mut self, dcid_seq: u64) -> Result<()>;
}
```

The old names -- `new_source_cid`, `active_source_cids`, `source_cids_left`,
`retire_destination_cid` -- are gone, with no `#[deprecated]` shim.

Four *neighbouring* methods keep their names and are all still required:
`source_ids()`, `retired_scids()` and `retired_scid_next()` from step 09, and
`available_dcids()` from step 08. The rename is not a systematic
"source"→"scid" sweep; it is these four methods and no others.

Two further connection-ID accessors keep their names too, and
`cap-scid-renames` reads both of them by name, so they have to exist with these
shapes:

```rust
impl Connection {
    pub fn source_id(&self) -> ConnectionId
    pub fn destination_id(&self) -> ConnectionId
}
```

`source_id()` reports the connection ID this endpoint is currently using as
its source -- the one the active path uses when there is an active path, and
the oldest one otherwise -- and `destination_id()` reports the peer's ID this
connection is addressing. Both return the step-04 `ConnectionId<'a>` wrapper
rather than a slice, and both can change over the connection's lifetime. The
group captures `source_id().into_owned()` before the peer retires the
handshake ID and asserts that `retired_scid_next()` later hands back that very
ID, so `ConnectionId::into_owned()` (step 05) is part of what has to compile;
it also compares `destination_id().as_ref()` against the peer's advertised
`initial_source_connection_id`, which is `AsRef<[u8]>` on the same wrapper
(step 04).

`quiche-client` and `quiche-server` use the new names, so a solution that
renames the library methods but leaves the binaries calling the old ones does
not build.

### `Connection::is_path_validated`

```rust
impl Connection {
    pub fn is_path_validated(&self, from: SocketAddr, to: SocketAddr) -> Result<bool>
}
```

Takes a path's local and peer address, in that order, and reports whether that
four-tuple is a path of this connection and has been validated: `Ok(true)`,
`Ok(false)`, or `Err(Error::InvalidState)` when the connection holds no such
path. The method has existed since step 08, where the path API arrived;
`cap-path-challenge-queue` calls it by name at this step, on the path a probe
has just validated, and requires `Ok(true)`.

The shape is worth reading beside the C entry point: the Rust method returns a
`Result<bool>`, while `quiche_conn_is_path_validated` in the header above
returns an `int` and packs the error into a negative code. A Rust-side method
declared to return a bare `bool` does not compile against this group.

### `Connection::peer_transport_params and the TransportParams struct`

```rust
impl Connection {
    pub fn peer_transport_params(&self) -> Option<&TransportParams>;
}
```

`None` until the peer's transport parameters have been received and parsed;
`Some` from then on. On a client this means `None` before the server's first
flight is processed, and on a server `None` before the client's Initial is
processed.

`TransportParams` is public, `Clone + Debug + PartialEq`, and carries the
thirteen values `Stats` used to mirror plus four more the peer can send. The
`peer_` prefix is dropped from every one of them:

```rust
pub struct TransportParams {
    pub original_destination_connection_id: Option<ConnectionId<'static>>,
    pub max_idle_timeout: u64,
    pub stateless_reset_token: Option<u128>,
    pub max_udp_payload_size: u64,
    pub initial_max_data: u64,
    pub initial_max_stream_data_bidi_local: u64,
    pub initial_max_stream_data_bidi_remote: u64,
    pub initial_max_stream_data_uni: u64,
    pub initial_max_streams_bidi: u64,
    pub initial_max_streams_uni: u64,
    pub ack_delay_exponent: u64,
    pub max_ack_delay: u64,
    pub disable_active_migration: bool,
    pub active_conn_id_limit: u64,
    pub initial_source_connection_id: Option<ConnectionId<'static>>,
    pub retry_source_connection_id: Option<ConnectionId<'static>>,
    pub max_datagram_frame_size: Option<u64>,
}
```

`Default` gives `max_udp_payload_size = 65527`, `ack_delay_exponent = 3`,
`max_ack_delay = 25` and `active_conn_id_limit = 2` -- the RFC 9000 Section 18.2
defaults -- and zero, `None` or `false` for the rest. A parameter the peer did
not send reads as its default, which is why the struct uses `Option` only where
RFC 9000 has no default to fall back on.

The values a peer reports are the ones it configured, not this endpoint's. A
connection whose peer called `set_initial_max_data(777_000)` reads
`initial_max_data == 777_000` here, and `max_idle_timeout` is in milliseconds,
matching `Config::set_max_idle_timeout`.

### `Stats: the peer_* fields leave, five counters arrive`

```rust
pub struct Stats {
    pub recv: usize,
    pub sent: usize,
    pub lost: usize,
    pub retrans: usize,
    pub sent_bytes: u64,
    pub recv_bytes: u64,
    pub lost_bytes: u64,
    pub stream_retrans_bytes: u64,
    pub paths_count: usize,
    pub reset_stream_count_local: u64,
    pub stopped_stream_count_local: u64,
    pub reset_stream_count_remote: u64,
    pub stopped_stream_count_remote: u64,
    pub path_challenge_rx_count: u64,
}
```

Fourteen fields, exactly. The first nine are what step 08 established. The
thirteen `peer_*` fields step 06 added are **not** here; see
`peer_transport_params()`.

The four stream counters count RESET_STREAM and STOP_SENDING *frames*, split by
who originated them, and `local`/`remote` is from the perspective of the
connection you are asking:

* `reset_stream_count_local` -- RESET_STREAM frames this endpoint sent.
* `reset_stream_count_remote` -- RESET_STREAM frames this endpoint received.
* `stopped_stream_count_local` -- STOP_SENDING frames this endpoint sent.
* `stopped_stream_count_remote` -- STOP_SENDING frames this endpoint received.

One consequence is worth stating because it is the behaviour a group asserts and
it is not obvious: a `stream_shutdown(id, Shutdown::Read, err)` sends
STOP_SENDING, and RFC 9000 Section 3.5 requires the peer to stop sending, which
this implementation does by resetting the stream. So a single STOP_SENDING
produces one `stopped_stream_count_local` on the sender **and** one
`reset_stream_count_local` on the receiver, with the mirrored `_remote` counters
on the other side. The counts are therefore asymmetric between the two endpoints
of the same connection.

`path_challenge_rx_count` counts PATH_CHALLENGE frames received, over all paths,
and is incremented when the frame is processed -- before any decision to queue
or discard it. It does not move for STREAM frames, ACK frames or
PATH_RESPONSE frames.

The C `quiche_stats` mirrors this struct minus `path_challenge_rx_count`, which
is deliberately not exposed in C at this milestone: it ends at
`stopped_stream_count_remote`.

### `Config::set_path_challenge_recv_max_queue_len`

```rust
impl Config {
    pub fn set_path_challenge_recv_max_queue_len(&mut self, queue_len: usize);
}
```

The maximum number of received PATH_CHALLENGE frames a single path will hold
waiting to be answered with a PATH_RESPONSE. Once a path's queue is full, a
further PATH_CHALLENGE on that path is **discarded** -- the frame is still
counted by `Stats::path_challenge_rx_count`, but no PATH_RESPONSE will ever be
sent for it. **The default is 3.**

The queue is per path, not per connection, and it is drained as responses are
written, so a peer that challenges, reads the response, and challenges again
never reaches the bound however many times it does so. The bound only bites when
challenges arrive faster than responses are sent.

### `The C API: paths, connection IDs, and the transport-parameter split`

The header is `/app/quiche/include/quiche.h`, and the FFI lives behind a
**non-default** Cargo feature named `ffi`:

```toml
[features]
default = ["boringssl-vendored"]
ffi = []
```

```toml
[lib]
crate-type = ["lib", "staticlib", "cdylib"]
```

`cargo build --features ffi` produces `libquiche.a` and `libquiche.so` carrying
the `extern "C"` symbols; a default build produces the Rust library only. Both
`quiche/src/ffi.rs` and `quiche/src/h3/ffi.rs` are gated on that feature. A
checker links the **static** library and passes no BoringSSL flags of its own.

Everything step 01 and step 02 documented about the C API still holds. New at
this milestone:

```c
ssize_t quiche_conn_send_on_path(quiche_conn *conn, uint8_t *out, size_t out_len,
                                 const struct sockaddr *from, size_t from_len,
                                 const struct sockaddr *to, size_t to_len,
                                 quiche_send_info *out_info);

int quiche_conn_new_scid(quiche_conn *conn, const uint8_t *scid, size_t scid_len,
                         const uint8_t *reset_token, bool retire_if_needed,
                         uint64_t *scid_seq);
int quiche_conn_retire_dcid(quiche_conn *conn, uint64_t dcid_seq);
size_t quiche_conn_active_scids(const quiche_conn *conn);
size_t quiche_conn_scids_left(const quiche_conn *conn);
size_t quiche_conn_available_dcids(const quiche_conn *conn);
size_t quiche_conn_retired_scids(const quiche_conn *conn);

int quiche_conn_probe_path(quiche_conn *conn,
                           const struct sockaddr *local, size_t local_len,
                           const struct sockaddr *peer, size_t peer_len,
                           uint64_t *seq);
int quiche_conn_migrate(quiche_conn *conn,
                        const struct sockaddr *local, size_t local_len,
                        const struct sockaddr *peer, size_t peer_len,
                        uint64_t *seq);
int quiche_conn_is_path_validated(const quiche_conn *conn,
                                  const struct sockaddr *from, size_t from_len,
                                  const struct sockaddr *to, size_t to_len);

typedef struct quiche_connection_id_iter quiche_connection_id_iter;
quiche_connection_id_iter *quiche_conn_source_ids(const quiche_conn *conn);
bool quiche_connection_id_iter_next(quiche_connection_id_iter *iter,
                                    const uint8_t **out, size_t *out_len);
void quiche_connection_id_iter_free(quiche_connection_id_iter *iter);

enum quiche_path_event_type {
    QUICHE_PATH_EVENT_NEW,
    QUICHE_PATH_EVENT_VALIDATED,
    QUICHE_PATH_EVENT_FAILED_VALIDATION,
    QUICHE_PATH_EVENT_CLOSED,
    QUICHE_PATH_EVENT_REUSED_SOURCE_CONNECTION_ID,
    QUICHE_PATH_EVENT_PEER_MIGRATED,
};
quiche_path_event *quiche_conn_path_event_next(quiche_conn *conn);
enum quiche_path_event_type quiche_path_event_type(quiche_path_event *ev);
void quiche_path_event_free(quiche_path_event *ev);

bool quiche_conn_peer_transport_params(const quiche_conn *conn,
                                       quiche_transport_params *out);
```

plus `quiche_path_event_new`, `quiche_path_event_validated`,
`quiche_path_event_failed_validation`, `quiche_path_event_closed`,
`quiche_path_event_reused_source_connection_id` and
`quiche_path_event_peer_migrated` to read each variant's addresses out into
caller-provided `struct sockaddr_storage *` / `socklen_t *` pairs; a
`quiche_socket_addr_iter` with `_next` and `_free` behind `quiche_conn_paths_iter`
and `quiche_conn_migrate_source`; `quiche_conn_retired_scid_next`;
`quiche_config_set_disable_dcid_reuse`; `quiche_config_set_ticket_key`;
`quiche_conn_is_resumed`; and `quiche_conn_is_dgram_send_queue_full` /
`quiche_conn_is_dgram_recv_queue_full`.

`quiche_transport_params` is a new plain struct carrying the thirteen values
`quiche_stats` used to hold, with the `peer_` prefix **kept** on the C side
(`peer_max_idle_timeout`, `peer_initial_max_data`, ...) and
`ssize_t peer_max_datagram_frame_size` so that "absent" can be signalled as a
negative value. `quiche_stats` loses those thirteen members.

**The header and the implementation must agree.** Two specific places where they
did not at step 09:

* `quiche_h3_event_type()` returns the value the header's
  `enum quiche_h3_event_type` declares: `QUICHE_H3_EVENT_GOAWAY` is **3**,
  `QUICHE_H3_EVENT_RESET` is **4**, `QUICHE_H3_EVENT_PRIORITY_UPDATE` is **5**.
  The enum in the header is unchanged from step 09; the returned integers move to
  match it.
* `quiche_h3_send_request`, `quiche_h3_send_response` and
  `quiche_h3_send_response_with_priority` take `const quiche_h3_header *headers`,
  and `quiche_h3_send_body` takes `const uint8_t *body`. Also new in the header:
  `int quiche_h3_send_goaway(quiche_h3_conn *conn, quiche_conn *quic_conn,
  uint64_t id)`.

### `qlog 0.10.0 and the MtuUpdated event`

The `qlog` workspace member steps from 0.9.0 to 0.10.0 and `quiche` depends on
it as `qlog = { version = "0.10", path = "../qlog", optional = true }`.

`ConnectivityEventType` gains an `MtuUpdated` variant and the schema gains the
event body behind it:

```rust
#[serde_with::skip_serializing_none]
#[derive(Serialize, Deserialize, Clone, Debug, PartialEq, Eq)]
pub struct MtuUpdated {
    pub old: Option<u16>,
    pub new: u16,
    pub done: Option<bool>,
}
```

Three properties of that declaration are the contract:

* Its `EventData` variant serialises its `name` as
  **`"connectivity:mtu_updated"`** -- the category and the snake_case event
  name, exactly as every other event in the schema is spelled.
* `old` and `done` are `Option` under `skip_serializing_none`, so a record
  written with `old: None, done: None` has a `data` object containing the single
  key `new`. Only `new` is mandatory, and it is a `u16`.
* Its importance is `EventImportance::Extra`, the lowest level. A
  `QlogStreamer` built with a `log_level` of `Core` or `Base` **drops** the
  event: `add_event_data_now` returns `Err(Error::Done)` and writes nothing.
  Only a streamer at `Extra` records it.

One other schema change comes with the version bump:
`PacketHeader::packet_number` becomes `Option<u64>` (it was `u64`), and being
`Option` under `skip_serializing_none` it is omitted from a packet header
written without one. **No group at this step asserts that field, and none at any
later step does either.** `cap-mtu-updated-qlog` writes `MtuUpdated` records and
nothing in this package ever constructs a `PacketHeader`, so the declaration
belongs to the same tier as the C entry points listed under "Required to exist,
out of scope at this step" below: the type must change, its absence is a contract
violation, and no assertion is pointed at it. It is stated here rather than
dropped because a candidate that left `packet_number` a bare `u64` would
silently diverge from the 0.20.0 schema everywhere a real qlog consumer looks. Every JSON-SEQ property step 06 established is otherwise
unchanged: `\x1e`-prefixed records, `qlog_version` `"0.3"`, `qlog_format`
`"JSON-SEQ"`, no `traces` key in the header, and every event record carrying
exactly `data`, `name` and `time`.

At this milestone `quiche` itself never emits `MtuUpdated` -- the event exists in
the schema and nothing in `quiche/src/` or `apps/src/` constructs one. The
requirement is the schema type and its serialisation, reachable by any consumer
of the `qlog` crate.

### `the qlog crate surface the probe compiles against`

`cap-mtu-updated-qlog` does not only read a file the candidate produced: it
**writes a program of its own**, declaring `qlog = { path = "<root>/qlog" }`
and building it against the candidate's workspace outside `/app`. Every name
that program imports is therefore part of this step's contract -- the rule
stated under "What the checker is allowed to look at" is that the names a probe
imports are written down here -- and so is the shape of every item it
constructs. All of them belong to the crate `qlog`, none of them to `quiche`.

```rust
use qlog::events::connectivity::MtuUpdated;
use qlog::events::quic::MetricsUpdated;
use qlog::events::{Event, EventData, EventImportance};
use qlog::streamer::QlogStreamer;
use qlog::{Configuration, TraceSeq, VantagePoint, VantagePointType};
```

* `qlog::QLOG_VERSION` is a `&str` and is still `"0.3"`; the probe prints it
  and the value is asserted.
* `qlog::VantagePoint` is a struct with exactly the public fields
  `name: Option<String>`, `ty: VantagePointType` and
  `flow: Option<VantagePointType>`, and `VantagePointType` is an enum with a
  `Client` variant. The probe constructs the struct with those three fields and
  no `..` rest pattern.
* `qlog::Configuration` is a struct with exactly the public fields
  `time_offset: Option<f64>` and `original_uris: Option<Vec<String>>`, and the
  probe constructs it with both.
* `qlog::TraceSeq::new` takes five arguments in this order --
  `(vantage_point: VantagePoint, title: Option<String>,
  description: Option<String>, configuration: Option<Configuration>,
  common_fields: Option<CommonFields>)` -- and the probe passes `None` for the
  last, so `CommonFields` must exist at the crate root as the element type of
  that parameter.
* `qlog::streamer::QlogStreamer::new` takes eight arguments in this order:
  `(qlog_version: String, title: Option<String>, description: Option<String>,
  summary: Option<String>, start_time: std::time::Instant, trace: TraceSeq,
  log_level: EventImportance, writer: Box<dyn std::io::Write + Send + Sync>)`.
  The probe passes `QLOG_VERSION.to_string()`, two `Some(..)` strings, `None`,
  `Instant::now()`, the `TraceSeq` above, an `EventImportance`, and
  `Box::new(File::create(..))`. `QlogStreamer` is named in a return type, so it
  carries no type parameters of its own.
* On the streamer: `start_log()` and `finish_log()` return a `Result`, and
  `add_event(&mut self, event: Event) -> Result<()>` writes one event, or
  returns `Err(Error::Done)` when the event's importance is not contained in the
  streamer's level -- that is how the `Core` streamer reports that it filtered
  the `Extra` `MtuUpdated` out, and the probe prints the error value.
* `qlog::events::Event::with_time(time: f32, data: EventData) -> Event` builds
  an event from its body; `EventImportance` has the variants `Core`, `Base` and
  `Extra`.
* `qlog::events::quic::MetricsUpdated` is a struct whose public fields are
  exactly `min_rtt`, `smoothed_rtt`, `latest_rtt`, `rtt_variance`, `pto_count`,
  `congestion_window`, `bytes_in_flight`, `ssthresh`, `packets_in_flight` and
  `pacing_rate`, all `Option`-wrapped and `congestion_window: Option<u64>`. The
  probe constructs one with every one of those names and no `..`, sets only
  `congestion_window`, and it is this package's `recovery:metrics_updated`
  control event, emitted at `EventImportance::Core`, so both streamers accept
  it.
* `EventData` is an enum with tuple variants `EventData::MtuUpdated(MtuUpdated)`
  and `EventData::MetricsUpdated(MetricsUpdated)` (among its others), and
  `connectivity::MtuUpdated` is the struct declared above.

A tree that has the `MtuUpdated` schema type and the JSON-SEQ writer but not
these names -- `TraceSeq`, `VantagePoint`, `Configuration`, `QLOG_VERSION`,
`Event::with_time` -- fails this group at the probe's compile step, and the
failure message is the probe build's own output. They have been part of the
crate since step 06; the shapes above are the 0.10.0 ones and are unchanged
from the 0.6.0 the crate started at, so a workspace that has carried the crate
forward has them already.

### `Required to exist, out of scope at this step`

A number of the new C entry points are required to exist and are deliberately
not driven by any requirement group. They are listed so that their absence is a
contract violation rather than a surprise, and so that no author reads their
silence as permission to skip them. Every one of them is a C mirror of a Rust
method step 08 or step 09 already required, so nothing here asks for new
behaviour -- only for the `extern "C"` wrapper.

The largest block of these is the path-event and migration family:
`quiche_conn_send_on_path`, `quiche_conn_migrate`,
`quiche_conn_path_event_next`, `quiche_path_event_type`,
`quiche_path_event_free` and the six per-variant accessors
`quiche_path_event_new`, `quiche_path_event_validated`,
`quiche_path_event_failed_validation`, `quiche_path_event_closed`,
`quiche_path_event_reused_source_connection_id` and
`quiche_path_event_peer_migrated`. Their declarations are pinned in the header
listing above and must compile; no group at this step calls them. Each mirrors a
Rust method step 08 already checked -- `cap-send-on-path`, `cap-migrate`,
`cap-probe-and-migrate` and `cap-path-events` -- and step 08's
`cap-probe-and-migrate` is carried into this step's gate as
`reg-08-probe-and-migrate`, so the *behaviour* is still observed at this
milestone; what is not observed is the C wrapper around it. Getting a
`quiche_path_event` out of the C API needs a second local address validated to
completion, and how promptly that exchange completes is a scheduling question
the harness does not control from inside a single C process. `cap-ffi-paths`
therefore checks the part of this surface that is deterministic without a second
path -- the error codes -- and leaves the event queue here.

The rest, one bullet each:

* `quiche_conn_paths_iter`, with `quiche_socket_addr_iter_next` and
  `quiche_socket_addr_iter_free`. A `quiche_socket_addr_iter` over the peer
  addresses reachable from a given local address, mirroring
  `Connection::paths_iter`.
* `quiche_conn_migrate_source`, mirroring `Connection::migrate_source`.
* `quiche_conn_send_quantum_on_path`, mirroring
  `Connection::send_quantum_on_path`. Pacing is not observable through any
  surface this package uses, so its value is never asserted.
* `quiche_conn_is_resumed` and `quiche_config_set_ticket_key`. Session
  resumption needs a cached ticket to observe and no group at this step
  establishes one.
* `quiche_config_set_disable_dcid_reuse`, mirroring
  `Config::set_disable_dcid_reuse`. Configuration whose effect no group at this
  step drives to an observable difference.

Also required and out of scope: `quiche_conn_is_dgram_send_queue_full` and
`quiche_conn_is_dgram_recv_queue_full`, the C mirrors of the two step 04
predicates.

## The groups at this step

One row per group. **gate** groups are all-or-nothing: a failure means the step is not satisfied; **requirement** groups cover this step's own new capability. The rows are grouped by tier for reading; the order they run in, and each one's result, is what `reward_details.json` records.

| group | tier | surface | what it observes |
| --- | --- | --- | --- |
| `candidate-workspace-builds` | gate | T4 | The one check every step shares: `cargo build --offline` (default targets -- libraries and binaries only, not tests or benches) must complete for the workspace at `/app` within the harness's build timeout, before any other group in this table runs. It is the one group every step carries identically, and no step declares it as a capability of its own -- but it is not optional either: one failing gate group, including this one, means the step is not satisfied, however much of the new work is right. |
| `cap-bbr2` | requirement | T3 | Three things, and the first two are cheap only if the third is also true. The **name table**: `set_cc_algorithm_name("bbr2")` returns `Ok(())`, and `"reno"`, `"cubic"` and `"bbr"` still do. `"BBR2"`, `"bbr2 "`, `"bbr3"` and `""` all return `Err(quiche::Error::CongestionControl)`, so the accept list grew by exactly one entry and did not become permissive. The **discriminant**: `(Reno as u8, CUBIC as u8, BBR as u8, BBR2 as u8)` is `(0, 1, 2, 3)`, asserted as a tuple in one assertion so the message carries all four. BBR2 has to be appended after BBR rather than inserted among the existing three, because the values cross the C boundary and step 01's and step 02's C groups pin the earlier ones. The **behaviour**: a `pair::Pair` built with `set_cc_algorithm(CongestionControlAlgorithm::BBR2)` on both sides handshakes, carries a 256 KiB body to completion byte-for-byte, and ends with a congestion window no smaller than the one it started the transfer with (the assertion is `>=`, not `>`) and `Stats::sent_bytes` at least the length of the body. Only ratios and inequalities are asserted, never an absolute window size or strict growth: a BBR-family controller's window is a function of its bandwidth and RTT estimates, an in-memory pair with no propagation delay reads both off the host's scheduling, and pinning a number -- or demanding the window rise -- would be pinning this machine. The same transfer is run once under `"bbr"` and once under `"bbr2"` and both must complete, so a candidate that aliases the new name onto the old algorithm still passes this group -- that is deliberate, since no observation available to a black-box checker can distinguish the two control laws on a lossless in-memory path, and asserting a difference would be asserting a machine-dependent accident. |
| `cap-ffi-paths` | requirement | T1/T4 | A C program the checker writes is compiled against `/app/quiche/include/quiche.h`, linked against the `libquiche.a` produced by `cargo build --features ffi`, and run. The checker builds the candidate's library itself with that feature into a scratch target directory, so a candidate whose default build omits the FFI is not penalised; a candidate whose `ffi` feature does not exist, or does not produce a `staticlib`, is. The link line is `-lpthread -ldl -lm -lstdc++` and nothing else, so the archive has to be self-contained exactly as step 01's group required. The program then drives the new path and connection-ID surface and prints a `key=value` report the checker parses: * `quiche_connect`, then `quiche_conn_send` to pull the Initial out, so there is a real connection to ask questions of. * `quiche_conn_scids_left` and `quiche_conn_active_scids` before and after a `quiche_conn_new_scid` call, with the sequence number the call writes through its out-parameter. The first issued ID is sequence 1 and `active_scids` rises by one, which is the C mirror of the step 08 Rust group. * `quiche_conn_source_ids` walked with `quiche_connection_id_iter_next` until it returns `false`, reporting how many IDs it yielded and the length of the first one; then `quiche_connection_id_iter_free`. The iterator has to be a real iterator over the connection's state, not a snapshot of one element. * `quiche_conn_peer_transport_params` returning **`false`** on a client that has not yet processed the server's flight -- the C mirror of `None`, and the observable half of the `Stats` split. A candidate that returns `true` with a zeroed struct fails. On a handshaken server it returns `true`, and the program then reads the ten leading `uint64_t` members of `quiche_transport_params` back and compares them against the seven distinctive numbers the peer's `quiche_config_*` calls set plus the three RFC 9000 Section 18.2 defaults the peer left alone. **Only those ten are read.** The Rust mirror of this struct carries no `#[repr(C)]` at this release, so the compiler is free to reorder the `bool` and the three members behind it and the C side reads garbage there; the ten leading `uint64_t`s line up under the reference layout and would still line up if the attribute were added, so they are the part of the struct a candidate can be held to. * `quiche_conn_probe_path` on a connection with no spare connection IDs returning the C value of `Error::OutOfIdentifiers` (-18), and, once IDs have been issued, returning 0 and driving `quiche_conn_is_path_validated` for that 4-tuple from 0 to 1. `quiche_conn_is_path_validated` on a 4-tuple that is not a path of the connection returns `QUICHE_ERR_INVALID_STATE` (-6) rather than crashing or reporting a plain `false`: the C function returns `v as c_int` on success and the error code otherwise, so -6 is distinguishable from 0. * `quiche_conn_retired_scids` reading zero on a fresh connection, rising to 1 after `quiche_conn_retire_dcid` retires a peer ID, and `quiche_conn_retire_dcid` on a sequence number that was never issued failing with a code that says why: `QUICHE_ERR_OUT_OF_IDENTIFIERS` (-18) on a client holding a single destination ID and no spare to replace it with, because the spare is checked before the sequence number is looked up, and `QUICHE_ERR_INVALID_STATE` (-6) once a spare exists and the sequence number itself is the problem. The same program then drives a real in-memory HTTP/3 exchange over the FFI -- `quiche_h3_send_request`, `quiche_h3_send_priority_update_for_request`, `quiche_h3_send_goaway` and a `quiche_conn_stream_shutdown` with error 0x100 -- and asks `quiche_h3_event_type` for the integer behind each event `quiche_h3_conn_poll` hands back. HEADERS, GOAWAY, RESET and PRIORITY_UPDATE must report 0, 3, 4 and 5, the values the header's `enum quiche_h3_event_type` declares. At the previous milestone the implementation returned 4, 5 and 6 for the last three while the header declared 3, 4 and 5, so every C caller that switched on the enum took the wrong branch; this half of the group fails on step 09's tree without reading a line of anyone's source. On step 09's tree it does not even link: ten of the functions the program calls are not in that release's header. Nothing here greps the header or the Rust source. Every value comes from a compiled, linked, executed C program, and a declaration that disagrees with the Rust behind it produces either a link error or a wrong number. |
| `cap-mtu-updated-qlog` | requirement | T4 | The `MtuUpdated` event as the candidate's own `qlog` crate serialises it. The checker writes a small Rust program depending on `/app/qlog` by path, builds it against the candidate's workspace, and runs it; the program constructs a `QlogStreamer` over a file in a scratch directory with `log_level: EventImportance::Extra`, calls `start_log()`, emits two `MtuUpdated` events and one event of a kind step 06 already pinned, then `finish_log()`. The checker reads the file back and parses it as a JSON Text Sequence, and requires: * the first record is the JSON-SEQ header step 06 specified -- `qlog_version` `"0.3"`, `qlog_format` `"JSON-SEQ"`, no `traces` key -- so the new event did not arrive by changing the envelope; * both `MtuUpdated` records have `name` exactly `"connectivity:mtu_updated"`, and each record's key set is exactly `{data, name, time}`; * the event emitted with only `new` set has a `data` object whose key set is exactly `{new}` -- `old` and `done` are omitted, not serialised as `null`, which is what `skip_serializing_none` on the struct means; * the event emitted with all three set has `data` keys exactly `{old, new, done}` with the numbers and the boolean the program passed in, round-tripped as JSON numbers rather than strings; * a second streamer, built at `EventImportance::Core` over a second file and fed the same `MtuUpdated`, writes **no** event record for it, because the event's importance is `Extra`. The program reports the `Err(Error::Done)` it gets back, and the second file holds its header record and the one `Core` control event and nothing else. This is what separates an implementation that added the schema type from one that also registered its importance -- and the control event is what separates it from a streamer that silently drops everything. No time value and no `reference_time` is asserted anywhere, and no substring search is used: every check is against parsed JSON key sets and values. |
| `cap-peer-transport-params` | requirement | T3 | `Connection::peer_transport_params()` and the disappearance of the `peer_*` fields from `Stats`, on a `pair::Pair` whose two `Config`s are given deliberately distinctive, mutually different values -- `set_initial_max_data`, `set_initial_max_streams_bidi`, `set_max_idle_timeout`, `set_max_recv_udp_payload_size`, `set_active_connection_id_limit` and `set_disable_active_migration`, every one of them a number (or a boolean) that differs between the client and the server and appears nowhere else in the harness. The particular numbers are a choice of this group's, not a protocol fact, and they are deliberately **not** written down here. Their whole job is to be values no candidate could produce except by carrying the peer's parameters across the handshake and reading them back out; a contract that named them would turn the assertion into something a lookup table could satisfy. What the contract pins is the shape -- every value distinct, every value different on the two ends, none of them a default -- and the accessor's obligation to report the *peer's* number for each one. Before any packet is exchanged, `peer_transport_params()` is `None` on both peers. That is the assertion the old `Stats` mirror could not express, and a candidate that returns `Some(TransportParams::default())` from the start fails here rather than anywhere else. After the handshake, each peer reads back **the other's** configured values, not its own: the client's view of `initial_max_data` is what the server was given, and the asymmetry is what catches an implementation that wired the accessor to the local parameters. `max_idle_timeout` is in milliseconds, matching the setter. `disable_active_migration` crosses as a boolean in both directions independently. `max_udp_payload_size` and `active_conn_id_limit` are asserted separately from the rest, because a struct that carried *some* of the peer's answers and its own configuration for the remainder would satisfy one block and not the other. The two parameters no `Config` in this group touches -- `ack_delay_exponent` and `max_ack_delay` -- read as RFC 9000 Section 18.2's defaults, 3 and 25, on both ends. A peer that accepts a default does not send the parameter at all, so an implementation that fills the struct only from what appeared on the wire reports zero here and fails. `initial_source_connection_id` is `Some` and equals the connection ID the peer actually used, which is the one field that ties the struct to this connection rather than to a config; `retry_source_connection_id` and `max_datagram_frame_size` are `None`, because no Retry was issued and neither side enabled DATAGRAM. The group also requires that the `Stats` struct returned by `stats()` has the `peer_*` fields **gone**: it reads every one of the fourteen fields `Stats` is specified to have, in one destructuring-style assertion, so a `Stats` that still carries the thirteen mirrors alongside the new counters does not compile against this group and a `Stats` missing any of the fourteen does not either. |
| `cap-scid-renames` | requirement | T3 | The four renamed methods, driven through the same sequence step 08 and step 09 drove the old names through, so the group is a rename check with real behaviour underneath it rather than a symbol-existence check. A `pair::Pair` handshakes with `set_active_connection_id_limit(3)`. Then: `scids_left()` is positive and `active_scids()` is 1 on each peer; `new_scid(&fresh_id, token, false)` returns `Ok(1)` on the client, because sequence 0 came from the handshake; `active_scids()` rises to 2 and `scids_left()` falls by one, so the two accessors are views of one piece of state and not independent counters; driving the pair makes the server's `available_dcids()` rise, which is the observable consequence of the NEW_CONNECTION_ID frame arriving; issuing past the peer's limit with `retire_if_needed = false` returns `Err(quiche::Error::IdLimit)`; and `retire_dcid(0)` on the server, driven until the retirement reaches the client, raises the client's `retired_scids()` to 1 and leaves `source_ids()` yielding exactly as many IDs as `active_scids()` counts -- two, the handshake's and the one `new_scid` issued, with the retired one gone from the iterator. `source_ids()` and `retired_scids()` are read throughout under their step 09 names, which is the half of the assertion that pins the rename as *exactly four methods*: a candidate that renamed everything containing "source" to "scid" breaks `source_ids()` and fails here, and a candidate that renamed nothing fails to compile against the group at all. The group also calls two singular accessors the rename leaves alone, `Connection::source_id()` and `Connection::destination_id()`, and compares their values -- `source_id().into_owned()` must come back out of `retired_scid_next()`, and `destination_id().as_ref()` must equal the peer's advertised `initial_source_connection_id`. |
| `cap-stream-reset-stop-counts` | requirement | T3 | The four new `Stats` counters, and specifically the asymmetry RFC 9000 Section 3.5 creates between the two endpoints of one connection. A `pair::Pair` handshakes and the client writes one byte to each of streams 0, 2, 4 and 8. All four counters read zero on both peers at that point. **Reset path.** The client calls `stream_shutdown(0, Shutdown::Write, 1001)` and the pair is driven until quiet. The client now reads `reset_stream_count_local` 1 and `reset_stream_count_remote` 0, while the server reads local 0 and remote 1: the same RESET_STREAM frame is counted under a different name at each end, which is the whole point of the local/remote split. The server then resets stream 0 in reaction and both peers read 1/1. The client resets streams 2, 4 and 8; the server resets 4 and 8; and the final reading is client local 4 / remote 3 and server local 3 / remote 4 -- the two endpoints disagree, by exactly the number of resets each originated. Every reading is taken after the pair has been driven, because the counters move when a frame is sent or received, not when the API call returns. **Stop path**, on a fresh pair. The server calls `stream_shutdown(0, Shutdown::Read, 1001)`, which sends STOP_SENDING. After driving, the server reads `stopped_stream_count_local` 1 and `reset_stream_count_remote` 1, and the client reads `stopped_stream_count_remote` 1 and `reset_stream_count_local` 1: the client auto-reset the stream because Section 3.5 requires it, so one API call moved a counter on each side of each name. Repeating for four streams gives server 4/0/0/4 and client 0/4/4/0 across (stopped_local, stopped_remote, reset_local, reset_remote). An implementation that counts API calls rather than frames, or that counts both directions into one field, cannot produce that table. |
| `cap-path-challenge-queue` | requirement | T3 | `Stats::path_challenge_rx_count` and the bound `Config::set_path_challenge_recv_max_queue_len` puts on responding. On a pair with `set_active_connection_id_limit(4)` and connection IDs exchanged, the server's `path_challenge_rx_count` is zero after the handshake and after the client has sent STREAM frames on the active path: ordinary data does not move it. The client then probes a second local address; the server's count reaches 1 and rises by exactly one per probe, so the counter is of PATH_CHALLENGE frames and not of packets or of paths. The count moves **before** the queue decides anything. With `set_path_challenge_recv_max_queue_len(1)` on the server and the same 4-tuple re-probed three further times, each datagram delivered before the server is ever given a chance to answer, four challenges reach a path whose queue holds one -- and the count rises by all four. A counter incremented where the frame is queued would stop at one. That is the assertion that distinguishes a counter incremented on receipt from one incremented at the point of queueing. What the group deliberately does **not** assert is a *behavioural* consequence of the bound. Measured against the reference: with the bound at one and four challenges outstanding, the path still validates, because the one challenge that was queued still produces a PATH_RESPONSE and one response is all validation needs. The discard is genuinely invisible from outside the connection; only the counter's disagreement with it is visible. The group asserts the path *does* validate, so that a candidate who made the discard observable by dropping validation would be caught too -- it reads that through `Connection::is_path_validated(local, peer)`, called by name on the probed four-tuple and required to be `Ok(true)` (see the interface element above for the shape, which is a `Result<bool>` and not the `int` the C wrapper returns). `send_on_path` for a 4-tuple that is not a path of this connection returns `Err(quiche::Error::InvalidState)` -- checked on a foreign local address and on a foreign peer address separately, because the check is on the 4-tuple and not on either half -- and `send_on_path` on the active path with nothing pending returns `Err(quiche::Error::Done)` rather than an empty packet. Both are inherited from step 08 and re-read here because the queue bound must not have changed them. |
| `reg-08-probe-and-migrate` | gate | T1 | The candidate's own client and server complete a half-megabyte transfer over UDP loopback with `--perform-migration`, the client exits zero and the bytes are identical. Carried because this step renames the connection-ID methods the migration path is built out of, and a rename applied to the library but not to `apps/` breaks exactly this. Step 08's group also required the client to be *observed* holding two UDP sockets at once, which is what distinguishes a migration from a reconnection. That leg is not carried into the gate. It needs a sampler -- the harness polls `/proc/<pid>/fd` against `/proc/net/udp` roughly every 20 ms -- and whether a poll lands inside the overlap is a question about the host's scheduler. Step 08's `cap-probe-and-migrate` still asks it, where a miss fails only that group. |
| `reg-09-key-update` | gate | T2 | The verifier owns a UDP socket, completes a handshake as the peer, flips the key-phase bit, and requires the candidate to derive the next generation of application keys with the `"quic ku"` label while reusing its header- protection key, to answer under the new generation with its own phase bit flipped, and to reject a second update before the first is acknowledged with `Err(Error::KeyUpdate)`. Carried because it is the step immediately before this one and its machinery sits underneath every short-header packet the new C entry points send. |
| `reg-04-datagram-roundtrip` | gate | T3 | With `enable_dgram(true, 10, 10)` on both sides, a 256-byte datagram is echoed back byte-for-byte, it never appears as stream data, and both queues drain to `Err(Error::Done)`. Carried because this step adds C predicates over those same two queues and because the transport DATAGRAM API is the one thing step 09 deleted *around* without touching. |
| `gate-endtoend` | gate | T1 | The stack works at all: the candidate's `quiche-server` starts, the candidate's `quiche-client` completes a handshake against it over UDP loopback and retrieves a small file, and the client exits zero. If this fails, every other T1 observation at this step is noise. |

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

`/app` contains everything step 09 left behind, unchanged. The milestone before this one is modelled on quiche 0.18.0; this one is modelled on 0.20.0. Read `WORKSPACE_HANDOFF.md` for what is already there and what must keep working.

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

