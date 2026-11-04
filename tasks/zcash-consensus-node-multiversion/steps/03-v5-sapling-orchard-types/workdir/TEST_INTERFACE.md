# TEST_INTERFACE.md — milestone 3, Sapling-in-V5 (ZIP-225), Orchard data types, and RedPallas

This document is the contract the tests are written against. Every
module path, type, method, field, constant, byte layout and error string the
verifier depends on is named here. If a behaviour is not stated here or in a
specification document named by `instruction.md`, it is not in scope.

## 1. How the verifier reaches your code

The verifier runs ten groups. Eight run **integration test files**, copied
into `zebra-chain/tests/`:

```
cargo test --offline --locked --jobs 4 -p zebra-chain --test <name> -- --test-threads=1
```

(If `--offline --locked` cannot resolve against the workspace as you leave it,
the verifier retries with `--offline` alone, then `--locked` alone, then
neither, for every cargo invocation it runs — build, check and test alike; but
assume the first form is the one that actually runs.) The other two groups run
whole-workspace commands instead of a test file:

| Group | Kind | Command |
| --- | --- | --- |
| `build` | gate | `cargo build --offline --locked -p zebra-chain --tests` |
| `workspace-checks` | requirement | `cargo check --offline --locked --workspace --all-targets --all-features` |

**The `build` group is also the gate for all eight test-file groups.** If
`cargo build -p zebra-chain --tests` fails, every driver group is recorded as
failed with that reason instead of being run. `build` is itself a gate group,
so a failing gate group is decisive: the step is not satisfied, however much of
the new work you finished. Two of the eight driver
groups -- `inherited-block-codec` and `inherited-sighash` -- are also gate
groups and would fail with it, so a `zebra-chain` that does not compile fails
the step three times over. `workspace-checks` vetoes nothing and stands as a
requirement group on its own.

Consequences you must respect:

- The tests reach your code **only through the public API of the
  `zebra_chain` library crate**, as an external consumer. `pub(crate)`,
  `pub(super)` and `#[cfg(test)]` items are invisible to them.
- Each test file is compiled against `zebra-chain/Cargo.toml` **as you leave
  it**. The Orchard/Pallas arithmetic this milestone introduces needs two
  crates that are **not** in the workspace you inherit, so **this milestone
  adds them**: you must put both in `zebra-chain`'s `[dependencies]`, spelled
  exactly as here —

  ```toml
  group = "0.9"
  halo2 = { git = "https://github.com/zcash/halo2.git", rev = "dda60a363001373d564156ad0334e2022d85a5b4"}
  ```

  The tests `use` both directly (`group::prime::PrimeCurveAffine`,
  `group::GroupEncoding`, `halo2::pasta::pallas`,
  `halo2::arithmetic::FieldExt`), so both must be ordinary `[dependencies]`
  entries — that is where upstream declares them, and it is what an injected
  integration test is guaranteed to be able to name — rather than
  `[dev-dependencies]`, even though your own code only needs `halo2`. The
  crate names, the version, the git URL and the rev are fixed by the two lines
  above: pin them exactly as written, because the verifier resolves
  **offline** against a cargo cache primed for that exact rev. Beyond these
  two, do not add any new third-party dependency and do not remove an existing
  one; the workspace must keep building **offline**. (The upstream release
  this milestone tracks carries further manifest changes at this tag — `aes`,
  `fpe`, `itertools`, the `funty` pin, dev-dependencies for a bench target,
  an optional `zebra-test`, and four raised version requirements. None of them
  is required by anything this document checks; they belong to code and
  targets the milestone does not ask for.)
- The `--all-features` flag on the workspace-wide `check` is load-bearing, not
  decorative. `--all-targets` selects *targets*, not *features*: a check
  without `--all-features` need not type-check a crate's optional-feature code
  at all, and `zebra-chain`'s `proptest-impl` modules are the largest such
  surface here. `--all-features` is what upstream's own CI runs, and what the
  `workspace-checks` group actually invokes, so every feature of every crate
  has to keep compiling. Upstream also adds a `bench` feature and a
  `zebra-chain/benches/block.rs` target **at this milestone** — not at its
  predecessor, which has neither — re-gating `zebra_chain::block::tests` as
  `#[cfg(any(test, feature = "bench"))]` and making `zebra-test` an optional
  dependency. That bench target needs `criterion`, which is nowhere in this
  workspace, so writing it is optional and **no group checks it** (it is not
  a §11 exclusion — nothing about it is required at all); if
  you do write it, `--all-targets --all-features` is what will compile it.
- A group whose driver fails to compile fails for that group. Tests run
  with `--test-threads=1` and must not depend on execution order between
  `#[test]` functions in the same file.

### How this step is checked

The gate groups are `build`, `inherited-block-codec` and `inherited-sighash`.
They add no new requirement and only veto: if any one of them fails, the step
is not satisfied no matter how much of the new work you completed.

The requirement groups carry this step's new capability and its peripheral
breadth; every case in them is in scope.

The practical consequence: preserving the inherited behaviour the gate
checks is necessary but adds nothing by itself, and doing nothing else
leaves all of the new work undone.

## 2. Where the implementation goes

You are extending existing modules, not adding new top-level ones except
`orchard` and `primitives::redpallas`:

| Module | Declared as (already) | What this milestone adds |
| --- | --- | --- |
| `zebra_chain::transaction::Transaction` | `pub enum Transaction`, `V5` variant already has `lock_time`, `expiry_height`, `inputs`, `outputs`, plus a placeholder `rest: Vec<u8>` field | `V5` gains `network_upgrade: NetworkUpgrade` and `sapling_shielded_data: Option<sapling::ShieldedData<sapling::SharedAnchor>>`, replacing `rest` entirely (§3) |
| `zebra_chain::sapling` | `pub mod sapling;` — `ShieldedData<AnchorV>` and the anchor-variant plumbing (`AnchorVariant`, `PerSpendAnchor`, `SharedAnchor`, `FieldNotPresent`, `Spend<AnchorV>`, `Output`) already exist, wired only to `V4`'s single-array encoding; `TransferData` itself is new here and replaces the predecessor's `first`/`rest_spends`/`rest_outputs` fields | the ZIP-225 split-array wire format for `V5`: `SpendPrefixInTransactionV5`, `OutputPrefixInTransactionV5`, `Spend::{from_v5_parts, into_v5_parts}`, `Output::{from_v5_parts, into_v5_parts}`, and `TransferData`'s two variants (`SpendsAndMaybeOutputs`, `JustOutputs`) each carrying an `AtLeastOne<_>` (§4) |
| `zebra_chain::parameters::NetworkUpgrade` | `pub enum NetworkUpgrade`, already has `branch_id(&self) -> Option<ConsensusBranchId>` | the inverse, `from_branch_id(u32) -> Option<NetworkUpgrade>` (§5) |
| `zebra_chain::serialization` | `pub mod serialization;` — `SerializationError`, `TrustedPreallocate`, `ZcashSerialize`/`ZcashDeserialize`, `MAX_PROTOCOL_MESSAGE_LEN` already exist | `AtLeastOne<T>` (§6), `zcash_serialize_external_count`/`zcash_deserialize_external_count` (§7) |
| `zebra_chain::orchard` | does not exist before this step | `Action`, and the plain wrapper types `NoteCommitment`, `ValueCommitment`, `EncryptedNote`, `keys::{Diversifier, EphemeralPublicKey}` (§8) — freestanding, not reachable from any `Transaction` |
| `zebra_chain::primitives::redpallas` | does not exist before this step | `SigningKey`, `VerificationKey`, `VerificationKeyBytes`, `Signature`, `Error`, generic over marker types `SpendAuth`/`Binding` (§8) |

Internal file layout inside each module is yours; only the paths and names
below are in scope.

## 3. `Transaction::V5`'s wire codec — group `tx-v5-zip225-codec` (`zebra-chain`)

`V5`'s header as ZIP-225 publishes it is `(header, nVersionGroupId,
nConsensusBranchId, lockTime, expiryHeight)` — five little-endian `u32`s, 20
bytes, with `header`'s bit 31 the overwintered flag and its low 31 bits the
version number (5).

**This milestone is where the header reaches that shape.** The version this
milestone's predecessor parses and writes has **no `nConsensusBranchId` field
at all** — it is `(header, nVersionGroupId, lockTime, expiryHeight)`, a 16-byte
prefix, with everything past the transparent outputs held as an opaque
`rest: Vec<u8>`. Inserting `nConsensusBranchId` as the third word, so that
`lockTime` moves from bytes 8..12 to 12..16 and `expiryHeight` from 12..16 to
16..20, is part of this step's work, and the fixtures the verifier parses are
20-byte-header transactions. Two of the five words now carry rules:

```rust
pub const TX_V5_VERSION_GROUP_ID: u32 = 0x26A7_270A; // zebra_chain::parameters
```

- `nVersionGroupId` (wire bytes 4..8, little-endian) must equal
  `TX_V5_VERSION_GROUP_ID`, or parsing fails with
  `SerializationError::Parse("expected TX_V5_VERSION_GROUP_ID")`.
- `nConsensusBranchId` (wire bytes 8..12) is looked up with
  `NetworkUpgrade::from_branch_id` (§5) and stored as the new `network_upgrade`
  field. A branch id `from_branch_id` resolves to `None` fails with
  `SerializationError::Parse("expected a valid network upgrade from the
  consensus branch id")`.
- A version-5 header with the overwintered bit cleared matches none of
  `V1..V5`'s header shapes and fails with `SerializationError::Parse("bad tx
  header")`.

After the header: a plain compactSize-counted transparent input array, then a
plain compactSize-counted transparent output array (both byte-identical in
shape to `V4`'s), then the Sapling bundle (§4), stored as
`sapling_shielded_data: Option<sapling::ShieldedData<sapling::SharedAnchor>>`
(`None` iff `nSpendsSapling == 0 && nOutputsSapling == 0`). After the Sapling
bundle, `nActionsOrchard` must decode to the value **zero**; a non-zero count
fails with `SerializationError::Parse("expected orchard
section to be just a zero")` — this milestone does not parse a populated
Orchard bundle (see §11). Any compactSize encoding of zero is accepted; the
rule is on the decoded count, not on its byte length.

Truncating a well-formed `V5` transaction by even one byte must fail with
`SerializationError::Io`, distinct from every `Parse` case above, which are
all bytes that are present but semantically wrong.

A well-formed `V5` transaction must serialize back to the exact bytes it was
parsed from — no field is dropped, reordered, or given a default on the way
through.

The verifier destructures the variant by name and **without a `..` rest
pattern**, so at this milestone `V5` must carry exactly these six fields, in
any order but with these names and types, and no seventh:

```rust
Transaction::V5 {
    network_upgrade: NetworkUpgrade,
    lock_time: LockTime,
    expiry_height: block::Height,
    inputs: Vec<transparent::Input>,
    outputs: Vec<transparent::Output>,
    sapling_shielded_data: Option<sapling::ShieldedData<sapling::SharedAnchor>>,
}
```

In particular, do not add an Orchard shielded-data field here — that is step
4's job (§11), and a seventh field breaks the destructuring.

One of this group's tests also reads the shared anchor out of the parsed
bundle and compares it against `v5tx.tsv`'s `shared_anchor_hex` column, which
needs the `From<sapling::tree::Root> for [u8; 32]` conversion §4 specifies —
see §4's "Reading an anchor's bytes".

## 4. The Sapling bundle inside `V5` — group `tx-v5-sapling-transfers` (`zebra-chain`)

Full wire layout of the Sapling section (from ZIP-225 §7.3), with the byte
offsets a **one-spend, two-output** transaction that has no transparent inputs
and no transparent outputs actually produces. Bytes `0..20` are the header
(§3); the two transparent arrays then contribute one compactSize zero byte
each, at `20` and at `21`, so the Sapling section begins at byte `22`:

| Bytes (example transaction) | Field | Size | Present iff |
| --- | --- | --- | --- |
| 22..23 | `nSpendsSapling` | compactSize | always |
| 23..119 | `vSpendsSapling` | `96 * nSpendsSapling` | always |
| 119..120 | `nOutputsSapling` | compactSize | always |
| 120..1632 | `vOutputsSapling` | `756 * nOutputsSapling` | always |
| 1632..1640 | `valueBalanceSapling` | `int64` | `nSpendsSapling + nOutputsSapling > 0` |
| 1640..1672 | `anchorSapling` | `[u8; 32]` | `nSpendsSapling > 0` |
| 1672..1864 | `vSpendProofsSapling` | `192 * nSpendsSapling` | — |
| 1864..1928 | `vSpendAuthSigsSapling` | `64 * nSpendsSapling` | — |
| 1928..2312 | `vOutputProofsSapling` | `192 * nOutputsSapling` | — |
| 2312..2376 | `bindingSigSapling` | `[u8; 64]` | `nSpendsSapling + nOutputsSapling > 0` |

Each `vSpendsSapling` entry (the 96-byte "prefix") is `cv (32) || nullifier
(32) || rk (32)`, in that order. Each `vOutputsSapling` entry (the 756-byte
prefix) is `cv (32) || cm_u (32) || ephemeral_key (32) || enc_ciphertext (580)
|| out_ciphertext (80)`.

Public types and methods this maps onto:

```rust
// derives: Clone, Debug, Serialize, Deserialize, PartialEq, Eq
pub struct ShieldedData<AnchorV: AnchorVariant + Clone> {
    pub value_balance: Amount,               // valueBalanceSapling
    pub transfers: TransferData<AnchorV>,
    pub binding_sig: redjubjub::Signature<Binding>, // bindingSigSapling
}
// `ShieldedData` must be `Clone` in particular: the driver clones the bundle
// out of a parsed transaction rather than borrowing it.

pub trait AnchorVariant {
    type Shared: Clone + Debug + /* ... */;
    type PerSpend: Clone + Debug + /* ... */;
}
// PerSpendAnchor { Shared = FieldNotPresent, PerSpend = tree::Root }  (V4 shape)
// SharedAnchor   { Shared = tree::Root,      PerSpend = FieldNotPresent } (V5 shape)
// FieldNotPresent, PerSpendAnchor, SharedAnchor are all zero-sized unit
// structs (size_of::<T>() == 0), constructible, Copy/Clone/Debug/PartialEq/Eq.

pub enum TransferData<AnchorV: AnchorVariant + Clone> {
    SpendsAndMaybeOutputs {
        shared_anchor: AnchorV::Shared,          // anchorSapling
        spends: AtLeastOne<Spend<AnchorV>>,       // vSpendsSapling, non-empty
        maybe_outputs: Vec<Output>,               // vOutputsSapling, may be empty
    },
    JustOutputs {
        outputs: AtLeastOne<Output>,               // vOutputsSapling, non-empty
    },
}
```

A parsed bundle is `SpendsAndMaybeOutputs` iff `nSpendsSapling > 0`, and
`JustOutputs` iff `nSpendsSapling == 0`. The cardinality rule ZIP-225 states is
`nSpendsSapling + nOutputsSapling > 0` — the same condition that governs
`valueBalanceSapling` and `bindingSigSapling` in the table above — so a bundle
with one or more spends and *zero* outputs is legal, and is a
`SpendsAndMaybeOutputs` with an empty `maybe_outputs`. What cannot exist is a
`ShieldedData` with zero spends *and* zero outputs: that is `None` on the
`Transaction::V5` field (§3), which is why both `TransferData` variants carry
an `AtLeastOne<_>`.

`Spend<AnchorV>` and its `V5` prefix:

```rust
pub struct Spend<AnchorV: AnchorVariant> {
    pub cv: ValueCommitment,
    pub per_spend_anchor: AnchorV::PerSpend,     // FieldNotPresent for SharedAnchor
    pub nullifier: Nullifier,
    pub rk: redjubjub::VerificationKeyBytes<SpendAuth>,
    pub zkproof: Groth16Proof,
    pub spend_auth_sig: redjubjub::Signature<SpendAuth>,
}
pub struct SpendPrefixInTransactionV5 { pub cv: ValueCommitment, pub nullifier: Nullifier, pub rk: redjubjub::VerificationKeyBytes<SpendAuth> }

// The prefix is its own serializable type, not just a field bundle: the
// 96-byte `cv || nullifier || rk` encoding is reachable as a per-type trait
// impl, which is also what lets `vSpendsSapling` be read and written with the
// external-count primitives of §7.
impl ZcashSerialize for SpendPrefixInTransactionV5;    // writes exactly 96 bytes, no length prefix
impl ZcashDeserialize for SpendPrefixInTransactionV5;  // reads exactly those 96 bytes
impl TrustedPreallocate for SpendPrefixInTransactionV5;

impl Spend<SharedAnchor> {
    pub fn from_v5_parts(prefix: SpendPrefixInTransactionV5, zkproof: Groth16Proof, spend_auth_sig: redjubjub::Signature<SpendAuth>) -> Spend<SharedAnchor>;
    pub fn into_v5_parts(self) -> (SpendPrefixInTransactionV5, Groth16Proof, redjubjub::Signature<SpendAuth>);
}
impl From<(Spend<SharedAnchor>, tree::Root)> for Spend<PerSpendAnchor>;
```

`Output` and its `V5` prefix:

```rust
pub struct Output {
    pub cv: ValueCommitment,
    pub cm_u: jubjub::Fq,
    pub ephemeral_key: sapling::keys::EphemeralPublicKey, // Sapling's own type, over Jubjub — distinct from orchard::keys::EphemeralPublicKey (§8), which is over Pallas
    pub enc_ciphertext: EncryptedNote,
    pub out_ciphertext: WrappedNoteKey,
    pub zkproof: Groth16Proof,
}
pub struct OutputPrefixInTransactionV5 { pub cv: ValueCommitment, pub cm_u: jubjub::Fq, pub ephemeral_key: sapling::keys::EphemeralPublicKey, pub enc_ciphertext: EncryptedNote, pub out_ciphertext: WrappedNoteKey }

// Same as the spend prefix: its own serializable type.
impl ZcashSerialize for OutputPrefixInTransactionV5;   // writes exactly 756 bytes, no length prefix
impl ZcashDeserialize for OutputPrefixInTransactionV5; // reads exactly those 756 bytes
impl TrustedPreallocate for OutputPrefixInTransactionV5;

impl Output {
    pub fn from_v5_parts(prefix: OutputPrefixInTransactionV5, zkproof: Groth16Proof) -> Output;
    pub fn into_v5_parts(self) -> (OutputPrefixInTransactionV5, Groth16Proof);
}
```

`ShieldedData<AnchorV>` accessor methods (all pre-existing on the type, but
now backed by `TransferData` rather than a flat field):

```rust
impl<AnchorV: AnchorVariant + Clone> ShieldedData<AnchorV> {
    pub fn spends(&self) -> impl Iterator<Item = &Spend<AnchorV>>;
    pub fn outputs(&self) -> impl Iterator<Item = &Output>;
    pub fn shared_anchor(&self) -> Option<AnchorV::Shared>; // None iff spends() is empty
    pub fn binding_verification_key(&self) -> redjubjub::VerificationKeyBytes<Binding>;
}
```

`binding_verification_key` computes `cv_old - cv_new - cv_balance` from the
spends' and outputs' own `cv` fields and `value_balance` (there is no
separate `value_balance` parameter to the method itself — it reads `self`).

**Reading an anchor's bytes.** `AnchorV::Shared` is `tree::Root` for
`SharedAnchor`, and both drivers that inspect the anchor take it out of
the parsed bundle and convert it to bytes with `Into`:

```rust
// tx-v5-zip225-codec, via the accessor:
let shared_anchor: [u8; 32] = shielded.shared_anchor().expect("…").into();
// tx-v5-sapling-transfers, straight out of the variant (the match binds a &Root, copied out):
let anchor_bytes: [u8; 32] = (*shared_anchor).into();
```

`tree::Root` is `Root(pub [u8; 32])`, and no document before this one asks for
a byte view of it, so **this milestone adds it**:

```rust
// zebra_chain::sapling::tree, beside `pub struct Root(pub [u8; 32])`
impl From<Root> for [u8; 32];   // the 32 bytes as written to `anchorSapling` on the wire
impl From<[u8; 32]> for Root;   // its exact inverse
```

The `From<Root>` direction is the one the drivers call, both times by value:
`shared_anchor()` hands back an owned `Root` (the type is `Copy`), and the
variant arm binds a `&Root` the driver copies out with `*`. The `Root` field is
public, so `root.0` is the same 32 bytes the impl must return. They are the
anchor exactly as `anchorSapling` holds it — bytes `1640..1672` in the table
above — so for the `v5tx_spends_and_outputs` fixture the conversion yields
`914e8b02fd17010db178285840a89c4de9b78fc2c0d6b926028b45626e1adf49`, the
`shared_anchor_hex` column of `v5tx.tsv`. Nothing here calls `From<[u8; 32]>`,
but it is the inverse a parser wants when it builds a `Spend<SharedAnchor>`'s
anchor from wire bytes, and the reference tree carries both.

Every field of a `Spend`/`Output` round-trips losslessly through
`into_v5_parts`/`from_v5_parts`; converting a `Spend<SharedAnchor>` to
`Spend<PerSpendAnchor>` via the `From<(Spend<SharedAnchor>, tree::Root)>` impl
changes only `per_spend_anchor` (which becomes the given root) and leaves
`cv`, `nullifier`, `rk`, `zkproof` and `spend_auth_sig` untouched.

## 5. `NetworkUpgrade::from_branch_id` — group `v5-consensus-branch-id` (`zebra-chain`)

```rust
pub enum NetworkUpgrade { Genesis, BeforeOverwinter, Overwinter, Sapling, Blossom, Heartwood, Canopy, Nu5 }

impl NetworkUpgrade {
    pub fn branch_id(&self) -> Option<ConsensusBranchId>;         // already exists
    pub fn from_branch_id(branch_id: u32) -> Option<NetworkUpgrade>; // new, its exact inverse
}
```

The full table `from_branch_id` (and `branch_id`) is built from — restated
here, not read out of the crate:

| Network upgrade | Consensus branch id |
| --- | --- |
| Overwinter | `0x5ba81b19` |
| Sapling | `0x76b809bb` |
| Blossom | `0x2bb40e60` |
| Heartwood | `0xf5b9230b` |
| Canopy | `0xe9ff75a6` |
| Nu5 | `0xf919a198` |

`from_branch_id` is a pure table lookup: it returns `Some` for exactly these
six values (round-tripping with each upgrade's own `branch_id()`), and `None`
for every other `u32` — including `Genesis` and `BeforeOverwinter` (neither of
which has ever had a consensus branch id: `branch_id()` is `None` for both,
and no `u32` may resolve to either variant), and including this crate's own
transaction version-group-id constants, which are a different four-byte
namespace that happens to be confusable with branch ids:

```rust
pub const OVERWINTER_VERSION_GROUP_ID: u32 = 0x03C4_8270;
pub const SAPLING_VERSION_GROUP_ID: u32 = 0x892F_2085;
pub const TX_V5_VERSION_GROUP_ID: u32 = 0x26A7_270A;
```

None of these three constants is equal to any of the six branch ids above, and
`from_branch_id` must return `None` for all three of them too.

## 6. `AtLeastOne<T>` — group `serialization-at-least-one` (`zebra-chain`)

```rust
pub struct AtLeastOne<T> { /* private: inner: Vec<T> */ }
// derives: Clone, Debug, PartialEq, PartialOrd
// TryFrom::Error = SerializationError, which derives: Debug

impl<T> TryFrom<Vec<T>> for AtLeastOne<T>;      // Err on an empty vector
impl<T: Clone> TryFrom<&[T]> for AtLeastOne<T>;
impl<T, const N: usize> TryFrom<[T; N]> for AtLeastOne<T>;      // Err when N == 0
impl<T: Clone, const N: usize> TryFrom<&[T; N]> for AtLeastOne<T>;

impl<T> Deref for AtLeastOne<T> { type Target = Vec<T>; }        // full read access to the inner Vec
impl<T> From<AtLeastOne<T>> for Vec<T>;

impl<T> AtLeastOne<T> {
    pub fn as_vec(&self) -> &Vec<T>;
    pub fn into_vec(self) -> Vec<T>;
    pub fn first(&self) -> &T;                    // not Option<&T> — the type itself guarantees one exists
    pub fn split_first(&self) -> (&T, &[T]);
}
```

All four `TryFrom` entry points agree on emptiness (all reject a 0-length
input, all accept a non-empty one and preserve element order). Equality and
ordering (`PartialEq`, `PartialOrd`) follow the inner `Vec<T>`'s own. This
group exercises the wrapper standalone, over `u32` — it does not touch a
`Spend`, `Output`, or anything Sapling-shaped; that wiring is §4's job.

## 7. External-count vector primitives — group `external-count-vectors` (`zebra-chain`)

```rust
pub fn zcash_serialize_external_count<W: io::Write, T: ZcashSerialize>(vec: &Vec<T>, writer: W) -> Result<(), io::Error>;
pub fn zcash_deserialize_external_count<R: io::Read, T: ZcashDeserialize + TrustedPreallocate>(external_count: usize, reader: R) -> Result<Vec<T>, SerializationError>;
```

`zcash_serialize_external_count` writes each element's own serialized bytes
back to back, with **no compactSize length prefix** — contrast with
`Vec<T>::zcash_serialize`/`zcash_deserialize`, which both read and write one.
`zcash_deserialize_external_count` reads exactly `external_count` elements
(each via `T::zcash_deserialize`) and stops there, leaving any following bytes
in the reader untouched — this is what lets a V5 parser read
`vSpendProofsSapling` right after `vSpendsSapling`'s own count, with no count
of its own on the wire (§4).

Before reading a single byte, `zcash_deserialize_external_count` must reject
`external_count > T::max_allocation()` with `SerializationError::Parse(_)` —
never `SerializationError::Io`, even when the reader is empty. A count exactly
equal to `T::max_allocation()` must succeed (given enough bytes); a reader
that runs out partway through an in-bounds count is `SerializationError::Io`.

`TrustedPreallocate::max_allocation() -> u64` already exists as a trait (used
elsewhere in the crate); this milestone's new functions are what make it
meaningful for externally-counted arrays too. For `block::Hash` specifically
(a plain 32-byte type, already `TrustedPreallocate`):

```rust
pub const MAX_PROTOCOL_MESSAGE_LEN: usize = 2 * 1024 * 1024; // already exists
// block::Hash::max_allocation() == (MAX_PROTOCOL_MESSAGE_LEN - 1) / 32 == 65_535
```

## 8. Orchard data types and RedPallas — group `orchard-redpallas-types` (`zebra-chain`)

`zebra_chain::orchard` is a brand-new module this milestone, re-exporting:

```rust
pub use action::Action;
pub use commitment::{CommitmentRandomness, NoteCommitment, ValueCommitment};
pub use keys::Diversifier;
pub use note::{EncryptedNote, Note, Nullifier};
pub mod keys; // also exposes keys::EphemeralPublicKey, not re-exported at the orchard root
```

`NoteCommitment(pub pallas::Affine)`, `ValueCommitment(pub pallas::Affine)`
and `keys::EphemeralPublicKey(pub pallas::Affine)` are plain wrappers around a
Pallas curve point, each with:

```rust
impl TryFrom<[u8; 32]> for T {
    type Error = &'static str;
    // Ok(_) iff the 32 bytes are a canonical compressed Pallas point encoding;
    // Err("Invalid pallas::Affine value") otherwise. No further validity check.
}
// derives: PartialEq, Debug (required for assert_eq! on Ok/Err results)
```

In particular, all three accept **both** the curve identity and the curve
generator — there is no small-order rejection on any of them.

`EncryptedNote(pub [u8; 580])` (`orchard::note`, re-exported at the module
root) is a plain fixed-size byte array wrapper: `ZcashSerialize`/
`ZcashDeserialize` write/read its 580 bytes verbatim, with no validation of
any kind. `EncryptedNote` implements `PartialEq` (for round-trip `assert_eq!`)
and `Debug`.

`zebra_chain::primitives::redpallas` is a brand-new module, mirroring the
`redjubjub` API already used for Sapling (`primitives::redjubjub`, re-exported
at the crate root) but over the Pallas curve:

```rust
pub use error::Error;                       // MalformedSigningKey, MalformedVerificationKey, InvalidSignature
// derives: PartialEq, Debug (required for assert_eq! on error results)
pub use signing_key::SigningKey;
pub use verification_key::{VerificationKey, VerificationKeyBytes};
pub use signature::Signature;
pub type Randomizer = pallas::Scalar;
pub trait SigType: private::Sealed {}
pub enum Binding {}   impl SigType for Binding {}
pub enum SpendAuth {} impl SigType for SpendAuth {}

pub struct VerificationKeyBytes<T: SigType> { /* private: bytes: [u8; 32] */ }
impl<T: SigType> From<[u8; 32]> for VerificationKeyBytes<T>;
impl<T: SigType> From<VerificationKeyBytes<T>> for [u8; 32];   // plain byte wrapper, no validation

pub struct Signature<T: SigType> { /* private: r_bytes, s_bytes: [u8; 32] each */ }
impl<T: SigType> From<[u8; 64]> for Signature<T>;
impl<T: SigType> From<Signature<T>> for [u8; 64];              // plain byte wrapper, no validation

pub struct SigningKey<T: SigType> { /* private */ }
impl<T: SigType> TryFrom<[u8; 32]> for SigningKey<T>;
// Err(Error::MalformedSigningKey) iff the 32 bytes are not a canonical
// encoding of a Pallas scalar (e.g. any value >= the scalar field's modulus,
// such as [0xff; 32]).
impl<T: SigType> From<&SigningKey<T>> for VerificationKey<T>; // derives the public key
impl<T: SigType> From<VerificationKey<T>> for VerificationKeyBytes<T>;  // converts back to raw bytes; no validation

pub struct VerificationKey<T: SigType> { /* private */ }
impl<T: SigType> TryFrom<VerificationKeyBytes<T>> for VerificationKey<T> {
    type Error = Error;
    // Err(Error::MalformedVerificationKey) if either:
    //   (1) the 32 bytes are not a canonical Pallas point encoding, or
    //   (2) the decoded point is small-order (the curve identity is the
    //       smallest-order point there is, and must be rejected here).
    // The curve generator has large prime order and must be accepted.
}
impl VerificationKey<SpendAuth> {
    pub fn randomize(&self, randomizer: &Randomizer) -> VerificationKey<SpendAuth>;
    // a nonzero randomizer must change the key's encoded bytes.
}
```

This is the one consensus-relevant difference this milestone's Orchard
surface draws: `VerificationKey::try_from` performs a small-order check that
`NoteCommitment`/`ValueCommitment`/`EphemeralPublicKey`/`VerificationKeyBytes`
do not. `VerificationKey`'s check is still only about encoding validity — it
is not signature verification (see §11).

## 9. Regression guard — group `inherited-block-codec` (GATE — adds no requirement, vetoes the step if it fails; `zebra-chain`)

A trimmed copy of step 1's fourteen-test block-codec driver, run against the
same 17-block corpus (`fixtures/blocks.tsv` + `fixtures/blocks/*.hex`,
unchanged since step 1): every block still round-trips byte-for-byte;
`Block::hash()`, `Block::coinbase_height()`, `transactions.len()`, the
coinbase-must-be-first rule, the merkle root computed from every
transaction's own `Transaction::hash()`, and the re-serialized length still
match the corpus; and the 2,000,000-byte block limit is still enforced on
deserialize from both sides (a block of exactly the limit parses, one byte
over does not). A block one byte short of complete is a
`SerializationError::Io`. This exists because this milestone's parser change
to `Transaction::V5` must not disturb any block whose transactions are all
`V1`..`V4` — the vast majority of the real chain.

## 10. Regression guard — group `inherited-sighash` (GATE — adds no requirement, vetoes the step if it fails; `zebra-chain`)

Unchanged from step 1's sighash driver **apart from one added match arm** — a
fifth, unreachable arm that keeps the driver's diagnostic branch exhaustive
against `Transaction`'s new `V5` variant. It reproduces the ZIP-143/ZIP-243
signature hash
vectors (`fixtures/sighash.tsv`, five rows straight out of the ZIPs' own
normative text — two ZIP-143/`V3`/Overwinter vectors, three ZIP-243/`V4`/
Sapling vectors) via

```rust
impl Transaction {
    pub fn sighash(&self, nu: NetworkUpgrade, hash_type: HashType, input: Option<(u32, transparent::Output)>) -> blake2b_simd::Hash;
}
```

`hash.as_bytes()` is the digest in exactly the byte order the ZIP prints it —
**not** reversed, unlike `block::Hash`'s and `transaction::Hash`'s own
`Display`. `scriptCode` (the value the ZIP hashes for the spent output's
script) is the *serialized* script, compactSize length prefix included — the
prefix must already be inside the `transparent::Output`'s `lock_script`
handed to `sighash`; the function itself adds no prefix of its own. This group
exists to prove this milestone's `V5` parser change leaves the existing
`V3`/`V4` sighash algorithm bit-for-bit unchanged; it does not add a `V5`
sighash (see §11).

## 11. Negative space — what this milestone does not ask for

- **Wiring a populated Orchard bundle into `Transaction::V5`.** The parser
  must still reject any `nActionsOrchard != 0` (§3); `orchard::Action` and its
  companion types (§8) exist as freestanding, independently serializable
  types this step, not as a field any `Transaction` variant carries.
  Consuming them from a real transaction is `REQUIREMENTS.md` step 4.
- **The `§8` symbol set this step's cases do not reach.** The cases
  construct and serialize `redpallas::{VerificationKeyBytes, Signature,
  SigningKey, VerificationKey, Error, Randomizer}`, `NoteCommitment`,
  `ValueCommitment`, `keys::EphemeralPublicKey` and `EncryptedNote`. The
  remaining symbols §8 names — `orchard::Action`, `orchard::Nullifier`,
  `keys::Diversifier`, `note::Note`, `commitment::CommitmentRandomness`, and
  the `redpallas::{Binding, SpendAuth}` marker types as *constructible*
  values — are required to exist at the paths §8 gives (`instruction.md` asks
  for them and the next milestone builds on that module tree), but
  **nothing in this step checks them**: no case constructs, serializes or
  validates one. Their own layouts are pinned by ZIP-225 §7.5 and by the next
  milestone's contract, not by this document.
- **RedPallas signature verification and Halo2 proof verification.**
  `VerificationKey::try_from`'s small-order rejection is an encoding-validity
  check, not verification of a signature over a real message; nothing here
  invokes RedPallas or Halo2 as a consensus rule.
  `primitives::proofs::Halo2Proof` (already present as of this milestone, a
  raw-bytes wrapper) is not exercised by any behaviour in scope here.
- **A `V5`-specific transaction digest (ZIP-244).** Every transaction
  version, `V5` included, still hashes with the same version-agnostic
  double-SHA256 over its serialized bytes; `Transaction::sighash` (§10) is
  unchanged and is not extended to cover `V5`'s own fields. That is
  `REQUIREMENTS.md` step 6's job.
- **Contextual or activation-height validation.** `from_branch_id` (§5) is a
  pure, context-free table lookup; nothing here checks whether a given
  network upgrade's branch id is the *correct* one for a transaction's block
  height, or enforces NU5 activation.
- **A dedicated non-empty-vector error message.** `AtLeastOne`'s `TryFrom`
  impls (§6) only need to be distinguishably `Err` on empty input; no exact
  error string or variant is checked for them.
- **Step 2's `zebra-consensus` verification groups.** Step 2's own milestones —
  ZIP-215 Ed25519 rules, mandatory Canopy checkpoint, batched async
  JoinSplit/Groth16 verification, ZIP-244 block commitment — are not re-checked
  at this step: `zebra-consensus` is not reachable from the eight
  `zebra-chain` integration tests, and step 2's verifier is not
  re-run. `groups.txt` carries no `zebra-consensus` groups for step 3.

## 12. Handoff

**Inherited from steps 1-2**: the `V1`-`V4` transaction codec and the `V5`
header/transparent-only skeleton, the 17-block regression corpus, and the
ZIP-143/ZIP-243 sighash implementation — all exercised again here (§9, §10)
only to prove this milestone's changes did not disturb them.

**What step 3 leaves for step 4**: a `Transaction::V5` that fully parses and
re-serializes its Sapling bundle (§3, §4), a working
`NetworkUpgrade::from_branch_id` (§5), `AtLeastOne<T>` and the external-count
primitives (§6, §7) ready to reuse for Orchard's own action array, and
`orchard::Action`/`primitives::redpallas` (§8) fully defined but not yet
reachable from any `Transaction` value — deliberately: `Transaction::V5`'s
Orchard section must still be exactly one zero byte (§3). Step 4
(`REQUIREMENTS.md`: wiring a populated Orchard bundle into `Transaction::V5`,
committing Orchard nullifiers into `zebra-state`, and a `scriptCode`
double-prefix fix) is expected to find all of the above in place and
unchanged in its public shape.
