# Step 7 — Contextual validation: value pools, ordered UTXOs, note commitment trees

## Implement this milestone

Every check `/app` performs today is *semantic*: it can be decided by looking
at one block, or one transaction, on its own. Steps 1–6 gave you the wire
format, the sighash and txid algorithms, the Orchard bundle, the ZIP-221
history tree, and a finalized state that stores blocks and hands them back.
None of that can answer "is this spend legal?" — that answer is not in the
block, it is in the chain before it.

This milestone makes validation *contextual*. Four capabilities, each of which
needs information the block does not carry:

1. **A value-pool ledger.** Add `zebra_chain::value_balance::ValueBalance<C>`,
   a four-pool record — transparent, Sprout, Sapling, Orchard — that says how
   much value a transaction, or a block, moves into or out of each pool. Give
   `Transaction` a `value_balance(&utxos)` method that computes it. **The four
   pools do not share a sign convention** — §4.3 of `TEST_INTERFACE.md` gives
   all four; an implementation that negates all four, or none, is wrong in a
   way that only shows up across pools. The transparent pool is the reason
   this method takes an argument: it needs the outputs the inputs spend, and
   those live in earlier blocks.

2. **Amount arithmetic that can be summed.** `Amount<C>` gains `zero()`,
   negation for `Amount<NegativeAllowed>`, and `std::iter::Sum` for both
   by-value and by-reference iterators, collecting into
   `Result<Amount<C>, amount::Error>`. Summation must detect `i64` overflow
   *before* the range check can be applied, which needs a new error variant.

3. **Ordered UTXOs and the coinbase maturity rules.** A transaction may spend
   an output created by an *earlier* transaction in the same block, but not by
   a later one; once a block's outputs are in a `HashMap` that ordering is
   gone. Introduce `transparent::OrderedUtxo` — a `Utxo` plus the index of the
   transaction that created it — and the functions that build and flatten an
   index of them. Then add
   `Transaction::coinbase_spend_restriction(spend_height)`, the predicate the
   two transparent coinbase rules are written against: a coinbase output may
   only be spent by a transaction with **no** transparent outputs, and only
   once `MIN_TRANSPARENT_COINBASE_MATURITY` blocks have passed.

4. **Working note commitment trees.** `sapling::tree::NoteCommitmentTree` and
   `orchard::tree::NoteCommitmentTree` exist but cannot be appended to, cannot
   produce a root, and cannot count their leaves. Make them real incremental
   Merkle frontiers of depth 32, with the correct per-pool uncommitted leaf
   and empty-root values, so appending a block's note commitments reproduces
   the anchors that block's successors use.

Then thread the results into `zebra-state`: `PreparedBlock::new_outputs`
becomes an index of `OrderedUtxo` rather than `Utxo`; `ValidateContextError`
grows this milestone's failure modes (double spends, missing, immature or
unshielded coinbase spends, duplicate Sapling and Orchard nullifiers, negative
remaining transparent value, note commitment tree failures); and
`DATABASE_FORMAT_VERSION` steps, because a step-6 database holds none of it.

The checks that *raise* those error variants live behind `mod service;` and
are not directly reachable by the tests — but every type, constant,
predicate and conversion they are built from is, and that is what is in scope.
§8 of `TEST_INTERFACE.md` says which is which; do not read "out of scope" as
"not wanted".

## Read these local references

- `/app/specs/nu5.txt` §3.8 and §5.4.1.3/§5.4.1.4 (note commitment
  trees, `MerkleCRH^Sapling`/`MerkleCRH^Orchard`, and the uncommitted leaf
  values `Uncommitted^Sapling`/`Uncommitted^Orchard`); §3.4 "Transactions and
  Treestates" (the transparent value pool balance rule — "the remaining value
  in the transparent transaction value pool MUST be nonnegative", line 917);
  §7.1.2 "Transaction Consensus Rules" (the per-block rules around coinbase
  maturity, `valueBalanceSapling` and coinbase shielded outputs). The PDF the
  text was extracted from, `/app/specs/nu5.pdf`, is the same document with
  figures and is authoritative where the two differ.
- `/app/specs/zips/zip-0213.rst` — shielded coinbase. From Heartwood on, a
  coinbase may pay its reward to a Sapling address, so a coinbase's Sapling
  pool change is **not** automatically zero. Anything assuming "coinbase ⇒
  shielded pools untouched" is wrong on mainnet.
- `/app/TEST_INTERFACE.md` — the contract the tests are written
  against for this step. Every type, method, field, constant, error variant
  and sign convention they touch is named there. Read §4.3 before you write a
  line of the value-pool code: the sign conventions are the likeliest way to
  fail this milestone while looking correct.
- `/app/WORKSPACE_HANDOFF.md` — what carries forward from steps 1–6 and what
  step 8 expects to find.
- `zebra-chain/src/{amount.rs,transparent/utxo.rs,{sapling,orchard,sprout}/tree.rs}`
  and `zebra-state/src/{request.rs,error.rs,constants.rs}` — the retained code
  this milestone extends.

## Explicit out-of-scope decisions (this step)

- **Block-wide and chain-wide value pools.** This milestone computes the value
  balance of *one transaction*. Summing those across a block, the running
  chain pool, and the ZIP-209 rule that no pool may go negative are step 8.
  Do not enforce a chain-level pool bound here.
- **Anchor validation.** The trees are built and rooted here; checking a
  transaction's stated anchor against a root the chain actually had is not.
- **The finalized-state disk layout for the trees.** That the version number
  moves is required; which column families hold what is yours.
- **Sprout tree arithmetic.** `sprout::tree::NoteCommitmentTree::append` stays
  a stub at this milestone — appending commitments and updating a Sprout root
  is a later milestone, so do not build a real frontier here. Sprout's `Root`
  conversions *are* in scope, and so are the type's `Default` impl and its
  `root()` accessor: `sprout::tree::NoteCommitmentTree` is private today and
  must become `pub`, because the tests are an external crate. §5.4 of
  `TEST_INTERFACE.md` has the exact shape.
- **Proof, signature and script verification** — unchanged, still pinned
  external primitives (see `REQUIREMENTS.md`'s shared out-of-scope list).
- **Reorg logic and the non-finalized chain's value pools** — the constants
  bounding a reorg are in scope, the logic is not.

## Meta requirements

- **Budget.** `bash /app/timer.sh` shows elapsed and remaining time.
- **Cumulative workspace.** `/app` carries forward in full (minus `.git/` and
  `target/`) to step 8; leave it building.
- **Network.** This environment has no network access, and everything the
  build needs is already installed. The upstream project's source, its
  releases, mirrors and package registries are unreachable, so a copy cannot
  be fetched by any route. Searching for, downloading, copying, wrapping or
  invoking an implementation of this milestone — or any task-specific
  answer — is prohibited.
- **Workspace scope.** Tests are added to `zebra-chain/`'s and
  `zebra-state/`'s `tests/` directories when the milestone is checked,
  compiled against the
  manifests as you leave them. `--offline` must keep working. The trees need
  an incremental Merkle frontier, and `incrementalmerkletree` is **not**
  already a dependency of `zebra-chain` — add `incrementalmerkletree = "0.1.0"`
  to `zebra-chain`'s `[dependencies]` yourself. That one addition is expected
  and required; `--offline --locked` must still resolve afterwards, so pin the
  version exactly and add nothing else to any manifest.
- **Local checks.** `cargo build --offline --workspace`,
  `cargo check --offline --workspace --all-targets --all-features` and
  `cargo test --offline -p zebra-chain -p zebra-state`. The `--all-features`
  form is not optional: the benches do not compile without it.
- **Handoff.** Do not delete `/app/specs/`, `/app/TEST_INTERFACE.md`,
  `/app/WORKSPACE_HANDOFF.md` or `/app/timer.sh`. Step 8 builds block- and
  chain-wide value pools directly on this step's `ValueBalance`.
