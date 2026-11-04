# Test interface — step 14, A netlog reader, a Huffman coder that changed crates, and a retry-aware accept

This is the contract between your implementation and the checker. Every
name, path and flag the checker depends on is written down here; nothing
outside this document is assumed about your code. Where the document and
your intuition disagree, the document wins — it is what the tests were
written against.

Generated, together with this step's interface contract, from one source.
The two cannot disagree.

## What this step is

Two new workspace members, one piece of existing machinery that moves down a
layer and changes shape on the way, a server-side accept that finally knows
about the Retry it just sent, and three smaller additions on the connection.

**A netlog reader, as a workspace member.** `netlog` is a deserializer for the
line-delimited JSON that Chromium writes when a user exports a network log. Its
first line is a *constants* object that maps every event type, phase and source
type used in that file to an integer, and every subsequent line is one event
that refers to those integers rather than to names. The crate reads the
constants, inverts the maps, and turns a raw record into a typed event for the
QUIC, HTTP/3, HTTP/2 and HTTP families it knows about.

The numbering is not a constant of the format. Two netlogs written by two
Chromium builds may give `QUIC_SESSION` different integers, so a reader that
hard-codes any of them is wrong on the next file it is handed. The case
is built on exactly that property: its fixture uses numbers no Chromium build
ever used.

**A second new member, `qlog-dancer`**, a log-analysis and visualisation tool
built on `netlog` and `qlog`. The workspace goes from nine members to eleven.

**The Huffman coder changes crates.** The HPACK Huffman code -- the one QPACK
string literals use -- used to live inside the transport crate, under
`h3::qpack`, where nothing outside it could reach. It is now in `octets`,
behind a non-default feature named `huffman_hpack`, as three public entry
points: a decoder on the octet reader, an encoder on the octet writer, and a
free function that answers how long an encoding would be without producing it.
The transport crate becomes a *consumer* of it, depending on `octets` with that
feature turned on.

That is the inversion. The same code, the same table, the same answers --
reachable from outside the workspace for the first time, and no longer present
at the path it used to occupy.

**`accept_with_retry`.** Until now a server that sent a Retry re-derived the
connection IDs by convention: `accept` took the original destination CID and
assumed the Retry's source CID was the same as the new connection's SCID. The
new entry point takes them as data -- a `RetryConnectionIds` carrying both --
so the server may choose a fresh SCID for the connection that is not the SCID
it put in the Retry. `accept` keeps its old behaviour, exactly.

**Three additions on the connection.** `Connection::pmtu` reports the path MTU
that discovery settled on, and `revalidate_pmtu` asks for it to be measured
again; `Connection::stream_discard` consumes bytes from a stream without
copying them anywhere; and a STOP_SENDING that is lost is now retransmitted for
as long as the peer's sending half is still open.

## The flip

Something an earlier step established changes meaning here. This is the
part a candidate most often gets half-right: the new behaviour appears,
the old one is left working beside it, and both the new group and the
inherited group pass for the wrong reason — until the step that
requires the old behaviour to be *gone*.

The Huffman coder is not where it was. Code that reached it through the
transport crate's internals is looking at a path that no longer exists, and
code that wants it now names `octets` and turns a feature on. A workspace that
depends on `octets` without `huffman_hpack` gets an `octets` with no Huffman in
it at all and no error saying so -- the feature is off by default, and the
three entry points are simply absent.

The encoder and the length function are generic over a `const LOWER_CASE: bool`
rather than taking a flag. The parameter is not decoration: with it `true` the
input is lowercased before it is coded, which changes both the bytes and the
length, so the two are the same function only for input that was already lower
case.

`huffman_encoding_len` can fail. It is not a pure size calculation that always
has an answer: input whose Huffman encoding would be no shorter than the input
itself is refused rather than measured, because a coder that never refuses
would hand its caller a "compressed" form that is larger. A caller that treats
the return as infallible has no path for the case the crate exists to signal.

`accept_with_retry` separates two connection IDs that `accept` conflated. The
client validates the pair: it remembers the destination CID it used on its
first Initial and the source CID of the Retry it got back, and it checks both
against the transport parameters the server sends. A server that passes either
one wrongly does not get a connection with a warning -- it gets a client that
closes with a transport parameter error, and the server never learns why.

`pmtu()` returns an `Option`, and `None` has two different causes that a caller
cannot tell apart: discovery was never enabled, or it is enabled and has not
finished. Both are "no answer yet". It is not an error, and a connection that
carries data perfectly well may answer `None` for its whole life.

## Interface elements the checker depends on

### `The eleven-member workspace and its two new members`

The workspace grows from nine members to eleven. The nine that were already
there stay, under the same directory names and the same package names:

```
apps  buffer-pool  datagram-socket  h3i  octets
qlog  quiche  task-killswitch  tokio-quiche
```

and two arrive:

```
netlog  qlog-dancer
```

For both, the directory name and the package name are the same string, so
`<root>/qlog-dancer/Cargo.toml` is the manifest of the package named
`qlog-dancer`. `exclude` keeps its two entries, `fuzz` and `tools/http3_test`,
and neither is a member.

The case reads the workspace the way cargo does:

```
cargo metadata --offline --no-deps --format-version 1
```

and takes the `packages` array that comes back. `--no-deps` is what makes this
a question about the workspace rather than about the dependency graph: the
answer is the members and nothing else, so the count is exactly eleven. It is
counted by the number of local-path package ids, not by matching names against
a list, because `qlog` is a substring of `qlog-dancer` and a substring match
would find the first inside the second.

`netlog` is a library with no binary. `qlog-dancer` is a tool, and it depends
on `netlog`, on `qlog`, and on one crate that comes from a git revision rather
than from a registry -- which is a fact about the lockfile, not about the code:
the revision is pinned, and a workspace that is fetched once can be built
offline afterwards.

### `octets, behind the huffman_hpack feature`

`octets` gains a feature, off by default:

```toml
[features]
# Build the Huffman encoding support as defined by HPACK (RFC7541).
huffman_hpack = []
```

and, behind it, three public entry points:

```rust
impl<'a> Octets<'a> {
    /// Decodes a Huffman-encoded value from the current offset.
    pub fn get_huffman_decoded(&mut self) -> Result<Vec<u8>>;
}

impl<'a> OctetsMut<'a> {
    /// Writes `v` to the current offset after Huffman-encoding it.
    pub fn put_huffman_encoded<const LOWER_CASE: bool>(
        &mut self, v: &[u8],
    ) -> Result<()>;
}

/// Returns how long the Huffman encoding of the given buffer will be.
pub fn huffman_encoding_len<const LOWER_CASE: bool>(src: &[u8])
    -> Result<usize>;
```

The code is the one defined for HPACK, and it is the code QPACK string
literals use. It is fully determined by the specification: for a given input
and a given `LOWER_CASE`, there is exactly one correct output and exactly one
correct length, and the case compares against a table of them.

Three properties are worth stating because each is a place an implementation
can be wrong while looking right:

* **`LOWER_CASE` is applied before coding, not after.** With it `true`, each
  byte is ASCII-lowercased and *then* looked up, so `WWW.EXAMPLE.COM` and
  `www.example.com` produce identical bytes and identical lengths. With it
  `false` they do not, and the upper-case form is the longer of the two.
* **The length function is the encoder's own arithmetic, not a second
  implementation of it.** `huffman_encoding_len::<L>(v)` is the number of bytes
  `put_huffman_encoded::<L>(v)` will write, for every `v` where both succeed.
* **Unprofitable input is refused, and equality is not unprofitable.** The
  encoding is padded to a byte boundary with ones, so for some inputs it comes
  out **strictly longer** than the input, and that is the case the length
  function reports as an error rather than as a number: a caller is expected to
  fall back to the literal form, which is what QPACK's Huffman bit is for. An
  input whose encoding is exactly as long as the input is *not* refused -- it is
  measured and returned, and the four rows that turn on `a` and `""`
  depend on that reading. `put_huffman_encoded` performs no such check at all:
  it writes the encoding for whatever it is handed, profitable or not, and the
  refusal lives only in the length function.

The encoder writes into the buffer it is called on and advances it; a buffer
too small for the encoding is a `BufferTooShort`, not a partial write.

The transport crate is now a consumer of this. Its own manifest says

```toml
octets = { workspace = true, features = ["huffman_hpack"] }
```

and `quiche/src/h3/qpack` no longer contains a Huffman implementation of its
own. Nothing about that is observable from outside except through `octets`,
which is the point: the case builds a crate that is not in the workspace
at all, depends on `octets` by path with the feature on, and exercises the
three entry points from there.

### `The netlog crate`

A netlog file is line-delimited JSON with a particular shape. The first line is
a *constants* object and ends in a bare comma rather than a closing brace; the
second line is the literal `"events": [` and carries no event; every line after
that is one event object ending in a comma, except the last, which closes the
array and the document.

The public surface the case uses:

```rust
pub fn read_netlog_constants<R: BufRead>(reader: &mut R)
    -> Result<Constants, serde_json::Error>;

pub fn read_netlog_record<R: BufRead>(reader: &mut R) -> Option<Vec<u8>>;

pub struct EventSource {
    #[serde(skip)]        // not in the netlog format; see below
    pub start_time_int: u64,
    pub id: i64,
    pub start_time: String,
    pub ty: i64,          // serde name: "type"
}

pub struct EventHeader {
    pub ty_string: String,     // filled in by populate_strings
    pub phase_string: String,  // filled in by populate_strings
    pub time_num: u64,         // filled in by the caller
    pub phase: i64,
    pub source: EventSource,
    pub time: String,
    pub ty: i64,               // serde name: "type"
}

impl EventHeader {
    pub fn populate_strings(&mut self, constants: &Constants);
}

pub enum Event { Http(http::Event), H2(h2::Event), H3(h3::Event), Quic(quic::Event) }

pub fn parse_event(event_hdr: &EventHeader, event: &[u8]) -> Option<Event>;

pub mod constants;  pub mod h2;  pub mod h3;  pub mod http;  pub mod quic;
```

`Constants` deserializes the first line with camel-case renaming, so
`logFormatVersion` is `log_format_version`, `timeTickOffset` is
`time_tick_offset`, `logEventTypes` is `log_event_types`, and so on. It also
carries an inverted copy of each map -- `log_event_types_id_keyed`,
`log_event_phase_id_keyed` and the rest -- which is empty until
`populate_id_keyed` has run. `read_netlog_constants` runs it before returning,
so a caller never sees the uninverted state.

**How the three readers compose.** `read_netlog_constants` takes the first
line, replaces its trailing comma with a closing brace, and parses the result.
`read_netlog_record` returns the next line with its trailing comma and newline
removed, and returns `None` at end of input, on a line that does not begin with
`{`, and on a short line. The final event line ends in `]}` rather than a
comma, and the reader strips the bracket so the object still parses.

**Names come from the file.** `populate_strings` looks `self.ty` up in the
file's own inverted type map and `self.phase` in its phase map. The integers
are the file's, not the format's: a netlog whose `QUIC_SESSION` is 500 and
whose `logEventPhase` says `PHASE_BEGIN` is 1 must be read that way, and the
same reader must read a file that numbers them differently. `time_num` is not
filled in by either call -- `time` is a decimal string in the JSON, and the
caller parses it.

**`parse_event` dispatches on the resolved name, not on the number.** The
prefix of `ty_string` selects the family: `HTTP_` goes to `http`, `HTTP2_` to
`h2`, `HTTP3_` to `h3`, and anything beginning `QUIC` to `quic`. Anything else
is `None`, and so is an event whose family module does not model that
particular type. `None` is the ordinary outcome for an unmodelled event, not an
error: a real netlog is mostly events no consumer cares about.

`start_time_int` is the one field in that header with no counterpart in the
file. It is derived from `start_time` (which is a string of decimal digits) and
it must be marked `#[serde(skip)]`, or make its absence from the JSON
tolerable some other way: the probe deserialises each record straight into
`EventHeader` with `serde_json::from_slice(..).expect(..)`, so a header type
that *requires* the key panics the probe on the first record of every netlog,
and `cap-netlog` is lost. Every other field above is present in the file and
should be read from it.

The `quic` and `h3` modules each expose an `Event` enum with one variant per
modelled type, and each variant holds a struct with a typed `params`. **The
names are part of this step's contract**, because the probe matches on
them by path rather than through a documented trait:

```rust
pub enum quic::Event {
    QuicSession(QuicSession),                                     // params: host, port, connection_id
    QuicSessionTransportParametersSent(QuicSessionTransportParametersSent),
                                                                  // params: quic_transport_parameters
    QuicSessionPacketSent(QuicSessionPacketSent),                 // params: encryption_level,
                                                                  //   packet_number, sent_time_us
    QuicSessionStreamFrameReceived(QuicSessionStreamFrameReceived),
                                                                  // params: stream_id, fin, offset, length
    QuicSessionStopSendingFrameSent(QuicSessionStopSendingFrameSent),
                                                                  // params: stream_id, quic_rst_stream_error
}

pub enum h3::Event {
    Http3HeadersSent(Http3HeadersSent),                           // params: stream_id, headers
    Http3HeadersDecoded(Http3HeadersDecoded),                      // params: stream_id, headers
    Http3DataSent(Http3DataSent),                                 // params: stream_id, payload_length
    Http3PriorityUpdateSent(Http3PriorityUpdateSent),              // params: prioritized_element_id,
                                                                  //   priority_field_value, ty
}

pub enum h3::PrioritizedElementType { RequestStream, PushStream }
```

Each variant is a tuple variant wrapping a struct of the same name, and that
struct's field is called `params`. `prioritized_element_id` is an integer,
`priority_field_value` a `String`, and `ty` an `Option<PrioritizedElementType>`
-- `None` is a value the fixture produces and the probe reports as
`absent`. **The types are part of the contract as well, because the probe is
compiled against them:** every `params` field it prints is printed with `{}`,
so each implements `Display`; the exception is `headers`, printed with
`.join("/")`, so it is a slice of string-like items -- `Vec<String>` is the
obvious choice. Both `Event` enums are printed with `{:?}` in the probe's
catch-all arm, so both derive `Debug`. Everything else in these modules is
yours to name as you like; these are not.

### `quiche::accept_with_retry and RetryConnectionIds`

```rust
/// A wrapper for connection IDs used in `accept_with_retry`.
pub struct RetryConnectionIds<'a> {
    /// The DCID of the first Initial packet received by the server, which
    /// triggered the Retry packet.
    pub original_destination_cid: &'a ConnectionId<'a>,
    /// The SCID of the Retry packet sent by the server. This can be different
    /// from the new connection's SCID.
    pub retry_source_cid: &'a ConnectionId<'a>,
}

pub fn accept_with_retry<F: BufFactory>(
    scid: &ConnectionId, retry_cids: RetryConnectionIds, local: SocketAddr,
    peer: SocketAddr, config: &mut Config,
) -> Result<Connection<F>>;
```

`quiche::retry` already existed and does not change: given the client's SCID,
the destination CID of its first Initial, a new source CID, a token and a
version, it writes a Retry packet. What was missing was the other end of the
exchange. `accept` takes an `odcid` and, where one is given, uses the new
connection's own SCID as the retry source CID -- that is the convention it has
always had, and it is kept for compatibility. It means a server using `accept`
must make the SCID of its Retry and the SCID of its connection the same value.

`accept_with_retry` takes the two as separate fields, so a server may generate
a fresh SCID for the connection after it has verified the token, and still tell
the client which Retry the connection descends from.

**The client checks both, and the check is not advisory.** A client that was
sent a Retry remembers the destination CID it used on its first Initial and the
source CID of the Retry it received. It compares them against the
`original_destination_connection_id` and `retry_source_connection_id` transport
parameters the server sends during the handshake. Either one wrong is a
`TRANSPORT_PARAMETER_ERROR`, code `0x08`: the client refuses the handshake and
closes, no connection is established at either end, and the failure is a
transport error rather than an application one.

So the three cases a server implementation must get right are:

| `retry_cids` passed | outcome |
|---|---|
| both fields correct | both ends establish, whatever `scid` was chosen |
| wrong `retry_source_cid` | client closes, `0x08`, neither end established |
| wrong `original_destination_cid` | client closes, `0x08`, neither end established |

The token is opaque to the crate. `quiche::retry` puts whatever bytes the
server gives it into the packet, and the client echoes them back in the
destination-CID-and-token of its second Initial without inspecting them; it is
the server's job to recognise its own token, and `accept_with_retry` does not
do that for it.

### `Connection::pmtu and Connection::revalidate_pmtu`

```rust
impl<F: BufFactory> Connection<F> {
    /// Returns the PMTU for the active path if it exists.
    ///
    /// This requires no additional packets to be sent but simply checks if
    /// PMTUD has completed and has found a valid PMTU.
    pub fn pmtu(&self) -> Option<usize>;

    /// Revalidates the PMTU for the active path by sending a new probe packet
    /// of PMTU size. If the probe is dropped PMTUD will restart and find a new
    /// valid PMTU.
    pub fn revalidate_pmtu(&mut self);
}
```

Path MTU discovery itself is not new -- `Config::discover_pmtu` has been there
since an earlier step, and so has `Config::set_pmtud_max_probes`:

```rust
impl Config {
    /// Configures whether to do path MTU discovery. Default: `false`.
    pub fn discover_pmtu(&mut self, discover: bool);

    /// Configures the maximum number of PMTUD probe attempts before treating
    /// a probe size as failed. Defaults to 3. If 0 is passed, the default is
    /// used.
    pub fn set_pmtud_max_probes(&mut self, max_probes: u8);
}
```

What is new is that the result is *readable*. Until now a caller could turn
discovery on and had no way to ask what it found.

`pmtu()` is a pure accessor: it sends nothing, it starts nothing, and calling
it does not change the connection. It reports the active path's discovered
value, and `None` means there is not one -- either because discovery is off for
this connection, or because it is on and has not yet converged. Those two are
deliberately indistinguishable, and neither is an error.

`revalidate_pmtu()` puts the active path back into the searching state, so a
`pmtu()` immediately after it is `None` again. The probe that resolves it is
sent on the ordinary send path, so the value reappears only once the connection
has been driven far enough to exchange the probe and its acknowledgement; a
caller that asks again without sending anything gets `None` forever.

On a loopback path with both ends discovering, the two ends converge on the
same value, and it lies between the QUIC minimum datagram size of 1200 bytes
and whatever maximum the endpoint is willing to send.

### `Connection::stream_discard`

```rust
impl<F: BufFactory> Connection<F> {
    /// Discards up to `len` bytes of readable data from the given stream,
    /// returning how many were discarded and whether this was the final
    /// piece of data on the stream.
    ///
    /// while let Ok((read, fin)) = conn.stream_discard(stream_id, 1) {
    ///     println!("Discarded {} byte(s) on stream {}", read, stream_id);
    /// }
    pub fn stream_discard(
        &mut self, stream_id: u64, len: usize,
    ) -> Result<(usize, bool)>;
}
```

The receive-side sibling of `stream_recv`, with the same contract in every
respect except that there is no output buffer. It consumes contiguous readable
data from the stream, advances the stream the same way a read would, and gives
the same `(usize, bool)` back: how many bytes it took, and whether the stream
is finished at that point.

Everything `stream_recv` does about limits and errors, this does too:

* It takes **at most** `len` bytes, and fewer if fewer are readable. It never
  blocks and never waits for more to arrive.
* When there is nothing readable it is `Error::Done`, which is the normal
  end of a discard loop and not a failure.
* `fin` is true only on the call that consumes the last byte before the
  stream's final offset, and `stream_finished` agrees with it afterwards.
* A stream the peer never created, or one this endpoint may not read from, is
  `Error::InvalidStreamState(stream_id)` -- and the id in the error is the id
  that was asked for.

Because the bytes are consumed rather than copied, the flow-control accounting
is the accounting of a read: discarding is how an application that has decided
it does not want a stream's data keeps the connection's window moving without
allocating anywhere to put it.

### `STOP_SENDING is retransmitted while the peer is still sending`

No new API. A behaviour that was missing arrives: when a packet carrying a
STOP_SENDING frame is declared lost, the frame is put back on the send queue
instead of being dropped.

RFC 9000 Section 13.3 is explicit about which control frames survive loss, and
STOP_SENDING is one of them -- *a request to cancel stream transmission, as
encoded in a STOP_SENDING frame, is sent until the receiving part of the stream
enters either a "Data Recvd" or "Reset Recvd" state*. The previous step's code
had no arm for this frame in its lost-frame handler at all, so a STOP_SENDING
in a lost packet was simply gone, and the peer went on sending data that this
endpoint had already decided to throw away.

The condition in the specification is the whole of the behaviour, and it cuts
both ways, which is why it is checked both ways:

| the receiving half of the stream | a lost STOP_SENDING is |
|---|---|
| still open -- the peer may send more | sent again |
| already complete -- the peer sent its fin | not sent again |

The second row is not an omission. Once all of the peer's data has arrived
there is nothing left to cancel, and re-sending the frame would ask the peer to
reset a stream it has already finished. An implementation that retransmits
unconditionally is as wrong as one that never retransmits; it is just wrong in
a direction that is harder to notice.

The frame is the one defined in Section 19.5, carrying the stream id and the
application error code the caller passed, and a retransmission carries the same
two values as the original.

### `MAX_STREAMS survives loss as well, and RESET_STREAM loses a guard`

STOP_SENDING is not the only arm the lost-frame handler gains at this
milestone. Two more frames change behaviour, and a reader who fixes only the
STOP_SENDING case has done two thirds of the work.

**MAX_STREAMS, both kinds.** A lost packet carrying MAX_STREAMS_BIDI or
MAX_STREAMS_UNI must cause the frame to be sent again. Section 13.3 puts it in
the same family and states the rule without a condition attached to it -- *an
updated value is sent in a MAX_STREAMS frame if the packet containing the most
recently sent MAX_STREAMS frame for that type is declared lost*. The previous
step's handler had no arm for either, and the consequence is worse than a
missed optimisation: the sender has already advanced its own record of the
limit it granted, so nothing will make it send another one, and the peer goes
on believing in a credit that was raised and never arrived.

**RESET_STREAM loses its guard.** The previous step re-queued a lost
RESET_STREAM only `if self.streams.get(stream_id).is_some()` -- that is, only
while the stream was still in the map. At this release the guard is gone and
the frame is re-queued unconditionally. The difference shows only for a stream
that has already been collected while its reset was in flight, which is a
narrow window and a real one.

What is in scope, and what is not:

| frame | at this step |
|---|---|
| STOP_SENDING | in scope, both directions of the Section 13.3 condition |
| MAX_STREAMS_BIDI | in scope, one leg -- a lost grant must come back |
| MAX_STREAMS_UNI | required, not separately checked |
| RESET_STREAM without the guard | required, out of scope |

MAX_STREAMS_UNI is out of scope because it is the same handler arm with a
different frame type and a different flag, and a second leg would re-measure
the code the bidirectional leg already covers rather than measure anything
new. The RESET_STREAM change is out of scope because reaching it means
arranging for a stream to be collected in the window between its reset being
sent and that packet being declared lost; the package has no way to hold a
connection at that point without a clock it can move, and a case that arranges
it by timing would be the kind of race a requirement group must not contain. Both
are stated here so that a candidate knows they are part of the release, and
neither is required.

### `Present at this release, and deliberately out of scope`

This release adds more than this step covers. These are named so that a reader
comparing the package against the upstream release knows they were seen and
left alone; implementing them is not required, and implementing them is not
rewarded.

```rust
impl Config {
    /// Sets the connection's initial RTT. The default value is `333` ms.
    pub fn set_initial_rtt(&mut self, v: Duration);

    /// Configure whether to enable relaxed loss detection on spurious loss.
    /// The default value is false.
    pub fn set_enable_relaxed_loss_threshold(&mut self, enable: bool);
}

impl<F: BufFactory> Connection<F> {
    /// Returns the early data reason for the connection, as BoringSSL
    /// defines it.
    pub fn early_data_reason(&self) -> u32;
}

#[cfg(feature = "custom-client-dcid")]
pub fn connect_with_dcid(
    server_name: Option<&str>, scid: &ConnectionId, dcid: &ConnectionId,
    local: SocketAddr, peer: SocketAddr, config: &mut Config,
) -> Result<Connection>;
```

Each is left out for a reason:

* `set_initial_rtt` is observable on a fresh connection -- the configured value
  reads back through `path_stats()` before any packet has been exchanged -- but
  the moment a handshake completes it is replaced by a measurement, and a
  measurement is a clock reading. There is no assertion here that is both
  interesting and deterministic.
* `set_enable_relaxed_loss_threshold` changes when a packet is *declared* lost,
  which is a timing decision. Observing it means constructing a loss pattern
  whose classification differs between the two settings, and that construction
  is exactly the kind of race a requirement group must not contain.
* `early_data_reason` returns a BoringSSL enumerator whose numbering is a
  property of the linked library rather than of this crate.
* `connect_with_dcid` and `connect_with_dcid_and_buffer_factory` are behind the
  non-default `custom-client-dcid` feature. Nothing in the package turns it on,
  and a feature that is off is not a surface.

One behaviour belongs in this list too, and it is not an API signature.

**Rate metrics reach qlog through the `ex_data` channel.** At this release the
recovery layer flattens three rate samples onto the qlog `MetricsUpdated`
event, under the keys `cf_delivery_rate`, `cf_send_rate` and `cf_ack_rate`,
each emitted only when its value has changed since the last event. The
previous milestone has none of the three anywhere in its tree. The same
mechanism carries `cf_lost_packets` and `cf_lost_bytes` as total-and-delta
pairs.

It is named here, and left out of scope, for a reason that is worth stating rather
than leaving to be rediscovered:

* The **values** are bandwidth samples taken over wall-clock intervals. There
  is no assertion about any of the three that is both interesting and
  deterministic -- quiche's own test module says as much where it tests this
  code, noting that modelling `delivery_rate` is non-trivial and testing only
  the parts that are not rate samples.
* The **presence of the keys** is deterministic and would be the assertion
  worth making, but reaching it means standing up a qlog writer and a JSON-SEQ
  reader inside a probe crate. No qlog-reading surface is carried forward past
  step 06's `cap-qlog-jsonseq`, and rebuilding one here to check that three
  keys appear would be a larger verifier than every requirement group at this step
  put together.

So the requirement is stated and unenforced. A candidate that emits the three
keys is right; a candidate that does not loses nothing for it at this step.

## The groups at this step

One row per group. **gate** groups are all-or-nothing: a failure means the step is not satisfied; **requirement** groups cover this step's own new capability. The rows are grouped by tier for reading; the order they run in, and each one's result, is what `reward_details.json` records.

| group | tier | surface | what it observes |
| --- | --- | --- | --- |
| `candidate-workspace-builds` | gate | T4 | The one check every step shares: `cargo build --offline` (default targets -- libraries and binaries only, not tests or benches) must complete for the workspace at `/app` within the harness's build timeout, before any other group in this table runs. It is the one group every step carries identically, and no step declares it as a capability of its own -- but it is not optional either: one failing gate group, including this one, means the step is not satisfied, however much of the new work is right. |
| `cap-huffman-in-octets` | requirement | T4 | The Huffman coder at its new address, reached from outside the workspace, and checked against the code the specification fixes. The verifier writes a small crate into scratch -- a `Cargo.toml` with its own empty `[workspace]` table so it is not pulled into the candidate's, and a path dependency on `<candidate root>/octets` with `features = ["huffman_hpack"]` -- and a `main.rs` that prints one line per observation. Building that crate is the first assertion, and it is the one that makes this a T4 group rather than a source inspection: an `octets` that has no such feature, or that has the feature but not the three entry points, does not compile against it, and nothing has been read from the candidate's source to find that out. It is built with `cargo build --offline -j 4` and with `CARGO_TARGET_DIR` pointed at the target directory the driver already used for the candidate's workspace, so the dependency is already compiled and the probe build is a link. The probe's output is compared against a fixed table, line for line, in order. A mismatch is reported as the first differing line with both sides quoted, so a failure names the input that was coded wrongly rather than saying the output differed. ``` enc|www.example.com|false|f1e3c2e5f23a6ba0ab90f4ff len|www.example.com|false|12 enc|no-cache|false|a8eb10649cbf len|no-cache|false|6 enc|custom-key|false|25a849e95ba97d7f len|custom-key|false|8 enc|custom-value|false|25a849e95bb8e8b4bf len|custom-value|false|9 enc|/index.html|false|60d5485f2bce9a68 len|/index.html|false|8 enc|a|false|1f len|a|false|1 enc||false| len||false|0 enc|WWW.EXAMPLE.COM|false|e5cb92f83f21d1af3e05ef6ad1 len|WWW.EXAMPLE.COM|false|13 enc|WWW.EXAMPLE.COM|true|f1e3c2e5f23a6ba0ab90f4ff len|WWW.EXAMPLE.COM|true|12 dec|f1e3c2e5f23a6ba0ab90f4ff|www.example.com dec|a8eb10649cbf|no-cache dec|25a849e95ba97d7f|custom-key dec|25a849e95bb8e8b4bf|custom-value dec|60d5485f2bce9a68|/index.html dec|| roundtrip|www.example.com|false|www.example.com roundtrip|WWW.EXAMPLE.COM|false|WWW.EXAMPLE.COM roundtrip|WWW.EXAMPLE.COM|true|www.example.com len_unprofitable|00|err len_unprofitable|00010203|err short_buffer|err ``` `enc` is `put_huffman_encoded::<LOWER_CASE>` into a fresh buffer, rendered as hex. `len` is `huffman_encoding_len::<LOWER_CASE>` on the same input, and every `len` line is the byte length of the `enc` line above it -- that pairing is the point of listing both. `dec` is `get_huffman_decoded` over the bytes. The three `roundtrip` lines encode and then decode, and the third is the one that shows `LOWER_CASE` is applied before coding rather than after: the value that comes back is lower case, so the transformation happened on the way in. The last three lines are the negatives. `len_unprofitable` asks for the encoded length of input whose encoding would not be shorter, and gets an error both for a single zero byte and for four ascending bytes. `short_buffer` asks the encoder to write `www.example.com` into a four-byte buffer and gets an error rather than a truncated write. Each is an exact-answer negative: the assertion is that the call failed, not that it failed with a particular message. The values are the canonical HPACK examples and are fixed by the code table. They were re-derived against the reference implementation before being written here. |
| `cap-netlog` | requirement | T4 | The netlog reader against a fixture the verifier writes, parsed into an exact expectation table. The fixture is a complete twelve-event netlog in the shape Chromium writes them: a constants line ending in a bare comma, an `"events": [` line, ten event lines ending in commas, and a final event line that closes the array and the document. It records one HTTP/3 session -- session begin, transport parameters sent, request headers, a priority update, a packet sent, a stream frame received, response headers decoded, data sent, settings sent, an unrelated socket event, session end, and a STOP_SENDING sent. **The event type numbers in the fixture are not Chromium's.** They are 500 to 504 for the QUIC types, 600 to 604 for the HTTP/3 types and 700 for the socket type, and the phase map gives `PHASE_NONE`, `PHASE_BEGIN` and `PHASE_END` the values 0, 1 and 2. Nothing outside this file uses those numbers. A reader that resolved names from a built-in table instead of from the file's own `logEventTypes` and `logEventPhase` maps produces a different answer for every line, which is precisely the property being checked: the constants are the file's, and the crate exists to read them. The probe crate -- again outside the workspace, again a path dependency, this time on `<candidate root>/netlog` plus `serde_json` -- reads the constants, skips the array-opening line, and then for each record prints ``` <index>|<type>|<phase>|<time>|<source id>|<source type>|<parsed event> ``` where the type and phase are the strings `populate_strings` resolved, the time is `time_num` after the caller parsed the decimal string, and the last field is a stable rendering of what `parse_event` returned. The rendering is written by the probe, not by the crate: it names the variant and the handful of fields the case cares about, so that the expectation does not depend on anyone's `Debug` output. ``` constants version=1 tick_offset=1700000000000 types=11 phases=3 0|QUIC_SESSION|PHASE_BEGIN|12|41|25|quic:session host=quic.test port=443 cid=5f6e7d8c 1|QUIC_SESSION_TRANSPORT_PARAMETERS_SENT|PHASE_NONE|13|41|25|quic:tp-sent initial_source_connection_id 5f6e7d8c 2|HTTP3_HEADERS_SENT|PHASE_NONE|21|41|25|h3:headers-sent id=0 headers=[:method: GET/:scheme: https/:authority: quic.test/:path: /index.html] 3|HTTP3_PRIORITY_UPDATE_SENT|PHASE_NONE|22|41|25|h3:priority-update elem=0 field=u=3, i ty=request_stream 4|QUIC_SESSION_PACKET_SENT|PHASE_NONE|23|41|25|quic:packet-sent level=ENCRYPTION_FORWARD_SECURE pn=7 sent_us=1234567 5|QUIC_SESSION_STREAM_FRAME_RECEIVED|PHASE_NONE|34|41|25|quic:stream-recv id=0 fin=false off=0 len=1450 6|HTTP3_HEADERS_DECODED|PHASE_NONE|35|41|25|h3:headers-decoded id=0 headers=[:status: 200/content-type: text/plain] 7|HTTP3_DATA_SENT|PHASE_NONE|41|41|25|h3:data-sent id=0 len=4096 8|HTTP3_SETTINGS_SENT|PHASE_NONE|42|41|25|none 9|SOCKET_ALIVE|PHASE_NONE|43|9|8|none 10|QUIC_SESSION|PHASE_END|44|41|25|none 11|QUIC_SESSION_STOP_SENDING_FRAME_SENT|PHASE_NONE|51|41|25|quic:stop-sending id=0 err=3 records=12 ``` Four lines matter most. Lines 0 and 10 are the *same* event type in two phases, and only one of them parses into a session event -- the reader distinguishes them by phase, not by type. Line 8 is a type the crate recognises by name and does not model, and line 9 is a type outside every family prefix; both must be `None` rather than an error, and a reader that treats an unmodelled event as a failure stops at line 8. Line 11 is the last record in the file, the one whose trailing `]}` the record reader has to strip, and it is the line that would be lost by an off-by-one at end of input -- which is why the count is printed and asserted rather than left implicit. The comparison is line for line against the table above, with the first difference reported. The whole fixture is the verifier's own bytes, so there is nothing timing-dependent, network-dependent or clock-dependent in it. |
| `cap-accept-with-retry` | requirement | T3 | A full Retry exchange driven through the public API, in memory, three times: once correctly and twice with one of the two connection IDs wrong. Each leg builds a client with `quiche::connect` and takes its first Initial without a server on the other side. The verifier parses that packet with `Header::from_slice`, and plays the server itself: it picks a Retry source connection ID that is deliberately **not** the connection's eventual SCID, calls `quiche::retry` with the client's SCID, the Initial's destination CID, that new SCID, a twenty-byte token and the packet's version, and hands the Retry back to the client. The client's response is the first observation. It produces a second Initial that carries a non-empty token and whose destination connection ID is the Retry source CID -- not the one it started with. Both are asserted. The server side is then built with `quiche::accept_with_retry`, given a fresh connection SCID and a `RetryConnectionIds` whose two fields the leg controls, and the two ends are pumped against each other for up to thirty-two rounds. | leg | `original_destination_cid` | `retry_source_cid` | expected | |---|---|---|---| | positive | the first Initial's DCID | the Retry's SCID | both ends established, client `local_error` is `None` | | negative A | the first Initial's DCID | a bogus CID | client not established, `local_error` is `(is_app = false, 0x08)` | | negative B | a bogus CID | the Retry's SCID | client not established, `local_error` is `(is_app = false, 0x08)` | The two negatives are what make the positive mean something. A server implementation that ignored `retry_cids` entirely and echoed its own SCID -- which is what `accept` does -- would pass a positive-only version of this group whenever the test happened to choose matching IDs, and the whole point of the new entry point is that they need not match. Here the negatives are what catch a conflation. The positive leg builds the server with the *same* connection ID the Retry carried -- `accept_with_retry` is handed the Retry source CID as both `retry_source_cid` and its own SCID -- so a server that treats the two fields as one passes it; the two negative legs are where the pair is deliberately mismatched, one field at a time, and a server that ignores `retry_cids` and echoes its own SCID fails both. `0x08` is `TRANSPORT_PARAMETER_ERROR`, and it is required by RFC 9000 Section 7.3 rather than chosen by this crate. The assertion is on the error code and on the `is_app` flag being false; nothing reads a reason string. **In memory, and on purpose.** This is a T3 group rather than a T1 one because the shipped `quiche-server` does not use this entry point -- it calls `quiche::accept` -- so there is no command line that would exercise it. The public API is the only surface it is reachable from. |
| `cap-retransmit-stop-sending` | requirement | T2 | That a lost STOP_SENDING comes back when the peer may still send, and does not when the peer has finished -- decrypted off the wire, both ways. Each leg runs a handshaked pair and then: 1. the peer opens a stream and sends data on it, with or without a fin depending on the leg; 2. this endpoint calls `stream_shutdown` with `Shutdown::Read` and an application error code, which queues a STOP_SENDING; 3. the datagram carrying it is taken and **withheld** -- never delivered; 4. six further rounds of ordinary traffic are exchanged on a second stream, so the peer acknowledges packets numbered after the withheld one; 5. every datagram this endpoint sent is decrypted with 1-RTT keys taken from the connection's own keylog, and its frames are decoded. Loss is established by the packet reordering threshold of RFC 9002 Section 6.1 -- later packets acknowledged, the withheld one not -- and not by waiting for a timer. Both legs assert that at least one packet was declared lost, so a leg that failed to create the loss it was arranging fails loudly instead of quietly reporting "no retransmission". | leg | the peer's stream when STOP_SENDING is sent | expected | |---|---|---| | A | still open, no fin sent | the frame appears again in a later packet | | B | already finished, fin delivered | it does not appear again | The frame is matched exactly -- the STOP_SENDING variant with this stream id and this application error code -- not by counting frames or by searching for a type byte. The stream id and the error code are this group's own choices, held in `const`s inside the case and deliberately not named here: what is asserted is that the code handed to `stream_shutdown` in step 2 is the code that comes back on the wire, and a contract that pinned a number would let an implementation emit *that* number instead of carrying the caller's. The packets are the real ones: they are read out of the send path, decrypted with the traffic secret from the keylog, and parsed into frames, so nothing here depends on an internal counter or a log line. Leg B is the half that a plausible wrong implementation fails. Retransmitting unconditionally satisfies "the frame comes back" and violates Section 13.3's condition, which stops the retransmission once the receiving part of the stream has reached "Data Recvd". Both legs were measured against the previous step's tree as well: there, neither leg retransmits, because that tree has no arm for this frame in its lost-frame handler at all. |
| `cap-retransmit-max-streams` | requirement | T2 | That a lost MAX_STREAMS_BIDI comes back -- decrypted off the wire, like its STOP_SENDING sibling. The case runs a handshaked pair and then: 1. completes client-initiated bidirectional streams, one at a time, until a datagram the **server** sends is found to carry a MAX_STREAMS frame for bidirectional streams. The loop is bounded and fails loudly if no grant ever appears, so a candidate that never regrants stream credit fails here rather than skipping the part that matters; 2. **withholds** that datagram -- it is never delivered -- and delivers the rest of the same batch; 3. exchanges six further rounds of ordinary traffic, so the client acknowledges packets numbered after the withheld one; 4. decrypts every datagram the server sent afterwards with 1-RTT keys taken from the connection's own keylog, and decodes their frames. Loss is established by the packet reordering threshold of RFC 9002 Section 6.1 -- later packets acknowledged, the withheld one not -- and not by waiting for a timer. The case asserts that the server declared at least one packet lost, so a run that failed to create the loss it was arranging fails loudly instead of quietly reporting "no retransmission". The keys are the **server's**, read from the client's keylog, because the server is the endpoint granting credit here. The frame is matched on its variant and its bidirectional flag, not by counting frames or searching for a type byte. One property of the arrangement carries the whole leg, and it is deliberate: **the filler traffic in step 3 runs on a unidirectional stream.** Completing more bidirectional streams would give the server a fresh reason to send MAX_STREAMS, and the leg would then pass against an implementation whose lost-frame handler does nothing at all. With the filler on a unidirectional stream, retransmission is the only route by which a second bidirectional grant can appear. Measured against the previous step's tree as well: there the frame never comes back, because that tree has no arm for it in its lost-frame handler. |
| `cap-stream-discard` | requirement | T3 | `stream_discard` as the receive-side sibling of `stream_recv`, in four exact legs on an in-memory pair. The client sends 4000 bytes on stream 0 with a fin. The server then discards in two pieces and asks twice more: | call | returns | |---|---| | `stream_discard(0, 1000)` | `Ok((1000, false))` | | `stream_discard(0, 5000)` | `Ok((3000, true))` | | `stream_discard(0, 1)` | `Err(Error::Done)` | | `stream_discard(400, 10)` | `Err(Error::InvalidStreamState(400))` | and `stream_finished(0)` is true between the second call and the third. Every leg is an exact value rather than a bound. The first shows the `len` argument is a ceiling that is honoured when more is available; the second shows `fin` arriving on the call that consumes the last byte and not before; the third shows an exhausted stream reporting `Done` rather than a zero-length success, which is the difference between a discard loop that terminates and one that spins; the fourth shows an unknown stream id being refused, with that id echoed back in the error. The pair runs a plain ALPN, not HTTP/3, so the only stream in play is the one the case opened and nothing else is competing for the connection's flow control window. The whole group is a single in-process exchange with no timers. |
| `cap-pmtu-discovered` | requirement | T3 | `pmtu()` and `revalidate_pmtu()` as a state machine with four observable states, on an in-memory pair. | when | `pmtu()` | |---|---| | before the handshake (discovery off, the leg that runs first) | `None` | | after handshake and settle, discovery **off** | `None` | | after handshake and settle, discovery **on** | `Some(v)`, the same `v` at both ends | | immediately after `revalidate_pmtu()` | `None` again | | after a further settle | `Some(v)` again | and the discovered value satisfies `1200 <= v <= 1452` -- the QUIC minimum datagram size from RFC 9000 Section 14, and the maximum this harness's pair will send. The second row is what stops this group from being satisfied by a stub. An accessor hard-wired to return a plausible number passes the third row and fails the second; one hard-wired to return `None` passes rows one, two and four and fails the third. The two together require the value to actually track whether discovery ran. The fourth and fifth rows are `revalidate_pmtu`. The `None` immediately after the call is checked before anything else is sent, which is the only moment at which it is unambiguous: it shows the call restarted the search rather than merely scheduling a probe for later. The value that comes back afterwards is compared to the one from before, so a revalidation on an unchanged path settles where it settled the first time. The bound is one-sided at neither end by accident: an exact figure would be an assertion about this harness's datagram size rather than about the candidate, and a bound that only checked the floor would accept an implementation that reported a number larger than anything it could send. |
| `cap-eleven-member-workspace` | requirement | T4 | The workspace layout, read the way cargo reads it. Two halves. First, each of the eleven expected members has a manifest at `<candidate root>/<name>/Cargo.toml` -- the nine carried forward and the two new ones, `netlog` and `qlog-dancer`. Second, ``` cargo metadata --offline --no-deps --format-version 1 ``` is run at the candidate root and its `packages` array is checked: every expected package name is present as a package whose id is a local path, and the number of local-path package ids is exactly eleven. Counting by path id rather than by matching names is deliberate. `qlog` is a substring of `qlog-dancer`, so a substring search over the metadata finds the first inside the second and would report a package that is not there; and a name-only check would not notice a twelfth member that should not exist. `--no-deps` is what makes the count a statement about the workspace rather than about the dependency graph, and `--offline` is what makes it run in the verifier at all. **This is a layout group and is checked as one.** It is structural breadth, not the milestone. It asserts nothing about behaviour, and the OBSERVATION-TOOLBOX's one exception to the no-source-reading rule is exactly this: a manifest may be read as a description of the package's shape, never as evidence of what the code does. |
| `cap-cc-name-collapse` | requirement | T3 | The contraction of `CongestionControlAlgorithm`, read as the integer each name resolves to rather than as the presence of a Rust identifier. This release deletes the `BBR` and `BBR2` variants and points both of their names at the gcongestion implementation. `FromStr` still accepts five names, and three of them now answer the same number: | name | discriminant | |---|---| | `reno` | 0 | | `cubic` | 1 | | `bbr` | 4 | | `bbr2` | 4 | | `bbr2_gcongestion` | 4 | and `"BBR"`, `"bbr3"` and `""` are refused with `Error::CongestionControl`. The enum is `#[repr(C)]` and the C API hands these values across the boundary unchanged, so the name-to-integer mapping is the part of this change a caller outside Rust can see. The previous milestone answered 2 for `bbr` and 3 for `bbr2`; a candidate that keeps those variants is selecting a controller this release no longer has, and every C caller that names either string gets it. Compared as integers, through `from_str`, and never by naming a variant. A case that wrote `CongestionControlAlgorithm::BBR` would fail to compile here rather than fail an assertion, which would cost a candidate every group in the file instead of this one. The whole group is three lines of table lookup with no connection, no timers and no I/O. |
| `reg-01-h3-static-table` | gate | T3 | Carried because this step moves the Huffman coder out of the crate that owns the QPACK implementation. A request and a response are exchanged over an in-memory HTTP/3 pair and every field arrives intact, indexed and literal alike, **in order**. The request is the four pseudo-headers, all of them QPACK static table entries. The response mixes one static entry, `:status: 200`, with two literal fields, one of which carries a non-ASCII value. The literals are the point. A static-table entry is an index on the wire and never reaches the Huffman coder; a literal does, because the encoder writes the shorter of the raw and coded forms and the decoder has to invert whichever it finds. This step took that coder out of the transport crate and put it in `octets` behind a non-default feature, so the failure this group exists to catch is a candidate that relocates the code correctly and forgets to turn the feature on for the transport crate: the build succeeds, the static-table fields still arrive, and every literal comes back wrong or not at all. Order is asserted as well as content. QPACK may choose its representation per field but may not reorder a header section, and a decoder that emitted the indexed fields first and the literals afterwards would satisfy a set comparison while breaking every HTTP/3 client. It is a gate group, so it adds no new requirement of its own, and it is deliberately weaker than the Huffman group: it asserts that header coding still works end to end, not that any particular byte was produced. |
| `reg-09-key-update` | gate | T2 | Carried as this step's re-check of a packet-protection observation taken off the wire. The verifier takes the connection's generation-0 traffic secrets from its keylog, derives the next generation itself, and injects a 1-RTT packet sealed with the new keys and the key-phase bit set. It proves the secrets before using them by opening one of the candidate's own packets with the generation-0 set, so a key-schedule disagreement between verifier and candidate cannot read as a candidate fault. Four assertions follow: 1. the candidate decrypts the injected packet -- so it derived the same next-generation keys; 2. the header-protection key was not rotated. This is implied rather than separately asserted: the verifier applied generation-0 header protection, so a candidate that rotated it could not find the packet number and nothing after this point would succeed; 3. a second update before the first is acknowledged is `Err(Error::KeyUpdate)`; 4. the candidate's own next short-header packet has the phase bit set. Carried because it is the one inherited group here that reaches the wire. The other two end at the HTTP/3 API, and a candidate can satisfy both while producing short-header packets the peer cannot open. This step rewrites the lost-frame handler, which decides what goes into a retransmitted packet and therefore runs inside the same send path that seals every 1-RTT packet; a key schedule or key-phase bookkeeping disturbed there breaks this group and nothing else in the file. It is also the cheapest orthogonality available: everything else inherited is header coding, because header coding is what moved. It reads no clock and waits for no timer. Everything it observes is a property of bytes that were produced. |
| `reg-01-h3-get` | gate | T1 | The end-to-end HTTP/3 fetch, run between the candidate's own `quiche-server` and its own `quiche-client`, as two real processes over UDP loopback. The server is started on a loopback port serving a 64 KiB file the verifier wrote. The client fetches it with `--no-verify` and with `--http-version HTTP/3` pinned rather than left to the apps' own negotiation, exits 0 within the group's deadline, and the bytes on its stdout match the file byte-for-byte. Pinning the version is what keeps the inherited observation HTTP/3 and stops it quietly becoming a different group. This is the plan-mandated end-to-end regression, and it is carried at every step for the same reason: it is the one observation that requires the whole stack -- the workspace, the transport crate, the HTTP/3 layer, QPACK, the examples and the two binaries -- to be simultaneously correct. This step adds two workspace members and moves a piece of QPACK's machinery into a different crate, either of which can produce a package that compiles and does not work. Ports are chosen free per case and the driver runs one case per `cargo test` invocation, so there is nothing to contend over; the harness's `serial_t1` guard is defined but called by no case at this step. |
| `gate-endtoend` | gate | T1 | Proof of life, and nothing else. The candidate's own `quiche-server` is started on a loopback port with `--listen`, `--cert`, `--key`, `--root` and `--no-retry`; its own `quiche-client` fetches one file from it with `--no-verify`; the client exits 0 and the bytes it wrote match the file the server was serving, byte-for-byte. Every other option is the reference's own default. Nothing from this step's milestone appears here on purpose -- no netlog, no Huffman coder, no `accept_with_retry`, no PMTU accessor. Those are checked where they can be seen. This group is the floor underneath all of them, and it is the group that fails first if the workspace's growth from nine members to eleven broke the build of the two binaries that were already there, or if moving the Huffman coder into `octets` left the transport crate unable to build at all. `--no-retry` is not incidental here. This step's Retry group drives the exchange itself through the public API; the proof-of-life path deliberately does not, so that a candidate whose Retry handling is wrong fails the group that checks it rather than the proof-of-life group. It adds no new requirement of its own. |

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

`/app` contains everything step 13 left behind, unchanged. The milestone before this one is modelled on quiche 0.24.3; this one is modelled on 0.26.0. Read `WORKSPACE_HANDOFF.md` for what is already there and what must keep working.

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

