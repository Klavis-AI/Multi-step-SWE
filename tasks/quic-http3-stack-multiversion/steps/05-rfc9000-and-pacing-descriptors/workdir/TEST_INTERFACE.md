# Test interface — step 05, RFC 9000 version 1 beside the drafts, and the pacing descriptors

This is the contract between your implementation and the checker. Every
name, path and flag the checker depends on is written down here; nothing
outside this document is assumed about your code. Where the document and
your intuition disagree, the document wins — it is what the tests were
written against.

Generated, together with this step's interface contract, from one source.
The two cannot disagree.

## What this step is

Add the published QUIC version 1 — `0x00000001`, RFC 9000 and RFC 9001 — to a
stack that so far has only ever spoken drafts, and make it the default without
removing the drafts. One server process must now answer a version-1 Initial and
a draft-29 Initial with the right key schedule for each, which means the Initial
salt, the Retry integrity key and the transport-parameters TLS codepoint all
become functions of the negotiated version rather than constants.

At the same time both halves of the I/O hot path change shape. `recv` grows a
second argument carrying the address a datagram came from, and `send` starts
returning a descriptor beside the length: where the packet should go, and the
time it should go at. The second field is the whole point — the send path now
has a pacer, and the caller is expected to honour the deadline rather than
write the datagram immediately.

Client-side session resumption comes back with it: a connection can export its
TLS session state and a later connection can import it, which is the ground
`is_resumed()` and 0-RTT stand on. HTTP/3 picks up the plain `h3` ALPN token
now that there is a final version to attach it to, and `Error` gains a second
variant carrying data.

## The flip

Something an earlier step established changes meaning here. This is the
part a candidate most often gets half-right: the new behaviour appears,
the old one is left working beside it, and both the new group and the
inherited group pass for the wrong reason — until the step that
requires the old behaviour to be *gone*.

`Connection::recv` and `Connection::send` both change signature in the same
release, and nothing about the old shapes keeps compiling:

```rust
// before (0.7.0)                       // after (0.9.0)
fn recv(&mut self, buf: &mut [u8])      fn recv(&mut self, buf: &mut [u8],
    -> Result<usize>                        info: RecvInfo) -> Result<usize>
fn send(&mut self, out: &mut [u8])      fn send(&mut self, out: &mut [u8])
    -> Result<usize>                        -> Result<(usize, SendInfo)>
```

`quiche::connect` and `quiche::accept` grow a `SocketAddr` in the same place —
`connect(server_name, scid, to, config)` and `accept(scid, odcid, from,
config)` — because a connection now knows its peer's address instead of being
told it implicitly by whoever owned the socket. A stack that keeps the old
signatures beside the new ones has not done this step; there is one shape, and
it is the new one.

The version flip is the subtler half. `PROTOCOL_VERSION` becomes
`0x00000001`, but drafts 27, 28 and 29 stay negotiable, so a server that
"upgraded to v1" by replacing the draft constants fails the half of this step
that requires the drafts still to work, and a server that added v1 to the
version list without making the Initial salt version-dependent fails the other
half. Both salts have to be live in the same process at the same time:
`38762cf7f55934b34d179ae6a4c80cadccbb7f0a` for version 1,
`afbfec289993d24c9e9786f19c6111e04390a899` for draft-29, and
`c3eef712c72ebb5a11a7d2432bb46365bef9f502` for drafts 27 and 28.

`Error::InvalidStreamState` stops being a bare variant and starts carrying the
stream id that provoked it, exactly as `StreamStopped` did one step earlier.
Code that matched `Error::InvalidStreamState` by name no longer compiles.

The HTTP/3 header types move to byte slices in the same release, and **none of
this step's groups touches them, so nothing here will fail you for leaving them
alone** — which is exactly what makes it worth reading:

```rust
// before (0.7.0)                          // after (0.9.0)
pub struct Header(String, String);         pub struct Header(Vec<u8>, Vec<u8>);
impl Header {                              impl Header {
    pub fn new(name: &str, value: &str)        pub fn new(name: &[u8], value: &[u8])
        -> Header;                                 -> Header;
}                                          }
                                           // on the NameValue trait, one step old:
trait NameValue {                          pub trait NameValue {
    fn name(&self) -> &str;                    fn name(&self) -> &[u8];
    fn value(&self) -> &str;                   fn value(&self) -> &[u8];
}                                          }

pub struct HeaderRef<'a>(&'a str, &'a str); pub struct HeaderRef<'a>(&'a [u8], &'a [u8]);
```

All five move together — the trait's two methods, `Header::new`, and the two
`impl NameValue` bodies — so a tree that converts the struct and the
constructor but leaves the trait returning `&str` does not compile, and neither
does the reverse. The step *after* this one builds every request it sends with
`h3::Header::new(b":method", b"GET")`-style calls and imports `h3::NameValue`
by name, so a tree carried forward with the `&str` shapes loses that step
entirely — a compile failure in its inherited gate target before anything that
step added is even evaluated.

Drafts 27 and 28 stay negotiable at this milestone alongside 29 and v1, and
nothing about their key schedules changes here, so this step's own groups
re-verify only v1 and draft-29 side by side (`cap-two-salts`) rather than all
four versions at once. Drafts 27/28 are not independently re-checked again
after this step until step 09, which removes them and asserts they are
rejected outright; a candidate that silently breaks their salts between here
and then is not caught by this task's verifiers, and that gap is accepted
rather than closed with a new group.

## Interface elements the checker depends on

### `quiche::PROTOCOL_VERSION and the negotiable version list`

`quiche::PROTOCOL_VERSION` is `0x00000001` — QUIC version 1, RFC 9000. It is
what `Config::new(quiche::PROTOCOL_VERSION)` selects and what a client puts on
the wire unless it is told otherwise.

Three draft versions remain negotiable beside it and a server must accept an
Initial in any of them rather than answering with a Version Negotiation packet:

| version | on the wire | Initial salt |
| --- | --- | --- |
| v1 | `0x00000001` | `38762cf7f55934b34d179ae6a4c80cadccbb7f0a` |
| draft-29 | `0xff00001d` | `afbfec289993d24c9e9786f19c6111e04390a899` |
| draft-28 | `0xff00001c` | `c3eef712c72ebb5a11a7d2432bb46365bef9f502` |
| draft-27 | `0xff00001b` | `c3eef712c72ebb5a11a7d2432bb46365bef9f502` |

The salt is selected by the version of the packet being protected, not by the
version the endpoint would have preferred, and both salts are live in one
process at the same time. Everything else about packet protection is RFC 9001
Section 5 unchanged.

The Retry integrity tag follows the same rule: RFC 9001 Section 5.8's key
`be0c690b9f66575a1d766b54e368c84e` and nonce `461599d35d632bf2239825bb` for
version 1, and these for draft-29, the pair the inherited tree already carries:

* key   `ccce187ed09a09d05728155a6cb96be1`
* nonce `e54930f97f2136f0530a8c1c`

The checker verifies the tag with its own implementation, so a Retry carrying a
tag computed with the wrong version's key is detected as such and not merely as
"a Retry".

### `quiche::RecvInfo and quiche::SendInfo`

```rust
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct RecvInfo {
    /// The address the packet was received from.
    pub from: std::net::SocketAddr,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct SendInfo {
    /// The address the packet should be sent to.
    pub to: std::net::SocketAddr,
    /// The time to send the packet out.
    pub at: std::time::Instant,
}
```

Both are plain public structs with public fields, both are `Copy`, and both
are re-exported from the crate root. `SendInfo.at` is the pacer's answer: an
`Instant` at or after which the caller should put the datagram on the wire. It
is never in the past relative to the moment the packet was produced and, for a
connection that has just been created and has nothing in flight, it is the
present rather than some far-future deadline.

### `the I/O hot path`

```rust
pub fn connect(server_name: Option<&str>, scid: &ConnectionId,
               to: std::net::SocketAddr, config: &mut Config)
    -> Result<std::pin::Pin<Box<Connection>>>;
pub fn accept(scid: &ConnectionId, odcid: Option<&ConnectionId>,
              from: std::net::SocketAddr, config: &mut Config)
    -> Result<std::pin::Pin<Box<Connection>>>;

impl Connection {
    pub fn recv(&mut self, buf: &mut [u8], info: RecvInfo) -> Result<usize>;
    pub fn send(&mut self, out: &mut [u8]) -> Result<(usize, SendInfo)>;
}
```

`send` still returns `Error::Done` when there is nothing to write, and `Done`
is still not a failure. What changed is that the success arm is a pair: the
length and the descriptor. `SendInfo.to` is the peer address the connection was
created with — the `to` given to `connect`, or the `from` given to `accept` —
until a later milestone makes it move.

### `client session resumption`

```rust
impl Connection {
    pub fn session(&self) -> Option<Vec<u8>>;
    pub fn set_session(&mut self, session: &[u8]) -> Result<()>;
    pub fn is_resumed(&self) -> bool;
    pub fn is_in_early_data(&self) -> bool;
}
impl Config {
    pub fn enable_early_data(&mut self);
    pub fn set_ticket_key(&mut self, key: &[u8]) -> Result<()>;
}
```

`session()` returns the serialised TLS session state once the server has
issued a session ticket — `None` before that — and the bytes are opaque to the
caller. `set_session()` is called on a fresh client connection before the
handshake starts; the resulting connection reports `is_resumed() == true` on
both ends once established, and so does the server that accepted it.

The server's ticket key is what makes a ticket minted by one `Config` usable
by another, so a checker that resumes across two server `Config`s sets the same
`set_ticket_key` on both. `key` must be exactly 48 bytes: a 16-byte key-name
prefix, a 16-byte AES-128 key and a 16-byte HMAC key, the layout BoringSSL's
underlying `SSL_CTX_set_tlsext_ticket_keys` requires. Any other length is
rejected with `Err(Error::TlsFail)` rather than panicking or silently
truncating.

That rejection is asserted. `cap-0rtt-client` calls `set_ticket_key` with a
47-byte and a 49-byte key before it does anything else and requires
`Err(Error::TlsFail)` from both, then requires the same `Config` to still
accept a well-formed 48-byte key. A `set_ticket_key` that returns `Ok` for a
wrong length, or that panics, or that truncates to fit, fails that group.

`enable_early_data()` and `is_in_early_data()` are part of this milestone's
API surface and are listed here for completeness, but this step covers only
ticket-based session resumption, not sending or receiving 0-RTT application
data: no group at this step calls `enable_early_data()` or inspects
`is_in_early_data()`. Deliberately out of scope here, not silently dropped.

### `quiche::Error::InvalidStreamState(u64)`

```rust
pub enum Error {
    // ...
    InvalidStreamState(u64),   // carries the stream id
    StreamStopped(u64),        // carries the error code, from step 04
    // ...
}
```

The variant that step 04 left bare now carries the stream id that provoked it.
`stream_recv`, `stream_send` and `stream_shutdown` against a stream that the
local endpoint is not allowed to touch — a server reading a server-initiated
unidirectional stream, for instance — return
`Err(Error::InvalidStreamState(id))` with `id` equal to the stream id passed
in. The wire error code it maps to is unchanged (`0x5`,
STREAM_STATE_ERROR) and the FFI integer is unchanged (`-7`).

### `the inherited surface the checker compiles against`

Beyond the names above, the checker's harness is linked against your crate and
compiles against the public surface the previous steps established. None of it
changes at this milestone; it is listed so that "the checker compiles against it"
is a fact you can read rather than infer:

```rust
quiche::MAX_CONN_ID_LEN                 // still a `usize` const, used as an array length
quiche::ConnectionId::from_ref(&[u8])   // the 0.7.0 wrapper, unchanged here
quiche::connect(Option<&str>, &ConnectionId, SocketAddr, &mut Config)
    -> Result<Pin<Box<Connection>>>
quiche::accept(&ConnectionId, Option<&ConnectionId>, SocketAddr, &mut Config)
    -> Result<Pin<Box<Connection>>>
Connection::{on_timeout, is_established, is_closed, application_proto,
             stream_send, stream_recv, stream_capacity, stats}
quiche::Error::Done                     // the terminator for a drained recv/send
quiche::h3::{Config, Connection, Header, Event}
quiche::h3::Config::new() -> Result<Config>
quiche::h3::Connection::with_transport(&mut quiche::Connection, &h3::Config)
    -> Result<h3::Connection>
quiche::h3::Connection::{send_request, send_response, poll}
quiche::h3::Error::Done                 // "no event right now"
```

`connect`, `accept`, `recv` and `send` are listed above with the shapes this
milestone gives them. The harness carries a shim for each of the four, so a
tree that has converted none of them still compiles; it is the shipped,
inherited surface underneath the shims -- the `Config` setters, the stream API,
`stats()`, the h3 layer and the error type -- that the harness always compiles
against directly, and the reason this list is worth reading before renaming
anything.

A candidate that leaves the inherited surface alone — which is what this step
asks — cannot fail on any of it. A rename or a signature change here is not
checked as a capability, but it stops the harness compiling, so it costs every
group at once.

### `quiche::h3::APPLICATION_PROTOCOL`

```rust
pub const APPLICATION_PROTOCOL: &[u8] = b"\x02h3\x05h3-29\x05h3-28\x05h3-27";
```

Wire format, length-prefixed, most preferred first. The plain `h3` token is
new here and it is first, because HTTP/3 over QUIC version 1 is the identifier
RFC 9114 registers; the three draft tokens stay for the peers that have not
moved. `Connection::application_proto()` on a pair configured with this
constant on both sides returns `b"h3"`.

### `quiche::Config setters at this milestone`

`Config::new(version: u32) -> Result<Config>` plus, in the spellings this
release uses:

```rust
set_max_idle_timeout(v: u64)               // milliseconds
set_max_recv_udp_payload_size(v: usize)    // note: usize
set_max_send_udp_payload_size(v: usize)
set_initial_max_data(u64)
set_initial_max_stream_data_bidi_local(u64)
set_initial_max_stream_data_bidi_remote(u64)
set_initial_max_stream_data_uni(u64)
set_initial_max_streams_bidi(u64)
set_initial_max_streams_uni(u64)
set_application_protos(&[u8])              // wire format
load_cert_chain_from_pem_file(&str)
load_priv_key_from_pem_file(&str)
verify_peer(bool)
grease(bool)
log_keys()
enable_dgram(bool, usize, usize)
enable_early_data()
set_ticket_key(&[u8])
set_disable_active_migration(bool)
set_cc_algorithm(CongestionControlAlgorithm)
```

`set_idle_timeout` and `set_max_packet_size` do not exist at this milestone;
they were renamed two and three steps ago and the old names are gone.

### `Connection getters at this milestone`

Three `Connection` methods arrive with this release, and all three are read by
a checker, at this step or a later one, **on the live connection** rather than
on the `Config` that built it:

```rust
impl Connection {
    pub fn max_send_udp_payload_size(&self) -> usize
    pub fn source_id(&self) -> ConnectionId
    pub fn destination_id(&self) -> ConnectionId
}
```

`max_send_udp_payload_size()` is the read-back of the setting of the same
name: the largest UDP payload this connection will put on the wire, taking
both the local `set_max_send_udp_payload_size` value and the peer's advertised
`max_udp_payload_size` into account, capped so that the value always encodes as
a two-byte varint (16383). During the handshake, before the peer's parameters
are known, it reports the 1200-byte minimum QUIC packet size. It is the same
quantity the initial congestion window is counted in -- packets of *this*
size, RFC 9002 Section 7.2 -- and a later milestone's checker reads it back off
a live connection and multiplies it, comparing the product against the window
that same connection reports. A stack whose accessor and whose window
arithmetic disagree fails there even though each is individually defensible,
so the value returned has to be the one the send path actually sizes packets
against, not a copy of the config field taken at construction time.

`source_id()` and `destination_id()` report the connection IDs this connection
is currently using, as the step-04 `ConnectionId<'a>` wrapper rather than as a
slice. Their values can change over the connection's lifetime. `ConnectionId`
gains one method alongside them, used wherever the caller needs the ID to
outlive the connection borrow:

```rust
impl<'a> ConnectionId<'a> {
    pub fn into_owned(self) -> ConnectionId<'static>
}
```

`into_owned()` converts a borrowed or owned ID into an owning one; it is what
a caller uses to compare an ID it read before a state change against one it
reads after. `AsRef<[u8]>` on the wrapper (step 04) is how the same ID is
compared as bytes.

### `Connection::set_keylog`, and the `Config::log_keys` that arms it

```
// on Config
pub fn log_keys(&mut self)
// on Connection
pub fn set_keylog(&mut self, writer: Box<dyn std::io::Write + Send + Sync>)
```

Two halves of one mechanism. `log_keys` turns secret export on in the TLS
context; `set_keylog` gives the connection somewhere to write the NSS-format
lines -- the same `CLIENT_TRAFFIC_SECRET_0` / `SERVER_TRAFFIC_SECRET_0` records
an `SSLKEYLOGFILE` would collect. Neither does anything without the other.

**Both are inherited and neither changes at this milestone.** The `Sync` half
of the writer bound arrived one step ago and is unchanged here. There is no
delta to implement; this element exists because the pair is load-bearing and
the setter list above names only one half of it.

What makes it load-bearing: `reg-04-error-enum` is a **gate** group, and it
derives real AEAD keys from what your connection exports. It calls `log_keys`
on the client config, installs a sink with `set_keylog`, runs the handshake,
pulls `SERVER_TRAFFIC_SECRET_0` out of the collected lines, and uses it to
protect a STOP_SENDING packet it hands back to the client through `recv`. The
secret it reads is the server's even though the sink is on the client, because
a TLS endpoint logs the secrets for both directions.

The consequences of getting this wrong are not proportional to its size:

* A missing `set_keylog`, or one whose bound is still `Send` without `Sync`,
  does not fail one group -- the verifier crate does not compile, so
  `candidate-workspace-builds` fails and the step is not satisfied.
* A `set_keylog` that compiles but writes nothing, or writes a format without
  a `SERVER_TRAFFIC_SECRET_0` line, fails `reg-04-error-enum`. That is also a
  gate group, so the step is still not satisfied, however many capability groups you
  passed.

This is the one place at this milestone where inherited, unchanged, entirely
undramatic code can fail a step it has nothing to do with. It is documented
here for that reason and not because there is work in it.

### `the example binaries`

Four programs the checker runs as processes: `client`, `server`,
`http3-client`, `http3-server`, found under `target/{debug,release}` or
`target/{debug,release}/examples`. Their command line is the positional one
this milestone inherited and does not change:

* `server` and `http3-server` take **no arguments**. They bind
  `127.0.0.1:4433`, load `examples/cert.crt` and `examples/cert.key` relative
  to the working directory they are started in, and serve files out of
  `examples/root` relative to the same directory. The checker does not write
  into your tree: it builds a directory of its own containing `examples/`
  with the certificate, the key and a `root/` holding the payload, and starts
  the binary with that directory as its working directory. Anything resolved
  from an absolute path, or from the location of the executable rather than
  from the process's working directory, is not found.
* `client` and `http3-client` take **exactly one argument**, a URL, and write
  the response body to stdout. Peer verification is compiled off; there is no
  flag for it and none is passed.
* `server` does address validation on every connection — a token-less Initial
  is answered with a Retry — and enables early data.

## The groups at this step

One row per group. **gate** groups are all-or-nothing: a failure means the step is not satisfied; **requirement** groups cover this step's own new capability. The rows are grouped by tier for reading; the order they run in, and each one's result, is what `reward_details.json` records.

| group | tier | surface | what it observes |
| --- | --- | --- | --- |
| `candidate-workspace-builds` | gate | T4 | The one check every step shares: `cargo build --offline` (default targets -- libraries and binaries only, not tests or benches) must complete for the workspace at `/app` within the harness's build timeout, before any other group in this table runs. It is the one group every step carries identically, and no step declares it as a capability of its own -- but it is not optional either: one failing gate group, including this one, means the step is not satisfied, however much of the new work is right. |
| `cap-rfc9000-v1` | requirement | T2 | `quiche::PROTOCOL_VERSION` is `0x00000001`, and the running server completes a whole version-1 address-validation exchange with a QUIC endpoint the checker implements itself. The checker sends a token-less version-1 Initial; the answer must be a Retry with `0x00000001` in its header, no inline ODCID field, a non-empty token, and a 16-byte integrity tag that recomputes correctly under RFC 9001 Section 5.8's *version-1* key `be0c690b9f66575a1d766b54e368c84e` and nonce `461599d35d632bf2239825bb` over the destination connection id the checker chose. The checker then sends a second version-1 Initial carrying that token and the Retry's source connection id, and opens the server's answer with Initial keys it derives itself from that connection id under version 1's salt `38762cf7f55934b34d179ae6a4c80cadccbb7f0a`; the answer has to be an Initial acknowledging packet number 0. A stack that has version 1 in its list but keys it from a draft salt, or tags its Retry with a draft's key, fails at the step that needs the key rather than at the version field. Nothing in the candidate's library is used to build or read any of the four packets. |
| `cap-two-salts` | requirement | T2 | One server process, two versions, two key schedules, all three observed from outside. First the checker sends an Initial at a version this milestone does not support (draft-23, `0xff000017`) and reads the Version Negotiation packet that comes back: the list in it must contain `0x00000001` *and* `0xff00001d`, so version 1 was added to the negotiable set rather than substituted for it. Then it completes a full Retry exchange at draft-29 and another at version 1 against that same process, and opens each server answer with Initial keys derived from that version's salt — `afbfec289993d24c9e9786f19c6111e04390a899` for draft-29 and `38762cf7f55934b34d179ae6a4c80cadccbb7f0a` for version 1. Both must open, and each answer must carry the version it was asked in. Finally each Retry's integrity tag is recomputed under the *other* version's Retry key and must not verify, which is what makes this a statement about two live key schedules rather than about one that happens to be reached twice. |
| `cap-client-version-v1` | requirement | T2 | The candidate's `client` is pointed at a UDP socket the checker owns, and the first datagram it sends is read with the checker's own implementation: a long header carrying version `0x00000001`, Initial keys derived from the packet's own destination connection id under version 1's salt, header protection removed with the AES-ECB mask, packet protection removed with AEAD_AES_128_GCM, and a CRYPTO frame at offset 0 whose bytes begin a TLS 1.3 ClientHello. This is `PROTOCOL_VERSION` observed from outside the process: a stack that still defaults to draft-29 produces a datagram that does not open under this salt. |
| `cap-sendinfo-at` | requirement | T3 | `Connection::send` returns `(usize, SendInfo)`, and `at` is an instant the caller can schedule against. Across a complete handshake driven through the in-memory pair, every `send` that produced bytes is bracketed by an `Instant` taken before the call and one taken after: `at` is never earlier than the first, and for a connection whose congestion window is nowhere near full it is never later than a second past the second. The `Instant` is also usable — `duration_since` against the pre-call reading succeeds rather than panicking. What this group can and cannot separate is worth stating plainly: it catches a far-future or unusable placeholder — `Instant::now() + 1_000_000`, or one that makes that `duration_since` underflow — and it does not catch `Instant::now()`. On a lossless in-memory pair whose congestion window is never driven to its limit there is nothing to pace, so a correct pacer's answer and `Instant::now()` are the same instant to within the clock's resolution, and no observation available to a black-box checker could tell them apart. What is checked is the descriptor's shape and the usability of the instant, not the existence of a pacer. |
| `cap-recvinfo-from` | requirement | T3 | The address plumbing the two descriptors carry. A client created with `connect(.., to, ..)` and a server created with `accept(.., from, ..)` are driven through a handshake in which every datagram is delivered with an explicit `RecvInfo { from }` naming the sender, and the `SendInfo.to` each side reports names its peer: the client's is the server address it was constructed with and the server's is the client address it was constructed with, on every packet of the exchange. A stack that ignores the addresses it is handed and reports a wildcard, a zero port or its own address fails. |
| `cap-0rtt-client` | requirement | T3 | A client exports its TLS session state and a second client imports it. After a first handshake against a server whose `Config` has a fixed ticket key, `session()` returns `Some(bytes)`; a fresh client connection built from a fresh `Config`, given those bytes through `set_session()` before its first packet, completes a second handshake against a server built from a second `Config` carrying the same ticket key, and both ends then report `is_resumed() == true`. A control connection that is given no session reports `is_resumed() == false` against the same server, so the group cannot be passed by returning `true` unconditionally. |
| `cap-h3-alpn-h3` | requirement | T3 | `h3::APPLICATION_PROTOCOL` is exactly `b"\x02h3\x05h3-29\x05h3-28\x05h3-27"` — the plain `h3` token first, the three draft tokens after it, wire format with one length byte each. A pair configured with the constant on both sides negotiates `h3` and not `h3-29`: `application_proto()` returns `b"h3"` on both ends. A pair whose server offers only `h3-29` still negotiates `h3-29`, so the list is a preference order and not a replacement. |
| `cap-invalid-stream-state` | requirement | T3 | `Error::InvalidStreamState` carries the stream id. On an established connection, a `stream_recv` on a locally-initiated unidirectional stream — a send-only stream, which the local endpoint may never read — returns `Err(Error::InvalidStreamState(id))`, and `if let Error::InvalidStreamState(got) = err` binds `got` to the stream id that was passed in. Two different stream ids produce two different payloads, so the group cannot be passed by a variant that carries a constant. |
| `reg-01-h3-get` | gate | T1 | Inherited from step 01: the candidate's `http3-client` performs a GET against the candidate's `http3-server` over UDP loopback and receives a 64 KiB body byte for byte. The invocation is this milestone's — the server takes no arguments and the body is staged into `examples/root/` under the crate root — but the observation is step 01's. Both hot-path signatures changed underneath HTTP/3 at this step, and the ALPN list grew a token; a stack that got either wrong serves nothing. |
| `reg-01-flow-control` | gate | T3 | Inherited from step 01: with `initial_max_stream_data_bidi_remote` set to 4096, `stream_capacity` falls to zero after 4096 bytes are in flight and `stream_send` accepts no more; once the receiver has drained them and the resulting MAX_STREAM_DATA has crossed, capacity reopens and the remainder of a 64 KiB transfer arrives intact and in order. The pacer now sits between `stream_send` and the wire, so a send path that honours `SendInfo.at` by withholding packets it should have written breaks this. |
| `reg-04-error-enum` | gate | T3 | Inherited from step 04: a stream that the peer has stopped with a STOP_SENDING frame carrying error code `0x123` makes the next `stream_send` on it return `Err(Error::StreamStopped(0x123))`, and the code is read out of the variant by pattern match rather than through an accessor. `Error` grows a second payload-carrying variant at this step; the one step 04 added must still carry its own payload and must still be the variant that this condition produces. |
| `gate-endtoend` | gate | T1 | The stack works at all: the candidate's `server` starts, the candidate's `client` completes a handshake against it over UDP loopback and retrieves a small file, and the client exits zero. If this fails, every other T1 observation at this step is noise. |

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

`/app` contains everything step 04 left behind, unchanged. The milestone before this one is modelled on quiche 0.7.0; this one is modelled on 0.9.0. Read `WORKSPACE_HANDOFF.md` for what is already there and what must keep working.

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

