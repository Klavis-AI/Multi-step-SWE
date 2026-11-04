# TEST_INTERFACE.md — milestone 5, chain history, Sprout freeze, batched RedPallas

This document is the contract the tests are written against. Every
module path, type, method, field, constant, error variant, message string and
byte layout the verifier depends on is named here. If a behaviour is not
stated here or in a specification document named by `instruction.md`, it is
not in scope.

## 1. How the verifier reaches your code

The verifier runs nine groups. Seven run **integration test files**, copied
into two crates' `tests/` directories:

```
cargo test --offline --locked --jobs 4 -p zebra-chain     --test <name> -- --test-threads=1
cargo test --offline --locked --jobs 4 -p zebra-consensus --test <name> -- --test-threads=1
```

(If `--offline --locked` cannot resolve, the verifier retries with `--offline`
alone, then `--locked` alone, then neither, for every cargo invocation —
build, check and test alike.) The other two groups run whole-workspace
commands instead of a test file:

```
cargo build --offline --locked -p zebra-chain --tests
cargo check --offline --locked --workspace --all-targets --all-features
```

Note `--all-features` on the check. `zebra-chain/benches/block.rs` uses
`zebra_chain::block::tests::generate::*`, and `block.rs` declares that module
as `#[cfg(any(test, feature = "bench"))] pub mod tests;`. A bare
`--all-targets` check therefore does not compile the benches, and the
`--all-features` form is what the verifier runs and what upstream CI runs.
Keep the `bench` feature and every `[[bench]]` entry already in
`zebra-chain/Cargo.toml` compiling under it. You are not required to add a
benchmark for anything this milestone introduces.

Consequences you must respect:

- The tests reach your code **only through the public API** of whichever
  crate they are compiled into (`zebra_chain` or `zebra_consensus`), as an
  external consumer. `pub(crate)`, `pub(super)` and private items are
  invisible to them — including a public method on a type declared inside a
  private module. Enum variants of a public enum are always reachable.
- Each test file is compiled against that crate's `Cargo.toml` **as you leave
  it**. Do not remove an existing `[dependencies]` or `[dev-dependencies]`
  entry from `zebra-chain` or `zebra-consensus`. In particular the drivers
  use `hex`, `rand_core` and `zebra-chain` itself, all already present.
- The workspace must keep building **offline**. `halo2`, `group`, `rand_core`
  and `hex` are already in the workspace you inherit. `zcash_history` is
  **not**, and adding it is part of this milestone — **required**, spelled
  exactly as here:

  ```toml
  zcash_history = { git = "https://github.com/zcash/librustzcash.git", rev = "0c3ed159985affa774e44d10172d4471d798a85a" }
  zcash_primitives = { git = "https://github.com/zcash/librustzcash.git", rev = "0c3ed159985affa774e44d10172d4471d798a85a" }
  bigint = "4"
  uint = "0.9.1"
  ```

  The rev must be pinned exactly: the verifier resolves `--offline` against a
  cache primed for that rev, so any other revision fails to fetch. All four
  crates are in that cache. `zcash_history` is the one the module wraps;
  `bigint` (with `uint` and `zcash_primitives` as its companions) is what the
  ZIP-221 node fields are typed over — a leaf's `subtree_total_work` is a
  `bigint::U256` — so add whichever of the four your implementation names. Add
  no *other* new third-party dependency to any manifest.
- A group whose driver fails to compile fails for that group.
- Tests run with `--test-threads=1` and must not depend on execution order
  between `#[test]` functions in the same file.

### How this step is checked

The gate groups are `build`, `workspace-checks`, `inherited-block-codec` and
`inherited-sighash`. They add no new requirement and only veto: if any one of
them fails, the step is not satisfied no matter how much of the new work you
completed.

The requirement groups carry this step's new capability and its peripheral
breadth; every case in them is in scope.

The practical consequence: preserving the inherited behaviour the gate
checks is necessary but adds nothing by itself, and doing nothing else
leaves all of the new work undone.

The two fixture files the drivers read are unchanged from earlier steps:
`$HBV_FIXTURES/blocks.tsv` (17 real mainnet/testnet blocks, with a
`root_bytes_hex` column holding each header's 32-byte commitment field) plus
`$HBV_FIXTURES/blocks/*.hex`, and `$HBV_FIXTURES/sighash.tsv` (the five
ZIP-143/ZIP-243 worked examples).

## 2. Where the implementation goes

| Module | Declared as | What this milestone adds |
| --- | --- | --- |
| `zebra_chain::primitives::zcash_history` | **new**, `pub mod zcash_history;` inside the existing `pub mod primitives` | the ZIP-221 MMR wrapper (§3) |
| `zebra_chain::block` | `pub mod block;` | re-export `ChainHistoryMmrRootHash`; `From<[u8; 32]>` for it; `Block::check_transaction_network_upgrade_consistency` (§3, §6) |
| `zebra_chain::primitives::redpallas::batch` | **new**, `pub mod batch;` inside the existing `pub mod redpallas` | `Item`, `Verifier` (§5) |
| `zebra_chain::transaction::Transaction` | `pub enum Transaction` | six header/pool accessors (§4) |
| `zebra_consensus::error` | `pub mod error;` | three new `TransactionError` variants (§7) |
| `zebra_consensus` (internal) | `mod primitives;`, `mod transaction;` — both private | the async RedPallas verifier service and the ZIP-211 / v5-before-NU5 checks (§8: not directly checked) |
| `zebra_state` (internal) | crate-internal update paths | return `Result` instead of asserting (§8: not in scope) |

Internal file layout is yours; only the names below, at the paths given, are
in scope.

## 3. Chain history — group `chain-history-commitment` (`zebra-chain`)

### 3.1 The MMR primitive

`zebra_chain::primitives::zcash_history` must exist as a public module
wrapping `zcash_history::Tree` (from the `librustzcash` crate this milestone
adds to `zebra-chain/Cargo.toml` — §1 gives the line). Per ZIP-221 it holds
**one network upgrade's**
history: appending a block from a different upgrade than the tree was built
for is a programming error, not a recoverable one.

Required surface, at minimum:

- `pub struct Tree` — an MMR over one network upgrade's blocks, built from a
  single block or from a cache of peak/extra nodes plus a length.
- `pub struct NodeData` — one encoded ZIP-221 node, at most
  `zcash_history::MAX_NODE_DATA_SIZE` bytes.
- `pub struct Entry` — an encoded node plus its position links, at most
  `zcash_history::MAX_ENTRY_SIZE` bytes; convertible from
  `zcash_history::Entry<zcash_history::V1>`.
- Building a leaf from `(Arc<Block>, Network, &sapling::tree::Root)`,
  appending and truncating leaves, and reading the tree's root.

A leaf's node data carries the ZIP-221 fields, all derived from the block:
`consensus_branch_id` (from `ConsensusBranchId::current(network, height)`),
`subtree_commitment` = the block hash, `start_time`/`end_time` = the header
timestamp as a `u32`, `start_target`/`end_target` = the header's
`difficulty_threshold`, `start_sapling_root`/`end_sapling_root` = the 32-byte
Sapling note commitment tree root passed in,
`subtree_total_work` = the work implied by the difficulty threshold,
`start_height`/`end_height` = the coinbase height, and `sapling_tx` = the
count of transactions in the block for which
`Transaction::has_sapling_shielded_data()` is true (§4) — ZIP-221's "either
of `vSpendsSapling` or `vOutputsSapling` is non-empty".

**Only the root leaves this module in an in-scope form.** The tree's own
mutators have no public entry point an external test can reach: constructing
a leaf from a block, appending and truncating are crate-internal here, and
`Tree`/`NodeData`/`Entry` are exposed as types, not as callable operations.
So **nothing in this step checks building the MMR** — a milestone that
returned the right root type and never appended a leaf passes this step just
the same — and what the driver checks is the root *type* and its
relationship to a header's commitment field, below. §8 states this as a
boundary too, so instruction, contract and verifier agree.

### 3.2 The root type and the header commitment

- `zebra_chain::block::ChainHistoryMmrRootHash` — the MMR root,
  `Clone + Copy + Debug + Eq + PartialEq`. It must be reachable at exactly
  that path: `pub use commitment::{ChainHistoryMmrRootHash, Commitment,
  CommitmentError};` in `block.rs`. Before this milestone only `Commitment`
  and `CommitmentError` were re-exported.
- `impl From<[u8; 32]> for ChainHistoryMmrRootHash` — infallible, a faithful
  byte copy in the order the bytes are given, with no reversal. This is new;
  without it the root could only ever be produced by the crate's own parser,
  and a caller could assert the commitment's *variant* but never its 32
  bytes. Two `ChainHistoryMmrRootHash`es built from different byte strings
  must compare unequal, and one built from a header's own commitment bytes
  must compare equal to what that header parses to.
- `Block::commitment(&self, network: Network) -> Result<Commitment, CommitmentError>`
  — unchanged from the predecessor milestone, and `Commitment`'s variants are
  unchanged:
  - `PreSaplingReserved([u8; 32])` — before Sapling activation.
  - `FinalSaplingRoot(sapling::tree::Root)` — Sapling and Blossom.
  - `ChainHistoryActivationReserved` — the Heartwood **activation block**
    itself. Per ZIP-221 that header's 32 bytes MUST be all zeroes and MUST
    NOT be read as a root; this variant carries no payload, so
    `Commitment::ChainHistoryRoot(ChainHistoryMmrRootHash::from([0u8; 32]))`
    must **not** equal it.
  - `ChainHistoryRoot(ChainHistoryMmrRootHash)` — from Heartwood activation
    + 1 block through Canopy.
  - `ChainHistoryBlockTxAuthCommitment(..)` — NU5 onward; not exercised here.

  `Commitment` must be `PartialEq` (it already is) so the whole
  `Commitment::ChainHistoryRoot(...)` value can be compared, not just its
  bytes.

The driver reads `root_bytes_hex` out of `blocks.tsv` independently of the
block bytes, hands those 32 bytes to `From`, and compares the result to what
`Block::commitment` returns for three corpus blocks: the block after mainnet
Heartwood activation, a later Heartwood-era mainnet block, and testnet's
Canopy activation block. It also checks that the mainnet Heartwood
activation block's all-zero reserved field is not read as a root.

## 4. Transaction header and pool accessors — group `tx-header-and-pool-fields` (`zebra-chain`)

Six new methods on `zebra_chain::transaction::Transaction`. None of them
existed on the predecessor milestone; several are what the checks in §6 and
§7 read.

```rust
/// Return if the `fOverwintered` flag of this transaction is set.
pub fn is_overwintered(&self) -> bool;
/// Return the version of this transaction.
pub fn version(&self) -> u32;
/// Get this transaction's network upgrade field, if any.
pub fn network_upgrade(&self) -> Option<NetworkUpgrade>;
/// The `vpub_old` fields from JoinSplits in this transaction, any version.
pub fn sprout_pool_added_values(
    &self,
) -> Box<dyn Iterator<Item = &amount::Amount<amount::NonNegative>> + '_>;
/// Return if the transaction has any Sapling shielded data.
pub fn has_sapling_shielded_data(&self) -> bool;
/// Access the Orchard flags in this transaction, if there are any.
pub fn orchard_flags(&self) -> Option<orchard::shielded_data::Flags>;
```

Exact required behaviour, per version:

| | V1 | V2 | V3 | V4 | V5 |
| --- | --- | --- | --- | --- | --- |
| `version()` | 1 | 2 | 3 | 4 | 5 |
| `is_overwintered()` | `false` | `false` | `true` | `true` | `true` |
| `network_upgrade()` | `None` | `None` | `None` | `None` | `Some(*network_upgrade)` |
| `has_sapling_shielded_data()` | `false` | `false` | `false` | `sapling_shielded_data.is_some()` | `sapling_shielded_data.is_some()` |

- `is_overwintered()` reports the `fOverwintered` **flag**, which is the high
  bit of the 4-byte transaction header word, not a version comparison that
  happens to agree. It is `false` for exactly V1 and V2.
- `version()` returns the version **without** the `fOverwintered` bit —
  `3`, not `0x8000_0003`. `Transaction`'s `ZcashSerialize` impl must now be
  the single place that recombines them: it writes
  `((self.is_overwintered() as u32) << 31) | self.version()` as one
  little-endian `u32` header word, for every version, replacing the five
  per-variant literal writes. The wire bytes are unchanged; the inherited
  block-codec group (§9) re-checks that.
- `network_upgrade()` is `Some` only for V5, where it returns the variant's
  own `network_upgrade` field (derived from `nConsensusBranchId` on the
  wire). For V1–V4 it is `None` even though those versions do have a branch
  id on the wire from Overwinter on — the field this method reports is V5's.
- `sprout_pool_added_values()` yields one `&Amount<NonNegative>` per
  JoinSplit, in JoinSplit order, for V2 and V3 (BCTV14 proofs) and V4
  (Groth16 proofs) when `joinsplit_data` is `Some`, and an **empty iterator**
  — never a panic — for V1, for V5, and whenever `joinsplit_data` is `None`.
  The value yielded is `vpub_old`: the amount moved *into* the Sprout pool
  and out of the transparent pool. Its count must agree with
  `Transaction::joinsplit_count()`.
- `orchard_flags()` returns `orchard_shielded_data().map(|d| d.flags)`. It is
  `Some` exactly when `orchard_shielded_data()` is `Some`, and `None` for
  every transaction version below 5 and for a V5 with an empty Orchard
  bundle.

`amount::Amount<NonNegative>` is `PartialEq` and comparable with
`Amount::try_from(0i64)`; `orchard::shielded_data::Flags` is the bitflags
type from step 4 with `ENABLE_SPENDS = 0b01` and `ENABLE_OUTPUTS = 0b10`.

The driver walks the whole 17-block corpus with these, cross-checking each
accessor against an independent source: `version()` against the transaction's
own wire header and the 1..=4 range the corpus covers,
`has_sapling_shielded_data()` against the existing
`sapling_spends_per_anchor()`/`sapling_outputs()` iterators, and
`sprout_pool_added_values()` against `joinsplit_count()`. The corpus TSV is
read for each block's name and hex bytes, not for a per-transaction version
column.

## 5. Batched RedPallas — group `redpallas-batch` (`zebra-chain`)

`zebra_chain::primitives::redpallas` gains `pub mod batch;`. The existing
`SigningKey`, `VerificationKey`, `VerificationKeyBytes`, `Signature`,
`SigType`, `SpendAuth`, `Binding`, `HStar`, `Randomizer` and `Error` stay
exactly as they are, including `Error`'s three variants
(`MalformedSigningKey`, `MalformedVerificationKey`, `InvalidSignature`).

```rust
pub struct Item { /* private */ }          // Clone + Debug
impl<'msg, M: AsRef<[u8]>>
    From<(VerificationKeyBytes<SpendAuth>, Signature<SpendAuth>, &'msg M)> for Item {}
impl<'msg, M: AsRef<[u8]>>
    From<(VerificationKeyBytes<Binding>, Signature<Binding>, &'msg M)> for Item {}
impl Item {
    pub fn verify_single(self) -> Result<(), Error>;
}

#[derive(Default)]
pub struct Verifier { /* private */ }
impl Verifier {
    pub fn new() -> Verifier;
    pub fn queue<I: Into<Item>>(&mut self, item: I);
    pub fn verify<R: RngCore + CryptoRng>(self, rng: R) -> Result<(), Error>;
}
```

Required behaviour:

- An `Item` is built from a `(key bytes, signature, &message)` triple and
  **does not borrow the message**: the challenge `c = H*(R‖vk‖M)` is computed
  at construction so the `Item` outlives the message buffer. This is what
  lets `zebra-consensus` queue items into an async service.
- One `Verifier` accepts `SpendAuth` and `Binding` items **in the same
  batch**. Each item remembers which role it is and therefore which generator
  its verification equation needs; the caller does not partition the queue.
- `verify` consumes the verifier and returns `Ok(())` if and only if every
  queued signature is valid. It draws per-item blinding coefficients from the
  supplied RNG and settles the whole batch with one multiscalar
  multiplication, so a batch of *n* valid signatures verifies and a batch
  containing even one invalid signature returns
  `Err(Error::InvalidSignature)` — a batch verifier must never report a set
  containing a forgery as valid.
- `Verifier::new().verify(rng)` on an **empty** queue is `Ok(())`.
- `Item::verify_single(self)` performs the same check without batching and
  must agree with `VerificationKey::try_from(vk_bytes)` followed by
  `VerificationKey::verify(msg, sig)` on every input, accept and reject
  alike. It is the fallback path a service uses to find the culprit after a
  batch fails.
- `Signature<T>` keeps its `From<[u8; 64]>` and `Into<[u8; 64]>` round trip,
  and `SigningKey<T>` its `TryFrom<[u8; 32]>`, so a test can corrupt one
  signature's bytes and re-queue it.

The batch equation itself is normative: `nu5.txt` appendix B.1, "RedDSA batch
validation". Scalar-multiplication strategy is yours — upstream adds a
variable-time multiscalar routine for it — as long as the results above hold.

There is no published known-answer vector for RedPallas, so the driver signs
with the crate's own `SigningKey::sign` against a fixed, reproducible RNG and
checks the claims that do not depend on any particular signature being the
right one: a mixed twelve-signature batch verifies, one corrupted signature
spoils an eight-signature batch, `verify_single` agrees with direct
verification for both roles, and an empty batch is trivially valid.

## 6. Transaction/block network upgrade consistency — group `network-upgrade-consistency` (`zebra-chain`)

```rust
impl Block {
    /// Check that every transaction's network upgrade matches the one this
    /// block's height implies.
    pub fn check_transaction_network_upgrade_consistency(
        &self,
        network: Network,
    ) -> Result<(), /* see below */>;
}
```

ZIP-244 §7.1: `nConsensusBranchId` MUST match the consensus branch ID used
for sighash. Required behaviour:

- The block's own upgrade is
  `NetworkUpgrade::current(network, self.coinbase_height().expect(..))`.
- Every transaction whose `network_upgrade()` (§4) is `Some` must report that
  same upgrade. Transactions reporting `None` — every version below 5 — are
  skipped, so a block with no V5 transactions is always `Ok(())` and must not
  manufacture a failure out of an empty search.
- One mismatching transaction anywhere in the list fails the whole block,
  regardless of position or of how many consistent transactions surround it.
- The error is a **unit variant meaning "wrong consensus branch id"**, whose
  `Display`/`.to_string()` text is exactly:

  ```
  transaction has wrong consensus branch id for block network upgrade
  ```

  Upstream puts it in a new `zebra_chain::block::error::BlockError`, declared
  `mod error;` — private. You may keep it private: the driver only uses
  `Result::is_ok`/`is_err` and the `Display` string above, because a type in a
  private module cannot be named from an integration test. If you make the
  module public instead, that is also fine; nothing in scope depends on it. The
  error type must implement `std::fmt::Display` and `std::fmt::Debug`.

`Block`'s fields stay public — `pub header: Header`, `pub transactions:
Vec<Arc<Transaction>>` — and `Transaction::V5`'s fields stay public and named
`network_upgrade`, `lock_time`, `expiry_height`, `inputs`, `outputs`,
`sapling_shielded_data`, `orchard_shielded_data`. The driver appends
hand-built V5 transactions to real parsed corpus blocks, so both must remain
constructible and mutable by an external caller.

## 7. Consensus error surface — group `consensus-error-surface` (`zebra-consensus`)

`zebra_consensus::error::TransactionError` gains three variants. The enum
keeps its derived `Debug` and `PartialEq` and its `thiserror` `Display`.

```rust
#[error("transaction version {0} not supported by the network upgrade {1:?}")]
UnsupportedByNetworkUpgrade(u32, zebra_chain::parameters::NetworkUpgrade),

#[error("Orchard bindingSig MUST represent a valid signature under the transaction binding validating key bvk of SigHash")]
RedPallas(zebra_chain::primitives::redpallas::Error),

#[error("adding to the sprout pool is disabled after Canopy")]
DisabledAddToSproutPool,
```

- `UnsupportedByNetworkUpgrade` is the rule that rejects a transaction
  version the block's network upgrade does not accept — notably **a V5
  transaction before NU5 activates**. Both payload fields are significant:
  `(5, Canopy)`, `(4, Canopy)` and `(5, Nu5)` are three different values, and
  `Display` renders the version with `{0}` and the upgrade with its `Debug`
  name via `{1:?}`, so `(5, Canopy).to_string()` is exactly
  `"transaction version 5 not supported by the network upgrade Canopy"`.
  It is distinct from the existing blanket `WrongVersion`
  (`"transaction version number MUST be >= 4"`), which it refines.
- `RedPallas` is the Orchard binding-signature rule — the consensus-visible
  end of §5 — and is a separate variant from the existing
  `RedJubjub(redjubjub::Error)` Sapling one. In the same change the two older
  signature rules are reworded to name their pool, so their `Display` strings
  become `"Sprout joinSplitSig MUST represent a valid signature under
  joinSplitPubKey of dataToBeSigned"` (`Ed25519`) and `"Sapling bindingSig
  MUST represent a valid signature ..."` (`RedJubjub`). The driver checks
  that the Sapling message starts with `Sapling bindingSig`.
- `DisabledAddToSproutPool` is ZIP-211: from Canopy activation onward,
  `vpub_old` MUST be zero in every JoinSplit of every transaction, which your
  check reads through `Transaction::sprout_pool_added_values()` (§4). It is a
  unit variant.

All three must compose with the existing
`BlockError::Transaction(#[from] TransactionError)` wrapper:
`BlockError::from(v).to_string()` is `"block contains invalid transactions"`
and matching the result back down to `BlockError::Transaction(inner)`
recovers the same value.

## 8. Negative space — what this milestone does not ask for, and what is out of scope

- **The MMR itself is not directly checked.** `zcash_history`'s `Tree`,
  `Entry` and `NodeData` are public types, but upstream keeps every
  constructor and method on them private at this tag, and an integration test
  is a separate crate. Build the module per §3.1 anyway — step 6 promotes it
  to a public `HistoryTree` and checks it, and step 8 splits its empty and
  non-empty cases and persists it — but understand that what is in scope here
  is the root type and the header commitment (§3.2). Do not weaken §3.1 into
  a stub: the next milestone's tests call into it.
- **Validating a commitment against a computed root.** This milestone must
  *produce* a root and *read* a header commitment. The consensus rule that
  the two agree — ZIP-221 FlyClient — is step 8's.
- **The ZIP-211 check function and the v5-before-NU5 check.** Both live in
  `zebra-consensus`'s `mod transaction; mod check;`, neither `pub`, so no
  integration test can call them. Only their error variants' shape and text
  are checked (§7). Implement the checks; the reviewer should not expect a
  driver that exercises them end to end.
- **The asynchronous RedPallas verifier *service*.** `mod primitives;` is
  private in `zebra-consensus/src/lib.rs`, so a `tower::Service` there,
  however correct, is unreachable from a test. Build it — it is goal 4 of
  `instruction.md` and step 6 calls it for real Orchard spend-auth
  signatures — but only the `zebra-chain` batch math underneath it is checked
  (§5).
- **`zebra-state` returning errors from update methods.** An internal
  refactor with no public request or response change, and no requirement group.
  It exists so step 7's contextual validation has an error path.
- **Orchard proof validity, spend-auth verification, ZIP-244 TxId, the V5
  sighash.** Steps 6 and 12.
- **Contextual validation of any kind** — nullifier and UTXO double spends,
  anchors, value-pool bounds, coinbase maturity. Steps 7 and 8.
- Everything already listed under `REQUIREMENTS.md`'s "Explicit out-of-scope
  decisions" (script interpreter internals, proving-system internals, mining,
  wallet, RPC, `lightwalletd`, Tor, NU6) remains out of scope.

## 9. Regression guards — groups `inherited-block-codec` and `inherited-sighash` (GATE — adds no requirement, vetoes the step if it fails; `zebra-chain`)

Trimmed copies of earlier milestones' drivers, run against the unchanged
17-block corpus and the unchanged five ZIP sighash vectors. They exist
because this milestone rewrites the transaction header word (§4) and adds a
public constructor to a commitment type (§3.2), and both sit under every
block and every transaction in the workspace.

- `inherited-block-codec`: every corpus block still round-trips byte for
  byte; `Block::hash()`, `Block::coinbase_height()` and the merkle root
  computed from `Transaction::hash()` still match the corpus; the first
  transaction of every block is still a coinbase; an absurd transaction count
  is an error rather than an allocation; a truncated block is an
  `io`-category `SerializationError`. The header-word refactor in §4 must be
  byte-identical to the five per-version literal writes it replaces — this
  group is what proves it.
- `inherited-sighash`: all five ZIP-143/ZIP-243 vectors still reproduce the
  digest the ZIP prints, via
  `Transaction::sighash(&self, network_upgrade: NetworkUpgrade, hash_type:
  HashType, input: Option<(u32, transparent::Output)>) ->
  blake2b_simd::Hash`, whose 32 bytes — read with `Hash::as_bytes()` — are in
  the ZIP's printed order and are **not** reversed the way `block::Hash` is
  for display. `HashType` must stay re-exported at
  `zebra_chain::transaction::HashType`; the module it is defined in may stay
  private, and upstream's is. The step-4
  `scriptCode` contract is unchanged and still load-bearing:
  `transparent::Script` holds raw, unprefixed bytes behind a private field,
  `Script::new(raw_bytes)` takes them without a prefix, `Script::as_raw_bytes()`
  returns them without one, and `SigHasher` writes the single compactSize
  prefix itself by calling `Script::zcash_serialize`. A caller that
  pre-prefixes gets a different, wrong digest.

## 10. Handoff

**Inherited from steps 1–4**, unchanged in public shape and re-exercised
here: the V1–V5 transaction codec and the block codec, the ZIP-143/ZIP-243
sighash, the Sapling bundle types and the `scriptCode` single-prefix rule.
Also inherited, but **not** re-exercised by any group of this step — they
ride on the `workspace-checks` and `inherited-block-codec` gates compiling,
not on an assertion here: the ZIP-225 Sapling-in-V5 *wire* layout, the
Orchard bundle codec beyond `Flags`, and Orchard nullifiers retrievable from
`zebra-state`.

**What step 5 leaves for step 6**: a `zcash_history` MMR that can be built
from blocks and asked for its root, a `ChainHistoryMmrRootHash` a caller can
construct and compare, a `Transaction` that can report its own version,
`fOverwintered` flag, network upgrade, Sapling presence, Orchard flags and
Sprout-pool contributions, a RedPallas batch verifier with its `Item`/`Verifier`
pair, and a `TransactionError` enum that already names the ZIP-211 and
version/upgrade rules. Step 6 (`REQUIREMENTS.md`: ZIP-244 TxId and V5
sighash, a public `HistoryTree`, Orchard spend-auth verification, ZIP-239
`MSG_WTX`) expects all of the above in place. In particular it promotes §3.1's
module into a public runtime type and queues §5's `Item`s through the
`zebra-consensus` service, so neither should be left as a stub.
