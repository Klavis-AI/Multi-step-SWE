# TEST_INTERFACE.md — milestone 7, contextual validation: value pools, ordered UTXOs, note commitment trees

This document is the contract the tests are written against. Every
module path, type, method, field, constant, error variant, sign convention and
byte layout the verifier depends on is named here. If a behaviour is not
stated here or in a specification document named by `instruction.md`, it is
not in scope.

## 1. How the verifier reaches your code

The verifier runs nine groups. Seven run **integration test files**, copied
into two crates' `tests/` directories:

```
cargo test --offline --locked --jobs 4 -p zebra-chain --test <name> -- --test-threads=1
cargo test --offline --locked --jobs 4 -p zebra-state --test <name> -- --test-threads=1
```

(If `--offline --locked` cannot resolve, the verifier retries with `--offline`
alone, then `--locked` alone, then neither, for every cargo invocation —
build, check and test alike.) The other three groups are the GATE tier — they
add no requirement and only veto, and two of them run whole-workspace commands
rather than a test file:

```
cargo build --offline --locked -p zebra-chain --tests
cargo check --offline --locked --workspace --all-targets --all-features
```

`--all-features` is **required** on the check: from `v1.0.0-alpha.8` on,
`zebra-chain/benches/block.rs` uses `zebra_chain::block::tests::generate::*`,
and `block.rs` gates that module behind `#[cfg(any(test, feature = "bench"))]`.
A bench target is not built with `cfg(test)`, so the bare `--all-targets` form
does not compile. Keep the `bench` feature and the bench target wired up.

Consequences you must respect:

- The tests reach your code **only through the public API** of whichever crate
  they are compiled into (`zebra_chain` or `zebra_state`), as an external
  consumer. `pub(crate)`, `pub(super)` and private items are invisible to
  them. A `#[non_exhaustive]` struct or enum *variant* cannot be constructed
  from outside the defining crate even when the type is `pub`.
- Each test file is compiled against that crate's `Cargo.toml` **as you leave
  it**. Do not remove an existing `[dependencies]` or `[dev-dependencies]`
  entry from `zebra-chain` or `zebra-state`. In particular the tests name
  `jubjub` and `halo2` directly (for `halo2::pasta::pallas` and
  `halo2::arithmetic::FieldExt`): in `zebra-chain` those are ordinary
  `[dependencies]`, which cargo also links into test targets. **This
  milestone adds `halo2` and `jubjub` to `zebra-state`'s
  `[dev-dependencies]`** — the workspace you inherit does not carry them
  there, and the `zebra-state` driver does not compile without them.
  `zebra-state`'s dev-dependency on `zebra-chain` carries the
  `proptest-impl` feature.
- The workspace must keep building **offline**; do not add a third-party
  dependency to any manifest beyond the one named below.
  **`incrementalmerkletree = "0.1.0"` is new at this milestone and you must add
  it to `zebra-chain`'s `[dependencies]`** — it is not in the workspace you
  inherit, and §5's depth-32 frontiers are expected to be built on
  `incrementalmerkletree::bridgetree`. Its exact crate, version (`0.1.0`) and
  source (crates.io) are fixed by this line: `--offline --locked` must resolve
  after you add it, so pin the version string exactly as written and do not
  reach for a later release, a git revision, or a different frontier crate.
  Everything else in the graph stays as you inherited it.
- A group whose driver fails to compile fails for that group.
- Tests run with `--test-threads=1` and must not depend on execution order
  between `#[test]` functions in the same file.

### How this step is checked

The gate groups are `build`, `workspace-checks` and `inherited-block-codec`.
They add no new requirement and only veto: if any one of them fails, the step
is not satisfied no matter how much of the new work you completed.

The requirement groups carry this step's new capability and its peripheral
breadth; every case in them is in scope.

The practical consequence: preserving the inherited behaviour the gate
checks is necessary but adds nothing by itself, and doing nothing else
leaves all of the new work undone.

## 2. Where the implementation goes

| Module | Declared as | What this milestone adds |
| --- | --- | --- |
| `zebra_chain::amount` | `pub mod amount;` (exists) | `Amount::zero`, `Neg` for `Amount<NegativeAllowed>`, two `Sum` impls, `Error::SumOverflow` (§3) |
| `zebra_chain::value_balance` | **new** `pub mod value_balance;` | `ValueBalance<C>`, `ValueBalanceError` (§4) |
| `zebra_chain::transaction::Transaction` | `pub enum Transaction` (exists) | `value_balance`, `coinbase_spend_restriction`, the four `has_*` predicates, `sapling_note_commitments`, `orchard_note_commitments` (§4, §6) |
| `zebra_chain::transparent` | `pub mod transparent;` (exists) | `OrderedUtxo`, `CoinbaseSpendRestriction`, `new_ordered_outputs`, `utxos_from_ordered_utxos`, `Input::value` (§6) |
| `zebra_chain::sapling::tree`, `zebra_chain::orchard::tree` | `pub mod tree;` (exists, near-empty) | a working depth-32 incremental Merkle frontier (§5) |
| `zebra_chain::sprout::tree` | `pub mod tree;` (exists) | `Root`'s four `From` impls; `NoteCommitmentTree` made `pub` with `Default` + `root()` (replacing the private `hash()`), serde derives, and a new **stub** `append` (§5.4) |
| `zebra_state` | crate root | `PreparedBlock::new_outputs` retyped, `ValidateContextError` variants, `DATABASE_FORMAT_VERSION` (§7) |

Internal file layout is yours; only the names below, at the paths given, are
in scope.

## 3. `zebra_chain::amount` — group `amount-sum-and-zero` (`zebra-chain`)

`Amount<C = NegativeAllowed>` and the marker types `NegativeAllowed` /
`NonNegative` are unchanged from step 1, as are `COIN = 100_000_000` and
`MAX_MONEY = 21_000_000 * COIN`, `NegativeAllowed::valid_range() ==
-MAX_MONEY..=MAX_MONEY` and `NonNegative::valid_range() == 0..=MAX_MONEY`.
`pub type Result<T, E = Error> = std::result::Result<T, E>` in that module is
what every fallible signature below returns.

Add:

```rust
impl<C> Amount<C> {
    /// Create a zero `Amount`. Zero is inside both constraints' ranges.
    pub fn zero() -> Amount<C> where C: Constraint;
}

impl std::ops::Neg for Amount<NegativeAllowed> {
    type Output = Self;
    fn neg(self) -> Self::Output;
}

impl<C: Constraint> std::iter::Sum<Amount<C>> for Result<Amount<C>> { .. }
impl<'amt, C: Constraint + Copy + 'amt> std::iter::Sum<&'amt Amount<C>> for Result<Amount<C>> { .. }
```

Required behaviour:

- `Amount::<NegativeAllowed>::zero()` and `Amount::<NonNegative>::zero()` both
  succeed and both equal `Amount::try_from(0).unwrap()`. Zero is the additive
  identity: `x + zero == Ok(x)` and `x - zero == Ok(x)` for any in-range `x`.
- Negation is only defined for `Amount<NegativeAllowed>`, is total on that
  type (because the range is symmetric), and satisfies `-(-x) == x` and
  `-Amount::zero() == Amount::zero()`.
- **Summation order of checks.** The sum is accumulated with
  `i64::checked_add`. If the running `i64` total overflows, the result is
  `Err(Error::SumOverflow)` and **not** a range error — the range check can
  only be applied to a total that exists. If the `i64` total does not overflow
  but lies outside `C::valid_range()`, the result is
  `Err(Error::Contains { range, value })` where `value` is the **total**, not
  the offending term, and `range` is `C::valid_range()`.
- An empty iterator sums to `Ok(Amount::zero())` for both constraints.
- Summing by reference gives the same answer as summing by value.
- The turbofish the tests use is `.sum::<Result<Amount<C>, Error>>()`, so the
  `Sum` impls must be reachable at exactly those type parameters.

Add one variant to the existing `pub enum Error` (`thiserror::Error` +
`displaydoc::Display`, `Clone + Debug + PartialEq`), leaving the four existing
variants `Contains`, `Convert`, `MultiplicationOverflow`, `DivideByZero`
unchanged:

```rust
/// i64 overflow when summing i64 amounts
SumOverflow,
```

`SumOverflow` is a **fieldless** variant, so its `Display` text is the whole
of it and is part of the contract: the tests build the variant and
compare `.to_string()` against the doc-comment line above, exactly
`i64 overflow when summing i64 amounts`.

The `Display` text of `Contains` stays
`input {value} is outside of valid range for zatoshi Amount, valid_range={range:?}`.
Where the tests read a variant's *fields* rather than its text — `Contains`'s
`value` and `range` — they match on the variant and read those fields, and do
not compare the surrounding message text; only `SumOverflow`, which has no
fields, is compared by its string.

`Amount`'s `Debug` impl **is replaced at this step** and is checked here: step 1
printed `Amount(42)`, with no trace of the constraint, and that is no longer
enough. The impl you leave must print `Amount<`, the **fully qualified**
`std::any::type_name::<C>()` gives it, `>`, then the `i64` in tuple-struct
form — e.g. `Amount<zebra_chain::amount::NonNegative>(42)`. The tests assert
that the printed name contains the constraint's name and that the two
constraints print differently, so do not erase `C` from the output.

## 4. Value pools — groups `value-balance-pools` (`zebra-chain`) and `transaction-value-balance` (`zebra-chain`)

### 4.1 `zebra_chain::value_balance::ValueBalance<C>`

```rust
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct ValueBalance<C> { /* four private Amount<C> fields */ }
```

The four pool fields are **private**; all access is through the methods below.
`C` is the same constraint parameter as `Amount<C>`; the methods are on
`impl<C> ValueBalance<C> where C: Constraint + Copy`.

```rust
pub fn zero() -> Self;

pub fn from_transparent_amount(transparent_amount: Amount<C>) -> Self;
pub fn from_sprout_amount(sprout_amount: Amount<C>) -> Self;
pub fn from_sapling_amount(sapling_amount: Amount<C>) -> Self;
pub fn from_orchard_amount(orchard_amount: Amount<C>) -> Self;

pub fn transparent_amount(&self) -> Amount<C>;
pub fn sprout_amount(&self) -> Amount<C>;
pub fn sapling_amount(&self) -> Amount<C>;
pub fn orchard_amount(&self) -> Amount<C>;

pub fn set_transparent_value_balance(&mut self, transparent_value_balance: ValueBalance<C>) -> &Self;
pub fn set_sprout_value_balance(&mut self, sprout_value_balance: ValueBalance<C>) -> &Self;
pub fn set_sapling_value_balance(&mut self, sapling_value_balance: ValueBalance<C>) -> &Self;
pub fn set_orchard_value_balance(&mut self, orchard_value_balance: ValueBalance<C>) -> &Self;

pub fn remaining_transaction_value(&self) -> Result<Amount<NonNegative>, amount::Error>;
```

Note the setters' shape: each takes a **whole `ValueBalance<C>`**, not an
`Amount<C>`, reads only its own pool out of the argument, and returns `&Self`.
That is what makes `set_sapling_value_balance(self.sapling_value_balance()?)`
compose in §4.3 without unwrapping an `Amount` first.

Required behaviour:

- `zero()` has all four pools at `Amount::zero()`.
- `from_X_amount(a)` fills pool `X` with `a` and leaves the other three at
  zero. Four constructors, four disjoint effects — a constructor that fills
  more than its own pool fails the group.
- `set_X_value_balance(other)` overwrites pool `X` of `self` with pool `X` of
  `other` and touches nothing else, including when `other` has all four pools
  populated.
- Equality is pool-wise and derived.

### 4.2 Arithmetic and `ValueBalanceError`

```rust
#[derive(thiserror::Error, Debug, Clone, PartialEq)]
pub enum ValueBalanceError {
    #[error("value balance contains invalid amounts")]
    AmountError(#[from] amount::Error),
}

impl<C: Constraint> std::ops::Add for ValueBalance<C> { type Output = Result<ValueBalance<C>, ValueBalanceError>; }
impl<C: Constraint> std::ops::Sub for ValueBalance<C> { type Output = Result<ValueBalance<C>, ValueBalanceError>; }
impl<C: Constraint> std::ops::Add<ValueBalance<C>> for Result<ValueBalance<C>, ValueBalanceError> { type Output = Result<ValueBalance<C>, ValueBalanceError>; }
impl<C: Constraint> std::ops::Sub<ValueBalance<C>> for Result<ValueBalance<C>, ValueBalanceError> { type Output = Result<ValueBalance<C>, ValueBalanceError>; }
```

- Addition and subtraction act **pool by pool**; no pool is mixed into
  another.
- The output is a `Result`, and the `Result`-on-the-left impls exist so that
  `a + b + c` and `a - b - c` chain without an intermediate `?`. An `Err` on
  the left short-circuits.
- If any pool leaves `C::valid_range()`, the whole operation is
  `Err(ValueBalanceError::AmountError(amount::Error::Contains { .. }))` — the
  amount error is wrapped, not discarded, and is reachable by matching the
  variant.
- `remaining_transaction_value()` returns `amount::Error` **directly**, not
  `ValueBalanceError`. It is the one method on the type that does not wrap.

`remaining_transaction_value()` computes

```
transparent - (sprout + sapling + orchard)
```

and then constrains the result to `NonNegative`. So:

- On a `ValueBalance<NegativeAllowed>` whose transparent pool is at least the
  sum of the three shielded pools, it returns `Ok` of that difference.
- If the difference is negative it returns
  `Err(amount::Error::Contains { range: 0..=MAX_MONEY, value })`, where
  `value` is the negative difference. This is the consensus rule "the
  remaining value in the transparent transaction value pool MUST be
  nonnegative" (protocol spec §3.4, "Transactions and Treestates", line 917 of
  the shipped `specs/nu5.txt`).
- It is also defined on a `ValueBalance<NonNegative>`, where it can still fail
  if the shielded pools exceed the transparent one.

### 4.3 `Transaction::value_balance` and the four sign conventions

```rust
impl Transaction {
    pub fn value_balance(
        &self,
        utxos: &HashMap<transparent::OutPoint, transparent::Utxo>,
    ) -> Result<ValueBalance<NegativeAllowed>, amount::Error>;
}
```

`utxos` must contain an entry for every `Input::PrevOut` of the transaction,
including outputs created by earlier transactions in the same block. Inputs
whose outpoint is absent are a caller error, not a recoverable one. A coinbase
transaction has no `PrevOut` inputs, so an **empty map** is sufficient for it.

The four pools, each of which the tests recompute independently from a
different public entry point and compare:

| Pool | Definition | Sign |
| --- | --- | --- |
| transparent | `Σ value(UTXO each input spends) − Σ value(outputs created)` | **not** negated |
| sprout | `Σ over all JoinSplits of (vpub_old − vpub_new)` | **not** negated |
| sapling | `Σ of all valueBalanceSapling fields` | **negated** |
| orchard | `Σ of all valueBalanceOrchard fields` | **negated** |

Two negated, two not. This is the single highest-risk item in the milestone:
an implementation that negates all four, or none, still produces a plausible
`ValueBalance` and still passes anything that looks at one pool in isolation.
The corpus the signed cases run on stops at Canopy (§9), so **no block in it
carries a non-empty Orchard bundle**: the Orchard row of the table is
required and stated, but the only Orchard value any case in scope observes is
zero, and a wrong Orchard sign is therefore not caught at this step. The
Sapling sign is caught, because the corpus does carry Sapling bundles.

Per-version sourcing, which the tests rely on:

- Sprout is non-zero only for `V2`, `V3` and `V4` (their `joinsplit_data`).
  `V1` has no JoinSplits and `V5` removed them: both contribute zero.
- Sapling comes from `sapling_shielded_data` on `V4` and `V5` only; `V1`,
  `V2`, `V3` contribute zero.
- Orchard comes from `orchard_shielded_data` on `V5` only.
- A transaction with no data for a pool contributes `Amount::zero()` to it,
  not an error.

Supporting accessors the tests use, all of which must be public:

```rust
// zebra_chain::transparent
impl Input {
    pub fn outpoint(&self) -> Option<OutPoint>;              // None for Coinbase
    pub fn value(&self, utxos: &HashMap<OutPoint, Utxo>) -> Amount<NegativeAllowed>;
}
impl Output {
    pub fn value(&self) -> Amount<NegativeAllowed>;           // the `pub value` field, constrained
}

// zebra_chain::transaction
impl JoinSplitData<P> { pub fn value_balance(&self) -> Result<Amount, amount::Error>; }
// zebra_chain::sapling
impl ShieldedData<AnchorV> { pub fn value_balance(&self) -> Amount<NegativeAllowed>; }  // also `pub value_balance` field
// zebra_chain::orchard
impl ShieldedData { pub fn value_balance(&self) -> Amount<NegativeAllowed>; }           // also `pub value_balance` field
```

`Input::value` on an `Input::Coinbase` is `Amount::zero()` — a coinbase input
creates value, it does not spend any. That is why a coinbase's transparent
pool change is exactly the negation of the value it creates.

**A coinbase transaction's shielded pools are not automatically zero.** From
Heartwood (ZIP-213) a coinbase may pay its reward to a Sapling address, and
the shipped block corpus contains such a block; the tests check the coinbase's
Sapling pool against its actual `valueBalanceSapling`, not against zero. What
*is* invariant for a coinbase is that it has no JoinSplits (Sprout pool zero)
and that, if it pays out anything at all, its
`remaining_transaction_value()` is an error.

## 5. Note commitment trees — group `note-commitment-trees` (`zebra-chain`)

`zebra_chain::sapling::tree` and `zebra_chain::orchard::tree` must each expose
a working incremental Merkle frontier. Both have the same shape; only the leaf
type, the hash function and the uncommitted leaf differ.

### 5.1 Common contract

Tree depth is **32** in both pools, counted as the number of levels between
the leaf layer and the root. Layer numbering in the spec is inverted relative
to the frontier library's altitude numbering: **layer 0 is the root and layer
32 is the leaf layer**; a frontier's altitude 0 is the leaf. Get this
backwards and every empty root comes out as some other layer's value.

```rust
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct NoteCommitmentTree { /* private */ }

impl Default for NoteCommitmentTree { /* the empty tree */ }
impl PartialEq for NoteCommitmentTree { /* equal iff their `hash()`es are equal */ }
impl Eq for NoteCommitmentTree {}

impl NoteCommitmentTree {
    pub fn append(&mut self, cm: Leaf) -> Result<(), NoteCommitmentTreeError>;
    pub fn root(&self) -> Root;
    pub fn hash(&self) -> [u8; 32];
    pub fn count(&self) -> u64;
}

impl From<Vec<Leaf>> for NoteCommitmentTree { .. }

#[derive(thiserror::Error, Debug, PartialEq, Eq)]
pub enum NoteCommitmentTreeError {
    #[error("The note commitment tree is full")]
    FullTree,
}
```

`NoteCommitmentTreeError` is **not** `#[non_exhaustive]`, at the type or the
variant: `FullTree` must be constructible by an external crate (§7.2 depends
on this).

Required behaviour, checked against the vectors in the fixture file
`note_commitment_trees.tsv`:

- `Default::default()` is an empty tree. `count()` on it is `0`.
- `count()` equals the number of successful `append` calls.
- `append` returns `Ok(())` while the tree has room; the depth-32 tree in the
  corpus never fills, so `FullTree` is never observed on a real append.
- `root()` of the empty tree equals the published depth-32 empty root for that
  pool.
- Appending the published commitments one at a time reproduces **every**
  published incremental root, in order — not merely the final one. An
  implementation that only gets the last root right fails.
- `From<Vec<Leaf>>` produces a tree equal (`PartialEq`) to one built by
  appending the same leaves in the same order, with the same `root()` and the
  same `count()`.
- `hash()` returns the same 32 bytes as `<[u8; 32]>::from(tree.root())`.

### 5.2 Sapling

- Leaf type: `jubjub::Fq` (the `cm_u` of a note commitment). `From<jubjub::Fq>`
  for the internal node type.
- `NoteCommitmentTree::uncommitted()` — an **associated function on the tree
  type** `zebra_chain::sapling::tree::NoteCommitmentTree`, not a free
  function and not a method on the internal node type introduced two lines
  above — returning `[u8; 32]`, the published `Uncommitted^Sapling`
  value, which is `I2LEBSP_256(1)`, i.e. the little-endian encoding of the
  field element 1. The drivers call it as
  `sapling::tree::NoteCommitmentTree::uncommitted()`.
- `pub struct Root(pub [u8; 32])` with `From<[u8; 32]>`, `From<&[u8; 32]>`,
  `From<Root> for [u8; 32]` and `From<&Root> for [u8; 32]`. Round-tripping a
  root through its byte array is the identity.
- `Root`'s `Debug` is **hand-written, not derived**, and renders its bytes as
  lowercase hex rather than as a byte list: `format!("{:?}", root)` contains
  `hex::encode(root.0)`. A derived `Debug` prints a decimal array and fails
  the conversion test, so keep the hand-written impl.
- Hash function is `MerkleCRH^Sapling` (protocol spec §5.4.1.3).

### 5.3 Orchard

- Leaf type: `halo2::pasta::pallas::Base` (the `cm_x` of an action's note
  commitment). `From<pallas::Base>` for the internal node type.
- `NoteCommitmentTree::uncommitted()` — the same associated-function shape as
  Sapling's (on `zebra_chain::orchard::tree::NoteCommitmentTree`) — returning
  `pallas::Base`, the published
  `Uncommitted^Orchard`, which is the field element **2**. Note the return
  type differs from Sapling's: Orchard returns the field element, Sapling
  returns bytes.
- `pub struct Root(..)` with a **private** inner `pallas::Base`, and therefore
  `TryFrom<[u8; 32]> for Root` (not `From`) — not every 32-byte string is a
  Pallas base field element. Conversions out (`From<Root> for [u8; 32]` and
  `From<&Root> for [u8; 32]`) are infallible. `Root` also implements
  `ZcashSerialize`/`ZcashDeserialize` and `Hash`.
- Hash function is `MerkleCRH^Orchard` (Sinsemilla, protocol spec §5.4.1.3).
- Because `pallas::Base::from_bytes`/`to_bytes` are `halo2::arithmetic::FieldExt`
  trait methods returning `subtle::CtOption`, keep `FieldExt` implemented for
  the leaf type and do not change those signatures.

### 5.4 Sprout

Sprout's tree is **not** reimplemented at this milestone, but two things about
it are checked, and both have to be reachable from outside `zebra-chain`
because the tests are an external crate:

- `zebra_chain::sprout::tree::Root`'s conversions: `From<[u8; 32]>`,
  `From<&[u8; 32]>`, `From<Root> for [u8; 32]`, `From<&Root> for [u8; 32]`,
  and that they round-trip.
- `zebra_chain::sprout::tree::NoteCommitmentTree` is a `pub struct` that
  implements `Default` and exposes `pub fn root(&self) -> Root`. The workspace
  you inherit declares this type `struct`, i.e. private, with no `root()` —
  make it public and add the accessor. Nothing else about it changes, and
  Sprout's tree is not built here: `Default::default()` keeps the derived
  empty value it has today (`count`/`height` zero, a zeroed root), and the
  test only requires that the two `Root` conversions agree on whatever
  `root()` returns. Do not seed it from a real frontier.

`sprout::tree::NoteCommitmentTree::append(&mut self, _cm: &sprout::note::NoteCommitment)`
is **new here, and is a stub**: the workspace you inherit has no `append` at
all, and this milestone adds one whose body may `todo!()` — no case in scope
calls it. Sprout tree *arithmetic* — appending commitments and updating the
root — is out of scope for this milestone and is implemented later in the
ladder; do not build a real frontier here. The same edit makes the type
`pub`, keeps `Default`, replaces the private `hash() -> [u8; 32]` with
`pub fn root(&self) -> Root`, and adds `Serialize`/`Deserialize` to its
derives.

## 6. Ordered UTXOs and coinbase spends — group `ordered-utxo-coinbase-spend` (`zebra-chain`)

### 6.1 `transparent::OrderedUtxo`

`Utxo` is unchanged from earlier steps:

```rust
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Utxo {
    pub output: Output,
    pub height: block::Height,
    pub from_coinbase: bool,
}
```

New:

```rust
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct OrderedUtxo {
    pub utxo: Utxo,
    /// The index, in its block, of the transaction that created this output.
    pub tx_index_in_block: usize,
}

impl OrderedUtxo {
    pub fn new(
        output: Output,
        height: block::Height,
        from_coinbase: bool,
        tx_index_in_block: usize,
    ) -> OrderedUtxo;
}
```

Both fields are `pub`, and `new` takes exactly those four arguments in that
order. `tx_index_in_block` is **not** the output's index within its
transaction (that is `OutPoint::index`); it is the transaction's index within
its block. A block with one transaction therefore has `tx_index_in_block == 0`
for every output, which is why the tests also check multi-transaction blocks.

### 6.2 Index construction

```rust
pub fn new_ordered_outputs(
    block: &Block,
    transaction_hashes: &[transaction::Hash],
) -> HashMap<transparent::OutPoint, OrderedUtxo>;

pub fn new_outputs(
    block: &Block,
    transaction_hashes: &[transaction::Hash],
) -> HashMap<transparent::OutPoint, Utxo>;

pub fn utxos_from_ordered_utxos(
    ordered_utxos: HashMap<transparent::OutPoint, OrderedUtxo>,
) -> HashMap<transparent::OutPoint, Utxo>;
```

All three are free functions at `zebra_chain::transparent`, not methods.
`transaction_hashes` is the block's transaction hashes in block order,
precomputed by the caller.

Required behaviour:

- `new_ordered_outputs` contains exactly one entry per transparent output of
  every transaction in the block, keyed by
  `OutPoint { hash: <that transaction's hash>, index: <output index as u32> }`.
- Each entry's `utxo.height` is the block's `coinbase_height()`.
- Each entry's `utxo.from_coinbase` is `true` exactly for the outputs of the
  block's **first** transaction, and `false` for all others.
- Each entry's `utxo.output` is the output itself.
- Each entry's `tx_index_in_block` is the index of the transaction that
  created it.
- `utxos_from_ordered_utxos` drops `tx_index_in_block` and changes nothing
  else; it neither adds nor removes an entry.
- `new_outputs(b, h) == utxos_from_ordered_utxos(new_ordered_outputs(b, h))`
  for every block. The two must not disagree.

### 6.3 `CoinbaseSpendRestriction`

```rust
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum CoinbaseSpendRestriction {
    /// The UTXO is spent in a transaction with one or more transparent outputs.
    SomeTransparentOutputs,
    /// The UTXO is spent in a transaction which only has shielded outputs.
    OnlyShieldedOutputs { spend_height: block::Height },
}
```

Neither the enum nor its variants are `#[non_exhaustive]`: the tests construct
and match both.

```rust
impl Transaction {
    pub fn coinbase_spend_restriction(&self, spend_height: block::Height)
        -> CoinbaseSpendRestriction;
}
```

The rule is exactly: **if the transaction has no transparent outputs**, the
result is `OnlyShieldedOutputs { spend_height }` carrying the height it was
given; otherwise it is `SomeTransparentOutputs`, which carries no height. The
decision looks only at `self.outputs().is_empty()` — the number of *inputs*,
the transaction version, and whether the transaction actually has shielded
outputs are all irrelevant to this predicate. `spend_height` is passed
through unchanged; it is the height of the block spending the UTXO, not the
height of the block that created it.

These two variants are the *inputs* to the two transparent coinbase consensus
rules (a coinbase output may only be spent by a transaction with no
transparent outputs; and only `MIN_TRANSPARENT_COINBASE_MATURITY` blocks after
it was created). The rules themselves live in `zebra-state`'s private check
module — see §8.

### 6.4 Supporting predicates

```rust
impl Transaction {
    pub fn has_transparent_or_shielded_inputs(&self) -> bool;
    pub fn has_shielded_inputs(&self) -> bool;
    pub fn has_transparent_or_shielded_outputs(&self) -> bool;
    pub fn has_shielded_outputs(&self) -> bool;
    pub fn sapling_note_commitments(&self) -> Box<dyn Iterator<Item = &jubjub::Fq> + '_>;
    pub fn orchard_note_commitments(&self) -> impl Iterator<Item = &pallas::Base>;
}
```

"Shielded inputs" means `joinsplit_count() > 0`, or at least one Sapling spend
(per anchor), or at least one Orchard action while
`orchard_flags()` contains `ENABLE_SPENDS`. "Shielded outputs" means
`joinsplit_count() > 0`, or at least one Sapling output, or at least one
Orchard action while `orchard_flags()` contains `ENABLE_OUTPUTS`. The
`has_transparent_or_shielded_*` pair is the disjunction of the transparent
side with its shielded counterpart. The note-commitment iterators yield in
transaction order and are the bridge between §4 and §5: they are what a
block's tree updates are built from.

**These six accessors are required but not directly asserted on by any
driver.** They are named here because step 8 and `zebra-consensus` call them
by exactly these names and signatures, and because the two transparent
coinbase rules in §6.3 are stated in terms of "only shielded outputs". Do not
rename or re-sign them.

## 7. `zebra-state` — group `state-contextual-surface` (`zebra-state`)

### 7.1 `PreparedBlock`

```rust
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PreparedBlock {
    pub block: Arc<Block>,
    pub hash: block::Hash,
    pub height: block::Height,
    pub new_outputs: HashMap<transparent::OutPoint, transparent::OrderedUtxo>,
    pub transaction_hashes: Vec<transaction::Hash>,
}
```

Re-exported at the crate root as `zebra_state::PreparedBlock`. **Every field
is `pub` and the struct is not `#[non_exhaustive]`**: the tests build one with
a struct literal. The change from step 6 is `new_outputs`' value type, `Utxo`
→ `OrderedUtxo`; a tree that leaves it as `Utxo` does not compile the test.

### 7.2 Errors

```rust
pub use error::{BoxError, CloneError, CommitBlockError, ValidateContextError};

pub type BoxError = Box<dyn std::error::Error + Send + Sync + 'static>;

#[derive(Debug, Error, PartialEq, Eq)]
#[error("block is not contextually valid")]
pub struct CommitBlockError(#[from] ValidateContextError);

#[derive(Debug, Error, Clone)]
#[error(transparent)]
pub struct CloneError { /* private Arc<dyn Error + Send + Sync> */ }

impl From<CommitBlockError> for CloneError { .. }
impl From<BoxError> for CloneError { .. }
```

`ValidateContextError` is a `#[non_exhaustive]` enum; add these variants,
each of which is itself `#[non_exhaustive]` except where noted, alongside the
existing `OrphanedBlock`, `NonSequentialBlock`, `TimeTooEarly`, `TimeTooLate`,
`InvalidDifficultyThreshold` and `DuplicateSproutNullifier`:

| Variant | Fields | `Display` |
| --- | --- | --- |
| `DuplicateTransparentSpend` | `outpoint: transparent::OutPoint, location: &'static str` | `transparent double-spend: {outpoint:?} is spent twice in {location:?}` |
| `MissingTransparentOutput` | `outpoint, location` | `missing transparent output: possible double-spend of {outpoint:?} in {location:?}` |
| `EarlyTransparentSpend` | `outpoint` | `out-of-order transparent spend: {outpoint:?} is created by a later transaction in the same block` |
| `UnshieldedTransparentCoinbaseSpend` | `outpoint` | `unshielded transparent coinbase spend: {outpoint:?} must be spent in a transaction which only has shielded outputs` |
| `ImmatureTransparentCoinbaseSpend` | `outpoint, spend_height, min_spend_height, created_height` (all `block::Height`) | names all four heights and `MIN_TRANSPARENT_COINBASE_MATURITY` |
| `DuplicateSaplingNullifier` | `nullifier: sapling::Nullifier, in_finalized_state: bool` | `sapling double-spend: duplicate nullifier: {nullifier:?}, in finalized state: {in_finalized_state:?}` |
| `DuplicateOrchardNullifier` | `nullifier: orchard::Nullifier, in_finalized_state: bool` | `orchard double-spend: duplicate nullifier: {nullifier:?}, in finalized state: {in_finalized_state:?}` |
| `InvalidRemainingTransparentValue` | `transaction_hash: transaction::Hash, in_finalized_state: bool` | `remaining value in the transparent transaction value pool MUST be nonnegative: ...` |
| `SaplingNoteCommitmentTreeError` | `#[from] sapling::tree::NoteCommitmentTreeError` — **tuple variant, not `#[non_exhaustive]`** | `error in Sapling note commitment tree` |
| `OrchardNoteCommitmentTreeError` | `#[from] orchard::tree::NoteCommitmentTreeError` — **tuple variant, not `#[non_exhaustive]`** | `error in Orchard note commitment tree` |

Behaviour in scope (this is what the driver can actually reach):

- `ValidateContextError::from(sapling::tree::NoteCommitmentTreeError::FullTree)`
  and the Orchard equivalent both compile and land in their **own** variant;
  the two are not equal.
- Each prints exactly the `Display` text in the table.
- Each keeps the originating tree error as its `std::error::Error::source()`,
  whose `Display` is `The note commitment tree is full`. The `#[from]`
  conversion must therefore be a wrapper, not a discard.
- `CommitBlockError::from(v)` prints `block is not contextually valid` — the
  category, not the detail — and exposes `v` as its `source()`. Two
  `CommitBlockError`s over equal `ValidateContextError`s compare equal; over
  different ones they do not.
- `CloneError::from(CommitBlockError)` and `CloneError::from(BoxError)` both
  exist, display **transparently** as whatever they wrapped, and survive
  `.clone()` with the message intact.

### 7.3 Constants

`pub mod constants;` stays public and must contain:

```rust
pub const MIN_TRANSPARENT_COINBASE_MATURITY: u32 = 100;
pub const MAX_BLOCK_REORG_HEIGHT: u32 = MIN_TRANSPARENT_COINBASE_MATURITY - 1;  // 99
pub const DATABASE_FORMAT_VERSION: u32 = 6;
```

`MAX_BLOCK_REORG_HEIGHT` is also re-exported at the crate root
(`zebra_state::MAX_BLOCK_REORG_HEIGHT`) and the two paths must name the same
value. The reorg limit is one less than the maturity so that a matured
coinbase output can never be un-matured by a legal reorg.

`DATABASE_FORMAT_VERSION` moves from `5` to `6`. This is not cosmetic: a
database written by step 6 holds neither the note commitment trees nor the
ordered UTXO index, so it must not be reopened by this step's code.

## 8. Negative space — what this milestone does not ask for, and what is out of scope

- **The contextual checks themselves.** `zebra_state::service::check`
  (transparent spend ordering, coinbase maturity, nullifier uniqueness, anchor
  lookup) is behind `mod service;`, and `ContextuallyValidBlock` — the type
  those checks produce — is `pub(crate)`. They are wanted; they are not
  reachable from an integration test and therefore not checked directly. What
  is checked is every public ingredient they are built from: §4's value
  balances, §6's ordered UTXOs and spend restrictions, §5's trees, and §7's
  error variants. Do not conclude from "out of scope" that the checks are
  optional — step 8 builds directly on them.
- **Most `ValidateContextError` variants.** Every variant in §7.2's table
  except the two tree wrappers is `#[non_exhaustive]` *per variant*, so no
  external crate can construct one and none has a public constructor. Their
  names, fields and `Display` strings are part of the contract and are
  reviewed, but no test asserts on them. Do not make them constructible to
  "help" — step 8 and later steps expect this shape.
- **`FinalizedBlock`.** Its fields are `pub(crate)`; the only public way in is
  `impl From<Arc<Block>> for FinalizedBlock`. Not checked this step.
- **Block-wide and chain-wide value pools.** Summing transaction value
  balances across a block, maintaining the running chain pool, and the ZIP-209
  rule that no pool may go negative are **step 8**. Do not enforce a
  chain-level bound here; §4 is per-transaction only.
- **Anchor validation.** Trees are built and rooted here. Checking a
  transaction's stated anchor against a root the chain actually had is not
  checked this step.
- **The on-disk layout of the trees.** That `DATABASE_FORMAT_VERSION` moves is
  checked; which column families hold what is yours.
- **Sprout tree arithmetic** — out of scope for this milestone (§5.4):
  `sprout::tree::NoteCommitmentTree::append` stays the stub it is. The type
  itself, its `Default` impl and its `root()` accessor *are* checked — see
  §5.4.
- **Proof, signature, script and Equihash verification** — unchanged, still
  pinned external primitives, per `REQUIREMENTS.md`'s shared out-of-scope
  list.
- **Reorg logic** — the constants that bound a reorg are checked, the logic is
  not.

## 9. Regression guard — group `inherited-block-codec` (GATE — adds no requirement, vetoes the step if it fails; `zebra-chain`)

Block serialization must not regress while you are changing `transparent`,
`amount` and the tree modules. The driver re-checks, over the same 17-block
mainnet/testnet corpus used elsewhere:

- every block round-trips byte-for-byte through `ZcashSerialize` /
  `ZcashDeserialize`;
- deserialization consumes the whole block and no more;
- `Block::hash()` matches the corpus;
- `Block::coinbase_height()` matches the corpus;
- the first transaction of every block is a coinbase;
- `Block::merkle_root` is the Merkle root of the block's transaction hashes,
  recomputed independently;
- an absurd transaction count is a deserialization error, not an allocation;
- a truncated block is an `io` error.

It touches no symbol introduced at this step.

**What the gate tier does and does not price.** Three groups veto this step:
`build`, `workspace-checks` and `inherited-block-codec`. The first two are
compile-time only — they fail on a workspace that does not build and pass on
one that does, whatever behaviour was deleted along the way — and
`inherited-block-codec` is the only gate group that *executes* anything, and
executes only the eight block-codec assertions above — among them the
merkle-root recomputation, which does read `Transaction::hash()` for every
corpus transaction, so the *transaction-identifier* path is in fact
re-executed by the gate even though the sighash path is not. So the gate
prices exactly this: the workspace compiles, the step-1 block codec still
round-trips the corpus, and `Transaction::hash()` still yields the corpus
roots. It does **not** re-execute milestones 2–6 (the
transparent coinbase rules, the `Amount` contract, the sighash algorithms,
the ZIP-221 history tree, the step-6 value-pool arithmetic) even
though this step edits the modules those live in — `amount.rs`,
`transparent/utxo.rs`, `transaction.rs`. Breaking one of those is not charged
by a gate here; it is charged downstream, by the later steps that exercise
them. Preserve them because the ladder still requires them and because they
are named in §10, not because a group here will catch a regression.

## 10. Handoff

**Inherited from steps 1–6 and still required:** the V1–V5 transaction codec
and its `TrustedPreallocate` limits, the ZIP-143/243/244 sighash and txid
algorithms, the Orchard bundle codec, `zebra_chain::history_tree::HistoryTree`
and the ZIP-221 chain history commitment, the network-upgrade tables, and a
finalized state that commits blocks and serves them back.

**What step 8 expects to find:** `ValueBalance<C>` with working per-pool
arithmetic and `remaining_transaction_value`, so that a *block's* value pool
change can be summed from its transactions' and the ZIP-209 bound applied to a
running chain pool; `Amount`'s `Sum` impls, because that summation is written
as `.sum()`; `OrderedUtxo` indexes on `PreparedBlock`, because the block-level
value pool needs the UTXOs spent within the block itself; and working
`sapling`/`orchard` note commitment trees with correct empty roots, because
step 8 splits the history tree per network upgrade and needs the tree roots as
anchors.
