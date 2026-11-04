# TEST_INTERFACE.md — milestone 2, ZIP-215 batch verification, NU5 constants, v5 transaction skeleton

This document is the contract the tests are written against. Every
module path, type, method, field, constant, error variant and byte layout the
verifier depends on is named here. If a behaviour is not stated here or in a
specification document named by `instruction.md`, it is not in scope.

## 1. How the verifier reaches your code

The verifier runs ten groups. Eight run **integration test files**, copied
into two different crates' `tests/` directories:

```
cargo test --offline --locked --jobs 4 -p zebra-chain      --test <name> -- --test-threads=1
cargo test --offline --locked --jobs 4 -p zebra-consensus  --test <name> -- --test-threads=1
```

(If `--offline --locked` cannot resolve, the verifier retries with `--offline`
alone, then `--locked` alone, then neither, for every cargo invocation —
build, check and test alike.) The other two groups run whole-workspace
commands instead of a test file: `cargo build --offline --locked -p
zebra-chain --tests` and `cargo check --offline --locked --workspace
--all-targets`.

Consequences you must respect:

- The tests reach your code **only through the public API** of `zebra_chain`
  or `zebra_consensus`, as an external consumer would. `pub(crate)`,
  `pub(super)` and private items are invisible to them, even though enum
  variants of a public enum are always reachable regardless of how the enum
  is constructed.
- Each test file is compiled against that crate's `Cargo.toml` **as you leave
  it**. Do not remove an existing `[dependencies]` or `[dev-dependencies]`
  entry from `zebra-chain` or `zebra-consensus`, and do not add a third-party
  crate that is not already somewhere in the workspace. **One dependency
  *version* change is required this milestone and is not an exception to
  that**: `ed25519-zebra` must move off the inherited `"1"` — see §8, which
  also states what `rand_core` has to do in step with it.
- A group whose driver fails to compile fails for that group.
- Tests run with `--test-threads=1` and must not depend on execution order
  between `#[test]` functions in the same file, or on any other group's
  driver having run first.

### How this step is checked

The gate groups are `build`, `workspace-checks` and `inherited-block-codec`.
They add no new requirement and only veto: if any one of them fails, the step
is not satisfied no matter how much of the new work you completed.

The requirement groups carry this step's new capability and its peripheral
breadth; every case in them is in scope.

The practical consequence: preserving the inherited behaviour the gate
checks is necessary but adds nothing by itself, and doing nothing else
leaves all of the new work undone.

**The toolchain you develop on is not the toolchain your work is checked on, and for
this milestone they are far apart.** Your `/app` container runs a modern
stable Rust (1.59). The verifier for *this* step builds and runs everything
under **rustc 1.52.1** — its image is `rust:1.52-buster`, it writes `1.52.1`
into a toolchain file in the image, and `test.sh` exports that as
`RUSTUP_TOOLCHAIN`, which overrides any `rust-toolchain` file in the
workspace. (That is not a quirk of the harness: `lexical-core 0.7.x`, reached
through `librocksdb-sys`/`bindgen`, does not compile on rustc newer than 1.52,
so this milestone's dependency graph has no newer option.)

Two consequences, in the order they will bite:

- Language and library features stabilised **after 1.52.1 do not exist** for
  the verifier, and the 1.53–1.59 window contains three that this codebase
  invites you to reach for:
  - `impl IntoIterator for [T; N]` landed in **1.53**. Under 1.52.1 an array
    is not `IntoIterator` by value at all, so `for x in [a, b]` does not
    compile — write `for x in [a, b].iter()` (or `.to_vec()`) instead.
  - **Edition 2021** needs **1.56**. Do not set `edition = "2021"` in any
    manifest; every crate here is edition 2018 and must stay there. The
    `rust-version` manifest key is also 1.56, and `dep:`-namespaced features
    are cargo 1.60 — a 1.52-era cargo will not parse either.
  - Captured identifiers in format strings (`format!("{x}")`,
    `panic!("{e}")`) are **1.58**. Under 1.52.1 they are an "argument never
    used" error; write `format!("{}", x)`.
- The verifier runs the `build` group (`cargo build -p zebra-chain --tests`)
  first, and **every test-file group is gated on it**: if that build fails,
  each is recorded as *failed* with the reason, never skipped —
  without being run. `build` is a gate group, so the consequence is
  decisive: a failing gate group means the step is not satisfied however much
  of the new work you completed. A single 1.53+ spelling that compiles
  cleanly in your container therefore does not fail just one group or part of
  the step: it fails the whole step. The same error also
  fails `workspace-checks` and `inherited-block-codec`, the other two gate
  groups, so the step fails three times over.

There is no way to run 1.52.1 inside your own container, so the practical
defence is conservatism: write code that would have compiled in mid-2021,
which is the era this milestone's upstream release is from. When in doubt,
prefer the older spelling.

## 2. Where the implementation goes

You are extending existing modules, not adding new ones:

| Module | Declared as (already, from step 1) | What this milestone adds |
| --- | --- | --- |
| `zebra_chain::block` | `pub struct Block`, `pub struct Header`, `pub enum RootHash`; `MAX_BLOCK_BYTES` is defined in the **private** `block::serialize` module, so `zebra_chain::block::MAX_BLOCK_BYTES` does *not* resolve | rename/replace `RootHash` with a fallible `Commitment`/`CommitmentError` pair (§3); add `pub use serialize::MAX_BLOCK_BYTES;` to the `block` module root so the constant is reachable at `zebra_chain::block::MAX_BLOCK_BYTES` (§7 uses it, and a driver in scope imports it by that path); `Header` and `Block` are otherwise unchanged |
| `zebra_chain::parameters` | `pub enum NetworkUpgrade` (7 variants, Genesis..Canopy) | an eighth variant `Nu5`, its consensus branch ID, and the three version-group-ID constants (§4) |
| `zebra_chain::transaction::Transaction` | `pub enum Transaction` (`V1`..`V4`) | a fifth variant, `V5` (§5) |
| `zebra_chain::sapling::{Spend, ShieldedData}` | `pub struct Spend`, `pub struct ShieldedData` (V4-only shape) | generalize both over an `AnchorVariant` type parameter (§6); V4's own wire format and existing accessors are unchanged |
| `zebra_chain::serialization` | `pub trait ZcashDeserialize`, blanket `impl<T: ZcashDeserialize> ZcashDeserialize for Vec<T>` — it reads a compactSize count and then grows a `Vec::new()` as it reads, precisely to avoid preallocating from an attacker's number, but nothing **bounds** that count | the `TrustedPreallocate` trait itself (it does not exist yet at all), the `+ TrustedPreallocate` bound on that blanket impl, an up-front `len > T::max_allocation()` rejection, and a `TrustedPreallocate` impl for every type a compactSize vector in a block or transaction can hold (§7) |
| `zebra_chain::primitives` | `pub use ed25519_zebra as ed25519;`, `pub use redjubjub;`, `pub use x25519_dalek as x25519;` — the `ed25519` re-export **already exists** | nothing new at this path: what changes is the *version* `ed25519_zebra` resolves to, in `zebra-chain/Cargo.toml` (§8) |
| `zebra_consensus` | crate exists, `error` module exists | `MAX_CHECKPOINT_HEIGHT_GAP`, `MAX_CHECKPOINT_BYTE_COUNT`, `Config`, `BoxError`, and the `TransactionError`/`BlockError`/`SubsidyError` variants named in §9 |

Internal file layout is yours; only the names below, at the paths given, are
in scope.

## 3. Block commitment — group `block-commitment` (`zebra-chain`)

Step 1's `RootHash::from_bytes` was infallible and did not check reserved
fields. This milestone replaces it:

```rust
// zebra_chain::block
#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
pub enum Commitment {
    PreSaplingReserved,
    FinalSaplingRoot(sapling::tree::Root),           // sapling::tree::Root(pub [u8; 32])
    ChainHistoryActivationReserved,
    ChainHistoryRoot(ChainHistoryMmrRootHash),        // opaque: no public field or accessor
    ChainHistoryBlockTxAuthCommitment(ChainHistoryBlockTxAuthCommitmentHash), // opaque, NU5-only
}

// Both payload wrappers hold a private `[u8; 32]` and carry the same derives
// — which is what lets `Commitment` derive its own.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
pub struct ChainHistoryMmrRootHash(/* private */ [u8; 32]);
#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
pub struct ChainHistoryBlockTxAuthCommitmentHash(/* private */ [u8; 32]);

#[derive(Error, Debug, PartialEq)]   // `Error` is `thiserror::Error`
pub enum CommitmentError {
    InvalidPreSaplingReserved { actual: [u8; 32] },
    InvalidChainHistoryActivationReserved { actual: [u8; 32] },
    MissingBlockHeight { block_hash: block::Hash },
    // InvalidFinalSaplingRoot, InvalidChainHistoryRoot and
    // InvalidChainHistoryBlockTxAuthCommitment may also exist on the enum,
    // but nothing in this milestone's in-scope surface can construct them —
    // see §10.
}

impl Block {
    pub fn commitment(&self, network: Network) -> Result<Commitment, CommitmentError>;
}
```

Of those derives, `Debug` and `PartialEq` on **both** enums are load-bearing
rather than decorative: most of this group's cases assert with
`assert_eq!` on a whole `Result<Commitment, CommitmentError>` value —
`assert_eq!(block.commitment(network), Ok(Commitment::PreSaplingReserved), ...)`
and `assert_eq!(block.commitment(network), Err(CommitmentError::InvalidPreSaplingReserved { actual: bytes }), ...)`
are both real — and the rest format the value they got with `{:?}` in a
`panic!`. That in turn requires `Debug + PartialEq` on the two opaque payload
wrappers, since the enum's own derives are only as good as its fields'. What
`Debug` *prints* is not checked, and neither are `Copy`/`Clone`/`Eq`/`Serialize`/
`Deserialize`; they are listed because that is the derive set the upstream
release carries, and dropping them costs nothing here but may cost a later
milestone.

`Commitment::from_bytes`/`to_bytes` are not part of the public surface (they
were `pub(super)` in the upstream release this milestone tracks); the only
checked entry point is `Block::commitment`. Dispatch is by
`NetworkUpgrade::current(network, height)` where `height` is
`self.coinbase_height()` — a block with no coinbase height (cleared
transactions) has no commitment at all:

- `Genesis | BeforeOverwinter | Overwinter` → `PreSaplingReserved` if
  `header.commitment_bytes == [0u8; 32]`, else
  `Err(InvalidPreSaplingReserved { actual: header.commitment_bytes })`.
- `Sapling | Blossom` → `FinalSaplingRoot(sapling::tree::Root(header.commitment_bytes))`
  unconditionally — any 32 bytes is accepted as a root at this milestone.
- `Heartwood`, **at exactly its own activation height** → `ChainHistoryActivationReserved`
  if `header.commitment_bytes == [0u8; 32]`, else
  `Err(InvalidChainHistoryActivationReserved { actual: header.commitment_bytes })`.
  This is the one boundary height where the reading is neither
  `FinalSaplingRoot` nor `ChainHistoryRoot`: it is reserved, and MUST NOT be
  read as a root (ZIP-221).
- `Heartwood` (after its activation height) `| Canopy` →
  `ChainHistoryRoot(ChainHistoryMmrRootHash(header.commitment_bytes))`
  unconditionally.
- No block in the fixture corpus reaches `Nu5` height (NU5 has no activation
  height this milestone — §4), so the fifth variant is not exercised; see §10.
- If `self.coinbase_height()` is `None`,
  `Err(MissingBlockHeight { block_hash: self.hash() })`, regardless of network
  or height.

`header.commitment_bytes: [u8; 32]` is read **verbatim**, with no
byte-reversal, and occupies header bytes **68..100**: `version` (4 bytes,
offset 0), `previous_block_hash` (32 bytes, offset 4), `merkle_root` (32
bytes, offset 36), then `commitment_bytes` (32 bytes, offset 68). The same
raw 32 bytes at the same height read as `PreSaplingReserved` on one network
and something else on the other, because Mainnet and Testnet activate
Heartwood at different heights (§4) — the driver checks this at height
903000 (Mainnet's own Heartwood activation, still pre-Heartwood on Testnet).

## 4. NU5 constants — group `nu5-parameters` (`zebra-chain`)

```rust
// zebra_chain::parameters
// The inherited derives stay: the drivers compare these values (`assert_ne!`,
// `assert_eq!`) and `{:?}`-format them, so `Debug` and `PartialEq` (and, for
// `ConsensusBranchId`, `Copy`) must remain on both enums.
pub enum NetworkUpgrade {
    Genesis, BeforeOverwinter, Overwinter, Sapling, Blossom, Heartwood,
    Canopy, Nu5,
}
impl NetworkUpgrade {
    pub fn current(network: Network, height: block::Height) -> NetworkUpgrade;
    pub fn next(network: Network, height: block::Height) -> Option<NetworkUpgrade>;
    pub fn activation_height(&self, network: Network) -> Option<block::Height>;
    pub fn branch_id(&self) -> Option<ConsensusBranchId>;
    pub fn target_spacing(&self) -> chrono::Duration;
    pub fn target_spacing_for_height(network: Network, height: block::Height) -> chrono::Duration;
    pub fn is_max_block_time_enforced(network: Network, height: block::Height) -> bool;
}
pub struct ConsensusBranchId(/* private */ u32);
impl ConsensusBranchId {
    pub fn current(network: Network, height: block::Height) -> Option<ConsensusBranchId>;
}
impl From<ConsensusBranchId> for u32 { /* recovers the raw branch id for comparison */ }

pub const TESTNET_MAX_TIME_START_HEIGHT: block::Height = block::Height(653_606);
pub const OVERWINTER_VERSION_GROUP_ID: u32 = 0x03C4_8270;
pub const SAPLING_VERSION_GROUP_ID: u32 = 0x892F_2085;
pub const TX_V5_VERSION_GROUP_ID: u32 = 0x26A7_270A;
```

**Nu5 has no activation height on either network.** `Nu5.activation_height(_)`
is `None`; `NetworkUpgrade::current(_, height)` must never return `Nu5` and
`NetworkUpgrade::next(_, height)` must never return `Some(Nu5)`, for **every**
`height`, including `Height::MAX` — this is what makes it an announced but
unscheduled upgrade rather than a live one. `Nu5.branch_id()` is
`Some(ConsensusBranchId)` whose `u32` value is `0xf919_a198`, published and
distinct from every earlier upgrade's branch id, even though it cannot be the
*current* branch id yet. `Nu5.target_spacing()` is the same post-Blossom 75
second spacing as Heartwood/Canopy (`target_spacing()` matches on
`Blossom | Heartwood | Canopy | Nu5` together).

**Where the shipped specification disagrees, and why.** `/app/specs/nu5.txt` is
the **final** NU5 protocol specification, written after NU5 had activated: the
opening of its §6, "Network Upgrades", says "A sixth upgrade, called NU5,
activated on Mainnet on 31 May, 2022 at block height 1687104". This milestone
reproduces a release cut while NU5 was still unactivated and still being
specified, so on that one value the tests follow **this document, not the
specification** — `Nu5.activation_height` is `None` on both networks, and an
implementation that hard-codes `1687104` because the spec says so loses
`nu5-parameters`. Nothing else NU5-related diverges: the branch id
`0xF919A198` named in §4.10 is exactly the value this milestone requires, and
§6's Mainnet Overwinter-through-Canopy heights agree with the table below.

Published activation heights (unchanged from step 1, restated here as the
contract, not to be re-derived): Mainnet Overwinter 347500, Sapling 419200,
Blossom 653600, Heartwood 903000, Canopy 1046400; Testnet Overwinter 207500,
Sapling 280000, Blossom 584000, Heartwood 903800, Canopy 1028500; `Genesis` at
height 0 and `BeforeOverwinter` at height 1 on both networks. `Canopy` is the
last *scheduled* upgrade, so `NetworkUpgrade::next` is `None` at and after
Canopy's activation height on both networks (`Nu5` is announced, not
scheduled, so it does not change this).

**The two height-dependent helpers, stated exactly.** Their values are pinned
here rather than derived from the specification's wording:

- `is_max_block_time_enforced(network, height)` is **`true` for every Mainnet
  height**, including `Height(0)` and `Height(1)`, and for Testnet is
  `height >= TESTNET_MAX_TIME_START_HEIGHT`: `false` at the genesis block and
  one block before the start height, `true` from the start height onward and
  at `Height::MAX`. `/app/specs/nu5.txt` §7.6 words the *rule* as applying to
  "each block at block height 2 or greater on Mainnet"; this function is the
  conservative gate the contextual checks call over it, and answering `true`
  for every Mainnet height is the required behaviour. Do not narrow it to
  height 2 or above.
- `target_spacing_for_height(network, height)` switches at the network's own
  Blossom activation height (Mainnet 653600, Testnet 584000): 150 seconds at
  every height below it, 75 seconds from that height onward, on both
  networks. `target_spacing()` is the same value for the upgrade the enum
  member names — 150 seconds for `Genesis | BeforeOverwinter | Overwinter |
  Sapling`, 75 for `Blossom | Heartwood | Canopy | Nu5`.

Published branch ids (unchanged from step 1 except for the new `Nu5` entry):
Overwinter `0x5ba81b19`, Sapling `0x76b809bb`, Blossom `0x2bb40e60`, Heartwood
`0xf5b9230b`, Canopy `0xe9ff75a6`, Nu5 `0xf919a198`. `Genesis` and
`BeforeOverwinter` have no branch id (`branch_id()` is `None`); branch ids
were introduced at Overwinter. `ConsensusBranchId::current(network, height)`
equals `NetworkUpgrade::current(network, height).branch_id()`, so it is also
`None` at heights 0 and 1.

The three version-group-ID constants above must be mutually distinct `u32`
values at exactly the hex values given, reachable at `zebra_chain::parameters`
(re-exported from wherever you define them).

## 5. Transaction v5 skeleton — group `transaction-v5-skeleton` (`zebra-chain`)

```rust
// zebra_chain::transaction
pub enum Transaction {
    V1 { inputs: Vec<transparent::Input>, outputs: Vec<transparent::Output>, lock_time: LockTime },
    V2 { /* unchanged */ },
    V3 { /* unchanged */ },
    V4 {
        lock_time: LockTime,
        expiry_height: block::Height,
        inputs: Vec<transparent::Input>,
        outputs: Vec<transparent::Output>,
        joinsplit_data: Option<JoinSplitData<Groth16Proof>>,
        // **Renamed and reshaped by this milestone.** The field was
        // `shielded_data: Option<ShieldedData>`; it is now
        // `sapling_shielded_data` with the `PerSpendAnchor` parameter below,
        // and the old `value_balance: Amount` field is **gone from `V4`** —
        // it moved into `sapling::ShieldedData` (§6). The drivers
        // pattern-match this arm by its new field name, so a V4 that keeps
        // the old name or the old field does not compile against them.
        sapling_shielded_data: Option<sapling::ShieldedData<sapling::PerSpendAnchor>>,
    },
    V5 {
        lock_time: LockTime,
        expiry_height: block::Height,
        inputs: Vec<transparent::Input>,
        outputs: Vec<transparent::Output>,
        rest: Vec<u8>,
    },
}
impl Transaction {
    pub fn inputs(&self) -> &[transparent::Input];      // all five versions
    pub fn outputs(&self) -> &[transparent::Output];     // all five versions
    pub fn lock_time(&self) -> LockTime;                 // all five versions
    pub fn expiry_height(&self) -> Option<block::Height>; // None for V1/V2, Some for V3/V4/V5
    pub fn is_coinbase(&self) -> bool;                   // all five versions
    pub fn contains_coinbase_input(&self) -> bool;       // all five versions
    pub fn hash(&self) -> Hash;                          // all five versions
}
```

**Wire format on `zcash_serialize`, in this exact field order, nothing
length-prefixed except `inputs`/`outputs` themselves (each a compactSize
count then that many entries)**:

| Bytes | Field | Value |
| --- | --- | --- |
| 0..4 | header | `(5u32 \| (1 << 31)).to_le_bytes()` = `[0x05, 0x00, 0x00, 0x80]` |
| 4..8 | version group id | `TX_V5_VERSION_GROUP_ID.to_le_bytes()` = `[0x0a, 0x27, 0xa7, 0x26]` |
| 8..12 | `lock_time` | `LockTime`'s own 4-byte encoding (unchanged from V1-V4) |
| 12..16 | `expiry_height` | `expiry_height.0.to_le_bytes()` (little-endian `u32`) |
| 16.. | `inputs` | compactSize count, then each `transparent::Input` |
| .. | `outputs` | compactSize count, then each `transparent::Output` |
| .. end | `rest` | written verbatim with `write_all`, **no length prefix of its own** |

A V5 transaction is built and inspected only through the struct literal and
the accessors above in this milestone's driver — full deserialization of an
arbitrary V5-shaped byte string is not exercised; see §10 for why.

`Transaction::hash()` must cover **every serialized field** of the
transaction: it is computed from the transaction's own byte encoding, so
changing any field the wire format writes — `expiry_height`, a byte of
`rest`, an input, an output — changes the hash, and two otherwise-identical
transactions hash alike. The driver checks both directions.

Version-group-id-per-version, restated as the contract every corpus
transaction must satisfy: V1/V2 have no `fOverwintered` bit at all (header is
the bare version number, `1` or `2`, no group id follows); V3 sets the bit and
is followed by `OVERWINTER_VERSION_GROUP_ID`; V4 sets the bit and is followed
by `SAPLING_VERSION_GROUP_ID`; only V3/V4/V5 carry a version-group id at all.
A V4-versioned header followed by the wrong group id (e.g.
`OVERWINTER_VERSION_GROUP_ID` instead of `SAPLING_VERSION_GROUP_ID`) is
`Err(SerializationError::Parse(_))`. The following 4-byte headers are never
legal for any transaction, and deserializing a bare header of just those 4
bytes must be `Err(SerializationError::Parse(_))`: the 4-byte words
`0x0000_0000` (version `0`); `0x8000_0001` and `0x8000_0002` (versions `1` and
`2` with `fOverwintered` set); `0x0000_0003` and `0x0000_0004` (versions `3`
and `4` **without** `fOverwintered`); `0x8000_0006` (`fOverwintered` set and
a 31-bit version of `6`, which is neither 3, 4 nor 5); and `0xffff_ffff`
(`fOverwintered` set with the maximum 31-bit version, `0x7fff_ffff`). Each is
rejected on its four bytes alone — the reader must not wait for more input to
decide.

## 6. Sapling shielded data over an anchor variant — group `sapling-shielded-data-anchors` (`zebra-chain`)

`Spend` and `ShieldedData` are generalized over an `AnchorVariant` type
parameter so the same generic types will describe both V4's per-spend-anchor
bundles and a future V5's single shared-anchor bundle. This milestone changes
none of V4's wire format or existing behaviour; it only makes the type
generic.

```rust
// zebra_chain::sapling
//
// Both associated types are bounded, and the bounds are load-bearing: they are
// what lets `Spend<AnchorV>` derive `Clone`/`Debug`/`PartialEq`/`Eq` for every
// anchor variant, which the driver relies on (it compares `Vec<Spend<
// PerSpendAnchor>>` values with `assert_eq!`). `DeserializeOwned` is
// `serde::de::DeserializeOwned`.
pub trait AnchorVariant {
    type PerSpend: Clone + Debug + DeserializeOwned + Serialize + Eq + PartialEq;
    type Shared:   Clone + Debug + DeserializeOwned + Serialize + Eq + PartialEq;
}

#[derive(Copy, Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
pub struct PerSpendAnchor; // AnchorVariant::PerSpend = sapling::tree::Root, ::Shared = FieldNotPresent
#[derive(Copy, Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
pub struct SharedAnchor;   // AnchorVariant::PerSpend = FieldNotPresent, ::Shared = sapling::tree::Root
#[derive(Copy, Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
pub struct FieldNotPresent;

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
pub struct Spend<AnchorV: AnchorVariant> {
    pub cv: commitment::ValueCommitment,
    pub per_spend_anchor: AnchorV::PerSpend,
    pub nullifier: note::Nullifier,
    pub rk: redjubjub::VerificationKeyBytes<SpendAuth>,
    pub zkproof: Groth16Proof,
    pub spend_auth_sig: redjubjub::Signature<SpendAuth>,
}

// The three fields that hold the spends and outputs are `pub`, like the other
// three: the driver reads `rest_spends`/`rest_outputs` and also pops an
// `Output` off `rest_outputs` on a clone of a bundle. Per §1, anything less
// than `pub` is invisible to it.
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct ShieldedData<AnchorV>
where
    AnchorV: AnchorVariant + Clone,
{
    pub value_balance: Amount,
    pub shared_anchor: AnchorV::Shared,
    // first + rest_spends + rest_outputs together hold >=1 spend-or-output
    pub first: Either<Spend<AnchorV>, Output>,
    pub rest_spends: Vec<Spend<AnchorV>>,
    pub rest_outputs: Vec<Output>,
    pub binding_sig: redjubjub::Signature<Binding>,
}
impl<AnchorV: AnchorVariant + Clone> ShieldedData<AnchorV> {
    pub fn spends(&self) -> impl Iterator<Item = &Spend<AnchorV>> + Clone;
    pub fn outputs(&self) -> impl Iterator<Item = &Output> + Clone;
    pub fn nullifiers(&self) -> impl Iterator<Item = &note::Nullifier>;      // from spends, in order
    pub fn note_commitments(&self) -> impl Iterator<Item = &jubjub::Fq>;      // from outputs, in order
    pub fn binding_verification_key(&self) -> redjubjub::VerificationKeyBytes<Binding>;
}

// `spends_per_anchor` needs a bound the other methods do not, so it lives in
// its own impl block: producing a `Spend<PerSpendAnchor>` from a
// `Spend<AnchorV>` requires the conversion to exist for that variant.
impl<AnchorV> ShieldedData<AnchorV>
where
    AnchorV: AnchorVariant + Clone,
    Spend<PerSpendAnchor>: From<(Spend<AnchorV>, AnchorV::Shared)>,
{
    pub fn spends_per_anchor(&self) -> impl Iterator<Item = Spend<PerSpendAnchor>> + '_;
}

// The two conversions that make that bound satisfiable for both variants.
impl From<(Spend<SharedAnchor>, sapling::tree::Root)> for Spend<PerSpendAnchor> { /* ... */ }
impl From<(Spend<PerSpendAnchor>, FieldNotPresent)> for Spend<PerSpendAnchor> { /* ... */ }
```

`ShieldedData`'s own `PartialEq` is hand-written rather than derived (two
bundles holding the same spends and outputs compare equal even when a
different one of them sits in `first`), but nothing in this milestone's in-scope
surface compares two bundles, so only the derives above are required. What
*is* required, and is what the four counts above come to: `Spend<AnchorV>`
must be `Clone + Debug + PartialEq`, and `ShieldedData<AnchorV>` must be
`Clone`. `binding_verification_key` returns the **bytes** form,
`VerificationKeyBytes<Binding>`, not `VerificationKey<Binding>` — the caller
does the `try_from` (see below).

A `Transaction::V4`'s `sapling_shielded_data: Option<ShieldedData<PerSpendAnchor>>`
field is what this group exercises (11 such bundles exist across the 17-block
corpus, holding 2 spends and 13 outputs in total). For `PerSpendAnchor`,
`AnchorV::Shared = FieldNotPresent` — a V4 bundle's `shared_anchor` field is
always that zero-sized marker type, never a real anchor.
`ShieldedData::spends_per_anchor()` — declared above, yielding
`Spend<PerSpendAnchor>` **by value**, borrowing the bundle for the lifetime of
the iterator — attaches the one shared anchor to each spend for a
`SharedAnchor` bundle, and is the identity when called on an
already-`PerSpendAnchor` bundle: for every corpus bundle,
`spends_per_anchor().collect::<Vec<_>>()` equals `spends().cloned().collect()`.

**A bundle always holds at least one spend or output** — `first` is a spend
if and only if `spends().count() > 0`, and disjoint from `outputs()`.

**Byte layout, `Spend<PerSpendAnchor>`, exactly 384 bytes**: `cv` (32, offset
0), `per_spend_anchor` (32, offset 32), `nullifier` (32, offset 64), `rk` (32,
offset 96), `zkproof` (192, offset 128), `spend_auth_sig` (64, offset 320).

**Byte layout, `Output`, exactly 948 bytes**: `cv` (32, offset 0), `cm_u` (32,
offset 32), `ephemeral_key` (32, offset 64), `enc_ciphertext` (580, offset
96), `out_ciphertext` (80, offset 676), `zkproof` (192, offset 756). An
`Output` is structurally distinct from, and a different size than, a `Spend`.

**Binding signature**: `sd.binding_verification_key()` returns a
`redjubjub::VerificationKeyBytes<Binding>` derived from the bundle's value
commitments and `value_balance` (it does not read `binding_sig`).

The derivation is checked — real corpus signatures are verified under the key
it returns, so an implementation that returns *a* deterministic key derived
some other way fails the group. It is the Sapling **binding validating key**
`bvk` of nu5 §4.13 "Balance and Binding Signature (Sapling)", specialised to
this bundle:

```
bvk = (Σ over spends of spend.cv)
    − (Σ over outputs of output.cv)
    − ValueCommitment::new(jubjub::Fr::zero(), self.value_balance)
```

— that is, the homomorphic sum of every `Spend.cv`, minus the homomorphic sum
of every `Output.cv`, minus a value commitment to `value_balance` taken with
randomness **zero**. The `Add`/`Sub`/`Sum` impls on
`commitment::ValueCommitment`, and `ValueCommitment::new(rcv, value)`, are the
step-1 surface this uses; the resulting point is converted to `[u8; 32]` and
that is the `VerificationKeyBytes<Binding>` returned. `ValueCommitment` is the
Sapling homomorphic Pedersen commitment of nu5 §5.4.8.3. Because the sums run
over all spends and all outputs, the key changes if any `Output` is added or
removed — which is what the "deterministic, and commits to the outputs" test
checks.

For every non-coinbase bundle in the corpus (at least 10 of the 11),
`redjubjub::VerificationKey::<Binding>::try_from(sd.binding_verification_key())`
then `.verify(sighash, &sd.binding_sig)` succeeds, where `sighash` is
`tx.sighash(upgrade, transaction::HashType::ALL, None)` and `upgrade` is
`NetworkUpgrade::current(fixture.network, fixture.height)`. (That `try_from` is
the one that only type-checks because the method returns the *bytes* form.)
Transparent-only transactions have `sapling_shielded_data == None`.

## 7. TrustedPreallocate DoS hardening — group `trusted-preallocate-limits` (`zebra-chain`)

```rust
// zebra_chain::serialization
pub trait TrustedPreallocate { fn max_allocation() -> u64; }
// The trait above is new this milestone. So is the `+ TrustedPreallocate`
// bound below: step 1 ships the same blanket impl *without* it, growing a
// `Vec::new()` element by element and bounding the count not at all.
impl<T: ZcashDeserialize + TrustedPreallocate> ZcashDeserialize for Vec<T> {
    // reads a compactSize `len`; if `len > T::max_allocation()`, returns
    // Err(SerializationError::Parse("Vector longer than max_allocation"))
    // *before* allocating or reading any element; otherwise allocates
    // exactly `len` capacity and reads `len` elements.
}
// New at this path too: step 1 has this value only as a *private* const in
// `zebra-network`'s external codec. It must become `pub` in
// `zebra_chain::serialization`, because the allocation limits below are
// derived from it.
pub const MAX_PROTOCOL_MESSAGE_LEN: usize = 2 * 1024 * 1024;
```

Every type that can appear in a compactSize-prefixed `Vec<T>` inside a block
or a transaction must implement `TrustedPreallocate`, with `max_allocation()`
computed from `MAX_PROTOCOL_MESSAGE_LEN` or `MAX_BLOCK_BYTES` (`= 2_000_000`; the
constant already exists in the private `block::serialize` module, but the path
`block::MAX_BLOCK_BYTES` does not resolve until you add the re-export named in §2)
divided by that type's minimum possible wire size — not
measured from real data, so an attacker who claims a huge count for tiny
elements is rejected before any allocation happens:

| Type | `max_allocation()` formula | Value |
| --- | --- | --- |
| `block::Hash` | `(MAX_PROTOCOL_MESSAGE_LEN - 1) / 32` | 65535 |
| `block::CountedHeader` | `(MAX_PROTOCOL_MESSAGE_LEN - 1) / (BLOCK_HEADER_LENGTH + 1)`, header length = version(4) + previous_block_hash(32) + merkle_root(32) + commitment_bytes(32) + time(4) + difficulty_bits(4) + nonce(32) + equihash solution's own 3-byte compactSize prefix + a 1344-byte solution = 1487, so `BLOCK_HEADER_LENGTH + 1` = 1488 | 1409 |
| `Arc<transaction::Transaction>` | `MAX_BLOCK_BYTES / MIN_TRANSPARENT_TX_SIZE`, tx size = input(41) + locktime(4) + output(9) = 54 | 37037 |
| `transparent::Input` | `MAX_BLOCK_BYTES / MIN_TRANSPARENT_INPUT_SIZE`, input size = outpoint(36) + sequence(4) + script(1) = 41 | 48780 |
| `transparent::Output` | `MAX_BLOCK_BYTES / MIN_TRANSPARENT_OUTPUT_SIZE`, output size = value(8) + script(1) = 9 | 222222 |
| `sapling::Output` | `(MAX_BLOCK_BYTES - 1) / OUTPUT_SIZE`, `OUTPUT_SIZE = 32+32+32+580+80+192 = 948` | 2109 |
| `sapling::Spend<SharedAnchor>` | `(MAX_BLOCK_BYTES - 1) / SAPLING_SPEND_FULL_SIZE`, size = (32+32+32) + 192 + 64 = 352 | 5681 |
| `sapling::Spend<PerSpendAnchor>` | `(MAX_BLOCK_BYTES - 1) / SAPLING_SPEND_PER_ANCHOR_SIZE`, size = 352 + 32 = 384 | 5208 |
| `sprout::JoinSplit<Bctv14Proof>` | `(MAX_BLOCK_BYTES - 1) / (JOINSPLIT_WITHOUT_ZKPROOF + 296)`, without-proof size = 1506 | 1109 |
| `sprout::JoinSplit<Groth16Proof>` | `(MAX_BLOCK_BYTES - 1) / (JOINSPLIT_WITHOUT_ZKPROOF + 192)` | 1177 |

(`JOINSPLIT_WITHOUT_ZKPROOF` = `vpub_old`(8) + `vpub_new`(8) + `anchor`(32) +
2 nullifiers(32 each = 64) + 2 commitments(32 each = 64) + `ephemeral_key`(32) +
`random_seed`(32) + 2 macs(32 each = 64) + 2 ciphertexts(601 each = 1202) =
1506 bytes.)

**`Vec<u8>` is not gated by `TrustedPreallocate`** (there is no
`impl TrustedPreallocate for u8`); it has its own hand-written
`ZcashDeserialize` impl whose limit is `MAX_PROTOCOL_MESSAGE_LEN - 5` (a
5-byte compactSize prefix is the most any length in this range can cost, so
that is the longest byte vector an honest peer's message can contain). A
`Vec<u8>` claiming exactly `MAX_PROTOCOL_MESSAGE_LEN - 5` bytes, backed by
enough real data, must deserialize successfully and byte-for-byte; one
claiming `MAX_PROTOCOL_MESSAGE_LEN - 4` must be
`Err(SerializationError::Parse(_))`, checked from the length alone before any
read of the (possibly absent) body.

For a `TrustedPreallocate` type such as `block::Hash`: a claimed count one
more than `Hash::max_allocation()`, with **no element bytes supplied at all**,
must still be `Err(SerializationError::Parse(_))` — proving the gate runs
before the read loop, not as a side effect of running out of data. A claimed
count of exactly `Hash::max_allocation()`, also with no element bytes
supplied, must instead be `Err(SerializationError::Io(_))` — the gate passes,
and only then does reading the (absent) elements fail.

## 8. ZIP-215 Ed25519 batch verification — group `zip215-ed25519` (`zebra-chain`)

```rust
// zebra_chain::primitives
pub use ed25519_zebra as ed25519;
```

`ed25519-zebra` is a pinned external crate that already implements ZIP-215.
**Step 1 pins it at `ed25519-zebra = "1"`, and version 1.x does not implement
ZIP-215 — raising that pin is the new work of this group.** The upstream
release this milestone tracks moves `zebra-chain/Cargo.toml` to
`ed25519-zebra = "2.2.0"`. That is a version bump of a dependency the crate
already declares, not a new third-party dependency, and `2.2.0` is already
resolved in the workspace `Cargo.lock` you inherit (`tower-batch`
dev-depends on it), so the bump does not introduce an unlocked crate.

Two things must move together. `ed25519-zebra` 2.x takes its RNG through
`rand_core`, and the driver **constructs its own RNG** — it `use`s
`rand_core::{CryptoRng, RngCore}`, implements them for a deterministic test
RNG, and hands that to `batch::Verifier::verify`. Those traits are resolved
through `zebra-chain`'s *own* `rand_core` dependency, so **the `rand_core`
version `zebra-chain` declares must be the same one the resolved
`ed25519-zebra` uses**, or the driver will not compile and this group fails. Step 1 declares `rand_core = "0.5.1"`. Both of these are internally
consistent and either will compile:

- registry `ed25519-zebra = "2.2.0"` → `rand_core 0.5.1`, which is what step
  1 already declares and already has locked;
- the `[patch.crates-io]` git pin the upstream release uses
  (`ZcashFoundation/ed25519-zebra`, rev `539fad04`) → `rand_core 0.6.2`,
  which also requires bumping `zebra-chain`'s `rand_core` to `"0.6"`.

Mixing them — for example the git pin with `rand_core = "0.5.1"` still
declared — does not compile, because the driver's RNG would implement the
wrong crate's traits. Nothing in this group checks *which* of the two you
pick; it checks that the re-exported API is ZIP-215's and that a test can
drive the batch verifier with its own RNG.

What version 2.x gives you, and 1.x does not, is the cofactored verification
equation — under which every canonical **and** every non-canonical encoding of
a low-order point is a valid signature or verification key, and batch
verification agrees with individual verification on every input, valid or not.
**This milestone's new work is the dependency version and the re-export
staying intact, not the cryptography** — do not reimplement Ed25519 or ZIP-215
batching; depend on the crate and re-export it unchanged. The driver imports,
and therefore requires visible at
`zebra_chain::primitives::ed25519`:

- `Signature`, `SigningKey`, `VerificationKey`, `VerificationKeyBytes` — the
  crate's own key/signature types, with their own `From`/`TryFrom` byte
  conversions.
- `Error` — the crate's own error type, with at least the variants
  `InvalidSignature`, `MalformedPublicKey` and `InvalidSliceLength` (the last
  is what a wrong-width byte slice converts to, and a case of §8's driver
  asserts it for a 31-byte key and a 63-byte signature; the other two are
  also relied on by §9's `TransactionError::Ed25519` conversion).
- `batch` — the module providing batch verification (a `Verifier` you add
  `(VerificationKeyBytes, Signature, &[u8])` items to, then verify with a
  caller-supplied `rand_core::{CryptoRng, RngCore}` source); single
  verification is `VerificationKey::verify(&self, &Signature, &[u8]) -> Result<(), Error>`.

**Where the rules are written down.** The normative statement of the three
rules is `/app/specs/zips/zip-0215.rst`, "Specification" — three bullets: both
`A` and `R` MUST be encodings of points on Ed25519; `S` MUST represent an
integer less than `ℓ`; and `[8][S]B = [8]R + [8][k]A` MUST be satisfied, with
an explicit note that the cofactorless equation `[S]B = R + [k]A` MUST NOT be
used and that `A` and `R` are *not* required to be canonical encodings.
`nu5.txt` §5.4.6 states the same rules in its "[Canopy onward]" form and is
worth reading for `ℓ`, `B`, `a`, `d` and the encoding of a signature; its
note that "it is not required that the integer encoding of the *y*-coordinate
of the points represented by `R` or `A` are less than 2^255 − 19" is the
non-canonical-encoding rule. `nu5.txt` Appendix B.3, "Ed25519 batch
validation", describes the batch optimisation itself.

Two warnings about §5.4.6, because it is easy to read the wrong thing out of
it. First, the only *table* in §5.4.6 is `PreCanopyExcludedPointEncodings`
— the eleven encodings that were **rejected** before Canopy. ZIP-215 abolishes
that rule ("The language about ExcludedPointEncodings ... no longer applies"),
so from Canopy onward those encodings are *accepted*. Do not implement that
table as a rejection list. Second, that table is **not** where the
driver's key material comes from: the driver derives its own low-order-point
encodings from the curve equation and carries them as literals, so you cannot
satisfy this group by special-casing the spec's eleven values.

Test vectors used: RFC 8032 §7.1's Ed25519 sign/verify vectors (TEST 1 and
TEST 2), the eight canonical encodings of the 8-torsion subgroup, four
non-canonical encodings that still decode into it, one `y` that is not on the
curve at all, and `ℓ` itself little-endian. A conforming implementation accepts
every low-order encoding as a valid verification key and reaches the same
verdict on a signature whether it is checked individually or in a batch, and
whether the encoding used was canonical or not. A scalar `S` is accepted only
when `S < ℓ`; `ℓ - 1` is accepted, `ℓ` is not.

## 9. Peripheral consensus surface — group `peripheral-consensus-surface` (`zebra-consensus`)

```rust
// zebra_consensus
pub const MAX_CHECKPOINT_HEIGHT_GAP: usize = 400;
pub const MAX_CHECKPOINT_BYTE_COUNT: u64 = 32 * 1024 * 1024; // 33_554_432
pub type BoxError = Box<dyn std::error::Error + Send + Sync + 'static>;
#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct Config { pub checkpoint_sync: bool }
impl Default for Config { /* checkpoint_sync: false */ }

// zebra_consensus::error
#[derive(Debug, PartialEq)]
pub enum SubsidyError {
    NoCoinbase,               // "no coinbase transaction in block"
    FoundersRewardNotFound,
}
#[derive(Debug, PartialEq)]
pub enum TransactionError {
    CoinbasePosition,
    CoinbaseInputFound,
    CoinbaseHasJoinSplit,
    CoinbaseHasSpend,
    CoinbaseHasOutputPreHeartwood,
    Subsidy(SubsidyError),                       // #[from] SubsidyError
    WrongVersion,
    NoInputs,
    NoOutputs,
    BadBalance,
    Script(zebra_script::Error),                 // #[from]
    SmallOrder,
    Groth16,
    Ed25519(zebra_chain::primitives::ed25519::Error),   // #[from]
    RedJubjub(zebra_chain::primitives::redjubjub::Error), // NOT #[from]; see BoxError impl below
    InternalDowncastError(String),
}
impl From<BoxError> for TransactionError {
    // downcasts the boxed error to redjubjub::Error: Ok => RedJubjub(*e);
    // Err => InternalDowncastError(format!("...{:?}", e))
}
#[derive(Debug, PartialEq)]
pub enum BlockError {
    Transaction(TransactionError),   // #[from] TransactionError
    NoTransactions,
    DuplicateTransaction,
    // BadMerkleRoot, AlreadyInChain, MissingHeight, MaxHeight,
    // InvalidDifficulty, TargetDifficultyLimit, DifficultyFilter also exist,
    // unchanged from before this milestone and not touched by this group.
}
impl From<SubsidyError> for BlockError {
    // BlockError::Transaction(TransactionError::Subsidy(err))
}
```

`MAX_CHECKPOINT_HEIGHT_GAP` (block count) and `MAX_CHECKPOINT_BYTE_COUNT`
(serialized byte budget) are two independent limits on how large a single
mandatory checkpoint span may be: the byte budget is the one that actually
binds in the worst case (`MAX_CHECKPOINT_HEIGHT_GAP` maximum-size blocks would
be far more than `MAX_CHECKPOINT_BYTE_COUNT`), while any single block, even
the largest legal one (`block::MAX_BLOCK_BYTES = 2_000_000`), always fits
inside one checkpoint's byte budget. `Config::default().checkpoint_sync` is
`false` (mandatory Canopy-and-earlier checkpoints always apply regardless;
this flag only controls optional post-Canopy ones); `Config` is `Clone` and
has a non-empty `Debug` output.

`ed25519::Error::InvalidSignature` and `::MalformedPublicKey` each `.into()` a
distinct `TransactionError::Ed25519(_)` that preserves which one it was. A
`BoxError` wrapping a `redjubjub::Error::InvalidSignature` converts via
`TransactionError::from(boxed)` to `TransactionError::RedJubjub(redjubjub::Error::InvalidSignature)`;
a `BoxError` wrapping anything else converts to `TransactionError::InternalDowncastError(_)`,
never to `RedJubjub`. `TransactionError::SmallOrder` is a variant in its own
right, distinct from `Ed25519(_)`, `Groth16` and `BadBalance`.
`BlockError::from(SubsidyError::NoCoinbase) == BlockError::Transaction(TransactionError::Subsidy(SubsidyError::NoCoinbase))`;
`BlockError::from(TransactionError::CoinbasePosition) == BlockError::Transaction(TransactionError::CoinbasePosition)`;
`BlockError::NoTransactions != BlockError::DuplicateTransaction`.

## 10. Regression guard — group `inherited-block-codec` (GATE — adds no requirement, vetoes the step if it fails; `zebra-chain`)

A trimmed copy of step 1's block-codec driver, run against the same 17-block
corpus (`fixtures/blocks.tsv` + `fixtures/blocks/*.hex`, unchanged since step
1): every block still round-trips byte-for-byte and parsing consumes exactly
the block's own bytes and no more; `Block::hash()`, `coinbase_height()`,
`transactions.len()` (cross-checked against an independent compactSize read
at byte offset 1487) and the re-serialized length still match the corpus; the
first transaction of every block is still a coinbase and no other is; the
merkle root computed from `Transaction::hash()` over all of a block's
transactions still matches `header.merkle_root`; the 2,000,000-byte block
limit (`block::MAX_BLOCK_BYTES`) is still enforced deserializing from both
sides (built from a real header plus repeated real transactions, not
synthetic data); a `u64::MAX`-claimed transaction count is still rejected as
an error rather than attempted as an allocation; a truncated block is still
`SerializationError::Io`; trailing bytes after one block are left on the
reader for a second, independent parse; `Block`'s `Display` still names both
height and hash. This exists because this milestone changes `Transaction`
(a new `V5` variant), `sapling::Spend`/`ShieldedData` (now generic), and
`block::Commitment` (renamed and made fallible) — all of which every
transaction and every block, V5 or not, depends on to parse and serialize at
all. `Block`, `Header`, `transparent::Input`/`Output` and the V1-V4
transaction wire formats are otherwise unchanged from step 1.

## 11. Negative space — what this milestone does not ask for

- **Parsing or giving structure to the Sapling/Orchard body of a v5
  transaction.** `Transaction::V5.rest` is opaque bytes; `sprout_nullifiers()`
  and `sapling_nullifiers()` are explicitly allowed to `unimplemented!()` for
  `Transaction::V5` at this milestone (they are not called by any driver
  here).
- **Full round-trip deserialization of a directly-constructed `V5`
  transaction.** This group's driver builds `Transaction::V5` values only
  through the struct literal, and only serializes them; it never feeds a
  V5-shaped byte string back through `Transaction::zcash_deserialize`. This
  is deliberate, not an oversight: the milestone's own reference build's
  decode path and encode path do not agree on whether a V5 header's
  `fOverwintered` bit is set (the encoder sets it per the byte table in §5;
  the decoder's dispatch was written expecting it clear), so no implementation
  satisfying *both* the encode-side byte layout in §5 *and* a from-scratch
  decode of that same layout can exist without picking one side to fix. Only
  the encode-side byte layout is checked. Do not treat a V5 value's ability to
  round-trip through decode as a requirement; §5's byte table is authoritative
  for what must be produced, not for what must be re-accepted.
- **The `Commitment::ChainHistoryBlockTxAuthCommitment` variant's reading, and
  three of `CommitmentError`'s variants** (`InvalidFinalSaplingRoot`,
  `InvalidChainHistoryRoot`, `InvalidChainHistoryBlockTxAuthCommitment`). NU5
  has no activation height (§4), so no corpus block ever reaches the fifth
  commitment reading, and post-Sapling commitments accept any 32 bytes at
  this milestone (§3), so the two "invalid root" errors have no path that
  raises them yet. Their names appear here only because a corpus block from a
  future, NU5-active chain would need them; nothing this milestone ships is
  required to construct them.
- **The `SharedAnchor` arm of `spends_per_anchor()` and of the `From`
  <(Spend<SharedAnchor>, tree::Root)> conversion.** The type-level plumbing
  must exist (§6) — the V5 variant's parameter depends on it — but every
  bundle in the corpus is a V4 `PerSpendAnchor` bundle, so no case in scope
  constructs or calls the `SharedAnchor` forms; only the
  `PerSpendAnchor` identity is checked.
- **The batching *service* and *invocation policy*** for Ed25519 and RedJubjub
  signatures — `zebra_consensus`'s async `tower` verification services are
  internal machinery out of scope; only the error enumeration in §9 that a
  batch rejection must surface through is checked.
- **Groth16, RedJubjub, RedPallas or Ed25519 cryptographic internals** — all
  pinned external crates (`redjubjub`, `ed25519-zebra`); §8 states exactly
  what must be visible and at what version, nothing about their internal
  algorithms is reimplemented or checked.
- **Contextual and semantic validation** of `FinalSaplingRoot` or
  `ChainHistoryRoot` against a real Sapling note-commitment tree or a real
  Merkle Mountain Range — both are accepted as opaque 32-byte values this
  milestone (§3).
- The standing exclusions that hold for every milestone in this ladder, none
  of which this step changes: the transparent **script interpreter**
  (`zcash_script`, a pinned C++ dependency — only the FFI wrapper's contract
  is ever in scope); **proof-system and signature-scheme internals**
  (Groth16 via `bellman`/`zcash_proofs`, Halo2, `redjubjub`, `reddsa`,
  `ed25519-zebra`, Equihash solution verification, note encryption, pairing
  and curve arithmetic — what is checked is the encoding of their inputs and
  the consensus rules deciding when each is invoked); **mining, proving,
  wallet, key management and note decryption**; the **`zcashd` RPC surface**;
  **`lightwalletd`**; **Tor** and any anonymity-network transport; and every
  **NU6-era rule**.

## 12. Handoff

**Inherited from step 1**: the V1-V4 transaction codec, the block/header
codec, the 17-block regression corpus, and the `ZcashSerialize`/
`ZcashDeserialize` traits with their blanket `Vec<T>` impl — all unchanged
except where this milestone explicitly says otherwise (§3's `Commitment`
rename, §6's generic `Spend`/`ShieldedData`, §7's allocation bound on that
blanket impl). `TrustedPreallocate` itself is **not** inherited: it does not
exist at step 1 and is new here.

**What step 2 leaves for step 3**: an eighth, unscheduled `NetworkUpgrade`
variant with a published branch id and no activation height; a `Transaction::V5`
skeleton whose transparent bundle and `rest` bytes round-trip on the encode
side; `Spend`/`ShieldedData` already generic over `AnchorVariant`, ready for a
`SharedAnchor` bundle to be given real structure instead of parsing into
`rest`; a `Commitment` enum whose fifth, NU5-only reading exists in name but
is not yet reachable by any real block; and a `zebra_consensus` error surface
that already names the Ed25519/RedJubjub signature-verification rules a
future step's batching services will actually enforce. Step 3 is expected to
find all of the above in place and unchanged in its public shape.
