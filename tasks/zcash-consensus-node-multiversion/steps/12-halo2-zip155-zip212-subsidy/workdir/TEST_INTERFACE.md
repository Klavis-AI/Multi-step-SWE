# TEST_INTERFACE — Step 12: Halo2 / ZIP-155 / ZIP-212 / Subsidy

This document is the contract between the tests and the candidate's
implementation. Every type, method, field, constant, and error variant the
drivers exercise is named here. The candidate must never have to guess
a name, module path, or value.

---

## §1 Groups

| Group | Kind | Crate |
| --- | --- | --- |
| build | gate | @build |
| workspace-checks | gate | @build |
| sprout-note-commitment-tree | requirement | zebra-chain |
| zip212-coinbase-decryptable | requirement | zebra-consensus |
| orchard-flags-rule | requirement | zebra-consensus |
| subsidy-error-variants | requirement | zebra-consensus |
| inherited-block-codec | gate | zebra-chain |
| inherited-milestones | gate | zebra-chain |
| workspace-handoff | gate | zebra-chain |

The gate groups are all-or-nothing vetoes: the step is not satisfied if any
one of them fails, whatever else passes.

The groups track where behaviour is actually observed from an integration
test, not where the milestone is largest. `zip212-coinbase-decryptable`
and `orchard-flags-rule` each hold one capability: capability 3 (ZIP-212
coinbase decryptability) and capability 4 (the Orchard flags rule) are
independent features that fail independently, so they are separate groups.
`sprout-note-commitment-tree` runs a real incremental Merkle tree against
pinned vectors. `subsidy-error-variants` is deliberately the smallest: the
subsidy and funding-stream arithmetic lives in a private module and cannot be
called from a test, so that group can only assert that the error types it
raises exist and read correctly. §8 records that limitation rather than
treating it as behaviour.

Capabilities 3, 4 and 6 therefore account for most of what the tests observe,
and capability 5 for the rest, as `instruction.md` states.

There is no separate `orchard-flags-limits` group. Capability 4's predicate has
three branches — pre-V5, V5 with no Orchard actions, and V5 with actions but no
enable bit — and only the third can fail. Building a V5 transaction that carries
an Orchard action requires `zebra-chain`'s `proptest-impl` feature, which is
enabled for `zebra-consensus`'s tests and not for `zebra-chain`'s, so all three
branches are asserted in `orchard-flags-rule` next to the `zebra-consensus`
wrapper that must agree with them. The `Flags` wire format, which is
byte-identical to app-11, is guarded in the `inherited-milestones`
gate instead of being treated as new work.

The two `@build` rows are not test drivers. Each is a cargo invocation that must
exit zero; each is all-or-nothing:

| Group | Command run in the workspace root |
|---|---|
| `build` | `cargo build -p zebra-chain --tests` |
| `workspace-checks` | `cargo check --workspace --all-targets --all-features` |

`workspace-checks` is by far the stricter of the two. It is a gate group, so
it adds no requirement of its own; failing it fails the step
regardless of the rest of the work. Three flags each widen it beyond a default
build:

- `--workspace` compiles every member crate, not just `zebra-chain`. A change
  that breaks `zebra-consensus`, `zebra-state`, `zebra-network`, `zebra-rpc` or
  `zebrad` fails here even if no driver touches that crate.
- `--all-targets` adds benches and examples to the usual lib/bin/test set.
- `--all-features` turns on every optional feature of every member
  simultaneously. For `zebra-chain` that means `proptest-impl`, which is
  `default = []` and therefore off in an ordinary build; enabling it pulls in
  `proptest`, `proptest-derive`, `itertools`, `zebra-test`, `rand` and
  `rand_chacha`, and activates every
  `#[cfg_attr(any(test, feature = "proptest-impl"), derive(Arbitrary))]` in the
  crate. A new or changed type that does not satisfy `Arbitrary`'s bounds
  compiles fine by default and fails here.

Code that builds under `cargo build` but breaks a feature-gated module still
fails this gate — and a failed gate fails the whole step.

---

### How this step is checked

The gate groups are `build`, `workspace-checks`, `inherited-block-codec`,
`inherited-milestones` and `workspace-handoff`. They add no new requirement
and only veto: if any one of them fails, the step is not satisfied no matter
how much of the new work you completed.

All three driver-backed gate groups are satisfied by the step-11 tree
exactly as you inherited it — they have been run against it — so they can
only fail if this step's work broke something that already worked, or
deleted something you were handed.

The requirement groups carry this step's new capability and its peripheral
breadth; every case in them is in scope.

The practical consequence: preserving the inherited behaviour the gate
checks is necessary but adds nothing by itself, and doing nothing else
leaves all of the new work undone.

## §2 `sprout-note-commitment-tree` — zebra-chain

### Module path

`zebra_chain::sprout::tree`

### Types

```rust
// Root is the Sprout note commitment tree root hash.
#[derive(Clone, Copy, Default, Eq, PartialEq, Serialize, Deserialize, Hash)]
pub struct Root([u8; 32]);

impl From<[u8; 32]> for Root { ... }
impl From<Root>    for [u8; 32] { ... }
impl From<&[u8; 32]> for Root { ... }
impl From<&Root>   for [u8; 32] { ... }
// Root has a hand-written Debug (not the derive) that hex-encodes its bytes
// inside the tuple-struct wrapper, i.e. it renders exactly as
//     Root("d7c612c817793191a1e68652121876d6b3bde40f4fa52bc314145ce6e5cdd259")
// -- the type name, a parenthesis, the 32 bytes as 64 lowercase hex digits in
// the same order `From<Root> for [u8; 32]` yields them, double-quoted, and a
// closing parenthesis. `f.debug_tuple("Root").field(&hex::encode(&self.0))`
// produces it. A driver asserts the prefix, the suffix and the hex body.

// NoteCommitmentTree is an incremental Merkle tree backed by
// incrementalmerkletree::bridgetree::Frontier with depth 29.
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct NoteCommitmentTree { /* private fields */ }
// NoteCommitmentTree also has manual `impl Eq` and `impl PartialEq`.

impl Default for NoteCommitmentTree { ... }

impl NoteCommitmentTree {
    /// Append a note commitment leaf. Returns Err if the tree is full.
    pub fn append(&mut self, cm: NoteCommitment) -> Result<(), NoteCommitmentTreeError>;

    /// Returns the current root hash.
    pub fn root(&self) -> Root;

    /// Returns the raw 32-byte root hash (same bytes as root()).
    pub fn hash(&self) -> [u8; 32];

    /// Returns the all-zeros 32-byte uncommitted leaf value.
    pub fn uncommitted() -> [u8; 32];

    /// Returns the count of appended leaf commitments (NEW in app-12).
    pub fn count(&self) -> u64;
}

// Error type for full-tree condition.
#[derive(Error, Debug, Clone, PartialEq, Eq)]
pub enum NoteCommitmentTreeError {
    #[error("the note commitment tree is full")]
    FullTree,
}
```

### NoteCommitment

```rust
// In zebra_chain::sprout::commitment
#[derive(Clone, Copy, Debug, Deserialize, PartialEq, Serialize)]
pub struct NoteCommitment(/* pub(crate) [u8; 32] */);

impl From<[u8; 32]> for NoteCommitment { ... }
impl From<NoteCommitment> for [u8; 32] { ... }
```

`NoteCommitment` is constructed from `[u8; 32]` via `From`. Its inner bytes
are `pub(crate)` and not directly accessible from integration tests.

### Constants

`pub(super) const MERKLE_DEPTH: usize = 29` — depth of the Sprout commitment
tree. Not accessible from integration tests; present for documentation.

### Test vectors

The empty tree root (all-zeros leaf propagated to depth 0) is:

```
d7c612c817793191a1e68652121876d6b3bde40f4fa52bc314145ce6e5cdd259
```

After appending the first four commitments from the `COMMITMENTS` test vector
(`zebra-chain/src/sprout/tests/test_vectors.rs`), the roots must match
`ROOTS[0..4]` from the same file:

| After N leaves | Root (hex) |
|---|---|
| 1 | `b8e10b6c157be92c43a733e2c9bddb963a2fb9ea80ebcb307acdcc5fc89f1656` |
| 2 | `83a7754b8240699dd1b63bf70cf70db28ffeb74ef87ce2f4dd32c28ae5009f4f` |
| 3 | `c45297124f50dcd3f78eed017afd1e30764cd74cdf0a57751978270fd0721359` |
| 4 | `b61f588fcba9cea79e94376adae1c49583f716d2f20367141f1369a235b95c98` |

Commitments used (bytes as hex, from `COMMITMENTS` test vector):

```
62fdad9bfbf17c38ea626a9c9b8af8a748e6b4367c8494caf0ca592999e8b6ba
68eb35bc5e1ddb80a761718e63a1ecf4d4977ae22cc19fa732b85515b2a4c943
836045484077cf6390184ea7cd48b460e2d0f22b2293b69633bb152314a692fb
92498a8295ea36d593eaee7cb8b55be3a3e37b8185d3807693184054cd574ae4
```

### What changed from app-11

App-11's whole `impl NoteCommitmentTree` was two methods, `root` and `append`.
Four of the five methods this group calls are new or changed:

| Method | app-11 | app-12 |
|---|---|---|
| `append` | `pub fn append(&mut self, _cm: &NoteCommitment)` — takes the commitment **by reference**, returns `()`, body is `todo!("implement sprout note commitment trees #2485")`, so calling it panics rather than doing nothing | `pub fn append(&mut self, cm: NoteCommitment) -> Result<(), NoteCommitmentTreeError>` — takes it **by value**, returns a `Result`, and drives a real `incrementalmerkletree::bridgetree::Frontier` |
| `root` | present | present; now reads the live frontier instead of a fixed empty root |
| `hash` | **absent** | `pub fn hash(&self) -> [u8; 32]` — the raw root bytes |
| `uncommitted` | **absent** | `pub fn uncommitted() -> [u8; 32]` — the all-zeros empty-leaf value, and the seed of the `EMPTY_ROOTS` table |
| `count` | **absent** | `pub fn count(&self) -> u64` |

`hash` and `uncommitted` are as new at this tag as `count` is, and the group
calls all three: `hash_and_root_agree`, `empty_tree_root_is_uncommitted` and
`uncommitted_leaf_and_root_hex_debug` would not compile against app-11 even if
`append`'s signature had not changed. A driver targeting app-11 does not
compile, for four independent reasons.

Two internals also changed and are not directly observable: `MERKLE_DEPTH`
became `pub(super)` (it was private), and `EMPTY_ROOTS` was re-indexed so that
index 0 is the root rather than the leaf, with `uncommitted()` as its seed.

---

## §3 Orchard flags surface — zebra-chain (not a group of its own)

This section specifies the `zebra-chain` side of capability 4. It is **not** a
group in its own right. Nothing in it can be exercised in both directions from
`zebra-chain/tests/`: building a V5 transaction that carries an Orchard action
needs `zebra-chain`'s `proptest-impl` feature, which is enabled for
`zebra-consensus`'s test build and not for `zebra-chain`'s. So the behaviour
specified here is asserted from two other groups:

| Surface | Asserted by | Group |
|---|---|---|
| `Transaction::has_enough_orchard_flags`, all three branches | §4's driver, alongside the `zebra-consensus` wrapper that must agree with it | `orchard-flags-rule` (requirement) |
| `ACTION_SIZE`, `SPEND_AUTH_SIG_SIZE`, `AUTHORIZED_ACTION_SIZE`, the `Flags` bit values, and the `Flags` wire format in both directions | §7's driver | `inherited-milestones` (gate) |

Implement everything here; none of it is optional. The split only decides where
it is observed from.

### Module paths

`zebra_chain::orchard::shielded_data` — constants and `Flags`
`zebra_chain::transaction` — `Transaction::has_enough_orchard_flags`

### Constants

These three have been `pub` since step 04 and are unchanged here, so they are
**not** new work. They are asserted by the `inherited-milestones` gate
group (§7); they are specified here because the rest of this section is
meaningless without them.

```rust
// In zebra_chain::orchard::shielded_data

/// Byte size of a single Orchard action in a V5 transaction (ZIP-225 §7.5).
pub const ACTION_SIZE: u64 = 820; // 5*32 + 580 + 80

/// Byte size of a SpendAuthSig (RedPallas signature).
pub const SPEND_AUTH_SIG_SIZE: u64 = 64;

/// Byte size of an authorized Orchard action (action + spend-auth sig).
pub const AUTHORIZED_ACTION_SIZE: u64 = 884; // ACTION_SIZE + SPEND_AUTH_SIG_SIZE
```

### Flags type

```rust
// In zebra_chain::orchard::shielded_data
// Backed by bitflags 1.x.
pub struct Flags: u8 {
    const ENABLE_SPENDS  = 0b00000001;
    const ENABLE_OUTPUTS = 0b00000010;
}
// Bits 2–7 are reserved and MUST be zero.
```

`Flags` supports `bits()`, `empty()`, `intersects()`, and all standard
`bitflags` operations. `Flags::empty()` has neither `ENABLE_SPENDS` nor
`ENABLE_OUTPUTS` set. The bit values themselves are inherited from step 04 and
are asserted by the `inherited-milestones` gate group, not new work.

The wire format is where the reserved-bit rule is enforced. It is **also**
inherited and also out of scope: diffing
`zebra-chain/src/orchard/shielded_data.rs` between app-11 and app-12 shows the
only change anywhere in that file is `Action::max_allocation` hoisting its
expression into a function-local `const MAX` and adding a `const_assert!`. Both
`impl`s below are byte-identical at app-11, so a candidate who changed nothing
in this file already satisfies them. They are specified because the milestone
depends on them and a regression in either is a veto:

```rust
// In zebra_chain::orchard::shielded_data, both impls public.

impl ZcashSerialize for Flags {
    // writes exactly one byte: self.bits()
}

impl ZcashDeserialize for Flags {
    fn zcash_deserialize<R: io::Read>(reader: R) -> Result<Self, SerializationError> {
        // reads one byte; a byte with any of bits 2-7 set is REJECTED, not
        // masked:
        //   Flags::from_bits(byte)
        //       .ok_or(SerializationError::Parse("invalid reserved orchard flags"))
    }
}
```

Both are reached through `zebra_chain::serialization::{ZcashSerialize,
ZcashDeserialize}`, whose `zcash_deserialize` takes anything implementing
`std::io::Read` — a driver passes `&[byte][..]`.

The `inherited-milestones` gate group (§7) drives this pair:

- `0x00`, `0x01`, `0x02` and `0x03` must each deserialize to the corresponding
  `Flags` value, report that byte back from `bits()`, and serialize to exactly
  that one byte.
- `0b0000_0100`, `0b0000_1000`, `0b1000_0000` and `0xff` must each return
  `Err(SerializationError::Parse(_))`. The variant is matched by name; the
  message text is not asserted. Writing this as `Flags::from_bits_truncate`
  returns `Ok` for every input and fails every case.

`SerializationError` is the enum already specified in §6; only its `Parse`
variant is used here.

### Transaction method

```rust
// In zebra_chain::transaction::Transaction
/// Returns true if the orchard flags rule is satisfied.
///
/// The rule: if effectiveVersion >= 5 and nActionsOrchard > 0, at least one
/// of enableSpendsOrchard and enableOutputsOrchard MUST be 1.
/// For pre-V5 transactions or V5 with no Orchard actions, always returns true.
pub fn has_enough_orchard_flags(&self) -> bool;
```

This method is new in app-12 and absent in app-11. The function pointer type
`fn(&Transaction) -> bool` is the expected signature, and §4's driver pins it
with a never-called `let _f: fn(&Transaction) -> bool =
Transaction::has_enough_orchard_flags;`. Rust type-checks that body at
definition time, so a rename, a change of receiver, or a different return type
is a compile error that fails every test in `orchard-flags-rule` rather than
silently passing them.

All three of its branches are asserted, in §4:

| Branch | Input | Expected |
|---|---|---|
| `version() < 5` | the mainnet genesis coinbase, read from the corpus | `true` |
| V5, `orchard_actions().count() == 0` | a V5 struct literal with `orchard_shielded_data: None` | `true` |
| V5, one Orchard action, `Flags::empty()` | `insert_fake_orchard_shielded_data` | `false` |
| V5, one Orchard action, either enable bit alone | same, with `ENABLE_SPENDS` or `ENABLE_OUTPUTS` | `true` |

The last two need `zebra_chain::transaction::arbitrary`, which is why all four
live in `zebra-consensus`'s test build rather than `zebra-chain`'s.

### Supporting surface the behavioural tests need

None of the following is new at step 12, but all of it must keep working:

```rust
// ── zebra_chain::transaction::Transaction ─────────────────────────────────
pub fn version(&self) -> u32;
pub fn orchard_actions(&self) -> impl Iterator<Item = &orchard::Action>;
pub fn orchard_flags(&self) -> Option<orchard::shielded_data::Flags>;

// The V5 struct variant, constructed field-by-field by the second test.
// Every field is public, so no builder is required.
pub enum Transaction {
    // ... V1 .. V4 ...
    V5 {
        network_upgrade: NetworkUpgrade,
        lock_time: LockTime,
        expiry_height: block::Height,
        inputs: Vec<transparent::Input>,
        outputs: Vec<transparent::Output>,
        sapling_shielded_data: Option<sapling::ShieldedData<sapling::SharedAnchor>>,
        orchard_shielded_data: Option<orchard::ShieldedData>,
    },
}

// ── zebra_chain::parameters ───────────────────────────────────────────────
pub enum NetworkUpgrade { /* ... */ Nu5 /* ... */ }

// ── zebra_chain::transaction::LockTime ────────────────────────────────────
pub enum LockTime {
    Height(block::Height),
    Time(chrono::DateTime<chrono::Utc>),
}

// ── zebra_chain::block ────────────────────────────────────────────────────
pub struct Height(pub u32);
pub struct Block { pub transactions: Vec<std::sync::Arc<Transaction>>, /* ... */ }
impl ZcashDeserialize for Block { /* ... */ }   // zebra_chain::serialization
```

Note that `LockTime` has **no** `unlocked()` constructor at this milestone —
that is a step-13 addition — so the V5 fixture is written
`lock_time: LockTime::Height(Height(0))`.

The pre-V5 input is read from the shipped corpus rather than constructed:
`HBV_FIXTURES/blocks/main-0000000.hex` is hex-decoded with `hex::decode` and
parsed with `Block::zcash_deserialize`, and `block.transactions[0]` is the
mainnet genesis coinbase. `hex` is a direct `[dependencies]` entry of
`zebra-chain`, so it is in scope for this integration test; see §6.

### Static assertion (not directly testable)

`shielded_data.rs` emits a `static_assertions::const_assert!(MAX < (1 << 16))`
where `MAX = (MAX_BLOCK_BYTES - 1) / AUTHORIZED_ACTION_SIZE`. This guarantees
that the maximum number of Orchard actions in any valid block fits in a `u16`.
It fires at compile time; it cannot fail at runtime. Write it — it is part of
the milestone — but be aware that no test in this group can observe it. §8
records why.

---

## §4 `zip212-coinbase-decryptable` and `orchard-flags-rule` — zebra-consensus

These are two requirement groups in the same crate, each holding one
capability: `zip212-coinbase-decryptable` (capability 3) and
`orchard-flags-rule` (capability 4). They are split because the two
capabilities are implemented in different functions and fail independently:
a candidate whose ZIP-212 work is correct can still fail the flags predicate
by returning `true` in the wrong branch. Both groups live in
`zebra-consensus/tests/` for the same reason -- building a V5 transaction
carrying an Orchard action needs `zebra-chain`'s `proptest-impl` feature,
which only `zebra-consensus`'s tests enable.

### Module paths

`zebra_consensus::error` — error types
`zebra_consensus::transaction::check` — check functions
`zebra_chain::block` — `Block`, `Height`, `Hash`
`zebra_chain::parameters` — `Network`, `NetworkUpgrade`
`zebra_chain::orchard::shielded_data` — `Flags` (see §3)
`zebra_chain::transaction::arbitrary` — `insert_fake_orchard_shielded_data`

### Block primitives from `zebra-chain`

The check-function signatures below name three `zebra-chain` types, and the
driver builds values of two of them directly. None is new at this step; they
are spelled out because this group's interface is unusable without them:

```rust
// zebra_chain::block

// (derives: Copy, Clone, Debug, Eq, PartialEq, PartialOrd, Ord, Hash,
//           Serialize, Deserialize)
pub struct Height(pub u32);      // the driver writes `Height(123_456)`

// (derives: Copy, Clone, Eq, PartialEq, Hash, Serialize, Deserialize --
//  note: NOT Debug. `Debug` and `Display` are hand-written impls that print
//  the big-endian hex form, and `TransactionError`/`BlockError` rely on the
//  `Debug` one through their `{hash:?}` format arguments.)
pub struct Hash(pub [u8; 32]);   // the driver writes `Hash([0u8; 32])`

// zebra_chain::parameters
// (derives: Copy, Clone, Debug, Eq, PartialEq, Hash, Serialize, Deserialize)
pub enum Network {
    Mainnet,
    Testnet,
}
```

`Height` and `Hash` are tuple structs whose single field is `pub`, so a test can
construct one with no constructor function and read it back as `.0`. Making
either field private, or replacing either newtype with a bare `u32`/`[u8; 32]`,
breaks this group at compile time.

### New error variants

```rust
// In zebra_consensus::error::TransactionError
// (derives: Error, Clone, Debug, PartialEq, Eq)
#[error("coinbase transaction Sapling or Orchard outputs MUST be decryptable \
         with an all-zero outgoing viewing key")]
CoinbaseOutputsNotDecryptable,

#[error("must have at least one active orchard flag")]
NotEnoughFlags,
```

```rust
// In zebra_consensus::error::BlockError
// (derives: Error, Clone, Debug, PartialEq, Eq)
#[error("block {height:?} {hash:?} has {legacy_sigop_count} legacy transparent \
         signature operations, but the limit is {MAX_BLOCK_SIGOPS}")]
TooManyTransparentSignatureOperations {
    height: zebra_chain::block::Height,
    hash: zebra_chain::block::Hash,
    legacy_sigop_count: u64,
},
```

`MAX_BLOCK_SIGOPS = 20_000` (defined in `zebra_consensus::block` which is
private; it appears in the error message but is not directly importable from
integration tests).

### Check functions (module `zebra_consensus::transaction::check`)

```rust
/// Checks that the transaction has at least one Orchard flag when it
/// has Orchard actions (V5+ only).
pub fn has_enough_orchard_flags(tx: &Transaction) -> Result<(), TransactionError>;

/// Checks that a transaction's Sapling and Orchard outputs are decryptable
/// with the all-zeros ovk (ZIP-212, Heartwood and later).
///
/// The consensus rule is about coinbase transactions, but this function does
/// NOT test coinbase-ness: its caller in `zebra_consensus::block` is what
/// applies it to the coinbase only. Its one height condition is Heartwood.
pub fn coinbase_outputs_are_decryptable(
    transaction: &Transaction,
    network: Network,
    height: Height,
) -> Result<(), TransactionError>;
```

`coinbase_outputs_are_decryptable` has exactly two branches and no third:

```text
if height < Heartwood.activation_height(network)  -> Ok(())
else if !decrypts_successfully(tx, network, height) -> Err(CoinbaseOutputsNotDecryptable)
else -> Ok(())
```

**Do not add an `is_coinbase()` guard.** It is not merely redundant, it is
wrong: the function is expected to return `Err` for *any* Heartwood-or-later
transaction whose shielded outputs do not decrypt under the all-zero outgoing
viewing key, coinbase or not, and the rejection test below passes it a
transaction that is not a coinbase. A guard that short-circuits to `Ok(())` for
non-coinbase input turns that test green-to-red and is the single most likely
way to fail this group while believing the rule was implemented.

`decrypts_successfully` is `zebra_chain::primitives::zcash_note_encryption::
decrypts_successfully(tx, network, height) -> bool`, which routes Sapling
outputs through librustzcash's `try_sapling_output_recovery` and Orchard
actions through `try_output_recovery_with_ovk`, both with an all-zero ovk and
both keyed on the network upgrade in force at `height`. That height argument is
what makes the Canopy `0x02` note-plaintext epoch observable: pass a constant
instead of the block's own height and one of the two acceptance tests below
fails.

`check` is declared `pub mod check` inside `zebra_consensus::transaction`,
so the full path is `zebra_consensus::transaction::check::has_enough_orchard_flags`.

Both functions are **called**, and both their accepting and their rejecting
branches are asserted. The signature above is pinned by the call sites
themselves: a different arity, receiver or return type is a compile error that
fails the whole group.

### Supporting surface these behavioural tests need

Almost none of the following is new at step 12: it existed at step 11 and must
keep working. The one exception is marked inline — `has_enough_orchard_flags` is
**added by this milestone** (§3), even though it is listed here beside the
inherited accessors it sits with.

```rust
// ── zebra_chain::transaction::arbitrary ───────────────────────────────────
//
// Declared in zebra-chain/src/transaction.rs as
//     #[cfg(any(test, feature = "proptest-impl"))]
//     pub mod arbitrary;
//
// zebra-consensus's [dev-dependencies] carry
//     zebra-chain = { path = "../zebra-chain", features = ["proptest-impl"] }
// so this module IS in scope for an integration test in zebra-consensus/tests/
// (and is NOT in scope for one in zebra-chain/tests/ -- see §3).

/// Attaches one dummy Orchard action to a V5 transaction and returns a mutable
/// reference to the inserted shielded data, so the caller can set `flags`.
/// The dummy action is `orchard::Action::zcash_deserialize(&[0u8; 820][..])`
/// paired with an all-zero `SpendAuthSig`; `flags` starts as `Flags::empty()`,
/// `proof` is an empty `Halo2Proof`, `shared_anchor` is
/// `orchard::tree::Root::default()`. Panics if the transaction is not V5.
pub fn insert_fake_orchard_shielded_data(
    transaction: &mut Transaction,
) -> &mut orchard::ShieldedData;

// ── zebra_chain::orchard::ShieldedData ────────────────────────────────────
// `flags` is a public field, so the driver writes `shielded_data.flags = ...`.
pub struct ShieldedData { pub flags: Flags, /* ... */ }

// ── zebra_chain::transaction::Transaction ─────────────────────────────────
pub fn orchard_actions(&self) -> impl Iterator<Item = &orchard::Action>;
pub fn orchard_flags(&self) -> Option<orchard::shielded_data::Flags>;
pub fn sapling_outputs(&self) -> Box<dyn Iterator<Item = &sapling::Output> + '_>;
pub fn has_enough_orchard_flags(&self) -> bool;   // NEW at step 12 — see §3

// ── zebra_chain::parameters::NetworkUpgrade ───────────────────────────────
pub fn activation_height(&self, network: Network) -> Option<block::Height>;
```

`NetworkUpgrade::Nu5.activation_height(Network::Testnet)` must be `Some(_)` at
this tag. NU5 has **no mainnet activation height yet** — `network_upgrade.rs`
carries a `// TODO` there — so the ZIP-212 rejection test runs on **Testnet**
and reads the height from `activation_height` instead of hard-coding it.

### What the behavioural tests assert

The `*_variant` tests in `zip212-coinbase-decryptable` and `orchard-flags-rule`
are the `Display` assertions on the error variants above; the rows below are
the behavioural ones.

The four flags-rule rows go to `orchard-flags-rule` (with its
`not_enough_flags_variant` test); the three coinbase-decryptability rows go
to `zip212-coinbase-decryptable` (with its `coinbase_outputs_not_decryptable_
variant` test).

| Test | Input | Expected |
|---|---|---|
| `has_enough_orchard_flags_true_for_pre_v5_transaction` | the mainnet genesis coinbase from `HBV_FIXTURES/blocks/main-0000000.hex` | `true` / `Ok(())`. This is the `version() < 5` early return; drop it and the fall-through reads `Flags::empty()`, whose `intersects` is `false` |
| `has_enough_orchard_flags_true_for_v5_without_orchard_actions` | a V5 struct literal with `orchard_shielded_data: None` | `true` / `Ok(())`. This is the `orchard_actions().count() == 0` early return; an implementation that asks "are the enable bits set?" returns `false` here, because `orchard_flags()` is `None` with no shielded data |
| `flags_rule_rejects_orchard_actions_with_no_enable_bits` | V5 + one dummy Orchard action, `flags = Flags::empty()` | `has_enough_orchard_flags()` is `false`; `check::has_enough_orchard_flags` is `Err(TransactionError::NotEnoughFlags)` |
| `flags_rule_accepts_either_enable_bit_alone` | the same, with `ENABLE_SPENDS` alone, then `ENABLE_OUTPUTS` alone | both `true` / `Ok(())`. ZIP-225 requires *at least one* bit, so an implementation using bitflags' `contains(A \| B)` — which means "all of" — fails here |
| `coinbase_outputs_are_decryptable_accepts_conforming_coinbases` | the coinbase of `HBV_FIXTURES/blocks/main-0982681.hex` at `Height(982_681)` on Mainnet (above Heartwood `903_000`, below Canopy `1_046_400`, one Sapling output), and the genesis coinbase at `Height(0)` (below Heartwood) | `Ok(())` for both |
| `coinbase_outputs_are_decryptable_accepts_a_post_canopy_testnet_coinbase` | the coinbase of `HBV_FIXTURES/blocks/test-1101629.hex` at `Height(1_101_629)` on Testnet (above testnet Canopy `1_028_500`, one Sapling output) | `Ok(())` |
| `coinbase_outputs_are_decryptable_rejects_undecryptable_orchard_output` | V5 + one dummy Orchard action (all-zero note ciphertext), both enable bits set, on Testnet at the NU5 activation height | `Err(TransactionError::CoinbaseOutputsNotDecryptable)` |

The last row is what makes the two acceptance rows non-vacuous: a
`coinbase_outputs_are_decryptable` that returns `Ok(())` unconditionally passes
both acceptance tests and fails the rejection test, and the reverse holds for
one that always errors.

The two acceptance rows straddle the Canopy note-plaintext boundary on purpose.
`main-0982681` is Heartwood-era but pre-Canopy, so its coinbase note plaintext
has lead byte `0x01`; `test-1101629` is post-Canopy, so its lead byte is `0x02`.
Only the block's own height distinguishes them, so an implementation that hands
the note-decryption bridge a fixed height passes one and fails the other. Note
that the rejection direction of the Canopy rule is deliberately *not* asserted:
the reference's own comment records that librustzcash still accepts a `0x01`
lead byte during the ZIP-212 grace period, so asserting it would pin a
dependency's behaviour rather than this milestone's.

Both acceptance tests assert `coinbase.sapling_outputs().count() == 1` before
calling, so a change that left a corpus block without a shielded output would
fail loudly rather than quietly reduce the test to a transparent-only no-op,
and each asserts its height's position relative to the relevant activation
height so the epoch claim is self-checking.

### Crate dependencies and fixtures this group relies on

**You must add `hex = "0.4.3"` to `zebra-consensus`'s `[dev-dependencies]`.**
It is new there at this tag, and `instruction.md`'s Dependencies bullet names it
for that reason; the row is in the table below. It is a dev-dependency rather
than a direct dependency here, unlike `zebra-chain`'s — see §6 — because only
integration tests use it. Integration tests link against dev-dependencies, so
the driver calls `hex::decode` to read the fixture blocks, and without the entry
this group fails to compile. The version needs no lockfile movement: `0.4.3` is
already pinned in the inherited `Cargo.lock` through `zebra-chain`.

This group is also where the milestone's dependency migration becomes
load-bearing, so the manifest entries it needs are named here rather than left
to be discovered. `WORKSPACE_HANDOFF.md`'s "do not add a third-party
dependency, do not bump past the lockfile" rule says in as many words that it is
overridden when this milestone's own documents name a version — these are those
versions, and they are required:

| Crate | Manifest | Version | Already in the inherited `Cargo.lock`? |
|---|---|---|---|
| `orchard` | `zebra-chain` | `"0.0"` | yes, `0.0.0`, as a transitive dependency |
| `static_assertions` | `zebra-chain` | `"1.1.0"` | yes, `1.1.0` |
| `zcash_note_encryption` | `zebra-chain` | git `https://github.com/zcash/librustzcash.git`, rev `53d0a51d33a421cb76d3e3124d1e4c1c9036068e` | yes, `0.0.0` |
| `halo2` | `zebra-consensus` | `"=0.1.0-beta.1"` | yes, `0.1.0-beta.1` |
| `orchard` | `zebra-consensus` | `"0.0.0"` | yes, `0.0.0` |
| `aes` | `zebra-chain` | `0.7.5` (from `0.6`) | yes, both versions |
| `bls12_381` | both | `0.6.0` (from `0.5.0`) | yes, both versions |
| `jubjub` | both | `0.8.0` (from `0.7.0`) | yes, both versions |
| `fpe` | `zebra-chain` | `0.5.1` (from `0.4`) | **no** — the lock pins `0.4.0` and `0.5.0` |
| `x25519-dalek` | `zebra-chain` | `1.2.0` (from `1.1`) | **no** — the lock pins `1.1.1` |
| `bellman` | `zebra-consensus` | `0.11.1` (from `0.10.0`) | **no** |
| `metrics` | `zebra-consensus` | `0.17.0` (from `0.13.0-alpha.8`) | **no** |
| `tokio` | `zebra-consensus` | `1.14.0` (from `0.3.6`) | **no** — the lock's newest `tokio` is `1.3.0` |
| `tower` | `zebra-consensus` | `0.4.9` (from `0.4`) | **no** — the lock's `tower` is a git-sourced `0.4.0`; this requirement moves it to the registry `0.4.10` the pinned graph uses |
| `hex` | `zebra-consensus` `[dev-dependencies]` | `"0.4.3"` | yes — `0.4.3` already in `Cargo.lock` via `zebra-chain`; add it as an explicit `[dev-dependencies]` entry |

`zcash_note_encryption` is what `decrypts_successfully` is built on, so this
group cannot pass without it. The five rows marked **no** move `Cargo.lock`;
that is expected here and is what the override clause is for. The verifier
image's cargo cache is populated from the graph these versions resolve to, so a
workspace that matches them builds with `--offline --locked`; one that does not
falls further down the verifier's resolution ladder, which is recorded but not
penalised.

Blocks are read from the same `HBV_FIXTURES` corpus described in §6:
`blocks/main-0982681.hex`, `blocks/main-0000000.hex` and
`blocks/test-1101629.hex`.

---

## §5 `subsidy-error-variants` — zebra-consensus

### SubsidyError

```rust
// In zebra_consensus::error
// (derives: Error, Copy, Clone, Debug, PartialEq, Eq)
pub enum SubsidyError {
    #[error("no coinbase transaction in block")]
    NoCoinbase,

    #[error("founders reward output not found")]
    FoundersRewardNotFound,

    /// NEW at app-12.
    #[error("funding stream expected output not found")]
    FundingStreamNotFound,
}
```

`FundingStreamNotFound` is entirely absent in app-11. Its presence is the
negative direction for this group.

### BlockError (sigop limit)

```rust
// In zebra_consensus::error
// (derives: Error, Clone, Debug, PartialEq, Eq)
pub enum BlockError {
    // ... pre-existing variants ...
    #[error("block {height:?} {hash:?} has {legacy_sigop_count} legacy transparent \
             signature operations, but the limit is {MAX_BLOCK_SIGOPS}")]
    TooManyTransparentSignatureOperations {
        height: zebra_chain::block::Height,
        hash: zebra_chain::block::Hash,
        legacy_sigop_count: u64,
    },
}
```

The elision is deliberate and it is a **promise**: the driver matches only the
variant shown. It does not pattern-match `BlockError` exhaustively, so the
pre-existing variants — whatever your step-01-to-11 work produced — are not
part of this step's contract and cannot cost you this group. Only
`TooManyTransparentSignatureOperations` is pinned, by being constructed and
formatted.

(`SubsidyError` above *is* matched exhaustively and wildcard-free, so adding a
fourth variant to it is a compile error. That is fair because its body is given
here in full.)

The `legacy_sigop_count` value appears in the Display output. Tests in
this group construct this variant directly: `too_many_sigops_display_includes_
count` uses `legacy_sigop_count: 99_999` and asserts the string contains
`"99999"`, and `too_many_transparent_sigops_variant` uses `20_001` and asserts
the count appears either as `"20001"` or `"20_001"`. The second test moved
here from `zip212-orchard-flags` in round 7: this variant is capability 6 (the
sigops rule), not capability 3 or 4, so asserting it inside the ZIP-212 group
would make a `BlockError` regression fail the wrong group.

Constructing that variant also requires `zebra_chain::block::Height` and
`zebra_chain::block::Hash`; both are specified in §4 under *Block primitives
from `zebra-chain`*. The driver writes `height: Height(500_000)` and
`hash: Hash([0xab_u8; 32])`, which only compiles while both remain public tuple
structs over `u32` and `[u8; 32]`.

### What is out of scope

The subsidy calculation functions themselves — `funding_stream_values`,
`funding_stream_address`, `filter_outputs_by_address` — live in the private
module `zebra_consensus::block::subsidy::funding_streams` and are not
reachable from integration tests. The ZIP-214 constants
(`FUNDING_STREAM_HEIGHT_RANGES`, `FUNDING_STREAM_RECEIVER_NUMERATORS`,
`FUNDING_STREAM_RECEIVER_DENOMINATOR`) are likewise in the private
`parameters::subsidy` module. These are documented here as negative space;
no test checks them.

That is why this group is the smallest: an error type that no
reachable code path can be made to raise supports fewer assertions than a rule
a test can drive. Implement the arithmetic — `workspace-checks` compiles it and step 13
builds on it — but do not expect this group to measure it. §8 states the same
thing for each unreachable capability in one place.

---

## §6 `inherited-block-codec` — zebra-chain (regression guard)

This group guards the **step-01 block codec**, not step 11's milestone; §7
guards the later ones. **Nothing in this section is new at step 12** — every
symbol below has existed since step 01, and a tree that stopped at any later
milestone passes all its tests unchanged. The surface is spelled out in full
anyway, because a regression guard only guards what it names: while adding this
step's Halo2, ZIP-155, ZIP-212 and subsidy work a solver needs to know exactly
which block-codec surface must survive untouched.

### Types and fields

```rust
// ── zebra_chain::block ────────────────────────────────────────────────────
pub struct Block {
    pub header: Header,                                 // public field
    pub transactions: Vec<std::sync::Arc<Transaction>>, // public field
}

impl Block {
    pub fn hash(&self) -> Hash;
    pub fn coinbase_height(&self) -> Option<Height>;
}

pub struct Header {
    // ... other fields ...
    pub merkle_root: merkle::Root,                      // public field
}

// Newtypes with a public single field, so `.0` reads the inner value.
pub struct Height(pub u32);
pub struct Hash(pub [u8; 32]);   // `Display` is the human-facing hex form

// ── zebra_chain::block::merkle ────────────────────────────────────────────
pub struct Root(pub [u8; 32]);

// Root is built by *collecting* transaction hashes; there is no constructor.
impl std::iter::FromIterator<zebra_chain::transaction::Hash> for Root;

// ── zebra_chain::transaction ──────────────────────────────────────────────
impl Transaction {
    pub fn hash(&self) -> Hash;
    pub fn inputs(&self) -> &[zebra_chain::transparent::Input];
}

// ── zebra_chain::transparent ──────────────────────────────────────────────
pub enum Input {
    PrevOut { outpoint: OutPoint, unlock_script: Script, sequence: u32 },
    Coinbase {
        height: zebra_chain::block::Height,
        data: CoinbaseData,
        sequence: u32,
    },
}

// ── zebra_chain::serialization ────────────────────────────────────────────
pub trait ZcashSerialize {
    fn zcash_serialize<W: std::io::Write>(&self, writer: W) -> Result<(), std::io::Error>;
}
pub trait ZcashDeserialize: Sized {
    fn zcash_deserialize<R: std::io::Read>(reader: R) -> Result<Self, SerializationError>;
}
// Blanket trait: gives `reader.zcash_deserialize_into::<Block>()`.
pub trait ZcashDeserializeInto { /* ... */ }

pub enum SerializationError {
    Io(#[from] std::io::Error),
    // ... other variants ...
}
```

### Which test needs which symbol

| Test | Surface it would break on |
|---|---|
| `every_block_round_trips_byte_for_byte` | `Block: ZcashDeserialize + ZcashSerialize` |
| `parsing_consumes_the_whole_block_and_no_more` | `ZcashDeserializeInto` blanket impl |
| `block_hash_matches_the_corpus` | `Block::hash`, `block::Hash: Display` |
| `coinbase_height_matches_the_corpus` | `Block::coinbase_height`, `Height(pub u32)` |
| `the_first_transaction_of_every_block_is_a_coinbase` | `Block::transactions`, `Transaction::inputs`, `transparent::Input::Coinbase` |
| `the_merkle_root_is_the_root_of_the_transaction_hashes` | `Transaction::hash`, `merkle::Root: FromIterator<transaction::Hash>`, `Root(pub [u8; 32])`, `Block::header`, `Header::merkle_root` |
| `an_absurd_transaction_count_is_an_error_not_an_allocation` | `SerializationError` (any variant; the assertion is only that it is an `Err`) |
| `a_truncated_block_is_an_io_error` | `SerializationError::Io` specifically |

Two of these are stricter than they look. The merkle-root test asserts that
`block.transactions.iter().map(|tx| tx.hash()).collect::<merkle::Root>()` equals
**both** the corpus value **and** `block.header.merkle_root.0`, so the
`FromIterator<transaction::Hash>` impl and the header field must agree.
`a_truncated_block_is_an_io_error` matches `Err(SerializationError::Io(_))` by
name, so reclassifying a short read under a different variant fails it.

### Crate dependency the driver relies on

`hex` (`hex = "0.4"`) is a direct `[dependencies]` entry of `zebra-chain`, not a
dev-dependency. Integration tests link against both sets, so the driver calls
`hex::decode(&str) -> Result<Vec<u8>, FromHexError>` to turn the fixture text
into block bytes. Dropping `hex` from `zebra-chain`'s manifest fails this group
at compile time even though no `zebra-chain` source file changed.

### Fixtures

Read from the `HBV_FIXTURES` environment variable (set by `run-step.sh` to
`testsrc/step12/fixtures/`). The corpus is eighteen mainnet and testnet blocks:
the seventeen step 11 ships, plus `blocks/test-1101629.hex`, a post-Canopy
testnet block whose coinbase carries a Sapling output, which §4 needs for the
`0x02` note-plaintext epoch. The layout is `blocks/<net>-<height>.hex` plus a
`blocks.tsv` whose `height`, `block_hash`, `tx_count` and `merkle_root_hex`
columns were derived from the block bytes themselves — SHA256d over the
1487-byte header for the hash, a compactSize reader for the transaction count —
and not from any Rust implementation of this interface. This group's driver
asserts the corpus has at least eighteen rows, so a corpus that silently shrank
fails the gate rather than testing less.

---

## §7 `inherited-milestones` — zebra-chain (regression guard)

A second gate group, and like §6 it adds no requirement. It exists because
`cargo check --workspace --all-targets --all-features` proves only that the
milestones of steps 02–11 still *compile*, not that they still *work*: a
mempool cost function rewritten as `fn cost(&self) -> u64 { 0 }` type-checks,
and so does a ZIP-244 sighash bridge that returns a constant. This group makes
four of those milestones fail loudly instead: ZIP-401 mempool cost and
eviction weight (steps 10–11), the Orchard action size constants and `Flags`
wire format (step 04), ZIP-244 v5 signature hashes (step 06), and the ZIP-209
transaction value pools (step 08).

**Nothing in this section is new at step 12.** All of its tests pass
against the step-11 tree exactly as you inherited it; they have been run
against it.

### ZIP-401 mempool cost and eviction weight (steps 10–11)

```rust
// ── zebra_chain::transaction ──────────────────────────────────────────────
pub struct VerifiedUnminedTx {
    pub transaction: UnminedTx,
    pub miner_fee: Amount<NonNegative>,
}

impl VerifiedUnminedTx {
    pub fn new(transaction: UnminedTx, miner_fee: Amount<NonNegative>) -> Self;
    pub fn cost(&self) -> u64;             // takes &self
    pub fn eviction_weight(self) -> u64;   // consumes self
}

impl From<Transaction> for UnminedTx { /* ... */ }

// ── zebra_chain::amount ───────────────────────────────────────────────────
pub struct Amount<C>(/* ... */);
pub enum NonNegative {}
impl TryFrom<u64> for Amount<NonNegative> { /* ... */ }

// ── zebra_chain::transaction::LockTime ────────────────────────────────────
pub fn min_lock_time() -> LockTime;
```

The arithmetic, as implemented at this band:

    cost(tx)            = max(serialized_size_bytes, 4000)
    eviction_weight(tx) = cost(tx) + low_fee_penalty
                          low_fee_penalty = 16000 if miner_fee < 1000 zatoshis
                                                  else 0

Note that the shipped `specs/zips/zip-0401.rst` states `cost_threshold = 10000`
and `low_fee_penalty = 40000`. Those are the values of a **later** release. The
code at this band implements 4000 and 16000 with a 1000-zatoshi conventional
fee, and that is what step 11 asked for and what this guard asserts. Where the
shipped ZIP text and this document disagree, this document is normative.

The tests cover: cost of a minimal V4 and V5 transaction (the threshold),
cost of a transaction larger than the threshold (its serialized size), that
`cost` takes `&self` and can be called twice, and eviction weight at, above and
below the conventional fee for both small and large transactions, plus field
accessibility on `VerifiedUnminedTx`. Serialized sizes are computed inside the
driver with `ZcashSerialize`, never hard-coded, so the assertions state the
rule rather than a number.

### Orchard action size constants and `Flags` wire format (step 04, ZIP-225)

The tests assert `ACTION_SIZE == 820`, `SPEND_AUTH_SIG_SIZE == 64`,
`AUTHORIZED_ACTION_SIZE == 884` and that the third is the sum of the first two;
that `Flags::ENABLE_SPENDS.bits() == 0x01`, `Flags::ENABLE_OUTPUTS.bits() ==
0x02`, their union is `0x03`, and `Flags::empty()` intersects neither; that
`0x00`, `0x01`, `0x02` and `0x03` each deserialize to the matching `Flags`,
report that byte from `bits()` and serialize back to exactly that byte; and
that `0b0000_0100`, `0b0000_1000`, `0b1000_0000` and `0xff` each return
`Err(SerializationError::Parse(_))` rather than being silently masked. All are
specified in §3 and all are inherited from step 04 — the whole `Flags` serde is
byte-identical at app-11 — which is why they sit in a gate group.

### ZIP-244 v5 signature hashes (step 06)

```rust
// ── zebra_chain::transaction ──────────────────────────────────────────────
pub struct SigHash(pub [u8; 32]);
pub struct HashType;   // bitflags: ALL, NONE, SINGLE, ANYONECANPAY
impl Transaction {
    pub fn sighash(
        &self,
        network_upgrade: NetworkUpgrade,
        hash_type: HashType,
        input: Option<(u32, transparent::Output)>,
    ) -> SigHash;
    pub fn hash(&self) -> Hash;   // the txid
}
```

The cases run against two published ZIP-244 vectors from `fixtures/zip244.tsv`:

| Test | Assertion |
|---|---|
| `zip244_sighash_reproduces_all_six_variants_for_a_transparent_input_vector` | vector `zip244-06`, signing transparent input 0 with the published `script_code_hex` and `amount`, must reproduce all six published digests under `NetworkUpgrade::Nu5`: `ALL`, `NONE`, `SINGLE`, and each with `ANYONECANPAY` |
| `zip244_sighash_all_for_a_no_input_vector_matches_its_txid` | vector `zip244-03`, signing no particular input, must reproduce the published `SIGHASH_ALL`, and that digest must equal both the published `txid_hex` and `tx.hash()` |

A `sighash` that ignored `hash_type`, or that ignored the signed input's script
code and amount, reproduces at most one of the six.

**The digest those two rows encode**, restated here because this gate group
is where an inherited mistake in it is caught, and because the shipped
`zip-0244.rst` describes a *later* revision of the scheme whose "S.2" section
does not match these vectors (it lists a `hash_type` byte and all-inputs
amounts/scriptPubKeys digests that this band's digest does not have, and
orders the per-input fields value-before-script). Where the shipped ZIP text
and this document disagree, **this document is normative**:

```text
v5 sighash, signing the transparent input at `index`:
BLAKE2b-256("ZcashTxHash_" || branch_id,
            header_digest || transparent_sig_digest ||
            sapling_digest || orchard_digest)

transparent_sig_digest =
BLAKE2b-256("ZTxIdTranspaHash",
            prevout_digest || sequence_digest || outputs_digest || txin_sig_digest)
```

- `header_digest`, `sapling_digest`, `orchard_digest` — the transaction
  identifier's own.
- `prevout_digest` — `"ZTxIdPrevoutHash"` over every input's 36-byte
  outpoint, or its empty form under `ANYONECANPAY`.
- `sequence_digest` — `"ZTxIdSequencHash"` over every input's 4-byte
  sequence, or its empty form under `ANYONECANPAY`, `NONE` or `SINGLE`.
- `outputs_digest` — `"ZTxIdOutputsHash"` over every output's
  `value (8-byte LE) || scriptPubKey (compactSize length + bytes)`; its empty
  form under `NONE`; under `SINGLE`, the digest of the single output at
  `index` (empty form when there is no output at that index).
- `txin_sig_digest` — `BLAKE2b-256("Zcash___TxInHash", outpoint ||
  scriptCode || value || sequence)` for the signed input: the outpoint, the
  spent coin's script **with** its compactSize length (so an empty script is
  the single byte `0x00`), the spent value as 8-byte little-endian **signed**,
  then the input's sequence — script code before value. The signed input's
  value and script code are committed to here and only here.
- With no signed input, the digest is the transaction identifier verbatim.

**These two rows are not byte-identical to the `zip244-06` and `zip244-03` rows
steps 06–09 ship.** NU5's consensus branch id changed from `0xf919a198` to
`0x37519621` between app-09 and app-10, so the older vectors no longer
deserialize at this band — `Transaction::zcash_deserialize` rejects them with
`Parse("expected a valid network upgrade from the consensus branch id")`. The
rows here are the same two vectors regenerated upstream for the current branch
id: same transaction bodies, same signed input, every digest moved because the
branch id is covered by all of them.

### ZIP-209 transaction value pools (step 08)

```rust
// ── zebra_chain::value_balance ────────────────────────────────────────────
pub struct ValueBalance<C>(/* ... */);
impl<C: Constraint + Copy> ValueBalance<C> {
    pub fn zero() -> Self;
    pub fn from_transparent_amount(amount: Amount<C>) -> Self;
    pub fn from_sprout_amount(amount: Amount<C>) -> Self;
    pub fn from_sapling_amount(amount: Amount<C>) -> Self;
    pub fn from_orchard_amount(amount: Amount<C>) -> Self;
    pub fn transparent_amount(&self) -> Amount<C>;
    pub fn sprout_amount(&self) -> Amount<C>;
    pub fn sapling_amount(&self) -> Amount<C>;
    pub fn orchard_amount(&self) -> Amount<C>;
}
impl ValueBalance<NegativeAllowed> {
    pub fn remaining_transaction_value(&self) -> Result<Amount<NonNegative>, amount::Error>;
}

// ── zebra_chain::amount ───────────────────────────────────────────────────
pub enum NegativeAllowed {}
pub const MAX_MONEY: i64;
pub enum Error { Constraint { value: i64, range: RangeInclusive<i64> }, /* ... */ }
```

`remaining_transaction_value` is implemented only for `NegativeAllowed` at this
tag, which is part of the contract: the sum of the four pools may legitimately
be negative before the rule rejects it. The tests:

| Test | Assertion |
|---|---|
| `zip209_remaining_transaction_value_is_the_sum_of_the_four_pools` | `(1000, -250, -300, -50)` leaves `Ok(400)`, not 1600 — it is the sum of all four pools, not transparent minus shielded; `(-9000, 4000, 5000, 250)` leaves `Ok(250)`; the zero balance leaves `Ok(0)` |
| `zip209_a_negative_pool_sum_is_rejected_with_the_non_negative_range` | `(100, -50, -51, 0)` is `Err(amount::Error::Constraint { value: -1, range: 0..=MAX_MONEY })`, and `(0, 0, -21_000, 0)` reports `value: -21_000` — the shortfall itself, not one of the individual pools |
| `zip209_each_pool_constructor_leaves_the_other_three_at_zero` | each `from_*_amount(7)` sets only its own pool, and `zero()` sets all four to zero |

Every `ValueBalance` in these tests is built from the four single-pool
constructors and addition, so a constructor that smeared one pool's amount
across the others fails the tests for the other pools too.

### Fixtures

`fixtures/zip244.tsv` — two rows of the ZIP-244 vector set, read by column name
so a reordered or extended fixture keeps working. Every other input in this
group is constructed in the driver.

---

## §8 Negative space — out of scope

Read this section before deciding where to spend time. Three of this step's six
capabilities are checked **only by `workspace-checks` compiling them**. That is
a deliberate, recorded limitation of the harness at this step — the
drivers are integration tests in `<crate>/tests/`, and an integration test can
only reach a crate's public API — not a hint that the work is optional. Step 13
builds on all of it.

The following capabilities are new at app-12 but cannot be exercised from
integration tests injected into a crate's `tests/` directory, because their
parent modules are declared `mod` (private) in `lib.rs`:

**Halo2 batch verifier** (`zebra_consensus::primitives::halo2`): `BatchVerifier`,
`Verifier`, `Halo2Error`, `VERIFYING_KEY`, `VERIFIER`. The module `primitives`
is private in `zebra-consensus/src/lib.rs`, so none of its children are
accessible externally. Wire the service into the consensus pipeline; the requirement
groups observe only the error variants it may raise, not the verifier directly —
and in fact no group in scope observes *any* Halo2 error variant at this step: the
module is internal, and what the cases in scope read is the behaviour that flows
through it (the Orchard flag rule and the ZIP-212 coinbase decryption), not the
verifier's own errors.

**ZIP-155 addrv2** (`zebra_network::protocol::external::addr::v2`): `AddrV2`
is `pub(in super::super)`, which restricts visibility to the `external` module
and below. Constants `MAX_ADDR_V2_ADDR_SIZE = 512`,
`ADDR_V2_IPV4_NETWORK_ID = 0x01` (4-byte address),
`ADDR_V2_IPV6_NETWORK_ID = 0x02` (16-byte address) are defined in this
module. Implement the `addrv2` wire encoding; no integration test checks it
at this step.

**ZIP-214 funding stream functions**: `funding_stream_values`,
`funding_stream_address`, `filter_outputs_by_address`, `check_script_form`
all live inside `zebra_consensus::block::subsidy::funding_streams`, which is
in the private `block` module. Implement them; the error variant
`SubsidyError::FundingStreamNotFound` they raise is checked via `§5`.

One further thing is new at app-12 but unobservable for a reason other than
module privacy, and is called out so it is clear it was considered rather than
missed:

**`Action::max_allocation` and its `const_assert!`.** At app-12
`orchard/shielded_data.rs:185-191` hoists the existing expression into a
function-local `const MAX` and adds
`static_assertions::const_assert!(MAX < (1 << 16))`. The *returned value* is
byte-identical to app-11, and a `const_assert!` is a compile-time check
internal to the crate with no effect observable from outside it. No integration
test can distinguish its presence, so none asserts on it.

### Every documented rejection path, and where it is checked

This document states eight things that an implementation must *refuse*. Each
one is listed here with the group that drives it, or with the reason none can.

| Rejection path | Stated at | Checked where |
|---|---|---|
| Orchard `Flags` bits 2–7 reserved: a byte setting one is a `SerializationError::Parse`, not a masked value | §3 | `inherited-milestones`, `reserved_flag_bits_are_rejected` (gate — adds no requirement, because the `Flags` serde is byte-identical at app-11) |
| `has_enough_orchard_flags` is `false` / `Err(NotEnoughFlags)` for V5 with an Orchard action and no enable bit | §3, §4 | `orchard-flags-rule`, `flags_rule_rejects_orchard_actions_with_no_enable_bits` |
| `coinbase_outputs_are_decryptable` raises `CoinbaseOutputsNotDecryptable` for an undecryptable Heartwood-or-later coinbase output | §4 | `zip212-coinbase-decryptable`, `coinbase_outputs_are_decryptable_rejects_undecryptable_orchard_output` |
| A block whose transaction count is absurd is a parse error, not an allocation | §6 | `inherited-block-codec` (gate — adds no requirement) |
| A truncated block is `SerializationError::Io` | §6 | `inherited-block-codec` (gate — adds no requirement) |
| `NoteCommitmentTree::append` returns `Err(FullTree)` when the tree is full | §2 | **Nowhere, and it cannot be.** The tree has depth 29, so provoking it needs 2^29 = 536,870,912 appends. `NoteCommitmentTreeError` is asserted to have exactly that one variant, with its `Display` and `Debug`, which is as close as a test can get in bounded time. Write the `Err` arm; nothing will call it |
| `SubsidyError::FundingStreamNotFound` when an expected funding-stream output is absent | §5 | **Nowhere, and it cannot be.** The only code that raises it is inside the private `zebra_consensus::block::subsidy` module; no integration test can call it. The variant's existence and `Display` are asserted by `subsidy-error-variants`, which is the whole of that group's coverage |
| A transaction value balance whose four pools sum below zero has no remaining transaction value: `Err(amount::Error::Constraint { value, range: 0..=MAX_MONEY })` | §7 | `inherited-milestones`, `zip209_a_negative_pool_sum_is_rejected_with_the_non_negative_range` (gate — adds no requirement, inherited from step 08) |

The distinction matters when you are deciding what to test yourself: the
behavioural rows will not survive a stub; the two "nowhere" rows are checked on
shape only, so your own unit tests under `src/` are the only thing that will
catch a mistake in them.

One rejection path is deliberately **not** asserted, and is recorded here so
that its absence reads as a decision rather than an oversight: the Canopy half
of the ZIP-212 rule in its rejecting direction, i.e. a coinbase output whose
note plaintext lead byte is `0x01` at a post-Canopy height. The reference's own
comment on `coinbase_outputs_are_decryptable` records that librustzcash still
accepts a `0x01` lead byte during the ZIP-212 grace period and that it offers no
way to change that, so any assertion here would pin a pinned dependency's
behaviour instead of this milestone's. The Canopy epoch is instead covered in
the accepting direction, by two acceptance tests that straddle the boundary —
see §4.

---

## §9 Inherited state and handoff

This step inherits the complete workspace from step 11, including the mempool,
ZIP-244 sighash, peer connection management, and transparent and Sapling
validation. The next step (13) will add the finalized state's history tree
and additional contextual checks.

Leave the workspace building and passing `cargo test -p zebra-chain -p
zebra-consensus --offline`. Do not delete `/app/specs/`,
`/app/TEST_INTERFACE.md`, `/app/WORKSPACE_HANDOFF.md`, or `/app/timer.sh`.

**This is checked.** The `workspace-handoff` gate group asserts that
all four still exist and are non-empty: `specs/` must still be a directory
holding at least `nu5.txt`, `PROVENANCE.md` and the three ZIP texts this
milestone's "Read these local references" section cites (`zip-0155.rst`,
`zip-0207.rst`, `zip-0212.rst`); `TEST_INTERFACE.md` and `WORKSPACE_HANDOFF.md`
must still be non-empty files; and `timer.sh` must still begin with a shebang.
The group resolves the workspace root as the parent of `CARGO_MANIFEST_DIR`, so
it reads `/app` without hard-coding it, and it touches no crate API at all.

Nothing about this is new at step 12 — the same four paths are handed down at
every band, and this group passes against the step-11 tree unchanged — so it is
a gate rather than a requirement group. The whole `/app` tree, minus `.git/` and
`target/`, is snapshotted as this step's artifact and restored as step 13's
starting workspace; deleting any of the four hands step 13 a workspace with no
specification to read, which is why it is a veto rather than a soft failure.
