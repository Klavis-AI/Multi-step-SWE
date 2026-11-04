# Test interface — step 01, "cells, circuits and directories"

This is the contract the requirement groups are written against. Everything the
checker touches is listed here: the crate, the module path, the exact public
signature, and the observable the group asserts on. If a name is not in this
document, no group depends on it and you may shape it as you like.

## 0. How the checker reaches your code

The checker builds a small driver crate **outside** your tree and path-links it
to your crates:

```toml
tor-cell = { path = "<your tree>/tor-cell" }
```

Consequences:

- **Crates live at the repository root** at this step — `<root>/tor-cell`,
  `<root>/tor-netdoc`, … — not under a `crates/` subdirectory.
- Every item below must be reachable from a crate root as a `pub` item with no
  non-default cargo feature enabled. The driver enables **no features**.
  A `pub(crate)` item, an item inside a private `mod`, a `#[doc(hidden)]`
  item, or an item behind a feature gate is not usable and no group relies
  on one.
- Your manifests are checksummed before and after the run; the checker never
  edits them. Crate names, and the crate in which each item lives, are part of
  the contract.
- Toolchain is rustc 1.98.1. The **driver** crate is `edition = "2021"`; your
  crates are `edition = "2018"` and are expected to stay that way. Nothing
  below asks you to change an edition, and your manifests are checksummed
  before and after the run.
- **Standard derives are part of the contract wherever a group observes a
  value.** The driver compares values with `assert_eq!`/`assert_ne!` and prints
  the unexpected branch of a `match` with `{:?}`, so every type named below
  that a group compares or reports must derive `Debug` and `PartialEq` (and
  `Eq`/`Hash`/`Ord` where the signature block says so). Where a specific derive
  or trait `impl` is load-bearing on its own it is written out in the block
  that introduces the type; this bullet is the blanket rule behind those.
- **Every constant newtype over an integer converts in both directions, and
  the outward direction must be `From`, not `Into`.** Twelve types below are
  declared as `pub struct X(uN)` with a table of associated constants —
  `ChanCmd`, `CircId`, `DestroyReason`, `RelayCmd`, `StreamId`, `EndReason`,
  `CertType`, `KeyType`, `ExtType`, `SocksCmd`, `SocksStatus`, `ProtoKind`.
  Each needs **both** `impl From<uN> for X` (wrap any integer, recognized or
  not — these are open sets, not enumerations) and `impl From<X> for uN` (read
  the number back out). The groups read values back with the *function* call
  `uN::from(x)` — `u8::from(CertType::TLS_LINK_X509)`, `u8::from(req.command())`,
  `u16::from(stream_id)` — and that call resolves **only** through
  `impl From<X> for uN`. An inherent `fn get()`, an `as_u8()`, or a
  hand-written `impl Into<uN> for X` does not satisfy it; the call site fails to
  compile, and because a group is compiled as one target the miss costs the
  whole group rather than the one case. Upstream declares ten of the twelve
  with a single declarative macro, `caret_int!`, which expands to both `From`
  impls plus `Display`, `Debug`, `is_recognized`, `from_name` and the
  associated constants; `CircId` and `StreamId` have the two impls written out
  by hand. The workspace reserves a `caret` crate for that macro and
  `tor-cell`'s manifest already depends on it, but like every crate here its
  `src/lib.rs` is an empty skeleton and no group imports it — writing the macro,
  using a third-party one, or writing the impls by hand all satisfy this
  contract equally.
- The driver's own dev-dependencies are `anyhow 1`, `base64 0.13`, `bytes 1`,
  `digest 0.9`, `futures 0.3`, `hex 0.4`, `http 0.2`, `rand 0.8`, `tempfile 3`.
  Where a signature below mentions a type from one of those crates
  (`bytes::BytesMut`, `http::Request<()>`, `rand::Rng`), your public API must
  use those same major versions.

Crates the driver depends on, all of which must therefore build with no
features: `tor-bytes`, `tor-cell`, `tor-cert`, `tor-checkable`, `tor-circmgr`,
`tor-consdiff`, `tor-dirclient`, `tor-dirmgr`, `tor-linkspec`, `tor-llcrypto`,
`tor-netdir`, `tor-netdoc`, `tor-proto`, `tor-protover`, `tor-rtcompat`,
`tor-socksproto`, `tor-units`.

## 1. Byte fixtures and how they are used

The driver ships its own fixtures; you do not need to produce any. They are
listed here because they define *which* wire formats are checked literally.

| fixture | what it is |
| --- | --- |
| `versions_handshake.bin` | the 11-byte VERSIONS cell emitted by `Versions::encode_for_handshake`: a **2-byte** circuit ID of zero, the command byte 7, a 2-byte length of 6, then the versions 3, 4 and 5 as big-endian `u16`s |
| `cell_relay_data.bin` | a 514-byte fixed-length RELAY cell carrying a RELAY_DATA body |
| `cell_certs.bin` | a variable-length CERTS cell carrying **two** certificates (an `RSA_ID_X509` body of `01 02 03` and an `IDENTITY_V_SIGNING` body of `ABCDEFGH`), 25 bytes in all |
| `cell_create2.bin` | a 514-byte CREATE2 cell with an ntor handshake |
| `relay_begin.bin`, `relay_extend2.bin`, `relay_sendme_tag.bin` | 509-byte relay cell payloads |
| `edcert_*.bin` | Ed25519 certificates: a well-formed signing cert, plus version, key-type, missing-extension, unknown-extension and critical-extension variants |
| `consensus-md.txt` and variants | a four-relay microdescriptor consensus, plus unsorted / unparsable / clamped-params / unsigned variants |
| `md-guardy.txt`, `md-exity.txt`, `md-middly.txt`, `md-bothy.txt` | the four matching microdescriptors, one per file |
| `mds-all.txt`, `mds-partial.txt` | the same microdescriptors bundled — all four, and three of them. Neither bundle carries annotations, so both parse under either `AllowAnnotations` setting |
| `md-bad-keylen.txt`, `md-no-ed-id.txt` | the two microdescriptors that must be refused: a wrong-length TAP onion key, and an `id` line naming some other key type |
| `authcert*.txt` | an authority certificate and two malformed variants |
| `routerdesc-*.txt` | malformed router descriptors |

These byte layouts were written from the specifications, not captured from any
implementation. Field order, field width and command numbers are therefore
requirements on your encoder, not descriptions of one particular encoder. See
`SPEC_NOTES.md` for the normative citation behind each one.

The consensus and microdescriptor fixtures describe four relays whose RSA
identities are `0x11…`, `0x22…`, `0x33…`, `0x44…` repeated twenty times and
whose nicknames are `guardy`, `exity`, `middly` and `bothy`.

## 2. Groups

Two crates are named by almost every group below rather than by one of them,
so their in-scope surface is stated once here. Both are driver dependencies
(§0), so both must build with no features.

### Shared items: the `tor-bytes` reader and writer

```rust
pub type Result<T> = std::result::Result<T, Error>;
pub enum Error { Truncated, ExtraneousBytes, BadMessage(&'static str), Internal }
pub struct Reader<'a> { .. }
impl<'a> Reader<'a> {
    pub fn from_slice(slice: &'a [u8]) -> Self;
    pub fn remaining(&self) -> usize;
    pub fn into_rest(self) -> &'a [u8];
    pub fn take(&mut self, n: usize) -> Result<&'a [u8]>;
    pub fn take_u8(&mut self) -> Result<u8>;
    pub fn take_u16(&mut self) -> Result<u16>;
    pub fn take_u32(&mut self) -> Result<u32>;
    pub fn take_until(&mut self, term: u8) -> Result<&'a [u8]>;
    pub fn extract<E: Readable>(&mut self) -> Result<E>;
    pub fn should_be_exhausted(&self) -> Result<()>;
}
pub trait Readable: Sized {
    fn take_from(b: &mut Reader<'_>) -> Result<Self>;
}
pub trait Writer {
    fn write_all(&mut self, b: &[u8]);
    fn write_u8(&mut self, x: u8);
    fn write_u16(&mut self, x: u16);
    fn write_u32(&mut self, x: u32);
    fn write_zeros(&mut self, n: usize);
    fn write<E: Writeable + ?Sized>(&mut self, e: &E);
    fn write_and_consume<E: WriteableOnce>(&mut self, e: E);
}
pub trait Writeable {
    fn write_onto<B: Writer + ?Sized>(&self, b: &mut B);
}
pub trait WriteableOnce {
    fn write_into<B: Writer + ?Sized>(self, b: &mut B);
}
impl Writer for Vec<u8> {}
impl Writer for bytes::BytesMut {}
```

The driver writes `let mut w: Vec<u8> = Vec::new(); w.write(&value);` and
`Reader::from_slice(bytes).extract::<T>()`, so `Writer` must be implemented
for `Vec<u8>` and every type this document says is encodable must implement
`Writeable`, and every type it says is decodable must implement `Readable`.
Integers are big-endian throughout.
`take_until(term)` returns the run of bytes before the next `term` byte and
consumes that byte with it; a reader with no `term` left is `Err`. It is part
of the surface from this step on -- the requirement groups read a NUL-terminated
name with `take_until(0)` -- so a `Reader` without it fails those groups to
compile, not only the cases that call it.

**`tor_bytes::Error` is checked only as a failure.** No group in this step
matches on a variant of it or on the string inside `BadMessage`: a malformed
input has to be rejected, and what the rejection is called is yours to choose.

### Shared items: identities and digests in `tor-llcrypto`

```rust
pub mod d { pub use sha1::Sha1; pub use sha2::{Sha256, Sha512};
            pub use sha3::{Sha3_256, Shake128, Shake256}; }
pub mod pk {
    pub mod rsa {
        pub struct RsaIdentity { .. }         // Copy, Eq, Ord, Hash, Display, Debug
        impl RsaIdentity {
            pub fn from_bytes(bytes: &[u8]) -> Option<Self>;   // None unless 20 bytes
            pub fn as_bytes(&self) -> &[u8];
        }
        impl From<[u8; 20]> for RsaIdentity {}
        pub struct PublicKey { .. }
        impl PublicKey {
            pub fn to_rsa_identity(&self) -> RsaIdentity;  // SHA-1 of the DER encoding
        }
    }
    pub mod ed25519 {
        pub use ed25519_dalek::{ExpandedSecretKey, Keypair, PublicKey, SecretKey, Signature};
        pub struct Ed25519Identity { .. }     // Copy, Eq, Ord, Hash, Display, Debug
        impl Ed25519Identity {
            pub fn from_bytes(id: &[u8]) -> Option<Self>;      // None unless 32 bytes
            pub fn as_bytes(&self) -> &[u8];
        }
        impl From<[u8; 32]> for Ed25519Identity {}
    }
    pub mod curve25519 { pub use x25519_dalek::{PublicKey, StaticSecret}; }
}
```

`d::Sha256` and `d::Sha3_256` are used through the `digest 0.9` traits
(`Digest::digest`), so they must be that crate's hashers and not a private
wrapper. `ed25519::PublicKey::from_bytes(&[u8])` returns a `Result`, while the
two `Identity` types return `Option` — the identities are opaque 20- and
32-byte labels and are not validated as curve points.

### `chancmd` — channel command numbers (`tor-cell`)

`tor_cell::chancell`:

```rust
pub struct ChanCmd(u8);                       // From<u8> for ChanCmd, From<ChanCmd> for u8,
                                              // Display
impl ChanCmd {
    pub fn is_var_cell(self) -> bool;
    pub fn is_recognized(self) -> bool;
    pub fn accepts_circid_val(self, id: CircId) -> bool;
}
pub struct CircId(u32);                       // From<u32> for CircId, From<CircId> for u32
impl CircId { pub fn is_zero(&self) -> bool; }
```

Associated constants on `ChanCmd`, each equal to the command number in
tor-spec: `PADDING`, `CREATE`, `CREATED`, `RELAY`, `DESTROY`, `CREATE_FAST`,
`CREATED_FAST`, `NETINFO`, `RELAY_EARLY`, `CREATE2`, `CREATED2`,
`PADDING_NEGOTIATE`, `VERSIONS`, `VPADDING`, `CERTS`, `AUTH_CHALLENGE`,
`AUTHENTICATE`, `AUTHORIZE`.

`accepts_circid_val` sorts every command into **three** classes, not two, and
a group asserts all three:

- **Zero only** — accepts a zero `CircId`, rejects a nonzero one:
  `PADDING`, `NETINFO`, `PADDING_NEGOTIATE`, `VERSIONS`, `VPADDING`, `CERTS`,
  `AUTH_CHALLENGE`, `AUTHENTICATE`.
- **Nonzero only** — rejects a zero `CircId`, accepts a nonzero one:
  `CREATE`, `CREATED`, `RELAY`, `DESTROY`, `CREATE_FAST`, `CREATED_FAST`,
  `RELAY_EARLY`, `CREATE2`, `CREATED2`.
- **Unconstrained** — every other command number, including
  `AUTHORIZE` and any number this version does not recognize, accepts
  **both**. A group builds `ChanCmd::from(201)` and asserts that it accepts a
  zero `CircId` *and* a nonzero one, so a rule written as a two-way test that
  answers `false` for an unrecognized command loses the group.

Observables: the `u8` value of each constant; `is_var_cell` true exactly for
the commands ≥ 128 and for `VERSIONS`; the three `accepts_circid_val` classes
above; `Display` renders a recognized command by name
and an unrecognized one by number; `is_recognized` is true for each of the
constants above and false for a command number this version does not define;
`CircId::is_zero`.

### `cellcodec` — channel cell framing (`tor-cell`)

`tor_cell::chancell`:

```rust
pub const CELL_DATA_LEN: usize = 509;
pub type RawCellBody = [u8; CELL_DATA_LEN];
pub struct ChanCell { .. }
impl ChanCell {
    pub fn new(circid: CircId, msg: msg::ChanMsg) -> Self;
    pub fn circid(&self) -> CircId;
    pub fn msg(&self) -> &msg::ChanMsg;
    pub fn into_circid_and_msg(self) -> (CircId, msg::ChanMsg);
}
```

`tor_cell::chancell::msg`, the two bodies this group frames directly:

```rust
pub struct Relay { .. }                       // RELAY and RELAY_EARLY
impl Relay {
    pub fn from_raw(body: RawCellBody) -> Self;
    pub fn into_relay_body(self) -> RawCellBody;
}
pub struct Destroy { .. }
impl Destroy { pub fn new(reason: DestroyReason) -> Self; }
pub struct DestroyReason(u8);                 // impl From<DestroyReason> for u8
                                              // NONE = 0, PROTOCOL = 1, INTERNAL = 2,
                                              // REQUESTED = 3, HIBERNATING = 4,
                                              // RESOURCELIMIT = 5, CONNECTFAILED = 6,
                                              // OR_IDENTITY = 7, CHANNEL_CLOSED = 8,
                                              // FINISHED = 9, TIMEOUT = 10,
                                              // DESTROYED = 11, NOSUCHSERVICE = 12
```

`tor_cell::chancell::codec`:

```rust
pub struct ChannelCodec { .. }
impl ChannelCodec {
    pub fn new(link_version: u16) -> Self;
    pub fn write_cell(&mut self, item: ChanCell, dst: &mut bytes::BytesMut) -> crate::Result<()>;
    pub fn decode_cell(&mut self, src: &mut bytes::BytesMut) -> crate::Result<Option<ChanCell>>;
}
```

Those `crate::Result`s are the crate's own, over this type at the crate root:

```rust
#[derive(Error, Debug, Clone)]
#[non_exhaustive]
pub enum Error {
    BytesErr(#[from] tor_bytes::Error),
    IoErr(#[source] std::sync::Arc<std::io::Error>),
    InternalError(String),
    ChanProto(String),
    BadStreamAddress,
    CantEncode,
}
```

Observables: a fixed-length cell encodes to exactly 514 bytes at link version
4 (4-byte circuit ID, 1 command byte, 509 payload bytes) with the payload
zero-padded; a variable-length cell is 5 header bytes plus a 2-byte length
field plus that many body bytes; each of `cell_relay_data.bin`, `cell_certs.bin` and
`cell_create2.bin` decodes to the expected circuit ID, command and body;
truncated input yields `Ok(None)` and leaves the buffer intact; a cell whose
circuit ID the command forbids is an `Err` — the variant is
`Error::ChanProto`, whose message names the circuit ID it refused (the ID is
data, not prose: keep it in the rendering); `Relay::from_raw` and
`into_relay_body` round-trip a 509-byte body unchanged; a DESTROY body is its
single reason byte (`DestroyReason::PROTOCOL` is 1) followed by zero padding
out to `CELL_DATA_LEN`.

Two scope limits on this group, both of which widen what you may do:

- **Every case constructs the codec with `ChannelCodec::new(4)`.** The
  `link_version` argument is part of the signature, but no group observes a
  channel at any other link version, so whatever you do with versions 1–3 is
  out of scope. Nothing here asks you to make the circuit-ID width depend on it.
- **No group frames a VERSIONS cell through `ChannelCodec`.** VERSIONS is the
  one cell whose circuit-ID width the link version cannot govern — it is what
  negotiates the link version — so it is not carried by the codec at all. Its
  wire format is produced by `Versions::encode_for_handshake` and checked in
  `chanmsg` below. `write_cell` is therefore only ever asked to frame the
  commands listed above, and `decode_cell` only ever to read them. This holds
  at every later step too, not only this one — `/app/SPEC_NOTES.md` §1.1
  states the basis and the one condition under which a later step could
  override it.

### `chanmsg` — channel message bodies (`tor-cell`)

`tor_cell::chancell::msg`:

```rust
pub enum ChanMsg { Padding, VPadding, Create, Created, .. }  // one variant per command
impl ChanMsg {
    pub fn cmd(&self) -> ChanCmd;
    pub fn write_body_onto<W: tor_bytes::Writer + ?Sized>(self, w: &mut W);
    pub fn take(r: &mut tor_bytes::Reader<'_>, cmd: ChanCmd) -> tor_bytes::Result<Self>;
}
pub struct Versions { .. }
impl Versions {
    pub fn new<B: Into<Vec<u16>>>(vs: B) -> Self;
    pub fn encode_for_handshake(self) -> Vec<u8>;
    pub fn best_shared_link_protocol(&self, my_protos: &[u16]) -> Option<u16>;
}
pub struct VPadding { .. }
impl VPadding { pub fn new(len: u16) -> Self; }
pub struct Certs { .. }
impl Certs {
    pub fn new_empty() -> Self;
    pub fn push_cert_body<B: Into<Vec<u8>>>(&mut self, certtype: tor_cert::CertType, cert: B);
    pub fn cert_body(&self, tp: tor_cert::CertType) -> Option<&[u8]>;
}
pub struct Netinfo { .. }
impl Netinfo {
    pub fn for_client(their_addr: Option<std::net::IpAddr>) -> Self;
    pub fn for_relay<V: Into<Vec<std::net::IpAddr>>>(timestamp: u32, their_addr: Option<IpAddr>, my_addrs: V) -> Self;
}
pub struct Padding { .. }
impl Padding { pub fn new() -> Self; }
pub struct AuthChallenge { .. }
impl AuthChallenge {
    pub fn new<B, M>(challenge: B, methods: M) -> Self
    where
        B: Into<[u8; 32]>,                    // the challenge is exactly 32 bytes
        M: Into<Vec<u16>>;
}
pub struct Authenticate { .. }
impl Authenticate { pub fn new<B: Into<Vec<u8>>>(authtype: u16, body: B) -> Self; }
```

Observables: the handshake encoding of VERSIONS matches `versions_handshake.bin`
byte for byte — a **2-byte** circuit ID of zero, the command byte 7, a 2-byte
body length, then one big-endian `u16` per offered version, so
`Versions::new([3, 4, 5]).encode_for_handshake()` is 11 bytes. This is the only
place the two-byte circuit ID of tor-spec §3 is checked, and it belongs to this
function rather than to `ChannelCodec` (see `cellcodec` above);
`best_shared_link_protocol` returns the highest version in both lists and
`None` when they are disjoint; `Certs` encodes a 1-byte count then
(type, 2-byte length, body) per certificate and round-trips
`cell_certs.bin`; `Netinfo::for_client` writes a zero timestamp, the
other party's address and an empty own-address list, while `for_relay` writes
the given timestamp and lists its own addresses.

The NETINFO body is compared byte for byte, so its record layout is part of
the contract: a 4-byte timestamp, then the other party's address as
(1-byte type, 1-byte length, value), then a **1-byte count** of the sender's
own addresses, then that many records in the same (type, length, value) form.
Type `0x04` is IPv4 with length 4; type `0x06` is IPv6 with length 16.
A client therefore emits `00 00 00 00 | 04 04 a b c d | 00`, **11 bytes**, and
a relay announcing one IPv4 and one IPv6 address of its own emits **35 bytes**.

A `Padding` body is empty and a VPADDING body is `len` zero bytes;
AUTH_CHALLENGE is a 32-byte challenge, a 2-byte method count and one `u16`
per method (38 bytes for two methods), and AUTHENTICATE is a 2-byte type, a
2-byte length and the body.

### `relaycmd` — relay command numbers (`tor-cell`)

`tor_cell::relaycell`:

```rust
pub struct RelayCmd(u8);                      // From<u8> for RelayCmd, From<RelayCmd> for u8,
                                              // Display
impl RelayCmd {
    pub fn accepts_streamid_val(self, id: StreamId) -> bool;
    pub fn is_recognized(self) -> bool;
}
pub struct StreamId(u16);                     // From<u16> for StreamId, From<StreamId> for u16
impl StreamId { pub fn is_zero(&self) -> bool; }
```

Constants: `BEGIN`, `DATA`, `END`, `CONNECTED`, `SENDME`, `EXTEND`, `EXTENDED`,
`TRUNCATE`, `TRUNCATED`, `DROP`, `RESOLVE`, `RESOLVED`, `BEGIN_DIR`, `EXTEND2`,
`EXTENDED2`.

Observables: the `u8` value of each; `accepts_streamid_val` requires a nonzero
stream for the stream-directed commands, requires zero for the
circuit-directed ones, and accepts either for `SENDME`; `Display` names a
known command; `is_recognized` is false for an undefined command number;
`StreamId::is_zero`.

### `relayhdr` — relay cell header and padding (`tor-cell`)

`tor_cell::relaycell`:

```rust
pub struct RelayCell { .. }
impl RelayCell {
    pub fn new(streamid: StreamId, msg: msg::RelayMsg) -> Self;
    pub fn cmd(&self) -> RelayCmd;
    pub fn msg(&self) -> &msg::RelayMsg;
    pub fn into_streamid_and_msg(self) -> (StreamId, msg::RelayMsg);
    pub fn encode<R: rand::Rng + rand::CryptoRng>(self, rng: &mut R) -> crate::Result<RawCellBody>;
    pub fn decode(body: RawCellBody) -> tor_bytes::Result<Self>;
    pub fn decode_from_reader(r: &mut tor_bytes::Reader<'_>) -> tor_bytes::Result<Self>;
}
```

`RawCellBody` is declared in **`tor_cell::chancell`**, beside `CELL_DATA_LEN`
(the block above); `tor_cell::relaycell` imports it privately for the
signatures above and does **not** re-export it, so the path
`tor_cell::relaycell::RawCellBody` does not exist and a `RawCellBody` defined
only under `relaycell` fails to compile every group target that compiles the
driver's shared module.

**The two `Result` types in that block are different types, and this document
qualifies both so that nothing has to be inferred.** `encode` returns
`crate::Result<_>` — `tor_cell`'s own alias, over `tor_cell::Error`. The two
decoders return `tor_bytes::Result<_>`, over `tor_bytes::Error`. Upstream
writes the decoders unqualified only because `relaycell.rs` opens with
`use tor_bytes::{Error, Result};`, which puts a second `Result` in scope
alongside the crate's own; read without that `use` line the bare spelling would
most naturally be taken for the `crate::Result` written on the line above it,
and it is not.

The same `tor_bytes::Result` is the return type of `RelayMsg::decode_from_reader`
in the `relaymsg` block below, and **there it is compile-checked**: one group
helper is declared `fn parse(cmd: RelayCmd, b: &[u8]) -> tor_bytes::Result<RelayMsg>`
and returns that call directly, and another pushes it through `?` into a
`tor_bytes::Error` return, so a decoder returning `tor_cell::Result` is a type
error that costs `relaymsg` and `sendmefc` outright. `RelayCell::decode` and
`RelayCell::decode_from_reader` are themselves only ever read through
`.expect(..)` / `.expect_err(..)`, which would accept either error type — but
they are the same functions in the same `impl` block in the upstream source,
so implement them with the error type stated here.

`tor_cell::relaycell::msg::Data`:

```rust
pub struct Data { .. }
impl Data {
    pub const MAXLEN: usize;                  // CELL_DATA_LEN - 11 == 498
    pub fn new(inp: &[u8]) -> Self;
}
impl AsRef<[u8]> for Data {}                  // how a group reads a body back out
```

`AsRef<[u8]>` is the **only** accessor these groups use to read a `Data` body:
they write `d.as_ref() == b"hello world"`. A `Deref<Target = [u8]>`, a
`body()`, an `as_slice()` or an `into_vec()` will not do instead, so implement
this trait even if you also provide one of those.

`msg::RelayMsg` must derive **`Debug`**. Both this group and `relaymsg` match
on a decoded message and report the unexpected branch with
`panic!("expected Data, got {:?}", other)`, so a `RelayMsg` that cannot be
formatted with `{:?}` fails to compile — and because a group is compiled as one
target, that costs the whole group rather than the one case.

Observables: `cell_relay_data.bin` decodes to the expected command, stream ID
and body; the encoded header is 11 bytes —
command / 2-byte recognized / 2-byte stream ID / 4-byte digest / 2-byte
length — with recognized and digest written as zero; the length field counts
only the message body; the encoded body is always `CELL_DATA_LEN` bytes.

The padding that follows a short body is **not** random from its first byte.
tor-spec requires four zero bytes immediately after the body, and only then
random filler (see `SPEC_NOTES.md` §1.3), so for an 11-byte `Data` payload
bytes 22..26 of the encoded body are `00 00 00 00` and the random filler
begins at byte 26. A full-length 498-byte payload leaves no room for either.

A length field that runs past the end of the payload is rejected; only the
rejection is checked, not which error reports it or what it says.
`Data::MAXLEN == CELL_DATA_LEN - 11 == 498`.

### `relaymsg` — relay message bodies (`tor-cell`)

`tor_cell::relaycell::msg`:

```rust
pub enum RelayMsg { Begin, Data, End, Connected, Sendme, Extend, Extended,
                    Extend2, Extended2, Truncate, Truncated, Drop, Resolve,
                    Resolved, BeginDir, Unrecognized }
impl RelayMsg {
    pub fn cmd(&self) -> RelayCmd;
    pub fn encode_onto(self, w: &mut Vec<u8>);
    pub fn decode_from_reader(c: RelayCmd, r: &mut tor_bytes::Reader<'_>) -> tor_bytes::Result<Self>;
}

The list above is the *names*; the payloads are part of the contract too. Every
variant wraps the message struct of the same name — `Begin(Begin)`,
`Data(Data)`, `Truncated(Truncated)`, `Resolved(Resolved)`,
`Unrecognized(Unrecognized)`, and so on — **except `Truncate`, `Drop` and
`BeginDir`, which are unit variants and carry nothing at all**. The groups
construct those three as bare values (`RelayMsg::Drop`, `RelayMsg::Truncate`,
`RelayMsg::BeginDir`), so a `RelayMsg::Drop(Drop)` shaped like the others is a
type error, and a type error in this crate costs the whole group rather than
the one case.
pub struct BeginFlags: u32;                   // bitflags; IPV6_OKAY, IPV4_NOT_OKAY, IPV6_PREFERRED
                                              // a group builds one with `from_bits_truncate`
pub struct Begin { .. }
impl Begin { pub fn new<F: Into<BeginFlags>>(addr: &str, port: u16, flags: F) -> crate::Result<Self>; }
pub struct End { .. }
impl End {
    pub fn new_misc() -> Self;
    pub fn new_with_reason(reason: EndReason) -> Self;
    pub fn new_exitpolicy(addr: std::net::IpAddr, ttl: u32) -> Self;
    pub fn reason(&self) -> EndReason;
}
pub struct EndReason(u8);                     // impl From<EndReason> for u8
// MISC = 1, RESOLVEFAILED = 2, CONNECTREFUSED = 3, EXITPOLICY = 4, DESTROY = 5,
// DONE = 6, TIMEOUT = 7, NOROUTE = 8, HIBERNATING = 9, INTERNAL = 10,
// RESOURCELIMIT = 11, CONNRESET = 12, TORPROTOCOL = 13, NOTDIRECTORY = 14
pub struct Connected { .. }
impl Connected {
    pub fn new_empty() -> Self;
    pub fn new_with_addr(addr: IpAddr, ttl: u32) -> Self;
}
pub struct Resolved { .. }
impl Resolved {
    pub fn new_empty() -> Self;
    pub fn new_err(transient: bool, ttl: u32) -> Self;
    pub fn add_answer(&mut self, answer: ResolvedVal, ttl: u32);
}
pub enum ResolvedVal { Ip(IpAddr), Hostname(Vec<u8>), TransientError, NontransientError, Unrecognized(u8, Vec<u8>) }
pub struct Unrecognized { .. }
impl Unrecognized {
    pub fn new<B: Into<Vec<u8>>>(cmd: RelayCmd, body: B) -> Self;
    pub fn cmd(&self) -> RelayCmd;
}
```

Observables: the RELAY_BEGIN body is `host:port\0` plus a 4-byte flag word and
matches `relay_begin.bin` byte for byte; the hostname is lowercased, an IPv6
literal is bracketed, and an all-zero flag word is omitted; a non-ASCII target
or an unparsable port is an error; a DATA body is the payload verbatim; an END
body is one reason byte, plus address and TTL for `EXITPOLICY`; an empty END
body means `MISC`; `u8::from` on `EndReason` gives the numbers tabulated above
and four of them are asserted directly — `MISC` = 1, `EXITPOLICY` = 4,
`TORPROTOCOL` = 13 and `NOTDIRECTORY` = 14; CONNECTED
distinguishes IPv4 (4-byte address) from IPv6 (the 0 sentinel, a type byte and
16 bytes); RESOLVED answers are type / length / value / 4-byte TTL, and a
declared length that disagrees with the fixed size of the value type it
introduces is refused rather than read generically; `Drop`/`BeginDir`/`Truncate`
have empty bodies and an unrecognized command keeps its bytes.

### `extend2` — circuit extension (`tor-cell`, `tor-linkspec`)

`tor_cell::relaycell::msg`:

```rust
pub struct Extend2 { .. }
impl Extend2 {
    pub fn new(linkspec: Vec<tor_linkspec::LinkSpec>, handshake_type: u16, handshake: Vec<u8>) -> Self;
    pub fn handshake_type(&self) -> u16;
    pub fn handshake(&self) -> &[u8];
}
pub struct Extended2 { .. }
impl Extended2 {
    pub fn new(handshake: Vec<u8>) -> Self;
    pub fn into_body(self) -> Vec<u8>;
}
```

`tor_cell::chancell::msg`:

```rust
pub struct Create2 { .. }
impl Create2 {
    pub fn new<B: Into<Vec<u8>>>(handshake_type: u16, handshake: B) -> Self;
    pub fn handshake_type(&self) -> u16;
    pub fn body(&self) -> &[u8];
}
pub struct Created2 { .. }
impl Created2 {
    pub fn new<B: Into<Vec<u8>>>(handshake: B) -> Self;
    pub fn into_body(self) -> Vec<u8>;
}
```

`tor_linkspec`:

```rust
pub enum LinkSpec {
    OrPort(std::net::IpAddr, u16),
    RsaId(tor_llcrypto::pk::rsa::RsaIdentity),
    Ed25519Id(tor_llcrypto::pk::ed25519::Ed25519Identity),
    Unrecognized(u8, Vec<u8>),
}
impl LinkSpec { pub fn sort_by_type(lst: &mut [Self]); }
```

`LinkSpec` implements `tor_bytes::Readable` and `tor_bytes::Writeable`.

Observables: `relay_extend2.bin` decodes to the expected link specifiers,
handshake type and handshake; `Extend2::new` sorts the specifiers and
reproduces those bytes; each link-specifier type encodes as type / 1-byte
length / value with lengths 6, 20, 32 and 18; sorting is IPv4, RSA, Ed25519,
IPv6; a length that disagrees with its type is rejected; EXTENDED2 is a
2-byte length then the handshake; `cell_create2.bin` decodes to type 2 and a
84-byte ntor handshake.

### `sendmefc` — SENDME flow control (`tor-cell`)

`tor_cell::relaycell::msg`:

```rust
pub struct Sendme { .. }
impl Sendme {
    pub fn new_empty() -> Self;
    pub fn new_tag(x: [u8; 20]) -> Self;
    pub fn into_tag(self) -> Option<Vec<u8>>;
}
```

Observables: a tagged SENDME body is version 1, a 2-byte length of 20 and the
20-byte tag, matching `relay_sendme_tag.bin`; an empty SENDME body is zero
bytes; version 0 and an unrecognized version both yield no tag; a tag shorter
than its declared length is rejected; SENDME is accepted both with stream ID 0
and with a nonzero stream ID.

### `linkhs` — the v3 link handshake (`tor-proto`)

`tor_proto::channel`:

```rust
pub struct ChannelBuilder { .. }
impl ChannelBuilder {
    pub fn new() -> Self;
    pub fn set_declared_addr(&mut self, target: std::net::SocketAddr);
    pub fn launch<T: futures::AsyncRead + futures::AsyncWrite + Send + Unpin + 'static>(self, tls: T)
        -> handshake::OutboundClientHandshake<T>;
}
pub mod handshake {
    pub struct OutboundClientHandshake<T> { .. }
    impl<T> OutboundClientHandshake<T> {
        pub async fn connect(self) -> Result<UnverifiedChannel<T>>;
    }
}
pub use tor_proto::Error;                     // the crate's error type
```

The driver drives this over an in-memory `AsyncRead + AsyncWrite` stream whose
read side is a pre-built byte script, and asserts on the bytes the client
wrote plus the `Result`.

Three properties of the client's own VERSIONS cell are checked and are easy to
miss:

- **`connect()` writes exactly one cell — its VERSIONS — and nothing else
  before it reads.** A group reads the 2-byte length field out of the cell it
  just captured and asserts that the total number of bytes written is exactly
  `5 + length`. Sending NETINFO, or anything else, optimistically ahead of the
  responder's reply fails that assertion.
- **The offered version list must contain at least one version ≥ 4.** A group
  parses the body as big-endian `u16`s and requires one of them to be 4 or
  greater, because the rest of the step frames cells with a 4-byte circuit ID.
  Upstream offers exactly `[4]`; any superset that still contains a version
  ≥ 4 also passes.
- **A responder that offers only versions 1 and 2 must be rejected**, even
  though those are real link protocol numbers and the arithmetic intersection
  with a wider client list would be non-empty. The client's own supported set
  starts at 4, so `[1, 2]` shares nothing with it and the handshake must fail.

Observables: the client's first write is a VERSIONS cell with a 2-byte circuit
ID, it is the only cell written before the client reads, and the versions it
offers include one ≥ 4; a responder script of VERSIONS / CERTS /
AUTH_CHALLENGE / NETINFO is
accepted; VPADDING and unrecognized commands interleaved in that script are
ignored; a peer that sends no VERSIONS, or one that offers only versions 1
and 2, or a script with no CERTS cell, or
with two CERTS cells, or with **two AUTH_CHALLENGE cells**, or with no
NETINFO, is rejected. Each of these is checked as a failed handshake only: no
case asserts which `tor_proto::Error` variant reports it, or what it says.

### `edcert` — Ed25519 certificates (`tor-cert`, `tor-checkable`)

`tor_cert`:

```rust
pub struct CertType(u8);   // impl From<CertType> for u8
                           // TLS_LINK_X509 = 0x01, RSA_ID_X509 = 0x02,
                           // LINK_AUTH_X509 = 0x03, IDENTITY_V_SIGNING = 0x04,
                           // SIGNING_V_TLS_CERT = 0x05, SIGNING_V_LINK_AUTH = 0x06,
                           // RSA_ID_V_IDENTITY = 0x07, NTOR_CC_IDENTITY = 0x0A
pub struct KeyType(u8);    // impl From<KeyType> for u8
                           // ED25519_KEY = 0x01, SHA256_OF_RSA = 0x02,
                           // SHA256_OF_X509 = 0x03
pub struct ExtType(u8);    // impl From<ExtType> for u8
                           // SIGNED_WITH_ED25519_KEY = 0x04
pub struct Ed25519Cert { .. }
impl Ed25519Cert {
    pub fn decode(cert: &[u8]) -> tor_bytes::Result<KeyUnknownCert>;
    pub fn expiry(&self) -> std::time::SystemTime;
    pub fn is_expired_at(&self, when: std::time::SystemTime) -> bool;
    pub fn subject_key(&self) -> &CertifiedKey;
    pub fn signing_key(&self) -> Option<&tor_llcrypto::pk::ed25519::PublicKey>;
    pub fn cert_type(&self) -> CertType;
}
pub enum CertifiedKey { Ed25519(ed25519::PublicKey), RsaSha256Digest([u8;32]), X509Sha256Digest([u8;32]), Unrecognized(UnrecognizedKey) }
impl CertifiedKey {
    pub fn key_type(&self) -> KeyType;
    pub fn as_bytes(&self) -> &[u8];
    pub fn as_ed25519(&self) -> Option<&ed25519::PublicKey>;
}
pub struct KeyUnknownCert { .. }
impl KeyUnknownCert {
    pub fn peek_cert_type(&self) -> CertType;
    pub fn peek_subject_key(&self) -> &CertifiedKey;
    pub fn check_key(self, pkey: &Option<ed25519::PublicKey>) -> tor_bytes::Result<UncheckedCert>;
}
pub struct UncheckedCert { .. }               // tor_checkable::SelfSigned<SigCheckedCert>
impl UncheckedCert {
    pub fn peek_signing_key(&self) -> &tor_llcrypto::pk::ed25519::PublicKey;
}
pub struct SigCheckedCert { .. }              // tor_checkable::Timebound<Ed25519Cert>
```

The Ed25519 key a certificate certifies is a **`tor_llcrypto::pk::ed25519::PublicKey`**
(the `ed25519_dalek` re-export), not an `Ed25519Identity`. The two are not
interchangeable here: `PublicKey::as_bytes` returns `&[u8; 32]` while
`Ed25519Identity::as_bytes` returns `&[u8]`, and a group writes
`assert_eq!(c.peek_subject_key().as_ed25519().map(|k| k.as_bytes()), Some(&f[7..39].try_into().unwrap()))`
— and the same shape again over `signing_key()`. The `try_into` there can only
infer a *sized* `[u8; 32]` target, which is what `&[u8; 32]` on the left-hand
side gives it. An `Ed25519Identity`
there is a trait-bound error, not a warning, and it costs the group.
`CertifiedKey::as_bytes` is separately `&[u8]`, because it also covers the two
32-byte digest variants.

`tor_cert` declares no `Result` alias of its own: the fallible functions above
return `tor_bytes::Result`, i.e. `std::result::Result<_, tor_bytes::Error>`.
No case in this group binds that error type — every one of them ends in
`.expect(..)` or `.expect_err(..)` — so a different error type still compiles
here; the qualification is stated so the signature is unambiguous rather than
because it is checked.

`tor_checkable`:

```rust
pub trait Timebound<T> {
    type Error;
    fn is_valid_at(&self, t: &std::time::SystemTime) -> std::result::Result<(), Self::Error>;
    fn dangerously_assume_timely(self) -> T;
    fn check_valid_at(self, t: &SystemTime) -> std::result::Result<T, Self::Error>;
    fn check_valid_now(self) -> std::result::Result<T, Self::Error>;
    fn check_valid_at_opt(self, t: Option<SystemTime>) -> std::result::Result<T, Self::Error>;
    // the last three have default bodies, as upstream writes them: check_valid_at
    // is is_valid_at then dangerously_assume_timely, check_valid_now calls it with
    // `SystemTime::now()`, and check_valid_at_opt passes `Some(t)` through and
    // treats `None` as "now"
}
pub trait SelfSigned<T> {
    type Error;
    fn is_well_signed(&self) -> std::result::Result<(), Self::Error>;
    fn dangerously_assume_wellsigned(self) -> T;
    fn check_signature(self) -> std::result::Result<T, Self::Error>;
}
pub trait ExternallySigned<T>: Sized {
    type Key: ?Sized;
    type KeyHint;
    type Error;
    fn key_is_correct(&self, k: &Self::Key) -> std::result::Result<(), Self::KeyHint>;
    fn is_well_signed(&self, k: &Self::Key) -> std::result::Result<(), Self::Error>;
    fn dangerously_assume_wellsigned(self) -> T;
    fn check_signature(self, k: &Self::Key) -> std::result::Result<T, Self::Error>;
}
pub struct TimerangeBound<T> { .. }           // impl Timebound<T>, Error = TimeValidityError
impl<T> TimerangeBound<T> {
    pub fn new<U: std::ops::RangeBounds<SystemTime>>(obj: T, range: U) -> Self;
    pub fn extend_tolerance(self, d: std::time::Duration) -> Self;
    pub fn extend_pre_tolerance(self, d: std::time::Duration) -> Self;
}
pub struct SignatureGated<T> { .. }           // impl SelfSigned<T>
pub enum TimeValidityError { Expired(std::time::Duration), NotYetValid(Duration), Unspecified }
```

`tor_checkable` has **three** root traits, not two. `Timebound` and
`SelfSigned` unwrap an object on their own; `ExternallySigned` is for an object
whose signature can only be checked against a key supplied from outside, and it
is the trait a consensus is unwrapped through (see `nsparse` below). The
driver's shared helper module imports `ExternallySigned` and `Timebound`, and
that module is compiled into **sixteen of this step's twenty-three group
targets** (the other eight build without it), so an
`ExternallySigned` that is missing, private, or named differently is not a lost
group — it is a lost step. `SelfSigned` is imported by the `edcert`, `authcert`
and `dirmgrcfg` groups directly rather than through the shared module. Note
that both `SelfSigned` and `ExternallySigned` declare a method named
`dangerously_assume_wellsigned`; which one applies is decided by the type it is
called on.

`Lifetime::new` **rejects** unless `valid_after < fresh_until < valid_until`
strictly, returning the crate's own error for that case (a `Result`, never a
panic); when it accepts, the three accessors return exactly the instants it was
given. `TimerangeBound::new` keeps the object and the range it was given —
inclusive and exclusive bounds are treated alike — `is_valid_at` reports
`TimeValidityError::NotYetValid` before that range and `Expired` after it, and
the two `extend_*` methods widen one end each.

Observables: `edcert_signing.bin` decodes to cert type 4, key type 1, its
32-byte subject key, and its signing key taken from the
`SIGNED_WITH_ED25519_KEY` extension; expiry is the 4-byte hour count times
3600 seconds, and `is_expired_at` flips at that instant; a version byte other
than 1 is rejected; a certificate with no key extension has
`signing_key() == None` and `check_key(&None)` fails while `check_key(&Some(k))`
succeeds; `check_key` with a key different from the embedded one fails;
an unknown non-critical extension is skipped, while an unknown extension with
the critical flag set is rejected; `peek_signing_key` returns the embedded
signing key before any signature has been checked; the numeric values of the
eight `CertType` and three `KeyType` constants listed above, and of
`ExtType::SIGNED_WITH_ED25519_KEY`. Every rejection here is checked as a
rejection only: no group asserts which error variant reports it, or what it
says.

### `nsparse` — consensus parsing (`tor-netdoc`)

`tor_netdoc::doc::netstatus`:

```rust
pub type MdConsensus = Consensus<MdConsensusRouterStatus>;
pub struct Consensus<RS> { .. }
impl<RS: ParseRouterStatus> Consensus<RS> {
    pub fn parse(s: &str) -> Result<(&str, &str, UncheckedConsensus<RS>)>;
}
impl<RS> Consensus<RS> {
    pub fn lifetime(&self) -> &Lifetime;
    pub fn relays(&self) -> &[RS];
    pub fn bandwidth_weights(&self) -> &NetParams<i32>;
    pub fn params(&self) -> &NetParams<i32>;
}
pub struct Lifetime { .. }
impl Lifetime {
    pub fn new(valid_after: std::time::SystemTime,
               fresh_until: std::time::SystemTime,
               valid_until: std::time::SystemTime) -> crate::Result<Self>;
    pub fn valid_after(&self) -> std::time::SystemTime;
    pub fn fresh_until(&self) -> std::time::SystemTime;
    pub fn valid_until(&self) -> std::time::SystemTime;
}
pub struct NetParams<T> { .. }
impl<T> NetParams<T> {
    pub fn get<A: AsRef<str>>(&self, v: A) -> Option<&T>;
    pub fn iter(&self) -> impl Iterator<Item = (&String, &T)>;
    pub fn set(&mut self, k: String, v: T);
}
pub trait RouterStatus {
    type DocumentDigest;
    fn rsa_identity(&self) -> &tor_llcrypto::pk::rsa::RsaIdentity;
    fn doc_digest(&self) -> &Self::DocumentDigest;
}
pub struct MdConsensusRouterStatus { .. }
impl MdConsensusRouterStatus {
    pub fn nickname(&self) -> &String;
    pub fn addrs(&self) -> &[std::net::SocketAddr];
    pub fn orport_addrs(&self) -> impl Iterator<Item = &std::net::SocketAddr>;
    pub fn flags(&self) -> &RelayFlags;
    pub fn version(&self) -> &Option<String>;
    pub fn protovers(&self) -> &tor_protover::Protocols;
    pub fn weight(&self) -> &RelayWeight;
    pub fn md_digest(&self) -> &[u8; 32];
    pub fn ed25519_id_is_usable(&self) -> bool;
    pub fn is_flagged_exit(&self) -> bool;
    pub fn is_flagged_guard(&self) -> bool;
    pub fn is_flagged_bad_exit(&self) -> bool;
    pub fn is_flagged_v2dir(&self) -> bool;
}
pub struct RelayFlags: u16;                   // bitflags, exactly twelve constants:
                                              // AUTHORITY, BAD_EXIT, EXIT, FAST, GUARD, HSDIR,
                                              // NO_ED_CONSENSUS, STABLE, STALE_DESC, RUNNING,
                                              // VALID, V2DIR
pub enum RelayWeight { Unmeasured(u32), Measured(u32) }
impl RelayWeight { pub fn is_measured(&self) -> bool; pub fn is_nonzero(&self) -> bool; }
```

`parse` returns the digest-covered slices of the document as its first two
`&str`. The third element is a doubly-wrapped consensus:

```rust
pub type UncheckedConsensus<RS> = tor_checkable::TimerangeBound<UnvalidatedConsensus<RS>>;
pub struct UnvalidatedConsensus<RS> { .. }
impl<RS> tor_checkable::ExternallySigned<Consensus<RS>> for UnvalidatedConsensus<RS> {
    type Key = [tor_netdoc::doc::authcert::AuthCert];
    type KeyHint = Vec<tor_netdoc::doc::authcert::AuthCertKeyIds>;
    type Error = tor_netdoc::Error;
}
```

The order matters and only one order is right: the outer wrapper is the
`Timebound`, and what it yields is an `UnvalidatedConsensus` — **not** a
`SignatureGated`, which is a different wrapper used by `authcert` below. A
consensus is reached in tests as

```rust
let (_, _, unchecked) = MdConsensus::parse(&text)?;
let consensus: MdConsensus = unchecked
    .dangerously_assume_timely()              // Timebound  -> UnvalidatedConsensus
    .dangerously_assume_wellsigned();         // ExternallySigned -> Consensus
```

`UnvalidatedConsensus` must therefore be `pub` and must implement
`ExternallySigned<Consensus<RS>>`. This is the most load-bearing statement in
this document: the helper that runs those two lines lives in the driver's
shared module, which sixteen of this step's twenty-three group targets compile,
so getting this wrong costs those sixteen groups at once rather than one.

Observables: the three lifetime instants; relays appear sorted by RSA identity
with the nickname, OR addresses, and `Option` version from the `r` and `v`
lines (`version()` is the `v` line's argument **verbatim** — `v Tor 0.4.6.5`
yields `Some("Tor 0.4.6.5")`, product token included, not the bare `0.4.6.5`
that `TorVersion`'s own `Display` writes); the `m` line supplies a 32-byte microdescriptor digest; the `s` line
decodes into `RelayFlags`; the `w` line supplies `RelayWeight::Measured` when
`Unmeasured=0` is absent; footer `bandwidth-weights` and header `params` are
keyed lookups; `pr` lines parse into `Protocols` per relay; a consensus with a
bad `network-status-version`, an out-of-order relay, or a missing signature is
an `Err`.

`tor_netdoc::Error` is the crate's one public error type and every fallible
entry point in this document returns it, but **no group in this step asserts
which variant it is, or what it says**. What is checked is accept versus
reject.

### `mdparse` — microdescriptor parsing (`tor-netdoc`)

`tor_netdoc::doc::microdesc`:

```rust
pub struct Microdesc { .. }                   // Clone, Debug
impl Microdesc {
    pub fn parse(s: &str) -> Result<Microdesc>;
    pub fn digest(&self) -> &[u8; 32];
    pub fn ntor_key(&self) -> &tor_llcrypto::pk::curve25519::PublicKey;
    pub fn ed25519_id(&self) -> &tor_llcrypto::pk::ed25519::Ed25519Identity;
    pub fn ipv4_policy(&self) -> &std::sync::Arc<tor_netdoc::types::policy::PortPolicy>;
    pub fn ipv6_policy(&self) -> &Arc<PortPolicy>;
    pub fn family(&self) -> &tor_netdoc::types::family::RelayFamily;
}
pub struct MicrodescReader<'a> { .. }         // Iterator<Item = Result<AnnotatedMicrodesc>>
impl<'a> MicrodescReader<'a> {
    pub fn new(s: &'a str, allow: tor_netdoc::AllowAnnotations) -> Self;
}
pub struct AnnotatedMicrodesc { .. }
impl AnnotatedMicrodesc {
    pub fn into_microdesc(self) -> Microdesc;
    pub fn md(&self) -> &Microdesc;
}
```

The crate's own error type is what these reads report through, and it carries
two variants this step's rejections are expected to use: `MissingToken(&'static
str)` — a required keyword was not found, and the payload is that keyword — and
`WrongStartingToken(String, _)` — the document's first item was a different
keyword, which the payload carries (the second field is the position, and
nothing reads it).

`tor_netdoc::AllowAnnotations` is `{ AnnotationsAllowed, AnnotationsNotAllowed }`.
Both variants are passed to `MicrodescReader::new`; neither bundle fixture
carries annotations, so both readings succeed and no group observes an
annotation's contents.

`Microdesc` must be **`Clone`**: `netdirsel` hands the same parsed
microdescriptor to a `PartialNetDir` by value after keeping its own copy
(`partial.add_microdesc(mds[0].clone())`), so a `Microdesc` that owns its key
material without deriving `Clone` costs that whole group at compile time.

Observables: the ntor and Ed25519 keys of `md-exity.txt`; `digest()` equals
the SHA-256 of the document text; the `p` line gives the IPv4 port policy and
`p6` the IPv6 one, with `reject 1-65535` as the IPv6 default; `family()` keeps
the `$`-prefixed identities from the `family` line; `MicrodescReader` yields
the bundle's microdescriptors in document order; a microdescriptor with a
wrong-length onion key, and one whose only `id` line names some other key
type, are each an `Err` — the rejection is checked, the error variant is not.

### `rdparse` — router descriptor structure (`tor-netdoc`)

`tor_netdoc::doc::routerdesc`:

```rust
pub struct RouterDesc { .. }
impl RouterDesc { pub fn parse(s: &str) -> Result<UncheckedRouterDesc>; }
```

`tor_netdoc::Error` must derive `Debug` and `PartialEq`, as the crate's error
type does upstream: this group compares two errors for **inequality** and
never names a variant of either.

Observables: only rejections, and how they are told apart. An identity
certificate placed before the `router` line, a `router` line with fewer than
its five arguments, and a descriptor with no `identity-ed25519` at all are
each an `Err`, and the three errors are pairwise different from one another.
A parser that answers one blanket error for every malformed descriptor does
not pass; which variants it answers with, and what they say, are out of scope.
This tag exposes no field accessors on `RouterDesc`, so no group asserts on
its contents.

### `authcert` — authority certificates (`tor-netdoc`, `tor-checkable`)

`tor_netdoc::doc::authcert`:

```rust
pub struct AuthCert { .. }
impl AuthCert {
    pub fn parse(s: &str) -> Result<UncheckedAuthCert>;
    pub fn parse_multiple(s: &str) -> impl Iterator<Item = Result<UncheckedAuthCert>> + '_;
    pub fn signing_key(&self) -> &tor_llcrypto::pk::rsa::PublicKey;
    pub fn key_ids(&self) -> &AuthCertKeyIds;
    pub fn id_fingerprint(&self) -> &tor_llcrypto::pk::rsa::RsaIdentity;
    pub fn sk_fingerprint(&self) -> &RsaIdentity;
    pub fn published(&self) -> std::time::SystemTime;
    pub fn expires(&self) -> std::time::SystemTime;
    pub fn is_expired_at(&self, when: std::time::SystemTime) -> bool;
}
pub struct AuthCertKeyIds { pub id_fingerprint: RsaIdentity, pub sk_fingerprint: RsaIdentity }
pub struct UncheckedAuthCert { .. }           // SignatureGated<TimerangeBound<AuthCert>>
```

Observables: `authcert.txt` parses and reports the
two 20-byte fingerprints and the two instants; `sk_fingerprint()` equals
`signing_key().to_rsa_identity()` (see the `tor-llcrypto` block above — the
SHA-1 of the key's DER encoding), and the two fingerprints differ from each
other; validity runs from `published`
to `expires`, with `TimeValidityError::NotYetValid` before and `Expired`
after — these two `TimeValidityError` variants **are** checked, and each
carries the `Duration` by which the check missed; a certificate with a
fingerprint that does not match its key, or with an unknown
`dir-key-certificate-version`, is an `Err`, checked as a rejection with no
`tor_netdoc::Error` variant named; `parse_multiple` over two concatenated
certificates yields two results.

### `ndargs` — netdoc argument types (`tor-netdoc`)

`tor_netdoc::types::policy`:

```rust
pub struct PortRange { pub lo: u16, pub hi: u16 }   // FromStr, Display
impl PortRange {
    pub fn new_all() -> Self;
    pub fn new(lo: u16, hi: u16) -> Option<Self>;
    pub fn contains(&self, port: u16) -> bool;
    pub fn is_all(&self) -> bool;
}
pub struct PortPolicy { .. }                  // FromStr, Display
impl PortPolicy {
    pub fn new_reject_all() -> Self;
    pub fn allows_port(&self, port: u16) -> bool;
    pub fn intern(self) -> std::sync::Arc<Self>;
}
pub struct AddrPolicy { .. }
impl AddrPolicy {
    pub fn new() -> Self;
    pub fn push(&mut self, kind: RuleKind, pattern: AddrPortPattern);
    pub fn allows(&self, addr: &std::net::IpAddr, port: u16) -> Option<RuleKind>;
    pub fn allows_sockaddr(&self, addr: &std::net::SocketAddr) -> Option<RuleKind>;
}
pub enum RuleKind { Accept, Reject }
pub struct AddrPortPattern { .. }             // FromStr, Display
impl AddrPortPattern {
    pub fn matches(&self, addr: &IpAddr, port: u16) -> bool;
    pub fn matches_sockaddr(&self, addr: &SocketAddr) -> bool;
}
pub enum PolicyError { .. }                   // InvalidPort, InvalidRange, InvalidPolicy, ..
```

`tor_netdoc::types::version::TorVersion` — `FromStr`, `Display`, `Ord`.
`tor_netdoc::types::family::RelayFamily` — `FromStr`, with
`new()`, `contains(&RsaIdentity) -> bool` and
`members() -> impl Iterator<Item = &RsaIdentity>`; it is also `Default`, and
the default is the empty family — the same value `new()` builds, so
`RelayFamily::default().members().count()` and
`RelayFamily::new().members().count()` agree.

`PortRange`'s `FromStr` accepts either a full range `"lo-hi"` or a **bare
port** `"9001"`, which is the one-port range `9001-9001`; its `Display` writes
a one-port range as the bare number and any wider range as `"lo-hi"`.
`TorVersion`'s `FromStr` accepts a release series of **at least three**
dot-separated numeric components, each fitting in a `u8`, followed by an
optional fourth patch level — a three-component version such as `"0.4.6"` has
patch level 0 — and then an optional status suffix, `-alpha`, `-beta` or `-rc`
(a bare `-dev` on its own is a stable version carrying the dev tag). Fewer than
three components, a component that is not a number in `u8` range, a second
suffix that is not literally `-dev`, and any further dash-separated part are
all refused. Its `Display` always writes all four components in full,
then the status suffix, then `-dev`: `"0.4.6.5"`, `"0.4.6.5-alpha"`,
`"0.4.6.5-alpha-dev"`. Its `Ord` compares major, minor, micro and patch level
first and the release status only afterwards, so every alpha of a series
precedes its rc, which precedes the release itself, and `-dev` sorts after the
same status without it.

Observables: `PortRange::new` is inclusive, and returns `None` both for
`hi < lo` **and whenever either endpoint is 0** — port 0 is not a port, so
`new(0, 10)` is `None` and `new_all()` is `1..=65535`;
`"reject 1-100"` is stored as the complementary accept policy; adjacent accept
ranges merge and `Display` reproduces them; a policy that allows nothing
renders as `"reject 1-65535"` rather than as an empty accept list, which is
also what `new_reject_all()` prints; `AddrPolicy::allows` returns the first
matching rule's kind and `None` when nothing matches.
`AddrPortPattern`'s `Display` is the pattern's own text form: the address with
its prefix length, then `:`, then the port range in `PortRange`'s rendering --
so `"192.0.2.0/24:80-443"` prints back as itself, and the suite compares that
rendering.

**Which `PolicyError` a malformed policy gets is checked**, and it is decided by
the defect, not by the directive:

- a directive word that is neither `accept` nor `reject` (e.g. `"allow 1-10"`)
  is `InvalidPolicy`;
- a port token that is not a decimal number in `1..=65535` (e.g.
  `"accept 80,ssh"`) is `InvalidPort`;
- a range whose end precedes its start, or that names port 0 as an endpoint
  (e.g. `"accept 700-600"`) is `InvalidRange`;
- accept ranges that are **not listed in strictly increasing, non-overlapping
  order** are `InvalidPolicy`: a range that begins at or before the end of the
  range before it (e.g. `"accept 100-200,150-300"`) is refused, not merged.
  Adjacent ranges — the next one beginning exactly one past the previous end —
  are the exception, and *are* merged into one range (`"accept 100-199,200-299"`
  prints as `"accept 100-299"`).

`RelayFamily`'s `FromStr` reads each space-separated token as a *long
identity*: an optional leading `$`, then exactly **40 hex digits**, optionally
followed by `=name` or `~name`, which are discarded. A token that does not fit
that shape — a nickname, or a hex run of the wrong length — is dropped
silently, and no error is returned. So `$AAAA…AA` and a bare, unprefixed
`bbbb…bb` are **both kept**, in the order they appear; only the nickname is
discarded. `members()` yields them in that order and `contains()` answers over
them.

### `consdiff` — consensus diffs (`tor-consdiff`)

```rust
pub fn looks_like_diff(s: &str) -> bool;
pub fn apply_diff<'a>(input: &'a str, diff: &'a str, check_digest_in: Option<[u8; 32]>)
    -> Result<DiffResult<'a>>;
pub struct DiffResult<'a> { .. }              // Display
impl<'a> DiffResult<'a> { pub fn check_digest(&self) -> Result<()>; }
pub enum Error { .. }                         // BadDiff, CantApply, DiffHeaderNotFound, ..
```

Observables: `looks_like_diff` is true exactly for input whose first line is
the `network-status-diff-version 1` header; a `d` command deletes an inclusive
line range; a `c` command replaces a range with the given lines; an `a`
command inserts after a line and `0a` prepends. **A `c` or an `a` command's
body ends at a line holding a single `.`**, and that dot line is a terminator,
not one of the replacement lines: `"2,3c\nreplacement\n.\n"` replaces lines 2
and 3 with the one line `replacement`, while `"2,3c\nreplacement\n"` — a body
with no terminating dot — is an `Err`. **Commands address the document from
the end backwards**: the highest line number comes first in the body, so
`"5a\nlast\n.\n0a\nfirst\n.\n"` applies and `"2d\n4d\n"` — the same two
commands in ascending order — is an `Err`. A command naming a line past the
end of the input, or a range whose end precedes its start, is an `Err`; the
`hash` line names the input and output digests, `apply_diff` with a mismatched
`check_digest_in` fails, and `check_digest()` succeeds when the result's
SHA3-256 matches the `hash` line and fails when it does not.

The `Error` variants are listed above because they are part of your public
API, **not** because anything matches on them: every failure in this group is
checked as `is_err()`, and `Error` need not even be nameable for the group to
pass.

### `dirreq` — directory client requests (`tor-dirclient`)

`tor_dirclient::request`:

```rust
pub trait Requestable {
    fn make_request(&self) -> Result<http::Request<()>>;
    fn partial_docs_ok(&self) -> bool;
    fn max_response_len(&self) -> usize;
}
pub struct ConsensusRequest { .. }            // Default
impl ConsensusRequest {
    pub fn new(flavor: tor_netdoc::doc::netstatus::ConsensusFlavor) -> Self;
    pub fn push_authority_id(&mut self, id: tor_llcrypto::pk::rsa::RsaIdentity);
    pub fn push_old_consensus_digest(&mut self, d: [u8; 32]);
    pub fn set_last_consensus_date(&mut self, when: std::time::SystemTime);
    pub fn old_consensus_digests(&self) -> impl Iterator<Item = &[u8; 32]>;
    pub fn authority_ids(&self) -> impl Iterator<Item = &RsaIdentity>;
    pub fn last_consensus_date(&self) -> Option<SystemTime>;
}
pub struct AuthCertRequest { .. }
impl AuthCertRequest {
    pub fn new() -> Self;
    pub fn push(&mut self, ids: tor_netdoc::doc::authcert::AuthCertKeyIds);
    pub fn keys(&self) -> impl Iterator<Item = &AuthCertKeyIds>;
}
pub struct MicrodescRequest { .. }
impl MicrodescRequest {
    pub fn new() -> Self;
    pub fn push(&mut self, d: [u8; 32]);
    pub fn digests(&self) -> impl Iterator<Item = &[u8; 32]>;
}
pub struct RouterDescRequest { .. }
impl RouterDescRequest {
    pub fn all() -> Self;
    pub fn new() -> Self;
    pub fn push(&mut self, d: [u8; 20]);
    pub fn digests(&self) -> impl Iterator<Item = &[u8; 20]>;
}
```

`ConsensusFlavor` is `{ Microdesc, Ns }` with
`name(&self) -> &'static str` and
`from_opt_name(name: Option<&str>) -> Result<Self>`.

**Every URI this client builds ends in `.z`.** The suffix is appended last,
after any digest list, and it is what asks the server for the compressed form
of the document (dir-spec §6.1, and `SPEC_NOTES.md` §4.5). A path that is
otherwise perfect but lacks the `.z` fails. Identities and digests are always
sorted before they are joined.

Observables — the method, URI, headers and `Requestable` answers of the
*built* `http::Request<()>`. Nothing is sent anywhere and no runtime is
involved.

- **Consensus.** `GET /tor/status-vote/current/consensus.z` for
  `ConsensusFlavor::Ns` and `…/consensus-microdesc.z` for `Microdesc`, which
  is also what `ConsensusRequest::default()` builds. Pushed authority
  identities become one further path element of `+`-joined lowercase hex
  fingerprints in sorted order, before the suffix:
  `/tor/status-vote/current/consensus-microdesc/<hex>+<hex>.z`.
  `if-modified-since` is the RFC 1123 HTTP-date
  (`Thu, 24 Jun 2021 00:00:00 GMT`). `x-or-diff-from-consensus` carries the
  sorted lowercase hex digests joined by **a comma and a space** — the
  separator is `", "`, not `+` — and those digests never appear in the path.
  Neither header exists until its setter has been called.
  `partial_docs_ok()` is `false`: one consensus is all or nothing.
- **Microdescriptors.** `GET /tor/micro/d/<b64>-<b64>.z`. Each 32-byte digest
  is **standard-alphabet unpadded base64** — `base64::STANDARD_NO_PAD`, with
  `+` and `/` and no `=` padding — and *not* the URL-safe alphabet. `-` is the
  separator precisely because `+` and `/` can occur inside a digest
  (dir-spec §4.3). Digests are sorted as raw bytes before encoding.
  `partial_docs_ok()` is true exactly when more than one digest has been
  pushed; `max_response_len()` is 8 KiB per pushed digest.
- **Authority certificates.** `GET /tor/keys/fp-sk/<id>-<sk>+<id>-<sk>.z`,
  each fingerprint lowercase hex, identity and signing key joined by `-` and
  the pairs joined by `+`. The pairs are sorted by `AuthCertKeyIds`, which
  orders on the identity fingerprint first and the signing-key fingerprint
  second. `partial_docs_ok()` is true for more than one pair;
  `max_response_len()` is 16 KiB per pair.
- **Router descriptors.** `GET /tor/server/d/<hex>+<hex>.z` with the 20-byte
  digests sorted, or `GET /tor/server/all.z` from `RouterDescRequest::all()`.
  Once a request is asking for everything, `push` adds nothing and
  `digests().count()` stays 0. `partial_docs_ok()` is true for more than one
  digest, and for `all()`.
- **Every request.** An `accept-encoding` header is always present and its
  value **begins with `deflate, identity`**. Further encodings may follow if
  your build supports them; the group asserts the prefix, not the whole value.

### `netdirsel` — assembling a network directory (`tor-netdir`)

`tor_netdir`:

```rust
pub struct PartialNetDir { .. }
impl PartialNetDir {
    pub fn new(consensus: tor_netdoc::doc::netstatus::MdConsensus,
               replacement_params: Option<&tor_netdoc::doc::netstatus::NetParams<i32>>) -> Self;
    pub fn lifetime(&self) -> &netstatus::Lifetime;
    pub fn fill_from_previous_netdir<'a>(&mut self, prev: &'a NetDir) -> Vec<&'a [u8; 32]>;
    pub fn have_enough_paths(&self) -> bool;
    pub fn unwrap_if_sufficient(self) -> std::result::Result<NetDir, PartialNetDir>;
}
pub struct NetDir { .. }
impl NetDir {
    pub fn lifetime(&self) -> &netstatus::Lifetime;
    pub fn relays(&self) -> impl Iterator<Item = Relay<'_>>;
    pub fn params(&self) -> &params::NetParameters;
    pub fn pick_relay<'a, R: rand::Rng, P: Fn(&Relay<'a>) -> bool>(
        &'a self, rng: &mut R, role: WeightRole, usable: P) -> Option<Relay<'a>>;
}
pub trait MdReceiver {
    fn missing_microdescs(&self) -> Box<dyn Iterator<Item = &[u8;32]> + '_>;
    fn add_microdesc(&mut self, md: tor_netdoc::doc::microdesc::Microdesc) -> bool;
    fn n_missing(&self) -> usize;
}
pub struct Relay<'a> { .. }
impl<'a> Relay<'a> {
    pub fn id(&self) -> &tor_llcrypto::pk::ed25519::Ed25519Identity;
    pub fn rsa_id(&self) -> &tor_llcrypto::pk::rsa::RsaIdentity;
    pub fn same_relay<'b>(&self, other: &Relay<'b>) -> bool;
    pub fn supports_exit_port_ipv4(&self, port: u16) -> bool;
    pub fn supports_exit_port_ipv6(&self, port: u16) -> bool;
    pub fn is_dir_cache(&self) -> bool;
    pub fn in_same_family<'b>(&self, other: &Relay<'b>) -> bool;
    pub fn ipv4_policy(&self) -> &std::sync::Arc<PortPolicy>;
    pub fn ipv6_policy(&self) -> &Arc<PortPolicy>;
}
```

`Relay` also implements `tor_linkspec::ChanTarget`
(`addrs() -> &[SocketAddr]`, `ed_identity() -> &Ed25519Identity`,
`rsa_identity() -> &RsaIdentity`) and `tor_linkspec::CircTarget`
(`ntor_onion_key() -> &curve25519::PublicKey`, `protovers() -> &Protocols`).

Observables: a directory built from the consensus alone reports all four
microdescriptor digests as missing and is not usable; adding all four makes
`n_missing() == 0` and `unwrap_if_sufficient()` succeed, and `relays()` then
lists every consensus relay; `have_enough_paths()` is **not** a count of relays
but the product of the *weighted* fractions of relays usable in each of the
three path roles, compared against the `min_circuit_path_threshold` consensus
parameter (60% by default) — so a directory that is three-quarters complete by
relay count is still not usable when the one missing microdescriptor belongs to
the only relay carrying a role's bandwidth weight, and `unwrap_if_sufficient()`
then hands the `PartialNetDir` back with `missing_microdescs()` naming what is
still absent; a relay's `ChanTarget`/`CircTarget` data comes
from joining its router status to its microdescriptor;
`supports_exit_port_ipv4`/`_ipv6` follow the microdescriptor's `p`/`p6` lines;
`in_same_family` answers for a relay against itself as well as against another
relay — **a relay is in its own family**, the way `same_relay` is true of it —
and for two distinct relays it is true only when the relation is *mutual*: each
one's microdescriptor family must name the other's RSA identity, so a one-sided
claim is `false` and the relation is symmetric;
`fill_from_previous_netdir` carries microdescriptors forward from an earlier
`NetDir` and returns the digests it filled.

### `netparams` — consensus parameters (`tor-netdir`, `tor-units`)

`tor_netdir::params::NetParameters` has these public fields, all
`#[non_exhaustive]`-safe to read:

```rust
pub bw_weight_scale: tor_units::BoundedInt32<0, { i32::MAX }>,
pub circuit_window: BoundedInt32<100, 1000>,
pub circuit_priority_half_life: tor_units::IntegerMilliseconds<BoundedInt32<1, { i32::MAX }>>,
pub extend_by_ed25519_id: BoundedInt32<0, 1>,
pub min_circuit_path_threshold: tor_units::Percentage<BoundedInt32<25, 95>>,
pub send_me_accept_min_version: tor_units::SendMeVersion,
pub send_me_emit_min_version: SendMeVersion,
```

`tor_units`: `BoundedInt32::get(&self) -> i32`, `Percentage::as_percent(self)
-> T`, the bound type itself — on `min_circuit_path_threshold` above that is a
`BoundedInt32<25, 95>`, so a percentage is read as `.as_percent().get()`, never
as a bare number — `Percentage::as_fraction(self) -> f64`,
`SendMeVersion::get(&self) -> u8`,
`impl TryFrom<IntegerMilliseconds<..>> for std::time::Duration`, and
`impl From<BoundedInt32<..>> for bool`.

Observables: the defaults — `bwweightscale` 10000, `circwindow` 1000,
half-life 30000 ms, `ExtendByEd25519ID` 0, `min_paths_for_circs_pct` 60 %,
both SENDME versions 0; consensus `params` values replace them, keyed by the
consensus spelling (`circwindow`, `sendme_emit_min_version`,
`CircuitPriorityHalflifeMsec`, `ExtendByEd25519ID`,
`min_paths_for_circs_pct`, `bwweightscale`, `sendme_accept_min_version`);
out-of-range values are clamped to the bound rather than rejected;
a parameter the client does not recognize is visible through
`MdConsensus::params().get()` but ignored by `NetDir::params()`; the
`replacement_params` argument to `PartialNetDir::new` beats the consensus.

The override map the group passes in is built as
`tor_netdoc::doc::netstatus::NetParams::<i32>::default()` and filled with
`set`, so `NetParams<T>` must also implement `Default`, as the empty map.

### `pathsel` — weighted path selection (`tor-circmgr`, `tor-netdir`)

`tor_netdir`:

```rust
pub enum WeightRole { Guard, Middle, Exit, BeginDir, Unweighted }
```

`tor_netdir::fallback`:

```rust
pub struct FallbackDir { .. }                 // impl tor_linkspec::ChanTarget
impl FallbackDir { pub fn builder() -> FallbackDirBuilder; }
pub struct FallbackDirBuilder { .. }
impl FallbackDirBuilder {
    pub fn new() -> Self;
    pub fn rsa_identity(&mut self, rsa_identity: RsaIdentity) -> &mut Self;
    pub fn ed_identity(&mut self, ed_identity: Ed25519Identity) -> &mut Self;
    pub fn orport(&mut self, orport: std::net::SocketAddr) -> &mut Self;
    pub fn build(&self) -> Result<FallbackDir>;
}
```

`orport` **appends**: it may be called more than once and each call adds one
address to the list. There is no plural `orports(Vec<SocketAddr>)` setter at
this tag, and adding one would not be checked. `build()` fails if either
identity is missing or if no ORPort was ever pushed.

`tor_circmgr`:

```rust
pub enum DirInfo<'a> { Nothing, Fallbacks(&'a [tor_netdir::fallback::FallbackDir]), Directory(&'a NetDir) }
pub struct TargetPort { .. }
impl TargetPort {
    pub fn ipv4(port: u16) -> TargetPort;
    pub fn ipv6(port: u16) -> TargetPort;
    pub fn is_supported_by(&self, r: &tor_netdir::Relay<'_>) -> bool;
}
pub enum Error { NoPath(String), NoRelays(String), NeedConsensus, .. }
pub mod path {
    pub struct TorPath<'a> { .. }
    impl<'a> TorPath<'a> {
        pub fn new_one_hop(relay: Relay<'a>) -> Self;
        pub fn new_fallback_one_hop(fallback_dir: &'a FallbackDir) -> Self;
        pub fn new_multihop(relays: impl IntoIterator<Item = Relay<'a>>) -> Self;
        pub fn len(&self) -> usize;
    }
    pub mod exitpath {
        pub struct ExitPathBuilder<'a> { .. }
        impl<'a> ExitPathBuilder<'a> {
            pub fn from_target_ports(wantports: impl IntoIterator<Item = TargetPort>) -> Self;
            pub fn from_chosen_exit(exit_relay: Relay<'a>) -> Self;
            pub fn pick_path<R: rand::Rng>(&self, rng: &mut R, netdir: DirInfo<'a>) -> Result<TorPath<'a>>;
        }
    }
    pub mod dirpath {
        pub struct DirPathBuilder {}          // also impl Default
        impl DirPathBuilder {
            pub fn new() -> Self;
            pub fn pick_path<'a, R: rand::Rng>(&self, rng: &mut R, netdir: DirInfo<'a>) -> Result<TorPath<'a>>;
        }
    }
}
```

`TorPath` exposes only `len()` at this tag, so path *composition* is checked by
hop count and error variant, and relay choice is checked through
`NetDir::pick_relay` instead.

Observables, all with a seeded `rand::rngs::StdRng`: over 200 draws each,
`WeightRole::Guard` only ever returns the relay with a nonzero `Wgg`,
`Middle` the one with a nonzero `Wmm`, and `Exit` the one with a nonzero
`Wee`; `BeginDir` is weighted by the **`Wb*`** column of the
`bandwidth-weights` line, chosen by the relay's flags — `Wbg` for a
Guard-only relay, `Wbd` for one flagged both Guard and Exit, `Wbm` for a plain
middle relay and `Wbe` for an Exit-only relay. (`Wgb`, `Wmb`, `Web` and `Wdb`
are a different set of weights, for how a *directory* relay is used in the
other roles, and are not what this role reads.) The fixture sets
`Wbg = Wbd = 10000` and `Wbm = Wbe = 0`, so the only two relays with any
BeginDir weight are drawn in proportion to their bandwidth, 1:4 over 4000
draws;
`WeightRole::Unweighted` draws every relay in proportion to its raw
consensus bandwidth (0.1/0.2/0.3/0.4 over 4000 draws) and honours the
`usable` predicate, returning `None` when nothing is usable; a relay whose
weight for a role is zero is never chosen for that role even when it
otherwise qualifies; `ExitPathBuilder::pick_path` gives `len() == 3`, gives
`Error::NoRelays` when no relay supports the wanted port, and
`Error::NeedConsensus` for `DirInfo::Fallbacks`; `DirPathBuilder::pick_path`
gives `len() == 1` from a directory and from a fallback list, and
`Error::NoRelays` from an empty fallback list. `DirPathBuilder::default()` and
`DirPathBuilder::new()` are interchangeable and the group uses both.

### `dirmgrcfg` — directory manager configuration (`tor-dirmgr`)

```rust
pub struct Authority { .. }
impl Authority {
    pub fn builder() -> AuthorityBuilder;
    pub fn v3ident(&self) -> &RsaIdentity;
    pub fn matches_cert(&self, cert: &tor_netdoc::doc::authcert::AuthCert) -> bool;
    pub fn matches_keyid(&self, id: &tor_netdoc::doc::authcert::AuthCertKeyIds) -> bool;
}
pub struct AuthorityBuilder { .. }            // new, name(&str), v3ident(RsaIdentity), build()
pub struct NetworkConfig { .. }               // Default
impl NetworkConfig {
    pub fn builder() -> NetworkConfigBuilder;
    pub fn authorities(&self) -> &[Authority];
    pub fn fallbacks(&self) -> &[tor_netdir::fallback::FallbackDir];
}
pub struct NetworkConfigBuilder { .. }        // new, fallback(FallbackDir), authority(Authority), build()
pub struct DownloadScheduleConfig { .. }      // Default, Clone
impl DownloadScheduleConfig {
    pub fn builder() -> DownloadScheduleConfigBuilder;
    pub fn microdesc_parallelism(&self) -> usize;   // clamped up to at least 1
}
pub struct DownloadScheduleConfigBuilder { .. }
impl DownloadScheduleConfigBuilder {
    pub fn new() -> Self;
    pub fn microdesc_parallelism(&mut self, parallelism: u8) -> &mut Self;
    pub fn build(&self) -> Result<DownloadScheduleConfig>;
}
pub struct DirMgrConfig { .. }
impl DirMgrConfig {
    pub fn builder() -> DirMgrConfigBuilder;
    pub fn authorities(&self) -> &[Authority];
    pub fn fallbacks(&self) -> &[FallbackDir];
    pub fn override_net_params(&self) -> &tor_netdoc::doc::netstatus::NetParams<i32>;
    pub fn schedule(&self) -> &DownloadScheduleConfig;
    pub fn microdesc_parallelism(&self) -> usize;
}
pub struct DirMgrConfigBuilder { .. }
impl DirMgrConfigBuilder {
    pub fn new() -> Self;
    pub fn network_config(&mut self, config: NetworkConfig) -> &mut Self;
    pub fn schedule_config(&mut self, schedule: DownloadScheduleConfig) -> &mut Self;
    pub fn cache_path(&mut self, path: &std::path::Path) -> &mut Self;
    pub fn override_net_param(&mut self, param: String, value: i32) -> &mut Self;
    pub fn build(&self) -> Result<DirMgrConfig>;
}
pub enum Error { BadNetworkConfig(&'static str), .. }
```

Builder `build()` methods return an error type that downcasts to
`tor_dirmgr::Error` through `anyhow::Error`.

Observables: `DirMgrConfigBuilder::build()` without a cache path fails with
`Error::BadNetworkConfig`, and succeeds otherwise with
`microdesc_parallelism() == 4` and no override parameters; naming any
authority opts out of the built-in fallback list, so such a configuration
fails to build until a fallback is supplied, while a fallback-only network
keeps the nine default authorities; `matches_cert`/`matches_keyid` compare the
authority's v3 identity against `authcert.txt`'s identity fingerprint and not
its signing-key fingerprint; the built-in `NetworkConfig::default()` has nine
distinct authorities, among them these three, whose v3 identities are compared
literally as the uppercase hex of `v3ident()`:

| authority | v3 identity |
|---|---|
| `moria1` | `D586D18309DED4CD6D57C18FDB97EFA96D330566` |
| `tor26` | `14C131DFC5C6F93646BE72FA1401C02A8DF2E8B4` |
| `dizum` | `E8A9C45EDE6D711294FADF8E7951F4DE6CA56B58` |

The other six are not checked by value, only counted and required to be
distinct. The same default network carries **at least fifty** fallbacks, each
with a 20-byte RSA identity, a 32-byte Ed25519 identity and an ORPort;
`DownloadScheduleConfig::builder().microdesc_parallelism(n)` round-trips
through `build()`, and 0 comes back as 1; `override_net_param("circwindow", 250)` reaches
`PartialNetDir::new(consensus, Some(cfg.override_net_params()))` as
`circuit_window == 250`; `microdesc_parallelism` is clamped to at least 1.

`DirMgr` itself is out of scope: at this tag it has no public constructor that
can run with no network and an empty cache, and `CacheUsage`, `RetryConfig`
and `RetryDelay` are not nameable from outside the crate.

### `socks` — the SOCKS proxy protocol (`tor-socksproto`)

```rust
pub struct SocksHandshake { .. }              // Default
impl SocksHandshake {
    pub fn new() -> Self;
    pub fn handshake(&mut self, input: &[u8]) -> Result<Action>;
    pub fn finished(&self) -> bool;
    pub fn into_request(self) -> Option<SocksRequest>;
}
pub struct Action { pub drain: usize, pub reply: Vec<u8>, pub finished: bool }
pub struct SocksRequest { .. }
impl SocksRequest {
    pub fn version(&self) -> u8;
    pub fn command(&self) -> SocksCmd;
    pub fn auth(&self) -> &SocksAuth;
    pub fn port(&self) -> u16;
    pub fn addr(&self) -> &SocksAddr;
    pub fn reply(&self, status: SocksStatus, addr: Option<&SocksAddr>) -> Vec<u8>;
}
pub enum SocksAddr { Hostname(SocksHostname), Ip(std::net::IpAddr) }
                                              // Display: the address itself — the
                                              // hostname's text, or the IpAddr's own
                                              // rendering — with no decoration
pub struct SocksHostname(String);             // AsRef<str>, TryFrom<String>
pub enum SocksAuth { NoAuth, Socks4(Vec<u8>), Username(Vec<u8>, Vec<u8>) }
pub struct SocksCmd(u8);                      // impl From<SocksCmd> for u8
                                              // CONNECT=1, BIND=2, UDP_ASSOCIATE=3,
                                              // RESOLVE=0xF0, RESOLVE_PTR=0xF1
pub struct SocksStatus(u8);                   // impl From<SocksStatus> for u8
                                              // SUCCEEDED = 0x00,
                                              // GENERAL_FAILURE = 0x01,
                                              // NOT_ALLOWED = 0x02,
                                              // NETWORK_UNREACHABLE = 0x03,
                                              // HOST_UNREACHABLE = 0x04,
                                              // CONNECTION_REFUSED = 0x05,
                                              // TTL_EXPIRED = 0x06,
                                              // COMMAND_NOT_SUPPORTED = 0x07,
                                              // ADDRTYPE_NOT_SUPPORTED = 0x08
pub enum Error { Truncated, Syntax, NoSupport, BadProtocol(u8), AlreadyFinished, .. }
```

Observables: a SOCKS5 no-auth negotiation (`05 01 00`) replies `05 00` and
drains three bytes, then a CONNECT request by hostname yields
`version() == 5`, `SocksCmd::CONNECT`, `SocksAuth::NoAuth`, the hostname and
the port; when the client offers both method 0 and method 2 the handshake
replies `05 02` and then consumes an RFC 1929 username/password message,
replying `01 00` and reporting `SocksAuth::Username`; address types 1, 3 and 4
are understood and type 2 is `Error::Syntax`; **`SocksAddr` implements
`Display`**, rendering the address itself — the hostname's text, or the
`IpAddr`'s own rendering (`192.0.2.33`, `2001:db8::1`) — with no decoration,
and the group compares that rendering; SOCKS4 and SOCKS4a requests
report `version() == 4`, `SocksAuth::Socks4(userid)`, and for 4a the trailing
hostname after the `0.0.0.x` sentinel; `SocksRequest::reply` emits a 10-byte
SOCKS5 reply with the given status and address, and an 8-byte SOCKS4 reply
whose second byte is 0x5a for `SocksStatus::SUCCEEDED` and 0x5b for any other
status (the group passes `CONNECTION_REFUSED`); a message split
mid-way gives `Error::Truncated` and succeeds when re-fed with the full
prefix; `RESOLVE` and `RESOLVE_PTR` are accepted, `BIND` and `UDP_ASSOCIATE`
are `Error::NoSupport`, port 0 is `Error::Syntax`, a version byte of 6 is
`Error::BadProtocol(6)`, calling `handshake` after completion is
`Error::AlreadyFinished`, and a client offering only method 0x80 is
`Error::NoSupport`.

### `protover` — subprotocol versions (`tor-protover`)

```rust
pub struct ProtoKind(u16);                    // impl From<ProtoKind> for u16, From<u16> for ProtoKind
                                              // Link = 0, LinkAuth = 1, Relay = 2,
                                              // DirCache = 3, HSDir = 4, HSIntro = 5,
                                              // HSRend = 6, Desc = 7, MicroDesc = 8,
                                              // Cons = 9, Padding = 10, FlowCtrl = 11
pub struct Protocols { .. }                   // FromStr<Err = ParseError>, Display, Default
impl Protocols {
    pub fn new() -> Self;
    pub fn supports_known_subver(&self, proto: ProtoKind, ver: u8) -> bool;
    pub fn supports_subver(&self, proto: &str, ver: u8) -> bool;
}
pub enum ParseError { Malformed, OutOfRange, Duplicate, .. }
```

Observables: `"Link=1-5 LinkAuth=3 Relay=1-2,9 Cons=2"` parses and answers
`supports_known_subver`/`supports_subver` correctly for members and
non-members; `ProtoKind`'s `Display` names; an unrecognized protocol name is
kept verbatim and is case-sensitive; an empty version list contributes
nothing to `Display`; `Display` is the canonical consensus form — entries
sorted alphabetically by name, versions ascending, runs collapsed to
`lo-hi` — and is idempotent; a version above 63, a repeated protocol name, a
repeated or overlapping version, and a malformed range each give the matching
`ParseError`.

The version grammar is **stricter than Rust's integer parsing**. A version is
a run of ASCII digits with no leading zero, so `"Link=01"` must be
`ParseError::Malformed` even though `u8::from_str("01")` succeeds; an entry
with no `=`, an unterminated range `"Link=1-"`, a reversed range `"Link=3-1"`
and a non-numeric version are malformed too.

## 3. What is deliberately out of scope

- No test opens a socket, resolves a name, spawns a process, or reads the
  clock for a threshold. The link handshake runs over an in-memory stream; all
  randomness comes from a seeded `StdRng`.
- The TLS implementation and the cryptographic primitives are out of scope;
  only their composition is checked (certificate chains, handshake sequencing,
  digest construction).
- Wall-clock throughput is not measured.
- Onion services are out of scope at every step.
- Relay-side and directory-authority-side behaviour is out of scope; the
  client's parsing and validation of the documents they publish is not.

`instruction.md` §1 describes the whole milestone, and four of its clauses have
**no requirement group at this step**. They are named here so you can budget around
them rather than discover it late:

| Clause of `instruction.md` §1 | Status at this step |
|---|---|
| ntor | Out of scope. No `tor-proto` handshake type appears in this document. `extend2` checks the `CREATE2`/`EXTEND2` *cell* bodies and treats the handshake payload as opaque bytes. |
| layered onion encryption | Out of scope. `relayhdr` checks the relay-cell header and its padding with the recognized and digest fields written as zero; no group derives or applies a layer of circuit cryptography. |
| a circuit manager | Out of scope as a manager. `tor-circmgr` is reached only through `path::{TorPath, exitpath, dirpath}` in `pathsel`; no group constructs a `CircMgr`. |
| a single-shot directory manager with an on-disk cache | Out of scope as a manager. `dirmgrcfg` checks the configuration builders only; no group constructs a `DirMgr` or opens a cache. |

Build them if you want the milestone to hang together — the next steps assume
them — but nothing in §4 requires them.

---

## 4. Requirement groups → interface items

The group names are those in `tests/assets/groups.txt`.

- **Gate** groups add no requirement of their own. Each must pass completely
  or the step is not satisfied, however much of the new work is finished. This
  is the first milestone, so there is no earlier behaviour to carry forward and
  the gate is proof of life only —
  `proofoflife` asserts no value this document assigns and names no item of any
  in-scope crate, only that each of the seventeen crates the driver depends on
  is present and resolves under that name. It has to stop there: a workspace of
  empty stubs must clear this gate, so any assertion here that such a workspace
  could fail would make the gate unpassable instead of confirming the build.
- **Requirement** groups carry this step's new work. Every case in one must
  pass.

| Group | Kind | Items |
|---|---|---|
| `proofoflife` | gate | §0 — no specified value and no item of any in-scope crate; each of the seventeen driver dependencies is reached by `use <crate> as _;` only, which a workspace of empty stubs satisfies |
| `chancmd` | requirement | §2 `chancmd` — `ChanCmd` constants, `is_var_cell`, `is_recognized`, `accepts_circid_val`, `CircId` |
| `cellcodec` | requirement | §2 `cellcodec` — `CELL_DATA_LEN`, `ChanCell`, `ChannelCodec`, `msg::Relay`, `msg::Destroy`, `DestroyReason`; §1 cell fixtures |
| `chanmsg` | requirement | §2 `chanmsg` — `ChanMsg`, `Versions`, `Padding`/`VPadding`, `Certs`, `Netinfo`, `AuthChallenge`, `Authenticate`; shared `tor-bytes` |
| `relayhdr` | requirement | §2 `relayhdr` — `RelayCell`, `RawCellBody`, `msg::Data`; §1 `cell_relay_data.bin` |
| `relaycmd` | requirement | §2 `relaycmd` — `RelayCmd` constants, `accepts_streamid_val`, `is_recognized`, `StreamId` |
| `relaymsg` | requirement | §2 `relaymsg` — `relaycell::msg` bodies (`Begin`, `Data`, `End`, `Connected`, `Sendme`, `Resolved`) |
| `extend2` | requirement | §2 `extend2` — `msg::Extend2`/`Extended2`, `tor_linkspec::LinkSpec`; shared `tor-bytes` |
| `sendmefc` | requirement | §2 `sendmefc` — `msg::Sendme`, tag encoding and round trip |
| `linkhs` | requirement | §2 `linkhs` — `tor_proto` channel handshake over an in-memory stream |
| `edcert` | requirement | §2 `edcert` — `tor_cert` types and constants; `tor_checkable::{Timebound, SelfSigned}`; §1 `edcert_*.bin` |
| `nsparse` | requirement | §2 `nsparse` — `netstatus::{Consensus, MdConsensus, Lifetime, NetParams, RelayFlags, RelayWeight}`, `UncheckedConsensus`, `UnvalidatedConsensus` |
| `mdparse` | requirement | §2 `mdparse` — `microdesc::{Microdesc, MicrodescReader, AnnotatedMicrodesc}`, `AllowAnnotations` |
| `ndargs` | requirement | §2 `ndargs` — `types::policy`, `types::version::TorVersion`, `types::family::RelayFamily` |
| `authcert` | requirement | §2 `authcert` — `doc::authcert::{AuthCert, AuthCertKeyIds}`; `tor_checkable::TimeValidityError` |
| `rdparse` | requirement | §2 `rdparse` — `doc::routerdesc::RouterDesc::parse`; `tor_netdoc::Error: PartialEq` |
| `consdiff` | requirement | §2 `consdiff` — `looks_like_diff`, `apply_diff`, `DiffResult::check_digest` |
| `dirreq` | requirement | §2 `dirreq` — `tor_dirclient::request::{Requestable, ConsensusRequest, MicrodescRequest, AuthCertRequest, RouterDescRequest}`, `ConsensusFlavor` |
| `netdirsel` | requirement | §2 `netdirsel` — `PartialNetDir`, `NetDir`, `Relay`, `MdReceiver`, `WeightRole` |
| `netparams` | requirement | §2 `netparams` — `params::NetParameters`, `tor_units` bounded types, `NetParams::default` |
| `pathsel` | requirement | §2 `pathsel` — `WeightRole`, `fallback::FallbackDir`, `tor_circmgr::path::{TorPath, exitpath, dirpath}` |
| `socks` | requirement | §2 `socks` — `SocksHandshake`, `Action`, `SocksRequest`, `SocksAddr`, `SocksAuth`, `SocksCmd`, `SocksStatus` |
| `dirmgrcfg` | requirement | §2 `dirmgrcfg` — `Authority`, `NetworkConfig`, `DownloadScheduleConfig`, `DirMgrConfig` and their builders |
| `protover` | requirement | §2 `protover` — `ProtoKind`, `Protocols`, `ParseError` |

