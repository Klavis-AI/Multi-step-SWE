# TEST_INTERFACE — step 9, mempool admission

This document is the contract the tests for this step are written
against. Every module path, type, method signature, public field, trait impl,
constant, error variant and byte layout those tests touch is named here. Where a
byte layout, a display convention, an error shape or an exact error string is
stated, it is normative: the tests assert it exactly.

The tests are ordinary integration tests placed in `zebra-chain/tests/`,
`zebra-network/tests/` and `zebra-consensus/tests/` and compiled against your
manifests. They see **only the public API** of those three crates, as an
external consumer. Anything you leave `pub(crate)`, `pub(super)`, private, or
behind `#[cfg(test)]` / `#[cfg(feature = "proptest-impl")]` is invisible to them
— §9 says exactly what that excludes, and why some of this milestone's work is
deliberately out of scope. "Invisible to a test" is not the same as "unable
to fail the step": the two `@build` groups compile targets where `cfg(test)` is
true, so a `#[cfg]`-gated item can still be a compile obligation. §9's
`Arbitrary` bullet is the one place where that distinction matters.

Conventions used below:

- "must" means a test fails if it is not so.
- **internal byte order** means the bytes as they appear on the wire and in the
  hash function's output. **display order** means those bytes reversed, which is
  the big-endian `u256` convention Bitcoin and `zcashd` print hashes in, and is
  what `Display` already produces for `block::Hash` and `transaction::Hash`.
- "the block corpus" is the 17-block fixture set described in §8.1; "the ZIP-244
  vectors" are the 10 v5 transaction vectors described in §8.2.
- "mined ID" is `transaction::Hash`. "unmined ID" is `transaction::UnminedTxId`.

---

## 1. How the verifier reaches your code

The verifier runs nine groups. Seven run **integration test files**, copied
into three crates' `tests/` directories:

```
cargo test --offline --locked --jobs 4 -p zebra-chain     --test <name> -- --test-threads=1
cargo test --offline --locked --jobs 4 -p zebra-network   --test <name> -- --test-threads=1
cargo test --offline --locked --jobs 4 -p zebra-consensus --test <name> -- --test-threads=1
```

(If `--offline --locked` cannot resolve, the verifier retries with `--offline`
alone, then `--locked` alone, then neither, for every cargo invocation — build,
check and test alike.) The other two groups run whole-workspace commands instead
of a test file: `cargo build --offline --locked -p zebra-chain --tests` and
`cargo check --offline --locked --workspace --all-targets --all-features`.

Consequences you must respect:

- Each test file is compiled against that crate's `Cargo.toml` **as you leave
  it**. Do not remove an existing `[dependencies]` or `[dev-dependencies]` entry
  from `zebra-chain`, `zebra-network` or `zebra-consensus`. In particular
  `zebra-chain` must keep its `hex` dependency; `zebra-consensus` does not have
  one and is not required to gain one.
- The workspace must keep building **offline**; do not add a new third-party
  dependency to any manifest.
- A group whose driver fails to compile fails for that group.
- Tests run with `--test-threads=1` and must not depend on execution order
  between `#[test]` functions in the same file.
- `--all-targets` builds `zebra-chain/benches/block.rs`, which reaches
  `zebra_chain::block::tests::generate`. That module is gated
  `#[cfg(any(test, feature = "bench"))]`, so the workspace check is run with
  `--all-features`; keep the `bench` feature and that gate intact.

### How this step is checked

The gate groups are `build`, `workspace-checks` and `inherited-block-codec`.
They add no new requirement and only veto: if any one of them fails, the step
is not satisfied no matter how much of the new work you completed.

The requirement groups carry this step's new capability and its peripheral
breadth; every case in them is in scope.

The practical consequence: preserving the inherited behaviour the gate
checks is necessary but adds nothing by itself, and doing nothing else
leaves all of the new work undone.

The group table, for reference:

| Group | Kind | Crate | Section |
| --- | --- | --- | --- |
| `unmined-transaction-ids` | requirement | `zebra-chain` | §3 |
| `wtxid-wire-format` | requirement | `zebra-chain` | §2 |
| `mempool-verifier-request` | requirement | `zebra-consensus` | §6 |
| `chain-tip-interface` | requirement | `zebra-chain` | §5 |
| `coinbase-input-predicates` | requirement | `zebra-chain` | §4 |
| `network-mempool-requests` | requirement | `zebra-network` | §7 |
| `inherited-block-codec` | gate | `zebra-chain` | §8.3 |
| `workspace-checks` | gate | `@build` | §1 |
| `build` | gate | `@build` | §1 |

## 1.1 Where the implementation goes

| Module path | Declared as | What this milestone adds |
| --- | --- | --- |
| `zebra_chain::transaction::hash` | already `mod hash;`, re-exported | `WtxId`; conversions, `Display`/`FromStr` and wire impls on `Hash` (§2) |
| `zebra_chain::transaction::auth_digest` | already `mod auth_digest;`, re-exported | public byte conversions, `Display`/`Debug`/`FromStr`, wire impls, `Hash` derive (§2.3) |
| `zebra_chain::transaction::unmined` | **new** private module, re-exported | `UnminedTxId`, `UnminedTx` (§3) |
| `zebra_chain::chain_tip` | **new** `pub mod chain_tip;` | `ChainTip` trait, `NoChainTip` (§5) |
| `zebra_chain::parameters::NetworkUpgrade` | existing | `is_activation_height` (§5.3) |
| `zebra_chain::transaction::Transaction` | existing | `unmined_id`, the two renamed coinbase predicates (§3.4, §4) |
| `zebra_chain::serialization::SerializationError` | existing | two new variants (§2.5) |
| `zebra_network` (`protocol::internal`, re-exported at the crate root) | existing | the unconfirmed-transaction `Request`/`Response` variants (§7) |
| `zebra_consensus::transaction` | **`mod transaction;` (private) — this milestone makes it `pub mod transaction;`** | `Request::Mempool`'s new payload, `Request` accessors, `Response` alias (§6), and the module becoming reachable as `zebra_consensus::transaction` |
| `zebra_consensus::error::TransactionError` | already `pub mod error;` | `CoinbaseInMempool` (§6.4) |

Internal file layout is yours; only the names below, at the paths given, are
in scope. Every module the table marks **new** must be reachable at exactly the
path the table gives it from outside the crate (`zebra_chain::transaction`'s
`unmined` module through its re-exports; `zebra_consensus::transaction` by the
`pub mod` line this milestone adds); where a type is re-exported, the re-export
path is the one the tests use.

---

## 2. `zebra_chain::transaction::WtxId` — the ZIP-239 witnessed identifier

Group `wtxid-wire-format` (`zebra-chain`).

ZIP-239 "Definitions": *"The term 'witnessed transaction identifier' ('wtxid'),
defined only for v5 and later transactions, is a 64-byte value given by
concatenating the txid and the authorizing data commitment of the transaction."*
That is the whole layout, and it is asserted directly.

### 2.1 The type

```rust
pub struct WtxId {
    pub id: Hash,                 // transaction::Hash, the mined/txid half
    pub auth_digest: AuthDigest,  // the ZIP-244 authorizing data commitment
}
```

Both fields are **public**. `WtxId` must derive or implement `Copy`, `Clone`,
`Debug`, `Eq`, `PartialEq` and `std::hash::Hash` — the last because the mempool
and the inventory layer key `HashSet`s on it.

**It must also carry the `Arbitrary` derive, and this one is load-bearing:**

```rust
#[cfg_attr(any(test, feature = "proptest-impl"), derive(Arbitrary))]
```

`WtxId`, `UnminedTxId`, `UnminedTx` and `AuthDigest` are the four new types this
milestone adds that **must** carry that attribute, spelled exactly that way (a
hand-written `impl Arbitrary for …` under the same `cfg_attr` is equivalent, but
the attribute is what the reference uses). §9 explains why — it is a compile
obligation, not a behaviour a test checks, and it is the one item in this document
whose omission invalidates every other line of it. Do not skip it because
the property tests that use it are not something you are asked to write.

Required inherent method:

```rust
pub fn as_bytes(&self) -> [u8; 64];
```

### 2.2 Conversions

```rust
impl From<Transaction>       for WtxId  // panics on a pre-v5 transaction
impl From<&Transaction>      for WtxId  // panics on a pre-v5 transaction
impl From<Arc<Transaction>>  for WtxId  // panics on a pre-v5 transaction
impl From<[u8; 64]>          for WtxId
impl From<WtxId>             for [u8; 64]
impl From<&WtxId>            for [u8; 64]
impl TryFrom<&[u8]>          for WtxId  { type Error = SerializationError; }
impl TryFrom<Vec<u8>>        for WtxId  { type Error = SerializationError; }
impl TryFrom<&Vec<u8>>       for WtxId  { type Error = SerializationError; }
```

**Layout, normative.** `as_bytes()` and `From<WtxId> for [u8; 64]` produce
`id` in bytes `0..32` and `auth_digest` in bytes `32..64`, both in **internal
byte order**. `From<[u8; 64]>` is the exact inverse: bytes `0..32` become `id`,
bytes `32..64` become `auth_digest`. The three `TryFrom` impls accept exactly 64
bytes and are equivalent to `From<[u8; 64]>` on that input.

**The wrong-length error, normative.** Any slice or `Vec` whose length is not 64
— the test uses 0, 32, 63 and 65 — must produce
`Err(SerializationError::TryFromSliceError(_))`, not a panic, not a truncation,
and not a different variant. This length arrives from the network, so it is
attacker-controlled.

Computing a `WtxId` from a pre-v5 transaction is a programming error and may
panic; no test does it.

### 2.3 Text form

```rust
impl fmt::Display    for WtxId
impl std::str::FromStr for WtxId { type Err = SerializationError; }
```

`Display` writes `id`'s `Display` output followed immediately by
`auth_digest`'s, with no separator: 128 lowercase hex characters, each half in
**display order** (its 32 bytes reversed). Formally, for every `WtxId w`,
`w.to_string() == format!("{}{}", w.id, w.auth_digest)`.

`FromStr` inverts exactly that. A string whose length is not 128 bytes — the
test uses 0, 64, 127 and 129 — must produce
`Err(SerializationError::Parse(_))`. A 128-character string is split at 64 and
each half is parsed with the corresponding type's own `FromStr`.

`AuthDigest` must therefore itself be public in the following ways, all new at
this milestone:

```rust
impl From<[u8; 32]>     for AuthDigest
impl From<AuthDigest>   for [u8; 32]
impl From<&AuthDigest>  for [u8; 32]
impl From<Transaction>      for AuthDigest   // panics on a pre-v5 transaction
impl From<&Transaction>     for AuthDigest   // panics on a pre-v5 transaction
impl From<Arc<Transaction>> for AuthDigest   // panics on a pre-v5 transaction
impl fmt::Display       for AuthDigest       // 64 hex chars, display order
impl fmt::Debug         for AuthDigest       // `AuthDigest("<display-order hex>")` (the
                                            // shape upstream uses; **no case in scope
                                            // asserts it** — see §9)
impl std::str::FromStr  for AuthDigest  { type Err = SerializationError; }
```

`AuthDigest` must also derive `Copy`, `Clone`, `Eq`, `PartialEq` and
`std::hash::Hash`. Its inner `[u8; 32]` stays private; the `From` impls above are
how a test reads and writes it. A hex string that does not decode into exactly 32
bytes must give `Err(SerializationError::Parse(_))`.

### 2.4 Wire form

```rust
impl ZcashSerialize   for WtxId
impl ZcashDeserialize for WtxId
impl ZcashSerialize   for AuthDigest
impl ZcashDeserialize for AuthDigest
impl ZcashSerialize   for transaction::Hash
impl ZcashDeserialize for transaction::Hash
```

All three serialize as their bare bytes in **internal byte order** with no
length prefix, no type tag and no framing: 64 bytes for `WtxId`, 32 for the other
two. Each `ZcashDeserialize` is the exact inverse of its `ZcashSerialize`, and
`w.zcash_serialize(..)` produces exactly `w.as_bytes()`.

**Two of the three pairs are asserted by the tests** — `WtxId`'s and
`AuthDigest`'s, by value against the ZIP-244 vectors, including that a truncated
buffer is an error rather than a shorter value. `transaction::Hash`'s pair is
asserted once, as the halves of a `WtxId`: the group's wire test serializes a
`WtxId`'s `id` field and reads it back, and checks the 64-byte concatenation it
produces. Implement that pair as stated and you are done with it; there is no
separate `Hash` wire test to satisfy. All six impls must exist regardless — the
crate does not compile without them — so treat this paragraph as telling you
which the checks can see, not which you may skip.

`transaction::Hash`'s tuple field stays public (`pub struct Hash(pub [u8; 32])`),
and it additionally gains `From<[u8; 32]>`, `From<Hash> for [u8; 32]`,
`From<&Hash> for [u8; 32]`, `From<Transaction>` and `From<Arc<Transaction>>`
alongside the `From<&Transaction>` it already has.

### 2.5 The two new `SerializationError` variants

`zebra_chain::serialization::SerializationError` gains exactly:

```rust
/// A string was not UTF-8.
#[error("string was not UTF-8: {0}")]
Utf8Error(#[from] std::str::Utf8Error),

/// A slice was an unexpected length during deserialization.
#[error("slice was the wrong length: {0}")]
TryFromSliceError(#[from] std::array::TryFromSliceError),
```

The `#[from]` conversions are what make `bytes.try_into()` usable with `?` inside
`TryFrom<&[u8]> for WtxId`. The existing variants (`Io`, `Parse`, `Amount`, …)
are unchanged and keep their meanings.

---

## 3. `zebra_chain::transaction::{UnminedTxId, UnminedTx}` — one ID for every version

Group `unmined-transaction-ids` (`zebra-chain`).

### 3.1 `UnminedTxId`

```rust
pub enum UnminedTxId {
    /// Uniquely identifies unmined v1-v4 transactions.
    Legacy(Hash),
    /// Uniquely identifies unmined v5 transactions.
    Witnessed(WtxId),
}
```

Both variants are public and are destructured by the tests. `UnminedTxId`
must derive `Copy`, `Clone`, `Debug`, `Eq`, `PartialEq` and `std::hash::Hash`, and
it must carry the `#[cfg_attr(any(test, feature = "proptest-impl"),
derive(Arbitrary))]` attribute described in §2.1 — as must `UnminedTx` (§3.5).

**The version rule, normative.** Whenever an ID is *derived from a transaction* —
`Transaction::unmined_id`, or any of the `From<…Transaction>` impls in §3.3 — the
version decides and the caller does not: for a `Transaction::V1`, `V2`, `V3` or
`V4` the result is `Legacy(t.hash())`, and for a `Transaction::V5` it is
`Witnessed(WtxId::from(&t))`. No derivation may give a v1–v4 transaction a
`Witnessed` ID or a v5 transaction a `Legacy` one.

This constrains derivation only. `UnminedTxId::from_legacy_id` and
`From<WtxId>` are version-blind constructors that build the variant their name
says, from a bare identifier and with no transaction in hand — that is exactly
their purpose, because the inventory layer receives identifiers before it
receives transactions. Building `from_legacy_id(h)` where `h` happens to be a v5
transaction's txid is legal, is what a peer advertising an `MSG_TX` inventory
entry for a v5 transaction produces, and yields a `Legacy` ID distinct from that
transaction's own `Witnessed` one. §7.1 depends on this.

**Distinctness, normative.** `Legacy(h)` and `Witnessed(w)` are never equal, even
when `w.id == h`. Both are legal keys in the same `HashSet`, and inserting both
yields two entries. This is what lets a mempool hold the same transaction offered
under either identifier without either shadowing the other. Two `Witnessed` IDs
that share an `id` but differ in `auth_digest` are likewise distinct.

### 3.2 Methods

```rust
impl UnminedTxId {
    /// Build a Legacy id. Must only be used for v1-v4 transaction IDs.
    pub fn from_legacy_id(legacy_tx_id: Hash) -> UnminedTxId;

    /// The ID this transaction will have once it is in a block.
    pub fn mined_id(&self) -> Hash;

    /// The authorizing-data digest, if this is a v5 transaction.
    pub fn auth_digest(&self) -> Option<AuthDigest>;
}
```

- `from_legacy_id(h)` returns `Legacy(h)` — this is the constructor the inventory
  layer uses when a peer advertises a bare 32-byte hash and there is no
  authorizing data to go with it.
- `mined_id()` returns the inner `Hash` for `Legacy`, and `wtx_id.id` for
  `Witnessed`. For **every** transaction, mined or unmined, of any version,
  `t.unmined_id().mined_id() == t.hash()`.
- `auth_digest()` returns `None` for `Legacy` and `Some(wtx_id.auth_digest)` for
  `Witnessed`. It is `None` for every v1–v4 transaction, because only v5 commits
  to its authorizing data separately.

### 3.3 Conversions and text form

```rust
impl From<Transaction>      for UnminedTxId
impl From<&Transaction>     for UnminedTxId
impl From<Arc<Transaction>> for UnminedTxId
impl From<WtxId>            for UnminedTxId   // always Witnessed
impl From<&WtxId>           for UnminedTxId   // always Witnessed
impl fmt::Display           for UnminedTxId
```

All five `From` impls must agree with each other and with
`Transaction::unmined_id`. `Display` delegates: a `Legacy` ID prints as its
`Hash` (64 hex characters, display order), a `Witnessed` ID prints as its
`WtxId` (128 hex characters, §2.3).

### 3.4 `Transaction::unmined_id`

```rust
impl Transaction {
    /// Compute the unmined transaction ID of this transaction.
    pub fn unmined_id(&self) -> UnminedTxId;
}
```

`Transaction::hash` keeps its existing meaning and signature (the mined
transaction ID: SHA-256d of the pre-v5 encoding for v1–v4, the ZIP-244 txid for
v5) and is unchanged.

### 3.5 `UnminedTx`

```rust
pub struct UnminedTx {
    /// A unique identifier for this unmined transaction.
    pub id: UnminedTxId,
    /// The unmined transaction itself.
    pub transaction: Arc<Transaction>,
}
```

Both fields public; derives `Clone`, `Debug`, `Eq`, `PartialEq`, plus the
`Arbitrary` attribute §2.1 requires. §9 gives both reasons, and this type is the
one with two: `UnminedTx`'s own derived `Arbitrary` demands the trait from
`UnminedTxId`, so the same-crate property tests in
`zebra-chain/src/transaction/tests/prop.rs` reach it from here; and
`zebra-network`'s internal `Request`/`Response` and external `Message` carry the
attribute in the tree you inherit and spell `UnminedTx` directly, so its
omission there is the one that errors in the other crate first. The ID is
**pre-calculated at construction** — the point of the type is that a transaction
which arrived over the network is never re-identified later.

```rust
impl From<Transaction>       for UnminedTx
impl From<&Transaction>      for UnminedTx
impl From<Arc<Transaction>>  for UnminedTx
impl From<&Arc<Transaction>> for UnminedTx
```

All four produce the same value for the same transaction: `id` is that
transaction's `unmined_id()`, and `transaction` is the transaction unmodified
(`*u.transaction == t`). The four impls differ only in how much cloning they do.

---

## 4. The two coinbase-input predicates

Group `coinbase-input-predicates` (`zebra-chain`).

`Transaction::is_coinbase` and `Transaction::contains_coinbase_input` are
**renamed and given precise, different meanings**. Update every caller in the
workspace; the old names need not be kept.

```rust
impl Transaction {
    /// True if this transaction has valid inputs for a coinbase transaction:
    /// exactly one input, and it is a coinbase input.
    pub fn has_valid_coinbase_transaction_inputs(&self) -> bool;

    /// True if at least one of this transaction's inputs is a coinbase input.
    pub fn has_any_coinbase_inputs(&self) -> bool;
}
```

Writing `n` for the number of entries in `self.inputs()` and `c` for how many of
them are `transparent::Input::Coinbase { .. }`, the contract is exactly:

| | `has_valid_coinbase_transaction_inputs` | `has_any_coinbase_inputs` |
| --- | --- | --- |
| definition | `n == 1 && c == 1` | `c > 0` |
| no inputs (`n == 0`) | `false` | `false` |
| one coinbase input | `true` | `true` |
| one `PrevOut` input | `false` | `false` |
| two coinbase inputs | **`false`** | `true` |
| one coinbase + one `PrevOut`, in either order | **`false`** | `true` |

The two bold cells are the whole reason for the split. The block question —
"is this the block's coinbase transaction?" — is the first column. The mempool
question — "does this transaction touch newly created coins at all?" — is the
second, and a mempool that asked the first question could be slipped a
two-input transaction carrying a coinbase input. The tests construct both
mixed cases by reusing a real coinbase input and a real `PrevOut` input from the
block corpus inside one `Transaction::V1`, and they assert the disagreement.

`Transaction::inputs(&self) -> &[transparent::Input]` and the
`transparent::Input::{PrevOut, Coinbase}` variants are unchanged and public.
`Transaction::V1 { inputs, outputs, lock_time }` keeps its three public fields so
these cases are constructible, as do the inherited pieces they are built from:
`transaction::LockTime::Height(block::Height)`, `transparent::Output { value:
Amount<NonNegative>, lock_script: transparent::Script }`,
`transparent::Script::new(&[u8])` and `transparent::OutPoint { hash, index }`.
None of those change at this milestone.

---

## 5. `zebra_chain::chain_tip` — the tip, without a service

Group `chain-tip-interface` (`zebra-chain`).

### 5.1 The trait

```rust
// zebra_chain::chain_tip
pub trait ChainTip {
    /// Return the height of the best chain tip.
    fn best_tip_height(&self) -> Option<block::Height>;

    /// Return the block hash of the best chain tip.
    fn best_tip_hash(&self) -> Option<block::Hash>;

    /// Return the mined transaction IDs of the transactions in the best chain
    /// tip block.
    fn best_tip_mined_transaction_ids(&self) -> Arc<[transaction::Hash]>;
}
```

Exactly these three methods, with exactly these signatures and return types, at
exactly this path. The trait exists so that `zebra-network` and `zebrad` can be
written against a tip without depending on `zebra-state`, and so that
`zebra-chain` need not depend on `tokio`; so it must be:

- **implementable outside `zebra-chain`** — a test defines its own type
  and writes `impl ChainTip for MyTip`, so no method may be sealed, and none may
  be added without a default body. The declaration carries **no supertraits and
  no bounds beyond the implicit `Sized` and auto traits**: the test's type
  derives nothing, so a `: Debug`, `: Clone`, `: Eq` or similar supertrait makes
  the impl unsatisfiable (E0277) and fails the whole group. Auto traits are
  fine — a `Send + Sync` bound would not break it;
- **usable as a generic bound** (`fn f<T: ChainTip>(t: &T)`), and
- **object safe** — a test builds a `Box<dyn ChainTip>` and calls all
  three methods through it. No generic methods, no `Self: Sized`, no associated
  constants.

`best_tip_mined_transaction_ids` returns the **mined** IDs
(`transaction::Hash`), not unmined IDs. That is deliberate: a transaction in the
tip block must evict the mempool entry for the same transaction even if the
mempool holds it with a different authorizing-data digest, and only the mined ID
is common to both. The returned slice is the tip block's transactions' hashes, in
the block's own order — the order the block's merkle root commits to. The return
type is `Arc<[transaction::Hash]>` (an `Arc`'d slice, not `Arc<Vec<_>>`), so a
caller can hold the list cheaply while the tip moves on.

**How that order is checked.** A test pins both the values and their order
by collecting the returned IDs into `block::merkle::Root` — the inherited
`impl FromIterator<transaction::Hash> for merkle::Root` from step 1 — and
comparing the result against the root the miner put in the block header. That
comparison is only meaningful because of the Bitcoin merkle rule that impl
already implements and that this milestone does not change: take the leaves in
transaction order; then at each level pair adjacent nodes and hash each pair with
SHA-256d, and **if a level holds an odd number of nodes, duplicate the last node
and pair it with itself**; repeat until one node remains. Three blocks in the
corpus have an odd transaction count greater than one, so the duplication rule is
load-bearing here. `merkle::Root` keeps its public tuple field: `root.0` is the
32 raw bytes, in internal byte order.

### 5.2 `NoChainTip`

```rust
pub struct NoChainTip;

impl ChainTip for NoChainTip { /* ... */ }
```

A unit struct deriving `Copy`, `Clone`, `Debug`, `PartialEq` and `Eq`. It is what
a component is handed when there is no state to talk to. Its three methods must
return `None`, `None` and an empty `Arc<[transaction::Hash]>` respectively —
never a panic, never a placeholder height, never an `unimplemented!()`. It is a
value, not a one-shot: repeated calls return the same answers, and because it is
`Copy` it stays usable after being passed by value.

### 5.3 `NetworkUpgrade::is_activation_height`

```rust
impl NetworkUpgrade {
    /// Returns `true` if `height` is the activation height of any network
    /// upgrade on `network`.
    pub fn is_activation_height(network: Network, height: block::Height) -> bool;
}
```

An **associated function**, not a method — it takes no `self`. A tip-change
consumer uses it to recognise the heights at which unconfirmed transactions must
be discarded, without rebuilding the activation table itself.

It must return `true` for a height if and only if some `NetworkUpgrade` variant
has that activation height on that network, i.e. it must agree exactly with the
existing `NetworkUpgrade::activation_height(network) -> Option<block::Height>`
over all eight variants. The tables are the ones already in the crate and are
unchanged by this milestone:

| Upgrade | Mainnet | Testnet |
| --- | --- | --- |
| `Genesis` | 0 | 0 |
| `BeforeOverwinter` | 1 | 1 |
| `Overwinter` | 347_500 | 207_500 |
| `Sapling` | 419_200 | 280_000 |
| `Blossom` | 653_600 | 584_000 |
| `Heartwood` | 903_000 | 903_800 |
| `Canopy` | 1_046_400 | 1_028_500 |
| `Nu5` | — (no activation height) | — (no activation height) |

So the predicate is `true` at exactly those seven heights per network and `false`
everywhere else — in particular `false` one below and one above each of them, and
`false` for a mainnet height on testnet and vice versa.

The test checks both off-by-one neighbours of every row **above height 1**
and cross-checks each network against the other's table. The two rows at height
`0` and `1` are exercised on their positive side only: there is no `Height` below
Genesis for a test to name, so the lower-neighbour half of those two rows is
unrepresentable rather than untested. Every other row is checked in both
directions.

---

## 6. The transaction verifier's two request kinds

Group `mempool-verifier-request` (`zebra-consensus`).

This is where validity becomes tip-dependent. The verifier no longer only answers
"is transaction `i` of block `h` valid"; it also answers "would this transaction
be valid in the block that has not been mined yet". The two share almost every
consensus rule and differ in three specific ways, so they are two variants of one
request type.

### 6.1 `zebra_consensus::transaction::Request`

```rust
pub enum Request {
    /// Verify the supplied transaction as part of a block.
    Block {
        transaction: Arc<Transaction>,
        known_utxos: Arc<HashMap<transparent::OutPoint, transparent::OrderedUtxo>>,
        height: block::Height,
    },
    /// Verify the supplied transaction as part of the mempool.
    Mempool {
        transaction: UnminedTx,
        height: block::Height,
    },
}
```

All fields public and named exactly as above. `Request` must derive `Clone`,
`Debug`, `Eq` and `PartialEq`: the verifier compares requests to deduplicate
in-flight work, so two requests built from the same transaction at the same
height are equal, and a change to *any* field makes an unequal request.

The three differences, normative:

1. **Payload.** A block request carries a bare `Arc<Transaction>`; a mempool
   request carries an `UnminedTx`, so the identifier computed when the
   transaction arrived over the network is carried through rather than recomputed.
2. **Known UTXOs.** A block request carries the outputs created earlier in the
   same block. A mempool transaction has no such context — its inputs must
   already be on chain — so `Mempool` has no `known_utxos` field at all.
3. **Height.** For `Block` it is that block's own height. For `Mempool` it is the
   height of the **next** block: the first block that could possibly contain the
   transaction. In both cases it is what selects the consensus rules.

### 6.2 `Request`'s accessors

```rust
impl Request {
    pub fn transaction(&self) -> Arc<Transaction>;
    pub fn tx_id(&self) -> UnminedTxId;
    pub fn known_utxos(&self) -> Arc<HashMap<transparent::OutPoint, transparent::OrderedUtxo>>;
    pub fn height(&self) -> block::Height;
    pub fn upgrade(&self, network: Network) -> NetworkUpgrade;
    pub fn is_mempool(&self) -> bool;
}
```

- `transaction()` returns the transaction unmodified for both variants — for
  `Mempool` that is the `UnminedTx`'s `transaction` field.
- `tx_id()` returns the `UnminedTx`'s stored `id` for `Mempool`, and
  `transaction.unmined_id()` for `Block`. A mempool request returns the ID it was
  handed rather than recomputing one. Because `UnminedTx`'s only public
  constructors are the four `From` impls of §3.5, which always compute the ID,
  the two are observationally equal — the requirement is about not re-hashing
  a v5 transaction on a hot path, and no test can tell them apart.
- `known_utxos()` returns the stored map for `Block` and an **empty** map for
  `Mempool`. Returning anything non-empty for a mempool request would let an
  unconfirmed transaction spend an output that does not exist yet.
- `height()` returns the variant's `height` field verbatim.
- `upgrade(network)` is `NetworkUpgrade::current(network, self.height())` — the
  upgrade in force *at* that height, for both variants. The test drives it
  across both networks at every activation height, plus the immediate
  predecessors Mainnet carries above height 1.
- `is_mempool()` is `true` for `Mempool` and `false` for `Block`, decided by the
  variant, never by inspecting the payload.

`transparent::OrderedUtxo::new(output, height, from_coinbase, tx_index_in_block)`
and its public `tx_index_in_block` field are unchanged from step 7 and are how
the test builds a block request's `known_utxos`.

### 6.3 `zebra_consensus::transaction::Response`

```rust
/// The response type for the transaction verifier service.
pub type Response = zebra_chain::transaction::UnminedTxId;
```

A **type alias**, not a newtype and not an enum. The verifier used to answer with
`transaction::Hash`; it now answers with the unmined ID, so a caller that
pipelined several transactions can match each reply to its request with no
unwrapping, and a mempool can insert under exactly the key the network layer
used. `<Verifier<ZS> as Service<Request>>::Response` must be this alias.

### 6.4 `TransactionError::CoinbaseInMempool`

`zebra_consensus::error::TransactionError` gains exactly one new unit variant:

```rust
#[error("coinbase inputs MUST NOT exist in mempool")]
CoinbaseInMempool,
```

Its `Display`/`.to_string()` text is exactly the string above. It must be
distinct from every existing coinbase variant — `CoinbasePosition`,
`CoinbaseAfterFirst`, `CoinbaseHasPrevOutInput`, `CoinbaseHasJoinSplit`,
`CoinbaseHasSpend`, `CoinbaseHasOutputPreHeartwood`,
`CoinbaseHasEnableSpendsOrchard` — so a caller can tell "you sent me a coinbase"
apart from "your coinbase is malformed".

`TransactionError` must additionally derive `Clone` at this milestone (it already
derives `Error`, `Debug` and `PartialEq`), because the buffered verifier service
hands the same error to every waiting caller. `VerifyTransactionError` likewise
gains `Clone`, and `SubsidyError` gains `Copy` and `Clone`.

**The rule the variant names**, which you must implement even though it is not
directly reachable from an integration test (§9): when
`Request::is_mempool()` is true and the transaction
`has_any_coinbase_inputs()` — note: the *any* predicate of §4, not the *valid*
one — verification fails with `TransactionError::CoinbaseInMempool`, and this
check runs before the other coinbase rules. The `TransactionError: From<BoxError>`
conversion must also learn to downcast a boxed `TransactionError` back to itself,
so a buffered error survives the round trip instead of becoming
`InternalDowncastError`.

---

## 7. `zebra_network`'s unconfirmed-transaction requests and responses

Group `network-mempool-requests` (`zebra-network`).

`zebra_network::{Request, Response}` (re-exported at the crate root from
`protocol::internal`) are the internal service API the peer set speaks. Their
**unconfirmed** half moves entirely onto the ZIP-239 identifiers; the block half
is untouched, because a block is not malleable the way a v5 transaction is.

### 7.1 `Request` — the variants that change

```rust
pub enum Request {
    // ... unchanged block and peer variants ...

    /// Request transactions by their unmined transaction ID.
    TransactionsById(HashSet<UnminedTxId>),

    /// Advertise a set of unmined transactions to all peers.
    AdvertiseTransactionIds(HashSet<UnminedTxId>),

    /// Push an unmined transaction to a remote peer, without advertising it first.
    PushTransaction(UnminedTx),

    /// Request the unmined transaction IDs in the remote peer's mempool.
    MempoolTransactionIds,
}
```

The four previous spellings — `TransactionsByHash(HashSet<transaction::Hash>)`,
`AdvertiseTransactions(HashSet<transaction::Hash>)`,
`PushTransaction(Arc<Transaction>)` and `MempoolTransactions` — are replaced, not
kept alongside. `MempoolTransactionIds` is a **unit variant**: the peer is asking
for everything we hold, so there is no payload.

`TransactionsById` and `AdvertiseTransactionIds` take a `HashSet`, so the same
identifier offered by two peers is one download; and because `UnminedTxId`
distinguishes a legacy ID, a witnessed ID and a differently-witnessed ID over the
same mined ID (§3.1), a set of those three has three entries, not one. That is
precisely why the network layer stopped using mined hashes here.

### 7.2 `Response` — the variants that change

```rust
pub enum Response {
    // ... unchanged Nil / block / peer variants ...

    /// A list of unmined transactions.
    Transactions(Vec<UnminedTx>),

    /// A list of unmined transaction IDs.
    TransactionIds(Vec<UnminedTxId>),
}
```

`Transactions` previously held `Vec<Arc<Transaction>>` and `TransactionHashes`
previously held `Vec<transaction::Hash>`. Both are `Vec`s, not sets: a reply
preserves the order the answering node chose.

`MempoolTransactionIds` is answered with `Response::TransactionIds`;
`TransactionsById` is answered with `Response::Transactions`;
`AdvertiseTransactionIds` and `PushTransaction` are fire-and-forget and are
answered with `Response::Nil`. When Zebra has no useful data it sends no response
at all rather than a `notfound`.

**Composition, normative and checked.** A `Response::TransactionIds` must be
usable, unmodified, to build a `Request::TransactionsById` — that round trip is
the mempool crawler's entire loop, and it only type-checks if both sides use the
same identifier type.

### 7.3 What does not change

`Request::{BlocksByHash(HashSet<block::Hash>), AdvertiseBlock(block::Hash),
FindBlocks{..}, FindHeaders{..}, Peers}` and
`Response::{Nil, Blocks(Vec<Arc<Block>>), BlockHashes(..), BlockHeaders(..),
Peers(..)}` keep their existing shapes. The test asserts this explicitly,
as a guard against an over-broad rename.

Both enums must keep deriving `Clone` and `Debug` — the peer set clones a request
as it fans it out to every peer, and both are logged on the error path. Neither
needs `PartialEq`: the reference does not derive it here, so the tests
match on the variant and inspect the payload rather than comparing whole values.
Deriving `PartialEq` yourself is harmless and out of scope.

Both enums also carry an inherited `#[cfg_attr(any(test, feature = "proptest-impl"),
derive(Arbitrary))]` attribute, and the external `Message` enum carries it too.
Leave all three in place: they are outside the scope of this milestone, and
removing them would be an unrelated change to inherited public surface. They are
also not a lever: these enums are what make `UnminedTx`'s own attribute
load-bearing in `zebra-network` — the `Tx(UnminedTx)` arm of `Message` and the
`TransactionsById` / `PushTransaction` / `AdvertiseTransactionIds` /
`Transactions` / `TransactionIds` payloads spell the new types directly — but
nothing in the crate ever consumes `Arbitrary for Request` or
`Arbitrary for Response`, so deleting those two derives would change the build
not at all. §9 has the full account.

---

## 8. Fixtures, and the inherited regression guard

### 8.1 The block corpus

`fixtures/blocks.tsv` plus `fixtures/blocks/<name>.hex`: 17 real blocks,
unchanged since step 1 — mainnet 0, 10, 202, 347_499, 347_500, 347_501, 419_199,
419_200, 419_201, 653_600, 903_000, 903_001, 982_681 and testnet 0, 207_500,
280_000, 1_028_500, carrying 72 transactions in total. The TSV columns
(`block_hash` and `previous_block_hash` in display order; `merkle_root_hex`,
`root_bytes_hex` and `nonce_hex` in internal wire order; the rest decimal) were
derived from the block bytes in Python, not from any Rust implementation of this
interface. Every transaction in it is v1–v4, so the corpus exercises the legacy
identifier path and never the witnessed one.

### 8.2 The ZIP-244 vectors

`fixtures/zip244.tsv`: the 10 published ZIP-244 v5 transaction test vectors, with
`tx_hex`, `txid_hex` and `auth_digest_hex` — the latter two in **internal byte
order**, lifted from the ZIP's own test vector file. These are the expected values
for every `WtxId`, `AuthDigest` and `Witnessed` assertion in §2, §3, §6 and §7;
nothing in the tests recomputes a digest. The `AuthDigest` value itself
is the one the inherited tree already computes — step 08's milestone authored
it, and that step's contract states the digest's composition, including how a
variable-length component like `proofsOrchard` is encoded into it. Five of the
ten rows carry Orchard actions, and those are the rows a digest bug in that one
component shows up on.

### 8.3 `inherited-block-codec` (GATE — adds no requirement, vetoes the step if it fails; `zebra-chain`)

A trimmed copy of the step-1 block-codec driver, run against the corpus: every
block round-trips byte-for-byte; parsing consumes the whole block and no more;
`Block::hash()`, `Block::coinbase_height()` and the merkle root recomputed from
`Transaction::hash()` match the corpus; the first transaction of every block has
a coinbase first input; an absurd transaction count is a `SerializationError`
rather than an allocation; and a block one byte short is
`SerializationError::Io(_)`.

Every symbol it touches has existed since step 1. It is here because this
milestone changes `Transaction`'s method set and `transaction::Hash`'s impls,
both of which every block in the corpus depends on to parse and serialize at all.
A tree that stopped at the previous milestone still passes this group; that is
what makes it a regression guard rather than a second `new` group.

---

## 9. Negative space — what this milestone does **not** ask for, and what is out of scope

This section is exhaustive about the gap between "implement this milestone" and
"a test can see it". Where something is listed here as out of scope, no test
asserts it; where a test *does* assert an adjacent thing, this section says
which.

- **The mempool component itself is out of scope.** The crawler (enabled only when
  the node is at the chain tip), the download-and-verify pipeline for gossiped
  transactions, the storage, the tip-change subscription, and the
  inbound-service handling of `mempool` / `getdata` messages all live in
  `zebrad`, and no test reaches `zebrad` (every driver is injected into
  `zebra-chain`, `zebra-consensus` or `zebra-network`), so there is no `tests/`
  directory an integration test can be injected into. Build them as you see fit.
  What is checked is the `zebra-chain`, `zebra-network` and `zebra-consensus`
  surface they are written against, which is §§2–7.
- **The concrete chain-tip types are out of scope.** `zebra_state`'s
  `LatestChainTip` is re-exported but has no public constructor, and
  `ChainTipSender`, `ChainTipChange`, `ChainTipBlock` and `TipAction` live inside
  `zebra_state::service`, which is a private module. §5 therefore checks the
  `ChainTip` trait, `NoChainTip`, and a test-defined implementation of the trait
  — not any type `zebra-state` publishes. Nothing in §5 asserts anything about
  `zebra-state`.
- **Running the transaction verifier is out of scope.**
  `zebra_consensus::transaction::Verifier::new(network, script_verifier)` needs a
  `zebra_consensus::script::Verifier`, and `script` is a private module, so the
  service cannot be constructed from an integration test. §6 checks the request
  type, its accessors, the response alias and the error variant — the surface
  every caller of the verifier is written against. No test awaits a
  verification, and none observes `CoinbaseInMempool` being *raised* by a running
  verifier. What §6.4 checks is the variant's identity, its exact `Display`
  string, its inequality with the sibling coinbase variants, and
  `TransactionError: Clone`. A test does additionally check that the
  triggering condition is observable from outside — for a `Request::Mempool` built
  around a corpus coinbase, `req.is_mempool()` and
  `req.transaction().has_any_coinbase_inputs()` must both be true — but it never
  calls the verifier. The rule itself is still required, and the next milestone
  depends on it.
- **The wire-level inventory encoding is out of scope.** `InventoryHash` (including
  the `Wtx(WtxId)` variant, the `MSG_WTX` code `5`, `from_legacy_tx_id` and
  `unmined_tx_id`), `Message::Tx(UnminedTx)` and the external codec live in
  `zebra_network::protocol::external`, which is private. §7 checks only the
  internal `Request`/`Response` enums, which are re-exported at the crate root.
  Implement the 68-byte `MSG_WTX` inventory entry (4-byte LE code + 64-byte
  wtxid) anyway — ZIP-239 requires it and the crate's own tests cover it.
- **Mempool policy is the next two milestones.** Storage keyed by both
  identifiers, rejection lists split by match type, fees from the verifier,
  expiry heights, removal on block commit, clearing at a network upgrade,
  conflicting spends, ZIP-401 weighted random eviction and size limits are all
  out of scope here (`REQUIREMENTS.md` steps 10–11). This step provides the
  identifiers, the tip interface and the admission request; it does not decide
  what the mempool keeps.
- **Peer-protocol version negotiation for `MSG_WTX`** (ZIP-239's `Deployment`
  section: only send `MSG_WTX` to peers that negotiated protocol version 170014
  or later) is not checked; the constants and the handshake are in private
  modules.
- **`Debug` output wording is out of scope everywhere except one place.** No
  test asserts the `Debug` string of `WtxId`, `UnminedTxId`, `UnminedTx`,
  `Request` or `Response`; they only require that `Debug` is implemented, because
  the error paths format them. The single exception is `AuthDigest`, whose `Debug`
  form is stated in §2.3 — and even there, the tests assert only its
  `Display` form and its `FromStr` round trip, not the `Debug` string. If you
  implement `AuthDigest: Debug` as the derive would, you produce
  `AuthDigest([0, 1, ...])` with internal-order bytes, which is legal for the
  tests and merely unhelpful; §2.3 states the hand-written form because it is
  what the reference does.
- **Proptest `Arbitrary` impls: the `Strategy` is out of scope, the derive is not.**
  Read this bullet as two separate statements, because it is the one place in
  this document where being wrong fails the whole step rather than a single
  group.
  - **Out of scope: how you shape a `StateMachine`/`Strategy`.** No test ever
    enables `proptest-impl`, and none names a proptest type. If you write a
    `Transaction::partial_chain_strategy`-style helper or a bespoke
    `StateMachine`, its signature and its shrink quality are yours.
  - **Load-bearing: the four new types carry the derive, or the workspace does
    not compile.** `UnminedTxId` (§3.1), `UnminedTx` (§3.5), `WtxId` (§2.1) and
    `AuthDigest` (§2.3) **must** carry

    ```rust
    #[cfg_attr(any(test, feature = "proptest-impl"), derive(Arbitrary))]
    ```

    There are two paths that demand it, and they reach different crates.

    **The `zebra-chain` path is a property test this milestone adds.**
    `zebra-chain/src/transaction/tests/prop.rs` is a file of `#[test]`s inside
    the crate's own `cfg(test)` target, and this milestone rewrites it: the
    round-trip tests for the two text forms carry the parameters
    `any::<AuthDigest>()` and `any::<WtxId>()`. Those two parameters are what
    demand `Arbitrary` from `AuthDigest` and from `WtxId`; `UnminedTxId` is
    demanded in turn by `WtxId`'s and `Hash`'s presence in *its* own variants
    (a derived `Arbitrary` demands the trait of every field of every variant,
    so `UnminedTxId` demands it of `WtxId` and of `Hash`, not the other way
    round).
    Omitting any of those three turns that parameter into an `E0277` in
    `zebra-chain`'s own test target, which the gate compiles. Nothing else in
    the crate demands the trait from the new types: `UnminedTx` in particular
    appears in no `zebra-chain` test, bench or property test, and the
    `transparent::OutPoint` derive you inherit is a false friend — it is on an
    inherited type, its only field the trait is not already satisfied for is a
    `transaction::Hash` that step 08 already made `Arbitrary`, and nothing
    downstream of it names any type this milestone introduces. It has to keep
    its attribute; it demands nothing new from you.

    **The `zebra-network` path is the enums you inherit, not one you write.**
    `Request`, `Response` and the external `Message` enum already carry the same
    attribute in the tree you inherit, and this milestone threads `UnminedTx`
    and `UnminedTxId` through their payloads — the `Tx(UnminedTx)` arm of
    `Message`, and the `TransactionsById`, `PushTransaction` and
    `AdvertiseTransactionIds` / `Transactions` / `TransactionIds` payloads of
    the internal pair. Those derives are what make `UnminedTx`'s attribute
    load-bearing **in a second crate**: remove it and the errors are all in
    `zebra-network`, naming the internal enums. This path is reached by the
    `workspace-checks` **gate group**:

    ```text
    cargo check --workspace --all-targets --all-features
    ```

    `--all-targets` is what reaches it: it compiles `zebra-network`'s
    **unit-test target**, where `cfg(test)` is true, and that crate declares
    `proptest` and `proptest-derive` in its **own** `[dev-dependencies]`, which
    are active for its own tests. `zebra-network/Cargo.toml` has no `[features]`
    section, so `--all-features` by itself contributes nothing to that crate.
    Nor is feature unification with `zebra-chain` what turns the branch on here:
    the `proptest-impl` that `zebra-network`'s `[dev-dependencies]` ask for is
    real, but redundant for this attribute, which is already live on that
    crate's own dev-dependencies alone. What `--all-features` does do is keep the
    `zebra-chain` side of the same command compiling, by turning on the `bench`
    feature the workspace check needs (§1).

    The obligation is on the four types and is **not** discharged by touching
    the two internal enums. Removing the attribute from `Request` or from
    `Response` is not a contribution at all: nothing in `zebra-network` ever
    consumes `Arbitrary for Request` or `Arbitrary for Response` — no
    `any::<Request>()` and no `any::<Response>()` exists in the crate — so those
    two derives sit unused, and dropping them leaves the gate compiling with a
    single `unused import: proptest_derive::Arbitrary` warning, while the
    obligation itself is untouched because the `UnminedTx` payloads still name
    the type. Do not reach for the enums when the build breaks: an error in
    `zebra-network` naming `UnminedTx`, `UnminedTxId` or `WtxId` is an error
    about the type, and the fix is in `zebra-chain`.

    A candidate who implements §§2–7 perfectly and omits these derives fails
    `workspace-checks`, so the whole step fails with all six requirement groups
    green and passing. Run the command above locally — the
    `instruction.md` "Local checks" list names it verbatim — and treat any
    error naming `Arbitrary` as this bullet, not as a reason to edit the
    gate.
- **`Ord`/`PartialOrd` on the new identifier types is out of scope** and not
  required. The tests order identifiers, where they need to, by converting
  them to bytes themselves.
- Everything already out of scope per `REQUIREMENTS.md`'s "Explicit out-of-scope
  decisions" list — the script interpreter, proof-system and signature-scheme
  internals (Halo2, Groth16, RedPallas, RedJubjub, Ed25519, Equihash), note
  encryption, mining, proving, wallet, key management, the RPC surface,
  `lightwalletd`, Tor, and every NU6-era rule — remains out of scope.

---

## 10. Handoff

**Inherited from steps 1–8, unchanged in shape and re-exercised here**: the V1–V5
transaction codec and the 17-block corpus (§8.3); `transaction::Hash` and the
ZIP-244 `AuthDigest` computation from step 8 (§2 makes them publicly convertible
but does not change what they compute); `transparent::{Input, Output, OutPoint,
Script, OrderedUtxo}` from steps 4 and 7; `NetworkUpgrade::{current,
activation_height}` and the activation tables (§5.3 only adds a predicate over
them); and `zebra_consensus::transaction::Request::Block`, whose `known_utxos`
and `height` fields keep their step-7 meanings.

**What step 9 leaves for step 10** (`REQUIREMENTS.md`: mempool storage keyed by
TxId and WTXID, rejection lists split by match type, fees from the verifier,
expiry, removal on block commit, clearing at a network upgrade, conflicting
spends, `AdvertiseTransactionIds`):

- `UnminedTxId` with both variants and `mined_id()`, which is exactly what a
  storage layer keyed by *both* identifiers needs: the full ID for the primary
  key and the mined ID for eviction when a block arrives.
- `UnminedTx`, so a stored entry already carries its key.
- The `ChainTip` trait and `NoChainTip`, which step 10's expiry and
  network-upgrade-clearing logic subscribes to, together with
  `NetworkUpgrade::is_activation_height` to recognise the clearing point.
- `zebra_consensus::transaction::Request::Mempool` and its `Response` alias, which
  step 10 extends to also return the transaction's fee.
- `zebra_network::Request::AdvertiseTransactionIds`, which step 10 starts
  actually sending.

Step 10 expects all of the above present and unchanged in public shape; it does
not need to re-derive the witnessed identifier or the coinbase predicates.
