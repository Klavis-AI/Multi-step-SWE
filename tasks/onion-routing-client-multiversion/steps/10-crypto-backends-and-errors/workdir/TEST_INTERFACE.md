# Test interface — step 10

Every public item this step's verifier exercises. The checker imports these by
crate and path; a rename or deletion breaks the test. Signatures below are the
ones the driver compiles against — argument order, ownership and return type
are all load-bearing, not illustrative.

Two things this step changes are *migrations*, not additions, and they break
step 09 code that still compiles otherwise:

- encoding is now fallible — `Writeable::write_onto`, `Writer::write`,
  `Versions::new`, `RelayCell::encode` and friends return `Result`;
- the client configuration key `download_tolerance` is now
  `directory_tolerance`. The old spelling must no longer resolve.

No cargo feature is enabled by the driver on any crate you write:

| Crate | Feature | Why the driver needs it |
| --- | --- | --- |
| _(none — every `@@APP@@` path dependency is taken with default features only)_ | _(none)_ | naming a feature here would make `cargo metadata` fail outright against a tree that has not declared it yet |

This is deliberate and it is stated in the driver's own manifest
(`tests/assets/harness/Cargo.toml`, the "NOTE ON FEATURES" comment): `cargo
metadata` validates every `<dep>/<feature>` reference eagerly, even one reached
only through a feature nobody enables, so writing `tor-cert = { ...,
features = ["encode"] }` in the driver would sink dependency resolution — and
therefore *every group in this step, including the inherited groups* — against a
candidate that has not added `encode` yet.

That is not a licence to skip the feature work. Three groups
(`accelbackend`, `fullfeat`, `certgenfeat`) check cargo features by **reading
your crates' `[features]` tables out of their `Cargo.toml` files at run time**
and asserting on what they say. Section 15 lists every feature name, every
crate that must declare it, and exactly what each must activate. A feature that
is missing from a `[features]` table fails its case; a feature named there but
spelled differently fails too, because the assertion is on the literal member
strings.

## What this document supersedes

`/app` is cumulative: every earlier step's `TEST_INTERFACE.md` is still on disk
under `/app/docs/step-NN/`, and `instruction.md` says those documents remain
binding. Five statements they make were accurate at their own checkpoint and
are **false at this one** (upstream `arti-v0.6.0`). Where an earlier document
conflicts with anything below, this one wins.

1. **`tor_error::ErrorKind` has 44 variants, not 40.** Step 06's §1.1 declares
   `pub enum ErrorKind { /* 40 variants */ }` and its §1.1.1 says "Every one of
   these 40 renderings is checked"; step 07's §12.1 corrects that to 42 and
   gives a 42-row table. Both were right where they were written. At this tag
   the enum has **44** variants. Nothing was renamed or removed — all 40 of
   step 06's names survive, in the same relative order — so an `ErrorKind`
   carried forward still compiles. Four variants were **added**:

   | Added variant | `Display` | First present at |
   | --- | --- | --- |
   | `ClockSkew` | `possible clock skew detected` | step 07 (already in its table) |
   | `Other` | `unclassified error` | step 07 (already in its table) |
   | `FsPermissions` | `problem with filesystem permissions` | step 09 |
   | `RemoteHostResolutionFailed` | `remote hostname lookup failure` | **this step** |

   Two of the four are asserted by name in this step: `protoerrctx` checks
   `ResolveError(Transient | Unrecognized)` → `RemoteHostResolutionFailed` and
   `HandshakeCertsExpired` → `ClockSkew`, both from §5's `HasKind` table. A
   third, `FsPermissions`, is named in §6's table and is true of this tag, but
   no group here asserts it — `persisterr` leaves `fs-mistrust` behaviour to
   `mistrustbase`, and step 09's `statemgrperm` is where that kind is checked.
   `Other` is neither used nor asserted in this step. The enum is
   `#[non_exhaustive]` and growing it is the expected thing for it to do — but
   the two in §5 are a hard dependency, so an `ErrorKind` copied from step 06's
   or step 07's table and left there will fail to compile against this step's
   driver.

2. **A `Display` string changed owner.** This is one reassignment, not two
   independent edits, and it is the only one in the enum.
   `RemoteHostNotFound` keeps its name and gains a new rendering; the string
   it vacated is inherited by the new `RemoteHostResolutionFailed`:

   | Variant | `Display` up to step 09 | `Display` here |
   | --- | --- | --- |
   | `RemoteHostNotFound` | `remote hostname lookup failure` | `remote hostname not found` |
   | `RemoteHostResolutionFailed` | _(did not exist)_ | `remote hostname lookup failure` |

   Step 06 §1.1.1 and step 07 §12.1 both still pair `RemoteHostNotFound` with
   `remote hostname lookup failure`, and both were right at their own tags.
   Carry either table forward unchanged, add `RemoteHostResolutionFailed` with
   its upstream label, and you get **two variants sharing one string** —
   contradicting step 07 §12.1's own stated property that the labels are
   distinct across the whole enum. At this tag all 44 renderings are distinct,
   and `RemoteHostNotFound` is the **only** variant whose rendering changed at
   all since step 06; every other surviving variant keeps its exact string.

   **Nothing in this step checks a rendering.** Every `ErrorKind` assertion in
   this step's groups compares variant identity —
   `assert_eq!(e.kind(), ErrorKind::Something)` — and no group here asserts
   `ErrorKind`'s `Display` output in any form. (Step 06's `errkind` group did
   check the renderings, against the 40-variant table correct at *that* tag.)
   So getting this wrong will not cost you a case here. It is a fidelity
   matter: the two variants are told apart by `kind()`, never by rendering — a
   *confirmed* denial is `RemoteHostNotFound`, while a transient or
   unrecognised answer, which may succeed on a retry, is
   `RemoteHostResolutionFailed`. §5's table is the part of this that *is*
   checked, and it is checked on the variants alone.

3. **`tor_bytes::Error` gained a variant and recased every message.** Step 06
   §7.1 declares four variants — `Truncated`, `ExtraneousBytes`,
   `BadMessage(&'static str)`, `Bug(#[from] tor_error::Bug)`. At this tag
   there are **five**: `BadLengthValue` is new. Step 06 gives no rendering
   table for this type, but the one fragment it does show — §7.3's worked
   example `parsing error: extra bytes at end of object` — is lower case, and
   at this tag every message on this enum is capitalised, so that fragment
   reads `Extra bytes at end of object` here, and `Bug` renders
   `Internal error` rather than `bug`. §1's `Error` (decoding) block is the
   authoritative list. What step 06 says about the derives still holds:
   `Clone + Debug`, no `Eq`, and a hand-written `PartialEq` under which a
   `Bug` compares equal to nothing. `tor_bytes::EncodeError`, which §1 also
   declares, has no counterpart at step 06 at all — it arrives with this
   step's fallible-encoding migration.

4. **`tor_cell::Error` changed the *shape* of two variants it kept.** Step 06
   §7.1 declares five variants with `BytesErr(#[from] tor_bytes::Error)` as a
   tuple variant and `CantEncode` as a unit variant, and its §7.3 renders
   `BytesErr(e)` as `parsing error: {0}`. At this tag there are **six**:
   `BytesErr` is a **struct** variant `{ err, parsed }`, `CantEncode` carries a
   `&'static str`, and `EncodeErr(#[from] tor_bytes::EncodeError)` is new. The
   names still resolve but the arities do not, so step 06's patterns
   `BytesErr(e)` and bare `CantEncode` no longer compile, and the `Display`
   strings are the ones in §4, not the ones in step 06 §7.3. §4 is
   authoritative for this type.

5. **`tor_persist::Error` is a struct now, not an enum.** Step 06 §7.1 declares
   a four-variant enum (`IoError`, `NoLock`, `Serialize`, `Deserialize`) and
   its §7.3 tabulates that enum's `Display` strings; step 09 describes it as a
   `#[non_exhaustive]` enum carrying a `Permissions(fs_mistrust::Error)`
   variant. At this tag `tor_persist::Error` is a **struct** with private
   fields holding a source, an action and a resource, and the variants both
   documents name live on a separate `ErrorSource` enum — where `Serialize`
   and `Deserialize` have merged into a single `Serde` arm, told apart by the
   action rather than by the variant. The rendering changed with it:
   `"{source} while {action} on {resource}"`, not the bare strings step 06
   tabulates. §6 is authoritative for this type.

## 1. `tor-bytes` — fallible encoding

`tor_bytes` re-exports its own result aliases:

```rust
pub type Result<T> = std::result::Result<T, Error>;
pub type EncodeResult<T> = std::result::Result<T, EncodeError>;
```

### The traits

```rust
pub trait Writeable {
    fn write_onto<B: Writer + ?Sized>(&self, b: &mut B) -> EncodeResult<()>;
}

pub trait WriteableOnce: Sized {
    fn write_into<B: Writer + ?Sized>(self, b: &mut B) -> EncodeResult<()>;
}

impl<W: Writeable + Sized> WriteableOnce for W { .. }   // blanket
```

`write_onto` taking `&self` and returning `EncodeResult<()>` is the whole point
of the group: the driver implements `Writeable` for its own types, so the
method signature must match exactly or the test file does not compile.

### `Writer`

```rust
pub trait Writer {
    fn write_all(&mut self, b: &[u8]);
    fn write_u8(&mut self, x: u8);
    fn write_u16(&mut self, x: u16);            // big-endian
    fn write_u32(&mut self, x: u32);            // big-endian
    fn write_u64(&mut self, x: u64);
    fn write_u128(&mut self, x: u128);
    fn write_zeros(&mut self, n: usize);
    fn write<E: Writeable + ?Sized>(&mut self, e: &E) -> EncodeResult<()>;
    fn write_and_consume<E: WriteableOnce>(&mut self, e: E) -> EncodeResult<()>;
    fn write_nested_u8len(&mut self) -> NestedWriter<'_, Self, u8>;
    fn write_nested_u16len(&mut self) -> NestedWriter<'_, Self, u16>;
    fn write_nested_u32len(&mut self) -> NestedWriter<'_, Self, u32>;
}
```

`impl Writer for Vec<u8>` and `impl Writer for bytes::BytesMut` must both
exist; the driver writes into a plain `Vec<u8>`.

`write` and `write_and_consume` **propagate** the inner object's error rather
than panicking or swallowing it. The integer writers stay infallible — a `u16`
always fits — so only the object-level entry points return `Result`.

### `NestedWriter`

```rust
pub struct NestedWriter<'w, W: ?Sized, L> { .. }

impl<'w, W, L> NestedWriter<'w, W, L>
where W: Writer + ?Sized,
      L: Default + Copy + Sized + Writeable + TryFrom<usize> + std::ops::Not<Output = L>
{
    pub fn finish(self) -> Result<(), EncodeError>;
}
```

`NestedWriter` derefs (`Deref`/`DerefMut`) to an inner `Vec<u8>`, which is how
the caller writes the body: every `Writer` method is reachable on it, including
`write_nested_*`, so nestings compose.

Observable behaviour the driver asserts:

- `finish()` writes the body length in the width named by the constructor —
  one, two or four bytes, big-endian — and then the body, onto the *outer*
  writer.
- If the body is longer than the length field can describe, `finish()` returns
  `Err(EncodeError::BadLengthValue)` **and appends nothing at all** to the outer
  writer. A partial body left behind is a failure: the test writes `0xde`,
  overflows a `u8` nesting with 1000 bytes, and asserts the buffer is still
  exactly `[0xde]`.
- A zero-length body is legal and encodes as a bare zero length field.

### `EncodeError`

```rust
pub enum EncodeError {
    BadLengthValue,        // Display: "Object length too large to encode"
    Bug(tor_error::Bug),
}
```

`Debug` is required (the driver prints it on mismatch) and `BadLengthValue`
must be nameable as a unit variant, because the driver constructs it directly.

### `Error` (decoding)

```rust
pub enum Error {
    Truncated,                       // "Object truncated (or not fully present)"
    ExtraneousBytes,                 // "Extra bytes at end of object"
    BadLengthValue,                  // "Object length too large to represent as usize"
    BadMessage(&'static str),        // "Bad object: {0}"
    Bug(tor_error::Bug),
}
```

`Error` must implement `PartialEq` — the driver compares against
`Error::Truncated` and `Error::BadMessage("...")` with `assert_eq!`. Note the
upstream quirk this step preserves: a `Bug` compares equal to nothing, not even
itself.

Five variants, not the four step 06 §7.1 lists: `BadLengthValue` is new here,
and every message above is capitalised where step 06's were lower case (`bug`
is now `Internal error`). See "What this document supersedes", item 3.

## 2. `tor-bytes` — length-prefixed decoding

```rust
impl<'a> Reader<'a> {
    pub fn from_slice(slice: &'a [u8]) -> Self;
    pub fn remaining(&self) -> usize;
    pub fn take_u8(&mut self) -> Result<u8>;
    pub fn take_u32(&mut self) -> Result<u32>;
    pub fn take_rest(&mut self) -> &'a [u8];
    pub fn should_be_exhausted(&self) -> Result<()>;

    pub fn read_nested_u8len<F, T>(&mut self, f: F) -> Result<T>
        where F: FnOnce(&mut Reader) -> Result<T>;
    pub fn read_nested_u16len<F, T>(&mut self, f: F) -> Result<T>
        where F: FnOnce(&mut Reader) -> Result<T>;
    pub fn read_nested_u32len<F, T>(&mut self, f: F) -> Result<T>
        where F: FnOnce(&mut Reader) -> Result<T>;
}
```

The nested readers are the mirror image of `NestedWriter`, and their contract
is what the group checks:

- read a length in the named width, big-endian;
- hand the closure a reader bounded to exactly that many bytes;
- if the outer buffer holds fewer bytes than the length promises, fail with
  `Error::Truncated` — the closure is never a place to discover truncation;
- if the closure returns `Ok` but has *not* consumed the whole counted region,
  fail with `Error::ExtraneousBytes`;
- if the closure returns `Err`, return that error unchanged — `take_u32()` on a
  two-byte region surfaces `Truncated`, not `ExtraneousBytes`;
- on success, consume exactly the length field plus the counted body from the
  outer reader and nothing more;
- a zero-length region is well-formed and yields an empty inner reader.

`take_rest` consumes everything left and returns it; calling it on an exhausted
reader yields an empty slice rather than an error.

## 3. `tor-cell` — cell encoding

### Channel cells

```rust
pub const tor_cell::chancell::CELL_DATA_LEN: usize = 509;
pub type tor_cell::chancell::RawCellBody = [u8; CELL_DATA_LEN];

pub struct CircId(u32);           // From<u32>, From<CircId> for u32, Display, is_zero()
pub struct ChanCmd(u8);           // caret_int: PADDING, VERSIONS, VPADDING,
                                  // PADDING_NEGOTIATE, ...; u8::from(ChanCmd)

pub struct ChanCell { .. }
impl ChanCell {
    pub fn new(circid: CircId, msg: ChanMsg) -> Self;
    pub fn circid(&self) -> CircId;
    pub fn msg(&self) -> &ChanMsg;
    pub fn into_circid_and_msg(self) -> (CircId, ChanMsg);
}
```

`ChanMsg` must have `cmd() -> ChanCmd`, and `From<Padding>` / `From<VPadding>`
(the driver writes `cmsg::Padding::new().into()`).

```rust
impl msg::Padding  { pub fn new() -> Self; }
impl msg::VPadding { pub fn new(len: u16) -> Self; }
```

### `Versions`

```rust
impl msg::Versions {
    pub fn new<B: Into<Vec<u16>>>(vs: B) -> crate::Result<Self>;
    pub fn encode_for_handshake(self) -> tor_bytes::EncodeResult<Vec<u8>>;
    pub fn best_shared_link_protocol(&self, others: &[u16]) -> Option<u16>;
}
```

`new` is fallible: a list with **`vs.len() >= u16::MAX / 2`** must return
`Err(Error::CantEncode("Too many versions"))` — that exact string, because the
driver matches on it and on the rendered message. The predicate is exact and
the boundary is inclusive: `vs.len()` one below `u16::MAX / 2` succeeds, and
`vs.len() == u16::MAX / 2` is refused (the encoded body would need one byte
more than a `u16` can describe).

`encode_for_handshake` produces the first-VERSIONS-cell layout, which always
uses a **two-byte** CircID regardless of the versions offered:
`[0x00, 0x00] | ChanCmd::VERSIONS | u16 body length | u16 versions...`. For
`[3, 4, 5]` that is exactly
`00 00 07 00 06 00 03 00 04 00 05`.

### Relay cells

```rust
pub struct RelayCmd(u8);          // caret_int: DATA = 2, ...; u8::from(RelayCmd)
pub struct StreamId(u16);         // From<u16>, From<StreamId> for u16, Display, is_zero()

impl RelayCell {
    pub fn new(streamid: StreamId, msg: RelayMsg) -> Self;
    pub fn cmd(&self) -> RelayCmd;
    pub fn stream_id(&self) -> StreamId;
    pub fn msg(&self) -> &RelayMsg;
    pub fn into_streamid_and_msg(self) -> (StreamId, RelayMsg);
    pub fn encode<R: Rng + CryptoRng>(self, rng: &mut R) -> crate::Result<RawCellBody>;
    pub fn decode(body: RawCellBody) -> crate::Result<Self>;
    pub fn decode_from_reader(r: &mut Reader<'_>) -> crate::Result<Self>;
}
```

`encode` takes the rng **by `&mut`** and consumes `self`; it returns a
509-byte body whose header is, in order: command (1), `recognized` (2, zero),
stream ID (2, big-endian), digest (4, zeroed at encode time), length (2), then
the body, then padding. The driver reads those offsets out of the array
directly, so the field order is checked, not just the round trip.

`RelayMsg` must expose `cmd()` and `Debug`, and carry a `Data` variant.

```rust
impl msg::Data {
    pub const MAXLEN: usize = CELL_DATA_LEN - 11;    // 498
    pub fn new(inp: &[u8]) -> crate::Result<Self>;
    pub fn split_from(inp: &[u8]) -> (Self, &[u8]);
}
impl AsRef<[u8]> for msg::Data { .. }
```

`Data::new` is fallible: more than `MAXLEN` bytes returns
`Err(Error::CantEncode(_))`; exactly `MAXLEN` is accepted. `From<Data> for
RelayMsg` must exist.

### `ChannelCodec`

```rust
impl ChannelCodec {
    pub fn new(link_version: u16) -> Self;
    pub fn write_cell(&mut self, item: ChanCell, dst: &mut BytesMut) -> crate::Result<()>;
    pub fn decode_cell(&mut self, src: &mut BytesMut) -> crate::Result<Option<ChanCell>>;
}
```

At link version 4 a fixed-length cell is 514 bytes (4-byte CircID + command +
509-byte payload) with the payload zero-padded; a variable-length cell is
CircID, command, u16 body length, body. `decode_cell` consumes exactly one
cell, returns `Ok(None)` for an incomplete prefix **without consuming
anything**, and only returns `Err` for input that is complete and malformed.

## 4. `tor-cell` — errors that carry context

Six variants. Step 06 §7.1 declares five, with `BytesErr` as a *tuple* variant
and `CantEncode` as a *unit* variant, and its §7.3 gives different `Display`
strings. That document is still binding and was correct at its own tag; the
shapes below are the ones at this tag, and step 06's patterns `BytesErr(e)`
and bare `CantEncode` no longer compile. See "What this document supersedes",
item 4.

```rust
#[non_exhaustive]
pub enum tor_cell::Error {
    BytesErr { #[source] err: tor_bytes::Error, parsed: &'static str },
                                     // "Error while parsing {parsed}"
    EncodeErr(#[from] tor_bytes::EncodeError),
                                     // "Error while encoding message"
    Internal(tor_error::Bug),
    ChanProto(String),               // "Channel protocol violation: {0}"
    BadStreamAddress,                // "Invalid stream target address"
    CantEncode(&'static str),        // "Message can't be represented in a Tor cell: {0}"
}
```

`BytesErr` is a **struct variant**: the driver destructures it by field name
and reads `parsed`. Channel-cell decoding must set `parsed` to the literal
`"channel cell"`. `err` is the `#[source]`, so
`std::error::Error::source()` on a `BytesErr` yields the `tor_bytes::Error`
and renders as `"Object truncated (or not fully present)"`.

`impl From<tor_bytes::EncodeError> for Error` must produce `EncodeErr`.

`impl HasKind for Error`:

| Error | `ErrorKind` |
| --- | --- |
| `BytesErr { err: tor_bytes::Error::Truncated, .. }` | `Internal` |
| `BytesErr { .. }` (any other inner error) | `TorProtocolViolation` |
| `EncodeErr(_)` | `BadApiUsage` |
| `Internal(_)` | `Internal` |
| `ChanProto(_)` | `TorProtocolViolation` |
| `BadStreamAddress` | `BadApiUsage` |
| `CantEncode(_)` | `Internal` |

The `Truncated` split is the interesting one and it is checked directly: the
codec has already length-checked the cell before parsing it, so a short read
inside is our own bug, not the peer's protocol violation.

Two decode paths are checked through the codec, so the errors they raise have
to be the ones named above:

- a VERSIONS body of odd length → `BytesErr { parsed: "channel cell",
  err: tor_bytes::Error::Truncated }`;
- a PADDING_NEGOTIATE cell whose version byte is not 0 → `BytesErr { parsed:
  "channel cell", err: tor_bytes::Error::BadMessage("Unrecognized padding
  negotiation version") }` (that exact string);
- a PADDING cell on a nonzero circuit → `ChanProto(msg)` where `msg` names the
  command (`"PADDING"` appears in it).

All three are **decode** rules. `write_cell` frames whatever cell it is handed
and never applies the circuit-ID convention, so a PADDING cell on a nonzero
circuit encodes normally -- a case in scope builds exactly that cell and requires
514 bytes out. A codec that validates on the way out as well as on the way in
refuses a cell the encoder must write.

## 5. `tor-proto` — errors that carry context

Re-exported at the crate root:

```rust
pub use util::err::{Error, ResolveError};
```

The variants the driver constructs by name (all `#[non_exhaustive]` enum
members; field names are checked):

```rust
pub enum tor_proto::Error {
    BytesErr { object: &'static str, #[source] err: tor_bytes::Error },
                               // "Unable to parse {object}"
    CellEncodeErr { object: &'static str, #[source] err: tor_cell::Error },
                               // "Unable to generate or encode {object}"
    CellDecodeErr { object: &'static str, #[source] err: tor_cell::Error },
                               // "Error while parsing {object}"
    EncodeErr { object: &'static str, #[source] err: tor_bytes::EncodeError },
                               // "Problem while encoding {object}"
    HandshakeCertErr(#[source] tor_cert::CertError),
                               // "Problem with certificate on handshake"
    HandshakeCertsExpired { expired_by: std::time::Duration },
        // "Handshake failed due to expired certificates (possible clock skew)"
    CircuitClosed,             // "Circuit closed"
    CircRefused(&'static str), // "Circuit extension refused: {0}"
    ChanMismatch(String),      // "Peer identity mismatch: {0}"
    ResolveError(#[source] ResolveError),  // "Remote resolve failed"
    ..
}

pub enum ResolveError {
    Transient,      // "Received retriable transient error"
    Nontransient,   // "Received non-retriable error"
    Unrecognized,   // "Received unrecognized result"
}
```

Note `CellDecodeErr`'s `Display`: it does **not** say "decode", it says
`"Error while parsing {object}"`. The four object-carrying variants exist so an
error can say *what* was being manipulated; that string is the whole feature and
it is asserted literally.

`impl HasKind for tor_proto::Error`:

| Error | `ErrorKind` |
| --- | --- |
| `BytesErr { .. }` | `TorProtocolViolation` |
| `CellEncodeErr { err, .. }` | whatever `err.kind()` is (delegated) |
| `CellDecodeErr { err, .. }` | whatever `err.kind()` is (delegated) |
| `EncodeErr { .. }` | `BadApiUsage` |
| `HandshakeCertErr(_)` | `TorProtocolViolation` |
| `HandshakeCertsExpired { .. }` | `ClockSkew` |
| `CircuitClosed` | `CircuitCollapse` |
| `CircRefused(_)` | `CircuitRefused` |
| `ChanMismatch(_)` | `RelayIdMismatch` |
| `ResolveError(Nontransient)` | `RemoteHostNotFound` |
| `ResolveError(Transient \| Unrecognized)` | `RemoteHostResolutionFailed` |

Two kinds in that table are absent from step 06's binding 40-variant
enumeration and are asserted here by name: `ClockSkew`, which step 07 §12.1
already adds, and `RemoteHostResolutionFailed`, which appears for the first
time at this tag and is named in no earlier document. The resolve split is the
reason it exists: a *confirmed* denial stays `RemoteHostNotFound`, while a
transient or unrecognised answer — which may succeed on a retry — is
`RemoteHostResolutionFailed`. This table is checked on the **variants**; no
group in this step asserts an `ErrorKind` rendering. But the new variant
arrived by taking over the old one's label — `RemoteHostNotFound` now renders
`remote hostname not found`, and `remote hostname lookup failure`, which step
06 §1.1.1 and step 07 §12.1 still give it, belongs to
`RemoteHostResolutionFailed` here — so a table copied forward from either
document is wrong about both. See "What this document supersedes", items 1
and 2.

Delegation is checked both ways round: `CellEncodeErr` wrapping
`tor_cell::Error::CantEncode` must report `Internal`, and `CellDecodeErr`
wrapping a `BytesErr { err: BadMessage(_) }` must report
`TorProtocolViolation`. A hard-coded kind on those two variants fails.

```rust
impl From<tor_proto::Error> for std::io::Error { .. }
```

must map the cell/bytes variants to `std::io::ErrorKind::InvalidData` and keep
the rendered message — the driver asserts the resulting `io::Error` still
mentions the `object` string.

## 6. `tor-persist` — errors that carry context

`tor_persist::Error` is a **struct** here. Step 06 §7.1 declares a four-variant
enum and step 09 describes it as an enum with a `Permissions` variant; both
were correct at their own tags and both are superseded by this section. See
"What this document supersedes", item 5.

```rust
pub use err::Error;                          // at the crate root
pub use fs::FsStateMgr;                      // non-wasm
pub use handle::{DynStorageHandle, StorageHandle};
```

```rust
pub struct tor_persist::Error { .. }         // a struct, not an enum
impl Error {
    pub fn source(&self) -> &ErrorSource;    // inherent; returns the cause value
}
```

`Display` is `"{source} while {action} on {resource}"`. The driver asserts the
shape end to end: storing without the lock renders as

```
Storage not locked while storing persistent data on persistent storage manager
```

so the `ErrorSource` strings `"Storage not locked"`, `"JSON error"`,
`"IO error"` and `"Invalid permissions"`, and the action string
`"storing persistent data"`, are all part of the contract, as is the resource
string `"persistent storage manager"` for a lock failure.

The inherent `source()` returns a value implementing `Display`; separately,
`std::error::Error::source()` must return `None` for a `NoLock` error, because
"storage not locked" has no inner cause of its own.

`impl HasKind for tor_persist::Error`:

| Situation | `ErrorKind` |
| --- | --- |
| no lock held | `BadApiUsage` |
| unparsable JSON on disk, while loading | `PersistentStateCorrupted` |
| I/O failure | `PersistentStateAccessFailed` |
| permissions rejected by `fs-mistrust` | `FsPermissions` |

```rust
pub trait StateMgr: Clone {
    fn load<D: DeserializeOwned>(&self, key: &str) -> Result<Option<D>>;
    fn store<S: Serialize>(&self, key: &str, val: &S) -> Result<()>;
    fn can_store(&self) -> bool;
    fn try_lock(&self) -> Result<LockStatus>;
    fn unlock(&self) -> Result<()>;
    fn create_handle<T>(self, key: impl Into<String>) -> Arc<dyn StorageHandle<T>>
        where T: Serialize + DeserializeOwned + 'static, Self: Send + Sync + Sized + 'static;
}

pub enum LockStatus { NoLock, AlreadyHeld, NewlyAcquired }
impl LockStatus { pub fn held(&self) -> bool; }
```

`load` on a key that was never written returns `Ok(None)`, not an error.
`can_store()` is false on a fresh manager, true after `try_lock()`, false again
after `unlock()`.

```rust
impl FsStateMgr {
    pub fn from_path_and_mistrust<P: AsRef<Path>>(path: P, mistrust: &fs_mistrust::Mistrust)
        -> Result<Self>;
    pub fn from_path<P: AsRef<Path>>(path: P) -> Result<Self>;
    pub fn path(&self) -> &Path;
}
```

On-disk layout is checked, because one case corrupts a state file by hand: a
value stored under key `k` in a manager rooted at `dir` lives at
`dir/state/<k>.json`.

## 7. `tor-cert` — Ed25519 certificate checking

```rust
pub use err::CertError;

pub struct CertType(u8);   // caret_int
//   TLS_LINK_X509 = 0x01, RSA_ID_X509 = 0x02, LINK_AUTH_X509 = 0x03,
//   IDENTITY_V_SIGNING = 0x04, SIGNING_V_TLS_CERT = 0x05,
//   SIGNING_V_LINK_AUTH = 0x06, RSA_ID_V_IDENTITY = 0x07, ...
pub struct KeyType(u8);    // ED25519_KEY = 0x01, SHA256_OF_RSA = 0x02,
                           // SHA256_OF_X509 = 0x03
pub struct ExtType(u8);    // SIGNED_WITH_ED25519_KEY = 0x04
```

`u8::From<CertType>` must exist; the driver asserts
`u8::from(CertType::IDENTITY_V_SIGNING) == 4`.

The three-stage checked pipeline, in order:

```rust
impl Ed25519Cert {
    pub fn decode(cert: &[u8]) -> tor_bytes::Result<KeyUnknownCert>;
    pub fn expiry(&self) -> std::time::SystemTime;
    pub fn is_expired_at(&self, when: std::time::SystemTime) -> bool;
    pub fn subject_key(&self) -> &CertifiedKey;
    pub fn signing_key(&self) -> Option<&Ed25519Identity>;
    pub fn cert_type(&self) -> CertType;
}

impl KeyUnknownCert {
    pub fn peek_cert_type(&self) -> CertType;
    pub fn peek_subject_key(&self) -> &CertifiedKey;
    pub fn check_key(self, pkey: Option<&Ed25519Identity>) -> CertResult<UncheckedCert>;
}

impl UncheckedCert {
    pub fn peek_subject_key(&self) -> &CertifiedKey;
    pub fn peek_signing_key(&self) -> &Ed25519Identity;
    pub fn dangerously_split(self) -> CertResult<(SigCheckedCert, Signature)>;
}

impl CertifiedKey {
    pub fn key_type(&self) -> KeyType;
    pub fn as_bytes(&self) -> &[u8];
    pub fn as_ed25519(&self) -> Option<&Ed25519Identity>;
}

impl tor_checkable::SelfSigned<SigCheckedCert> for UncheckedCert { type Error = CertError; }
impl tor_checkable::Timebound<Ed25519Cert> for SigCheckedCert {
    type Error = tor_checkable::TimeValidityError;
}
```

`decode` returns a `tor_bytes::Result`, so a truncated input surfaces as
`tor_bytes::Error::Truncated`, not a `CertError`.

`check_key(pkey)` resolves the signing key, and its three outcomes are checked
separately:

- `pkey = None` and the certificate carries a `SIGNED_WITH_ED25519_KEY`
  extension → the extension supplies the signer;
- `pkey = Some(k)` and the certificate names a *different* key →
  `Err(CertError::KeyMismatch)`;
- `pkey = None` and the certificate names no key →
  `Err(CertError::MissingPubKey)`. An externally supplied key is accepted in
  that same situation -- and it *becomes* the certificate's signing key, so
  once the certificate has been checked all the way through, its
  `signing_key()` returns that key rather than `None`.

```rust
pub enum CertError {
    KeyMismatch,     // "Key on certificate was not as expected"
    MissingPubKey,   // "Missing signing key on certificate"
    BadSignature,    // "Signature on certificate was invalid"
}
```

`CertError` must be `PartialEq` — the driver compares variants with
`assert_eq!`.

`expiry()` converts the certificate's four-byte expiration field from **hours**
to a `SystemTime`, and `is_expired_at(when)` is `when >= expiry()`, so the
expiry instant itself counts as expired. `check_valid_at` past the expiry
yields `TimeValidityError::Expired(_)`.

Fixtures: `tests/assets/harness/fixtures/cert_id_v_signing.bin` and
`cert_no_ext.bin`, both `IDENTITY_V_SIGNING` certificates over the same
subject key, signed by the same identity, expiring at hour 500 000. The first
carries the signing-key extension; the second carries none. Their byte layout
is the one in `SPEC_NOTES.md` §6 ("Ed25519 certificates").

## 8. `tor-checkable` — timeliness and self-signature

```rust
pub enum TimeValidityError {
    NotYetValid(Duration),   // how long until it becomes valid
    Expired(Duration),       // how long ago it stopped being valid
    Unspecified,
}

pub trait Timebound<T>: Sized {
    type Error;
    fn is_valid_at(&self, t: &SystemTime) -> Result<(), Self::Error>;
    fn dangerously_assume_timely(self) -> T;
    fn check_valid_at(self, t: &SystemTime) -> Result<T, Self::Error>;
    fn check_valid_now(self) -> Result<T, Self::Error>;
    fn check_valid_at_opt(self, t: Option<SystemTime>) -> Result<T, Self::Error>;
}

pub trait SelfSigned<T>: Sized {
    type Error;
    fn is_well_signed(&self) -> Result<(), Self::Error>;
    fn dangerously_assume_wellsigned(self) -> T;
    fn check_signature(self) -> Result<T, Self::Error>;
}
```

`TimeValidityError` must be `PartialEq` and `Debug`.

```rust
impl<T> TimerangeBound<T> {
    pub fn new<U>(obj: T, range: U) -> Self where U: RangeBounds<SystemTime>;
    pub fn extend_tolerance(self, d: Duration) -> Self;        // widens the END
    pub fn extend_pre_tolerance(self, d: Duration) -> Self;    // widens the START
    pub fn dangerously_into_parts(self) -> (T, (Option<SystemTime>, Option<SystemTime>));
    pub fn dangerously_peek(&self) -> &T;
}
impl<T> Timebound<T> for TimerangeBound<T> { type Error = TimeValidityError; }
```

The boundary semantics are checked exactly:

- `new(obj, t(1000)..t(2000))` accepts 1001 and 1999 but rejects **both**
  endpoints — `t(1000)` gives `NotYetValid(Duration::ZERO)` and `t(2000)` gives
  `Expired(Duration::ZERO)`. `x..y` and `x..=y` are not distinguished.
- The `Duration` in each error is the overshoot: 60 seconds early is
  `NotYetValid(60s)`, 60 seconds late is `Expired(60s)`.
- A half-open range is bounded only on the side it names; `..` is valid
  always.
- `extend_tolerance` moves only the end and `extend_pre_tolerance` only the
  start; neither touches the other side.
- `check_valid_at_opt(None)` means "check against now"; `Some(t)` means
  "check against `t`".

## 9. `tor-netdir` / `tor-dirmgr` — directory timeliness levels

```rust
#[derive(Copy, Clone, Eq, PartialEq, Debug)]
pub enum tor_netdir::Timeliness { Strict, Timely, Unchecked }
```

Three distinct variants, `Debug` rendering as the bare names `"Strict"`,
`"Timely"`, `"Unchecked"`.

```rust
pub trait NetDirProvider: UpcastArcNetDirProvider + Send + Sync {
    fn netdir(&self, timeliness: Timeliness) -> Result<Arc<NetDir>>;
    fn timely_netdir(&self) -> Result<Arc<NetDir>> {
        self.netdir(Timeliness::Timely)          // provided method
    }
    fn events(&self) -> BoxStream<'static, DirEvent>;
}

pub trait UpcastArcNetDirProvider {
    fn upcast_arc<'a>(self: Arc<Self>) -> Arc<dyn NetDirProvider + 'a> where Self: 'a;
}
impl<T> UpcastArcNetDirProvider for T where T: NetDirProvider + Sized { .. }   // blanket
```

`pub type tor_netdir::Result<T>` and `pub enum DirEvent` must both be public —
the driver implements `NetDirProvider` for its own recorder type, so the trait
shape is compiled against, not merely called.

`netdir` must forward the caller's level **unchanged**; `timely_netdir` must be
a provided method that delegates to `netdir(Timeliness::Timely)` rather than
an independent implementation. Blanket impls for `Arc<T>` and for references
must keep that delegation.

```rust
pub enum tor_netdir::Error {
    NotEnoughInfo,    // "Not enough directory information to build circuits"
    NoInfo,           // "No directory information available"
    DirExpired,       // "Directory is expired, and we haven't got a new one yet"
    DirNotYetValid,   // "Directory is published too far in the future: Your clock is probably wrong"
}
```

| Error | `ErrorKind` |
| --- | --- |
| `DirExpired` | `DirectoryExpired` |
| `DirNotYetValid` | `ClockSkew` |
| `NoInfo`, `NotEnoughInfo` | `BootstrapRequired` |

`tor_dirmgr` re-exports the same type, not a copy:
`pub use tor_netdir::Timeliness;`. The driver passes a
`tor_dirmgr::Timeliness` to a function typed `tor_netdir::Timeliness`, which
only compiles if it is one type.

## 10. `tor-dirmgr` / `tor-chanmgr` / `arti-client` — bootstrap blockage

```rust
pub use event::{DirBlockage, DirBootstrapEvents, DirBootstrapStatus};   // tor-dirmgr

pub enum DirBlockage {
    Stalled,          // "Can't make progress."
    TooManyErrors,    // "Too many errors without making progress."
    TooManyResets,    // "Had to reset bootstrapping too many times."
}
```

The trailing full stops are part of the strings.

```rust
#[derive(Clone, Debug, Default)]
pub struct DirBootstrapStatus(..);
impl DirBootstrapStatus {
    pub fn frac_at(&self, when: SystemTime) -> f32;
    pub fn usable_at(&self, now: SystemTime) -> bool;
    pub fn blockage(&self, now: SystemTime) -> Option<DirBlockage>;
}
```

`Default` must be derivable/derived and must mean "nothing attempted yet":
`frac_at` is `0.0`, `usable_at` is false, and `blockage` is `None` at *any*
instant. All three accessors take the instant explicitly — no wall-clock reads.

```rust
pub enum tor_chanmgr::ConnBlockage { NoTcp, NoHandshake, CertsExpired }
```

```rust
// arti_client::status
#[derive(Debug, Clone, Default)]
pub struct BootstrapStatus { .. }
impl BootstrapStatus {
    pub fn as_frac(&self) -> f32;
    pub fn ready_for_traffic(&self) -> bool;
    pub fn blocked(&self) -> Option<Blockage>;
}

pub struct Blockage { .. }
pub enum BlockageKind {
    Offline,        // "We seem to be offline"
    Filtering,      // "Our internet connection seems filtered"
    CantReachTor,   // "Can't reach the Tor network"
    ClockSkewed,    // "Clock is skewed."
    CantBootstrap,  // "Can't bootstrap a Tor directory."
}
```

**`BootstrapStatus::default()` means "unbootstrapped and unblocked"**, and a
case in scope asserts all three answers on it: `as_frac() == 0.0`,
`ready_for_traffic() == false` and `blocked().is_none()`. That last one is the
interesting half — a fresh client has had nothing fail yet, so there is nothing
to report, and `Default` must not synthesize a `Blockage`. The rule stated
above for `DirBootstrapStatus::default()` is the same rule; this paragraph is
where the client-level half of it is written down.

The two conversions are the point of the group:

| From | To |
| --- | --- |
| `ConnBlockage::NoTcp` | `BlockageKind::Offline` |
| `ConnBlockage::NoHandshake` | `BlockageKind::Filtering` |
| `ConnBlockage::CertsExpired` | `BlockageKind::ClockSkewed` |
| any other `ConnBlockage` | `BlockageKind::CantReachTor` |
| **every** `DirBlockage` | `BlockageKind::CantBootstrap` |

A directory that cannot bootstrap is not a connection problem, so
`From<DirBlockage> for BlockageKind` is deliberately constant. Both `From`
impls must exist; the driver writes `let k: BlockageKind = b.into();`.

## 11. `arti-client` / `tor-dirmgr` — directory tolerance configuration

```rust
// tor_dirmgr::config
#[derive(Debug, Clone, Builder, Eq, PartialEq)]
pub struct DirTolerance {
    pre_valid_tolerance: Duration,     // default 24 h
    post_valid_tolerance: Duration,    // default 72 h (3 days)
}
impl DirTolerance { pub fn builder() -> DirToleranceBuilder; }
impl Default for DirTolerance { .. }
impl DirToleranceBuilder {
    pub fn pre_valid_tolerance(&mut self, v: Duration) -> &mut Self;
    pub fn post_valid_tolerance(&mut self, v: Duration) -> &mut Self;
    pub fn build(&self) -> Result<DirTolerance, ConfigBuildError>;
}
```

`DirTolerance` must be `PartialEq`, and building it with the default values
explicitly must produce a value equal to `DirTolerance::default()`.

Re-exported for clients:

```rust
pub mod arti_client::config::dir {
    pub use tor_dirmgr::{DirTolerance, DirToleranceBuilder, DownloadSchedule,
                         DownloadScheduleConfig, DownloadScheduleConfigBuilder, ..};
}
```

**The rename.** `TorClientConfig` carries the field as

```rust
pub struct TorClientConfig {
    pub directory_tolerance: dir::DirTolerance,   // sub-builder
    pub download_schedule: dir::DownloadScheduleConfig,
    ..
}
```

with the matching builder accessor
`TorClientConfigBuilder::directory_tolerance(&mut self) -> &mut DirToleranceBuilder`.

Three things are checked together and only pass as a set:

1. `[directory_tolerance]` is consumed by `TorClientConfig` — resolving a
   configuration that contains it leaves **no** unrecognized keys;
2. `[download_tolerance]`, the step-09 spelling, is reported *as unrecognized*
   and configures nothing — the resolved config still equals
   `TorClientConfig::default()`;
3. `[download_schedule]` is untouched by the rename and is still recognized.

`TorClientConfig` must be `PartialEq` and `Default`, and
`TorClientConfig::builder()` must exist.

The driver resolves through

```rust
pub fn tor_config::resolve_return_unrecognized<T: Resolvable>(input: config::Config)
    -> Result<(T, Vec<UnrecognizedKey>), ConfigResolveError>;
pub use config as tor_config::config_crate;   // the `config` crate, re-exported
```

`UnrecognizedKey` must implement `Display`, rendering the dotted key path, so
that a key starting with `download_tolerance` is recognizable in the report.

## 12. `reachable_addrs` — patterns, configuration, guard filter, targets

```rust
// tor_netdoc::types::policy
pub struct AddrPortPattern { .. }
impl AddrPortPattern {
    pub fn new_all() -> Self;
    pub fn matches(&self, addr: &IpAddr, port: u16) -> bool;
    pub fn matches_sockaddr(&self, addr: &SocketAddr) -> bool;
}
impl FromStr for AddrPortPattern { type Err = PolicyError; }
impl Display for AddrPortPattern { .. }     // "*:*", "*:443", "10.0.0.0/8:*"
```

`AddrPortPattern` must be `PartialEq`, `Clone` and `Debug`. The grammar the
driver parses is in `SPEC_NOTES.md` §9 ("Address patterns and reachable
addresses"): `new_all()` renders as `"*:*"` and
matches every address and port; `"*:443"` is port-exact; `"10.0.0.0/8:*"` is a
v4 prefix that must **not** match an IPv6 address; `"*:80-443"` is an inclusive
range.

```rust
// tor_circmgr::config
pub struct PathConfig { reachable_addrs: Vec<AddrPortPattern>, .. }
// default: vec![AddrPortPattern::new_all()]
```

with list-builder accessors on `PathConfigBuilder` (generated by
`tor_config::define_list_builder_accessors!`):

```rust
impl PathConfigBuilder {
    pub fn reachable_addrs(&mut self) -> &mut Vec<AddrPortPattern>;
    pub fn set_reachable_addrs(&mut self, list: Vec<AddrPortPattern>);
    pub fn opt_reachable_addrs(&self) -> &Option<Vec<AddrPortPattern>>;
    pub fn opt_reachable_addrs_mut(&mut self) -> &mut Option<Vec<AddrPortPattern>>;
}
```

Behaviour checked: `opt_reachable_addrs()` is `None` until the list is touched;
calling `reachable_addrs()` materialises the default (`[*:*]`) and
`opt_reachable_addrs()` is `Some` afterwards; a configuration that merely
restates the wildcard equals `TorClientConfig::default()`, while pushing
`*:443` does not.

The client reaches it through
`TorClientConfigBuilder::path_rules(&mut self) -> &mut PathConfigBuilder`, and
`arti_client::config::circ` re-exports `PathConfig` and `PathConfigBuilder`.

```rust
// tor_guardmgr
#[derive(Debug, Clone, Default, Eq, PartialEq)]
pub struct GuardFilter { .. }
impl GuardFilter {
    pub fn unfiltered() -> Self;
    pub fn push_reachable_addresses(&mut self, addrs: impl IntoIterator<Item = AddrPortPattern>);
}
```

`unfiltered()` must equal `GuardFilter::default()`; pushing a restriction must
change the value; two filters built from equal pattern lists must compare
equal, and from different lists must not. Equality is therefore structural —
a filter that stores a boxed closure cannot satisfy this group.

```rust
// tor_linkspec
pub trait ChanTarget {
    fn addrs(&self) -> &[SocketAddr];
    fn ed_identity(&self) -> &Ed25519Identity;
    fn ed_identity_key(&self) -> Option<ed25519::PublicKey>;
    fn rsa_identity(&self) -> &RsaIdentity;
}

impl OwnedChanTarget {
    pub fn new(addrs: Vec<SocketAddr>, ed_identity: Ed25519Identity,
               rsa_identity: RsaIdentity) -> Self;
    pub fn from_chan_target<C: ChanTarget + ?Sized>(target: &C) -> Self;
    pub fn restrict_addr(&self, addr: &SocketAddr) -> Result<Self, Self>;
}
impl ChanTarget for OwnedChanTarget { .. }
```

`restrict_addr` returns `Ok(target_with_only_that_address)` when `addr` is one
of the target's, keeping both identities unchanged; otherwise it returns
`Err(clone_of_the_unrestricted_target)` — the error arm carries the original
back so the caller can report it, and the driver asserts its `addrs()` are the
full original list.

Identity constructors used: `Ed25519Identity::from([u8; 32])` and
`RsaIdentity::from_bytes(&[u8]) -> Option<Self>` (20 bytes).

## 13. `tor-netdoc` — document parse errors

```rust
pub use err::{BuildError, Error, ParseErrorKind, Pos};
pub type Result<T> = std::result::Result<T, Error>;
```

```rust
pub struct tor_netdoc::Error { .. }
impl Error {
    pub fn parse_error_kind(&self) -> ParseErrorKind;
    pub fn within(self, s: &str) -> Error;          // add document context
    pub fn at_pos(self, p: Pos) -> Error;           // add positional context
}
```

`Display` leads with the kind, then the position, then `": {msg}"` when there
is a message — so an annotated policy error still starts with
`"invalid policy entry"`.

`ParseErrorKind` must be `PartialEq` and `Debug`, with these `Display`
strings (the four the driver asserts, plus the two it reaches indirectly):

| Variant | `Display` |
| --- | --- |
| `BadPolicy` | `invalid policy entry` |
| `BadTorVersion` | `couldn't parse Tor version` |
| `BadSignature` | `couldn't validate signature` |
| `MissingToken` | `didn't find required entry` |
| `Undecodable` | `decoding error` |
| `BadArgument` | `bad argument for entry` |

`pub enum Pos { None, Unknown, Invalid(usize), .. }` must be public and
constructible as `Pos::None`.

Conversions into `Error`, each fixing the kind:

| Source | `ParseErrorKind` |
| --- | --- |
| `tor_netdoc::types::policy::PolicyError` | `BadPolicy` |
| `tor_bytes::Error` | `Undecodable` |
| `std::num::ParseIntError` | `BadArgument` |
| `std::net::AddrParseError` | `BadArgument` |
| `signature::Error` | `BadSignature` |

The two parsers the driver drives:

```rust
impl FromStr for tor_netdoc::types::version::TorVersion { type Err = tor_netdoc::Error; }
impl FromStr for tor_netdoc::types::policy::PortPolicy { type Err = PolicyError; }
```

`TorVersion::from_str` fails straight into `ParseErrorKind::BadTorVersion`
(note its `Err` is `tor_netdoc::Error` directly, not `PolicyError`), and
`"0.4.7.8"` must still parse. `PortPolicy::from_str("accept 1-2-3")` fails, and
that `PolicyError` converts to `BadPolicy`. Adding context with `within` and
`at_pos` must not change the classification.

## 14. `tor-llcrypto` — digest aliases and the pluggable backend

```rust
pub mod tor_llcrypto::d {
    pub use sha1::Sha1;                        // when `with-openssl` is off
    #[cfg(feature = "with-openssl")]
    pub use openssl_compat::Sha1;              // when it is on
    pub use sha2::{Sha256, Sha512};
    pub use sha3::{Sha3_256, Shake128, Shake256};
}
```

All six names must be usable through the RustCrypto `digest` 0.10 traits, with
no wrapper of arti's own in the way:

- `Sha1`, `Sha256`, `Sha512`, `Sha3_256`: `digest::Digest` — `D::digest(msg)`,
  `D::new()`, `update`, `finalize`, `output_size()` (20, 32, 64, 32
  respectively).
- `Shake128`, `Shake256`: `Default + digest::Update + digest::ExtendableOutput`
  — `finalize_xof()` then `XofReader::read`, squeezing an arbitrary number of
  bytes as a continuous stream (a 64-byte squeeze must extend, not replace,
  the 32-byte one).

The whole point of the accelerated backend is that it is invisible from here:
whichever `Sha1` is selected must produce the FIPS-180-4 vectors. The group
asserts published vectors, not arti's own output, so it is anchored outside the
implementation — see `SPEC_NOTES.md` §7 ("Digests, and why the backend is
swappable").

When `with-openssl` is enabled, the OpenSSL-backed `Sha1` has to implement
`digest::Update`, `OutputSizeUser` (`OutputSize = U20`), `FixedOutput` and
`HashMarker` so that the blanket `Digest` impl applies to it too. The driver
builds with the feature **off**, so this is a compile-time contract on your
side, not something the driver observes directly.

## 15. Cargo feature milestones

These are read out of your crates' `Cargo.toml` files at run time, by name.
The driver parses the `[features]` table and the `[dependencies]` line for the
named dependency; nothing is compiled with the feature on. Put each feature on
a single line, or as an array broken over a few lines — both parse.

### `tor-llcrypto` — accelerated backends

| Feature | Must activate |
| --- | --- |
| `with-openssl` | a member whose name contains `openssl` (upstream: `openssl`, `typenum`, `cipher`) |
| `with-sha1-asm` | exactly the member string `sha-1/asm` |

### `arti-client` — user-facing acceleration switches

| Feature | Must activate |
| --- | --- |
| `accel-sha1-asm` | `tor-llcrypto/with-sha1-asm` |
| `accel-openssl` | `tor-llcrypto/with-openssl` |

### The `arti` binary crate — the same switches, forwarded

`crates/arti/Cargo.toml` is read directly from the workspace even though the
driver does not depend on the binary crate.

| Feature | Must activate |
| --- | --- |
| `accel-sha1-asm` | `arti-client/accel-sha1-asm` |
| `accel-openssl` | `arti-client/accel-openssl` |
| `full` | `journald` (among others) |

### The `full` feature set

| Crate | Feature | Must activate |
| --- | --- | --- |
| `arti` | `full` | `journald` |
| `arti-client` | `full` | `tokio` **and** `async-std` |
| `tor-dirmgr` | `full` | (must merely exist) |
| `tor-netdoc` | `full` | (must merely exist) |

### `tor-cert` — certificate generation is opt-in

| Feature | Must activate |
| --- | --- |
| `encode` | **exactly** `["derive_builder"]` — nothing more |
| `experimental` | `encode` |
| `default` | **empty** |

and `[dependencies]` must carry a `derive_builder` line whose right-hand side
contains the literal text `optional = true`.

Upstream writes that dependency as
`derive_builder = { version = "0.11.2", package = "derive_builder_fork_arti", optional = true }`;
the key must be `derive_builder`, since that is the name the feature activates
and the name the driver looks up.

## 16. `tor-config` — `resolve_option`

```rust
pub fn resolve_option<T, DF>(input: &Option<Option<T>>, def: DF) -> Option<T>
where T: Clone + Default + PartialEq,
      DF: FnOnce() -> Option<T>;
```

The three-state semantics are checked exhaustively:

| `input` | result |
| --- | --- |
| `None` (key absent) | `def()` |
| `Some(None)` (explicit null) | `None` — the default is **not** consulted |
| `Some(Some(v))` where `v == T::default()` | `None` — the type's own default is the "disable this" sentinel |
| `Some(Some(v))` otherwise | `Some(v.clone())` |

`def` is a closure, called at most once and only in the first row.

## 17. `tor-basic-utils` — filter counting

```rust
pub mod tor_basic_utils::iter;

#[derive(Copy, Clone, Default, Debug, Eq, PartialEq)]
pub struct FilterCount {
    pub n_accepted: usize,
    pub n_rejected: usize,
}
impl FilterCount {
    pub fn count(&mut self, accept: bool) -> bool;
    pub fn display_frac_rejected(&self) -> DisplayFracRejected<'_>;
}

pub trait IteratorExt: Iterator {
    fn filter_cnt<P>(self, count: &mut FilterCount, pred: P) -> CountingFilter<'_, P, Self>
        where Self: Sized, P: FnMut(&Self::Item) -> bool;
}
impl<I> IteratorExt for I where I: Iterator { .. }    // blanket
```

Both fields are **public** — the driver constructs a `FilterCount` with literal
field values and reads them back.

Behaviour checked:

- `filter_cnt` **resets** the counter to `FilterCount::default()` when it is
  installed, so a reused counter never accumulates stale totals;
- counting is lazy: only items the iterator actually examined are tallied, so
  taking one element out of `(1..=100).filter_cnt(.., |x| x % 10 == 0)` leaves
  `n_accepted = 1, n_rejected = 9`;
- `count(accept)` tallies and returns its argument, for filters written by
  hand;
- `display_frac_rejected()` renders as `"{n_rejected}/{n_accepted + n_rejected}"`
  — `"5/10"` for five of ten, `"0/0"` for an empty counter (not a division by
  zero).

## 18. `fs-mistrust` — filesystem trust (inherited from step 09)

```rust
pub use dir::CheckedDir;
pub use err::Error;
pub use user::{TrustedGroup, TrustedUser};

impl Mistrust {
    pub fn builder() -> MistrustBuilder;
    pub fn new() -> Self;
    pub fn new_dangerously_trust_everyone() -> Self;
    pub fn verifier(&self) -> Verifier<'_>;
    pub fn check_directory<P: AsRef<Path>>(&self, dir: P) -> Result<()>;
    pub fn make_directory<P: AsRef<Path>>(&self, dir: P) -> Result<()>;
}

impl MistrustBuilder {
    pub fn ignore_prefix(&mut self, path: PathBuf) -> &mut Self;
    pub fn trust_group(&mut self, g: impl Into<TrustedGroup>) -> &mut Self;  // unix only
    pub fn trust_user(&mut self, u: impl Into<TrustedUser>) -> &mut Self;    // unix only
    pub fn trust_no_group_id(&mut self) -> &mut Self;                        // unix only
    pub fn trust_admin_only(&mut self) -> &mut Self;                         // unix only
    pub fn dangerously_trust_everyone(&mut self) -> &mut Self;
    pub fn build(&self) -> Result<Mistrust, ConfigBuildError>;
    ..
}

impl<'a> Verifier<'a> {
    pub fn require_file(self) -> Self;
    pub fn require_directory(self) -> Self;
    pub fn permit_readable(self) -> Self;
    pub fn all_errors(self) -> Self;
    pub fn check<P: AsRef<Path>>(self, path: P) -> Result<()>;
    pub fn make_directory<P: AsRef<Path>>(self, path: P) -> Result<()>;
    pub fn secure_dir<P: AsRef<Path>>(self, path: P) -> Result<CheckedDir>;
    pub fn make_secure_dir<P: AsRef<Path>>(self, path: P) -> Result<CheckedDir>;
}

impl CheckedDir {
    pub fn as_path(&self) -> &Path;
    pub fn join<P: AsRef<Path>>(&self, path: P) -> Result<PathBuf>;
    pub fn make_directory<P: AsRef<Path>>(&self, path: P) -> Result<()>;
    pub fn open<P: AsRef<Path>>(&self, path: P, options: &OpenOptions) -> Result<File>;
    pub fn read_to_string<P: AsRef<Path>>(&self, path: P) -> Result<String>;
    pub fn read<P: AsRef<Path>>(&self, path: P) -> Result<Vec<u8>>;
    pub fn write_and_replace<P: AsRef<Path>, C: AsRef<[u8]>>(&self, path: P, contents: C)
        -> Result<()>;
}
```

```rust
pub enum fs_mistrust::Error {
    NotFound(PathBuf),
    BadPermission(PathBuf, u32, u32),
    BadOwner(PathBuf, u32),
    BadType(PathBuf),
    InvalidSubdirectory,
    ..
}
impl Error {
    pub fn path(&self) -> Option<&Path>;
    pub fn is_bad_permission(&self) -> bool;
}
```

**The group axis decides which bits are forbidden, and it is not implicit.**
`MistrustBuilder` defaults `trust_group` to `TrustedGroup::SelfNamed`, which
resolves through the *host's* group database: a process whose user has no
identically-named group resolves it to `TrustedGroup::None`. The trust
predicate then masks a final directory with `0o077` instead of `0o022`, so the
same mode can be accepted on one machine and rejected on another. The group
that owns a tree is pinned by naming it — `trust_group(gid)` takes any
`impl Into<TrustedGroup>`, and `TrustedGroup::Id(u32)` is a pure numeric path
that never reads `/etc/group`. Under the checker's identity the running user is
`nobody`, which has no self-named group.

The predicate, verbatim, is what the cases below assert against:

```rust
let mut forbidden_bits = if !self.readable_okay && path_type == PathType::Final {
    0o077                       // an unreadable final object forbids all group and world bits
} else {
    if meta.is_dir() && meta.mode() & STICKY_BIT != 0 && path_type == PathType::Intermediate {
        0o000                   // a sticky intermediate only its owner can change
    } else {
        0o022               // otherwise the group-write and world-write bits
    }
};
#[cfg(not(target_os = "ios"))]
if self.mistrust.trust_group == Some(meta.gid()) {
    forbidden_bits &= !0o070;   // a trusted group is relieved of the group bits
}
let bad_bits = meta.mode() & forbidden_bits;
```
`readable_okay` is false by default and is set by `Verifier::permit_readable`;
the modes below are all asserted with it false, so the first arm applies to a
final directory and the third to everything else.

Three consequences follow, and each is checked:

- A **trusted** group (`trust_group(gid)` matching the tree's owner) accepts a
  `0o750` final directory and a `0o640` file, because `0o070` is cleared from
  the mask. It also accepts a `0o770` *intermediate* directory — the same
  clearance applies on that path — but still rejects a `0o777` one, since the
  world-write bit `0o002` survives.
- An **untrusted** group (`trust_group(TrustedGroup::None)`) rejects a `0o750`
  final directory with `BadPermission(path, 0o750, 0o050)` and a `0o640` file
  with `BadPermission(path, 0o640, 0o040)`. A `0o700` path is unaffected by
  either setting.
- `trust_admin_only` sets *both* `trust_user` and `trust_group` to `None`;
  `trust_no_group_id` sets only `trust_group`.

Behaviour checked: `0o700` is accepted for a directory regardless of the group
axis; `0o777` and `0o702` are rejected with `is_bad_permission()` true and
`path()` naming the offending directory; a missing path is `NotFound` with
`is_bad_permission()` **false**; `require_file()`/`require_directory()`
mismatches are `BadType`; `make_directory` creates `0o700` and is idempotent; a
`CheckedDir` refuses `join("..")`, `join("../escape")` and `join("/etc/passwd")`
with `InvalidSubdirectory` while accepting a plain relative name; and
`write_and_replace` / `read_to_string` round-trip through it.

## 19. `safelog` — sensitive values (inherited from step 08)

```rust
pub use err::Error;
pub use flags::{disable_safe_logging, enforce_safe_logging,
                with_safe_logging_suppressed, Guard};

pub struct Sensitive<T>(T);        // Deref, DerefMut, Clone, Default, Eq,
                                   // Hash, Ord, PartialEq, PartialOrd (bounded)
impl<T> Sensitive<T> {
    pub fn new(value: T) -> Self;
    pub fn unwrap(sensitive: Sensitive<T>) -> T;   // associated fn, not a method
}
pub fn sensitive<T>(value: T) -> Sensitive<T>;
impl<T> From<T> for Sensitive<T> { .. }
```

`Display`, `Debug`, `Binary`, `Octal`, `LowerHex`, `UpperHex`, `LowerExp`,
`UpperExp` and `Pointer` must **all** be forwarded through the same scrubbing
path — `{:x}` and `{:o}` on a `Sensitive<u32>` print `[scrubbed]`, not the
number — and each of them **delegates to the inner `T`'s impl of the same
trait** whenever unsafe logging is in force (§5.3's process-global flag being
negative, or the current thread being inside `with_safe_logging_suppressed`),
so a wrapped value formats exactly as it would unwrapped. Scrubbing is a
formatting-time behaviour only: `*s`, `s.len()` and `Sensitive::unwrap(s)` all
see the real value.

```rust
pub fn disable_safe_logging() -> Result<Guard, Error>;
pub fn enforce_safe_logging() -> Result<Guard, Error>;
pub fn with_safe_logging_suppressed<T, F: FnOnce() -> T>(f: F) -> T;

pub enum safelog::Error {
    AlreadySafe,    // "Cannot enable unsafe logging: safe logging is already enforced"
    AlreadyUnsafe,  // "Cannot enforce safe logging: unsafe logging is already enabled"
    Overflow,
}
```

The global switches are mutually exclusive and guard-scoped: while a
`disable_safe_logging` guard is alive, `enforce_safe_logging()` fails with
`AlreadyUnsafe`, and vice versa with `AlreadySafe`. Dropping the guard restores
the previous state exactly. `with_safe_logging_suppressed` is a *local* escape
hatch that keeps working even under enforcement, and passes the closure's
return value through unchanged.

## 20. Requirement groups and the items they use

Every group this step uses, and the sections above it exercises. A group is
checked only through items named in this document; anything reachable but
undocumented is out of scope.

| Group | Kind | Items |
|---|---|---|
| `writeable` | requirement | §1 — `Writeable`, `WriteableOnce`, `Writer`, `NestedWriter::finish`, `EncodeError` |
| `nestedread` | requirement | §2 — `Reader::read_nested_{u8,u16,u32}len`, `take_rest`, `Error::{Truncated, ExtraneousBytes}` |
| `cellencode` | requirement | §3 — `Versions::{new, encode_for_handshake}`, `RelayCell::{encode, decode}`, `Data::{MAXLEN, new}`, `ChannelCodec` |
| `cellerrctx` | requirement | §4 — `tor_cell::Error` variants, their `Display`, `source()` and `HasKind`; §3 for the decode paths that raise them |
| `protoerrctx` | requirement | §5 — `tor_proto::Error` struct variants, `ResolveError`, `HasKind`, `From<Error> for io::Error` |
| `persisterr` | requirement | §6 — `tor_persist::Error`, `ErrorSource` strings, `StateMgr`, `FsStateMgr`, `LockStatus` |
| `certcheck` | requirement | §7 — `Ed25519Cert::decode`, `KeyUnknownCert::check_key`, `CertError`, `expiry`; §8 for `SelfSigned`/`Timebound` |
| `timeliness` | requirement | §9 — `Timeliness`, `NetDirProvider`, `UpcastArcNetDirProvider`, `tor_netdir::Error` kinds, `tor_dirmgr::Timeliness` |
| `dirblockage` | requirement | §10 — `DirBlockage`, `DirBootstrapStatus`, `BootstrapStatus`, `Blockage`, `BlockageKind`, `ConnBlockage` |
| `dirtolerance` | requirement | §11 — `DirTolerance`, `TorClientConfig::directory_tolerance`, `resolve_return_unrecognized` |
| `reachaddrs` | requirement | §12 — `AddrPortPattern`, `PathConfigBuilder::reachable_addrs`, `GuardFilter`, `OwnedChanTarget::restrict_addr` |
| `netdocerr` | requirement | §13 — `tor_netdoc::Error::{parse_error_kind, within, at_pos}`, `ParseErrorKind`, `Pos`, `TorVersion`, `PortPolicy` |
| `mistrustbits` | requirement | `fs-mistrust` — the third field this step adds to `Error::BadPermission`: the rejected mode masked by the bits the configured policy forbids, and how `trust_group` narrows it |
| `accelbackend` | requirement | §15 — `tor-llcrypto` `with-openssl`/`with-sha1-asm`, `arti-client` and `arti` `accel-*` |
| `fullfeat` | requirement | §15 — the `full` feature on `arti`, `arti-client`, `tor-dirmgr`, `tor-netdoc` |
| `cfgoption` | requirement | §16 — `tor_config::resolve_option` |
| `filtercount` | requirement | §17 — `FilterCount`, `IteratorExt::filter_cnt`, `display_frac_rejected` |
| `certgenfeat` | requirement | §15 — `tor-cert` `encode`/`experimental`/`default` and the optional `derive_builder` dependency |
| `llcryptodigest` | gate | §14 — `tor_llcrypto::d::{Sha1, Sha256, Sha512, Sha3_256, Shake128, Shake256}` |
| `mistrustbase` | gate | §18 — `Mistrust`, `Verifier`, `CheckedDir`, `fs_mistrust::Error` |
| `timebound` | gate | §8 — `TimerangeBound`, `Timebound`, `TimeValidityError` |
| `safelogbase` | gate | §19 — `Sensitive`, `sensitive`, the logging guards, `safelog::Error` |

The four `inherited` groups are **gate** groups: they add no requirement of
their own, and a single failure among them means the step is not satisfied.
They are not free: they fail if the migrations in this step break them, which
is exactly what they are there to catch.

### Declared here, not driven by any case

The contract above is the surface, not the call list. The items below are named
because later steps or real callers need them and no group in this step reaches
them; implement them as written.

- The re-exports the review identified and no case names:
  `OwnedChanTarget::from_chan_target` and the `arti_client::config::circ`
  module re-export (§12), `BuildError`'s re-export (§13), and the helper
  methods on the encoding types that no case calls directly
  (`into_circid_and_msg`, `into_streamid_and_msg`, `decode_from_reader`,
  `split_from`, `best_shared_link_protocol`).
- `tor_llcrypto::d::Shake128`/`Shake256` beyond the XOF contract
  `llcryptodigest` asserts — `SPEC_NOTES.md` §7 says where their consumers
  arrive — and the two accelerated backends' effects: `accelbackend` checks the
  feature declarations, and nothing is compiled with `with-openssl` or
  `with-sha1-asm` on (§15).
- The `Bug` quirk stated in §`tor_bytes` (`a `Bug` compares equal to nothing`)
  is a property of `tor_error::Bug` that this step preserves and no case
  constructs.
- `Writer::write_u64`/`write_u128`/`write_zeros`' infallible tail: `writeable`
  proves through `AlwaysBad` that `Writer::write` propagates `EncodeError`, but
  no case asserts the infallible methods' behaviour.

One documented rejection is likewise unexercised and is worth naming because it
is the only place a permissive implementation loses nothing: **an unrecognised
certificate extension with the affects-validation flag set must be rejected**
(`SPEC_NOTES.md` §6 states it; no case feeds such a certificate). Implement the
rejection.
