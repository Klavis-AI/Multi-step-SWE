# TEST_INTERFACE — step 11, ZIP-401 eviction weights, a decode ceiling and connection limits

This document is the contract the tests for this step are written
against. Every module path, type, method signature, public field, constant,
error variant and exact `Display` string those tests touch is named here. Where
a numeric constant, an error shape, a receiver (`self` vs `&self`) or an exact
string is stated, it is normative: the tests assert it exactly.

The tests are ordinary integration tests placed in `zebra-chain/tests/`,
`zebra-consensus/tests/` and `zebra-network/tests/` and compiled against your
manifests. They see **only the public API** of those three crates, as an
external consumer. Anything you leave `pub(crate)`, `pub(super)`, private, or
behind `#[cfg(test)]` / `#[cfg(feature = "proptest-impl")]` is invisible to them
— §6 says exactly what that excludes, and why some of this milestone's work is
deliberately out of scope.

Conventions used below:

- "must" means a test fails if it is not so.
- "the block corpus" is the 17-block fixture set described in §5.1, carried
  forward unchanged from step 10.
- "the cost" and "the eviction weight" are the two ZIP-401 quantities defined in
  §2.2; where they are quoted as numbers, those numbers are normative and
  **differ from the shipped specification** (§2.3).
- all fee arithmetic is in zatoshis, the smallest unit, carried as
  `zebra_chain::amount::Amount<NonNegative>` and compared as `u64`.

---

## 1. How the verifier reaches your code

The verifier runs seven groups. Five run **integration test files**, copied into
three crates' `tests/` directories:

```
cargo test --offline --locked --jobs 4 -p zebra-chain     --test <name> -- --test-threads=1
cargo test --offline --locked --jobs 4 -p zebra-consensus --test <name> -- --test-threads=1
cargo test --offline --locked --jobs 4 -p zebra-network   --test <name> -- --test-threads=1
```

(If `--offline --locked` cannot resolve, the verifier retries with `--offline`
alone, then `--locked` alone, then neither, for every cargo invocation — build,
check and test alike.) The other two groups run whole-workspace commands instead
of a test file: `cargo build --offline --locked -p zebra-chain --tests` and
`cargo check --offline --locked --workspace --all-targets --all-features`.

Consequences you must respect:

- Each test file is compiled against that crate's `Cargo.toml` **as you leave
  it**. Do not remove an existing `[dependencies]` or `[dev-dependencies]` entry
  from `zebra-chain`, `zebra-consensus` or `zebra-network`. In particular
  `zebra-chain` must keep its `hex` dependency (a driver in that crate
  uses `hex::decode`); `zebra-consensus` does not have one and is not required to
  gain one. `zebra-network`'s inherited dev-dependencies are not touched by any
  driver in this step — keep them, but nothing asserts them.
- `zebra-network` has **no `tests/` directory at present**, and neither do the
  other driver crates as shipped; the verifier creates each crate's `tests/`
  directory itself before copying a driver in, so
  no action is strictly required — but a `tests/` directory in your tree is the
  cheapest way to confirm a driver will land where you expect.
- The workspace must keep building **offline**; do not add a new third-party
  dependency to any manifest.
- A group whose driver fails to compile fails for that group. This is the
  fate of all four new drivers against the predecessor milestone, which is
  exactly what makes them discriminating — see §6.5.
- Tests run with `--test-threads=1` and must not depend on execution order
  between `#[test]` functions in the same file.
- `--all-targets` builds `zebra-chain/benches/block.rs`, which reaches
  `zebra_chain::block::tests::generate`. That module is gated
  `#[cfg(any(test, feature = "bench"))]`, so the workspace check is run with
  `--all-features`; keep the `bench` feature and that gate intact. Without
  `--all-features` this tag does not compile under `--all-targets` at all.

The group table, for reference:

| Group | Kind | Crate | Section |
| --- | --- | --- | --- |
| `zip401-cost-eviction` | requirement | `zebra-chain` | §2 |
| `tx-size-limit` | requirement | `zebra-chain` | §3 |
| `duplicate-spend-error-variants` | requirement | `zebra-consensus` | §4 |
| `connection-limit-config` | requirement | `zebra-network` | §5.3 |
| `inherited-block-codec` | gate | `zebra-chain` | §5.2 |
| `workspace-checks` | gate | `@build` | §1 |
| `build` | gate | `@build` | §1 |

The gate groups are mandatory: each adds no new requirement of its own, and a
failure in any one of them means the step is not satisfied regardless of the
requirement groups. The requirement groups cover this step's new capability.

## 1.1 Where the implementation goes

| Module path | Declared as | What this milestone adds |
| --- | --- | --- |
| `zebra_chain::transaction::unmined` | existing private module, re-exported from `zebra_chain::transaction` | `VerifiedUnminedTx::cost`, `VerifiedUnminedTx::eviction_weight`, the private cost-threshold constant (§2) |
| `zebra_chain::transaction::Transaction`'s `ZcashDeserialize` impl | existing, in `zebra-chain/src/transaction/serialize.rs` | the `MAX_BLOCK_BYTES` reader cap (§3) |
| `zebra_chain::block` | existing, `pub use serialize::MAX_BLOCK_BYTES` | nothing new; the constant the cap is read from (§3.1) |
| `zebra_consensus::error::TransactionError` | already `pub mod error;` | four new variants (§4) |
| `zebra_consensus::transaction::check` | **existing private module** | `spend_conflicts` (§4.4) — out of scope, see §6.2 |
| `zebra_network::Config` | existing private module `config`, re-exported from the crate root | three connection-limit methods (§5.3) |
| `zebra_network::constants` | existing `pub mod constants;` | `OUTBOUND_PEER_BIAS_DENOMINATOR` (§5.3) |

Internal file layout is yours; only the names below, at the paths given, are
in scope. Where a type is re-exported, the re-export path is the one the tests
use.

---

## 2. ZIP-401 cost and eviction weight — group `zip401-cost-eviction` (`zebra-chain`)

### 2.1 The type

`VerifiedUnminedTx` already exists (step 10) at
`zebra_chain::transaction::VerifiedUnminedTx`, re-exported from
`zebra_chain::transaction`, and already carries:

```rust
pub struct VerifiedUnminedTx {
    pub transaction: UnminedTx,
    pub miner_fee: Amount<NonNegative>,
}
```

with `VerifiedUnminedTx::new(transaction: UnminedTx, miner_fee: Amount<NonNegative>) -> Self`.
Neither the fields nor `new` change. `UnminedTx` keeps its public
`size: usize` field, and `UnminedTx::from(Transaction)` keeps filling it in.

### 2.2 The two methods

```rust
impl VerifiedUnminedTx {
    pub fn cost(&self) -> u64;
    pub fn eviction_weight(self) -> u64;
}
```

Both are **inherent methods** on `VerifiedUnminedTx`; the tests call them as
methods, not through a trait. Note the receivers, which are not symmetric and
are part of the contract:

- `cost` takes `&self`. Calling it twice on the same value must give the same
  answer, and must not move the value — a test calls `cost()` twice in a row.
- `eviction_weight` takes `self` **by value** and consumes it. The tests rely on
  this: they call `.eviction_weight()` on an owned value and never use it again.

The arithmetic, which is the whole of the requirement:

```
cost(tx)            = max(tx.transaction.size as u64, COST_THRESHOLD)
low_fee_penalty     = LOW_FEE_PENALTY if (miner_fee as u64) < CONVENTIONAL_FEE else 0
eviction_weight(tx) = cost(tx) + low_fee_penalty
```

The comparison is **strictly less than**. A fee exactly equal to the
conventional fee is *not* penalised; a fee one zatoshi below it is.

### 2.3 The three constants, which are normative

| Name | Value | Visibility | Where |
| --- | --- | --- | --- |
| cost threshold | `4000` (`u64`) | module-private `const` | `zebra-chain/src/transaction/unmined.rs` |
| low-fee penalty | `16_000` (`u64`) | inline in the accessor | `zebra-chain/src/transaction/unmined.rs` |
| conventional fee | `1000` (`u64`, zatoshis) | inline in the accessor | `zebra-chain/src/transaction/unmined.rs` |

Visibility here is not something the tests can check — the threshold is a
private `const MEMPOOL_TRANSACTION_COST_THRESHOLD: u64 = 4000;` and the other
two are locals inside `eviction_weight`. Only the **values** are checked, through
the observable output of the two methods. Do not export them, and do not make
the penalty configurable.

> **Specification divergence — read this before copying any number.**
>
> `/app/specs/zips/zip-0401.rst` is the *current* revision of ZIP-401 and it states
> `max(memory size in bytes, 10000)` for the cost and `40000` for the
> low-fee penalty, and its §"Rationale" describes the fee threshold as having
> been lowered to `1000` zatoshis by ZIP 313 in `zcashd` v4.2.0 and then
> adjusted again when the cost threshold moved from `4000` to `10000`.
>
> This milestone reproduces a release that predates that adjustment. The code
> at `v1.0.0-beta.0` implements `4000` / `16000` / `1000`, its own doc comments
> quote the older rationale text, and **the code is what the tests
> assert**. Take `4000`, `16000` and `1000` from this table. The algorithmic
> shape the spec describes, the strict-below comparison and the
> `cost + penalty` sum are all normative as written; only the two magnitudes
> moved.

### 2.4 What the tests do

Against `zebra_chain::transaction::{Transaction, UnminedTx, VerifiedUnminedTx}`
and `zebra_chain::amount::{Amount, NonNegative}`:

1. **`cost_of_small_transaction_is_the_threshold`** — a minimal v4 transaction
   (empty inputs, empty outputs, `LockTime::min_lock_time()`, `block::Height(0)`
   expiry, no JoinSplit, no Sapling) serializes to well under 4000 bytes; its
   `cost()` must be exactly `4000`, not its serialized size. The test computes
   the size itself with `ZcashSerialize::zcash_serialize` into a `Vec<u8>` and
   asserts it is below the threshold first, so a failure here means the floor
   was not applied rather than that the fixture changed.
2. **`cost_of_large_transaction_is_its_size`** — a v4 transaction padded with
   150 transparent outputs, each carrying a 50-byte `transparent::Script`, is
   constructed, measured, asserted to exceed 4000 bytes, and then asserted to
   have `cost() == serialized_size`. Each output contributes
   `8 (value) + 1 (compactSize) + 50 (script)` bytes, plus a per-transaction
   overhead, so the total comes to roughly 8.6 kB — comfortably above the
   4,000-byte threshold, and the case asserts the measured size rather than a
   pinned figure.
3. **`cost_equals_max_of_size_and_threshold`** — the invariant
   `cost() == max(size, 4000)` re-derived against the interface's own
   `zcash_serialize` for both a v4 and a v5 transaction, on both sides of the
   floor.
4. **`eviction_weight_with_zero_fee_adds_penalty`** — fee `0 < 1000`, so
   `eviction_weight() == 4000 + 16000 == 20000`.
5. **`eviction_weight_with_fee_below_conventional_adds_penalty`** — fee `999`,
   the boundary from below: `4000 + 16000`.
6. **`eviction_weight_at_conventional_fee_has_no_penalty`** — fee `1000`, the
   boundary itself: `4000`, penalty `0`. This is the test that pins
   `<` against `<=`.
7. **`eviction_weight_above_conventional_fee_has_no_penalty`** — a fee well
   above the conventional fee gives `4000`.
8. **`eviction_weight_large_tx_low_fee`** — the large padded transaction from
   (2) with a fee below the conventional fee: `serialized_size + 16000`, so the
   penalty is additive on top of the *size*, not the floor.
9. **`eviction_weight_large_tx_adequate_fee`** — the same transaction with an
   adequate fee: `serialized_size` exactly.
10. **`cost_does_not_consume_self`** — `cost()` twice on one value, both equal.
11. **`verified_unmined_tx_fields_are_accessible`** — the struct's two public
    fields hold what `VerifiedUnminedTx::new` was given, read back through
    `Amount<NonNegative>` → `u64`.

### 2.5 Who is expected to consume this

The eviction policy — weighted random removal from a "recently evicted" set
under a `mempooltxcostlimit` — lives in `zebrad`, which is out of scope (§6.1). The
two accessors above are the whole of the surface in scope, and they are pure: no
I/O, no clock, no configuration.

---

## 3. A decode ceiling for transactions — group `tx-size-limit` (`zebra-chain`)

### 3.1 The constant

```rust
// zebra_chain::block
pub const MAX_BLOCK_BYTES: u64 = 2_000_000;
```

This already exists, is already `pub`, and is already re-exported from
`zebra_chain::block` (`pub use serialize::MAX_BLOCK_BYTES;`). Its value does not
change and it is checked only as a fixed point the rest of the group depends on:
a test reads it back to confirm it is still exactly `2_000_000`, because every
other assertion in the group is expressed against it.

### 3.2 The cap

`ZcashDeserialize for Transaction`, in
`zebra-chain/src/transaction/serialize.rs`, must wrap its reader:

```rust
fn zcash_deserialize<R: io::Read>(reader: R) -> Result<Self, SerializationError> {
    // If the limit is reached, we'll get an UnexpectedEof error.
    let mut limited_reader = reader.take(MAX_BLOCK_BYTES);
    // ... every subsequent read goes through `limited_reader`
}
```

`MAX_BLOCK_BYTES` is a `u64`, which is what `io::Read::take` wants, so no
conversion is needed. The body of the deserializer after that line must read
only from the limited reader; the existing protocol-message limits
(`MAX_PROTOCOL_MESSAGE_LEN`, applied by `zcash_deserialize_into` and the
`zcash_serialize` compactSize readers) are **not** changed and must keep
working.

### 3.3 What the cap is observable as

A transaction is a self-describing encoding, so the cap does **not** show up as
"a long stream of bytes is refused". It shows up as *a transaction that declares
more content than 2,000,000 bytes cannot be decoded*. The tests build such a
transaction legitimately — they serialize it with `zcash_serialize`, which is
what makes the fixture well-formed — and then require:

- `Transaction::zcash_deserialize(&bytes[..])` returns `Err(SerializationError::Io(_))`,
  because the limited reader runs dry mid-transaction rather than at a clean
  boundary. The tests match
  `Err(SerializationError::Io(_))` and treat any other `Err` as a failure
  (`Err(SerializationError::Parse(_))`, for instance, means the cap was not the
  thing that stopped it).
- The failure is an **error, not a panic, and not a successful parse**.

The fixture is a v1 transaction, the version with the cheapest per-input
encoding, so the input count needed to cross two megabytes is large but the
construction stays simple:

- `zebra_test::vectors::DUMMY_INPUT1` and `DUMMY_OUTPUT1` are parsed with
  `transparent::Input::zcash_deserialize` / `transparent::Output::zcash_deserialize`
  to get already-valid values; no hand-built scripts are needed.
- The input count is `MAX_BLOCK_BYTES / serialized_len_of_one_input`, so the
  inputs alone exceed the limit.
- `chrono::{DateTime, NaiveDateTime, Utc}` build the `LockTime::Time`, matching
  the crate's own oversized-transaction test.
- The fixture is then asserted to be longer than `MAX_BLOCK_BYTES` before it is
  used — an assumption check, so a shrinking `DUMMY_INPUT1` cannot silently
  turn the group vacuous.

`zebra_test` and `chrono` are already dev/regular dependencies of `zebra-chain`
(§1); `chrono` is a `[dependencies]` entry, so the test may name it directly.

### 3.4 The tests

1. **`max_block_bytes_constant_is_two_million`** — `MAX_BLOCK_BYTES == 2_000_000`.
2. **`oversized_transaction_is_rejected`** — the discriminating assertion. The
   well-formed, longer-than-two-megabyte v1 transaction does not parse.
3. **`oversized_transaction_fails_as_an_io_error`** — and it fails as
   `SerializationError::Io(_)`, not `Parse(_)` and not `Ok(_)`.
4. **`the_oversized_fixture_is_a_well_formed_encoding`** — the control that makes
   (2) meaningful. A transaction of roughly *half* the limit, built the same
   way, parses successfully as `Transaction::V1` and is asserted to be shorter
   than the oversized fixture. Without this, (2) could be satisfied by a
   deserializer that rejects everything.
5. **`small_v4_transaction_still_round_trips`** — a serialized v4 transaction
   parses back as `Transaction::V4`, unchanged.
6. **`small_v5_transaction_still_round_trips`** — likewise for `Transaction::V5`.
7. **`empty_input_is_an_io_error`** — a zero-length input buffer is
   `SerializationError::Io(_)`.
8. **`truncated_header_is_an_io_error`** — four bytes, one short of a version
   header, is `SerializationError::Io(_)`.
9. **`oversized_large_v5_transaction_is_rejected_too`** — the same ceiling
   applies to v5, so the cap is on the reader and not on one version's parser.

### 3.5 What is *not* the mechanism

The tests deliberately avoid two constructions that look like they test a size
limit and do not:

- **Padding a stream with zero bytes past the limit.** The trailing zeros parse
  as a valid minimal transaction body, so the result is `Ok`, not an error, in
  *every* tree. A bounded reader only bites when the parser actually needs a
  byte past the bound.
- **A compactSize field claiming an enormous count.** That is rejected by
  `MAX_PROTOCOL_MESSAGE_LEN` in `zebra-chain/src/serialization/read_zcash.rs`,
  which is `2 * 1024 * 1024` in both the predecessor and this tag — so the
  assertion would hold against a tree with no transaction size limit at all.
  It is not part of this group.

---

## 4. Naming a double spend — group `duplicate-spend-error-variants` (`zebra-consensus`)

### 4.1 The four variants

`zebra_consensus::error::TransactionError` gains four variants, appended after
`IncorrectFee` (which itself arrived in step 10). The enum already derives
`Error, Clone, Debug, PartialEq, Eq`.

```rust
#[error("transparent double-spend: {_0:?} is spent twice")]
DuplicateTransparentSpend(transparent::OutPoint),

#[error("sprout double-spend: duplicate nullifier: {_0:?}")]
DuplicateSproutNullifier(sprout::Nullifier),

#[error("sapling double-spend: duplicate nullifier: {_0:?}")]
DuplicateSaplingNullifier(sapling::Nullifier),

#[error("orchard double-spend: duplicate nullifier: {_0:?}")]
DuplicateOrchardNullifier(orchard::Nullifier),
```

### 4.2 The payload types

Each payload type is one the tests construct as a standalone value and place in
the variant; none of them requires a transaction to build. All four are `Clone`
and `Debug` and `PartialEq`.

| Payload | Path | How a test builds one |
| --- | --- | --- |
| `transparent::OutPoint` | `zebra_chain::transparent::OutPoint` | directly: `{ hash: transaction::Hash(bytes), index: u32 }` |
| `sprout::Nullifier` | `zebra_chain::sprout::Nullifier` | newtype over `[u8; 32]`, field public |
| `sapling::Nullifier` | `zebra_chain::sapling::Nullifier` | newtype over `[u8; 32]`, field public |
| `orchard::Nullifier` | `zebra_chain::orchard::Nullifier` | `TryFrom<[u8; 32]>` — **no public tuple field**; the inner `pallas::Base` is `pub(crate)` |

Two consequences, both already reflected in the tests:

- `zebra_chain::transaction::Hash`'s field is public, so outpoints and their
  hashes are built from byte arrays without a hex dependency. `zebra-consensus`
  has **no `hex` dev-dependency** (§1) and the tests use none.
- `orchard::Nullifier` is reached through
  `<orchard::Nullifier as std::convert::TryFrom<[u8; 32]>>::try_from`. Every
  low-byte value in `0..=255` (rest zero) is a valid `pallas::Base` encoding, so
  `try_from([seed, 0, 0, ...])` always succeeds. `use std::convert::TryFrom;`
  must be in scope — the crate's edition is 2018 and `TryFrom` is not in the
  2018 prelude (rustc 1.59, no `let ... else`, no `array::from_fn`, no
  `Option::is_some_and`).

### 4.3 The exact `Display` strings

`#[error(...)]` on the enum drives the `Display` impl. The tests assert these
**prefixes**, in full and exactly, and separately assert the substrings noted:

| Variant | Required prefix | Also required |
| --- | --- | --- |
| `DuplicateTransparentSpend` | `transparent double-spend:` | contains `is spent twice` |
| `DuplicateSproutNullifier` | `sprout double-spend:` | contains `duplicate nullifier` |
| `DuplicateSaplingNullifier` | `sapling double-spend:` | contains `duplicate nullifier` |
| `DuplicateOrchardNullifier` | `orchard double-spend:` | contains `duplicate nullifier` |

The rendering after the prefix is the payload's `Debug` output, so
`transparent double-spend: OutPoint { ... } is spent twice` and
`sprout double-spend: duplicate nullifier: Nullifier("...")`. The tests do not
pin the payload's rendering — only the prefix, the trailing phrase, and the fact
that the four strings are pairwise different.

`TransactionError::IncorrectFee` (step 10) is re-asserted here with its exact
string, `could not calculate the transaction fee`, because it is the variant
these four are added beside.

### 4.4 The producer, which is out of scope

`zebra-consensus/src/transaction/check.rs` is a private module; the function
that detects the conflicts and raises these variants is
`pub fn spend_conflicts(...)` inside it, and a private module is not reachable
from an integration test. Implement it as the code requires, but understand that
**only the error values are checked**. The tests construct each variant directly
and assert its properties.

### 4.5 The tests

1. **`transparent_double_spend_carries_the_outpoint`** — constructed from an
   outpoint with a known hash and index; the payload comes back equal, and a
   different index gives an unequal value.
2. **`sprout_double_spend_carries_the_nullifier`** — likewise for a Sprout
   nullifier.
3. **`sapling_double_spend_carries_the_nullifier`** — likewise for Sapling.
4. **`orchard_double_spend_carries_the_nullifier`** — likewise for Orchard,
   built through `TryFrom`.
5. **`the_four_variants_are_distinct_and_compare_by_value`** — the four are
   pairwise unequal, and equality is by value: two independently built
   `DuplicateSaplingNullifier`s over the same nullifier are equal.
6. **`transparent_display_names_the_pool`** — prefix and `is spent twice`.
7. **`sprout_display_names_the_pool`** — prefix and `duplicate nullifier`.
8. **`sapling_display_names_the_pool`** — prefix and `duplicate nullifier`.
9. **`orchard_display_names_the_pool`** — prefix and `duplicate nullifier`.
10. **`the_four_display_strings_are_pairwise_different`** — the pool name is what
    distinguishes them.
11. **`the_variants_are_debug_printable`** — `format!("{:?}", err)` is
    non-empty and contains the variant's own name. (`Clone` on `TransactionError`
    is part of §4.1's derive list; this case does not clone.)
12. **`earlier_variants_still_exist_beside_the_new_ones`** — `IncorrectFee`
    still renders exactly `could not calculate the transaction fee`, and the four
    pre-existing unit variants `TransactionError::{NoInputs, NoOutputs,
    CoinbasePosition, WrongVersion}` still exist and are still constructible as
    unit values from outside the crate. The case names all four, so renaming one
    or turning it into a struct variant does not compile this group.

---

## 5. The two carried-forward groups

### 5.1 The block corpus

The fixture set the verifier ships holds the step-10 corpus, copied byte for byte:
`blocks.tsv` (a 17-row TSV of expected values) and `blocks/*.hex` (one hex file
per block, 17 files). The driver reads them through the `HBV_FIXTURES`
environment variable, which the verifier sets to the shipped fixture directory.
The corpus spans mainnet genesis, v1 coinbases, Sprout JoinSplits, Overwinter
v3, Sapling v4 with shielded spends and outputs, and the four different readings
of the header's 32 reserved bytes across Sapling, Heartwood and Canopy. It is
not regenerated for this step; `zip244.tsv` from step 10 is **not** carried
forward, because no step-11 group reads it.

### 5.2 `inherited-block-codec` (GATE — adds no requirement, vetoes the step if it fails; `zebra-chain`)

Byte-identical to step 10's driver of the same name, and deliberately so: it
asserts nothing that is new at this tag. Every symbol it touches — `Block`,
`block::merkle::Root`, `transparent::Input::Coinbase`,
`ZcashSerialize`/`ZcashDeserialize`/`ZcashDeserializeInto` and
`SerializationError::Io` — has existed since step 01.

Its tests: `every_block_round_trips_byte_for_byte`,
`parsing_consumes_the_whole_block_and_no_more`,
`block_hash_matches_the_corpus`, `coinbase_height_matches_the_corpus`,
`the_first_transaction_of_every_block_is_a_coinbase`,
`the_merkle_root_is_the_root_of_the_transaction_hashes`,
`an_absurd_transaction_count_is_an_error_not_an_allocation`,
`a_truncated_block_is_an_io_error`.

Its purpose at this step is specific: this milestone edits
`Transaction`'s deserializer (§3.2), which every block parse runs through, so a
candidate that adds the ceiling in a way that truncates ordinary transactions
will break the round trip here. It is a **gate** group rather than a
requirement group precisely because a tree stopped at the previous milestone
passes it unchanged: preserving this round trip is necessary, and adds nothing.

### 5.3 `connection-limit-config` (`zebra-network`)

`zebra_network::Config` already has `pub peerset_initial_target_size: usize`
(default `50`), and `Config` is re-exported from the crate root. This milestone
adds three inherent methods, all `&self -> usize`:

```rust
impl Config {
    pub fn peerset_inbound_connection_limit(&self) -> usize {
        self.peerset_initial_target_size
    }

    pub fn peerset_outbound_connection_limit(&self) -> usize {
        let inbound_limit = self.peerset_inbound_connection_limit();
        inbound_limit + inbound_limit / constants::OUTBOUND_PEER_BIAS_DENOMINATOR
    }

    pub fn peerset_total_connection_limit(&self) -> usize {
        self.peerset_outbound_connection_limit() + self.peerset_inbound_connection_limit()
    }
}
```

and one new public constant:

```rust
// zebra_network::constants
pub const OUTBOUND_PEER_BIAS_DENOMINATOR: usize = 2;
```

The arithmetic is exact and normative:

- **inbound** is the target size unchanged.
- **outbound** is `inbound + inbound / OUTBOUND_PEER_BIAS_DENOMINATOR`, with
  **integer division that truncates**. An odd target size does not round the
  bias up.
- **total** is outbound plus inbound — the total is *not* `2 * inbound +
  inbound / denominator` written as one expression; it is the sum of the two
  accessors, which is the same thing but is what the test asserts.
- With the default target size of `50`: inbound `50`, outbound `75`, total
  `125`.

The outbound bias exists so a node is more likely to be connected to peers it
selected than to peers that dialled it; `OUTBOUND_PEER_BIAS_DENOMINATOR` is the
divisor in that bias, not a multiplier, so a *larger* value gives a *smaller*
outbound limit.

The tests, in `zebra-network/tests/`:

1. **`outbound_bias_denominator_is_two`** — `constants::OUTBOUND_PEER_BIAS_DENOMINATOR == 2`.
2. **`inbound_limit_equals_the_target_size`** — on a `Config::default()`.
3. **`default_config_targets_fifty_peers`** — the field is `50` and so is the
   inbound limit.
4. **`outbound_limit_is_biased_above_the_inbound_limit`** — the arithmetic, and
   `outbound > inbound`.
5. **`default_limits_are_fifty_inbound_and_seventy_five_outbound`** — the
   concrete numbers.
6. **`total_limit_is_inbound_plus_outbound`** — the sum, and `125` for the
   default.
7. **`limits_follow_a_configured_target_size`** — the assertions re-derived for
   target sizes `0, 1, 2, 3, 4, 10, 7, 100, 1000`; the limits track the field
   rather than being hard-coded.
8. **`odd_target_sizes_truncate_the_bias`** — target `3` gives inbound `3`,
   outbound `4`, total `7`; target `1` gives inbound `1`, outbound `1`.
9. **`a_larger_target_size_yields_strictly_larger_limits`** — a `Config` with
   target `20` permits strictly more outbound and total connections than one
   with target `10`.

The tests use only `zebra_network::{Config, constants}`. Nothing here is async
and nothing opens a socket.

---

## 6. Negative space — what this milestone does **not** ask for, and what is out of scope

### 6.1 The eviction policy and its host

Storage for the "recently evicted" set, the `mempooltxcostlimit` accounting, the
weighted random drop, the expiry sweep, the crawler and the inbound wiring all
live in `zebrad`, and no test reaches `zebrad` — every driver is injected
into `zebra-chain`, `zebra-consensus` or `zebra-network`. Build them as you see
fit; they are not checked, and the two pure accessors in §2 are the entire surface in scope
of ZIP-401 in this step.

### 6.2 The conflict detector

`zebra-consensus/src/transaction/check.rs` is a private module and
`spend_conflicts` is not reachable from outside the crate, so the logic that
*produces* the four errors is not checked. The errors themselves are, as values
(§4). Do not widen the module's visibility to make it testable — the
tests are written against the current shape and a widened module changes
nothing for them.

### 6.3 The connection limits' consumers

`ActiveConnectionCounter`, `ConnectionTracker` and the peer-set builder are
`pub(crate)` or lower, and the accept loop, the handshake and `init()` all need
a running Tokio reactor and a real listener. Only the `Config` arithmetic and
the public constant are reachable from an integration test, and only those are
checked.

### 6.4 Where the size cap actually bites in production

The behaviour in scope is what `Transaction::zcash_deserialize` does with an
oversized input (§3.3). The framing that decides how many bytes are handed to it
— `OutboundMessage` decoding, the peer-set buffer, the block downloader — is
unchanged by this milestone and out of scope.

### 6.5 Why these four drivers are discriminating

All four new drivers fail to **compile** against the predecessor milestone
(`v1.0.0-alpha.19`), which is the strongest available form of a negative
direction: a candidate who makes no change fails each, rather than
merely failing an assertion.

| Driver | Error against the predecessor |
| --- | --- |
| `hbv_zip401_cost_eviction` | no method named `cost` / `eviction_weight` found for struct `VerifiedUnminedTx` (E0599) |
| `hbv_tx_size_limit` | compiles — the assertion fails at run time instead: an oversized transaction parses successfully |
| `hbv_duplicate_spend_error_variants` | no variant named `DuplicateTransparentSpend` / `DuplicateSproutNullifier` / `DuplicateSaplingNullifier` / `DuplicateOrchardNullifier` in `TransactionError` (E0599) |
| `hbv_connection_limit_config` | no method named `peerset_inbound_connection_limit` found for struct `Config` (E0599), and cannot find value `OUTBOUND_PEER_BIAS_DENOMINATOR` in module `constants` (E0425) |

`hbv_tx_size_limit` is the one exception and it is deliberate: the constant it
reads, `MAX_BLOCK_BYTES`, already exists, so the file compiles everywhere and
the discrimination is in the assertion. Its control test
(`the_oversized_fixture_is_a_well_formed_encoding`) is what keeps that
assertion from being satisfiable by a deserializer that rejects everything.

The fifth driver, `hbv_inherited_block_codec`, compiles and passes against both
trees by construction (§5.2) — that is what "inherited" means here.

### 6.6 Not in this milestone

Halo2/RedPallas verification, the script interpreter, mining, wallet, RPC,
`lightwalletd`, Tor and NU6 remain out of scope per `REQUIREMENTS.md`.

---

## 7. Handoff

What step 12 may assume is present, and what it must not assume:

- `VerifiedUnminedTx::cost` and `eviction_weight` exist with the receivers and
  semantics in §2, and their constants are `4000` / `16000` / `1000`. A later
  milestone that moves the ceiling to `10000` changes *these* values and the
  tests for it will say so; nothing in this step exports them.
- `Transaction`'s deserializer is bounded by `block::MAX_BLOCK_BYTES` and
  reports the overrun as `SerializationError::Io`.
- `TransactionError` has the four pooled double-spend variants (§4.1) with the
  exact `Display` prefixes in §4.3, `IncorrectFee` still beside them, and the
  enum still `Clone + Debug + PartialEq + Eq`.
- `Config::{peerset_inbound,peerset_outbound,peerset_total}_connection_limit`
  exist with the arithmetic in §5.3, and
  `constants::OUTBOUND_PEER_BIAS_DENOMINATOR == 2`.
- `zebra-network`'s crate layout is unchanged, so the verifier's driver lands in
  the `tests/` directory it creates. If a `tests/` directory is already present,
  do not delete it.
- The block corpus the verifier ships with this step (`tests/assets/fixtures/`)
  is unchanged from the one step 10 used, copied byte for byte.
