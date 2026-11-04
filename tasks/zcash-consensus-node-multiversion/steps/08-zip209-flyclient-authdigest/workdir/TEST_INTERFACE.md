# TEST_INTERFACE — step 8

This document is the contract the tests for this step are written
against. Every type, module path, method signature, trait impl, constant and
error variant those tests touch is named here — including the inherited
elements they merely name, which appear in the sections that declare that
surface unchanged. Where a byte layout, a hash
personalization or an error shape is stated, it is normative: the tests assert
it exactly.

The tests are ordinary integration tests placed in `zebra-chain/tests/` and
`zebra-state/tests/` and compiled against your manifests. They see **only the
public API** of those crates. Anything you leave `pub(crate)`, `pub(super)`,
`private`, or behind `#[cfg(test)]` / `#[cfg(feature = "proptest-impl")]` is
invisible to them — see §9 for what that deliberately excludes.

Conventions used below:

- "must" means a test fails if it is not so.
- `C` is always a type parameter bounded by `amount::Constraint`.
- Amounts are zatoshi; 1 ZEC = 100,000,000 zatoshi.
- "the corpus" means the 17-block fixture set described in §8.

---

### How this step is checked

The gate groups are `build`, `workspace-checks`, `inherited-block-codec`,
`inherited-zip215-ed25519`, `inherited-orchard-bundle-codec` and
`inherited-note-commitment-trees`. They add no new requirement and only veto:
if any one of them fails, the step is not satisfied no matter how much of the
new work you completed.

Four of these six gate groups re-run inherited behaviour from earlier
milestones (§6) — nothing in them is new here, and breaking any of it
fails the step.

The requirement groups carry this step's new capability and its peripheral
breadth; every case in them is in scope.

The practical consequence: preserving the inherited behaviour the gate
checks is necessary but adds nothing by itself, and doing nothing else
leaves all of the new work undone.

## 1. `zebra_chain::amount` — the arithmetic substrate

### 1.1 `Amount<C>`

```rust
pub struct Amount<C = NegativeAllowed>(/* private */);
```

The constraint parameter must default to `NegativeAllowed`. The inner value is
an `i64` count of zatoshi and stays private.

Required inherent methods on `impl<C> Amount<C>`:

```rust
pub fn constrain<C2>(self) -> Result<Amount<C2>>   where C2: Constraint;
pub fn to_bytes(&self) -> [u8; 8];
pub fn from_bytes(bytes: [u8; 8]) -> Result<Amount<C>>  where C: Constraint;
pub fn zero() -> Amount<C>                         where C: Constraint;
```

`to_bytes` must produce the **little-endian** two's-complement encoding of the
inner `i64`, and `from_bytes` must be its exact inverse for every in-range
value, re-applying `C`'s range check on the way in. `Amount::<C>::from_bytes(a.to_bytes())`
must equal `Ok(a)` for any `a` that `C` admits. A test round-trips
`0`, `1`, `-1`, `MAX_MONEY` and `-MAX_MONEY` and also checks the byte order
directly against `i64::to_le_bytes`.

`Result<T>` here is `amount::Result<T> = std::result::Result<T, amount::Error>`.

### 1.2 The `Constraint` trait, and genericity

```rust
pub trait Constraint {
    fn valid_range() -> std::ops::RangeInclusive<i64>;
    fn validate(value: i64) -> Result<i64> { /* provided */ }
}

pub struct NegativeAllowed;   // valid_range() == -MAX_MONEY..=MAX_MONEY
pub struct NonNegative;       // valid_range() ==           0..=MAX_MONEY

pub const COIN: i64 = 100_000_000;
pub const MAX_MONEY: i64 = 21_000_000 * COIN;
```

`Constraint` must be **public and implementable outside the crate**: a
test defines its own marker type with `valid_range() == -10..=10` and runs the
arithmetic below through it. So every operator impl must be written
`impl<C> Op for Amount<C> where C: Constraint`, not specialised to the two
markers shipped in the crate. Concretely, a test does this and it must compile
and behave:

```rust
struct Tiny;
impl Constraint for Tiny { fn valid_range() -> RangeInclusive<i64> { -10..=10 } }
let a: Amount<Tiny> = 7i64.try_into().unwrap();
assert!((a + a).is_err());       // 14 is outside -10..=10
```

### 1.3 Conversions and operators

Required, all generic over `C: Constraint` unless noted:

- `TryFrom<i32>`, `TryFrom<i64>`, `TryFrom<u64>`, `TryFrom<i128>` for `Amount<C>`,
  each with `Error = amount::Error`.
- `From<Amount<C>> for i64` (infallible, no constraint bound).
- `Add`, `Sub` in all four `Amount`/`Result<Amount>` combinations, and the
  matching `AddAssign`/`SubAssign` on `Result<Amount<C>>`.
- `Neg for Amount<C>` with **`type Output = Amount<NegativeAllowed>`** — negating
  any amount, including a `NonNegative` one, yields a `NegativeAllowed` amount
  directly, not a `Result`. This is a change from the previous step; see §10.
- `Mul<u64> for Amount<C>` and `Mul<Amount<C>> for u64`, `Div<u64> for Amount<C>`,
  each returning `Result<Amount<C>>`.
- `Sum<Amount<C>>` and `Sum<&Amount<C>>` for `Result<Amount<C>>`.
- `PartialEq<Amount<C2>> for Amount<C1>` (cross-constraint), `Eq`,
  `PartialEq<i64> for Amount<C>` and `PartialEq<Amount<C>> for i64`,
  `PartialOrd<Amount<C2>> for Amount<C1>`, and `Ord for Amount<C>`.
- `Hash`, `Clone`, `Copy`, and a `Debug` impl.

Division truncates toward zero, as `i64` division does: `Amount(-7) / 2` must be
`Ok(-3)`, not `-4`.

### 1.4 `amount::Error` — exact variants

```rust
pub enum Error {
    Constraint { value: i64, range: RangeInclusive<i64> },
    Convert { value: i128, source: std::num::TryFromIntError },
    MultiplicationOverflow { amount: i64, multiplier: u64, overflowing_result: i128 },
    DivideByZero { amount: i64 },
    SumOverflow { partial_sum: i64, remaining_items: usize },
}
```

The enum and every field must be public, and the enum must **not** be
`#[non_exhaustive]`: tests construct values of it and match on it
exhaustively. It must derive/impl `Debug`, `Clone`, `PartialEq` and `Eq`.

Which variant is raised where — all asserted:

- A value outside `C::valid_range()`, from `try_from`, `from_bytes` or
  `constrain`, is `Constraint { value, range }`, where `value` is the
  offending value as an `i64` and `range` is `C::valid_range()` for the
  **target** constraint. `Add` and `Sub` report `Constraint` too: they convert
  their `i64` result with the same `try_into`.
- `TryFrom<i128>` for a value that does not fit in an `i64` is
  `Convert { value, source }` carrying the original `i128`. (A value that fits in
  an `i64` but is out of range is `Constraint`, not `Convert`.)
- `Mul<u64>` whose product does not convert into a valid `Amount<C>` is
  `MultiplicationOverflow { amount, multiplier, overflowing_result }` —
  **whether the product overflows `i64` or merely leaves
  `C::valid_range()`**. `overflowing_result` is the **wide `i128`** product,
  not a wrapped `i64`. `Mul` is the one operator that folds a plain range
  violation into its own variant; it never reports `Constraint`. So
  `MAX_MONEY * 2u64` and `Amount::<Tiny>::try_from(-3)? * 4u64` are both
  `MultiplicationOverflow`, even though `4_200_000_000_000_000` and `-12` fit
  in an `i64` with room to spare.
- `Div<u64>` by `0` is `DivideByZero { amount }`. `Div` does **not** share
  `Mul`'s folding: dividing a valid `Amount<C>` by a non-zero `u64` always
  lands inside the range, so `DivideByZero` is the only error it can raise.
- `Sum` that leaves the range partway through is
  `SumOverflow { partial_sum, remaining_items }`, where `partial_sum` is the
  **offending total** — the running total *including* the item that broke the
  range, i.e. the `value` the underlying `Constraint` carried — and
  `remaining_items` is the number of items the iterator still holds **after**
  the one that broke it. A test sums
  `[MAX_MONEY, MAX_MONEY, 1, 2]` as `Amount<NonNegative>` and requires
  `SumOverflow { partial_sum: 2 * MAX_MONEY, remaining_items: 2 }`.

Plus:

```rust
impl Error { pub fn invalid_value(&self) -> i128; }
```

returning `value` for `Constraint` and `Convert`, `overflowing_result` for
`MultiplicationOverflow`, `amount` for `DivideByZero`, and `partial_sum` for
`SumOverflow`. A test calls this on one instance of every variant.

---

## 2. `zebra_chain::value_balance` — the four chain value pools (ZIP-209)

### 2.1 The type

```rust
pub struct ValueBalance<C> { /* private fields */ }
```

Four pools in this order, and the order is observable (§2.3): **transparent,
sprout, sapling, orchard**. `Clone + Copy + Debug + PartialEq + Eq + Hash`.

Required on `impl<C> ValueBalance<C> where C: Constraint + Copy`:

```rust
pub fn zero() -> Self;
pub fn from_transparent_amount(a: Amount<C>) -> Self;
pub fn from_sprout_amount(a: Amount<C>) -> Self;
pub fn from_sapling_amount(a: Amount<C>) -> Self;
pub fn from_orchard_amount(a: Amount<C>) -> Self;

pub fn transparent_amount(&self) -> Amount<C>;
pub fn sprout_amount(&self) -> Amount<C>;
pub fn sapling_amount(&self) -> Amount<C>;
pub fn orchard_amount(&self) -> Amount<C>;

pub fn remaining_transaction_value(&self) -> Result<Amount<NonNegative>, amount::Error>;
pub fn constrain<C2>(self) -> Result<ValueBalance<C2>, ValueBalanceError> where C2: Constraint;
pub fn to_bytes(self) -> [u8; 32];
pub fn from_bytes(bytes: [u8; 32]) -> Result<ValueBalance<C>, ValueBalanceError>;

pub fn update_with_block(
    self,
    block: impl std::borrow::Borrow<Block>,
    utxos: &HashMap<transparent::OutPoint, transparent::Utxo>,
) -> Result<ValueBalance<C>, ValueBalanceError>;
```

Each `from_*_amount` constructor sets exactly one pool and leaves the other
three at `Amount::zero()`; a test checks all four against all four.

### 2.2 `remaining_transaction_value` — the sign convention

```
remaining = (transparent + sprout + sapling + orchard)?.constrain::<NonNegative>()
```

That is: the **sum of all four** pool balances, then constrained to be
non-negative. This is the consensus rule "the remaining value in the transparent
transaction value pool MUST be nonnegative" (`/app/specs/nu5.txt` §3.4),
expressed over the transaction value pools, where shielded balances already
carry the sign that says whether they add to or take from the transparent pool.

**This is a change of convention from the previous step**, which computed
`transparent - (sprout + sapling + orchard)`. Implementing the old formula makes
every test in this section fail. See §10.

A balance whose four pools sum to a negative number must return
`Err(amount::Error::Constraint { value: <the negative sum>, range: 0..=MAX_MONEY })`
— the error type here is `amount::Error`, **not** `ValueBalanceError`.

### 2.3 Byte layout

`to_bytes` concatenates the four pools' `Amount::to_bytes()` in the order
transparent, sprout, sapling, orchard, giving 32 bytes: bytes `0..8` transparent,
`8..16` sprout, `16..24` sapling, `24..32` orchard, each little-endian `i64`.
`from_bytes` is the inverse and re-checks each amount against `C`.

When `from_bytes` or `constrain` rejects, the error must **name the pool that
failed**, using the variant below, and must check the pools in the order
transparent, sprout, sapling, orchard so the first failing pool is the one
reported. A test plants an out-of-range `i64` in each 8-byte quarter in
turn and requires the matching variant.

### 2.4 `ValueBalanceError`

```rust
pub enum ValueBalanceError {
    Transparent(amount::Error),
    Sprout(amount::Error),
    Sapling(amount::Error),
    Orchard(amount::Error),
}
```

Public, not `#[non_exhaustive]`, `Debug + Clone + PartialEq + Eq`, and an
`Error`/`Display` impl. The pool that failed is identified by the *variant*, and
that is what the tests match on; the rendered `Display` wording is not
pinned, only that the impl exists and renders. Tests also construct these
values and compare whole `Result`s against them, so the inner `amount::Error`
must match §1.4 exactly.

### 2.5 Operators

Required: `Add` and `Sub` in all four `ValueBalance`/`Result<ValueBalance>`
combinations, `AddAssign`/`SubAssign` on `Result<ValueBalance<C>>`,
`Sum<ValueBalance<C>>` and `Sum<&ValueBalance<C>>` for
`Result<ValueBalance<C>, ValueBalanceError>`, and
`Neg for ValueBalance<C>` with `type Output = ValueBalance<NegativeAllowed>`.

All of these operate **pool-wise and independently**: adding two balances adds
transparent to transparent and sprout to sprout, and a failure in one pool is
reported with that pool's variant. Negation flips the sign of all four.

### 2.6 `update_with_block` — the ZIP-209 rule

`update_with_block` takes this balance as the running chain pool total, asks the
block for its chain value pool change (§3.1), and applies it. It must return
`Err` naming the pool if any of the four pools would leave `C`'s range.

`/app/specs/zips/zip-0209.rst` states this bound over the chain value pools it
names, the transparent pool among them. This milestone models four of them —
transparent, sprout, sapling, orchard — and a test relies on all four
being checked through the same path.

Note the sign relation the tests use: a chain pool total of `ValueBalance::zero()`
updated with a coinbase-only block ends up with the whole block subsidy in the
**transparent** pool as a positive amount.

---

## 3. `Block` and `Transaction` accessors

### 3.1 `zebra_chain::block::Block`

`Block`'s two fields stay public:

```rust
pub struct Block {
    pub header: Header,
    pub transactions: Vec<std::sync::Arc<Transaction>>,
}
```

Tests build derived blocks by cloning a header and supplying a filtered
transaction list, so neither field may become private or change type.

New methods:

```rust
pub fn sapling_transactions_count(&self) -> u64;
pub fn orchard_transactions_count(&self) -> u64;
pub fn chain_value_pool_change(
    &self,
    utxos: &HashMap<transparent::OutPoint, transparent::Utxo>,
) -> Result<ValueBalance<NegativeAllowed>, ValueBalanceError>;
pub fn auth_data_root(&self) -> AuthDataRoot;
```

- `sapling_transactions_count` counts transactions for which
  `Transaction::has_sapling_shielded_data()` is true — a transaction with at
  least one Sapling spend or output. `orchard_transactions_count` likewise over
  `has_orchard_shielded_data()`. Both return `u64`.
- `chain_value_pool_change` is **the negation of** the sum of the block's
  transactions' `value_balance`s. The chain pool has the opposite sign to the
  transaction pool: value the transactions take out of the transparent pool is
  value the chain's transparent pool gains. A block with no transactions changes
  no pool (`ValueBalance::zero()`), which is a case a test constructs.
- `utxos` must contain every `Utxo` the block's transparent inputs spend. Tests
  avoid needing a UTXO set by evaluating sub-blocks whose transactions have
  no `PrevOut` inputs, so `chain_value_pool_change` must not require entries it
  does not actually consult.

### 3.2 `zebra_chain::transaction::Transaction`

```rust
pub fn value_balance(
    &self,
    utxos: &HashMap<transparent::OutPoint, transparent::Utxo>,
) -> Result<ValueBalance<NegativeAllowed>, ValueBalanceError>;

pub fn output_values_to_sprout(&self) -> Box<dyn Iterator<Item = &Amount<NonNegative>> + '_>;
pub fn input_values_from_sprout(&self) -> Box<dyn Iterator<Item = &Amount<NonNegative>> + '_>;

pub fn has_sapling_shielded_data(&self) -> bool;
pub fn has_orchard_shielded_data(&self) -> bool;
pub fn auth_digest(&self) -> Option<AuthDigest>;
```

The two Sprout iterators are the delicate ones, and they replace the previous
step's single `sprout_pool_added_values`:

- `output_values_to_sprout` yields each JoinSplit's **`vpub_old`** — value moved
  *out of* the transparent pool and *into* the Sprout pool.
- `input_values_from_sprout` yields each JoinSplit's **`vpub_new`** — value moved
  *out of* the Sprout pool and *into* the transparent pool.

Both must cover V2, V3 and V4 transactions with either BCTV14 or Groth16
JoinSplit proofs, and must yield nothing for V1 and V5. Getting these two
swapped is invisible in a transaction's own totals but inverts the sign of the
Sprout chain pool; a test checks the two separately against per-block
totals decoded from the raw block bytes (§8.2), and separately checks that the
Sprout **chain** pool change equals `vpub_old - vpub_new` summed over the
block — the negation of the transaction-side balance below, because §3.1
defines `chain_value_pool_change` as the negation of the transactions' value
balances.

The Sprout contribution to a transaction's value balance follows from the above:
`sprout` pool balance = (value into the transparent pool) − (value out of it) =
`sum(vpub_new) − sum(vpub_old)`.

---

## 4. `zebra_chain::history_tree` — the empty/non-empty split (ZIP-221)

### 4.1 Types

```rust
pub struct NonEmptyHistoryTree { /* private */ }

#[derive(Debug, Default, Clone)]
pub struct HistoryTree(Option<NonEmptyHistoryTree>);

impl From<NonEmptyHistoryTree> for HistoryTree {}
impl std::ops::Deref for HistoryTree { type Target = Option<NonEmptyHistoryTree>; }
```

`HistoryTree` must be a newtype over `Option<NonEmptyHistoryTree>` and must
`Deref` to that `Option`. Tests call `tree.is_none()`, `tree.is_some()`
and `tree.as_ref().unwrap().hash()` directly on a `HistoryTree`, which only works
through this `Deref`. `HistoryTree` must also derive `Default` (an empty tree)
and `Clone`.

`NonEmptyHistoryTree` must be `Debug` and `Clone`. Its `Clone` has to produce an
independently usable tree, not a shallow alias.

### 4.2 `NonEmptyHistoryTree`

```rust
pub fn from_block(
    network: Network,
    block: Arc<Block>,
    sapling_root: &sapling::tree::Root,
    orchard_root: &orchard::tree::Root,
) -> Result<Self, HistoryTreeError>;

pub fn push(
    &mut self,
    block: Arc<Block>,
    sapling_root: &sapling::tree::Root,
    orchard_root: &orchard::tree::Root,
) -> Result<(), HistoryTreeError>;

pub fn from_cache(
    network: Network,
    size: u32,
    peaks: BTreeMap<u32, Entry>,
    current_height: Height,
) -> Result<Self, HistoryTreeError>;

pub fn hash(&self) -> ChainHistoryMmrRootHash;
pub fn peaks(&self) -> &BTreeMap<u32, Entry>;
pub fn size(&self) -> u32;
pub fn current_height(&self) -> Height;
pub fn network(&self) -> Network;
```

Note that `orchard_root` is a plain reference, **not** an `Option`: pre-Orchard
blocks pass `orchard::tree::Root::default()` and it is ignored. `orchard::tree::Root`
must therefore implement `Default`.

`Entry` in the two signatures above is the entry type of the `zcash_history`
crate. The tests **do not** name it, because they cannot: `zcash_history`
is a dependency of `zebra-chain` but not of an integration test compiled against
it, and `zebra-chain` re-exports only `V1` and `V2`, so no path in scope spells
the type. Every assertion reaches `peaks()` through inference —
`peaks().len()`, `peaks().keys()` — or reaches a rebuilt tree through
`from_block` plus `push`. Keep the signatures as written; the type itself is not
part of what is asserted.

Observable behaviour the tests assert, on mainnet:

- `from_block` at the Heartwood activation height (**903,000** on mainnet) gives
  a tree with `size() == 1`, exactly one entry in `peaks()`,
  `current_height() == Height(903_000)` and `network() == Network::Mainnet`.
- `hash()` of that one-block tree equals the `ChainHistoryMmrRootHash` that block
  **903,001** commits to in its header (§4.4). This is the headline assertion of
  the group: the tree root a block builds is what the *next* block commits to.
- `hash()` must actually depend on the Sapling note commitment root passed in:
  building the same tree with a byte-reversed `sapling_root` must give a
  different hash.
- `push`ing block 903,001 onto that tree gives `size() == 3`,
  `current_height() == Height(903_001)`, and exactly one peak.
- That surviving peak is the node at position **2**, and `peaks().keys()`
  collected in order must be exactly `[2]`. The position is a separate
  assertion from the count, and it has to be, because the count alone cannot
  distinguish a correct peak walk from a broken one: a mountain range over two
  leaves stores node 0 and node 1 (the two leaves) and node 2 (their parent),
  and a peak walk that is off by one also retains exactly one entry -- it just
  retains a leaf instead of the parent. Pruning to `{1}` and pruning to `{2}`
  are both "one peak"; only one of them is the mountain range's peak.
- `push` **panics** if the block's height is not exactly one more than
  `current_height()`. This is a programming error, not a `HistoryTreeError`; the
  test drives it under `std::panic::catch_unwind`, so it must be a panic and not
  an abort or a silently corrupted tree.

### 4.3 `HistoryTree`

```rust
pub fn from_block(
    network: Network, block: Arc<Block>,
    sapling_root: &sapling::tree::Root, orchard_root: &orchard::tree::Root,
) -> Result<Self, HistoryTreeError>;

pub fn push(
    &mut self, network: Network, block: Arc<Block>,
    sapling_root: sapling::tree::Root, orchard_root: orchard::tree::Root,
) -> Result<(), HistoryTreeError>;
```

Note the asymmetry, which the tests rely on: `HistoryTree::from_block` takes the
roots **by reference**, `HistoryTree::push` takes them **by value**, and
`HistoryTree::push` additionally takes the `Network` (the non-empty tree already
knows its own).

Behaviour:

- `from_block` with a **pre-Heartwood** block returns an *empty* tree —
  `Ok(HistoryTree(None))` — not an error. Corpus blocks 0, 10, 202, 347499,
  347500, 347501, 419199, 419200, 419201 and 653600 are all pre-Heartwood.
- `from_block` at or after activation returns a non-empty tree wrapping the
  `NonEmptyHistoryTree` that `NonEmptyHistoryTree::from_block` would build, with
  the same `hash()`.
- `push` on an **empty** tree behaves like `from_block` on the pushed block:
  a **pre-Heartwood** block is accepted and leaves the tree empty — `Ok(())`,
  receiver still `None` — and the **activation block** creates the tree in
  place, so the receiver becomes `Some(..)` with `size() == 1`. The tests start
  from `HistoryTree::default()`, push block 653,600 (pre-Heartwood) and then
  block 903,000 into it. Pushing a pre-activation block onto an empty tree is
  **not** the "not the immediate successor of the tip" programming error: there
  is no tip yet.

### 4.4 `HistoryTreeError` and the block commitment types

```rust
#[non_exhaustive]
pub enum HistoryTreeError {
    #[non_exhaustive] InnerError { inner: zcash_history::Error },
    IOError(#[from] std::io::Error),
}
```

The enum may stay `#[non_exhaustive]`, but `From<std::io::Error> for HistoryTreeError`
must be public — §7 constructs one that way. `HistoryTreeError` must impl
`std::error::Error`, `Debug`, `PartialEq` and `Eq`.

From `zebra_chain::block`:

```rust
pub struct ChainHistoryMmrRootHash(/* private */);
impl From<[u8; 32]> for ChainHistoryMmrRootHash {}
impl From<ChainHistoryMmrRootHash> for [u8; 32] {}

pub enum Commitment {
    PreSaplingReserved([u8; 32]),
    FinalSaplingRoot(sapling::tree::Root),
    ChainHistoryActivationReserved,
    ChainHistoryRoot(ChainHistoryMmrRootHash),
    ChainHistoryBlockTxAuthCommitment(ChainHistoryBlockTxAuthHash),
}
```

The inner array of `ChainHistoryMmrRootHash` may stay private, but both `From`
impls are required — that is how a test turns a header's committed bytes into a
value comparable with `NonEmptyHistoryTree::hash()`, and how it reverses one.

`Block::commitment(network) -> Result<Commitment, CommitmentError>` must return
`Commitment::ChainHistoryActivationReserved` for the Heartwood activation block
and `Commitment::ChainHistoryRoot(..)` for blocks after it. Per
`/app/specs/zips/zip-0221.rst`, the activation block's header field is all zero
bytes and "MUST NOT be interpreted as a root hash"; a test checks both
that the raw header bytes at that height are `[0u8; 32]` and that `commitment`
reports the reserved variant rather than a root.

`CommitmentError` must name the reason a commitment cannot be computed; the
variant the tests match is `CommitmentError::MissingBlockHeight { .. }`, which a
block with no coinbase height (cleared transactions) produces in place of any
commitment — `Block::commitment` returns `Err` for such a block rather than
panicking.

`sapling::tree::Root` must stay `pub struct Root(pub [u8; 32])` with
`From<[u8; 32]>` and `From<Root> for [u8; 32]`.

`Height` must support `Height - Height -> i32` (used by `push`'s height check),
and `NetworkUpgrade::Heartwood.activation_height(Network::Mainnet)` must be
`Some(Height(903_000))`.

---

## 5. ZIP-244 authorizing data commitments

### 5.1 `zebra_chain::transaction::AuthDigest`

```rust
pub struct AuthDigest(/* [u8; 32], may be pub(crate) */);
impl<'a> From<&'a Transaction> for AuthDigest {}
```

`Clone + Copy + Eq + PartialEq + Debug`. The inner array may stay `pub(crate)`.
Unlike `AuthDataRoot` (§5.2), `AuthDigest`'s `Debug` *rendering* is not pinned —
the trait is required only so that tests can compare digests with
`assert_eq!`; nothing parses its output.

There are two entry points and they differ only in what they do with a pre-v5
transaction. `From<&Transaction>` panics for one; the fallible
`Transaction::auth_digest` returns `None` for V1–V4 and `Some(digest)` for V5.
**For a v5 transaction the two must agree**: `AuthDigest::from(&tx)` and
`tx.auth_digest().unwrap()` are the same commitment, and a test asserts
that equality against the published vectors. The panic's *message* is not part
of the contract; only that it panics.

The digest is BLAKE2b-256 over the concatenation of
`transparent_scripts_digest || sapling_auth_digest || orchard_auth_digest`
(32 bytes each), personalized `"ZTxAuthHash_" || CONSENSUS_BRANCH_ID` — twelve
ASCII bytes with exactly one underscore, followed by the 4-byte little-endian
branch id. See `/app/specs/zips/zip-0244.rst`, "auth_digest".

**What "field encoding" means for the three sub-digests** — the ZIP defines
the term (line 25) as *the binary serialized form of a Zcash transaction field
as specified in section 7.1 of the protocol specification*, and §7.1's field
table is what settles the two components that carry a length on the wire:

- **A.1, `transparent_scripts_digest`** hashes the transparent inputs'
  `scriptSig` values **with** the compactSize length prefix each one carries
  in the transaction encoding — `scriptSig` is a `byte[]` field, so its own
  serialized form includes its length, and hashing the raw script bytes alone
  is short by that prefix.
- **A.3, `orchard_auth_digest`** is
  `BLAKE2b-256("ZTxAuthOrchaHash", proofsOrchard || vSpendAuthSigsOrchard ||
  bindingSigOrchard)` where `proofsOrchard` is the aggregated proof **without**
  a length prefix: §7.1 gives it as `byte[sizeProofsOrchard]` with the length
  in the *separate* `sizeProofsOrchard` field that precedes it, so the field's
  own encoding is `sizeProofsOrchard` proof bytes and nothing more. The trap
  here is the crate's own `Halo2Proof`, whose inner `Vec<u8>` is public: its
  `ZcashSerialize` writes that compactSize length (which is what makes
  `self.proof.zcash_serialize(..)` produce the `sizeProofsOrchard ||
  proofsOrchard` pair the bundle layout asks for), so a digest built by pushing
  a proof through `zcash_serialize` is wrong by exactly that one byte — hash
  the proof's bytes, not its serialization.
  `vSpendAuthSigsOrchard` is one 64-byte signature per action and
  `bindingSigOrchard` is 64 bytes; neither carries a prefix of its own.
- **A.2, `sapling_auth_digest`** is the Sapling proofs, spend auth sigs and
  binding sig in that order; `Groth16Proof` is a fixed 192 bytes per proof, so
  no field there carries a length.

The published vectors are the authority: the `auth_digest_hex` column of
`zip244.tsv` is what the driver compares against, and it walks the table
in row order, so the first row it can disagree on is the first row with an
Orchard bundle (`zip244-00`; five of the ten rows — `-00`, `-05`, `-07`, `-08`,
`-09` — carry actions, and they are the only ones where the `proofsOrchard`
rule above is observable at all: on the other five the Orchard sub-digest is
`BLAKE2b-256("ZTxAuthOrchaHash", [])` and the rule cannot change it).

### 5.2 `zebra_chain::block::merkle::AuthDataRoot`

```rust
pub struct AuthDataRoot(/* [u8; 32], may be pub(crate) */);
impl<T: AsRef<Transaction>> std::iter::FromIterator<T> for AuthDataRoot {}
impl std::iter::FromIterator<transaction::AuthDigest> for AuthDataRoot {}
```

`Clone + Copy + Eq + PartialEq`. Both `FromIterator` impls are required, and the
blanket one must be over `T: AsRef<Transaction>` so that iterators of
`Arc<Transaction>`, `&Arc<Transaction>` and `&Transaction` all collect.

**`Debug` format is normative.** Tests read the digest bytes out of the
`Debug` rendering, because the inner array is not public. It must be exactly:

```
AuthRoot("<64 lowercase hex characters>")
```

i.e. `f.debug_tuple("AuthRoot").field(&hex::encode(&self.0)).finish()`. The hex
is the raw internal byte order, not reversed.

### 5.3 The tree

Per `/app/specs/zips/zip-0244.rst`, "Block Header Changes":

1. Each transaction contributes its `auth_digest()`. A transaction **before v5**
   contributes the placeholder **32 bytes of `0xFF`**.
2. The leaf list is padded with `[0u8; 32]` leaves up to the next power of two.
   (A one-transaction block needs no padding, so a one-leaf tree's root **is**
   that leaf — tests use this to observe a single transaction's digest.)
3. Interior nodes are `BLAKE2b-256(left || right)` personalized with the
   sixteen-byte string **`"ZcashAuthDatHash"`**, combined pairwise, level by
   level, until one node remains.

Note the two different padding values: the pre-v5 *placeholder* is `0xFF` x 32
and the tree *padding* leaf is `0x00` x 32. Swapping them changes the root of
every pre-NU5 block, and a test independently recomputes the tree with
`blake2b_simd` to catch exactly that.

`Block::auth_data_root()` must equal `block.transactions.iter().collect::<AuthDataRoot>()`.

---

## 6. Retained surface still exercised

Four gate groups re-run behaviour established at earlier milestones. None of it
is new here and none of it adds a requirement; all of it vetoes. Everything these
groups touch is `zebra-chain` public API, which is the crate this step rewrites
`amount`, `value_balance` and `history_tree` in, so a refactor that reaches
further than intended is caught here rather than passing for free.

- **`inherited-block-codec`** re-runs the block codec against the whole corpus.
  It needs `Block: ZcashDeserialize + ZcashSerialize`, `block::Hash: Display`
  (byte-reversed hex), `Block::coinbase_height()`,
  `block::merkle::Root: FromIterator<transaction::Hash>`,
  `transparent::Input::Coinbase`, and the existing behaviour that an absurd
  transaction count is a `SerializationError` rather than a huge allocation and
  a truncated block is an I/O error.
- **`inherited-zip215-ed25519`** re-runs the ZIP-215 signature rules through
  `zebra_chain::primitives::ed25519`: its re-exported `VerificationKey`,
  `VerificationKeyBytes`, `SigningKey`, `Signature`, `Error` and
  `batch::{Item, Verifier}`, the `TryFrom<VerificationKeyBytes>` and
  byte-array conversions, and the cofactored verification equation that makes
  low-order keys and non-canonical encodings valid. It is fixture-free: the
  vectors are constants in the test and the one randomised path uses a
  fixed-seed generator, so the group is deterministic.
- **`inherited-orchard-bundle-codec`** re-runs V5 Orchard bundle parsing over
  `zip244.tsv`: `Transaction::orchard_shielded_data()`,
  `Transaction::orchard_actions()`, and `Transaction::orchard_nullifiers()`
  yielding one `orchard::Nullifier` per action with `[u8; 32]` conversion, plus
  a byte-for-byte reserialization. It deliberately reaches the bundle through
  the inherited `Option` accessor rather than this step's new
  `has_orchard_shielded_data()` predicate (§3.2), which a requirement group asserts.
- **`inherited-note-commitment-trees`** re-runs the incremental Sapling and
  Orchard note commitment trees over `note_commitment_trees.tsv`:
  `sapling::tree::{NoteCommitmentTree, Root}`,
  `orchard::tree::{NoteCommitmentTree, Root}`, their `append`, `root`, `count`
  and `Uncommitted` behaviour, `sprout::tree::Root`'s byte conversions, and the
  published depth-32 empty roots. This step memoizes both roots (§9); the
  memoized value must equal the value the previous milestone returned.

Do not regress any of it.

**Inherited construction shapes the *requirement* groups build with.** Separately
from the gate groups above, the tests for this step's new behaviour
build their inputs out of surface that is unchanged from step 07. It is not new
here and there is no reason to touch it, but the tests name these variants and
fields directly, so a rename fails them even where the behaviour under test is
implemented correctly:

- `Transaction::V1 { inputs, outputs, lock_time }` and
  `transaction::LockTime::Height(Height)` — used to build a pre-v5 transaction
  by literal (§5.1).
- `Transaction::version() -> u32`, `Transaction::inputs() -> &[transparent::Input]`,
  `Transaction::outputs() -> &[transparent::Output]`.
- `transparent::Utxo { output, height, from_coinbase }`,
  `transparent::Output { value, lock_script }`,
  `transparent::Script::new(&[u8])`, and the
  `transparent::Input::PrevOut { outpoint, .. }` variant — used to build the
  UTXO maps `chain_value_pool_change` is called with (§3.1). Note that
  `value` is **both** a public field and an accessor method, and the
  tests reach it through the **method**:
  `pub fn value(&self) -> Amount<NonNegative>`. This accessor existed in the
  previous milestone returning `Amount<NegativeAllowed>`; the constraint is
  narrowed to `NonNegative` here, and tests sum its results directly
  with `.sum::<Result<Amount<NonNegative>, _>>()`. Implement the accessor with
  that return type, and keep the field — an implementation that offers only
  one of the two fails to compile against the drivers.
- `block::Header`'s public `commitment_bytes: [u8; 32]` field — read directly
  at the Heartwood activation height (§4.4).

**The crates the tests compile against.** The tests are ordinary
integration tests injected into `zebra-chain/tests/` and `zebra-state/tests/`,
so each one sees exactly its host crate's `[dependencies]` and
`[dev-dependencies]` — nothing else. The `zebra-chain` tests name five of them
directly, and all five are already dependencies of `zebra-chain` and **must
remain so**:

| crate | referenced by | for |
| --- | --- | --- |
| `hex` | eight of the ten drivers | decoding the fixture corpus, and rendering roots for comparison |
| `blake2b_simd` | `zip244-auth-digest` | the independent tree recomputation of §5.3 |
| `rand_core` | `inherited-zip215-ed25519` | implementing the test RNG the batch verifier is driven with |
| `halo2` | `inherited-note-commitment-trees` | `pasta::pallas`, to re-derive Sapling commitments independently |
| `jubjub` | `inherited-note-commitment-trees` | `Fq::from_bytes`, to build note commitments from the vectors |

This costs nothing to honour — the offline build rule in `WORKSPACE_HANDOFF.md`
("do not add a third-party dependency that is not already somewhere in
`Cargo.lock`") means dropping any of them is strictly more work than leaving it
— but a test that cannot compile fails for a reason that has nothing to
do with this step's behaviour. The last three are load-bearing inside
`zebra-chain/src` as well, so nothing in this step is likely to remove them; the
row is here so that a reader who prunes the dependency list knows which
injections depend on which entry.

---

## 7. `zebra-state`

```rust
pub mod constants;   // must stay public

pub use zebra_chain::transparent::MIN_TRANSPARENT_COINBASE_MATURITY;
pub const MAX_BLOCK_REORG_HEIGHT: u32 = MIN_TRANSPARENT_COINBASE_MATURITY - 1;
pub const DATABASE_FORMAT_VERSION: u32 = 9;
pub const MAX_LEGACY_CHAIN_BLOCKS: usize = 100;
```

- `DATABASE_FORMAT_VERSION` must be **9**. The previous release wrote 6. The
  state's cache directory is keyed on this value, so leaving it alone makes an
  upgraded node reopen a database whose layout no longer matches.
- `MAX_LEGACY_CHAIN_BLOCKS` is new and must be `100` with type `usize`.
- `MIN_TRANSPARENT_COINBASE_MATURITY` must **move to
  `zebra_chain::transparent`** and be re-exported from `zebra_state::constants`,
  so both paths name one `u32` equal to `100`. A test reads both paths and
  compares them.
- `MAX_BLOCK_REORG_HEIGHT` is `MIN_TRANSPARENT_COINBASE_MATURITY - 1`, i.e. `99`.

And:

```rust
pub enum ValidateContextError {
    /* ... */
    HistoryTreeError(#[from] zebra_chain::history_tree::HistoryTreeError),
}
```

`ValidateContextError` must be publicly re-exported from the crate root, and
`From<HistoryTreeError> for ValidateContextError` must exist. Its `Display` for
that variant must mention "history tree", and `Error::source()` on it must return
the underlying `HistoryTreeError`. The variant itself may be `#[non_exhaustive]`
as long as the `From` impl is generated.

---

## 8. Fixtures available to tests

`$HBV_FIXTURES` holds:

### 8.1 The block corpus

`blocks.tsv` plus `blocks/<name>.hex`: 17 real blocks — mainnet 0, 10, 202,
347499, 347500, 347501, 419199, 419200, 419201, 653600, 903000, 903001, 982681
and testnet 0, 207500, 280000, 1028500.

### 8.2 `value_pools.tsv`

Per-block expected values, decoded from the raw block bytes by an independent
Python implementation of the transaction wire format that shares no code with
`zebra-chain`. Columns: `name`, `tx_count`, `clean_txs` (transactions with no
`PrevOut` input), then the four pool deltas `transparent`, `sprout`, `sapling`,
`orchard` over those transactions, then `sapling_txs`, `orchard_txs`,
`vpub_old`, `vpub_new`.

Facts from it that tests assert by name, useful as a sanity check while
implementing:

- Nine corpus blocks are coinbase-only and change only the transparent pool.
- Mainnet 982,681 is a ZIP-213 shielded coinbase: its change is
  `(transparent 125_000_000, sprout 0, sapling 500_026_380, orchard 0)`, summing
  to 625,026,380 zatoshi — the post-Blossom 6.25 ZEC subsidy plus 26,380 zatoshi
  of fees.
- Mainnet 903,000 is a Sprout-to-Sapling migration: sprout −610,000,000 and
  sapling +609,970,000, the 30,000 difference being the fee.
- No corpus block is post-NU5, so `orchard_transactions_count()` is `0` and
  `has_orchard_shielded_data()` is `false` everywhere.

### 8.3 `zip244.tsv`, `history_tree.tsv` and `note_commitment_trees.tsv`

`zip244.tsv` carries the ten published v5 transaction vectors. Its `tx_hex`
column is the whole transaction on the wire — tests parse it and
reserialize it — and `txid_hex` and `auth_digest_hex` are the expected
identifiers for each. Five of the ten carry a real Orchard bundle, of one, two
or three actions; the other five carry none.

`history_tree.tsv` carries, for mainnet 903,000 and 903,001, the
`sapling_final_root_hex` of that block's final treestate. That is an **input**
to the tree, not an expected output: `NonEmptyHistoryTree::from_block` and
`push` take it as the Sapling root argument (§4.2). The expected MMR root is
not shipped as a column **because it is not needed to check one**: the
tests compare `hash()` against the `ChainHistoryMmrRootHash` that block
903,001's own header commits to, and that header is already in the block corpus
(§8.1). That comparison is the headline assertion of the group (§4.2); the
fixture file carries only the inputs it cannot derive.

`note_commitment_trees.tsv` carries the published Sapling and Orchard note
commitment vectors: 33 empty roots for each pool, one per layer from the leaves
to the root, and the incremental commitments — 16 Sapling ones with the root
after each, and 50 Orchard ones in groups of five with a root after each group,
so 10 Orchard roots. It is read only by the `inherited-note-commitment-trees` gate
group (§6).

---

## 9. Negative space — changes at this step that are *out of scope*

Named here so the gaps are deliberate rather than accidental. Implement them or
not; no *in-scope* group can see them. One of them — the memoized note commitment
tree roots — is watched by a gate group, which adds no requirement either
and says so in its own entry below.

- **Outdated-peer rejection.** `Version::min_remote_for_height` changes to accept
  an `impl Into<Option<Height>>`, and the handshake starts refusing peers below
  it. All of it is inside `zebra-network`'s private `mod protocol` and
  `mod peer`; `CURRENT_NETWORK_PROTOCOL_VERSION` does not change.
- **Cached note commitment tree roots.** The Sapling and Orchard trees memoize
  their root. The cache itself is unobservable — no group in scope can tell a
  memoized root from a recomputed one, so caching adds nothing. The root's
  *value* is observable, and it must not change: the
  `inherited-note-commitment-trees` gate group (§6) checks it against the
  published vectors, so a cache that goes stale vetoes the step.
- **The `#[cfg(any(test, feature = "proptest-impl"))]` mutators.**
  `ValueBalance::{update_with_transaction, update_with_transparent_input,
  sapling_value_balance_mut, orchard_value_balance_mut, output_values_mut,
  output_values_to_sprout_mut, input_values_from_sprout_mut}` and
  `Transaction::{orchard_shielded_data_mut, sapling_value_balance_mut,
  orchard_value_balance_mut, output_values_mut}` are gated out of a normal build.
  Keep them gated; do not promote them to satisfy a test.
- **`ValueBalance::update_with_chain_value_pool_change`** is `pub(crate)`. The
  public path to the same behaviour is `update_with_block`.
- **The per-pool private accessors** `sapling_value_balance`,
  `orchard_value_balance`, `sprout_value_balance`, `transparent_value_balance`.
  The public readers are `*_amount` (§2.1). `instruction.md` §2 asks for these
  balances to be *reachable* while computing a block's pool change; that is an
  in-crate requirement, satisfied by these existing private methods. Keeping
  them `pub(crate)` conforms: no test names them, and promoting them to
  `pub` also conforms, so neither choice costs anything.
- **`Config::db_config`** is `pub(crate)`, so nothing checks the cache path the
  bumped format version produces — only the constant itself.
- **The new `#[non_exhaustive]` `ValidateContextError` variants**
  `NegativeRemainingTransactionValue { amount_error, height, tx_index_in_block,
  transaction_hash }` (renamed from `InvalidRemainingTransparentValue`),
  `CalculateRemainingTransactionValue` and `CalculateTransactionValueBalances`.
  They cannot be constructed from outside the crate and the checks that raise
  them are behind private modules, so only the `HistoryTreeError` conversion
  (§7) is checked. Rename and add them anyway: the next milestone's state work
  expects them.
- **The state service's contextual checks** that actually apply §2.6 and §4 to a
  committing block. Reachable only through an async service with a real database.

---

## 10. Cross-step hazards

Three retained meanings change here. All three compile either way, so none of
them shows up as a build error:

1. **`ValueBalance::remaining_transaction_value` flips convention** from
   `transparent - (sprout + sapling + orchard)` to
   `transparent + sprout + sapling + orchard`, constrained non-negative (§2.2).
2. **`Neg for Amount<C>` and `Neg for ValueBalance<C>` return a value, not a
   `Result`**, with `Output` fixed to the `NegativeAllowed` constraint. Code from
   the previous step that wrote `(-amount)?` no longer type-checks; code that
   silently relied on a `Result` will.
3. **The Sprout term of a transaction's value balance flips sign.** The previous
   step computed it as `sum(vpub_old) - sum(vpub_new)`; here it is
   `sum(vpub_new) - sum(vpub_old)` (§3.2). Carrying the earlier step's
   expression forward compiles, produces amounts of the right magnitude, and
   inverts the Sprout column of every `ValueBalance` — and, through the negation
   in §3.1, of every Sprout chain pool change as well.

Also note `Transaction::sprout_pool_added_values` is **removed** and replaced by
the two directional iterators in §3.2. Keeping the old method as well is
harmless; keeping only the old one fails §3.
