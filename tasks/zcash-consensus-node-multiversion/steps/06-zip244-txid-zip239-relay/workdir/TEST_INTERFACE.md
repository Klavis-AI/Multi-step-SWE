# TEST_INTERFACE.md — milestone 6, ZIP-244 transaction identifiers, the v5
signature hash, and the ZIP-221 chain history tree

This document is the contract the tests are written against. Every
module path, type, method, field, constant, error variant and byte layout
the verifier depends on is named here. If a behaviour is not stated here or
in a specification document named by `instruction.md`, it is not in scope.

## 1. How the verifier reaches your code

The verifier runs seven groups. Five run **integration test files**, copied
into `zebra-chain/tests/`:

```
cargo test <flags> --jobs 4 -p zebra-chain --test <stem> -- --test-threads=1
```

where `<stem>` is the group name with every `-` turned into `_` and `hbv_`
prepended (`zip244-txid` → `hbv_zip244_txid.rs`). `<flags>` is chosen once,
in this order, by whichever is the first to make `cargo metadata` succeed
against `/app/Cargo.toml`: `--offline --locked`, then `--offline` alone,
then `--locked` alone, then neither. The other two groups run whole-workspace
commands with the same `<flags>` instead of a test file:

```
cargo build <flags> --jobs 4 -p zebra-chain --tests
cargo check <flags> --jobs 4 --workspace --all-targets --all-features
```

Consequences you must respect:

- The tests reach your code **only through the public API** of
  `zebra_chain`, as an external consumer. `pub(crate)`, `pub(super)` and
  private items are invisible to them.
- Each test file is compiled against `zebra-chain/Cargo.toml` **as you leave
  it**. Do not remove an existing `[dependencies]` or `[dev-dependencies]`
  entry, and do not add a new third-party dependency (the offline/locked
  resolution above must keep succeeding).
- The group literally named `build` gates every driver group: if it fails,
  every driver group is recorded failed with "workspace did not build",
  never skipped, and none of your other work counts.
- The verifier asserts the **exact executed test count** from `cargo test`'s
  own `running N tests` line against `groups.txt`'s `arg` for that group. A
  test that is filtered out, renamed away, or `#[ignore]`d fails the whole
  group even if everything that did run passed.
- Tests run with `--test-threads=1` and must not depend on execution order
  between `#[test]` functions in the same file.
- Fixtures are found through the `HBV_FIXTURES` environment variable, which
  the verifier sets to point at this step's shipped `fixtures/` directory —
  the same variable every driver below reads with
  `std::env::var("HBV_FIXTURES")`.

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

| Module | Declared as (already) | What this milestone adds or changes |
| --- | --- | --- |
| `zebra_chain::transaction::Transaction` | `pub enum Transaction` | `hash()` switches construction for `V5` (§3); `sighash()` gains v5 support (§4) |
| `zebra_chain::transaction::sighash` (re-exported items) | `HashType` already public; **`SigHash` is new at this milestone** (the re-export `pub use sighash::SigHash;` is part of this step's work) | no change to `sighash()`'s signature; the v5 code path inside it is new, and it now returns the wrapper `SigHash(pub [u8; 32])` |
| `zebra_chain::history_tree` | **does not exist before this milestone** | new module, `pub struct HistoryTree` and `pub enum HistoryTreeError` (§5) |
| `zebra_chain::block::Commitment`, `ChainHistoryMmrRootHash` | already public since an earlier milestone | unchanged; `HistoryTree::hash()` returns the same `ChainHistoryMmrRootHash` type a header's `Commitment::ChainHistoryRoot` already carries |

Internal file layout is yours; only the names below, at the paths given, are
in scope. `zebra_chain::lib.rs` must gain a `pub mod history_tree;` declaration
(or equivalent re-export) for §5 to be reachable at all.

## 3. ZIP-244 transaction identifiers — group `zip244-txid` (`zebra-chain`)

This group hand-loads ten published ZIP-244 v5 transaction vectors and
checks that `Transaction::hash()` reproduces each one's published identifier
— the values are never recomputed by the driver, only compared.

**Fixture**: `$HBV_FIXTURES/zip244.tsv`, one header row
(`# name\ttx_hex\ttxid_hex\tauth_digest_hex\ttransparent_input\tscript_code_hex\tamount\tsighash_all\tsighash_none\tsighash_single\tsighash_all_anyone\tsighash_none_anyone\tsighash_single_anyone`)
then ten data rows. `tx_hex` is the transaction's full wire serialization;
`txid_hex`/`auth_digest_hex` are 32-byte hex, in the same byte order
`transaction::Hash` stores internally (not the byte-reversed Display order);
a column holding `-` means "not applicable to this vector" (six of the ten
vectors sign no particular transparent input, so their `sighash_none`
onward columns read `-`). Provenance: the `zcash-test-vectors` project's
`zip_0244.py` generator, by way of upstream Zebra's own
`zebra-test/src/zip0244.rs` (PRs #2129, #2165), pinned at this
release; nothing in this column is derived by the implementation under test.

**Types and methods this group calls**:

- `Transaction::hash(&self) -> transaction::Hash` — for a `V5` transaction,
  must equal the vector's `txid_hex`. `transaction::Hash` prints
  byte-reversed via `Display` (the same big-endian convention as
  `block::Hash`) and its `FromStr` inverts exactly that; `Hash`'s own byte
  representation — the drivers read the payload as the tuple field `Hash.0` — is in
  the same order the vector's `txid_hex` column already uses.
- `Transaction::zcash_deserialize`/`zcash_serialize` — every vector's
  `tx_hex` must round-trip byte-for-byte.
- `Transaction::version(&self) -> u32` and
  `Transaction::network_upgrade(&self) -> Option<NetworkUpgrade>` — every
  vector is a v5 transaction naming `NetworkUpgrade::Nu5`.
- `NetworkUpgrade::Nu5.branch_id()` — this release's NU5 consensus branch id
  is `0xf919a198`.
- `NetworkUpgrade::current(network: Network, height: Height) -> NetworkUpgrade`
  — used only to confirm the corpus's own assumption that mainnet has no
  configured NU5 activation height yet at this release (height 1,687,104
  still resolves to `NetworkUpgrade::Canopy`), so the vectors below reach
  NU5 only because each transaction names it directly in its own
  `network_upgrade` field.
- `zebra_chain::serialization::sha256d::Writer` — the crate's own
  double-SHA256 writer, used only as a basic sanity check that it still
  produces a 32-byte digest; its output is not compared against
  `Transaction::hash()` by this test. The property that a v5 identifier is
  *not* SHA256d of the serialization is instead proved by a hand-written
  `sha2::{Digest, Sha256}` double-hash the driver computes independently of
  the crate's own hashing code (function `independent_sha256d`), which is
  also what reproduces the v1-v4 identifier rule against 50+ real pre-v5
  transactions pulled from `blocks.tsv`/`blocks/*.hex` below.

**Behavioural requirements**: `Transaction::hash()` on a `V1`-`V4`
transaction is unchanged — still SHA256d over the transaction's own
`zcash_serialize` bytes (checked against 50+ real pre-v5 transactions
pulled from this step's shared `blocks.tsv`/`blocks/*.hex` corpus, hashed
independently in the driver, not by the crate). On a `V5` transaction with
any authorizing data, `hash()` must differ from that same independent
SHA256d of the serialization (checked on at least 8 of the 10 vectors), and
must differ from the vector's own `auth_digest_hex` whenever the two are
not already equal in the vector table.

## 4. The v5 ZIP-244 signature hash — group `v5-sighash-zip244` (`zebra-chain`)

Reuses `zip244.tsv` (§3) for its `sighash_*` columns. (`zip244-txid`'s
driver parses every column — its `Vector` carries all six `sighash_*` fields —
but asserts on none of them, so dropping the columns would fail *this* group,
not that one.) Four of the ten vectors name a transparent input
(`transparent_input`, `script_code_hex`, `amount` all populated) and carry
all six signature hashes; the other six carry only `sighash_all`.

**Types and methods this group calls**:

- `Transaction::sighash(&self, network_upgrade: NetworkUpgrade, hash_type: HashType, input: Option<(u32, transparent::Output)>) -> SigHash`
  — called with `NetworkUpgrade::Nu5` for every vector in this group.
- `SigHash` is `pub struct SigHash(pub [u8; 32])`; `.0` is the digest, in the
  same byte order the vector's `sighash_*` columns print it (no
  byte-reversal, unlike `block::Hash`/`transaction::Hash`'s `Display`).
- `HashType` is a `bitflags`-derived `pub struct HashType: u32` with
  `const ALL = 0b0000_0001`, `const NONE = 0b0000_0010`,
  `const SINGLE = ALL.bits | NONE.bits`, `const ANYONECANPAY = 0b1000_0000`
  — combined with `|`, e.g. `HashType::ALL | HashType::ANYONECANPAY`.
- `transparent::Output { value: Amount<NonNegative>, lock_script: transparent::Script }`
  and `transparent::Script::new(bytes: &[u8]) -> Script` — the `input`
  argument is built from a vector's own `transparent_input` (the index),
  `script_code_hex` (raw scriptCode, no length prefix — `Script::new` does
  *not* add one when you build the script — and a row may carry an **empty
  column**, which means a genuinely empty script rather than a missing value;
  only the literal `-` marks an absent cell)
  not add one) and `amount` (zatoshi, non-negative).

**Behavioural requirements**: for every vector naming a transparent input,
all six of `ALL`/`NONE`/`SINGLE`, each with and without `ANYONECANPAY`, must
equal the vector's corresponding column exactly. For a vector naming no
input, `sighash(Nu5, HashType::ALL, None)` must equal both the vector's
`sighash_all` column and the vector's own `txid_hex` (ZIP-244 defines the
no-input `SIGHASH_ALL` as the transaction digest itself). Changing the
`nHashType`, the signed input index, or the spent amount by one zatoshi
must each change the resulting digest (checked by direct inequality, not by
a second expected value).

**The digest those columns encode, component by component.** The
assertions in scope compare against those bytes, so implement this construction.

**The copy of `zip-0244.rst` under `/app/specs/` describes a later revision of
this digest than the one those vectors encode.** Its "S.2" section lists a
`hash_type` byte as the first component, `amounts_sig_digest` and
`scriptpubkeys_sig_digest` digests over every input of the transaction, and a
`txin_sig_digest` whose fields are ordered value-before-scriptPubKey. None of
those is part of the digest at this band, and following that text produces
digests none of the vectors match. Where the shipped ZIP text and this
document disagree, **this document is normative** (the same
situation as `zip-0401.rst`'s later values at a later milestone).

For a v5 transaction with a transparent input to sign — `input =
Some((index, output))`, the `output` being the spent coin, i.e. the caller's
`script_code`/`amount` columns — the signature hash is

```text
BLAKE2b-256("ZcashTxHash_" || branch_id, header_digest ||
            transparent_sig_digest || sapling_digest || orchard_digest)
```

where `header_digest`, `sapling_digest` and `orchard_digest` are the
transaction identifier's own (§3), and `transparent_sig_digest` —
personalized `"ZTxIdTranspaHash"` — is a hash over **four** components, in
this order:

| Component | What is hashed |
| --- | --- |
| `prevout_digest` | `T.2a`'s digest: `"ZTxIdPrevoutHash"` over every input's 36-byte outpoint. Under `ANYONECANPAY`, the *empty* form instead — the same personalization over no bytes at all. |
| `sequence_digest` | `T.2b`'s digest: `"ZTxIdSequencHash"` over every input's 4-byte little-endian sequence. Under `ANYONECANPAY`, `NONE` or `SINGLE` (the low five bits), the empty form instead. |
| `outputs_digest` | `T.2c`'s digest: `"ZTxIdOutputsHash"` over every output's `value (8-byte LE) || scriptPubKey (compactSize length + bytes)`. Under `NONE`, the empty form. Under `SINGLE`, the digest of the **one** output at `index` (and the empty form when the transaction carries no output at that index). |
| `txin_sig_digest` | `BLAKE2b-256("Zcash___TxInHash", prevout || scriptCode || value || sequence)` for the input at `index`: that input's 36-byte outpoint, then the spent coin's script as a script *inside a TxOut* — its compactSize length followed by its bytes, so an empty script is the single byte `0x00` — then the spent value as 8-byte little-endian **signed**, then that input's 4-byte sequence. **Script code before value**, whichever order the ZIP's field list prints. |

Two consequences of that shape are worth stating outright, because they are
where a ZIP-faithful implementation goes wrong: the hash type enters only by
emptying or selecting those components — no `hash_type` byte is hashed, so
`NONE` and `SINGLE` produce the *same* digest on a transaction with no
transparent outputs (`zip244-00`'s `sighash_none` and `sighash_single` columns
are equal for exactly that reason, as are its two `ANYONECANPAY` columns) —
and no digest over all inputs' amounts or scriptPubKeys exists at all: the
signed input's own value and script code are committed to by `txin_sig_digest`,
and by nothing else.

## 5. `HistoryTree`, the ZIP-221 runtime — group `history-tree-contract` (`zebra-chain`)

**Module**: `zebra_chain::history_tree` (new this milestone; absent
entirely from step 5's tree — a requirement group by construction).

**Type**, all fields private:

```rust
pub struct HistoryTree { /* private */ }

#[derive(Debug, thiserror::Error)]
#[non_exhaustive]
pub enum HistoryTreeError {
    InnerError { inner: zcash_history::Error },  // #[non_exhaustive]
    IOError(#[from] std::io::Error),
}
```

**Public methods** (exact signatures):

```rust
impl HistoryTree {
    pub fn from_block(
        network: Network,
        block: Arc<Block>,
        sapling_root: &sapling::tree::Root,
        orchard_root: Option<&orchard::tree::Root>,
    ) -> Result<Self, std::io::Error>;

    pub fn push(
        &mut self,
        block: Arc<Block>,
        sapling_root: &sapling::tree::Root,
        orchard_root: Option<&orchard::tree::Root>,
    ) -> Result<(), HistoryTreeError>;
    // Panics if `block`'s coinbase height is not exactly the tree's current
    // height + 1. The panic message must mention the height it was *given*
    // (the offending one), which the case checks by looking for that
    // height's decimal digits.

    pub fn try_extend<'a, T>(&mut self, iter: T) -> Result<(), HistoryTreeError>
    where
        T: IntoIterator<Item = (Arc<Block>, &'a sapling::tree::Root, Option<&'a orchard::tree::Root>)>;

    pub fn hash(&self) -> ChainHistoryMmrRootHash;
}

impl Clone for HistoryTree {}  // an independent snapshot, not a shared handle
```

Note `from_block` returns a plain `std::io::Error`, not `HistoryTreeError`
— the two error types are not interchangeable and a driver must not assume
one converts to the other.

That `InnerError` variant names `zcash_history::Error` because that crate's
own error type is what the ZIP-221 MMR library returns, and **it is already
in this workspace's dependency graph**: `zcash_history` is declared in
`zebra-chain/Cargo.toml` and pinned in `Cargo.lock` (a `librustzcash` git
dependency) from the earlier milestone that added the MMR wrapper, so a
crate in this workspace can name `zcash_history::Error` directly with **no
`Cargo.toml` edit and no `Cargo.lock` change** — do not add an entry for it,
and do not try to pin it to a registry version, because there is none
(`zcash_history` is published only as the git revision the lockfile already
records). `sapling::tree::Root` is
`pub struct Root(pub [u8; 32])`, directly constructible; the third argument
of every method above is Orchard's analogous root and is accepted but not
yet incorporated into the digest at this milestone (the driver passes
`None` throughout; §5's hash-tree section, the same section this sentence
sits in, states why).

**Fixtures**:

- `$HBV_FIXTURES/blocks.tsv` + `$HBV_FIXTURES/blocks/*.hex` — the shared
  17-block corpus every group in this step can use; this group only touches
  `main-0903000` (mainnet height 903,000, the Heartwood activation block)
  and `main-0903001` (the very next mainnet block). `blocks.tsv`'s
  `root_bytes_hex` column is the raw 32-byte commitment field out of each
  block's own mined header — for `main-0903001` this is the real
  `ChainHistoryRoot` a full node computed; for `main-0903000` it is
  ZIP-221's all-zero reserved placeholder (the activation block commits to
  nothing usable, by design).
- `$HBV_FIXTURES/history_tree.tsv`, header
  `# name\tnetwork\theight\tsapling_final_root_hex`, two rows
  (`main-0903000`, `main-0903001`). `sapling_final_root_hex` is each block's
  real historical Sapling final root (as a full node's `getblock <height> 1`
  RPC would print in its `finalsaplingroot` field), byte-reversed into wire
  order the same convention `blocks.tsv`'s own hash columns use. This is the
  one input `from_block`/`push` need that a block's own bytes do not carry;
  nothing in this file is computed by the driver or the crate.

**Behavioural requirements**: a tree built by `HistoryTree::from_block`
seeded with `main-0903000` and its own Sapling final root must, after
`.hash()`, equal `ChainHistoryMmrRootHash::from(<main-0903001's
root_bytes_hex>)` — the real value the next block's own header commits to,
read independently of the tree. The same fact must hold in reverse:
`main-0903001.commitment(Network::Mainnet)` must equal
`Commitment::ChainHistoryRoot(tree.hash())`. Passing a wrong Sapling root to
`from_block` must produce a different, non-matching hash (the argument may
not be silently ignored). `push`-ing `main-0903001` onto the seeded tree
must change `.hash()`; pushing a block whose height does not equal the
current height + 1 (this driver reuses the activation block itself) must
panic, with the offending height mentioned in the panic message.
`try_extend` over exactly the one next block must produce the same
`.hash()` as the equivalent `push`; `try_extend` over zero blocks must
change nothing. `Clone` must produce an independent value: mutating the
original after cloning must not change the clone's `.hash()`.

**Why no test replays a decode-level `Commitment`/`CommitmentError` check**:
`Block::commitment(&self, network: Network) -> Result<Commitment, CommitmentError>`,
its `Commitment::ChainHistoryRoot(ChainHistoryMmrRootHash)` variant, and the
`InvalidPreSaplingReserved`/`InvalidChainHistoryActivationReserved` rejection
paths are already exhaustively covered as *inherited* regression groups from
step 2 (offset/decode checks, per-era rejection) and step 5 (exact-value
`ChainHistoryRoot` comparison against three real blocks). Retesting that
decode surface under a group in scope here would claim for this milestone
behaviour it did not add; this group is scoped to the one
capability that is actually new — building a tree at runtime.

## 6. ZIP-143/ZIP-243 signature hashes across the rewrite — group `sighash-zip143-zip243-migration` (`zebra-chain`)

This group is a requirement group, not a gate, and the distinction is the point of it.
It reads its digests off `Transaction::sighash`, whose return type THIS
milestone changes from a raw BLAKE2b state to the `SigHash` newtype (§4).
Against the predecessor tree it does not compile at all, so it cannot be
satisfied by inherited work and does not belong in the gate; what it covers
is the migration — that you carried the four pre-NU5 upgrades through the
rewrite intact. The inherited end-to-end path is gated separately by §8.
This milestone rewrites `Transaction::sighash` to route every post-Overwinter
network upgrade — including the new v5/NU5 case (§4) — through a shared
bridge, and changes its return type from a raw BLAKE2b state to the
`SigHash` newtype (§4). This group exists to prove that rewrite left the
four pre-NU5 upgrades untouched.

**Fixture**: `$HBV_FIXTURES/sighash.tsv`, header
`# name\tzip\tnetwork_upgrade\thash_type\tinput_index\tscript_hex\tamount\traw_tx_hex\texpected_sighash`,
five rows lifted from the normative text of ZIP-143 and ZIP-243 themselves
(the `.rst` sources), not from any implementation of them. `input_index =
-1` marks a vector signing no particular input (no `script_hex`/`amount` to
supply); `script_hex` is the raw scriptCode with no compactSize prefix.

**Types and methods this group calls**: the same
`Transaction::sighash`/`SigHash`/`HashType`/`transparent::Output`/
`transparent::Script::new` surface as §4, called instead with
`NetworkUpgrade::Overwinter`, `::Sapling`, `::Blossom`, `::Heartwood` or
`::Canopy`, matching each vector's `network_upgrade` column.

**Behavioural requirements**: all five vectors' `expected_sighash` values
must still be reproduced exactly, in the byte order the ZIPs print them
(not reversed). Changing `nHashType`, the signed input index, or the spent
amount/scriptCode by the smallest possible unit must each change the
digest. Signing the same v4/Sapling transaction under all five upgrades
that have a consensus branch id must yield five distinct digests, with only
the `Sapling` one matching the ZIP-243 vector — this is what proves the
consensus-branch replay protection ZIP-143 introduced survived the v5
rewrite.

## 7. `build` and `workspace-checks` (both GATE — add no requirement, veto the step if they fail)

`build`'s `arg` is `build -p zebra-chain --tests`; `workspace-checks`'s is
`check --workspace --all-targets --all-features`. Both are whole-workspace
commands (§1), not injected test files, and both are proof-of-life gates:
they add no requirement, and a failure of either fails the step outright.

`--all-features` is not extra strictness here, it is required for the
*retained* code to type-check at all: `zebra-chain/benches/block.rs`
unconditionally imports `zebra_test::vectors::BLOCK_TESTNET_141042_BYTES`
and `zebra_chain::block::tests::generate::*`, both of which are only
available when the crate's `bench` (or `proptest-impl`) Cargo feature is
enabled — `zebra-test` is an `optional = true` dependency gated behind
exactly those features. A bare `cargo check --workspace --all-targets`
therefore fails to compile this bench on this and every later release, with
no change required to trigger it; do not "fix" this by deleting or
feature-gating the bench yourself, and do not be surprised that the
workspace check needs `--all-features` where an earlier step's may not have.

## 8. Regression guard — group `inherited-block-codec` (GATE — adds no requirement, vetoes the step if it fails; `zebra-chain`)

This is the step's inherited end-to-end path, and the only group here that a
correct no-op would already satisfy. Block serialization must not regress
while you rewrite `Transaction::sighash` and add the runtime history tree.
The driver re-checks, over the same 17-block mainnet/testnet corpus used
elsewhere:

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

It touches no symbol introduced at this step — which is what makes it usable
as a gate. §6, by contrast, reaches the digests through this step's own
`SigHash` type and so is a requirement group rather than a gate.

**Fixture**: `$HBV_FIXTURES/blocks.tsv` and `$HBV_FIXTURES/blocks/`, the same
corpus steps 5 and 7 use, byte-identical to theirs.

## 9. Negative space — what this milestone does not ask for

- **The `zebra-state` Sprout-nullifier duty, as a *checked* behaviour.** The
  instruction requires that duplicate-Sprout-nullifier rejection and
  rejected-parent/child pruning keep working — the release patch does change
  `zebra-state` — but **no group in this step reaches `zebra-state`**: there
  is no `zebra-state` driver here at all, so that preservation duty is
  instruction-level only and is charged, if anywhere, by a later step. It is
  recorded here rather than left as a silent gap.

- **ZIP-244's `auth_digest`.** No function in `zebra_chain`'s public surface
  is asked to compute the authorizing-data commitment at this milestone; the
  `auth_digest_hex` column in `zip244.tsv` is carried only as a published
  value to distinguish it from the txid (§3's
  `the_txid_is_not_the_auth_digest` test), never as a value this driver
  asks the crate to reproduce.
- **ZIP-239's `MSG_WTX` inventory type — not driven at all.** This release
  adds `InventoryHash`'s wire codec support for `MSG_WTX` and the constants
  it needs, but every one of them lives inside `zebra-network`'s `protocol`
  module, and that module is declared `mod protocol;` (not `pub mod`) in
  `zebra-network/src/lib.rs`, unconditionally, with no feature or cfg that
  changes it. `InventoryHash` and its codec are therefore unreachable from
  any test compiled as an external consumer of `zebra_network` — confirmed
  by attempting the import (`error[E0603]: module 'protocol' is private`)
  and by grepping the whole workspace for any other crate reaching it
  (there are none; `zebra-network`'s own public `Request`/`Response` enums
  use `zebra_chain::block::Hash`/`transaction::Hash` exclusively, never
  `InventoryHash`). The upstream changelog itself describes this item as
  "parse and ignore" — there is no behavioural change to observe even if
  the type were reachable. No group in this step touches `zebra-network`.
- **Orchard spend-authorization verification and full contextual validation
  of a v5-with-Sapling transaction.** Both live inside
  `zebra_consensus::transaction`, which is `mod transaction;` (not `pub
  mod`) in `zebra-consensus/src/lib.rs` — as private and as unreachable as
  `zebra-network`'s `protocol` module above. The only public entry point
  that indirectly exercises that code, `zebra_consensus::chain::init`, would
  require constructing genuine RedPallas/Groth16 proof and signature
  material to drive a real accept/reject outcome — the same "pinned
  external primitive" class of work this corpus has excluded at every
  earlier step (`REQUIREMENTS.md`'s shared out-of-scope list). Neither
  capability has a group here.
- **Duplicate Sprout nullifier rejection and rejected-parent/child pruning
  in `zebra-state`.** Both are real, named changes in this upstream release
  (`ValidateContextError::DuplicateSproutNullifier` is public, reachable
  through `zebra_state::init` the same way step 4's Orchard-nullifier state
  group already reaches the finalized state), and are not, on inspection,
  architecturally unreachable the way the two items above are. They are
  deliberately not given a dedicated group in this authoring pass: this
  step's requirement groups are scoped to the three structural, data-layer changes
  that are this milestone's own headline (the ZIP-244 identifier split, the
  v5 signature hash, and the `HistoryTree` runtime), matching this step's
  directory name and `REQUIREMENTS.md`'s "two identifiers per transaction;
  relay uses the witnessed hash" migration line; extending coverage into
  `zebra-state`'s block-commit path would repeat step 4's own
  `orchard-state-commit` testing pattern rather than add new interface
  surface, and would push this step past the "aim for 7-10 groups"
  guidance for a step that (unlike step 1) is not the whole baseline. A
  reviewer who considers this gap disqualifying should treat it as a
  candidate follow-up group for `zebra-state`, not as a defect in the three
  groups that are here.
- **`Block::commitment`/`Commitment`/`CommitmentError` decode-level
  behaviour.** Already inherited coverage from steps 2 and 5 (§5); not
  repeated here.
- Everything already out of scope per `REQUIREMENTS.md`'s "Explicit
  out-of-scope decisions" list (the script interpreter, proof-system and
  signature-scheme internals, mining/wallet/RPC, `lightwalletd`, Tor, NU6)
  remains out of scope.

## 10. Handoff

**Inherited from steps 1-5**: the V1-V5 transaction codec, ZIP-225's
Sapling/Orchard v5 layout, Orchard nullifiers retrievable from
`zebra-state`, the ZIP-211 Sprout pool freeze, an async batched RedPallas
verifier, and a block header that can already *decode* a `ChainHistoryRoot`
commitment — all unchanged in shape, exercised again here (§6, and §5's
reuse of `blocks.tsv`) only to prove this milestone's rewrite of `sighash`
and addition of `history_tree` did not disturb them.

**What step 6 leaves for step 7**: `Transaction::hash()` and
`Transaction::sighash()` that are correct for every transaction version
this workspace can parse, including v5 under ZIP-244; a
`zebra_chain::history_tree::HistoryTree` that can be seeded and extended
from real block data and whose `.hash()` agrees with a real header's own
`ChainHistoryRoot` commitment; and `zebra-state`'s existing (if here
untested) duplicate-Sprout-nullifier and rejected-parent/child behaviour
left exactly as this release shipped it. Step 7
(`REQUIREMENTS.md`: UTXO double spends rejected, `ValueBalance` and `Sum for
Amount`, incremental note commitment trees and anchors in state, duplicate
Sapling/Orchard nullifiers, coinbase maturity) is expected to find all of
the above in place and unchanged in its public shape — in particular, it
extends this step's Sprout-only duplicate-nullifier rule to Sapling and
Orchard rather than introducing nullifier bookkeeping from scratch, and it
does not need to re-derive a transaction's identifier or its signature hash
to do so.
