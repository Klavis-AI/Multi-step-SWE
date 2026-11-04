# Test interface — step 13, An async workspace, a connection generic over its buffers, and a second BBRv2

This is the contract between your implementation and the checker. Every
name, path and flag the checker depends on is written down here; nothing
outside this document is assumed about your code. Where the document and
your intuition disagree, the document wins — it is what the tests were
written against.

Generated, together with this step's interface contract, from one source.
The two cannot disagree.

## What this step is

The largest structural change since the workspace conversion, and it arrives on
four fronts at once: the repository grows an asynchronous front end, the
connection type becomes generic over where its outbound bytes live, the
congestion controller list grows a fifth entry, and two smaller knobs land
beside them.

**An asynchronous front end, as four new workspace members.** `tokio-quiche`
is a Tokio-based server and client built on top of the transport crate;
`buffer-pool`, `datagram-socket` and `task-killswitch` are the three small
crates it rests on. The workspace goes from five members to nine. The root
manifest also grows `[workspace.package]` and `[workspace.dependencies]`
tables, so the members inherit shared metadata and shared dependency versions
instead of each repeating them, and it declares `resolver = "2"`.

`tokio-quiche` is a consumer of the transport crate, not a dependency of it.
Nothing in `quiche` may depend on it, and the existing binaries keep working
with no knowledge that it is there.

**`Connection` becomes generic over a buffer factory.** Two new public traits,
`BufFactory` and `BufSplit`, describe how the crate allocates and slices the
buffers that hold outbound stream data. `Connection` gains a type parameter
with a default, so every existing signature keeps compiling, and two new
constructors let a caller pick a different one: `connect_with_buffer_factory`
and `accept_with_buf_factory`. The HTTP/3 layer follows -- every `h3` method
that takes a connection now takes `&mut Connection<F>`.

The point of the parameter is the two sends built on top of it.
`Connection::stream_send_zc` takes ownership of a buffer the factory produced
and returns whatever it could not fit, so the bytes are never copied into the
crate's own storage; `h3::Connection::send_body_zc` does the same for an
HTTP/3 body, and will ask the buffer to grow a frame header on its front if the
buffer says it can.

**A fifth congestion controller.** The `gcongestion` port of BBR and BBRv2
arrives as `CongestionControlAlgorithm::Bbr2Gcongestion = 4`, named
`bbr2_gcongestion` in string form, and `Connection::gcongestion_enabled`
reports which of the two recovery implementations a connection is running. The
four discriminants that were already there do not move. The enum itself moves
from `recovery.rs` into `recovery/mod.rs`, which changes nothing a caller can
see: it is still `quiche::CongestionControlAlgorithm`.

**Two smaller additions.** `Config::set_send_capacity_factor` multiplies the
send capacity the connection is willing to hand out in one call.
`h3::Priority` becomes `Clone` and `Copy`, and
`h3::Connection::send_additional_headers_with_priority` sends an additional
header section and reprioritises the stream in the same call.

## The flip

Something an earlier step established changes meaning here. This is the
part a candidate most often gets half-right: the new behaviour appears,
the old one is left working beside it, and both the new group and the
inherited group pass for the wrong reason — until the step that
requires the old behaviour to be *gone*.

`Connection` stops being a plain struct and becomes `Connection<F>` with a
default type parameter. The default is what keeps every existing call site
compiling -- `quiche::Connection` still means the same thing it meant last
step, and `connect` and `accept` still return it. What changes is that code
which *names* the type in a signature of its own, or writes an `impl` block for
it, now has to decide whether it is talking about the default instantiation or
about any instantiation. The HTTP/3 layer made that decision one way: every
method is generic.

The default factory type is deliberately not exported. A caller can use the
default by writing `Connection`, and can supply its own by naming its own
factory; there is no third option in which it names the crate's.

`CongestionControlAlgorithm` gains a fifth variant. Anything that matches on it
exhaustively stops compiling until the new arm is added, and anything that
assumed the set had four members -- a lookup table, a round-trip test over every
name -- is now wrong rather than merely incomplete.

`bbr2_gcongestion` is a name in the string table whether or not the
implementation behind it is compiled in. Selecting it is always accepted;
whether a connection then *runs* on the new recovery implementation is what
`gcongestion_enabled` answers, and on the default build -- the one the checker
compiles, where the feature is on -- the answer for a `Bbr2Gcongestion`
connection is `Some(true)`. What the feature controls is whether the
implementation is *in* the build at all, not what the name reports when it is.

## Interface elements the checker depends on

### `The nine-member workspace and the root manifest`

The workspace grows from five members to nine. The five that were already
there stay, under the same directory names and the same package names:

```
apps  h3i  octets  qlog  quiche
```

and four arrive:

```
buffer-pool  datagram-socket  task-killswitch  tokio-quiche
```

For each of the four the directory name and the package name are the same
string, so `<root>/tokio-quiche/Cargo.toml` is the manifest of the package
named `tokio-quiche`. The case reads the workspace the way cargo does:

```
cargo metadata --offline --no-deps --format-version 1 --manifest-path <root>/Cargo.toml
```

and requires that all nine appear in `workspace_members` and that there are
exactly nine of them -- an extra member is as wrong as a missing one, because
the point of the case is that this is the membership list upstream shipped.
`--no-deps` keeps the output to the workspace itself; `--offline` is required
because the verifier has no network.

The root manifest also gains three things beyond the member list:

* `[workspace.package]`, holding the metadata the members inherit -- the
  edition, the licence, the repository. Each member keeps its own `version`
  field rather than inheriting one from here; the root manifest carries no
  version key, and a member that drops its own for a `version.workspace = true`
  is reading from a key that is not there.
* `[workspace.dependencies]`, holding the versions the members inherit. A
  member that writes `foo = { workspace = true }` has no version of its own
  and is reading it from here.
* `resolver = "2"`.

Write the first two as tables, spelled `[workspace.package]` and
`[workspace.dependencies]`, and set `resolver` to the string `"2"`. The
spelling is part of the requirement because it is what the requirement group reads:
`cargo metadata` reports workspace *membership* and cannot see the root's own
tables at all, so `cap-async-workspace` reads the root manifest's text for
these three and would not recognise them written as inline tables inside
`[workspace]`. That is a real constraint on the manifest and not merely a
description of one, which is why it is stated here rather than left implied.

Do not carry the previous step's root manifest forward unchanged and add four
members to it.

`tokio-quiche` depends on the transport crate. The transport crate must not
depend on `tokio-quiche`, on `buffer-pool`, on `datagram-socket` or on
`task-killswitch`: the asynchronous layer is built on top of the crate, and
adding a Tokio runtime underneath the transport would be a different library.
`cap-async-workspace` reads `quiche/Cargo.toml` and requires that none of the
four names appears in it, in any dependency table -- ordinary, development or
build. Whole-line comments are stripped before the check, so a **whole-line**
comment that mentions one of them costs nothing. A trailing comment on the same
line as code is not stripped and is read as part of the table's text, so keep
the four names out of those.
The existing binaries -- `quiche-client` and `quiche-server` in `apps` -- are
unchanged and must keep building and running in the larger workspace.

### `the `tokio_quiche` surface the probe compiles against`

`cap-tokio-quiche` writes a small `tokio_quiche` server into a scratch crate,
builds it against the candidate's workspace, and runs it. The names it imports
are therefore part of this step's contract, not an implementation detail:

```rust
tokio_quiche::buf_factory::BufFactory
tokio_quiche::http3::driver::{H3Event, IncomingH3Headers, OutboundFrame, ServerH3Event}
tokio_quiche::http3::settings::Http3Settings
tokio_quiche::listen
tokio_quiche::metrics::DefaultMetrics
tokio_quiche::quic::SimpleConnectionIdGenerator
tokio_quiche::quiche::h3                      // the transport crate, re-exported
tokio_quiche::quiche::h3::NameValue
tokio_quiche::settings::{CertificateKind, Hooks, QuicSettings, TlsCertificatePaths}
tokio_quiche::{ConnectionParams, ServerH3Connection, ServerH3Driver}
```

Three shapes go with them: the `h3` types are reached through the
`tokio_quiche::quiche` re-export rather than by depending on `quiche` directly;
`DefaultMetrics` and `SimpleConnectionIdGenerator` are unit structs (the probe
constructs them with no fields); and the stream item the driver yields is a
`Result`-wrapped pair, not a bare one. `listen` takes the listener address, the
`ConnectionParams`, and the generator, in that order.

A candidate who organises `tokio_quiche`'s modules differently -- or shapes any
of those three items differently -- fails to compile the probe and loses this
group whole. Everything else the probe asserts is behaviour, but this header of
imports is a layout, and it is written down here so that it can be read rather
than guessed.

### `BufFactory, BufSplit, and Connection<F>`

Two new public traits describe where the bytes a caller sends live before they
go out on the wire, and `Connection` becomes generic over the first of them.

```rust
pub trait BufFactory: Clone + Default + std::fmt::Debug {
    /// The type of buffer this factory produces.
    type Buf: Clone + std::fmt::Debug + AsRef<[u8]>;

    /// Create a buffer holding a copy of the given slice.
    fn buf_from_slice(buf: &[u8]) -> Self::Buf;
}

pub trait BufSplit {
    /// Split the buffer in two at `at`, returning the tail and keeping the
    /// head in `self`.
    fn split_at(&mut self, at: usize) -> Self;

    /// Try to prepend `prefix` to the buffer in place, returning `true` if it
    /// worked. A buffer that cannot do this returns `false` and the caller
    /// falls back to sending the prefix separately.
    fn try_add_prefix(&mut self, _prefix: &[u8]) -> bool { false }
}
```

Both are exported from the crate root: `quiche::BufFactory` and
`quiche::BufSplit`.

```rust
pub struct Connection<F = DefaultBufFactory> { /* ... */ }
```

The default parameter is what keeps the rest of the crate source-compatible.
`quiche::Connection` still names the same type it named at the previous step,
`connect` and `accept` still return it, and every method that was on
`Connection` is still reachable on it. A caller that never mentions a factory
sees no change at all.

**The default factory type is not part of the public surface.** The module it
lives in is private and it is not re-exported, so `quiche::DefaultBufFactory`
does not resolve. A caller uses the default by writing `Connection` and nothing
else, or supplies its own factory and writes `Connection<MyFactory>`. There is
no spelling in between.

That is deliberate, and **no group asserts it.** Proving a name does not
resolve means compiling something that must fail to compile, and the five
observation surfaces this package uses are all built around running code that
does: there is no `trybuild` dependency in the harness and no compile-fail
fixture anywhere in it. So the requirement is stated and unenforced, and it is
recorded that way rather than dressed up: a candidate that exported the default
factory type would be wrong here and would lose nothing for it. What the case
does instead is refuse to rely on the name -- `cap-generic-connection`
reaches the default by writing `Connection`, and reaches a custom factory by
naming the harness's own type, so it passes on a candidate that keeps the type
private and on one that does not.

Two constructors take the parameter explicitly:

```rust
pub fn connect_with_buffer_factory<F: BufFactory>(
    server_name: Option<&str>, scid: &ConnectionId, local: SocketAddr,
    peer: SocketAddr, config: &mut Config,
) -> Result<Connection<F>>;

pub fn accept_with_buf_factory<F: BufFactory>(
    scid: &ConnectionId, odcid: Option<&ConnectionId>, local: SocketAddr,
    peer: SocketAddr, config: &mut Config,
) -> Result<Connection<F>>;
```

Note that the two are not spelled alike -- one says `buffer_factory` and the
other says `buf_factory`. That is what upstream shipped; do not normalise it.

A `Connection<F>` for a caller-supplied `F` must complete a handshake and carry
stream data exactly as the default one does, over the same `send`/`recv` loop
and the same `stream_send`/`stream_recv` calls. The case supplies a
factory that counts every `buf_from_slice` call, and runs three legs:

* both ends on the supplied factory -- the transfer arrives byte-for-byte with
  its fin, **and the count moved**, so a `Connection<F>` that accepted the type
  parameter and then quietly used the default storage fails;
* both ends built by plain `connect`/`accept` -- the transfer still arrives,
  and the count must **not** move, so a counter that was measuring something
  ambient rather than this connection's factory fails;
* a client on one factory and a server on a different one -- the bytes agree,
  because the factory is a local storage decision and nothing about it is
  visible on the wire.

The two ends of a connection need not share a factory, and a connection's
factory is not negotiated.

The HTTP/3 layer follows the parameter through. Every `h3::Connection` method
that takes a transport connection takes `&mut super::Connection<F>` and is
generic in `F`; `h3::Connection` itself is not generic.

### `Connection::stream_send_zc`

```rust
impl<F: BufFactory> Connection<F> {
    /// Send a buffer the factory produced on a stream without copying it.
    ///
    /// Returns the number of bytes accepted and, when the whole buffer did
    /// not fit, the remainder. `len` caps how much of the buffer to send;
    /// `None` means all of it.
    pub fn stream_send_zc(
        &mut self, stream_id: u64, buf: F::Buf, len: Option<usize>, fin: bool,
    ) -> Result<(usize, Option<F::Buf>)>
    where F::Buf: BufSplit;
}
```

The buffer is moved in. What comes back is how many bytes the connection took
and, if it could not take all of them, the tail -- produced by
`BufSplit::split_at` on the caller's own buffer type, so the tail is the same
kind of buffer and can be handed straight back on the next call.

* When everything fits, the second element is `None` and the first is the
  length sent.
* When it does not, the first element is what was accepted and the second is
  `Some(tail)` whose length is the rest. The two always sum to the length
  offered.
* `len = Some(n)` sends at most `n` bytes and returns the rest as the tail
  even when the connection had capacity for more.
* `fin` is applied at the offset the call actually reached. An explicit `len`
  is the caller's own limit rather than a capacity truncation, so a call made
  with `len = Some(n)` and `fin = true` returns a tail **and still closes the
  stream** at the `n` bytes it took. The tail it handed back sits past the
  final size and can no longer be sent: offering it to a further call is
  `Err(Error::FinalSize)`. Only a truncation forced by the stream's own
  flow-control capacity demotes `fin` to false.

The errors are the ones `stream_send` already returns -- `Error::Done` when
there is no capacity at all, `Error::InvalidStreamState` for a stream the
local endpoint may not write to, `Error::StreamStopped` after a STOP_SENDING.

**The tail must be the caller's buffer, split -- not a copy of its bytes.**
That is the whole point of the call and it is what the case checks: the
factory it supplies builds buffers whose bytes live behind an `Arc`, it keeps a
clone of the buffer before handing it over, and it requires the returned tail
to share that allocation. An implementation that copies the unsent bytes into
storage of its own and rebuilds a buffer from a slice produces a tail with the
right contents and the wrong identity, and fails.

What arrives at the peer is ordinary stream data. The case sends with
`stream_send_zc` and reads with `stream_recv`, and the bytes compare equal.

### `h3::Connection::send_body_zc`

```rust
impl h3::Connection {
    /// Send an HTTP/3 body from a buffer the factory produced, without
    /// copying the body.
    pub fn send_body_zc<F>(
        &mut self, conn: &mut super::Connection<F>, stream_id: u64,
        body: &mut F::Buf, fin: bool,
    ) -> Result<usize>
    where F: BufFactory, F::Buf: BufSplit;
}
```

The transport-level analogue of `stream_send_zc`, one layer up. It takes the
body by `&mut` rather than by value: on return, `body` has been advanced past
whatever was sent, so a caller loops until it is empty rather than reassigning
a returned tail. The return value is the number of body bytes written.

A DATA frame needs a header in front of the payload. `send_body_zc` first asks
the buffer to grow one on its front with `BufSplit::try_add_prefix`; a buffer
that says `true` gets its frame header written in place and goes out as one
piece, and a buffer that says `false` -- which is what the default
implementation of the method returns -- causes the header to be written
separately, followed by the body. Both paths must produce the same bytes on the
wire, and the case checks exactly that: it runs the same body through a
factory whose buffers accept a prefix and through one whose buffers refuse it,
and the peer's `h3::Event::Data` yields the same bytes either way.

The return value counts **body** bytes. When the frame header was prepended
into the caller's buffer, the bytes written to the stream are more than the
bytes the caller asked to send, and the difference is not reported: a caller
that sends an N-byte body in one call is told N on both branches.

Ordering is the ordering `send_body` already has. The header section must have
gone first, or the call fails with `h3::Error::FrameUnexpected`.

### `Config::set_send_capacity_factor`

```rust
impl Config {
    /// Scales the send capacity the connection will hand out.
    ///
    /// The factor multiplies the congestion window's available capacity when
    /// the connection decides how much stream data it may take in one go.
    /// The default is 1.0.
    pub fn set_send_capacity_factor(&mut self, v: f64);
}
```

Set on the `Config`, so it applies to connections created from it afterwards
and not to ones already made. Internally the connection computes its per-call
transmit capacity as

```
tx_cap = (available_capacity as f64 * factor).ceil()
```

which means a factor above 1.0 lets a single `stream_send` accept more bytes
than the congestion window alone would allow, and a factor below 1.0 lets it
accept fewer. Rounding is upward, so a small window and a small factor still
yield at least one byte rather than zero.

This changes how much the connection *buffers*, not how fast it sends. The
congestion controller still governs what leaves the socket; the factor governs
how much the application is allowed to hand over before it is told to wait.
The scaling is exact, and the case pins it as such. On a freshly
handshaked connection, against a payload deliberately larger than any of the
capacities involved, a single `stream_send` accepts some number of bytes `n` at
a factor of 1.0; at 2.0 it accepts exactly `2 * n`, and at 4.0 exactly
`4 * n`. Leaving the factor unset accepts exactly `n` as well, because the
default is 1.0. A connection carrying a scaled factor must still complete an
ordinary transfer byte-for-byte.

### `CongestionControlAlgorithm::Bbr2Gcongestion, and gcongestion_enabled`

The algorithm enum gains a fifth variant. Its four existing discriminants are
unchanged:

```rust
#[repr(C)]
pub enum CongestionControlAlgorithm {
    Reno            = 0,
    CUBIC           = 1,
    BBR             = 2,
    BBR2            = 3,
    Bbr2Gcongestion = 4,
}
```

It is still reached as `quiche::CongestionControlAlgorithm`, and
`Config::set_cc_algorithm` and `Config::set_cc_algorithm_name` still select
with it. The enum's source file moves, which no caller can observe.

The name table gains one entry and keeps the four it had:

| name | variant |
| --- | --- |
| `reno` | `Reno` |
| `cubic` | `CUBIC` |
| `bbr` | `BBR` |
| `bbr2` | `BBR2` |
| `bbr2_gcongestion` | `Bbr2Gcongestion` |

The lookup is exact: no trimming, no case folding. `"BBR2"`, `"bbr2 "`,
`"bbr3"`, `"bbr3_gcongestion"` and `""` are all `Err(Error::CongestionControl)`
-- an unrecognised name is rejected, never silently defaulted.
In particular `"bbr2"` still selects `BBR2`, not the new variant -- the two
implementations are two names, and an existing configuration that asked for
`bbr2` gets the implementation it has been getting.

```rust
impl<F: BufFactory> Connection<F> {
    /// Whether this connection is running the gcongestion recovery
    /// implementation, or `None` if that cannot be determined.
    pub fn gcongestion_enabled(&self) -> Option<bool>;
}
```

`Some(true)` for a connection configured with `Bbr2Gcongestion`, `Some(false)`
for one configured with any of the other four. `None` only where there is no
active path to ask about; an established connection always has one. The case
asserts the answer on live, handshaken pairs -- one pair per algorithm --
and requires the two ends of a pair to agree, since both were configured the
same way.

The five variants are also five *distinct* values: `BBR2` and `Bbr2Gcongestion`
must not compare equal.

Both selections must also *work*: a connection set to `bbr2_gcongestion` by
either route completes a handshake and carries a multi-hundred-kilobyte stream
transfer to completion. A variant that parses but cannot move bytes is not the
feature.

### `h3::Priority is Copy, and send_additional_headers_with_priority`

```rust
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Priority { /* ... */ }
```

`h3::Priority` gains `Clone` and `Copy`, so it can be passed to a call and
still used by the caller afterwards. Its existing behaviour is unchanged:
`Priority::new(urgency, incremental)`, `Default` equal to
`Priority::new(3, false)`, and equality over both fields, so
`Priority::new(5, true) != Priority::new(5, false)`.

```rust
impl h3::Connection {
    /// Send an additional header section and set the stream's priority in
    /// the same call.
    pub fn send_additional_headers_with_priority<T: NameValue, F: BufFactory>(
        &mut self, conn: &mut super::Connection<F>, stream_id: u64,
        headers: &[T], priority: &Priority, is_trailer_section: bool,
        fin: bool,
    ) -> Result<()>;
}
```

The priority-carrying form of the additional-header-section send the previous
step introduced. The rules it inherits are the rules it keeps:

* An additional section is only legal after an initial header section has been
  sent on that stream. Called before one, it is
  `Err(h3::Error::FrameUnexpected)`.
* At most one **trailer** section per stream. A second call with
  `is_trailer_section = true` is `Err(h3::Error::FrameUnexpected)`.
* A client may send an additional section only as trailers. A client call with
  `is_trailer_section = false` is `Err(h3::Error::FrameUnexpected)`.

The urgency is clamped, not rejected. Urgency is a three-bit field, so a
`Priority` built with a value above 7 is treated as 7 before the internal
offset of 124 is added; the call succeeds and nothing overflows. The case
sends a trailer section at urgency 200 and requires `Ok(())`.

What the peer sees is unchanged by the priority argument: the sections arrive
in order, as separate `h3::Event::Headers`, carrying exactly the name/value
pairs that were sent, and the stream then reports `h3::Event::Finished`.

### `The tokio-quiche server surface`

`tokio-quiche` is an asynchronous front end over the transport crate. Its
public surface is large; what the end-to-end case exercises is the server path,
and only that part is specified here. The case builds a small crate that
depends on `tokio-quiche` by path, runs it, and drives the candidate's own
`quiche-client` at it over HTTP/3.

Listening:

```rust
pub fn listen<S, M>(
    sockets: impl IntoIterator<Item = S>,
    params: ConnectionParams,
    cid_generator: impl ConnectionIdGenerator<'static> + Clone,
    metrics: M,
) -> std::io::Result<Vec<QuicConnectionStream<M>>>;
```

It takes already-bound sockets -- a `std::net::UdpSocket` is accepted -- and
returns one stream of incoming connections per socket. `SimpleConnectionIdGenerator`
and `DefaultMetrics` are provided, in `tokio_quiche::quic` and
`tokio_quiche::metrics`, for callers that need no opinion about either.

Configuration:

```rust
impl<'a> ConnectionParams<'a> {
    pub fn new_server(
        settings: QuicSettings,
        tls_cert: TlsCertificatePaths<'a>,
        hooks: Hooks,
    ) -> Self;
}

pub struct TlsCertificatePaths<'p> {
    pub cert: &'p str,
    pub private_key: &'p str,
    pub kind: CertificateKind,   // X509 | RawPublicKey
}
```

`QuicSettings` and `Hooks` both implement `Default`, and the default ALPN list
is `["h3"]` -- which is why the case can point a plain HTTP/3 client at the
server without configuring protocols at all.

Serving one connection:

```rust
let (h3_driver, h3_controller) = ServerH3Driver::new(Http3Settings::default());
let conn = incoming.start(h3_driver);
let mut h3 = ServerH3Connection::new(conn, h3_controller);
```

The driver is the half that runs inside the connection's task; the controller
is the half the application holds. Requests arrive on
`h3.h3_controller.event_receiver_mut()` as `ServerH3Event::Core(H3Event::...)`,
and a request is `H3Event::IncomingHeaders(IncomingH3Headers { stream_id,
headers, send, recv, read_fin, h3_audit_stats })`.

Responding is two sends on the `send` half, which is a `futures::Sink`:

```rust
send.send(OutboundFrame::Headers(vec![h3::Header::new(b":status", b"200")], None)).await?;
send.send(OutboundFrame::body(BufFactory::buf_from_slice(&body), true)).await?;
```

`OutboundFrame::Headers` carries the header list and an optional
`quiche::h3::Priority`; `OutboundFrame::body(buf, fin)` wraps a pooled buffer
and the FIN flag. `tokio_quiche::buf_factory::BufFactory::buf_from_slice`
produces the pooled buffer from a slice.

The end-to-end case asserts three things and nothing about logging format:
the server observes the `:path` the client requested; the client exits 0; and
the body the client wrote to disk is byte-for-byte the body the server sent.
The response is a fixed 4096-byte pattern, so the comparison is exact.

## The groups at this step

One row per group. **gate** groups are all-or-nothing: a failure means the step is not satisfied; **requirement** groups cover this step's own new capability. The rows are grouped by tier for reading; the order they run in, and each one's result, is what `reward_details.json` records.

| group | tier | surface | what it observes |
| --- | --- | --- | --- |
| `candidate-workspace-builds` | gate | T4 | The one check every step shares: `cargo build --offline` (default targets -- libraries and binaries only, not tests or benches) must complete for the workspace at `/app` within the harness's build timeout, before any other group in this table runs. It is the one group every step carries identically, and no step declares it as a capability of its own -- but it is not optional either: one failing gate group, including this one, means the step is not satisfied, however much of the new work is right. |
| `cap-tokio-quiche` | requirement | T1 | The new workspace member, used the way a consumer would use it, against the candidate's own client. The verifier writes a small crate into scratch -- a `Cargo.toml` with its own empty `[workspace]` table so it does not get pulled into the candidate's, a path dependency on `<candidate root>/tokio-quiche`, `tokio` with `rt` and `macros`, and `futures` -- and a `main.rs` of about sixty lines that binds a `std::net::UdpSocket`, calls `listen` with `ConnectionParams::new_server`, `SimpleConnectionIdGenerator` and `DefaultMetrics`, and answers every `H3Event::IncomingHeaders` with `:status 200` and a fixed body. That crate is the observation instrument; building it is the first assertion, because a `tokio-quiche` whose public surface is not the one the interface document fixes does not compile against it. It is built with `cargo build --offline -j 4` and with `CARGO_TARGET_DIR` pointed at the same target directory the driver already used for the candidate's workspace, so every one of the crate's dependencies is already compiled and the probe build is a link rather than a rebuild -- measured at about 20 seconds cold and nothing thereafter, against the group's per-case ceiling. Then the two halves meet. The probe is spawned as a server on a free loopback port with the fixture certificate and key; the candidate's **own** `quiche-client` is run at it with `--no-verify --http-version HTTP/3` and a `https://127.0.0.1:<port>/probe.bin` URL. Four assertions, in order: * the client did not hit its deadline; * the client exited 0; * the probe's stderr contains `path=/probe.bin`, which is the server's report of the `:path` pseudo-header it actually received -- so the request reached the application layer and was decoded, not merely accepted at the transport; * the bytes the client wrote out are exactly the 4096-byte fixed pattern the probe sent, compared by length first and then byte-for-byte. The `path=` line is the probe's own output, written by the verifier's own source, so it is not a claim about the candidate's logging. Nothing is asserted about `tokio-quiche`'s tracing, its metrics values, or its client side; the client path, the datagram surface, and the connection hooks are all out of scope for this step's observation. The case takes the harness's T1 serial guard, because it binds a port and runs two processes. |
| `cap-generic-connection` | requirement | T3 | That `Connection<F>` is generic in more than its signature: the buffers really come from the factory the caller named. The harness supplies `CountingFactory`, whose `buf_from_slice` increments a thread-local counter and whose buffer holds its bytes behind an `Arc`, and `PrefixFactory`, a second factory of a different Rust type. Both are built out of the crate's own `BufFactory`/`BufSplit`, so the group fails to compile at all if those traits are not exported from the crate root -- which is the first thing it is measuring. Three legs over a 48 KiB stream transfer, all through `connect_with_buffer_factory`/`accept_with_buf_factory` and the ordinary `send`/`recv`/`stream_send`/`stream_recv` loop: * **both ends on `CountingFactory`** -- the server reassembles the payload byte-for-byte and sees the fin, *and* the counter is strictly higher after the transfer than before. A candidate that accepted the type parameter and then routed through its own default storage passes the first half and fails the second. * **both ends on plain `connect`/`accept`** -- the transfer still works and the counter does **not** move. This is the control: without it, a counter that was being bumped by something ambient would make leg 1 vacuous. * **client on `CountingFactory`, server on `PrefixFactory`** -- the bytes still agree. The factory is a local storage decision; nothing about it is negotiated or visible on the wire, and the two ends of a connection are free to disagree. `DefaultBufFactory` is deliberately never named. The harness cannot name it -- it is not exported -- and the group is built so that it does not need to: the default is reached by writing `Connection`, and a custom one by naming the harness's own type. Not asserted: how many buffers the factory is asked for, what sizes they are, or when. Those are scheduling details of the send path and would make the group a statement about an implementation's chunking rather than about the type parameter. |
| `cap-gcongestion-bbr2` | requirement | T3 | The fifth congestion controller, pinned at four levels: the discriminant, the name, the accessor, and whether it actually moves bytes. **Discriminants.** `Reno as u32 == 0`, `CUBIC == 1`, `BBR == 2`, `BBR2 == 3`, `Bbr2Gcongestion == 4`. The enum is `#[repr(C)]` and crosses the FFI boundary, so the four older values must be unchanged and the new one appended rather than inserted. `BBR2 != Bbr2Gcongestion` as values, because they are two different algorithms that happen to share a name in prose. **Names.** `set_cc_algorithm_name` accepts each of `reno`, `cubic`, `bbr`, `bbr2` and `bbr2_gcongestion` with `Ok(())`, and answers `Err(Error::CongestionControl)` for `bbr3_gcongestion` -- a near-miss chosen deliberately over an obviously absurd string, because the failure this catches is a lookup that falls through to a default when it does not recognise a name. **The accessor.** Three pairs that differ in exactly one line, each fully handshaked, each asked `gcongestion_enabled()` at both ends. The answers must be `(CUBIC, false)`, `(BBR2, false)`, `(Bbr2Gcongestion, true)` -- in that order, as a whole -- and the two ends of each pair must agree with each other, since both were configured the same way. `bbr2` mapping to `false` is not incidental: the crate is built with its default features, `gcongestion` is not among them, so the legacy implementation is what that name selects and the group would be wrong to expect otherwise. **That it works.** Each of the three pairs then carries a 256 KiB transfer, which must arrive byte-for-byte with its fin and leave neither end closed. A variant that parses and then cannot move data is not the feature. Not asserted: any throughput, timing or window figure. What separates BBRv2 from the gcongestion BBRv2 at runtime is pacing and window dynamics, and neither is something a bounded in-process case can measure without becoming a benchmark. |
| `cap-stream-send-zc` | requirement | T3 | The zero-copy stream send, and specifically the part of it that an implementation could fake. Over a handshaked `Connection<CountingFactory>` pair: * **The explicit length is honoured exactly.** A 48 KiB buffer is sent with `len = Some(1000)` on a connection whose capacity is far larger, so nothing but the caller's own limit can be binding: the call returns `1000`, the remainder is `Some`, its length is `payload.len() - 1000`, and its bytes are the tail of the buffer that was handed over. * **The remainder is the caller's buffer, not a copy of it.** A second send of the whole buffer with `len = None` must be partial -- the payload is deliberately larger than a fresh connection's capacity -- and the harness keeps a clone of the buffer before giving it away. `CountingBuf::clone` shares the allocation, so `Arc::ptr_eq` between that witness and the returned remainder is the zero-copy claim itself. An implementation that copied the unsent bytes into storage of its own and rebuilt a buffer from a slice returns the right bytes in the wrong allocation and fails here. * **The arithmetic closes.** The transfer is then finished by feeding the remainder back through the same call in a bounded loop, and on every round the bytes written plus the remainder's length equal what was handed over. `Error::Done` is a retry, not a failure. * **The bytes arrive.** The stream is closed with an empty `stream_send(.., true)` and the server reassembles the full 48 KiB byte-for-byte with its fin. * **`fin` lands at the offset the call reached.** The buffer goes out once more, on a fresh stream, with `len = Some(1000)` and `fin = true`. The call returns a tail, and the peer must then see exactly 1000 bytes on that stream **with the fin**: an explicit length is the caller's own limit, not a capacity truncation, so it does not demote `fin`. Offering the returned tail to a further call is then `Err(FinalSize)` -- those bytes are past the final size the stream just committed to. The partiality is forced with an explicit length rather than by leaning on the send capacity, so this says the same thing on a loaded host as on an idle one. * **The errors are the ones `stream_send` already returns.** Two of them are driven directly. A send on stream 3 -- server-initiated, unidirectional, and this is the client -- is `Err(InvalidStreamState(3))`, naming the stream the call was made on. A send on a stream the peer has shut down with a STOP_SENDING is `Err(StreamStopped(code))` carrying **the peer's code**. The particular code is this case's own choice, held in a `const` and deliberately not written down here: what is asserted is that the number the peer shut the stream down with is the number that comes back, and a contract that named it would let an implementation return *that* number instead of carrying the peer's. Not asserted: how large each accepted chunk is, or how many rounds the loop takes. Both follow from the congestion window at each moment and neither is part of the contract; the loop only carries a sanity bound so that a non-progressing implementation fails as a stall rather than as a hang. Nor is `Error::Done` driven deliberately -- it arrives on its own during the transfer loop, where it is treated as a retry. |
| `cap-h3-send-body-zc` | requirement | T3 | Both branches of the HTTP/3 zero-copy body send, and that they are indistinguishable to the peer. `send_body_zc` asks the caller's buffer to grow the DATA frame header on its front via `BufSplit::try_add_prefix`. The harness supplies one factory whose buffers accept a prefix (`PrefixFactory`) and one whose buffers refuse it (`CountingFactory`, which takes the trait's default implementation and returns `false`), and runs the same 3 KiB body through a complete HTTP/3 request/response over a `Connection<F>` pair on each. * **The count excludes the header.** Both legs must report exactly the body length. The prefix-accepting branch writes more bytes to the stream than the application supplied, and reporting that larger number would be reporting bytes the caller never asked to send; the case fails a candidate that forgot to subtract the prefix. * **Both legs deliver the body.** The client's `h3::Event::Data` yields the 3 KiB byte-for-byte in each leg. * **The two branches agree.** The bytes from the accepting leg and the bytes from the refusing leg compare equal to each other, so a candidate that ignored `try_add_prefix` and always wrote the header separately still has to produce the same wire bytes -- and one that mis-framed only the in-place branch fails this comparison even if each leg were somehow self-consistent. The exchange runs on `ALPN_H3` and confirms `application_proto() == b"h3"` before sending anything, so an HTTP/3 assertion cannot be made against a connection that negotiated something else. * **The header section has to have gone first.** On a third pair, the server polls the request and then calls `send_body_zc` without having called `send_response`: that must be `Err(h3::Error::FrameUnexpected)`, the documented variant and not a generic failure. The same call then succeeds once the response headers have gone, on the same stream with a buffer of the same kind -- so the leg is about the ordering rule and not about the stream or the buffer being wrong in some other way. Not asserted: the DATA frame's bytes as seen on the wire, or how many `stream_send` calls each branch made. The distinction between the branches is an allocation strategy; the only thing that may differ observably is nothing. |
| `cap-send-capacity-factor` | requirement | T3 | That the send capacity factor scales what a single `stream_send` will accept, exactly and not merely monotonically. Four freshly handshaked pairs, identical but for one line, each handed the same 512 KiB payload in one `stream_send` -- a payload deliberately larger than any of the capacities involved, so every measurement is capacity-bound rather than payload-bound: | configuration | accepted | | --- | --- | | factor unset | `n` | | `set_send_capacity_factor(1.0)` | `n` | | `set_send_capacity_factor(2.0)` | `2 * n` | | `set_send_capacity_factor(4.0)` | `4 * n` | with `0 < n < 512 KiB`. The unset row and the 1.0 row must be equal, which is how the documented default of 1 is checked without reading it back. The 2.0 and 4.0 rows are exact multiples, which is what distinguishes an implementation that multiplies from one that merely enlarges a buffer somewhere. A fifth pair at factor 2.0 then carries a 128 KiB transfer to completion, byte-for-byte with its fin, so that a factor which produced a bigger number and a broken connection does not pass. Not asserted: any effect on throughput, on the congestion window, or on what leaves the socket. The factor governs how much the application may hand over before being told to wait, and a case that claimed more would be claiming the congestion controller had been overridden. |
| `cap-h3-priority-helpers` | requirement | T3 | The two HTTP/3 priority additions, held to their documented contracts over a `Connection<CountingFactory>` -- because the whole `h3` API became generic in the buffer factory in the same release, and a group that used the default connection would not notice if it had not. **`Priority` as a value type.** A `Priority` is assigned away and the original is still usable afterwards, which only compiles if it is `Copy`; it is cloned; `Priority::default()` equals `Priority::new(3, false)`, the default urgency the specification names; and `Priority::new(5, true) != Priority::new(5, false)`, so equality covers the incremental flag and not only the urgency. **`send_additional_headers_with_priority`.** Over a complete HTTP/3 exchange: * called by the server **before** any initial header section on that stream -- `Err(h3::Error::FrameUnexpected)`; * called after the response headers, with `is_trailer_section = true` and a `Priority` built at **urgency 200** -- `Ok(())`. Urgency is a three-bit field, so this is the clamp: the value is pinned to 7 before the internal offset of 124 is added, and nothing overflows. A candidate that rejected the value, or that added 200 to 124 and wrapped, fails here; * called a **second** time with `is_trailer_section = true` -- `Err(h3::Error::FrameUnexpected)`, because a stream carries at most one trailer section; * called by the **client** with `is_trailer_section = false` -- `Err(h3::Error::FrameUnexpected)`, because an additional non-trailing section is a response construct. **What the peer sees.** The client is then drained, and must observe exactly two header sections on the request stream -- the `:status 200` response section first, then the trailer section with its name/value pairs verbatim -- followed by `h3::Event::Finished`. Two, not three: the refused calls must have put nothing on the wire. Not asserted: any scheduling consequence of the priority. The specification is explicit that priority parameters are a suggestion a server need not act on, and there is nothing for a single-request case to observe even if it did. |
| `cap-async-workspace` | requirement | T4 | The shape of the workspace itself, read the way cargo reads it. First, cheaply: a `Cargo.toml` must exist at each of the four new members -- `buffer-pool`, `datagram-socket`, `task-killswitch`, `tokio-quiche` -- and at each of the five carried ones -- `apps`, `h3i`, `octets`, `qlog`, `quiche`. This runs before the metadata call so that a missing directory is reported as a missing directory rather than as a cargo error. Then authoritatively: ``` cargo metadata --offline --no-deps --format-version 1 --manifest-path <root>/Cargo.toml ``` with `CARGO_TARGET_DIR` in scratch. Each of the nine names must appear in the `workspace_members` array, and the array must have **exactly nine** entries -- an extra member fails as surely as a missing one, because the membership list is the thing being observed and a candidate that added a tenth crate of its own has not reproduced this release's workspace. Member entries are matched by package name against the id syntax cargo emits, accepting both the `path+file://...#name@version` and `...#name` spellings, so the group does not break on a cargo whose id format changed. The metadata call proves membership and only membership. It does **not** stand in for a check of the root manifest's own tables: `--no-deps` asks cargo for the workspace's package list, and a candidate that wrote literal values into every member manifest instead of inheriting them produces exactly the same nine-member list whether the root declares `[workspace.package]` at all. Nor can it say anything about dependency edges, which `--no-deps` omits by construction. So the group reads two things off the manifests directly, with whole-line comments stripped first: * the root manifest carries `[workspace.package]` and `[workspace.dependencies]` and sets `resolver` to `"2"` -- either TOML string spelling, any spacing; * `quiche/Cargo.toml` names none of `buffer-pool`, `datagram-socket`, `task-killswitch` or `tokio-quiche`, in any dependency table. Not asserted: dependency versions, feature lists, edition, or any member's internal structure, and nothing at all about what the four new manifests contain beyond existing. The group's claim is the shape of the workspace -- which crates are in it, what the root hands down, and which way the one dependency that matters points. The rest is checked where it can be seen behaving. |
| `reg-05-recvinfo-from` | gate | T3 | Carried as this step's plan-mandated re-check of a step-5 hot-path observation. `RecvInfo` is the descriptor every inbound datagram is delivered with, and it is on the receive path of every connection in the package. This step replaces the type that path lives on -- `Connection` becomes `Connection<F>` -- and adds a second, independent stack on top of it, so if either of those rebuilt the receive path, the address descriptor is where it shows first. Two halves: * **The type.** `RecvInfo { from, to }` values compare equal to themselves and unequal when either field differs. Step 05 asserted this over a single-field `RecvInfo { from }`; the type gained `to` at a later release, so the carried assertion is widened to both fields rather than dropped. That is the only adaptation, and it is an adaptation of the *same* observation. * **The hot path.** A 4 KiB `client_to_server` transfer, and then, over every `SendInfo` the exchange produced at both ends, `info.to` is the address the datagram was actually addressed to -- the server's address for client-side sends, the client's for server-side sends. **Deliberately clock-free.** Step 05's sibling observation `cap-sendinfo-at` read `SendInfo::at`, a wall-clock instant, and was removed from the gate at steps 07 and 11 under the determinism rule: a gate group is all-or-nothing, so anything in it that could read differently under load means the step is not satisfied for every candidate. It stays removed here, and nothing in this case reads a clock, a duration or a timer. |
| `reg-10-bbr2` | gate | T3 | Carried because this step adds a fifth entry to the very enum that group was about. The failure mode it guards is specific and likely: appending a variant is the moment a name table, a discriminant or an exhaustive match gets rewritten, and a rewrite that gets the *new* entry right while disturbing an old one would otherwise be checked only by the group that was looking at the new entry. * **Names.** `"bbr2"` maps to `Ok(())` and selects `BBR2` -- not the gcongestion variant, and not a default. `"BBR2"`, `"bbr2 "`, `"bbr3"` and `""` are each `Err(Error::CongestionControl)`, as they were at step 10: the lookup does not case-fold, does not trim, and does not fall through. `reno`, `cubic` and `bbr` still resolve. * **Discriminants.** `(Reno, CUBIC, BBR, BBR2)` as `u32` is still `(0, 1, 2, 3)`. The new variant is checked where it belongs, in `cap-gcongestion-bbr2`; what this group asserts is that adding it moved nothing. * **That it still carries data.** Both selection routes -- by name and by variant -- are handshaked and each carries a 256 KiB transfer that arrives byte-for-byte with its fin, and `stats().sent_bytes` is at least the payload length. The `sent_bytes` bound is one-sided on purpose. An exact figure depends on how the send path chunked the transfer and on retransmission, neither of which a gate group may assert; "at least what we handed it" is true of every correct implementation and false of one that dropped the data. |
| `reg-08-probe-and-migrate` | gate | T1 | The end-to-end connection-migration path, run between the candidate's own `quiche-server` and `quiche-client`. The client is given `--no-verify` and `--perform-migration` and asked for a 512 KiB file. It must exit 0 within the group's deadline and write the file out byte-for-byte. That is the whole assertion. Carried because migration exercises path validation, connection-ID retirement and the address descriptors on both the send and receive paths at once, and because this step rebuilds the type those paths hang off. It is also the only gate group other than `gate-endtoend` that runs the two real binaries, so it is what would notice a workspace that builds but whose `apps` member no longer links. **The socket-sampling leg is deliberately absent.** Step 10's copy of this group counted locally bound UDP ports and required at least two at once, to witness the migration rather than merely its result. That leg was removed upstream under the determinism rule -- the sample is a race against the client's own socket lifetime -- and it stays removed. What survives is the one-sided, deterministic half: a migrating client that completes its transfer. The case takes the harness's T1 serial guard. |
| `gate-endtoend` | gate | T1 | Proof of life, and nothing else. The candidate's own `quiche-server` is started on a loopback port with `--listen`, `--cert`, `--key`, `--root` and `--no-retry`; its own `quiche-client` fetches one file from it with `--no-verify`; the client exits 0 and the bytes it wrote match the file the server was serving, byte-for-byte. Every other option is the reference's own default. Nothing from this step's milestone appears here on purpose -- no `tokio-quiche`, no buffer factory, no zero-copy send, no fifth congestion controller. Those are checked where they can be seen. This group is the floor underneath all of them, and it is the group that fails first if the workspace's growth from five members to nine broke the build of the two binaries that were already there, or if the root manifest's new inherited-dependency tables are malformed. It adds no new requirement of its own. |

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

`/app` contains everything step 12 left behind, unchanged. The milestone before this one is modelled on quiche 0.23.0; this one is modelled on 0.24.3. Read `WORKSPACE_HANDOFF.md` for what is already there and what must keep working.

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

