# Test interface — step 12, An HTTP/3 console debugger, a live idle timeout, and unknown transport parameters

This is the contract between your implementation and the checker. Every
name, path and flag the checker depends on is written down here; nothing
outside this document is assumed about your code. Where the document and
your intuition disagree, the document wins — it is what the tests were
written against.

Generated, together with this step's interface contract, from one source.
The two cannot disagree.

## What this step is

One new tool, one new place to set an old value, one new thing the transport
remembers about its peer, and two additions to the HTTP/3 layer.

**A console debugger for HTTP/3.** A new workspace member, `h3i`, is a client
built for taking an HTTP/3 server apart rather than for fetching from it. It
opens a QUIC connection, sends exactly the frames it is told to send -- including
frames a conforming client would never send -- and reports everything that comes
back. It has two modes. Interactively it prompts for frames. Non-interactively
it is given a qlog file of *actions* with `--qlog-input` and replays them in
order: an `http:frame_created` event carrying a `headers` frame becomes a real
HEADERS frame on the stream that event names, and `"fin_stream": true` closes
the stream after it. A run logs each frame it sends and each frame it receives,
and ends by summarising the connection.

`h3i` is a workspace member and a binary, not a library the transport depends
on: the transport crate does not change because it exists.

**The idle timeout becomes settable on a live connection.** `Config` has carried
`set_max_idle_timeout` since the first milestone, and it still does. What is new
is the same method on `Connection`. It writes the local transport parameter and
then re-encodes the parameter block, so a connection created from a `Config` that
said one thing can be made to advertise another -- provided it is done
immediately after creation, before any packet has been sent or received. It
returns `Result<()>` rather than nothing, because the re-encoding can fail.

**Unknown transport parameters can be kept instead of discarded.** A QUIC
endpoint must ignore transport parameters it does not recognise, and until now
this stack forgot them the moment it skipped them. `Config::enable_track_unknown_transport_parameters`
turns on a bounded record: the argument is a number of *bytes*, not a number of
parameters, and each entry charges its value's length plus eight for the
identifier. When the budget will not take another entry the parameter is dropped
silently -- failing to remember an unknown parameter is not a connection error.
The record is reachable through the decoded peer parameters, and it is present,
empty, and sized as configured even when the peer sent nothing unrecognised: the
decision to track is the configuration's, not the peer's.

Two public types come with it. One is a single parameter -- an identifier and
the bytes that followed it -- which can say whether its identifier lies in the
reserved space RFC 9000 Section 18.1 sets aside for parameters that must always
be ignored. The other is the bounded record itself, which anyone may build and
push into, and which refuses an entry that does not strictly fit.

**HTTP/3 grows two things.** A response may now be preceded by informational
(1xx) header sections and followed by a trailer section, both sent through one
new method that takes a flag saying which it is. The rules are asymmetric and are
part of the contract: a server may send either, a client may send only trailers,
and neither may send anything before the stream's initial header section has gone
out. Separately, an HTTP/3 connection can report how many bytes it has received
on the peer's two QPACK streams.

**Nothing is removed and nothing moves.** Every name the previous milestone
exported is still exported, in the same module, with the same signature. The one
structural change is the new workspace member, and it is checked by running its
binary rather than by looking for its directory.

## The flip

Something an earlier step established changes meaning here. This is the
part a candidate most often gets half-right: the new behaviour appears,
the old one is left working beside it, and both the new group and the
inherited group pass for the wrong reason — until the step that
requires the old behaviour to be *gone*.

`set_max_idle_timeout` stops being a `Config`-only name. Both methods exist from
this step on and they are not interchangeable: the one on `Config` shapes every
connection the config makes and returns nothing; the one on `Connection` changes
that connection alone, must be called before its first packet, and returns
`Result<()>`. A step that had only ever seen the first now has to keep them
apart, and a candidate that implements the new one by delegating to the old one
will advertise the old value, because the old one does not re-encode.

The peer's transport parameters gain a field. Anything that constructs a
`TransportParams` by naming every field will not compile until it names the new
one, and its correct value when tracking is off is `None` rather than an empty
record -- the difference between "not asked for" and "asked for and nothing
arrived" is observable and is the point of the field.

## Interface elements the checker depends on

### The `h3i` workspace member and its qlog replay contract

A new member of the workspace, beside the transport crate and the existing
`apps`/`tools` members. It is a binary crate; the binary is named `h3i`, which
is what the end-to-end case invokes. Nothing in the transport crate's public
surface changes because it exists, and the transport crate must not depend on
it.

Interactively `h3i` prompts for frames on a connection it has opened. That mode
is out of scope: a case that drives a prompt loop is a case that is checking a
prompt loop. What is checked is the non-interactive mode.

```
h3i [--connect-to <addr:port>] [--no-verify] [--idle-timeout <ms>]
    [--qlog-input <file>] [--replay-host-override <authority>]
    <host:port>
```

* `<host:port>` is positional and is what the request's `:authority` and the
  TLS SNI are taken from.
* `--connect-to` overrides the address actually dialled, so the positional can
  stay a name while the socket goes to a loopback port.
* `--no-verify` skips certificate verification, as in the existing example
  client.
* `--idle-timeout` is in **milliseconds** and defaults to 5000. The run ends
  when the connection does; with nothing left to send, that is the idle timeout
  expiring, so this is also how long a replay takes.
* `--replay-host-override` rewrites the authority in replayed request headers
  and is only meaningful alongside `--qlog-input`.

`--qlog-input` is the interface this step defines. It takes a **JSON-SEQ qlog**:
every record is preceded by a `0x1E` record separator and followed by a
newline. The first record is the qlog header, the rest are events. An
`http:frame_created` event is an instruction to send that frame:

```
<RS>{"qlog_version": "0.3", "qlog_format": "JSON-SEQ", "title": "...",
     "trace": {"vantage_point": {"type": "client"},
               "configuration": {"time_offset": 0.0}}}
<RS>{"time": 0.0, "name": "http:frame_created",
     "data": {"stream_id": 0,
              "frame": {"frame_type": "headers",
                        "headers": [{"name": ":method",   "value": "GET"},
                                    {"name": ":scheme",   "value": "https"},
                                    {"name": ":authority","value": "..."},
                                    {"name": ":path",     "value": "/..."}]}},
     "fin_stream": true}
```

`stream_id` names the stream the frame goes out on, the `headers` array is sent
verbatim as a HEADERS frame -- verbatim is the point of the tool, so a
pseudo-header a conforming client would never send is sent anyway -- and
`"fin_stream": true` closes the stream after the frame. Actions are replayed in
file order.

Nothing is asserted about `h3i`'s own stderr. Its wording is a logging
convention, not an interface. The end-to-end case asserts on the **server's**
qlog and on the exit status: `h3i` runs against the candidate's own
`quiche-server` with `QLOGDIR` set, and the server's `.sqlog` must record the
request arriving and the response going back.

The trace file is the one step 06's contract names -- `<role>-<scid hex>.sqlog`,
matched by its `server` prefix -- and it has to be written and flushed **while
the server is running**: the group reads it as soon as the replay has exited and
before the server is stopped, so a writer that holds the whole trace in memory
until the process ends leaves nothing to read.

### `Connection::set_max_idle_timeout`

```rust
impl Connection {
    /// Sets the `max_idle_timeout` transport parameter, in milliseconds.
    ///
    /// This must only be called immediately after creating a connection, that
    /// is, before any packet is sent or received.
    pub fn set_max_idle_timeout(&mut self, v: u64) -> Result<()>;
}
```

`Config::set_max_idle_timeout(&mut self, v: u64)` is unchanged and still
returns nothing. The two are not the same method and one is not implementable
by calling the other:

| | `Config` | `Connection` |
| --- | --- | --- |
| scope | every connection made from that config | that one connection |
| when | before `connect`/`accept` | after, and before the first packet |
| returns | `()` | `Result<()>` |
| effect | seeds the connection's local parameters | sets the local parameter **and re-encodes the parameter block** |

The re-encoding is the whole of it. A connection's transport parameters are
serialised when it is created; writing the field without re-serialising leaves
the connection advertising the config's value, and the peer will never see the
new one. That is exactly what a delegating implementation does, and it is what
the case distinguishes.

`Result<()>` is not decoration -- the re-encode writes into a fixed buffer and
can fail -- but no case is required to provoke the error.

The observable is the peer's view: a client created from a config saying one
thing, then told another thing before its first `send()`, completes a handshake
after which the **server** reports the second value as its peer's idle timeout.

### `Tracking unknown transport parameters`

```rust
impl Config {
    /// Enables tracking unknown transport parameters.
    ///
    /// Specify the maximum number of bytes used to track unknown transport
    /// parameters. The size includes the identifier and its value. If storing
    /// a transport parameter would cause the limit to be exceeded, it is
    /// quietly dropped.
    ///
    /// The default is that the feature is disabled.
    pub fn enable_track_unknown_transport_parameters(&mut self, size: usize);
}

/// A QUIC transport parameter that is not specifically recognized by this
/// implementation.
pub struct UnknownTransportParameter<T> {
    pub id: u64,
    pub value: T,
}

impl<T> UnknownTransportParameter<T> {
    /// Checks whether an unknown Transport Parameter's ID is in the reserved
    /// space. See Section 18.1 in RFC 9000.
    pub fn is_reserved(&self) -> bool;
}

/// Track unknown transport parameters, up to a limit.
#[derive(Clone, Debug, PartialEq, Default)]
pub struct UnknownTransportParameters {
    pub capacity: usize,
    pub parameters: Vec<UnknownTransportParameter<Vec<u8>>>,
}

impl UnknownTransportParameters {
    pub fn push(&mut self, new: UnknownTransportParameter<&[u8]>) -> Result<()>;
}

pub struct UnknownTransportParameterIterator<'a>;   // &UnknownTransportParameters: IntoIterator

pub struct TransportParams {
    // ... every field the previous milestone had, plus:
    /// Unknown peer transport parameters and values, if any.
    pub unknown_params: Option<UnknownTransportParameters>,
}
```

Three separate things, and the cases keep them separate.

**The flag decides the record's existence, not the peer.** The tracking set is
allocated while the peer's parameters are being decoded, from the configuration
alone, before a single parameter has been looked at. So after any ordinary
handshake:

* config silent -- `peer_transport_params().unwrap().unknown_params` is `None`;
* `enable_track_unknown_transport_parameters(N)` -- it is `Some(u)` with
  `u.capacity == N` and `u.parameters` empty, because this ladder's peer sends
  nothing unrecognised.

`None` and `Some` with an empty vector are different answers to different
questions -- "nobody asked" against "somebody asked and nothing arrived" -- and
a candidate that collapses them fails.

**The budget is bytes and the admission test is strict.** `push` charges
`value.len() + size_of::<u64>()`, that is the value's length plus eight for the
identifier, and admits the entry only while that charge is **strictly less
than** the remaining capacity. An entry whose charge exactly equals the capacity
is refused. A refusal returns `Err` -- `BufferTooShort` -- and must leave both
`capacity` and `parameters` exactly as they were: dropping is quiet at the
protocol level, but it is not silent at this API.

The exact arithmetic matters, so it is worth stating once. Starting from
`capacity: 20` and pushing a 4-byte value: the charge is 12, `12 < 20` holds,
capacity becomes 8, one entry is stored. Push the same value again: the charge
is 12 again, `12 < 8` fails, `Err`, capacity still 8, still one entry. Push a
zero-length value: the charge is 8, `8 < 8` fails -- refused, even though there
are eight bytes left.

**Reserved identifiers.** `is_reserved` is true exactly for identifiers of the
form `31n + 27` for non-negative integer `n` -- 27, 58, 89, 120, and so on --
the space RFC 9000 Section 18.1 sets aside for parameters that have no meaning
and must always be ignored. Everything else is false.

What is **not** part of this step's contract: an unknown transport parameter
arriving from a peer. Both endpoints in this ladder's test harness are the
candidate, and the candidate sends no unrecognised transport parameter, so no
case can produce one. The plumbing and the accounting are checked; a delivery is
not.

### `h3::Connection::send_additional_headers`

```rust
impl h3::Connection {
    pub fn send_additional_headers<T: NameValue>(
        &mut self, conn: &mut quiche::Connection, stream_id: u64,
        headers: &[T], is_trailer_section: bool, fin: bool,
    ) -> Result<()>;
}
```

One method for two things. `is_trailer_section` says which:

* `false` -- an *informational* header section: a 1xx response sent ahead of the
  final one, or another 1xx after a preceding 1xx.
* `true` -- a *trailer* section, sent after the message body.

The rules are asymmetric, and the asymmetry is the contract rather than an
implementation detail:

1. a **client** may only send trailers. `send_additional_headers` with
   `is_trailer_section == false` on a client returns `FrameUnexpected`,
   checked before anything else and regardless of the stream's state;
2. either side calling it before the stream's initial header section has been
   sent -- before `send_request` on a client, before `send_response` on a server
   -- returns `FrameUnexpected`;
3. at most one trailer section per stream. A second `is_trailer_section == true`
   on the same stream returns `FrameUnexpected`;
4. an unknown stream returns `FrameUnexpected`.

Otherwise it writes a HEADERS frame, honouring `fin` the same way
`send_response` does, and `StreamBlocked` if the stream cannot take it yet. That
last path is documented here and **exercised nowhere**: no group at this step
drives a stream to the state where the header section cannot be taken, so a
candidate that gets `StreamBlocked` wrong loses nothing -- until a later step's
ordinary HTTP/3 traffic reaches it.

On the wire and at the peer these are ordinary HEADERS frames: the receiver
polls `Event::Headers` for each of them in order, which is how a client sees a
1xx followed by the final response, and how either side sees a trailer section
after the body.

### The `Event::Headers` field rename

```rust
pub enum Event {
    Headers {
        list: Vec<Header>,
        /// Whether more frames will follow the headers on the stream.
        more_frames: bool,
    },
    // the other variants are unchanged
}
```

The field previously named `has_body` is now named `more_frames`. This is a
breaking change to a public enum: every `match` arm and every construction of
the variant has to be rewritten, including the two in the crate-level example
at the top of the HTTP/3 module.

What it is **not** is a change of meaning. The value has always been computed
the same way -- whether the stream is still open once the header section has
been read -- and it still is. Only the name moves, because
`send_additional_headers` makes the old one wrong: after an informational
header section what follows is another header section, not a body, and a
receiver told `has_body: true` there would be told something false. The new
name says what the flag always measured.

A candidate that keeps the old name has an `h3::Event` that no caller written
against this release can destructure, so the cases that read header sections
do not compile against it. Renaming the field is the whole of the work; a
candidate that also changes how the flag is computed breaks the informational
and trailer cases, which pin the value on every section they observe.

### `h3::Stats and h3::Connection::stats`

```rust
/// Statistics about the connection.
#[derive(Clone, Default)]
pub struct Stats {
    /// The number of bytes received on the QPACK encoder stream.
    pub qpack_encoder_stream_recv_bytes: u64,
    /// The number of bytes received on the QPACK decoder stream.
    pub qpack_decoder_stream_recv_bytes: u64,
}

impl h3::Connection {
    pub fn stats(&self) -> Stats;
}
```

Two counters, both of them about what was **received** on the peer's QPACK
streams, and both of them independent of the transport's own `quiche::Stats`.
The type has the same name as the transport's statistics struct and is a
different type in a different module; nothing is shared between them.

What "received" counts is narrower than it sounds, and the narrowness is the
contract. Only bytes read out of a stream **already identified** as the peer's
QPACK encoder or decoder stream are counted: the leading stream-type byte is
consumed by the type parser and is not, and nothing on the control stream is.
This stack's QPACK encoder emits only literal field lines and so writes no
instruction at all, which means an ordinary request and response leaves both
counters at **zero on both sides**. A candidate is not required to change that;
the counters are about what arrives, and nothing arrives.

The consequence for anyone implementing this: the counters must be driven by
what is read off those two streams, not derived from the header blocks, not
from the transport's byte totals, and not from the stream-type byte. Whatever
is written on the peer's encoder stream is added to the first counter and
whatever is written on its decoder stream to the second, in bytes, cumulatively,
per connection, and QPACK instructions are otherwise discarded -- reading them
must not close the connection or disturb anything else on it.

### `What the workspace gains, and what does not change`

The workspace member list grows by one: `h3i` joins the transport crate and the
existing members. Anything that builds the workspace builds it too, and a
workspace manifest that does not list it leaves the binary unbuilt -- which is
how a candidate most easily fails the end-to-end case without any of its own
code being wrong.

Two unrelated trees leave in the same milestone -- an nginx patch directory and
a release script. Neither is referenced by anything this step checks, and no
group looks for their absence.

One more addition, required to exist and deliberately out of scope. `Stats` and
`PathStats` each gain two counters:

```rust
pub struct Stats {
    // ... the previous milestone's fields, unchanged ...

    /// The number of DATAGRAM frames received.
    pub dgram_recv: usize,

    /// The number of DATAGRAM frames sent.
    pub dgram_sent: usize,

    // ... the rest, unchanged ...
}
```

`PathStats` gains the same two, in the same position and with the same doc
comments.

Neither struct's `Debug` implementation changes in the reference, so neither
counter appears in its formatted output -- `Stats`'s `Debug` is hand-written, not
derived, and it names a fixed list of fields. That is recorded as what the
reference does and is **not** a requirement of the candidate. No group reads
either struct's `Debug` output, and none will: pinning a `Debug` string would
fail a candidate whose formatting is *more* complete than the reference's, which
is not something this package penalises anywhere.

**No group checks their values, and a candidate must not be surprised by
that.** In this release the counters are always zero. Both increment sites --
one beside the DATAGRAM frame being pushed into a packet, one beside the
received payload being pushed into the receive queue -- are written

```rust
let _ = self.dgram_sent_count.saturating_add(1);
```

and `saturating_add` on an integer takes `self` by value and *returns* the sum
rather than mutating in place, so the statement computes a number and throws it
away. All four counters therefore read 0 no matter how many DATAGRAM frames
cross the connection. A group asserting they count would require the candidate
to be better than the release; a group asserting they stay at zero would fail
any candidate that got it right. Neither is a case, so the fields are stated
here as required surface and left out of scope. The only thing that would break a
candidate is omitting them: `Stats` is constructed field-by-field in this crate
and read by name elsewhere in the workspace.

Nothing else moves. The public surface of the transport crate is the previous
milestone's surface plus the additions above: no name is removed, no name
changes module, and no signature changes except by the additions already
described. There is **no layout group at this step**. `h3i` is checked by running
it; a group that merely found `h3i/Cargo.toml` would pay a candidate for
creating an empty crate.

## The groups at this step

One row per group. **gate** groups are all-or-nothing: a failure means the step is not satisfied; **requirement** groups cover this step's own new capability. The rows are grouped by tier for reading; the order they run in, and each one's result, is what `reward_details.json` records.

| group | tier | surface | what it observes |
| --- | --- | --- | --- |
| `candidate-workspace-builds` | gate | T4 | The one check every step shares: `cargo build --offline` (default targets -- libraries and binaries only, not tests or benches) must complete for the workspace at `/app` within the harness's build timeout, before any other group in this table runs. It is the one group every step carries identically, and no step declares it as a capability of its own -- but it is not optional either: one failing gate group, including this one, means the step is not satisfied, however much of the new work is right. |
| `cap-h3i-replay` | requirement | T1 | Three of the candidate's own processes' worth of work in one: its `quiche-server` is started on a loopback port with `--listen`, `--cert`, `--key`, `--root`, `--no-retry` and `--idle-timeout 6000` and with `QLOGDIR` pointing at an empty scratch directory (the server's `--idle-timeout` is the same flag `h3i` has, in milliseconds, and it is passed so the server outlives the replay rather than idling out under it); its `h3i` binary is given a two-record JSON-SEQ action file and told to replay it at that port; and the **server's** qlog is read to see what arrived. The action file is written by the verifier, not by the candidate, and is the shape the interface document fixes: a `0x1E`-prefixed header record naming `qlog_version 0.3`, `qlog_format JSON-SEQ` and a client vantage point, then a `0x1E`-prefixed `http:frame_created` record carrying a `headers` frame on stream 0 with `:method GET`, `:scheme https`, `:authority`, a `:path` naming a file the docroot contains, and `"fin_stream": true`. `h3i` is invoked as `h3i --qlog-input <file> --no-verify --connect-to 127.0.0.1:<port> --idle-timeout <ms> <authority>:<port>`, and must exit 0 within the group's deadline. Exiting 0 is the first half of the assertion and is not sufficient on its own: a binary that parsed its arguments and connected to nothing would also exit 0, which is why the second half reads the server. What the server's `.sqlog` must show, matched as qlog event names and their payloads rather than as log prose: the HEADERS frame arriving on stream 0 with the `:path` the action file named, and a response going back on the same stream. Assertions are on the server's qlog because that is an emitted artefact with a schema; `h3i`'s own stderr is a `log`-crate format that nothing in this step declares, and a group that grepped it would be checking a logging convention. Three failure modes were seen while proving this shape and each is asserted against rather than assumed away: `h3i` absent from the workspace member list, so the binary is never built (the group then fails on the missing binary, and says so); `--qlog-input` pointed at a file that does not exist, which the reference answers with a panic rather than a diagnostic, so the file is written before the process starts and its path is absolute; and the authority in the replayed headers not matching what the server will serve, which `--replay-host-override` exists to fix and which the verifier avoids by writing the same authority into both. Not asserted, and each for a reason rather than by omission: * the interactive prompt mode, the connection summary's wording, the `h3i` qlog *output* file, and any frame the action file did not ask for; * **`--replay-host-override` itself.** The paragraph above explains what it is for; the verifier sidesteps the problem instead, by writing the same authority into the replayed headers and into what the server serves. The flag must exist and take an argument -- a candidate that omits it from the usage string has not built this milestone's `h3i` -- but no group drives it, and that is a choice, not an oversight; * **the default value of `--idle-timeout`.** It is 5000 ms, and the contract above says so because a candidate has to pick a number. Every invocation in this package passes the flag explicitly (2000 ms), so the default is never the value in play. Proving it would mean running one replay with the flag omitted and waiting for the connection to idle out on its own -- five seconds of wall clock, per run, to pin a number nothing else depends on. Documented, not asserted. |
| `cap-live-idle-timeout` | requirement | T3 | Two in-memory `connect`/`accept` pairs that differ in exactly one line, read from the side that can actually see the answer. Both configurations set `Config::set_max_idle_timeout(30_000)` on **both** ends. In the first pair, and only there, the client is then told `set_max_idle_timeout(7_000)` on the **connection**, immediately after `quiche::connect` and before a single datagram has been produced. Both pairs then handshake normally. Measured against the reference: | | server's view of the client | client's view of the server | | --- | --- | --- | | live call made | **7000** | 30000 | | no live call | 30000 | 30000 | Three separate things are being pinned by those four numbers. That the server sees 7000 is the feature. That the *client's* view of the *server* stays 30000 in the same pair is what stops a candidate from having changed a shared default. And that the baseline pair reports 30000 on the same code path is what stops the first row from being satisfied by a connection that ignores its config. The value is read through `Connection::peer_transport_params()`, which the previous milestone already provided -- so the group is about the new setter and not about a new way of looking. The return type is load-bearing and is exercised, not merely mentioned: the call site binds the result and propagates it, so a candidate whose method returns `()` does not compile. It is not required to fail, and no case provokes the error path. Deliberately not asserted: that a call made *after* the first packet is rejected. The documentation says "must only be called immediately after creating a connection", and the reference enforces nothing -- it writes the field and re-encodes whenever it is asked. Requiring a rejection would require the candidate to be stricter than the reference. Deliberately not asserted either: anything about a connection actually timing out. Idle timeout expiry is a wall-clock observation and has no place in a case that must be deterministic. |
| `cap-unknown-tp-tracking-enabled` | requirement | T3 | One handshake, both answers. The **server's** config calls `enable_track_unknown_transport_parameters(128)`; the **client's** does not. Everything else is the pair's default. After an ordinary handshake, read through `Connection::peer_transport_params()`: * the server's view of the client is `Some(u)` with `u.capacity == 128` and `u.parameters` **empty**; * the client's view of the server is `None`. Measured against the reference, exactly: `Some(UnknownTransportParameters { capacity: 128, parameters: [] })` and `None`. That is the whole group, and it is deliberately small. The record is allocated while the peer's parameter block is being decoded, from the configuration alone, before any parameter has been examined -- so its existence and its declared capacity are observable even though this ladder's peer sends nothing unrecognised. `None` and an empty `Some` are different answers to different questions, and a candidate that returns `Default::default()` for both, or `Some(empty)` for both, fails one half or the other. A second capacity is not tested here and does not need to be: the arithmetic belongs to `cap-unknown-tp-capacity`, where it is exercised directly on the public type instead of through a handshake. Not observed anywhere in this step: a tracked parameter that actually arrived. Both endpoints of the pair are the candidate, the candidate emits no unrecognised transport parameter, and the verifier's support crate has no TLS and so cannot complete a handshake to send one. The narrowing is real and is recorded rather than papered over with a group that hand-builds a `TransportParams` and asserts on its own struct literal. |
| `cap-unknown-tp-capacity` | requirement | T3 | The public accounting type, driven directly. No connection, no handshake, nothing timed: `UnknownTransportParameters { capacity: 20, parameters: vec![] }` is built by the verifier -- both fields are public and the type derives `Default` -- and then pushed into, in this order. Measured against the reference: | push | charge | admitted? | capacity after | entries after | | --- | --- | --- | --- | --- | | `{ id: 31, value: b"abcd" }` | 4 + 8 = 12 | yes, `12 < 20` | 8 | 1 | | `{ id: 62, value: b"abcd" }` | 12 | **no**, `12 < 8` is false | 8 | 1 | | `{ id: 63, value: b"" }` | 0 + 8 = 8 | **no**, `8 < 8` is false | 8 | 1 | Four claims, and the third row is the one that matters most. The admission test is a **strict** `<`, so an entry whose charge exactly equals the remaining capacity is refused; a candidate that wrote `<=` passes the first two rows and fails the third. The eight bytes charged for the identifier are fixed and are charged even for an empty value, which is what the third row also shows. Both refusals return `Err` -- `BufferTooShort` -- and both leave `capacity` and `parameters` exactly as they were. A silent drop at this API would be a different contract from the quiet drop the protocol layer performs, and the group asserts the state after each refusal rather than only the return value. The surviving entry is then read back through the `IntoIterator` on `&UnknownTransportParameters`, yielding one item whose `id` is 31 and whose `value` is `b"abcd"` -- so a candidate that accounted correctly but stored nothing, or stored a borrowed slice it could not return, fails here. |
| `cap-tp-reserved-ids` | requirement | T3 | `UnknownTransportParameter::is_reserved` over a fixed table, and nothing else. RFC 9000 Section 18.1 reserves the identifiers `31 * N + 27` for parameters that have no meaning and must always be ignored; the predicate is true for exactly those. The table, measured against the reference: | id | 27 | 28 | 31 | 57 | 58 | 62 | 88 | 89 | 90 | 119 | 120 | 121 | 1000 | 7963 | | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | | reserved | Y | n | n | n | Y | n | n | Y | n | n | Y | n | n | Y | Five true, nine false, the true ones being 27, 58, 89, 120 and 7963 -- the first four members of the family and one well above where an off-by-one in a loop would still agree -- and the false ones chosen to sit immediately on either side of a true one (28, 57, 88, 90, 119, 121) so that a predicate off by one in either direction fails. **Identifiers below 27 are deliberately absent, and this is not an oversight.** The reference computes `(id - 27) / 31` on a `u64`, which underflows for every identifier under 27 and panics outright in the debug profile the verifier builds -- `id: 0` was measured taking the whole probe down at `quiche/src/lib.rs:8085`. A candidate writing the obvious non-underflowing form, `id >= 27 && (id - 27) % 31 == 0`, answers `false` there instead. RFC 9000 Section 18.1 defines the reserved set only upward from 27 and says nothing about what lies below, so both readings are defensible and neither is this milestone's capability. Checking the difference would check an arithmetic accident of the reference rather than the feature; the table stops at 27 and the two forms agree on every row of it. The smallest group at this step: this is a pure predicate over a public type, peripheral to the tracking machinery that `cap-unknown-tp-tracking-enabled` and `cap-unknown-tp-capacity` carry. |
| `cap-h3-informational` | requirement | T3 | An HTTP/3 exchange over an in-memory pair in which the response arrives in two header sections, and a client that tries the same thing is refused. The server answers a GET with `send_response(103, [link: </s.css>], fin=false)` -- the informational section, which is the stream's *initial* header section and so goes out through the ordinary call -- and then `send_additional_headers(&[:status 200, content-length: 5], is_trailer_section: false, fin: false)` for the final response, followed by a five-byte body. The client polls and must see, in this order and with nothing between them: 1. `Event::Headers` whose list contains `:status` `103`, `more_frames` true; 2. `Event::Headers` whose list contains `:status` `200`, `more_frames` true; 3. `Event::Data`, carrying the five bytes; 4. `Event::Finished`. Both header sections are asserted by content, not merely counted: a candidate that delivered the same section twice, or coalesced the two into one, fails on the `:status` values. The refusal leg is the other half and is checked in the same case. Before any of the above, the **client** calls `send_additional_headers` on its own request stream with `is_trailer_section: false`. It must return `FrameUnexpected`: informational responses are a response construct, a request cannot contain one, and the reference rejects it before examining the stream at all. Measured: `Err(FrameUnexpected)`, on a stream that had already had its initial HEADERS sent -- so the refusal is about the direction, not about the state. A server calling `send_additional_headers` on a stream whose initial header section has not been sent is also refused, and that leg belongs to `cap-h3-trailers`, which is where the stream-state rules are gathered. Nothing is asserted about what the 1xx status code *means*, about `Retry-After`, or about Early Hints semantics. The contract is positional: two header sections before the body, both delivered, in order. |
| `cap-h3-trailers` | requirement | T3 | A trailer section after a body, and the three ways the same method must refuse. The positive leg: the server sends `send_response(200, fin=false)`, then a body with `fin=false`, then `send_additional_headers(&[x-checksum: deadbeef], is_trailer_section: true, fin: true)`. The client polls and must see `Event::Headers` (the 200), `Event::Data` (the body, byte for byte), `Event::Headers` again -- carrying `x-checksum` with the value the server sent, and with `more_frames` **false**, because the trailer section closed the stream -- and then `Event::Finished`. The transition of `more_frames` from true on the first section to false on the trailer is asserted: it is how a receiver knows the trailer was a trailer. Three refusals, each `FrameUnexpected`, each measured against the reference: * **a second trailer section on the same stream.** Once a trailer section has gone out the stream is finished with header sections; the repeat call returns `FrameUnexpected` rather than writing a second HEADERS frame. * **before the initial header section.** A server calling `send_additional_headers` on a stream on which it has not yet called `send_response` returns `FrameUnexpected`. This is the leg that is about stream state rather than direction, which is why it sits here rather than in `cap-h3-informational`. * **an unknown stream id.** The same error, not a panic and not a different error: a stream the connection has never seen is refused the same way as one in the wrong state. A client **is** allowed to send trailers, and that asymmetry is the reason this group and `cap-h3-informational` exist separately: `is_trailer_section: true` is the one combination a client may use, and `is_trailer_section: false` is the one it may not. |
| `cap-h3-stats` | requirement | T2/T3 | The two QPACK byte counters, driven by bytes the verifier writes onto the candidate's own QPACK streams -- because an ordinary request and response leaves them both at zero and a group that read them after a GET would assert `0 == 0` and pass on a `Default::default()`. **Finding the streams (T2, no assumption).** The client's 1-RTT datagrams are decrypted with keys derived from `CLIENT_TRAFFIC_SECRET_0` out of its own keylog, their STREAM frames are decoded, and every client-initiated unidirectional stream (`stream_id % 4 == 2`) carrying `offset == 0` is read for its HTTP/3 stream type byte. The one whose type is `0x02` is the QPACK encoder stream and the one whose type is `0x03` is the QPACK decoder stream. Measured against the reference: control `2`, encoder `6`, decoder `10`, and a GREASE stream `14`. The group reads the type byte rather than hard-coding the ids, so a candidate that opens its unidirectional streams in a different order still passes -- what it may not do is fail to open them. **Driving the counters (T3).** Twenty bytes are written on the encoder stream and twelve on the decoder stream through the client's transport connection, the pair settles, and the server's HTTP/3 connection is polled until it reports `Done`. Then: | | encoder | decoder | | --- | --- | --- | | `server.stats()` | **20** | **12** | | `client.stats()` | 0 | 0 | Five more bytes on the encoder stream, settle and poll again, and the server reports **25** and **12** -- cumulative on the one that moved, untouched on the one that did not. The client's own counters staying at zero throughout is what makes the reading per-connection and per-direction rather than a connection-wide total; the second write is what makes it cumulative rather than a high-water mark or a last-read size. Both endpoints must still be open afterwards: QPACK instructions on a stack with no dynamic table are discarded, and discarding them must not close a critical stream. Exact byte counts are safe to assert here precisely because the verifier chose them. Nothing is asserted about bytes the *candidate's* encoder wrote -- and the client's own pair of counters reading zero is not a claim that a real client never writes QPACK instructions, only that it need not in this exchange: the field lines this request uses are all static-table entries, and a decoder-stream instruction is only owed once the encoder emits one. An encoder that does send instructions leaves the client's counters non-zero, and no assertion here reads them. |
| `reg-11-pmtud` | gate | T3 | Carried as the previous milestone's headline. Both endpoints are configured with `set_max_send_udp_payload_size(1452)` and `set_max_recv_udp_payload_size(1452)`, and the only thing that differs between the two legs is `discover_pmtu`: * **on** -- at least one **server** datagram is exactly 1452 bytes, and every server datagram is either at or under 1200 or exactly 1452, with nothing in between; * **off** -- no server datagram exceeds 1200 at all; * **both** -- a 64 KiB body arrives byte-for-byte with the stream finished, and neither end of the on-configuration pair is closed afterwards. Carried because this step adds a workspace member that opens its own connections and a setter that re-encodes a live connection's transport parameters, and because PMTU discovery is the youngest behaviour in the stack -- a rebuild of the send path would show here first. **Three of step 11's legs are deliberately left behind**, and each for the same reason: a gate case is all-or-nothing, so anything in it that could read differently under load means the step is not satisfied for every candidate. * The **probe count and position** were never asserted at step 11 either, and are not asserted here. * The **client histograms** -- `{43:1, 483:1, 904:1, 936:1, 1050:1, 1200:54}` with discovery on against `{43:1, 483:1, 932:1, 962:1, 1452:45}` with it off -- are dropped. They are a statement about how a 64 KiB write is chopped up, which depends on the congestion window at each `send`, and the exact multiset is not something a gate should be asserting. The *bound* survives in the server legs above, where it is one-sided. * The **binary-search-on-loss** leg was already absent at step 11, for the reason recorded there: the reference needed more timer ticks than a bounded case can justify before it re-probed at a smaller size. `PathStats::pmtu` is not read, at step 11 or here: it reports the *configured* maximum from the moment the connection is created and reads 1452 in both legs before either has sent a packet, so it cannot tell them apart. |
| `reg-09-key-update` | gate | T2 | The verifier becomes the peer that initiates the update, because there is no public API for initiating one and the candidate is only required to implement the responder: from `CLIENT_TRAFFIC_SECRET_0` in the candidate's own keylog it derives the next generation itself -- HKDF-Expand-Label with `quic ku`, then `quic key` and `quic iv` -- and builds a protected 1-RTT packet with the key- phase bit set. It proves those keys first by opening one of the candidate's own packets with them, so that a key-schedule disagreement cannot read as a candidate fault. Then: the candidate decrypts the injected STREAM frame and `stream_recv` returns the payload byte-for-byte; its own next packet has the phase bit set; and a second update before the first is acknowledged is `Err(Error::KeyUpdate)`. Carried because this step's new setter re-encodes a live connection's transport parameters and its new workspace member drives connections of its own, and every short-header packet either of those produces is sealed by exactly this machinery. The header-protection reuse leg is implied rather than separately asserted, as at step 09: the verifier applies header protection with the generation-0 key, so a candidate that rotated it could not find the packet number and nothing later would succeed. |
| `reg-01-h3-get` | gate | T1 | Inherited from step 01, re-seeded from step 07, the most recent step to carry it: a 64 KiB body travels from the candidate's own `quiche-server` to the candidate's own `quiche-client` over UDP loopback, byte for byte, as two real processes, with `--http-version HTTP/3` pinned so the observation stays HTTP/3 rather than whatever the apps would negotiate. Carried because this step reaches into the HTTP/3 layer twice -- a new way to send header sections and a new statistics surface -- and because it is the only inherited gate case here that exercises the whole stack as the user gets it. `cap-h3i-replay` also crosses the process boundary, but through a new binary and against a hand-written action file; this one is the ordinary path, and if it fails, that one's diagnosis is guesswork. |
| `gate-endtoend` | gate | T1 | The mandatory process-boundary gate. The candidate's own `quiche-server` is started on a loopback port with `--listen`, `--cert`, `--key`, `--root` and `--no-retry`; the candidate's own `quiche-client` fetches a 2 KiB body from it over real UDP with `--no-verify`; the client exits zero and the bytes match what the server was given. Everything else is the reference's own default -- version 1, ALPN negotiated from the app's own list, HTTP/3 alongside HTTP/0.9. Nothing about this step's milestone appears here: no `h3i`, no live idle timeout, no transport-parameter tracking. That is the point. If this group fails, every other T1 observation at this step is noise. |

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

`/app` contains everything step 11 left behind, unchanged. The milestone before this one is modelled on quiche 0.22.0; this one is modelled on 0.23.0. Read `WORKSPACE_HANDOFF.md` for what is already there and what must keep working.

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

