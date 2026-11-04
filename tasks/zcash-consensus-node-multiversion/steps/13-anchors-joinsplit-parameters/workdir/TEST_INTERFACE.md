# TEST_INTERFACE.md — milestone 13, anchors-joinsplit-parameters (v1.0.0-beta.3)

This document is the contract the tests are written against. Every
module path, type, method, field, constant, error variant, and byte layout the
verifier depends on is named here. If a behaviour is not stated here or in a
specification document named by `instruction.md`, it is not in scope.

## 1. How the verifier reaches your code

The verifier runs eight groups. Six run **integration test files**, copied
into three different crates' `tests/` directories:

```
cargo test --offline --locked --jobs 4 -p zebra-chain      --test <name> -- --test-threads=1
cargo test --offline --locked --jobs 4 -p zebra-consensus  --test <name> -- --test-threads=1
cargo test --offline --locked --jobs 4 -p zebra-state      --test <name> -- --test-threads=1
```

The other two groups run cargo commands instead of a test file:
`cargo build --offline --locked -p zebra-chain --tests` and
`cargo check --offline --all-targets --all-features --workspace`. The second is
by far the stricter; the first exists as proof of life for the crate the
`block-codec-regression` gate driver is compiled into.

Consequences you must respect:

- The tests reach your code **only through the public API** of whichever
  crate they are compiled into, as an external consumer. `pub(crate)`,
  `pub(super)`, and private items are invisible to them.
- Each test file is compiled against that crate's `Cargo.toml` **as you leave
  it**. Do not remove an existing `[dependencies]` or `[dev-dependencies]`
  entry from `zebra-chain`, `zebra-consensus`, or `zebra-state`.
- The workspace must keep building **offline**; do not add a new third-party
  dependency to any manifest.
- A group whose driver fails to compile fails for that group.
- Tests run with `--test-threads=1` and must not depend on execution order
  between `#[test]` functions in the same file.

### How this step is checked

The gate groups are `build`, `workspace-checks` and `block-codec-regression`.
They add no new requirement and only veto: if any one of them fails, the step
is not satisfied no matter how much of the new work you completed.

The requirement groups carry this step's new capability and its peripheral
breadth; every case in them is in scope.

The practical consequence: preserving the inherited behaviour the gate
checks is necessary but adds nothing by itself, and doing nothing else
leaves all of the new work undone.

## 2. Where the implementation goes

| Module | What this milestone adds |
| --- | --- |
| `zebra_chain::block::Height` | `MAX_EXPIRY_HEIGHT: Height` constant |
| `zebra_chain::transaction::LockTime` | `unlocked()` — the only *new* constructor; `min_lock_time()`/`max_lock_time()` and the `MIN_TIMESTAMP`/`MAX_TIMESTAMP` constants already exist at the predecessor and are unchanged |
| `zebra_chain::transaction::Transaction` | `lock_time()`'s **return type changes** here: `pub fn lock_time(&self) -> Option<LockTime>`, where the predecessor's was `-> LockTime` (§4) |
| `zebra_consensus::transaction::check` | four public check functions: `lock_time_has_passed`, `joinsplit_has_vpub_zero`, `coinbase_expiry_height`, `non_coinbase_expiry_height` |
| `zebra_consensus::error::TransactionError` | eight new variants (§4) |
| `zebra_consensus::error::SubsidyError` | two new variants: `InvalidMinerFees`, `SumOverflow` |
| `zebra_consensus::error::BlockError` | one new struct variant: `SummingMinerFees` |
| `zebra_consensus::groth16::Groth16Parameters` | struct with `directory()` and `failure_hint()` associated functions (upstream carries `sapling`/`sprout` path fields; no case in scope reads a field) |
| `zebra_state::ValidateContextError` | four new variants (§5) |
| `zebra_state::CommitBlockError` | unchanged public surface; `From<ValidateContextError>` already present |

Internal file layout is yours; only the names below, at the paths given, are
in scope.

## 3. `Height::MAX_EXPIRY_HEIGHT` constant — group `max-expiry-height` (`zebra-chain`)

`zebra_chain::block::Height` gains one new associated constant:

```rust
impl Height {
    /// The maximum expiry height for Zcash transactions, per the protocol spec.
    pub const MAX_EXPIRY_HEIGHT: Height = Height(499_999_999);
}
```

Import path: `zebra_chain::block::Height`.

This constant:

- Is `Copy` (inherited from `Height`).
- Is `Debug` (inherited from `Height`), with `{:?}` producing a string
  that contains `"499999999"` or `"Height"`.
- Satisfies `Height::MAX_EXPIRY_HEIGHT == Height(499_999_999)`.
- Satisfies `Height::MAX_EXPIRY_HEIGHT.0 == 499_999_999u32`.
- At v1.0.0-beta.3, `Height::MAX_EXPIRY_HEIGHT == Height::MAX` (both are
  `Height(499_999_999)`). A later PR (#1113) will lower `MAX_EXPIRY_HEIGHT`;
  the invariant asserted here is `<=`, not strict inequality.
- `Height(500_000_000).0 > Height::MAX_EXPIRY_HEIGHT.0` — the value one above
  the limit is strictly greater.

## 4. Lock-time and expiry-height check functions — group `lock-time-expiry-checks` (`zebra-consensus`)

Four public functions are added to `zebra_consensus::transaction::check`:

```rust
pub fn lock_time_has_passed(
    tx: &Transaction,
    block_height: Height,
    block_time: DateTime<Utc>,
) -> Result<(), TransactionError>
```

Returns `Ok(())` when the transaction's lock time has passed or when all
inputs have sequence `0xFFFFFFFF` (finalized by sequence). Returns
`Err(TransactionError::LockedUntilAfterBlockHeight(unlock_height))` when the
lock time is a block height and `block_height <= unlock_height`. Returns
`Err(TransactionError::LockedUntilAfterBlockTime(unlock_time))` when the
lock time is a timestamp and `block_time <= unlock_time`. When
`tx.lock_time()` returns `None` (see the accessor just below), the
function returns `Ok(())` unconditionally regardless of `block_height` and
`block_time`.

**`Transaction::lock_time()`'s return type changes at this milestone.** The
accessor exists at the predecessor with the signature `-> LockTime`; here it
becomes

```rust
// zebra_chain::transaction::Transaction, all five versions
pub fn lock_time(&self) -> Option<LockTime>;
```

`None` when the transaction carries no *effective* lock time, `Some` with the
lock-time value it decoded with otherwise. A lock time is effective exactly
when **both** of these hold:

- it is not the unlocked sentinel — i.e. the stored lock time is not
  `LockTime::unlocked()` (`Height(Height(0))`); and
- at least one of the transaction's transparent inputs has a `sequence`
  number other than `u32::MAX`. `Transaction::inputs()` is iterated in order
  and each element's `sequence` tested, for every transaction version; a
  transaction with no transparent inputs at all (a shielded-only V5, for
  instance) therefore has no effective lock time.

So a transaction whose `nLockTime` decoded to `Height(1_000_000)` and whose
single input has `sequence` `0` gives `Some(LockTime::Height(Height(1_000_000)))`,
while the mainnet genesis coinbase — lock time `Height(0)` *and* sole input
`sequence` `0xFFFFFFFF` — gives `None` on both counts. The driver calls
this accessor directly (`assert_eq!(tx.lock_time(), Some(LockTime::…)`), so it
must be public on `Transaction`, in `zebra-chain`, with exactly this return
type; the `-> LockTime` form no longer compiles against the driver.

```rust
pub fn joinsplit_has_vpub_zero(tx: &Transaction) -> Result<(), TransactionError>
```

Returns `Ok(())` when the transaction has no JoinSplit descriptions, or when
every JoinSplit has at least one of `vpub_old` or `vpub_new` equal to zero.
Returns `Err(TransactionError::BothVPubsNonZero)` when any JoinSplit has
both fields non-zero.

```rust
pub fn coinbase_expiry_height(
    block_height: &Height,
    coinbase: &Transaction,
    network: Network,
) -> Result<(), TransactionError>
```

For pre-NU5 blocks: returns `Ok(())` if the coinbase transaction's
`expiry_height` is at most `Height::MAX_EXPIRY_HEIGHT` (`Height(499_999_999)`),
or `expiry_height` is `None`. Returns
`Err(TransactionError::MaximumExpiryHeight { .. })` if the expiry height
exceeds the maximum. From NU5 activation onward, the expiry height must equal
the block height exactly; a mismatch returns
`Err(TransactionError::CoinbaseExpiryBlockHeight { .. })`.

```rust
pub fn non_coinbase_expiry_height(
    block_height: &Height,
    transaction: &Transaction,
) -> Result<(), TransactionError>
```

Only applies to Overwinter-and-later transactions (`transaction.is_overwintered()`
returns `true`). Returns `Ok(())` if `expiry_height` is `None` or is within
bounds and has not passed. Returns `Err(TransactionError::MaximumExpiryHeight
{ .. })` if the expiry exceeds `Height::MAX_EXPIRY_HEIGHT`. Returns
`Err(TransactionError::ExpiredTransaction { .. })` if
`block_height > expiry_height`. Non-Overwinter transactions always return
`Ok(())` from this function.

**`LockTime` type**, at `zebra_chain::transaction::LockTime`:

```rust
pub enum LockTime {
    Height(block::Height),
    Time(DateTime<Utc>),
}

impl LockTime {
    pub const MIN_TIMESTAMP: i64 = 500_000_000;
    pub const MAX_TIMESTAMP: i64 = u32::MAX as i64;  // 4_294_967_295

    /// Constructs LockTime::Height(Height(0)) — always finalized.
    pub fn unlocked() -> Self;

    /// Constructs LockTime::Time at MIN_TIMESTAMP.
    pub fn min_lock_time() -> LockTime;

    /// Constructs LockTime::Time at MAX_TIMESTAMP.
    pub fn max_lock_time() -> LockTime;
}
```

The threshold between the height and time domains is 500_000_000: values
below this are block heights, values at or above this are Unix timestamps.

## 5. State anchor error variants — group `state-anchor-error-variants` (`zebra-state`)

Four new variants are added to `zebra_state::ValidateContextError` (the two
Sapling/Orchard tree-error variants date from step 07 and are unchanged):

```rust
// Constructable via From<NoteCommitmentTreeError>:
SproutNoteCommitmentTreeError(
    #[from] zebra_chain::sprout::tree::NoteCommitmentTreeError
),
SaplingNoteCommitmentTreeError(
    #[from] zebra_chain::sapling::tree::NoteCommitmentTreeError
),
OrchardNoteCommitmentTreeError(
    #[from] zebra_chain::orchard::tree::NoteCommitmentTreeError
),

// #[non_exhaustive] struct variants — cannot be constructed externally:
#[non_exhaustive]
UnknownSproutAnchor  { anchor: sprout::tree::Root },
#[non_exhaustive]
UnknownSaplingAnchor { anchor: sapling::tree::Root },
#[non_exhaustive]
UnknownOrchardAnchor { anchor: orchard::tree::Root },
```

The `NoteCommitmentTreeError` variants are constructable from outside the
crate via the `From` impl: `ValidateContextError::from(NoteCommitmentTreeError::FullTree)`.
Each such value pattern-matches to the corresponding variant. All three tree
error types have a `FullTree` unit variant.

The `Unknown*Anchor` variants are `#[non_exhaustive]` and cannot be
constructed from outside the crate (E0639). They **can** be matched with
`..` syntax from outside the crate, and their existence as named variants
is verified at compile time. Their anchor field types (`sprout::tree::Root`,
`sapling::tree::Root`, `orchard::tree::Root`) are not required to be
round-trippable to `[u8; 32]` by this driver.

`ValidateContextError` derives `Debug, PartialEq, Eq` but **not** `Clone`.
Two values constructed independently from the same `NoteCommitmentTreeError`
variant compare equal. `Display` text is non-empty for every constructable
variant:

- `SproutNoteCommitmentTreeError(_)` → `"error in Sprout note commitment tree"`
- `SaplingNoteCommitmentTreeError(_)` → `"error in Sapling note commitment tree"`
- `OrchardNoteCommitmentTreeError(_)` → `"error in Orchard note commitment tree"`

`zebra_state::CommitBlockError` wraps `ValidateContextError` via
`#[from]`: `CommitBlockError::from(ValidateContextError::from(NoteCommitmentTreeError::FullTree))`
produces a `CommitBlockError` whose `Display` text is `"block is not contextually valid"`.

**Module paths**:

- `zebra_state::ValidateContextError` — exported from `zebra_state` crate root
- `zebra_state::CommitBlockError` — exported from `zebra_state` crate root
- `zebra_chain::sprout::tree::NoteCommitmentTreeError`
- `zebra_chain::sapling::tree::NoteCommitmentTreeError`
- `zebra_chain::orchard::tree::NoteCommitmentTreeError`

## 6. Transaction and block error variants — group `transaction-error-variants` (`zebra-consensus`)

The following types and variants are added to or confirmed on `zebra_consensus::error`:

**`SubsidyError`** (`Copy, Clone, Debug, PartialEq, Eq`):

```rust
pub enum SubsidyError {
    InvalidMinerFees,  // "miner fees are invalid"
    SumOverflow,       // "a sum of amounts overflowed"
    // ... pre-existing variants unchanged
}
```

**New `TransactionError` variants** (the enum already derives
`Clone, Debug, PartialEq, Eq`):

```rust
LockedUntilAfterBlockHeight(block::Height),
// Display: "transaction is locked until after block height {height}"

LockedUntilAfterBlockTime(DateTime<Utc>),
// Display: "transaction is locked until after block time {time}"

CoinbaseExpiryBlockHeight {
    expiry_height: Option<zebra_chain::block::Height>,
    block_height: zebra_chain::block::Height,
    transaction_hash: zebra_chain::transaction::Hash,
},
// Display: not pinned word for word — the case requires the rendered
//          message to contain "expiry" or "coinbase".

MaximumExpiryHeight {
    expiry_height: zebra_chain::block::Height,
    is_coinbase: bool,
    block_height: zebra_chain::block::Height,
    transaction_hash: zebra_chain::transaction::Hash,
},
// Display: not pinned word for word — the case requires the rendered
//          message to contain "expiry" or "maximum".

ExpiredTransaction {
    expiry_height: zebra_chain::block::Height,
    block_height: zebra_chain::block::Height,
    transaction_hash: zebra_chain::transaction::Hash,
},
// Display: not pinned word for word — the case requires the rendered
//          message to contain "expir" or "mined".

Groth16(String),
// Display: "spend proof MUST be valid given a primary input formed from the other
//           fields except spendAuthSig"
// NOT a new variant: the inherited tree already has `Groth16,` as a unit
// variant. Change that one in place to carry the String payload rather than
// adding a second `Groth16` next to it -- a duplicate variant is a compile
// error that fails `workspace-checks` and takes every other group with it.

MalformedGroth16(String),
// Display: "Groth16 proof is malformed"

BothVPubsNonZero,
// Display: "either vpub_old or vpub_new must be zero"
```

`transaction::Hash` implements `From<[u8; 32]>`, so test code may construct
it as `zebra_chain::transaction::Hash::from([0u8; 32])`.

**`BlockError::SummingMinerFees`** (the enum already derives `Debug, PartialEq, Eq`):

```rust
SummingMinerFees {
    height: zebra_chain::block::Height,
    hash: zebra_chain::block::Hash,
    source: amount::Error,
},
// Display: "summing miner fees for block {height:?} {hash:?} failed: {source:?}"
```

This variant is **not** `#[non_exhaustive]`, unlike the three
`Unknown*Anchor` variants in §5: a case builds it from outside the
crate with a struct literal —
`BlockError::SummingMinerFees { height, hash, source }` — so adding the
attribute here makes the group uncompilable. Its `Display` text above is
asserted, via the `BlockError` wrapper as well as directly.

`amount::Error::Constraint` has fields `value: i64` and
`range: RangeInclusive<i64>` (not a `String`). `block::Hash` is a tuple
struct `Hash(pub [u8; 32])` with no `From<[u8; 32]>` impl; construct it
directly as `zebra_chain::block::Hash([0u8; 32])`.

`BlockError::from(TransactionError::BothVPubsNonZero)` must produce a
`BlockError::Transaction(TransactionError::BothVPubsNonZero)` — the
pre-existing `#[from]` wrapper is unchanged.

## 7. Groth16 parameter surface — group `groth16-params-surface` (`zebra-consensus`)

`zebra_consensus::groth16::Groth16Parameters` is a struct with two associated
functions. Upstream gives it public `sapling`/`sprout` path fields; **no
case in scope reads a field**, so their visibility is yours to choose — what is
checked is the two functions:

```rust
pub struct Groth16Parameters { /* the fields are not checked */ }

impl Groth16Parameters {
    /// Returns the directory path where Groth16 proving parameters are stored.
    /// The path is deterministic: two calls in the same process return the
    /// same value. The returned path is non-empty.
    pub fn directory() -> PathBuf;

    /// Returns a human-readable string explaining what to do when the
    /// parameter files are missing. Non-empty and deterministic across calls.
    /// The string should reference the parameter directory or a recovery action.
    pub fn failure_hint() -> String;
}
```

Both functions have the signatures shown: `directory` returns a `PathBuf`,
`failure_hint` returns a `String`. Both are deterministic: repeated calls
return equal values. Neither triggers a download or file I/O at call time.

The test driver does **not** access `GROTH16_PARAMETERS` or any static that
reads parameter files from disk; it tests only the path-and-hint surface.

Import path: `zebra_consensus::groth16::Groth16Parameters`.

## 8. Regression guard — group `block-codec-regression` (GATE — adds no requirement, vetoes the step if it fails; `zebra-chain`)

A trimmed copy of step 1's block-codec driver, run against the same block
corpus loaded at runtime from `HBV_FIXTURES/blocks/`. Every block in the
corpus still round-trips byte-for-byte. The following properties are
re-verified against each corpus block:

- `Block::hash()` matches the expected hash from the corpus manifest.
- `block.transactions[0].has_valid_coinbase_transaction_inputs()` is `true`;
  all other transactions return `false` from `has_any_coinbase_inputs()`.
- `block.transactions.len()` matches the corpus expected count.
- The Merkle root computed from `Transaction::hash()` values matches the
  expected root from the corpus.
- Re-serialized length matches the corpus expected byte length.
- Deserializing a byte vector of length `MAX_BLOCK_BYTES + 1 = 2_000_001`
  returns a serialization error (the size cap is enforced).

`MAX_BLOCK_BYTES` is exported at `zebra_chain::block::MAX_BLOCK_BYTES` and
equals `2_000_000usize`.

`Transaction` in this codebase does **not** have an `is_coinbase()` method.
Use `has_valid_coinbase_transaction_inputs()` (returns `true` for the first
transaction with exactly one coinbase input) and `has_any_coinbase_inputs()`
(returns `true` for any transaction with a coinbase input).

The fixture directory is the path provided in the `HBV_FIXTURES` environment
variable at test run time. Do not use `include_str!` or a compile-time path;
read block files with `std::fs::read_to_string` at runtime.

## 9. Negative space — what this milestone does not ask for

- **Runtime anchor validation against a live state service.** The driver
  verifies only the variant names and their constructability via `From`-based
  paths. The state service call that actually raises `UnknownSproutAnchor` is
  crate-internal and is not exercised by the tests.
- **JoinSplit proof verification.** `Groth16Parameters::directory()` and
  `failure_hint()` are the extent of the surface in scope. Proof verification
  correctness is not checked; the static `GROTH16_PARAMETERS` is not accessed
  by any driver.
- **Block subsidy arithmetic.** `SubsidyError::InvalidMinerFees` and
  `SumOverflow` are tested only as named variants with correct `Display` text;
  the numerical correctness of subsidy or funding-stream calculations is not
  checked here (inherited from step 12).
- **Sapling/Orchard proof verification.** Only Groth16 surface is new here;
  Halo2 verification was step 12's concern and is unchanged.
- **The `CoinbaseExpiryBlockHeight` rule end-to-end.** The function
  `coinbase_expiry_height` is tested with synthetic transactions; only the
  pre-NU5 path (expiry must be at most `MAX_EXPIRY_HEIGHT`) is exercised by
  the driver, because NU5 has no activation height in this oracle tree.
- Everything already out of scope per `REQUIREMENTS.md`'s
  "Explicit out-of-scope decisions" list remains out of scope.

## 10. Handoff

**Inherited from steps 1–12**: the V1–V5 transaction codec, Orchard and
Sapling shielded data, nullifier indexing, ZIP-244 sighash, the mempool, peer
management, Halo2 proof verification, block subsidy and funding streams, and
the 17-block regression corpus.

**What step 13 leaves for step 14**: a `ValidateContextError` with named
anchor variants, the four lock-time/expiry check functions at their documented
signatures, `Height::MAX_EXPIRY_HEIGHT = Height(499_999_999)`, the miner-fee
error variants in `BlockError` and `SubsidyError`, and the `Groth16Parameters`
path/hint surface. Step 14 (`REQUIREMENTS.md`: JSON-RPC server, Sapling/Orchard
trees in state, ZIP-212 grace-period checkpointing) is expected to find all of
the above unchanged in its public shape.
