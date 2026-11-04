# TEST_INTERFACE.md — milestone 1, the Zcash wire format core

This document is the contract the tests are written against. Every
module path, type, trait, method, constant and build command the verifier
depends on is named here. If a behaviour is not stated here or in a
specification document named by `instruction.md`, it is not in scope.

## 1. How the verifier reaches your code

The verifier runs twelve groups. Ten run **integration test files** copied
into `/app/zebra-chain/tests/`, from `/app`:

```
cargo test --offline --locked --jobs 4 -p zebra-chain --test <name> -- --test-threads=1
```

If `--offline --locked` cannot resolve against the workspace as you leave it,
the verifier retries with `--offline` alone, then with `--locked` alone, then
with neither, for every cargo invocation it runs (build, check and test
alike); but you should assume the first form is the one that runs. The other
two groups run whole-workspace commands instead of a test file, and gate the
ten above (§2 explains why): `cargo build --offline --locked -p zebra-chain
--tests` and `cargo check --offline --locked --workspace --all-targets`.

Consequences you must respect:

- The tests reach your code **only through the public API of the `zebra_chain`
  library crate**, as an external consumer. Anything the tests use must be
  `pub` and reachable from `zebra_chain::...`. `pub(crate)`, `pub(super)` and
  `#[cfg(test)]` items are invisible to them.
- The tests are compiled against `zebra-chain/Cargo.toml` **as you leave it**.
  They use only dependencies that crate already declares. Do not remove
  existing dependencies from `[dependencies]` or `[dev-dependencies]`.
- The workspace must build **offline**. Every crate the workspace needs is
  already in the image's cargo registry cache, so `--offline --locked`
  against the shipped `Cargo.lock` resolves out of that cache and fetches
  nothing. Do not add a new third-party dependency: a crate that is not in
  the cache cannot resolve offline, and the verifier's fallback would then
  have to reach the registry, checking a dependency graph that is no longer
  the pinned one.
- Do not rename the `zebra-chain` package or change its `lib` target name.
- The ten test-file groups never touch `zebra-test`, so nothing about its
  *contents* is in scope — but it is a `[dev-dependencies]` entry of
  `zebra-chain`, so both build-only commands above still compile it. Leave it
  buildable.
- A group whose driver fails to compile fails for that group. Compilation
  of the tests is part of the contract. The two build-only groups fail for
  themselves, and the workspace-wide `check` in particular reaches code
  well outside these four modules (§2 below).

### How this step is checked

The gate groups are `build` and `workspace-checks`. They add no new
requirement and only veto: if any one of them fails, the step is not satisfied
no matter how much of the new work you completed.

The requirement groups carry this step's new capability and its peripheral
breadth; every case in them is in scope.

The practical consequence at this step: the gate groups only prove that the
workspace still builds and type-checks, so passing them is necessary and
adds nothing. This is the first milestone — there is no earlier step and no
inherited behaviour to preserve (`instruction.md`) — so a workspace that
merely compiles leaves the whole of the new work undone.

## 2. Where the implementation goes

Supply these files (the module tree `zebra-chain/src/lib.rs` already declares):

| Module | Declared as | Must export |
| --- | --- | --- |
| `zebra_chain::serialization` | `pub mod serialization;` | the encode/decode framework, §3 below |
| `zebra_chain::block` | `pub mod block;` | §4 below |
| `zebra_chain::transaction` | `pub mod transaction;` | §5 below |
| `zebra_chain::transparent` | `pub mod transparent;` | §6 below |

Internal file layout inside each module is yours. Submodule paths are **not**
in scope — only the re-exported names at the module root listed below.

Retained code in the workspace — 33 files carrying 35 `use` lines — already
imports 15 distinct paths from these four modules: 13 named items plus the
`block` and `transparent` modules themselves.
`rg -n 'use crate::(block|transaction|transparent|serialization)' /app`
and `rg -n 'use zebra_chain::(block|transaction|transparent|serialization)' /app`
enumerate them; satisfying this document satisfies all of them.

The two build-only groups (§1) compile more than the test drivers exercise:
`build -p zebra-chain --tests` and `check --workspace --all-targets` also
compile the crate's own retained `#[cfg(test)]` modules and the rest of the
workspace's retained test code, and those already call into a small,
fixed piece of `proptest` surface on these four modules that you must keep
providing:

- `#[cfg_attr(test, derive(proptest_derive::Arbitrary))]` (or an equivalent
  hand-written `impl proptest::arbitrary::Arbitrary`) on `block::Header` and
  on `block::Hash` — retained property tests write `any::<block::Header>()`
  and `any::<block::Hash>()`.
- The same, on `Block`, `block::Height`, and `transparent::Output` — a
  retained `zebra-state` test derives `Arbitrary` for a struct with a
  `transparent::Output` and a `block::Height` field, and another calls
  `Block::partial_chain_strategy`, below.
- `Block::partial_chain_strategy(init: zebra_chain::LedgerState, count: usize) -> proptest::strategy::BoxedStrategy<Vec<std::sync::Arc<Block>>>`
  — a retained `zebra-state` test calls it directly (`Block::partial_chain_strategy(LedgerState::new(tip_height, Network::Mainnet), 100)`)
  to build a chain of related blocks for property testing. `LedgerState` is
  already defined at the crate root (`zebra-chain/src/lib.rs`); it is retained
  code, not part of the four modules this document asks you to supply, and
  you do not need to touch it.

None of this is reached by the injected test drivers, and how you shape the
strategies is entirely up to you — only that these names exist, have this
shape, and compile under the `proptest-impl`/`test` configuration the
retained callers already use.

## 3. `zebra_chain::serialization`

```rust
pub trait ZcashSerialize: Sized {
    fn zcash_serialize<W: std::io::Write>(&self, writer: W) -> Result<(), std::io::Error>;
    fn zcash_serialize_to_vec(&self) -> Result<Vec<u8>, std::io::Error>;   // provided
}

pub trait ZcashDeserialize: Sized {
    fn zcash_deserialize<R: std::io::Read>(reader: R) -> Result<Self, SerializationError>;
}

pub trait ZcashDeserializeInto {
    fn zcash_deserialize_into<T>(self) -> Result<T, SerializationError> where T: ZcashDeserialize;
}
impl<R: std::io::Read> ZcashDeserializeInto for R { ... }

#[derive(thiserror::Error, Debug)]
pub enum SerializationError {
    Io(#[from] std::io::Error),
    Parse(&'static str),
    Amount { #[from] source: crate::amount::Error },
}
```

Which variant is produced is part of the contract, not just that some `Err`
comes back: a stream that ends before a fixed-width or length-prefixed field
is fully read is `Io`; a stream that is complete but encodes something the
format forbids (a non-canonical `compactSize`, an out-of-range height, a
malformed coinbase height push, and so on) is `Parse`; a value that decodes
fine as an integer but falls outside the amount range is `Amount`. One
existing exception, stated in full in §7: `Script::zcash_deserialize` (and
anything whose last field is a `Script`) truncates silently on a short read
instead of returning `Io`, because that is what happens when its length-
prefixed body is read with `Read::take(len).read_to_end(..)`.

**Every `zcash_deserialize` is self-delimiting, and where it leaves the reader
is part of the contract.** A successful `zcash_deserialize` consumes exactly
the bytes of the one object it was asked for and leaves the reader positioned
immediately after that object's last byte — no earlier and no later. This
holds for every implementor of the trait, for the blanket `Vec<T>` impl below
(which reads its `compactSize` count and then exactly that many elements,
leaving whatever follows the last element untouched), and for
`ZcashDeserializeInto`, which is only a thin wrapper over
`ZcashDeserialize::zcash_deserialize` and inherits the property from it.

This is load-bearing rather than tidy. Consensus objects are concatenated on
the wire with nothing between them and no outer framing to re-synchronise on:
a block is followed immediately by the next block in a block file, a
transaction by the next transaction inside a block, a `Script` by the sequence
number that follows it in its input, a vector element by the next element.
Where the reader is left is therefore the only thing that makes the *next*
read possible. An implementation that reads ahead — wrapping the caller's
reader in a `BufReader` of its own, or draining the stream to the end and
parsing out of the resulting buffer — returns the correct value for the first
object and then cannot read the second at all. The verifier reads several
objects back to back from one `std::io::Cursor` and checks `Cursor::position()`
after each, so this is observed directly and not inferred from the values that
come back.

Two limits on that rule, so it is not read as saying more than it does. It
describes the **successful** path only: where the reader is left after a
deserialize that returns `Err` is unspecified and not in scope. And the
truncated-`Script` case of the paragraph above is not an exception to it — a
length-prefixed body shorter than its own prefix promises is read to the end
of what was actually there, which is where the object ended, and nothing
follows it to leave the reader in front of.

`ZcashSerialize` and `ZcashDeserialize` are implemented blanket-style for
`Vec<T>`: the vector is length-prefixed with a `compactSize` count followed by
the elements. Deserialization of a `Vec<T>` must **not** pre-allocate from the
attacker-supplied count, and must **not** hang on one either: it reads
elements one at a time from the same reader and stops at the first element the
stream cannot supply, returning `SerializationError::Io` there. A count of
`u64::MAX` therefore costs one failed element read — not an allocation of
`u64::MAX` elements, and not `u64::MAX` iterations of a loop. Both of those
are checked; every other allocation and performance property is not (§7).

Byte-order and framing helpers, on extension traits blanket-implemented for
every `std::io::Read` / `std::io::Write`:

```rust
pub trait ReadZcashExt: std::io::Read {
    fn read_compactsize(&mut self) -> Result<u64, SerializationError>;
    fn read_ip_addr(&mut self) -> std::io::Result<std::net::IpAddr>;
    fn read_socket_addr(&mut self) -> std::io::Result<std::net::SocketAddr>;
    fn read_string(&mut self) -> Result<String, SerializationError>;
    fn read_4_bytes(&mut self) -> std::io::Result<[u8; 4]>;
    fn read_12_bytes(&mut self) -> std::io::Result<[u8; 12]>;
    fn read_32_bytes(&mut self) -> std::io::Result<[u8; 32]>;
    fn read_64_bytes(&mut self) -> std::io::Result<[u8; 64]>;
}
impl<R: std::io::Read + ?Sized> ReadZcashExt for R {}

pub trait WriteZcashExt: std::io::Write {
    fn write_compactsize(&mut self, n: u64) -> std::io::Result<()>;
    fn write_ip_addr(&mut self, addr: std::net::IpAddr) -> std::io::Result<()>;
    fn write_socket_addr(&mut self, addr: std::net::SocketAddr) -> std::io::Result<()>;
    fn write_string(&mut self, string: &str) -> std::io::Result<()>;
    fn write_32_bytes(&mut self, bytes: &[u8; 32]) -> std::io::Result<()>;
    fn write_64_bytes(&mut self, bytes: &[u8; 64]) -> std::io::Result<()>;
}
impl<W: std::io::Write + ?Sized> WriteZcashExt for W {}
```

`compactSize` is the Bitcoin-inherited variable-length integer of protocol
specification §7.1 ("CompactSize"), over the full `u64` range. It is
**canonical**: each of the four forms covers a disjoint range —

| Lead byte | Payload | Encodes |
| --- | --- | --- |
| `0x00`–`0xfc` | none | the lead byte itself, `0` to `0xfc` |
| `0xfd` | `u16` LE | `0x0000_00fd` to `0x0000_ffff` |
| `0xfe` | `u32` LE | `0x0001_0000` to `0xffff_ffff` |
| `0xff` | `u64` LE | `0x1_0000_0000` to `0xffff_ffff_ffff_ffff` |

— and a value encoded in a longer form than it needs must be rejected with
`SerializationError::Parse`. There is no upper bound at this layer: the size
limits that stop an oversized vector are the *element* limits (`MAX_BLOCK_BYTES`
and the reader running out of input), not a cap on the count.

A string is framed the same way a vector is: `write_string` writes a
`compactSize` **byte** count (not a character count) followed by the UTF-8
bytes of the string, and `read_string` reads that count and that many bytes
back. A body that is shorter than its own length prefix is
`SerializationError::Io` — the stream ran out — while a body that arrives in
full but is not valid UTF-8 is `SerializationError::Parse`, which is the §3
discipline above applied to the one place a byte string becomes a `String`.

`read_ip_addr` reads a 16-byte IPv6-mapped address; ports are
big-endian; every other multi-byte integer in this codebase is little-endian.
The reader **unmaps**: sixteen bytes in the IPv4-mapped form
`::ffff:a.b.c.d` come back as `IpAddr::V4(a.b.c.d)`, not as an `IpAddr::V6`
holding the same sixteen bytes, and a genuine IPv6 address comes back as
`IpAddr::V6`. `write_ip_addr` is the inverse: an `IpAddr::V4` is written as
its IPv4-mapped sixteen bytes, so the pair round-trips.

Double-SHA256 helpers:

```rust
pub mod sha256d {
    pub struct Writer { /* .. */ }          // implements std::io::Write
    impl Writer {
        pub fn default() -> Writer;         // via Default
        pub fn finish(self) -> [u8; 32];
    }
    pub struct Checksum(pub [u8; 4]);   // Debug prints `Sha256dChecksum("<hex>")`
    impl<'a> From<&'a [u8]> for Checksum {} // first 4 bytes of SHA256d(input)
}
```

`Writer` implements `Default`, and `finish()` returns
SHA256(SHA256(everything written)). `Checksum::from(&[u8])` is the first four
bytes of that digest.

Serde adapters used by retained crates (their shape is fixed, their contents
are not in scope beyond compiling): `serialization::serde_helpers::{AffinePoint, Fq, Either, BigArray}`,
with `From<AffinePoint> for jubjub::AffinePoint` and `From<Fq> for jubjub::Fq`.
`BigArray` is the `serde-big-array` adapter used as
`#[serde(with = "serde_helpers::BigArray")]` on large fixed-size byte arrays
(e.g. the Equihash solution) that `derive(Serialize, Deserialize)` cannot
handle directly.

## 4. `zebra_chain::block`

```rust
pub struct Height(pub u32);
// Debug prints `Height(<decimal>)` -- the ordinary shape `#[derive(Debug)]`
// gives a one-field tuple struct, e.g. `Height(347499)`. Height has no
// Display; to render just the number, use the inner `u32` (`height.0`).
impl Height {
    pub const MIN: Height;                  // Height(0)
    pub const MAX: Height;                  // Height(499_999_999)
    pub const MAX_AS_U32: u32;              // 499_999_999
}
// impl Add<Height> for Height { type Output = Option<Height>; }
// impl Add<i32>    for Height { type Output = Option<Height>; }
// impl Sub<i32>    for Height { type Output = Option<Height>; }
//   -- these three return None whenever the result would leave MIN..=MAX, or
//      whenever either operand is already outside it (heights are constructed
//      from integers unchecked, so the arithmetic is where the range is
//      enforced).
// impl Sub<Height> for Height { type Output = i32; }
//   -- note the different output: a difference of heights is a signed number
//      of blocks, not a height. It panics rather than returning None if an
//      operand or the result is outside i32.
// FromStr parses a decimal u32 and rejects anything above MAX with
// SerializationError::Parse.

pub struct Hash(pub [u8; 32]);              // a block hash
// Display and FromStr use the *reversed* (big-endian) hex convention that
// block explorers use; Debug prints `block::Hash("<same hex>")`.
// FromStr accepts exactly 64 hex characters and nothing else, and its
// `Err` type is `SerializationError` — a bad hash comes back as
// `SerializationError::Parse`, the same way `Height`'s does above.
// From<&Header> and From<&Block> compute SHA256d over the 1487-byte header.
// ZcashSerialize / ZcashDeserialize write and read the 32 bytes as stored.

pub mod merkle {
    pub struct Root(pub [u8; 32]);
    impl std::iter::FromIterator<crate::transaction::Hash> for Root {}
}
// `Root` is the Bitcoin-inherited transaction merkle root, and the
// FromIterator impl is where it is built: take the block's
// `transaction::Hash` values in block order as the leaves, then SHA256d
// each adjacent pair (the two 32-byte hashes concatenated, in order) to
// form the next level up, and repeat until a single node is left -- that
// node is the root. When a level holds an *odd* number of nodes, the last
// node is paired with itself; it is never dropped, and never padded with
// zeroes. A block with a single transaction therefore has that
// transaction's hash as its root, unchanged. Every value in the tree --
// leaves, internal nodes and the root -- stays in internal (wire) byte
// order, so nothing is reversed at any step and `Root` compares directly
// against the `merkle_root` read out of the header.

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
pub struct Header {
    pub version: u32,
    pub previous_block_hash: Hash,
    pub merkle_root: merkle::Root,
    pub root_bytes: [u8; 32],
    pub time: chrono::DateTime<chrono::Utc>,
    pub difficulty_threshold: crate::work::difficulty::CompactDifficulty,
    pub nonce: [u8; 32],
    pub solution: crate::work::equihash::Solution,
}
// `Copy` is load-bearing, and it is not the derive this shape would suggest:
// a header is 1487 bytes on the wire and about 1.4 kB in memory, so
// `#[derive(Clone, Debug, Eq, PartialEq)]` alone is the idiomatic default for
// a struct this size. The verifier moves `block.header` out of a `Block` and
// then uses that same `Block` again, which compiles only if `Header: Copy`,
// and a group whose driver fails to compile fails for that group (§1).
// `Debug`, `PartialEq` and `Clone` are also relied on; `Eq`, `Serialize` and
// `Deserialize` are listed because the reference derives them and retained
// code may want them, not because a driver reads them.
//
// `version` is not a free field, and the check is in the codec. Reading a
// header starts by reading the version word, and the header is rejected --
// before any other field is read -- when the high bit of that word is set,
// or when the low 31 bits of it are below 4. Both rejections are
// `SerializationError::Parse`. zcashd reads `nVersion` as a signed `i32`, so
// a word with the high bit set is a *negative* version rather than a large
// one, and a node that accepted it would follow a chain the rest of the
// network will not. This is a defensive rejection performed by
// `Header::zcash_deserialize` itself, not something left to a verification
// service further up: a header that fails it never becomes a `Header` at
// all. (The protocol specification states the "at least 4" requirement in
// §7.6; what this document adds is *where* the check is performed, which is
// what the verifier can observe.) The `version` a successful deserialize
// yields is therefore the low 31 bits of the word that was read, and is
// always at least 4.
impl Header {
    pub fn time_is_valid_at(&self, now: DateTime<Utc>, height: &Height, hash: &Hash)
        -> Result<(), BlockTimeError>;      // Err if time > now + 2 hours
}
pub enum BlockTimeError {
    // fields, in order: the header's own (too-late) `time`; the `height` and
    // `hash` `time_is_valid_at` was called with; and the latest time that
    // would still have been accepted (`now` plus 2 hours).
    // derives: Debug and thiserror::Error (the retained `zebra-consensus`
    // code wraps this type in `#[error(transparent)]`, and one driver
    // `{:?}`-formats a `Result<(), BlockTimeError>`, so `Debug` must exist;
    // its wording is out of scope, per §7).
    InvalidBlockTime(DateTime<Utc>, Height, Hash, DateTime<Utc>),
}

pub struct CountedHeader { pub header: Header, pub transaction_count: usize }
// On the wire: the 1487 header bytes followed by the transaction count as a
// `compactSize` -- i.e. exactly the prefix of the block the header belongs
// to, up to and including its transaction count, and nothing after it.

pub struct Block {
    pub header: Header,
    pub transactions: Vec<std::sync::Arc<crate::transaction::Transaction>>,
}
impl Block {
    pub fn coinbase_height(&self) -> Option<Height>;
    pub fn hash(&self) -> Hash;
    pub fn root_hash(&self, network: crate::parameters::Network) -> Option<RootHash>;
}
// impl Display for Block: exactly
//   format!("Block {{ height: Height({}), hash: {} }}", height, hash)
// where `height` is the *bare decimal* inside the block's own coinbase
// height -- the `u32` at `height.0`. The literal `Height(` in the format
// string already supplies the wrapper, so do **not** feed the `Height`
// itself to `{:?}` here: that renders `Height(Height(347499))` and fails.
// `hash` is `self.hash()` printed the way `block::Hash`'s own Display
// prints it (reversed hex). This is checked byte-for-byte, not just
// "mentions the height and the hash".

pub enum RootHash {
    PreSaplingReserved([u8; 32]),
    FinalSaplingRoot(crate::sapling::tree::Root),
    ChainHistoryActivationReserved([u8; 32]),
    ChainHistoryRoot(ChainHistoryMmrRootHash),
}
pub struct ChainHistoryMmrRootHash([u8; 32]);
// Both of these derive Clone, Copy, Debug, Eq, PartialEq, Serialize and
// Deserialize. Of those, the verifier relies only on `Debug` *existing* -- it
// formats a `RootHash` with `{:?}` inside a panic message on the failure
// path. `RootHash` is not one of the three types §7 lists as having an
// in-scope `Debug` shape, so what its `Debug` prints is not checked.
// `sapling::tree::Root` is `Root(pub [u8; 32])`: the verifier reads `.0`.

pub const MAX_BLOCK_BYTES: u64 = 2_000_000;
```

Wire format (protocol specification §7.6, and §7.1 for the transaction
vector): the header is exactly **1487 bytes** — `version` (4, LE),
`previous_block_hash` (32), `merkle_root` (32), `root_bytes` (32), `time`
(4, LE, seconds since the epoch), `difficulty_threshold` (4, LE),
`nonce` (32), `solution` (a `compactSize` length of 1344 followed by 1344
bytes). A block is the header followed by a `compactSize` transaction count
and that many transactions. A block that serializes to more than
`MAX_BLOCK_BYTES` must fail to deserialize, and so must a header whose
version word fails the rule stated at the `Header` declaration above. A
`CountedHeader`'s `transaction_count`, by contrast, is **not** bounded at this
milestone: it is a plain `compactSize` and §3's "no upper bound at this layer"
applies to it unchanged, so no count is rejected for being large, and nothing
here asks you to decide which counts are plausible.

`coinbase_height` reads the height from the first input of the first
transaction when that input is `transparent::Input::Coinbase`, and is `None`
otherwise. `root_hash` is `None` exactly when `coinbase_height` is — it has
no height to dispatch on — and otherwise interprets the same 32 `root_bytes`
according to the network upgrade active at that height, via
`crate::parameters::NetworkUpgrade::current(network, height)`. The reserved
header field is read four different ways, and this is the whole mapping
(protocol specification §7.6's header field table, and ZIP-221 for the
activation-block case):

| `NetworkUpgrade::current(network, height)` | `root_hash` returns |
| --- | --- |
| `Genesis`, `BeforeOverwinter`, `Overwinter` | `PreSaplingReserved(root_bytes)` |
| `Sapling`, `Blossom` | `FinalSaplingRoot(sapling::tree::Root(root_bytes))` |
| `Heartwood`, **at exactly Heartwood's own activation height** | `ChainHistoryActivationReserved(root_bytes)` |
| `Heartwood` above that height, and `Canopy` | `ChainHistoryRoot(ChainHistoryMmrRootHash(root_bytes))` |

The third row is the one that is easy to miss and is checked: ZIP-221 sets the
activation block's field to all zeroes rather than to a chain-history root,
because the root at that height would have to commit to the whole preceding
epoch, so the activation block alone is `ChainHistoryActivationReserved` and
the block after it is already `ChainHistoryRoot`. At this milestone the
reading is infallible — the reserved variants carry whatever bytes the header
holds and nothing is validated — so no row here can fail.

## 5. `zebra_chain::transaction`

```rust
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub enum Transaction {
    V1 { inputs: Vec<transparent::Input>, outputs: Vec<transparent::Output>,
         lock_time: LockTime },
    V2 { inputs: .., outputs: .., lock_time: LockTime,
         joinsplit_data: Option<JoinSplitData<crate::primitives::Bctv14Proof>> },
    V3 { inputs: .., outputs: .., lock_time: LockTime,
         expiry_height: crate::block::Height,
         joinsplit_data: Option<JoinSplitData<crate::primitives::Bctv14Proof>> },
    V4 { inputs: .., outputs: .., lock_time: LockTime,
         expiry_height: crate::block::Height,
         value_balance: crate::amount::Amount<crate::amount::NegativeAllowed>,
         joinsplit_data: Option<JoinSplitData<crate::primitives::Groth16Proof>>,
         shielded_data: Option<ShieldedData> },
}
// `value_balance` is the one *signed* amount in a transaction, and the
// constraint is load-bearing rather than decorative: `Amount` without a
// constraint parameter means `Amount<NegativeAllowed>` (`Amount<C =
// NegativeAllowed>` is the declared default -- §5's `Amount<NegativeAllowed>`
// in this field and `Amount` bare are the same type, so write it either way),
// and it is the pair of marker types that decides which of the two ranges a
// value is checked against. `NegativeAllowed::valid_range()` is
// `-MAX_MONEY..=MAX_MONEY` and `NonNegative::valid_range()` is
// `0..=MAX_MONEY`; `transparent::Output`'s `value` (§6) is the `NonNegative`
// one. `valueBalance` is the net value moving out of the Sapling pool, so it
// is negative whenever more value is shielded than unshielded. nu5 §7.1's
// transaction-format table gives this field (there named `valueBalanceSapling`,
// and always present for a v5-era reading of a v3/v4 transaction) the data type
// `int64`, and §7.1.2's consensus rules require it to lie in
// `{ -MAX_MONEY .. MAX_MONEY }` from the Sapling upgrade onward -- the same
// range `NegativeAllowed::valid_range()` states. ZIP-243 serializes the same
// field as an 8-byte little-endian signed integer (its `valueBalance`, pre-NU5
// name for this same field), and the `NegativeAllowed` reader is what admits
// the negative values a pool balance can really take. The two constraints are
// not interchangeable: the same eight wire bytes are a valid `NegativeAllowed`
// amount and an out-of-range `NonNegative` one, which is why the reader for
// this field must admit negative values and the reader for an output value
// must not.
impl Transaction {
    pub fn hash(&self) -> Hash;
    pub fn inputs(&self) -> &[transparent::Input];
    pub fn outputs(&self) -> &[transparent::Output];
    pub fn lock_time(&self) -> LockTime;
    pub fn expiry_height(&self) -> Option<crate::block::Height>;
    pub fn sprout_nullifiers(&self) -> Box<dyn Iterator<Item = &crate::sprout::Nullifier> + '_>;
    pub fn sapling_nullifiers(&self) -> Box<dyn Iterator<Item = &crate::sapling::Nullifier> + '_>;
    pub fn contains_coinbase_input(&self) -> bool;
    pub fn is_coinbase(&self) -> bool;   // exactly one input, and it is Coinbase
    pub fn sighash(&self,
                   network_upgrade: crate::parameters::NetworkUpgrade,
                   hash_type: HashType,          // transaction::HashType; see §5's sighash module below
                   input: Option<(u32, transparent::Output)>) -> blake2b_simd::Hash;
}

pub struct Hash(pub [u8; 32]);          // a transaction id
// From<&Transaction> is SHA256d of the serialized transaction.
// Display/FromStr use reversed hex; Debug prints `transaction::Hash("<hex>")`.

#[derive(Copy, Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
pub enum LockTime { Height(crate::block::Height), Time(DateTime<Utc>) }
impl LockTime {
    pub const MIN_TIMESTAMP: i64 = 500_000_000;
    pub const MAX_TIMESTAMP: i64 = u32::MAX as i64;
    pub fn min_lock_time() -> LockTime;   // Time(MIN_TIMESTAMP) -- see below
    pub fn max_lock_time() -> LockTime;   // Time(MAX_TIMESTAMP)
}
// Wire: a single u32 LE. Values < 500_000_000 are a Height, others a Time.
// Both constructors return the `Time` variant: they bracket the timestamp
// range, not the whole `LockTime` range. `min_lock_time()` is the *first*
// value that is a timestamp at all -- `Time(500_000_000)`, 1985-11-05 --
// and is therefore larger than lock times like `Height(Height(0))` that
// sit below the threshold. It is not a minimum over `LockTime` as a whole,
// and nothing orders the two variants against each other.
// `Copy` is load-bearing here too: the verifier binds one `LockTime` and
// then moves it into three separate `Transaction::V1` literals, which
// compiles only if `LockTime: Copy`. `Transaction`'s own `PartialEq` and
// `Debug` are load-bearing as well -- the verifier compares whole
// transactions with `==` and formats one with `{:?}` when a comparison
// fails. `Eq`, `Serialize` and `Deserialize` are listed because the
// reference derives them, not because a driver reads them.

pub struct Memo(/* 512 bytes, private */);
impl<'a> std::convert::TryFrom<&'a [u8]> for Memo { type Error = &'static str; }
// Shorter input is zero-padded to 512; longer input is an error.

pub struct JoinSplitData<P: crate::primitives::ZkSnarkProof> {
    pub first: crate::sprout::JoinSplit<P>,
    pub rest: Vec<crate::sprout::JoinSplit<P>>,
    pub pub_key: crate::primitives::ed25519::VerificationKeyBytes,
    pub sig: crate::primitives::ed25519::Signature,
}
impl<P: ZkSnarkProof> JoinSplitData<P> {
    pub fn joinsplits(&self) -> impl Iterator<Item = &JoinSplit<P>>;
}
// Wire: `compactSize` count, then that many JoinSplits, then (only if the
// count was non-zero) the 32-byte pub_key and 64-byte sig. A zero count
// deserializes to `None`, not to an empty JoinSplitData.

pub struct ShieldedData {
    pub first: crate::serialization::serde_helpers::Either<crate::sapling::Spend,
                                                           crate::sapling::Output>,
    pub rest_spends: Vec<crate::sapling::Spend>,
    pub rest_outputs: Vec<crate::sapling::Output>,
    pub binding_sig: redjubjub::Signature<redjubjub::Binding>,
}
impl ShieldedData {
    pub fn spends(&self) -> impl Iterator<Item = &Spend>;
    pub fn outputs(&self) -> impl Iterator<Item = &Output>;
    pub fn nullifiers(&self) -> impl Iterator<Item = &crate::sapling::Nullifier>;
    pub fn note_commitments(&self) -> impl Iterator<Item = &jubjub::Fq>;
    pub fn binding_verification_key(
        &self,
        value_balance: crate::amount::Amount<crate::amount::NegativeAllowed>,
    ) -> redjubjub::VerificationKeyBytes<redjubjub::Binding>;
}
// PartialEq/Eq compare the *sets* of spends and outputs plus the binding
// signature, not the `first`/`rest` split.

pub mod sighash {
    bitflags::bitflags! {
        pub struct HashType: u32 {
            const ALL          = 0b0000_0001;
            const NONE         = 0b0000_0010;
            const SINGLE       = 0b0000_0011;
            const ANYONECANPAY = 0b1000_0000;
        }
    }
}
pub use sighash::HashType;   // re-exported at the module root -- see §2
```

`HashType` is checked at `transaction::HashType` (the root re-export §2 already
promises for every module), not at `transaction::sighash::HashType`; keep
both paths working (the re-export does that for free) if you also want it
reachable at the submodule path used above.

Transaction wire format is protocol specification §7.1. Version 1 and 2 use
the pre-Overwinter (Bitcoin-style) SHA256d signature hash. Version 3 uses
**ZIP-143** with version group id `0x03C48270`, and version 4 uses **ZIP-243**
with version group id `0x892F2085`; both produce a BLAKE2b-256 digest under a
**16-byte personalisation built by concatenation** (`zip-0143.rst` gives it as
`"ZcashSigHash" || CONSENSUS_BRANCH_ID`, and writes it out again in its
reference code): the twelve ASCII bytes `ZcashSigHash` followed by the
consensus branch id of the network upgrade passed to `sighash`, written as a
little-endian `u32`. It is not a XOR or any other combination — 12 + 4 is
exactly the 16 bytes BLAKE2b takes. The `input` argument is
`Some((index, previous_output))` when signing a transparent input and `None`
for a shielded-only signature.

The version word is the first four bytes of a transaction and the reader
dispatches on it before anything else: its high bit is `fOverwintered` and its
low 31 bits are the version number. Only four combinations are defined here —
version 1 and version 2 **without** the flag, and version 3 and version 4
**with** it — and every other combination is rejected with
`SerializationError::Parse`, including version 0, versions 1 and 2 carrying
the flag, versions 3 and 4 without it, and any version above 4 with or
without it. When the flag is set, the four bytes after the version word are
the version group id, and it too is checked before the body is read: exactly
`0x03C48270` for a version 3 transaction and exactly `0x892F2085` for a
version 4 one. Any other value is `SerializationError::Parse`, including the
*other* defined id.

**`scriptCode`, the one exception to §6's `Script` convention.** ZIP-143's
signature preimage includes `scriptCode`, defined there as the spent output's
script *as it is serialized on the wire* — its compactSize length prefix
included. `sighash` writes `previous_output.lock_script.0` straight into the
hash preimage and adds no prefix of its own, so — for this call only — the
`Script` you build for `previous_output` must already carry that length
prefix *inside* `.0`: construct it as `compactsize(raw_script.len()) ++
raw_script`, not as `raw_script` alone. That is the opposite of what
`Script::zcash_deserialize` produces (§6: reading a `Script` off the wire
strips the prefix into a bare `.0`), and it is unique to the `Script` passed
into `sighash` this way; every other `Script` in this document — including
`transparent::Input::PrevOut::unlock_script` and any `Script` you read off
the wire — follows §6's general rule unchanged.

## 6. `zebra_chain::transparent`

```rust
#[derive(Copy, Clone, Debug, Eq, PartialEq, Serialize, Deserialize, Hash)]
pub struct OutPoint { pub hash: crate::transaction::Hash, pub index: u32 }

#[derive(Clone, Eq, PartialEq, Serialize, Deserialize)]   // Debug: see below
pub struct CoinbaseData(/* private Vec<u8> */);
impl AsRef<[u8]> for CoinbaseData {}

#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
pub enum Input {
    PrevOut { outpoint: OutPoint, unlock_script: Script, sequence: u32 },
    Coinbase { height: crate::block::Height, data: CoinbaseData, sequence: u32 },
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize, Hash)]
pub struct Output { pub value: crate::amount::Amount<crate::amount::NonNegative>,
                    pub lock_script: Script }

#[derive(Clone, Eq, PartialEq, Serialize, Deserialize, Hash)]   // Debug: see below
pub struct Script(pub Vec<u8>);   // opaque bytes, length-prefixed on the wire
// Exception: the `Script` passed as part of `previous_output` to
// `Transaction::sighash` (§5) must already hold that length prefix *inside*
// `.0` -- `sighash` writes `.0` verbatim and adds no prefix of its own. See
// §5's "scriptCode" note. Every other use of `Script` follows the rule above.

pub enum Address {
    PayToScriptHash    { network: crate::parameters::Network, script_hash:  [u8; 20] },
    PayToPublicKeyHash { network: crate::parameters::Network, pub_key_hash: [u8; 20] },
}
// Address implements Display, FromStr, ZcashSerialize and ZcashDeserialize.
```

Which of those derives the verifier leans on: `Clone` on `Input` and `Output`
(it clones inputs and calls `.to_vec()` on an output slice), `PartialEq` on
`Input`, `OutPoint`, `Output` and `Script` (it compares re-parsed values, and
whole `Vec<Input>`s, with `==`), and `Debug` on all four (it formats the
actual value into the failure message of those comparisons). `Debug` only has
to *exist*: §7 checks `Debug` wording for exactly three types, and none of
these is one of them, so a hand-written impl is as good as a derived one.
`Script` and `CoinbaseData` are the two that the reference writes by hand
rather than deriving — both print a hex rendering of their bytes rather than
the byte vector — which is why their derive lines above omit `Debug`. `Eq`,
`Hash`, `Serialize` and `Deserialize` are printed because the reference
derives them and retained code may want them, not because a driver reads
them.

An `Input::Coinbase` is recognised by an outpoint whose hash is all zeroes and
whose index is `0xFFFF_FFFF`; its script carries the BIP-34 height encoding
followed by free data, and the whole script (height push plus data) must be at
most 100 bytes.

**The other half of nu5 §7.1.2's length rule is out of scope here.** That
section requires a coinbase script to have a length in `{2 .. 100}` bytes, and
only the upper bound is a deserialization obligation at this milestone. The
minimum of 2 is **not** checked by `Input::zcash_deserialize` and is not
in scope: a one-byte script consisting of nothing but an `OP_1`..`OP_16` height
push deserializes successfully, as `Input::Coinbase` with that height and an
empty `CoinbaseData`, and you are not asked to reject it. (An *empty* script
is still rejected, but by the push-encoding clause below rather than by any
length rule — there is no first byte to be a height push.) Enforcing the
minimum is a whole-transaction consensus check, not a codec check, and this
milestone ships no consensus verifier to put it in; what the codec enforces is
the 100-byte cap and the four clauses that follow.

A coinbase script is rejected with `SerializationError::Parse` (§3) when: its
data plus the height push exceeds 100 bytes; its outpoint's index is anything
other than `0xFFFF_FFFF` (an all-zero-hash outpoint is otherwise only
meaningful as a coinbase); its leading bytes are not one of the BIP-34 push
encodings (a minimal-length push of the height, big enough to hold it, using
the `OP_1`..`OP_16` short forms for heights 1 through 16); or the height push
decodes to a value above `Height::MAX`. This is the same rule stated in nu5
§7.1.2, made explicit here because the specification gives the *encoding*
without enumerating every input shape that fails to be one. A three-byte
script `00 00 00`, for instance, is a `Parse` error under the third clause:
`0x00` is not one of the push encodings the rule admits.

**The one exception, and the exact rule that separates it from the rejections
above (protocol specification §7.12).** BIP-34 does not apply to the Mainnet
and Testnet genesis blocks, whose coinbase script is not a height push at all.
The two networks share the *same* 77-byte script, and it is **those exact
bytes** — not "any 77-byte script", not "any script whose push decodes out of
range" — that `Input::zcash_deserialize` special-cases. That distinction is
load-bearing, because `Input::zcash_deserialize` is handed script bytes and
nothing else: no height, no network, no surrounding block. The discriminator
is therefore a whole-value comparison of the script against this one constant,
which is `04 ff ff 07 1f 01 04 45` followed by the 69 ASCII bytes
`Zcash0b9c4eef8b7cc417ee5001e3500984b6fea35683a7cac141a043c42064835d34`:

```text
04ffff071f0104455a6361736830623963346565663862376363343137656535303031
653335303039383462366665613335363833613763616331343161303433633432
303634383335643334
```

(77 bytes; the line breaks above are for legibility only. The same bytes are
on the chain, in the two retained genesis vectors
`zebra-test/src/vectors/block-main-0-000-000.txt` and
`block-test-0-000-000.txt`, so you can extract them from the block rather than
trusting this transcription.)

**Precedence.** The equality test above is applied *first*, ahead of the
rejection rules. It has to be: the genesis script begins with `0x04`, the
four-byte push opcode, and the four bytes after it decode to 520,617,983 —
above `Height::MAX`, so the last clause of the rejection list would otherwise
reject the genesis block outright. When the script matches the constant
exactly, the input deserializes as `Input::Coinbase` with `height ==
Height(0)` and **all 77 bytes retained** in `CoinbaseData` (nothing is split
off as a height), it re-serializes to those same 77 bytes with no height push
written back, and `Block::coinbase_height` on either genesis block reports
`Some(Height(0))`. Any script that does *not* match the constant — including
some other 77-byte script — is judged by the BIP-34 rules above with no
exception made for it: accepted if it is a well-formed minimal height push
within range, rejected with `Parse` otherwise. `00 00 00` is in the second
category. Falling back to `Height(0)` whenever a BIP-34 parse fails is
therefore **not** a conforming reading of this section: it would accept inputs
the rejection list requires you to reject, and the rejection list is checked.

`Address` uses Base58Check (protocol specification §5.6.1) with these
two-byte version prefixes, which are **not** required to be public constants:

| Network | P2PKH prefix | P2SH prefix |
| --- | --- | --- |
| Mainnet | `0x1C 0xB8` (`t1...`) | `0x1C 0xBD` (`t3...`) |
| Testnet | `0x1D 0x25` (`tm...`) | `0x1C 0xBA` (`t2...`) |

`Display` and `FromStr` round-trip through that encoding, and
`ZcashSerialize`/`ZcashDeserialize` write the two-byte prefix followed by the
20-byte hash with no checksum. The payload hash is `RIPEMD160(SHA256(payload))`
— of the serialized secp256k1 public key for `PayToPublicKeyHash`, and of the
script bytes for `PayToScriptHash`.

## 7. Explicit non-requirements

Out of scope, and not read — except where a bullet says otherwise:

- internal module file layout, private item names, field ordering inside
  private types, and which submodule a public name is *defined* in (only the
  path it is re-exported at matters);
- `Debug` wording, and error message text (the `&'static str` inside
  `SerializationError::Parse`, and anything `#[error("...")]` renders) — with
  the exceptions this document states explicitly. *Which*
  `SerializationError` variant comes back is checked where §3 and §6 say so.
  `Display` is checked for the types this document gives an exact format for
  (`block::Hash`, `transaction::Hash`, `transparent::Address`, `Block`,
  `Height`'s `FromStr` round trip). And `Debug` *is* checked, byte for byte,
  for exactly three of the types you supply: `block::Hash`, which prints
  `block::Hash("<reversed hex>")` (§4); `transaction::Hash`, which prints
  `transaction::Hash("<reversed hex>")` (§5); and `Height`, which prints
  `Height(<decimal>)`, e.g. `Height(347499)` (§4). Each is exactly what
  `#[derive(Debug)]` emits for that shape, so deriving is enough, and a
  hand-written `Debug` that prints anything else -- a bare decimal for
  `Height`, say -- is checked as wrong. `Debug` wording is out of scope for every
  other type you supply, including `sha256d::Checksum`, whose shape §3
  describes for orientation only. (The verifier does also read the `Debug`
  output of the *retained* `work::equihash::Solution`,
  `work::difficulty::CompactDifficulty` and
  `work::difficulty::ExpandedDifficulty`, but §2 does not ask you to write
  those modules, so leaving them alone is all that is required.);
- allocation strategy, copy counts, timing, and any performance property,
  except the "does not pre-allocate from an attacker-supplied count" and
  "does not hang" requirements §3 states explicitly for `Vec<T>`;
- the **iteration order** of `Transaction::{sprout_nullifiers,
  sapling_nullifiers}` and `ShieldedData::{spends, outputs, nullifiers,
  note_commitments}`. §5 fixes each one's *element type*, and the surrounding
  structure fixes which elements are yielded, but the sequence they come back
  in is yours: any order is conforming, and nothing compares these iterators
  as sequences. Should a later milestone need an order, it will state one
  then; do not infer one from the `first`/`rest` split, which exists to make
  the "at least one" invariant representable and is not an ordering claim.
  `ShieldedData`'s `PartialEq` is consistent with this — it compares the
  *sets* of spends and outputs, so two `ShieldedData` that differ only in how
  the same spends are split across `first`/`rest` are equal;
- `binding_verification_key`, and the cryptographic content of every shielded
  type generally. This milestone is a codec: shielded data must round-trip
  byte-for-byte and its containers must expose the surface §5 lists, but no
  signature is verified, no note commitment is checked, and no proof is
  validated here. `binding_verification_key` must exist and compile with the
  signature §5 gives; its return value is not checked;
- `serialization::serde_helpers` beyond compiling, as §3 already says. These
  are `serde` adapters the retained crates name in `#[serde(with = ..)]`
  attributes; the verifier never serializes through them;
- rustfmt/clippy cleanliness, documentation comments, and your own tests;
- the *contents* of `zebra-test/` beyond what §1 already requires (that it
  keeps compiling as a `[dev-dependencies]` entry);
- how you shape any `proptest` `Strategy`, beyond the specific impls and the
  one function §2 lists as required to compile;
- **This bullet is in scope, unlike the rest of this section:**
  `Script::zcash_deserialize` (and any type whose last field is a `Script`,
  e.g. `transparent::Output`) returning `Ok` with a shortened value, rather
  than `Err(SerializationError::Io(..))`, when its length-prefixed body is
  truncated or its prefix promises more than the stream holds. This is the
  one place the variant-identity rule in §3 does not apply; it is checked as
  `Ok` with the truncated content, to match what reading a length-prefixed
  body with `Read::take(len).read_to_end(..)` does.
