# TEST_INTERFACE.md — milestone 4, Orchard shielded data, nullifiers in state, scriptCode fix

This document is the contract the tests are written against. Every
module path, type, method, field, constant, error variant and byte layout the
verifier depends on is named here. If a behaviour is not stated here or in a
specification document named by `instruction.md`, it is not in scope.

## 1. How the verifier reaches your code

The verifier runs seven groups. Five run **integration test files**, copied
into three different crates' `tests/` directories:

```
cargo test --offline --locked --jobs 4 -p zebra-chain      --test <name> -- --test-threads=1
cargo test --offline --locked --jobs 4 -p zebra-state      --test <name> -- --test-threads=1
cargo test --offline --locked --jobs 4 -p zebra-consensus  --test <name> -- --test-threads=1
```

(If `--offline --locked` cannot resolve, the verifier retries with `--offline`
alone, then `--locked` alone, then neither, for every cargo invocation —
build, check and test alike.) The other two groups run whole-workspace
commands instead of a test file: `cargo build --offline --locked -p
zebra-chain --tests` and `cargo check --offline --locked --workspace
--all-targets --all-features`.

Consequences you must respect:

- The tests reach your code **only through the public API** of whichever
  crate they are compiled into (`zebra_chain`, `zebra_state` or
  `zebra_consensus`), as an external consumer. `pub(crate)`, `pub(super)` and
  private items are invisible to them, even though enum variants of a public
  enum are always reachable regardless of how the enum is constructed.
- Each test file is compiled against that crate's `Cargo.toml` **as you leave
  it**. Do not remove an existing `[dependencies]` or `[dev-dependencies]`
  entry from `zebra-chain`, `zebra-state` or `zebra-consensus`.
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

You are extending existing modules, not adding new ones:

| Module | Declared as (already) | What this milestone adds |
| --- | --- | --- |
| `zebra_chain::orchard` | `pub mod orchard;` with `orchard::{Action, Nullifier}` and the wrapper types (step 3). `ShieldedData`, `Flags`, `AuthorizedAction` and `shielded_data`'s constants do **not** exist yet | **adds** the bundle layer: `orchard::shielded_data::ShieldedData`/`Flags`/`AuthorizedAction`/`ACTION_SIZE`/`SPEND_AUTH_SIG_SIZE`/`AUTHORIZED_ACTION_SIZE` and the parsing/serializing of a bundle with 1+ real actions, not just the empty case |
| `zebra_chain::transaction::Transaction` | `pub enum Transaction` | working `orchard_shielded_data`, `orchard_actions`, `orchard_nullifiers` methods (§3) |
| `zebra_chain::transparent::Script` | `pub struct Script` | change from a public tuple field to a private one, with `new`/`as_raw_bytes` (§5) |
| `zebra_state` (finalized state, internal) | crate-internal commit path | index Orchard-shielded transactions and their nullifiers the same way transparent outputs are already indexed, so they survive a commit and come back out through the existing read requests (§4) |
| `zebra_consensus::error` | `pub mod error;` | one new `TransactionError` unit variant (§6) |

Internal file layout is yours; only the names below, at the paths given, are
in scope.

## 3. Orchard `Action` and `ShieldedData` codec — group `orchard-shielded-data-codec` (`zebra-chain`)

This group hand-builds four V5 transactions (1-action, 2-action, spends-only,
outputs-only bundles, spanning negative/zero/positive `value_balance`) and
round-trips them through `Transaction::zcash_deserialize`/`zcash_serialize`.

**Byte layout, `orchard::Action` (ZIP-225 §7.5), exactly 820 bytes, fields in
this order, none length-prefixed**:

| Field | Type | Bytes |
| --- | --- | --- |
| `cv` | `orchard::commitment::ValueCommitment` | 32 |
| `nullifier` | `orchard::Nullifier` | 32 |
| `rk` | `redpallas::VerificationKeyBytes<SpendAuth>` | 32 |
| `cm_x` | `pallas::Base` | 32 |
| `ephemeral_key` | `orchard::keys::EphemeralPublicKey` | 32 |
| `enc_ciphertext` | `orchard::note::EncryptedNote` | 580 |
| `out_ciphertext` | `orchard::note::WrappedNoteKey` | 80 |

**The three sizes this milestone names in `orchard::shielded_data`.** They are
`pub` constants of type `u64`, at that module path, from this milestone onward
— later milestones read them unchanged, so they must keep these names, this
type and these values:

```rust
// In zebra_chain::orchard::shielded_data
pub const ACTION_SIZE: u64 = 820;             // 5 * 32 + 580 + 80, the Action above
pub const SPEND_AUTH_SIG_SIZE: u64 = 64;      // one Signature<SpendAuth>, as in vSpendAuthSigsOrchard below
pub const AUTHORIZED_ACTION_SIZE: u64 = 884;  // ACTION_SIZE + SPEND_AUTH_SIG_SIZE
```

`AUTHORIZED_ACTION_SIZE` is the serialized size of one `AuthorizedAction` —
the 820-byte `Action` followed by its 64-byte spend-auth signature, the two
halves §3's second table writes separately as `nActionsOrchard` actions plus
`vSpendAuthSigsOrchard`. It is also what bounds the Orchard allocation chain:
`TrustedPreallocate for Action` returns `(MAX_BLOCK_BYTES - 1) /
AUTHORIZED_ACTION_SIZE`, and `TrustedPreallocate for Signature<SpendAuth>`
returns the same number.

**Byte layout, the Orchard bundle inside a `V5` transaction, in order**:
`nActionsOrchard` (compactSize) + that many `Action`s back-to-back, then —
present if and only if `nActionsOrchard > 0` — `flagsOrchard` (1 byte),
`valueBalanceOrchard` (`Amount`, 8-byte signed LE), `anchorOrchard`
(`orchard::tree::Root`, 32 bytes), `sizeProofsOrchard` (compactSize) +
`proofsOrchard`, `vSpendAuthSigsOrchard` (exactly one 64-byte
`Signature<SpendAuth>` per action, **not** separately length-prefixed — its
count is the action count), `bindingSigOrchard` (64 bytes). If
`nActionsOrchard == 0`, none of those six fields are present at all, and
`Transaction::orchard_shielded_data()` must return `None`.
`sizeProofsOrchard` is the byte length of the aggregated proof,
`2720 + 2272 · nActionsOrchard` (ZIP-225 §7.1); the proof's *contents* are
opaque at this milestone (§8) — only its length is structural.

**The V5 transaction ends at `bindingSigOrchard`.** That is this
milestone's last field; with `nActionsOrchard == 0` the transaction ends at
the `nActionsOrchard` byte itself. The copy of `nu5.txt` under
`/app/specs/` is a later revision of the protocol document than this
milestone's release, and continues its V5 table with a further *Ironwood*
region after `bindingSigOrchard`; those fields are a later protocol addition
and are **not** part of this milestone's format. Every fixture and every
hand-built transaction in this step's tests ends exactly at
`bindingSigOrchard`, and a parser that requires (or reads) an Ironwood region
fails all of them.

**Types and methods this group calls**:

- `Transaction::orchard_shielded_data(&self) -> Option<&orchard::ShieldedData>`
  — `Some` for `V5` with a non-empty bundle, `None` for every other version
  and for a `V5` with an empty bundle.
- `Transaction::orchard_actions(&self) -> impl Iterator<Item = &orchard::Action>`
  and `Transaction::orchard_nullifiers(&self) -> impl Iterator<Item = &orchard::Nullifier>`
  — both empty iterators, not a panic, when there is no Orchard data.
- `orchard::ShieldedData` public fields: `flags: orchard::Flags`,
  `value_balance: Amount`, `shared_anchor: orchard::tree::Root`,
  `proof: Halo2Proof`, `actions: AtLeastOne<orchard::AuthorizedAction>`,
  `binding_sig: Signature<Binding>`.
  The container is the inherited `zebra_chain::serialization::AtLeastOne<T>`
  (re-exported at `zebra_chain::serialization`, the same type §7 describes),
  not a new container of this milestone's own: the driver calls
  `AtLeastOne::len(&self)`, `AtLeastOne::iter(&self)` and
  `AtLeastOne::actions(&self)` on this field, so a differently-shaped
  container that happens to be *named* `AtLeastOne` does not compile.
- `orchard::ShieldedData::actions(&self) -> impl Iterator<Item = &orchard::Action>`
  and `::nullifiers(&self) -> impl Iterator<Item = &orchard::Nullifier>`, and
  the analogous `AtLeastOne<orchard::AuthorizedAction>::actions(&self)`  —
  all three must agree on order and count.
- `orchard::Flags::bits(&self) -> u8` — `ENABLE_SPENDS = 0b01`,
  `ENABLE_OUTPUTS = 0b10` (bitflags, reserved bits 2-7 zero).
- `orchard::AuthorizedAction { action: orchard::Action, spend_auth_sig: Signature<SpendAuth> }`,
  `::into_parts(self) -> (Action, Signature<SpendAuth>)`,
  `::from_parts(Action, Signature<SpendAuth>) -> AuthorizedAction` — must be
  exact inverses (the nullifier and signature must survive the round trip).
- `AuthorizedAction` must derive `Clone` (the codec driver calls
  `.clone().into_parts()` to decompose without consuming the `AtLeastOne` wrapper).
- `orchard::Nullifier`: `From<[u8; 32]>` and `From<Nullifier> for [u8; 32]`
  (used to compare nullifier values against fixture bytes).

## 4. Orchard nullifiers reach `zebra-state` — group `orchard-state-commit` (`zebra-state`)

This group builds small, correctly-linked chains of synthetic blocks (a
version-4 coinbase transaction plus, in most blocks, a V5 transaction
carrying real Orchard actions), commits each one **in order** to an ephemeral
finalized state, and checks that the block, its transactions and the
Orchard nullifiers inside them come back out through the existing read
surface.

**Scope boundary**: this group only performs commits where each block is the
immediate, correctly-hashed child of the current tip. `zebra_state`'s
finalized-state commit path asserts chain order and linkage internally
(`assert_eq!`, not a recoverable `Result::Err`); out-of-order, duplicate or
wrong-parent commits are not exercised by this driver and are not part of
this milestone's contract — see §7.

**Types and methods this group calls** (all already public before this
step; what changes is what comes back once Orchard data is present):

- `zebra_state::init(config: Config, network: Network) -> BoxService<Request, Response, BoxError>`,
  `zebra_state::Config::ephemeral() -> Config`.
- `Request::CommitFinalizedBlock(FinalizedBlock)` — `FinalizedBlock: From<Arc<block::Block>>`.
  The `From` impl panics if `block.coinbase_height()` is `None`; every block
  this driver commits has a well-formed BIP34-style coinbase height.
- `Response::Committed(block::Hash)` — the committed block's own hash,
  returned on success.
- `Request::Block(HashOrHeight)` (`HashOrHeight: From<block::Hash>`,
  `From<block::Height>`) → `Response::Block(Option<Arc<Block>>)`, `None` for
  an uncommitted hash or height, `Some` with byte-identical re-serialization
  otherwise.
- `Request::Transaction(transaction::Hash)` → `Response::Transaction(Option<Arc<Transaction>>)`.
  A transaction retrieved this way must expose the same
  `orchard_nullifiers()` values it was committed with (§3), and
  `orchard_shielded_data().is_none()` for a V5 transaction with zero Orchard
  actions.
- `Request::Tip` → `Response::Tip(Option<(block::Height, block::Hash)>)` —
  advances to the latest committed block; every earlier block stays
  queryable by hash and by height after later blocks are committed.

## 5. The `scriptCode` double-prefix fix — group `sighash-scriptcode-fix` (`zebra-chain`)

`transparent::Script` changes from a public tuple field the caller had to
prefix by hand, to:

```rust
pub struct Script(Vec<u8>); // private field
impl Script {
    /// Create a new Bitcoin script from its raw bytes.
    /// The raw bytes must not contain the length prefix.
    pub fn new(raw_bytes: &[u8]) -> Self;
    /// Return the raw bytes of the script without the length prefix.
    pub fn as_raw_bytes(&self) -> &[u8];
}
```

`Script`'s `ZcashSerialize` impl writes exactly one compactSize length prefix
(`raw_bytes.len()`) followed by the raw bytes — the same as before this
milestone — but now that is the **only** place a prefix is ever added, because
callers can no longer construct a `Script` holding a pre-baked one. This
group reproduces all 5 sighash vectors shipped with the verifier's fixtures in
`sighash.tsv` (ZIP-143 vectors 1-2, ZIP-243 vectors 1-3) through
`Script::new()` and
`Transaction::sighash`, and separately proves the fix is not a no-op: taking
one of the two vectors with a non-empty `scriptPubKey`, manually
double-prefixing it the way a caller had to before this milestone, and
feeding *that* into `Script::new()` produces a **different**, wrong digest.

`Transaction::sighash(&self, network_upgrade: NetworkUpgrade, hash_type: sighash::HashType, input: Option<(u32, transparent::Output)>) -> blake2b_simd::Hash`
is unchanged; `transparent::Output { pub value: Amount<NonNegative>, pub lock_script: Script }`
is unchanged.

## 6. Consensus error surface — group `consensus-error-surface` (`zebra-consensus`)

`zebra_consensus::error::TransactionError` gains one new unit variant:

```rust
#[error("coinbase transaction MUST NOT have the EnableSpendsOrchard flag set")]
CoinbaseHasEnableSpendsOrchard,
```

It must be `Debug` and `PartialEq` (both already derived on the enum), its
`Display`/`.to_string()` text must be exactly the string above, and it must
compose with the existing `BlockError::Transaction(#[from] TransactionError)`
wrapper: `BlockError::from(TransactionError::CoinbaseHasEnableSpendsOrchard).to_string()`
is `"block contains invalid transactions"`, and matching that `BlockError`
back down to `BlockError::Transaction(inner)` recovers the same
`TransactionError` value. **The check that decides when to raise this
variant is not part of this milestone's public surface** — see §7.

## 7. Regression guard — group `block-codec-regression` (GATE — adds no requirement, vetoes the step if it fails; `zebra-chain`)

A trimmed copy of step 1's block-codec driver, run against the same 17-block
corpus (`fixtures/blocks.tsv` + `fixtures/blocks/*.hex`, unchanged since step
1): every block still round-trips byte-for-byte; `Block::hash()`,
`Block::coinbase_height()`, `transactions.len()`, the merkle root computed
from `Transaction::hash()`, and the re-serialized length still match the
corpus; and `MAX_BLOCK_BYTES = 2_000_000` is still enforced on deserialize
from both sides. This exists because this milestone changes `Transaction`
(a new field on `V5`) and `transparent::Script` (public field replaced by
private + accessors), both of which every transaction in every block —
Orchard or not — depends on to parse and serialize at all.

## 8. Negative space — what this milestone does not ask for

- **Orchard proof and signature validity.** Halo2 proof verification and
  RedPallas signature verification over Orchard actions are still pinned
  external primitives; nothing here checks that a bundle's proof or
  signatures are cryptographically valid, only that the bundle's bytes parse,
  serialize, and expose the fields and iterators named in §3.
- **A nullifier-spent query.** `zebra-state` is only required to make a
  committed nullifier's *value* retrievable through the existing
  transaction/block read requests (§4). A dedicated query such as "has this
  nullifier been seen before" is not part of this milestone's contract.
- **Duplicate or double-spend detection**, for Orchard or otherwise — the
  data is now indexed; deciding whether two blocks' worth of it conflicts is
  future work (`REQUIREMENTS.md` steps 6-7).
- **The `CoinbaseHasEnableSpendsOrchard` *check* itself.** The function that
  inspects a coinbase transaction's Orchard flags and raises this error is
  crate-internal to `zebra-consensus` (`mod transaction; mod check;`, neither
  `pub`) and is not reachable from an integration test. Only the error
  variant's own shape and text are checked (§6); the reviewer should not
  expect a driver that exercises the actual coinbase-flag rule end to end.
- **Out-of-order, duplicate, or mis-parented block commits.** Unchanged from
  earlier steps: the finalized state still asserts strict chain order
  internally rather than returning a recoverable error, and this milestone
  does not change that.
- **The V5 fields a later revision of the protocol document adds after
  `bindingSigOrchard`** — the Ironwood region (see §3). Not part of this
  milestone's wire format, not parsed, not serialized, not in scope.
- **The milestone-wide meta obligations** — `--offline` continuing to work,
  adding no new third-party dependency, and leaving `/app/specs/`,
  `TEST_INTERFACE.md`, `WORKSPACE_HANDOFF.md` and `timer.sh` in place — are
  instruction-level duties, not behaviours in scope. No group in this step
  inspects a manifest or checks that a document still exists: the verifier's
  resolution ladder falls back to the registry rather than failing, and its
  only file check is that `/app/Cargo.toml` is present. They are listed here
  so a reader does not mistake them for assertions.
- **The zeroing of `flagsOrchard`'s reserved bits 2-7** is a property of the
  type (§3), not a rejection rule: no case feeds a flags byte with a reserved
  bit set, and none asserts what the parser would do with one. The cases
  compare `flags.bits()` against fixture bytes whose reserved bits are zero.
- Everything already out of scope per `REQUIREMENTS.md`'s
  "Explicit out-of-scope decisions" list (the script interpreter, proof
  system internals, mining/wallet/RPC, `lightwalletd`, Tor, NU6) remains out
  of scope.

## 9. Handoff

**Inherited from steps 1-3**: the V1-V5 transaction codec, the ZIP-225
Sapling-in-V5 layout, the Orchard data structure definitions, and the
17-block regression corpus — all unchanged in shape, exercised again here
only to prove this milestone's changes did not disturb them.

**What step 4 leaves for step 5**: a finalized state that can retrieve
Orchard-shielded transactions and their nullifiers by hash and by height, a
`Script` type whose bytes are never double-prefixed, and a `TransactionError`
enum that already names the coinbase-Orchard-flags rule even though nothing
yet enforces it. Step 5 (`REQUIREMENTS.md`: ZIP-221 history tree, ZIP-211
Sprout freeze, async RedPallas verification, transparent in/out in V5) is
expected to find all of the above in place and unchanged in its public
shape; it does not need to re-derive nullifier extraction or the scriptCode
fix.
