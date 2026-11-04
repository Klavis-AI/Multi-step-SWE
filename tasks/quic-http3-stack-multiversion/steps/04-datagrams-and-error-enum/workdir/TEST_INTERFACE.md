# Test interface — step 04, DATAGRAM extension and Error enum transformation

This is the contract between your implementation and the checker. Every
name, path and flag the checker depends on is written down here; nothing
outside this document is assumed about your code. Where the document and
your intuition disagree, the document wins — it is what the tests were
written against.

Generated, together with this step's interface contract, from one source.
The two cannot disagree.

## What this step is

Add the DATAGRAM extension (draft-ietf-quic-datagram) to the transport layer,
allowing unreliable application data to bypass the stream layer entirely. Transform
the Error enum from C-style discriminants to idiomatic Rust enum variants carrying
associated data. Strengthen the ConnectionId type from raw slices to a lifetime-aware
wrapper. These changes land in quiche 0.7.0 between draft-29 and the final RFCs.

DATAGRAM frames (types 0x30 and 0x31) carry application payloads up to one packet's
worth, with no delivery guarantees and no retransmission. The receiver maintains a
bounded queue; the sender learns the peer's maximum datagram size from transport
parameters and can query available capacity before sending.

## The flip

Something an earlier step established changes meaning here. This is the
part a candidate most often gets half-right: the new behaviour appears,
the old one is left working beside it, and both the new group and the
inherited group pass for the wrong reason — until the step that
requires the old behaviour to be *gone*.

The Error enum changes identity: what was `pub enum Error { Done = -1, ... }` with
explicit C-compatible discriminants becomes `pub enum Error { Done, BufferTooShort,
..., StreamStopped(u64), ... }`. At this step exactly one variant gains associated
data -- `StreamStopped(u64)`, carrying the peer's STOP_SENDING error code. Every
other variant, including `InvalidStreamState`, is still bare here; the transformation
is one variant at a time across releases, not all at once, and `InvalidStreamState`
does not gain its own `u64` payload until 0.9.0 (step 05). This is a breaking change
for any code matching on the integer representation or using FFI error codes,
though the Rust-side match arms remain compatible by name.

ConnectionId transitions from `&[u8]` / `Vec<u8>` to `ConnectionId<'a>`, a wrapper
that can hold either a borrowed slice or an owned vector. Existing call sites that
passed raw slices now need `ConnectionId::from_ref(&slice)` or
`ConnectionId::from_vec(vec)`.

## Interface elements the checker depends on

### `DATAGRAM transport parameters`

The `max_datagram_frame_size` transport parameter (0x20) advertises the largest
datagram the endpoint is willing to receive. If omitted, datagrams are not supported.
A value of 0 also indicates no support. The parameter is sent in both directions
during the handshake; support is negotiated independently for each direction.

### `DATAGRAM methods on quiche::Config and quiche::Connection`

One new method on `Config`, three new methods on `Connection` -- read the
receiver type on each signature, not just the method name, before deciding
where to call it:

```rust
// on quiche::Config
pub fn enable_dgram(&mut self, enabled: bool, recv_queue_len: usize,
                     send_queue_len: usize)

// on quiche::Connection
pub fn dgram_send(&mut self, buf: &[u8]) -> Result<()>
pub fn dgram_recv(&mut self, buf: &mut [u8]) -> Result<usize>
pub fn dgram_max_writable_len(&self) -> Option<usize>
```

`enable_dgram` is a **`Config`** method. It is called on the `Config` before
that `Config` builds a `Connection` (via `quiche::connect()` / `quiche::accept()`),
never on a `Connection` value directly -- there is no `enable_dgram` on
`Connection`. Calling it configures the transport parameter that negotiates the
extension during the handshake; the default, if it is never called, is
disabled (`false`), so a `Connection` built from an untouched `Config`
advertises no DATAGRAM support at all. `recv_queue_len` and `send_queue_len`
bound the queues, and each bound fails in its own direction: datagrams that
arrive past `recv_queue_len` are dropped, while a `dgram_send` past
`send_queue_len` reports `Error::Done` -- it does not drop silently and it does
not fail with `BufferTooShort`.

`dgram_send`, `dgram_recv` and `dgram_max_writable_len` are **`Connection`**
methods, called on the live connection after it exists.
`dgram_max_writable_len` returns `Some(n)` when the peer supports datagrams and
`n` is the largest payload that fits in a single packet, or `None` when
unsupported. `dgram_send` returns `Done` when the send queue is full;
`dgram_recv` returns `Done` when the receive queue is empty.

### `ConnectionId<'a>`

Previously `dcid` and `scid` were `Vec<u8>` in `Header` and `&[u8]` in functions like
`connect()` and `accept()`. Now they are `ConnectionId<'a>`, defined in
`src/packet.rs`:

```rust
pub struct ConnectionId<'a>(ConnectionIdInner<'a>);

impl<'a> ConnectionId<'a> {
    pub const fn from_vec(cid: Vec<u8>) -> Self
    pub const fn from_ref(cid: &'a [u8]) -> Self
}
```

The inner `ConnectionIdInner<'a>` is private; there is no derive on the wrapper.
Instead it hand-implements `Default`, `From<Vec<u8>>`, `PartialEq`, `Eq`,
`AsRef<[u8]>`, `Hash`, `Deref<Target = [u8]>`, `Clone`, and `Debug` (the `Debug`
impl prints lower-hex, not the derived form). `.len()`, `.is_empty()` and slice
indexing come from the `Deref` impl, not from an inherent method.

`Header::dcid` and `Header::scid` are now `ConnectionId<'a>`, and so are the
connection-ID parameters of the free functions: `connect(server_name:
Option<&str>, scid: &ConnectionId, config: &mut Config)` and `accept(scid:
&ConnectionId, odcid: Option<&ConnectionId>, config: &mut Config)`. Every call
site that used to pass `&[u8]` now needs `ConnectionId::from_ref(&slice)` or
`ConnectionId::from_vec(vec)`; this is the step's most consequential signature
change, since it will not compile silently.

### `Error enum with associated data`

The enum loses its C-style discriminants and gains associated data:

```rust
pub enum Error {
    Done,
    BufferTooShort,
    UnknownVersion,
    InvalidFrame,
    InvalidPacket,
    InvalidState,
    InvalidStreamState,       // bare at 0.7.0; gains a u64 stream-ID payload at 0.9.0
    InvalidTransportParam,
    FlowControl,
    StreamLimit,
    FinalSize,
    CongestionControl,
    StreamStopped(u64),       // carries error code
    // ... others
}
```

This is still `#[repr(C)]`-compatible via the FFI layer's separate error codes, but
Rust matches are now by variant name only. `StreamStopped` carries its error code
inline at this step; `InvalidStreamState` is still a bare variant here and only
gains its `u64` stream-ID payload at 0.9.0 (step 05), so do not match it as a
tuple variant yet.

The FFI layer itself is not built or checked at this step: this milestone's build
commands (`cargo build --offline`, `cargo build --offline --examples`) never pass
`--features ffi` or `--all-features`, so the C surface behind `feature = "ffi"` is
out of scope here regardless of whether it compiles. It first becomes an in-scope
surface at step 10.

### `quiche::h3::NameValue and quiche::h3::HeaderRef`

A small API change rides along with this release, and nothing at this step or
the next one calls it, so the compiler will not tell you about it — read it
here instead of discovering it two milestones later:

```rust
pub trait NameValue {
    fn name(&self) -> &str;
    fn value(&self) -> &str;
}

impl NameValue for Header { .. }        // Header's own tuple struct is unchanged

pub struct HeaderRef<'a>(&'a str, &'a str);
impl<'a> HeaderRef<'a> {
    pub fn new(name: &'a str, value: &'a str) -> Self;
}
```

The accessors move **off** `Header`'s inherent impl and **onto** this trait, so
`name()` and `value()` are only callable by a module that has imported
`quiche::h3::NameValue`. A tree that keeps them inherent still compiles for as
long as nothing imports the trait — and stops compiling the moment something
does. `HeaderRef<'a>` is a borrowed counterpart of `Header` for callers that do
not want to own the pair.

`Header::new` also stops lower-casing the name it is given: it now stores the
bytes as passed. Nothing in this package passes a name that is not already
lower-case, so no group observes the difference — but a caller that was relying
on `new` to normalise (or on the returned `name()` to be lower-case) is now
relying on nothing.

Both types take `&str` payloads at this milestone. The next release moves all
three of them to byte slices, and that is the change a later step's harness
compiles against, so build against the trait rather than against inherent
methods and the move will cost you a type, not a rewrite.

### `DATAGRAM frame encoding`

Two frame types in `src/frame.rs`:

- 0x30: DATAGRAM without Length — the datagram payload runs to the end of the packet
- 0x31: DATAGRAM with Length — a varint length, then the payload

Both are 1-RTT only and cannot appear in Initial, 0-RTT, or Handshake packets. The
parser is in `parse_datagram_frame()` and the encoder writes the type and optional
length prefix.

### `Config UDP payload size setters`

`Config::set_max_packet_size()` is **gone**. It is replaced by two setters that
separate the two directions the single old name conflated:

    pub fn set_max_recv_udp_payload_size(&mut self, v: usize)
    pub fn set_max_send_udp_payload_size(&mut self, v: usize)

`set_max_recv_udp_payload_size` sets the `max_udp_payload_size` transport
parameter the peer is told to respect when sending to us. `set_max_send_udp_payload_size`
caps what this endpoint will put on the wire and is clamped against whatever the
peer advertised, so setting it above the peer's limit does not raise the limit.

Both take a plain `usize` byte count, as `set_max_packet_size()` did, so a
carried-forward call site changes name only and not shape. A workspace that
still exposes `set_max_packet_size()` and not the two new names does not
satisfy this milestone: the verifier configures every connection through
`set_max_recv_udp_payload_size`, so the rename is load-bearing for every group
at this step and not only for the DATAGRAM ones.

Only the recv setter is independently checked here. `set_max_send_udp_payload_size`
must exist with this signature — it is part of the rename every group implicitly
depends on — but no group at this step calls it or observes its effect; it has no
outbound clamping consequence to check until step 05, where it is first exercised.

### `Connection::set_keylog`

pub fn set_keylog(&mut self, writer: Box<dyn std::io::Write + Send + Sync>)

Installs a sink that receives NSS-format keylog lines -- the same
`CLIENT_TRAFFIC_SECRET_0` / `SERVER_TRAFFIC_SECRET_0` records an
`SSLKEYLOGFILE` would collect -- as the connection derives each secret.

The method itself is inherited: the previous step already had it, taking a
`Box<dyn std::io::Write + Send>`. What changes at this milestone is the trait
bound. The writer must now also be `Sync`, so a sink that was accepted by the
previous release no longer compiles unless it is safe to share across threads.
That is the whole delta, and it is a compile-time one.

This is load-bearing for every group at this step and at every later step, not
only for the groups that read key material: the verifier installs a keylog on
the client of each connection pair it builds, so a workspace whose
`set_keylog` is missing fails to compile the verifier itself and therefore
fails all of its groups at once rather than failing one.

The `+ Sync` half is a **compile-time delta only**: the verifier hands the
method a writer that is already `Send + Sync`, so a signature that still says
`Box<dyn Write + Send>` accepts it and compiles. Nothing at this step observes
which bound you declared, and the change is not separately checked. Make it
anyway -- it is what this milestone's release changed, and a later step that
shares a sink across threads is the reason it changed.

### `Connection::is_draining`

```rust
pub fn is_draining(&self) -> bool
```

`true` while the connection is in the draining period that follows a received
`CONNECTION_CLOSE`: no new application data can be sent or received and the
connection object cannot yet be dropped, but `is_closed()` is still `false`
until the draining timer expires and `on_timeout()` retires it. `false` on a
live connection.

No group at this step calls it -- this milestone's DATAGRAM, error-enum and
connection-ID groups all pass without it -- and it is listed here rather than
left to be inferred because it is part of this release's `Connection` surface
and a later step's checker compiles against it by name: the ECN group at step 06
asserts that a connection which has processed an ACK frame carrying ECN counts
is neither closed nor draining, and it reads that through `is_draining()`.
A tree that carries the draining state internally but never exposes the
accessor builds and passes every group here, then fails that later step's whole
test target at compile time. Name it, return `bool` from it, and take
`&self`.

### `the AEAD the checker assumes`

Several groups recover 1-RTT keys from the secret your keylog exports and read
your own packets with the verifier's independent implementation. **That
implementation assumes `TLS_AES_128_GCM_SHA256` and nothing else**: it derives
the key and IV for AES-128-GCM from the traffic secret in the keylog line.
RFC 9001 Section 5.3 permits `TLS_AES_256_GCM_SHA384` and
`TLS_CHACHA20_POLY1305_SHA256` as well -- the forbidden one is
`TLS_AES_128_CCM_8` -- but a stack that negotiates either of those two is read
as sending packets that do not decrypt, and `cap-datagram-not-retransmitted`
and `cap-error-enum` then fail with a message about the verifier's own
decryption rather than about your code. The keylog line format this document
fixes carries no suite field, so nothing else tells the checker which one you
chose. Keep the pinned BoringSSL's preference order: AES-128-GCM first.

## The groups at this step

One row per group. **gate** groups are all-or-nothing: a failure means the step is not satisfied; **requirement** groups cover this step's own new capability. The rows are grouped by tier for reading; the order they run in, and each one's result, is what `reward_details.json` records.

| group | tier | surface | what it observes |
| --- | --- | --- | --- |
| `candidate-workspace-builds` | gate | T4 | The one check every step shares: `cargo build --offline` (default targets -- libraries and binaries only, not tests or benches) must complete for the workspace at `/app` within the harness's build timeout, before any other group in this table runs. It is the one group every step carries identically, and no step declares it as a capability of its own -- but it is not optional either: one failing gate group, including this one, means the step is not satisfied, however much of the new work is right. |
| `cap-datagram-roundtrip` | requirement | T3 | An in-memory candidate connection pair both enable datagrams with `enable_dgram(true, 16, 16)`. The client sends a 256-byte pseudorandom payload via `dgram_send()`, the server receives it with `dgram_recv()`, echoes it back, and the client reads the echo and verifies byte-for-byte equality. The datagram traverses the connection without stream framing, and both sides confirm `dgram_max_writable_len()` returns `Some(n)` with a non-zero `n` before sending. This step's own `client`/`server` examples have no DATAGRAM flags at all (they take no configuration until step 06), so the application-level echo behaviour this group needs is not drivable end to end through the candidate's binaries; the in-memory `Connection` API pair is the only surface that observes it. |
| `cap-datagram-not-retransmitted` | requirement | T2/T3 | The checker's verifier decrypts a packet from the candidate containing a DATAGRAM frame (type 0x30 or 0x31), deliberately drops it, and confirms the candidate never retransmits that frame -- even after loss recovery has genuinely run. The verifier never acknowledges anything, then drives the candidate's own PTO (probe-timeout) loop directly: it calls the candidate's `timeout()` to learn when the next timer fires, sleeps, calls `on_timeout()`, and repeats for up to 5 seconds of wall-clock time. A same-flight STREAM frame dropped the same way must reappear once a PTO fires -- proof that the timer genuinely ran and would have retransmitted something -- while the DATAGRAM frame's payload must never reappear in any packet the PTO loop produces. The candidate may send a new DATAGRAM frame with different data, but the original frame's payload never reappears. This distinguishes datagrams from STREAM frames, which are always retransmitted on loss via that same PTO mechanism. The verifier only reads: it decrypts the candidate's own packets with 1-RTT keys recovered from the candidate's keylog and never constructs or injects a packet of its own, so the observation runs on the in-memory candidate connection pair (T3) with a passive crypto-derived read (T2) layered on top, not on an independently-injected wire packet. |
| `cap-dgram-queue-bounded` | requirement | T3 | With `enable_dgram(true, 3, 3)`, the sender calls `dgram_send()` four times in rapid succession. The first three succeed (or the first three fit in the send queue), and the fourth returns `Error::Done` because the queue is full. Once the receiver has drained the queue with `dgram_recv()`, the sender can enqueue another. This confirms the queue length is enforced and that `Done` is the correct signal, not a panic or silent drop. |
| `cap-error-enum` | requirement | T2/T3 | The verifier builds its own STOP_SENDING frame for stream 4 with error code 0x123, protects it with 1-RTT keys derived from the candidate's own keylog, and injects that crafted packet into the candidate's connection via `recv()` -- the candidate never sends STOP_SENDING itself at this milestone, so the verifier acts as an independent peer rather than reading passively. The next `stream_send()` call on that stream then returns `Err(Error::StreamStopped(0x123))`, and a match on the error extracts the code via `if let Error::StreamStopped(code) = err { assert_eq!(code, 0x123); }`. `stream_capacity()` on the same stream shows the same identity: it also returns `Err(Error::StreamStopped(0x123))` rather than a byte count, since a peer that no longer wants the data cannot have capacity for it either. This confirms the enum carries associated data rather than requiring a separate accessor, and that the STOP_SENDING signal is visible through both APIs that can observe it. |
| `cap-dgram-max-size` | requirement | T3 | The client enables datagrams and completes the handshake. `enable_dgram` sets `max_datagram_frame_size` to 65536 on both sides -- quiche's own fixed value at this release, recommended by draft-ietf-quic-datagram-01 (see this step's DATAGRAM methods interface entry) -- so the transport parameter itself is never the binding limit; the real ceiling is how much of one packet is left after header, packet number and AEAD tag overhead. The group does not assert a literal byte count: it reads the candidate's own `dgram_max_writable_len()` -> `Some(n)` and then proves that `n` is the true boundary rather than a guess. It checks `n` is smaller than the connection's configured maximum UDP payload (so the claimed budget accounts for overhead at all), sends a datagram of exactly `n` bytes and requires it to succeed, sends `n + 1` bytes and requires `Err(Error::BufferTooShort)`, and then inspects the actual wire packet the `n`-byte datagram travelled in: that packet must be larger than `n` (room existed for the frame header) and less than 64 bytes larger than `n` (`carrier - max < 64`; the reported budget was not needlessly conservative). This self-calibrates against whatever `n` the candidate returns rather than asserting a fixed byte count directly, so it does not depend on the exact header, packet-number and AEAD-tag overhead a correct implementation computes -- only on that overhead being comfortably under the 64-byte slack, which it is at this release. What it does *not* catch is a conservative but self-consistent answer: a value that leaves more than 64 bytes of the packet unused still passes, because the slack is measured against the packet the candidate itself produced. An overstatement -- a budget larger than one packet can carry -- fails, on the `n + 1` leg or on the wire-length check. |
| `cap-dgram-not-negotiated` | requirement | T3 | The client enables datagrams with `enable_dgram(true, 16, 16)` but the server does not, or advertises `max_datagram_frame_size = 0`. After the handshake, the client's `dgram_max_writable_len()` returns `None`, and `dgram_send()` returns `Error::InvalidState` rather than sending a datagram the peer cannot parse. This confirms unilateral enablement does not break the connection. |
| `cap-connid-wrapper` | requirement | T3 | `Header::from_slice()` over a draft-29 Initial packet returns a `Header` whose `dcid` and `scid` are `ConnectionId<'a>`. Calling `.as_ref()` on each yields a `&[u8]` that matches the raw bytes from the wire. Constructing a `ConnectionId::from_vec()` and a `ConnectionId::from_ref()` and comparing them with `.as_ref()` confirms both borrowed and owned paths work, and that the wrapper does not corrupt the ID. The group also exercises the rest of the type's hand-implemented (not derived) trait surface: `ConnectionId::default()` is the empty ID and equals `from_vec(vec![])`; `From<Vec<u8>>::into()` produces the same value `from_vec()` does; `.clone()` on both a borrowed and an owned value produces an equal value; two equal-content connection IDs built through different constructors hash identically; and `format!("{:?}", cid)` prints lower-hex with no separators (e.g. `deadbeef01`), not the bracketed list a derived `Debug` would produce. |
| `reg-01-h3-get` | gate | T1 | Inherited from step 01: the candidate's `http3-client` performs a GET against the candidate's `http3-server` and retrieves the exact body. At step 04 the ALPN is still `h3-29` from step 03, and the HTTP/3 layer is unchanged; this group confirms no regression from adding DATAGRAM support to the transport. HTTP/3 SETTINGS-frame hardening (rejecting a missing or malformed SETTINGS frame at the start of the control stream) is not independently checked at this step. A working SETTINGS exchange is already this group's own precondition — any regression there fails this gate outright — so no separate group is added for it, and this step's theme is the transport layer (DATAGRAM, the Error enum, `ConnectionId`), not HTTP/3-layer robustness. |
| `reg-01-flow-control` | gate | T3 | Inherited from step 01: with `initial_max_stream_data_bidi_remote = 4096`, the sender's `stream_capacity()` limits the transfer correctly and reopens after the receiver drains. The Error enum change must not break the `FlowControl` variant, and the new DATAGRAM path must not interfere with stream flow control. |
| `gate-endtoend` | gate | T1 | The candidate's `client` completes a handshake against the candidate's `server` over UDP loopback and retrieves a small file. If this fails, every other observation at this step is noise. The DATAGRAM changes and type refactorings must not break basic connectivity. |

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

`/app` contains everything step 03 left behind, unchanged. The milestone before this one is modelled on quiche 0.5.0; this one is modelled on 0.7.0. Read `WORKSPACE_HANDOFF.md` for what is already there and what must keep working.

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

