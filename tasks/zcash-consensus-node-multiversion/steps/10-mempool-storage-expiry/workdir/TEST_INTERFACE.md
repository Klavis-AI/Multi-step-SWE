# TEST_INTERFACE — step 10, mempool storage: sizes, fees, expiry and tip changes

This document is the contract the tests for this step are written
against. Every module path, type, method signature, public field, trait impl,
constant, error variant, `Display` layout and byte layout those tests touch is
named here. Where a byte layout, a display convention, a sentinel value, an
error shape or an exact error string is stated, it is normative: the tests
assert it exactly.

The tests are ordinary integration tests placed in `zebra-chain/tests/`,
`zebra-consensus/tests/` and `zebra-state/tests/` and compiled against your
manifests. They see **only the public API** of those three crates, as an
external consumer. Anything you leave `pub(crate)`, `pub(super)`, private, or
behind `#[cfg(test)]` / `#[cfg(feature = "proptest-impl")]` is invisible to them
— §10 says exactly what that excludes, and why some of this milestone's work is
deliberately out of scope.

Conventions used below:

- "must" means a test fails if it is not so.
- **internal byte order** means the bytes as they appear on the wire and in the
  hash function's output. **display order** means those bytes reversed, which is
  the big-endian `u256` convention Bitcoin and `zcashd` print hashes in, and is
  what `Display` already produces for `block::Hash` and `transaction::Hash`.
- "the block corpus" is the 17-block fixture set described in §9.1.
- "mined ID" is `transaction::Hash`. "unmined ID" is `transaction::UnminedTxId`.
- "a `debug_struct` rendering" means the output of
  `fmt::Formatter::debug_struct(name)` with `.field(label, value)` calls in the
  stated order and `.finish()` — i.e. `Name { label: value, label: value }` in
  non-alternate mode. Several `Display` impls below are specified that way
  because that is what they are.

---

## 1. How the verifier reaches your code

The verifier runs ten groups. Eight run **integration test files**, copied
into three crates' `tests/` directories:

```
cargo test --offline --locked --jobs 4 -p zebra-chain     --test <name> -- --test-threads=1
cargo test --offline --locked --jobs 4 -p zebra-consensus --test <name> -- --test-threads=1
cargo test --offline --locked --jobs 4 -p zebra-state     --test <name> -- --test-threads=1
```

(If `--offline --locked` cannot resolve, the verifier retries with `--offline`
alone, then `--locked` alone, then neither, for every cargo invocation — build,
check and test alike.) The other two groups run whole-workspace commands instead
of a test file: `cargo build --offline --locked -p zebra-chain --tests` and
`cargo check --offline --locked --workspace --all-targets --all-features`.

Consequences you must respect:

- Each test file is compiled against that crate's `Cargo.toml` **as you leave
  it**. Do not remove an existing `[dependencies]` or `[dev-dependencies]` entry
  from `zebra-chain`, `zebra-consensus` or `zebra-state`. In particular
  `zebra-chain` and `zebra-state` must keep their `hex` dependency;
  `zebra-consensus` does not have one and is not required to gain one.
- The workspace must keep building **offline**; do not add a new third-party
  dependency to any manifest.
- A group whose driver fails to compile fails for that group.
- Tests run with `--test-threads=1` and must not depend on execution order
  between `#[test]` functions in the same file.
- `--all-targets` builds `zebra-chain/benches/block.rs`, which reaches
  `zebra_chain::block::tests::generate`. That module is gated
  `#[cfg(any(test, feature = "bench"))]`, so the workspace check is run with
  `--all-features`; keep the `bench` feature and that gate intact.
- Two `zebra-state` tests call `zebra_state::init(Config::ephemeral(), ..)`
  outside any async runtime, so `init` must remain callable without a Tokio
  reactor being entered.

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
| `serialized-size` | requirement | `zebra-chain` | §2 |
| `unmined-tx-fee-and-size` | requirement | `zebra-chain` | §3 |
| `expiry-height-zip203` | requirement | `zebra-chain` | §4 |
| `spent-outpoints-and-display` | requirement | `zebra-chain` | §5 |
| `nu5-activation-parameters` | requirement | `zebra-chain` | §6 |
| `verifier-fee-response` | requirement | `zebra-consensus` | §7 |
| `chain-tip-change-actions` | requirement | `zebra-state` | §8 |
| `inherited-block-codec` | gate | `zebra-chain` | §9.2 |
| `workspace-checks` | gate | `@build` | §1 |
| `build` | gate | `@build` | §1 |

## 1.1 Where the implementation goes

| Module path | Declared as | What this milestone adds |
| --- | --- | --- |
| `zebra_chain::serialization` | existing, re-exports `zcash_serialize::*` | `FakeWriter`; `ZcashSerialize::zcash_serialized_size` (§2) |
| `zebra_chain::transaction::unmined` | existing private module, re-exported | `UnminedTx::size`, `VerifiedUnminedTx`, two `Display` impls (§3) |
| `zebra_chain::transaction::Transaction` | existing | `expiry_height`'s zero sentinel, `spent_outpoints`, `Display` (§4, §5) |
| `zebra_chain::transparent::Input` | existing | `outpoint()`, `Display` (§5) |
| `zebra_chain::parameters::network_upgrade` | existing, re-exported with `pub use network_upgrade::*` | the NU5 branch id and the NU5 Testnet activation height (§6) |
| `zebra_consensus::transaction` | already `pub mod transaction;` | `Response` becomes an enum, with accessors and a `From` impl (§7) |
| `zebra_consensus::error::TransactionError` | already `pub mod error;` | `IncorrectFee` (§7.4) |
| `zebra_state` (crate root) | existing `lib.rs` re-exports | `ChainTipChange` and `TipAction` re-exported unconditionally (§8) |
| `zebra_state::service::chain_tip` | existing private-module contents | `TipAction::best_tip_height`, `ChainTipChange::last_tip_change` (§8) |

Internal file layout is yours; only the names below, at the paths given, are
in scope. Where a type is re-exported, the re-export path is the one the tests
use.

---

## 2. Measuring a serialized object — group `serialized-size` (`zebra-chain`)

A mempool prices an entry by its serialized length. Serializing into a `Vec` and
reading `.len()` costs an allocation the size of the object, for a number that
is a few dozen bytes of arithmetic. This milestone adds the cheap path.

### 2.1 `ZcashSerialize::zcash_serialized_size`

`zebra_chain::serialization::ZcashSerialize` gains a **provided** method:

```rust
fn zcash_serialized_size(&self) -> Result<usize, std::io::Error>;
```

It is a default method on the trait, so every existing implementor acquires it
without change. The tests measure four receivers — `Block`,
`Arc<block::Header>`, `Arc<Transaction>` and `Vec<Arc<Transaction>>` — the forms
a parsed `Block` is actually made of. They do not measure a bare `block::Header`
or a bare `Transaction`; because the method is provided on the trait those
acquire it too, and the `Arc` forms serialize through them.

Required behaviour:

- The returned count must equal the number of bytes `zcash_serialize` writes for
  the same value. Nothing else is required of the mechanism; the natural
  implementation writes into §2.2's `FakeWriter` and reads the counter back.
- It must be infallible for anything that serializes: the tests `expect()` the
  `Ok` on every corpus block, header and transaction.

### 2.2 `zebra_chain::serialization::FakeWriter`

```rust
pub struct FakeWriter(pub usize);
```

- The tuple field is **public**: the tests construct `FakeWriter(0)` and
  `FakeWriter(7)` directly and read `writer.0` afterwards.
- It implements `std::io::Write`. `write(buf)` must add `buf.len()` to the
  counter and return `Ok(buf.len())` — it consumes the whole buffer every time,
  never a short write. `flush()` must return `Ok(())`.
- It keeps no bytes.
- Constructing it with a nonzero count must *accumulate* from that count rather
  than reset it: the tests build `FakeWriter(7)`, write five bytes into it, and
  require the final counter to be `12`.
- It must be re-exported at `zebra_chain::serialization::FakeWriter`, not only
  at the inner module path.

### 2.3 The arithmetic the tests pin it against

Expected values come from the corpus, not from the crate:

- `blocks.tsv`'s `bytes` column, and the length of the raw `.hex` fixture, both
  give a block's size independently. Both must equal
  `Block::zcash_serialized_size()`.
- **A block header is exactly 1487 bytes** at every height in the corpus. The
  tests measure it through `Arc<block::Header>`, which is what `block.header`
  is, so the `Arc` impl must report the header's own serialized size.
- The transaction count in a block is a compactSize immediately after the
  header, at offset 1487. With `compactSize(n)` being 1 byte for `n < 0xfd`,
  3 bytes for `n <= 0xffff`, 5 bytes for `n <= 0xffff_ffff` and 9 bytes
  otherwise, the identity

  ```text
  1487 + compactSize(tx_count) + sum(tx.zcash_serialized_size()) == block size
  ```

  must hold for every corpus block. In particular a block with a single
  transaction has `block size - 1487 - 1` bytes of transaction.
- **A `Vec<T>` serializes as a compactSize length prefix followed by the items**,
  so `Vec<T>::zcash_serialized_size()` is `compactSize(len)` plus the sum of the
  items' own sizes. This is the encoding the existing `ZcashSerialize` impl for
  `Vec<T>` already writes, inherited unchanged: this milestone adds the
  *measurement*, not the format, and you write no new vector codec for it.
- Concretely, `Vec<Arc<Transaction>>::zcash_serialized_size()` must equal the
  whole block's size minus 1487, because a block is exactly a header followed by
  that vector.
- Every transaction in the corpus must report a size greater than zero.

---

## 3. Size and fee on the mempool entry — group `unmined-tx-fee-and-size` (`zebra-chain`)

### 3.1 `UnminedTx` gains a size

```rust
pub struct UnminedTx {
    pub id: UnminedTxId,
    pub transaction: Arc<Transaction>,
    pub size: usize,
}
```

`size` is a new **public** field. It must be the transaction's serialized size
in bytes, as §2 defines it, computed when the `UnminedTx` is built — not on
demand. It must be filled in identically by all four conversions:

```rust
impl From<Transaction>       for UnminedTx
impl From<&Transaction>      for UnminedTx
impl From<Arc<Transaction>>  for UnminedTx
impl From<&Arc<Transaction>> for UnminedTx
```

The tests build the same transaction through all four and require the resulting
`UnminedTx` values to be equal, including `id`, `transaction` and `size`. They
also require, for every corpus block, that `sum of UnminedTx::size over the
block's transactions + 1487 + compactSize(tx_count)` equals the block's size —
the same identity as §2.3, reached through `UnminedTx`.

`UnminedTx` keeps `Clone`, `Debug`, `Eq` and `PartialEq`. The `id` of a v1–v4
transaction remains `UnminedTxId::Legacy(transaction.hash())`.

### 3.2 `Display for UnminedTx`

A `debug_struct("UnminedTx")` rendering with these fields, in this order:

| Label | Value |
| --- | --- |
| `transaction` | the `Transaction` (its own `Display`, §5.3) |
| `serialized_size` | `self.size` |

Normative points the tests assert: the rendering **starts with** `UnminedTx {`,
and it **contains** the substring `serialized_size: <n>` where `<n>` is the
decimal `size`. Note the label is `serialized_size`, not `size`.

### 3.3 `zebra_chain::transaction::VerifiedUnminedTx`

```rust
pub struct VerifiedUnminedTx {
    pub transaction: UnminedTx,
    pub miner_fee: Amount<NonNegative>,
}

impl VerifiedUnminedTx {
    pub fn new(transaction: UnminedTx, miner_fee: Amount<NonNegative>) -> Self;
}
```

- Both fields are public; `new` stores them unchanged. In particular
  `verified.transaction.size` must equal the size of the `UnminedTx` passed in.
- It derives or implements `Clone`, `Debug`, `Eq` and `PartialEq`. Two values
  are equal exactly when both the transaction and the fee are equal: changing
  only the fee must make them unequal, and so must changing only the
  transaction.
- It must be re-exported at `zebra_chain::transaction::VerifiedUnminedTx`.
- The fee type is `zebra_chain::amount::Amount<zebra_chain::amount::NonNegative>`,
  which already exists. The tests build fees with `Amount::try_from(i64)` and
  read them back with `i64::from(..)`. A negative value must not convert:
  `Amount::<NonNegative>::try_from(-1)` must be an `Err`. Zero must convert.

### 3.4 `Display for VerifiedUnminedTx`

A `debug_struct("VerifiedUnminedTx")` rendering with these fields, in this
order:

| Label | Value |
| --- | --- |
| `transaction` | the `UnminedTx` (its own `Display`, §3.2) |
| `miner_fee` | the `Amount<NonNegative>` |

The tests assert that the rendering starts with `VerifiedUnminedTx {` and
contains both `transaction: ` and `miner_fee: `. The rendering of the fee value
itself is not asserted.

---

## 4. ZIP-203 expiry — group `expiry-height-zip203` (`zebra-chain`)

### 4.1 The rule

The NU5 specification's transaction encoding tables (§7.1) describe the field
as

```text
nExpiryHeight  uint32  A block height after which the transaction will expire,
                       or 0 to disable expiry.  [ZIP-203]
```

and the matching consensus rule reads: *[Overwinter onward] If a transaction is
not a coinbase transaction and its nExpiryHeight field is nonzero, then it MUST
NOT be mined at a block height greater than its nExpiryHeight.*

So **zero is a sentinel, not a height.** Until now `Transaction::expiry_height`
handed the raw field straight back, and a caller comparing it against the chain
tip would discard every transaction that opted out of expiry.

### 4.2 `Transaction::expiry_height`

```rust
pub fn expiry_height(&self) -> Option<block::Height>;
```

| Transaction version | Result |
| --- | --- |
| V1, V2 | `None` — the versions have no `nExpiryHeight` field at all |
| V3, V4, V5 with `expiry_height == Height(0)` | `None` |
| V3, V4, V5 with `expiry_height == Height(n)`, `n != 0` | `Some(Height(n))` |

`Some(Height(0))` must therefore never be returned, for any input. The accessor
reports the value; it does not validate it. The largest value the consensus
rules permit, `499_999_999`, must be reported as `Some(Height(499_999_999))`
rather than rejected — the bound is enforced elsewhere.

### 4.3 The sentinel lives in the accessor, not in the codec

The zero must still be **written to and read from the wire unchanged**, or the
transaction's ID would change. Both directions are required, on every version
that carries the field:

- A V3/V4/V5 transaction whose `expiry_height` field is `Height(0)` must
  serialize and deserialize back to a transaction that is equal to the original
  and still reports `expiry_height() == None`.
- The same for a nonzero height, which must round-trip and still report
  `Some(..)`.

The two round-trip tests construct **V4** transactions; V3 and V5 reach the
sentinel through the accessor (§4.2) instead. The requirement is the general one
stated above: the codec must not filter the zero at any version.

So do not filter the zero at the codec boundary, and do not change the
`expiry_height` *field*'s type — it remains `block::Height`, a plain `u32`
wrapper, on the V3, V4 and V5 variants, and those variant fields stay public so
a test can construct a transaction directly.

### 4.4 The corpus

Over the 17-block corpus the tests require that no transaction reports
`Some(Height(0))`, that every v1/v2 transaction reports `None`, and — as an
assertion about the fixtures rather than about your code — that at least 20
Overwinter-or-later transactions report no expiry and at least 20 report a real
height. (The shipped corpus has 27 and 31 of the 58 transactions that carry the
field.)

---

## 5. Spent outpoints, and one-line renderings — group `spent-outpoints-and-display` (`zebra-chain`)

### 5.1 `Transaction::spent_outpoints`

```rust
pub fn spent_outpoints(&self) -> impl Iterator<Item = transparent::OutPoint> + '_;
```

Two transactions conflict, for mempool purposes, exactly when they spend the
same transparent outpoint. Required behaviour:

- It yields the `outpoint` of every `transparent::Input::PrevOut` input, **in
  input order**, by value.
- It yields nothing for a `transparent::Input::Coinbase` input: a coinbase input
  creates value rather than spending an outpoint, so it can conflict with
  nothing. In particular every block's coinbase transaction must yield an empty
  iterator.
- For a transaction with no coinbase inputs, the number of items yielded equals
  `self.inputs().len()`.

`transparent::OutPoint` keeps its public fields `hash: transaction::Hash` and
`index: u32`, and keeps `Copy`, `Clone`, `Debug`, `Eq`, `PartialEq` and `Hash`;
the tests compare the yielded outpoints field-by-field against the ones they
destructure out of `Input::PrevOut { outpoint, .. }` themselves.

`transparent::Input::outpoint(&self) -> Option<OutPoint>` is the natural helper
and may be used, but only `spent_outpoints` is asserted.

### 5.2 `Display for transparent::Input`

`transparent::Input` gets a `Display` that summarises the input instead of
dumping its script bytes and coinbase data.

For `Input::PrevOut { outpoint, unlock_script, sequence }`, a
`debug_struct("transparent::Input::PrevOut")` rendering, fields in this order:

| Label | Value |
| --- | --- |
| `unlock_script_len` | `unlock_script.as_raw_bytes().len()` |
| `outpoint` | the `OutPoint` |

For `Input::Coinbase { height, data, sequence }`, a
`debug_struct("transparent::Input::Coinbase")` rendering, fields in this order:

| Label | Value |
| --- | --- |
| `height` | the `block::Height` |
| `data_len` | the number of bytes of coinbase data |

Normative points the tests assert, over every input of every corpus block:

- The `PrevOut` rendering starts with `transparent::Input::PrevOut {`, and
  contains `unlock_script_len: <n>` with `<n>` the decimal length of the raw
  unlock script, and `index: <n>` from the nested outpoint (so the outpoint must
  be rendered with its own `Debug`, which prints `OutPoint { hash: .., index: .. }`).
- The `Coinbase` rendering starts with `transparent::Input::Coinbase {`, and
  contains `height: Height(<n>)` — `block::Height`'s derived `Debug`, not a bare
  decimal — followed later by `data_len: `.
- `transparent::Script::as_raw_bytes(&self) -> &[u8]` must stay public: it is
  how the test computes the expected length.

### 5.3 `Display for Transaction`

A `debug_struct("Transaction")` rendering, with `network_upgrade` present
**only** when `self.network_upgrade()` is `Some` — that is, only for v5:

| Order | Label | Value |
| --- | --- | --- |
| 1 | `version` | `self.version()` (a `u32`: 1, 2, 3, 4 or 5) |
| 2 | `network_upgrade` | the `NetworkUpgrade`, **omitted entirely for v1–v4** |
| 3 | `transparent_inputs` | `self.inputs().len()` |
| 4 | `transparent_outputs` | `self.outputs().len()` |
| 5 | `sprout_joinsplits` | the number of JoinSplits |
| 6 | `sapling_spends` | the number of Sapling spends |
| 7 | `sapling_outputs` | the number of Sapling outputs |
| 8 | `orchard_actions` | the number of Orchard actions |
| 9 | `unmined_id` | `self.unmined_id()` |

Normative points the tests assert:

- The rendering starts with `Transaction {`.
- The eight always-present labels appear in the order given above (the test
  searches for each label after the end of the previous one, so order is
  checked, and so is the exact spelling of each label — `transparent_inputs`,
  not `inputs`).
- `version: <n>`, `transparent_inputs: <n>` and `transparent_outputs: <n>` carry
  the decimal values above.
- No v1–v4 transaction's rendering contains the substring `network_upgrade: `.
- A v5 transaction on the NU5 branch renders `version: 5`, then
  `network_upgrade: Nu5`, then `transparent_inputs: 0` for an empty one. `Nu5`
  is `NetworkUpgrade`'s derived `Debug` spelling of that variant.

`Transaction::version()`, `inputs()`, `outputs()`, `network_upgrade()` and
`unmined_id()` all already exist and keep their current signatures.

---

## 6. The two NU5 parameters this release moves — group `nu5-activation-parameters` (`zebra-chain`)

NU5 is still being specified, and this release carries two parameter changes.
Neither is cosmetic: both change which bytes a node accepts.

### 6.0 Where the shipped specification disagrees, and why

`/app/specs/nu5.txt` is the **final** NU5 protocol specification. This milestone
reproduces a release made while NU5 was still unactivated and still being
specified, and NU5's branch id and activation heights moved more than once
before they were settled. Three values therefore differ, and on these three the
tests follow this document, not the specification:

| Value | `nu5.txt` says | This milestone requires |
| --- | --- | --- |
| NU5 consensus branch id | `0xF919A198` (§4.10) | **`0x37519621`** (§6.1) |
| NU5 Mainnet activation height | `1687104` (§6) | **none** (§6.3) |
| NU5 Testnet activation height | not given | **`1_599_200`** (§6.3) |

Everything else in the specification is normative as written, including the
Overwinter-through-Canopy branch ids in §6.1's table (which match §4.10's
bullets exactly), the v5 header field order in §7.1 that §6.2 relies on, and
the `nExpiryHeight` rules §4 quotes. A previous milestone of this ladder shipped
`0xf919a198`; this one replaces it, and the *only* thing that makes the new
value correct is that this release adopted it.

**The contradiction resolves against a second document already in your
workspace.** `nu5.txt`'s branch-id bullet names its own authority: it requires
`0xF919A198` *"as defined in [ZIP-252]"*. ZIP-252 ships beside it, at
`/app/specs/zips/zip-0252.rst`, drawn from the same upstream commit — and it
does not define that value anywhere. What it *does* record, in the note beneath
its network-upgrade constants, is that NU5 **first activated on Testnet at block
height 1599200, with consensus branch ID 0x37519621**, and that the chain
carrying that first activation was later rolled back. Those are exactly the two
values this document requires, because this milestone reproduces a release built
for that first activation. `nu5.txt` §4.10's bullet is therefore not merely
older than this milestone — it is stale against the ZIP it cites, and the
workspace already carries the numbers you are being asked to implement.

Read the rest of ZIP-252 as carefully as you read `nu5.txt`. Its *headline*
constants describe the second, post-rollback activation — `CONSENSUS_BRANCH_ID`
`0xc2d6d0b4`, Testnet `1842420`, Mainnet `1687104` — and **none of those three
is this milestone's value either.** Only the first-activation note matches.

One navigational warning about the bullet itself: `nu5.txt` §4.10 writes `NU5`
with U+200B zero-width spaces between the letters, so searching the file for
`NU5` will not surface the mandate. Reach it by section number, not by search
string.

### 6.1 The NU5 consensus branch id

The branch-id table must now read:

| Network upgrade | Consensus branch id |
| --- | --- |
| `Genesis` | none |
| `BeforeOverwinter` | none |
| `Overwinter` | `0x5ba81b19` |
| `Sapling` | `0x76b809bb` |
| `Blossom` | `0x2bb40e60` |
| `Heartwood` | `0xf5b9230b` |
| `Canopy` | `0xe9ff75a6` |
| `Nu5` | **`0x37519621`** (was `0xf919a198`) |

Branch ids are the same on Mainnet and Testnet. An upgrade that has activated
never changes its id; NU5 can still change because it has not.

The surface the tests use, all of which already exists:

```rust
pub struct ConsensusBranchId(/* private */);     // Copy, Clone, Debug, Eq, Hash, PartialEq
impl From<ConsensusBranchId> for u32 { .. }      // the only way out of the type

impl NetworkUpgrade {
    pub fn branch_id(&self) -> Option<ConsensusBranchId>;
    pub fn from_branch_id(branch_id: u32) -> Option<NetworkUpgrade>;
}

impl ConsensusBranchId {
    pub fn current(network: Network, height: block::Height) -> Option<ConsensusBranchId>;
}
```

`ConsensusBranchId` has **no public constructor from `u32`**; the tests compare
by converting the other way with `u32::from(..)`. It must stay reachable at
`zebra_chain::parameters::ConsensusBranchId`.

Required behaviour:

- `NetworkUpgrade::Nu5.branch_id()` converts to `0x37519621`.
- Every earlier upgrade in the table above converts to the value shown, and
  `Genesis` and `BeforeOverwinter` return `None`.
- `NetworkUpgrade::from_branch_id(0x37519621) == Some(NetworkUpgrade::Nu5)`.
- `NetworkUpgrade::from_branch_id(0xf919a198) == None` — the superseded id maps
  to nothing. So must `from_branch_id(0)`.
- `ConsensusBranchId::current(Network::Testnet, Height(1_599_200))` converts to
  `0x37519621`.

### 6.2 The branch id on the wire

A v5 transaction carries its branch id in its own header, so the change is a
wire-format gate. The header layout, little-endian throughout:

```text
offset 0..4    header           0x80000000 | 5    (fOverwintered set, version 5)
offset 4..8    nVersionGroupId  0x26A7270A
offset 8..12   nConsensusBranchId
offset 12..16  lock_time
offset 16..20  nExpiryHeight
```

The tests serialize a `Transaction::V5 { network_upgrade: NetworkUpgrade::Nu5, .. }`
with empty inputs, outputs and bundles, and require:

- bytes `0..4` little-endian to be `0x80000005`;
- bytes `4..8` little-endian to be `0x26A7270A`;
- bytes `8..12` little-endian to be `0x37519621`.

They then deserialize those bytes (which must succeed, and must report
`network_upgrade() == Some(NetworkUpgrade::Nu5)`), rewrite **only** bytes
`8..12` to `0xf919a198`, and require deserialization of the rewritten bytes to
return an `Err`. Rejecting an unknown branch id at deserialization is the
existing behaviour; what changes is which id is unknown.

### 6.3 The NU5 Testnet activation height

The Testnet activation list gains a final row:

| Testnet height | Network upgrade |
| --- | --- |
| `1_028_500` | `Canopy` |
| **`1_599_200`** | **`Nu5`** |

Mainnet gains nothing: NU5 still has no Mainnet activation height. Required
behaviour, through the existing accessors:

- `NetworkUpgrade::Nu5.activation_height(Network::Testnet) == Some(Height(1_599_200))`.
- `NetworkUpgrade::is_activation_height(Network::Testnet, Height(1_599_200))` is
  `true`.
- `NetworkUpgrade::current(Network::Testnet, Height(1_599_200)) == NetworkUpgrade::Nu5`,
  and `NetworkUpgrade::current(Network::Testnet, Height(1_599_199)) == NetworkUpgrade::Canopy`.
- `NetworkUpgrade::Nu5.activation_height(Network::Mainnet) == None`.
- `NetworkUpgrade::current(Network::Mainnet, Height(1_046_400)) == NetworkUpgrade::Canopy`
  (`1_046_400` is Mainnet Canopy activation) and
  `NetworkUpgrade::next(Network::Mainnet, Height(1_046_400)) == None`.

### 6.4 The invariants this milestone preserves

§6.1's table changes exactly one row. The rest of it is an invariant that must
survive the change untouched, and tests assert all three of the following
directly:

1. **Every network upgrade before NU5 keeps its published consensus branch id.**
   `Overwinter`, `Sapling`, `Blossom`, `Heartwood` and `Canopy` still convert to
   the values listed in §6.1 — `0x5ba81b19`, `0x76b809bb`, `0x2bb40e60`,
   `0xf5b9230b`, `0xe9ff75a6`. A branch id is fixed once its upgrade activates;
   this milestone changes none of them, and NU5's can still move only because
   NU5 has not activated.
2. **Upgrades that predate branch ids report `None`.** Branch ids were
   introduced by Overwinter, so `NetworkUpgrade::Genesis.branch_id()` and
   `NetworkUpgrade::BeforeOverwinter.branch_id()` are both `None`.
3. **The mapping is injective — a branch id maps back to exactly one upgrade.**
   `from_branch_id` is the inverse of `branch_id` on §6.1's table, so
   `from_branch_id(0xe9ff75a6) == Some(NetworkUpgrade::Canopy)` just as
   `from_branch_id(0x37519621) == Some(NetworkUpgrade::Nu5)`, and any value the
   table does not list — including the superseded `0xf919a198` and `0` — is
   `None`.

None of the three is new work: an implementation that changes only the NU5 row
preserves all of them by not touching anything else. They are stated because
tests name them literally, so renumbering, reordering or dropping a
historical upgrade fails `nu5-activation-parameters` even though this milestone
never asked you to touch one.

---

## 7. The verifier's answer carries a fee — group `verifier-fee-response` (`zebra-consensus`)

### 7.1 `Response` stops being an alias

At the previous milestone the reply was `pub type Response = UnminedTxId`. It
becomes an enum mirroring the request:

```rust
pub enum Response {
    Block {
        tx_id: UnminedTxId,
        miner_fee: Option<Amount<NonNegative>>,
    },
    Mempool {
        transaction: VerifiedUnminedTx,
    },
}
```

at `zebra_consensus::transaction::Response`. Both variants' fields are public;
the tests construct both variants directly and destructure them.

Why the shapes differ:

- A **block** reply carries only the identifier and the fee, because the block
  verifier already holds the transaction. The fee is `Option` because a coinbase
  transaction has none — the remaining value in its transparent value pool is
  destroyed, not paid — and every block has exactly one coinbase transaction.
- A **mempool** reply carries the whole `VerifiedUnminedTx`, because that struct
  is exactly what the mempool stores: transaction, identifier, serialized size
  and fee. A mempool transaction always has a fee, because coinbase
  transactions are rejected from the mempool.

`Response` must derive or implement `Clone`, `Debug`, `Eq` and `PartialEq`. Two
`Mempool` responses are equal exactly when their `VerifiedUnminedTx` values are
(so a different fee makes them unequal, and so does a different transaction); a
`Block` response is never equal to a `Mempool` one; two `Block` responses with
the same id differ when one has a fee and the other does not.

### 7.2 `Response`'s accessors

```rust
impl Response {
    pub fn into_mempool_transaction(self) -> Option<VerifiedUnminedTx>;
    pub fn tx_id(&self) -> UnminedTxId;
    pub fn miner_fee(&self) -> Option<Amount<NonNegative>>;
    pub fn is_mempool(&self) -> bool;
}
```

| Method | `Block { tx_id, miner_fee }` | `Mempool { transaction }` |
| --- | --- | --- |
| `into_mempool_transaction` | `None` | `Some(transaction)` |
| `tx_id` | `tx_id` | `transaction.transaction.id` |
| `miner_fee` | `miner_fee` (passed through, including `None`) | `Some(transaction.miner_fee)` |
| `is_mempool` | `false` | `true` |

`tx_id` is asserted against an independent value: for each corpus block that
holds exactly one transaction, the block's merkle root **is** that transaction's
mined ID in internal byte order, so `response.tx_id().mined_id()` must equal the
root the corpus records. For such a v1–v4 transaction
`response.tx_id().auth_digest()` must be `None`.

### 7.3 `From<VerifiedUnminedTx> for Response`

```rust
impl From<VerifiedUnminedTx> for Response {
    fn from(transaction: VerifiedUnminedTx) -> Self;   // Response::Mempool { transaction }
}
```

It must produce the `Mempool` variant, unchanged.

### 7.4 `TransactionError::IncorrectFee`

`zebra_consensus::error::TransactionError` gains one unit variant:

```rust
#[error("could not calculate the transaction fee")]
IncorrectFee,
```

The `Display` string is normative and asserted character for character:
`could not calculate the transaction fee`. `TransactionError` keeps `Clone`,
`Debug`, `Eq` and `PartialEq`, and keeps every variant it already has — the
tests compare `IncorrectFee` against `CoinbaseInMempool` for inequality, so that
step-9 variant must still exist under that name.

### 7.5 Request side: unchanged

`zebra_consensus::transaction::Request` and its accessors are exactly as step 9
left them, and **nothing in this group asserts them** — the reference release
adds a helper on `Request`, which is permitted but not required: leaving
`Request` alone is safe, and so is extending it.

---

## 8. The chain tip change stream — group `chain-tip-change-actions` (`zebra-state`)

Step 9 gave the node a read-only tip. A mempool also has to react when the tip
*moves*, and the two things it can do about a move are different enough to have
different names.

### 8.1 Crate-root re-exports

`zebra-state`'s `lib.rs` must re-export both types **unconditionally**:

```rust
pub use service::{
    chain_tip::{ChainTipChange, LatestChainTip, TipAction},
    init,
};
```

`zebra_state::ChainTipChange` and `zebra_state::TipAction` must name these types
from outside the crate. At the previous milestone only `LatestChainTip` was
re-exported, so a test naming the other two did not compile.

`zebra_state::Config` is reached from the crate root by the same tests
(`Config::ephemeral()`, §8.4). It is a **pre-existing** export, already in place
from an earlier milestone and listed among the inherited surface in §9.3 — this
milestone neither adds nor changes it — but it must stay reachable at
`zebra_state::Config`.

`ChainTipBlock` and `ChainTipSender` are **exported** only under
`#[cfg(any(test, feature = "proptest-impl"))]` — the cfg sits on the crate-root
re-exports in §10, **not** on the type definitions, which stay unconditional so
that `zebra-state`'s default-feature build still compiles them. Do not apply
the cfg to the definitions.

### 8.2 `TipAction`

```rust
pub enum TipAction {
    Grow  { block: ChainTipBlock },
    Reset { height: block::Height, hash: block::Hash },
}
```

- `Grow` means the tip advanced to a child of the previous block: a mempool
  removes the transactions that one block mined. The genesis block is a `Grow`.
- `Reset` means the tip jumped — a newly created or cloned `ChainTipChange`, a
  rollback to another fork, a network-upgrade activation block, or several
  blocks committed at once. A mempool clears itself. `Reset` deliberately
  carries no transaction hashes, because everything is cleared anyway.
- `Reset`'s two fields are **public** and the tests construct the variant
  directly.
- `TipAction` must derive or implement `Clone`, `Debug`, `PartialEq` and `Eq`.
  Two `Reset`s are equal exactly when both height and hash match.

### 8.3 `TipAction`'s variant-independent accessors

```rust
impl TipAction {
    pub fn is_reset(&self) -> bool;
    pub fn best_tip_hash(&self) -> block::Hash;
    pub fn best_tip_height(&self) -> block::Height;     // new at this milestone
}
```

For `Reset { height, hash }` they return `true`, `hash` and `height`
respectively. The tests build a `Reset` from each corpus block's recorded height
and hash — parsing the hash out of its display-order hex with
`block::Hash::from_str` — and require `best_tip_height()` to equal that height
and `best_tip_hash().to_string()` to equal that display-order string.

### 8.4 `ChainTipChange::last_tip_change`

```rust
impl ChainTipChange {
    pub fn last_tip_change(&mut self) -> Option<TipAction>;
}
```

The non-blocking counterpart to the existing `async fn wait_for_tip_change`.
It returns `Some(action)` if the tip has changed since the last call and `None`
if it has not. Required behaviour the tests can reach:

- `zebra_state::init(Config::ephemeral(), Network::Mainnet)` returns
  `(BoxService<..>, LatestChainTip, ChainTipChange)` — that tuple shape is
  unchanged from step 9, and `init` must remain callable outside an async
  runtime.
- Against a state with no committed blocks, `last_tip_change()` must return
  `None`, and must keep returning `None` however many times it is called (the
  test calls it five times).
- `ChainTipChange` must stay `Clone`. A clone of a quiescent change stream must
  also report `None`.

A test additionally coerces each of the four methods above to a function
pointer with exactly the signatures given, so the names, receivers and return
types are checked, not just the values.

`zebra_state::Config::ephemeral()` already exists and is unchanged.

---

## 9. Fixtures, and the inherited regression guard

### 9.1 The block corpus

`$HBV_FIXTURES` holds `blocks.tsv` and `blocks/<name>.hex`, 17 real blocks:
Mainnet 0, 10, 202, 347499, 347500, 347501, 419199, 419200, 419201, 653600,
903000, 903001, 982681 and Testnet 0, 207500, 280000, 1028500. Nine of them
hold exactly one transaction; the set holds 72 transactions in all.

`blocks.tsv` columns: `name network height bytes block_hash previous_block_hash
merkle_root_hex root_bytes_hex version time difficulty_threshold nonce_hex
tx_count note`. `block_hash` and `previous_block_hash` are **display order**;
`merkle_root_hex`, `root_bytes_hex` and `nonce_hex` are **internal (wire)
order**.

You do not ship these fixtures and you cannot see them; they are listed so you
know what the numeric assertions above are measured against. Several assertions
in §2.3, §3.1, §4.4, §5.1, §5.2 and §7.2 are statements about the *corpus*
rather than about your code (for example "at least 20 transactions report no
expiry", "the corpus spends at least 300 outpoints", "nine blocks hold one
transaction", "at least 8 transactions are checked"). They exist
so a group cannot pass by degenerating into a no-op, and they are marked as such
in the test sources.

The ZIP-244 vector file shipped at earlier steps is **not** used at this step:
its v5 transactions carry the superseded NU5 branch id of §6.1 and no longer
deserialize against a correct implementation of this milestone.

### 9.2 `inherited-block-codec` (GATE — adds no requirement, vetoes the step if it fails; `zebra-chain`)

A regression guard, not new behaviour: a trimmed copy of the step-1 block-codec
driver, run against §9.1's 17-block corpus. It touches nothing introduced after
step 1 and must keep passing unchanged.

Because it is a **gate**, any single one of its assertions fails the whole
step. They are therefore listed one by one,
each under the name of the `#[test]` function that makes it, in file order:

- `every_block_round_trips_byte_for_byte` — every corpus block parses with
  `Block::zcash_deserialize` and re-serializes with `zcash_serialize` to bytes
  identical to the input, at the same length.
- `parsing_consumes_the_whole_block_and_no_more` — parsed with
  `zcash_deserialize_into` from a `std::io::Cursor`, a block leaves the reader
  positioned exactly at the end of its bytes. A block is a header and a
  compactSize-counted vector of transactions and nothing else, so the parser
  must neither stop short nor read past.
- `block_hash_matches_the_corpus` — `block.hash().to_string()` equals the
  corpus's `block_hash`, in display order.
- `coinbase_height_matches_the_corpus` — `Block::coinbase_height()` returns
  `Some(h)` with `h.0` equal to the height the corpus records.
- `the_first_transaction_of_every_block_is_a_coinbase` — the first input of the
  first transaction of every corpus block matches
  `transparent::Input::Coinbase { .. }`, reached through `Transaction::inputs()`.
- `the_merkle_root_is_the_root_of_the_transaction_hashes` — collecting every
  transaction's `hash()` into a `merkle::Root` reproduces both the root the
  corpus records and `block.header.merkle_root`.
- `an_absurd_transaction_count_is_an_error_not_an_allocation` — the first
  corpus block's header bytes followed by a nine-byte compactSize declaring
  2^64 - 1 transactions must make `Block::zcash_deserialize` return `Err`. The
  count is attacker-supplied, so it must never be used as an allocation size.
- `a_truncated_block_is_an_io_error` — every corpus block with its last byte
  removed must fail as `SerializationError::Io(_)` — §9.3's retained variant —
  rather than as another variant and rather than parsing successfully.

The gate asserts nothing else. In particular it does **not** check that a header
is 1487 bytes and does **not** check that the transaction count parses as a
compactSize at offset 1487: both of those are §2.3's, where they are *checked*
rather than veto-carrying, and 1487 appears in this driver only as the offset at
which it splices the over-size count above. The driver parses the corpus's
`previous_block_hash` column but never asserts on it.

### 9.3 Inherited surface the tests also name

These all exist already, from steps 1–9. They are listed because a test
names them literally, so renaming or re-scoping any of them breaks a group even
though this milestone does not change them.

```rust
zebra_chain::amount::{Amount, NonNegative}       // Amount: TryFrom<i64>, Into<i64>, Copy, Eq
zebra_chain::block::{self, Block, Height, merkle}
zebra_chain::parameters::{ConsensusBranchId, Network, NetworkUpgrade}
zebra_chain::serialization::{
    SerializationError, ZcashDeserialize, ZcashDeserializeInto, ZcashSerialize,
};
zebra_chain::transaction::{LockTime, Transaction, UnminedTx, UnminedTxId};
zebra_chain::transparent::{Input, OutPoint, Script};
zebra_state::{init, Config};
zebra_consensus::error::TransactionError;
```

- `Block`'s `header: Arc<block::Header>` and `transactions: Vec<Arc<Transaction>>`
  are public fields; `Block::hash()` and `Block::coinbase_height()` exist.
- `block::Hash` and `transaction::Hash` are `pub struct .. (pub [u8; 32])` —
  the tuple field is public and holds the digest in internal byte order — and
  both implement `Display`/`FromStr` in display order.
- `block::Height` is `pub struct Height(pub u32)` and derives `Debug`, so its
  `Debug` is `Height(<n>)`.
- `merkle::Root` is `FromIterator<transaction::Hash>`, duplicating the last node
  of an odd row.
- `Transaction::has_any_coinbase_inputs(&self) -> bool` is still present and
  still returns `true` for any transaction with at least one `Input::Coinbase`.
  It was added at step 9; it is repeated here because a test in
  `spent-outpoints-and-display` calls it directly to partition the block corpus
  into coinbase and non-coinbase transactions before asserting on
  `spent_outpoints`. Removing or renaming it fails that group even though this
  milestone does not change it.
- `ZcashSerialize::zcash_serialize_to_vec()` and
  `ZcashDeserialize::zcash_deserialize(reader)` keep their signatures;
  `ZcashDeserializeInto` still provides `zcash_deserialize_into`.
- `SerializationError` keeps its `Parse(&'static str)` and `Io(..)` variants.
- The `Transaction` variants' fields are public and are constructed literally by
  the tests, so these field names are checked:
  - `V1 { inputs, outputs, lock_time }`
  - `V2 { inputs, outputs, lock_time, joinsplit_data }`
  - `V3 { inputs, outputs, lock_time, expiry_height, joinsplit_data }`
  - `V4 { inputs, outputs, lock_time, expiry_height, joinsplit_data, sapling_shielded_data }`
  - `V5 { network_upgrade, lock_time, expiry_height, inputs, outputs, sapling_shielded_data, orchard_shielded_data }`
- `LockTime::min_lock_time()` exists and returns `Time(MIN_TIMESTAMP)` — `LockTime::Time(500_000_000)`, which is 1985-11-05. (The unlocked sentinel `Height(Height(0))` is returned by `LockTime::unlocked()`.)
- `UnminedTxId` keeps `Legacy(transaction::Hash)` and `Witnessed(WtxId)`, the
  `mined_id()` and `auth_digest()` accessors (the latter `None` for `Legacy`),
  and `From<&Transaction>`.
- `TransactionError::CoinbaseInMempool` still exists under that name.
- `zebra_state::init(Config, Network) -> (BoxService<..>, LatestChainTip, ChainTipChange)`
  and `Config::ephemeral()` are unchanged.

---

---

## 10. Negative space — what this milestone does **not** ask for, and what is out of scope

1. **The mempool component itself is not checked.** Storage, the expiry sweep,
   rejection lists, the crawler, the download-and-verify pipeline and the
   inbound-service wiring live in `zebrad`, which no group in this step checks —
   every driver is injected into `zebra-chain`, `zebra-consensus` or
   `zebra-state`, never into `zebrad`. Build them as you like.
   What is checked is the `zebra-chain`, `zebra-consensus` and `zebra-state`
   surface they are written against.
2. **`TipAction::Grow` is not checked.** Its payload type `ChainTipBlock`, and
   the `ChainTipSender` that would publish one, are exported only under
   `#[cfg(any(test, feature = "proptest-impl"))]`, and `zebra-state`'s own
   dev-dependencies do not enable that feature for itself — so an integration
   test cannot name the type or drive a tip forward. Implement `Grow` (§8.2
   describes it, and a mempool needs it); no test constructs or inspects it.
   The `Reset` variant and the variant-independent accessors are checked.
3. **`wait_for_tip_change` is not checked.** It is `async` and only completes
   once a block is committed, which a test cannot arrange. Only
   `last_tip_change` is asserted.
4. **Running the transaction verifier is not checked.** `Verifier::new` takes a
   `script::Verifier` and `zebra_consensus::script` is private, so the service
   is unconstructible from an integration test. §7 checks the response type, its
   accessors, its `From` impl and the error variant — the values a verifier
   would produce, not the production of them.
5. **Where the miner fee comes from is not checked.** The value-balance
   arithmetic that computes a fee, and the conditions under which
   `TransactionError::IncorrectFee` is raised, run inside that unconstructible
   service. §7.4 checks only the variant's existence, its `Display` string and
   its equality behaviour.
6. **ZIP-401 cost, eviction and random-weighted dropping are out of scope.**
   This step provides the size (§2, §3.1) and the fee (§3.3, §7); the policy
   that spends them is step 11.
7. **`Debug` renderings are not checked, except where a `Display` impl embeds
   one.** Three places in §5.2 and §5.3 *do* depend on a derived `Debug`
   because the `Display` impls delegate to it: `height: Height(<n>)` from
   `block::Height`, `index: <n>` from `OutPoint`, and `network_upgrade: Nu5`
   from `NetworkUpgrade`. Those three spellings are checked. No test asserts a
   whole `Debug` string of any type.
8. **`WtxId`, `AuthDigest` and the `ChainTip` trait are not re-checked.** They
   are step 9's groups and are unchanged here. `UnminedTxId` is not re-checked
   *as a group* either, but some assertions do reach it and are listed above:
   §3.1's requirement that a v1–v4 `UnminedTx::id` is
   `UnminedTxId::Legacy(transaction.hash())`, and §7.2's use of `mined_id()` and
   `auth_digest()` to read a response's identifier.
9. **The mutable accessors are not checked.** `UnminedTxId::mined_id_mut`,
   `UnminedTxId::auth_digest_mut` and `Transaction::expiry_height_mut` are
   `#[cfg(any(test, feature = "proptest-impl"))]` upstream and invisible to an
   integration test. Add them or not.
10. **Halo2/RedPallas verification, the script interpreter, mining, wallet, RPC,
    `lightwalletd`, Tor and NU6** remain out of scope, per `REQUIREMENTS.md`.

---

## 11. Handoff

**This step inherits**, and must not break: the V1–V5 transaction codec and the
1487-byte header (step 1), ZIP-215 batched verification (step 2), the v5
Sapling/Orchard types (steps 3–4), the ZIP-221 history tree (step 5), the
ZIP-244 txid and `auth_digest` (step 6), contextual validation (step 7), ZIP-209
value pools (step 8), and step 9's `UnminedTxId`/`UnminedTx`, `ChainTip` trait,
renamed coinbase predicates and tip-dependent verifier request.

**Step 11 expects to find**, at the paths above:

- `zcash_serialized_size()` on every serializable type, and `UnminedTx::size`
  already populated — ZIP-401 computes a transaction's mempool *cost* from its
  serialized size, and recomputing it per eviction pass is not viable.
- `VerifiedUnminedTx` carrying a `miner_fee`, since ZIP-401's eviction weight is
  a function of the fee.
- `Transaction::spent_outpoints()`, for the conflicting-spend index.
- `Transaction::expiry_height()` returning `None` for the zero sentinel, for the
  height-triggered expiry sweep.
- `zebra_state::{ChainTipChange, TipAction}` at the crate root, with
  `last_tip_change()` — step 11's eviction and expiry both run off a tip change.
- The NU5 branch id `0x37519621` and the NU5 Testnet activation height
  `1_599_200`; step 11 does not move them again.
