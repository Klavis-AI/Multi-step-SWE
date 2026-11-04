# Step 10 — Mempool storage: sizes, fees, expiry, and a tip that changes

## Implement this milestone

`/app` already builds and passes its own tests. Step 9 gave the node an
admission surface: an unmined transaction has an identifier that survives
malleability, the verifier accepts a `Request::Mempool` against the height of
the next block, and a component can read the chain tip through a trait instead
of a service. That is enough to *accept* a transaction. It is not enough to
**keep** one.

A mempool that keeps transactions has to answer three questions the node cannot
answer today. How big is this transaction, in bytes, without serializing it into
a buffer you then throw away? What did it pay, and is that fee still attached to
it after verification finishes? And when does it stop being a candidate — either
because it expired at a height, or because the chain tip moved out from under
it? This milestone adds exactly those answers, and moves two NU5 consensus
parameters that the release also carries.

1. **Measure a serialized object without allocating it.**
   `ZcashSerialize` gains a provided method `zcash_serialized_size()`, built on a
   new public `serialization::FakeWriter(pub usize)` — an `io::Write` that
   counts bytes and keeps none. Every type that already serializes gets the
   measurement for free, and the count must equal the length of the bytes
   `zcash_serialize` would have produced.
2. **Carry the size and the fee with the transaction.** `UnminedTx` gains a
   public `size: usize` field, filled in by every conversion that builds one, so
   an entry's cost is computed once at admission. A new
   `transaction::VerifiedUnminedTx` pairs an `UnminedTx` with the
   `Amount<NonNegative>` it paid — the value the mempool orders and prices
   entries by. Both, and `Transaction` and `transparent::Input`, get `Display`
   impls that summarise rather than dump, because these types now appear in
   logs.
3. **Implement the ZIP-203 zero-expiry sentinel.**
   `Transaction::expiry_height()` returns `Option<block::Height>`, and an
   `nExpiryHeight` field of `0` means *no expiry*, not *expires at height 0*.
   A transaction with a zero expiry height is valid forever; the field still
   travels on the wire unchanged. v1 and v2 have no such field at all.
4. **Name the outpoints a transaction spends.**
   `Transaction::spent_outpoints()` iterates the `transparent::OutPoint`s the
   transaction's `PrevOut` inputs reference, skipping coinbase inputs, which
   spend nothing. This is the key a mempool detects conflicting spends with.
5. **Make the verifier's answer carry the fee.** `zebra_consensus::transaction::Response`
   stops being an alias for `UnminedTxId` and becomes a two-variant enum
   mirroring the request: a `Block` reply carries the id and an *optional* miner
   fee (a coinbase transaction has none), a `Mempool` reply carries the whole
   `VerifiedUnminedTx`. `TransactionError` gains `IncorrectFee`.
6. **Publish the chain-tip *change* stream.** `zebra_state` re-exports
   `ChainTipChange` and `TipAction` from its crate root.
   `TipAction::Reset { height, hash }` and `TipAction::Grow { block }` are the
   two things a mempool must react to; `is_reset`, `best_tip_hash` and the new
   `best_tip_height` read an action regardless of variant, and the new
   non-blocking `ChainTipChange::last_tip_change()` reports a change if one has
   happened since it was last called and `None` otherwise.
7. **Move the two NU5 parameters this release moves.** The NU5 consensus branch
   id becomes `0x37519621`, replacing `0xf919a198`; a v5 transaction naming the
   old id must now be rejected. NU5 gains a Testnet activation height of
   `1_599_200`. Mainnet still has none.

   > **The shipped specification disagrees with this milestone here, and this
   > milestone wins.** `/app/specs/nu5.txt` is the *final* NU5 protocol
   > specification: it names the branch id `0xF919A198` and says NU5 activated
   > on Mainnet at height `1687104`. This milestone reproduces a release made
   > while NU5 was still unactivated and still being specified, and its branch
   > id and activation heights moved more than once before they were settled.
   > For these three values — the NU5 branch id, the NU5 Testnet activation
   > height, and the absence of a NU5 Mainnet activation height — take the
   > numbers from `TEST_INTERFACE.md` §6, not from the specification. Every
   > other value in the spec, including the branch ids of Overwinter through
   > Canopy and everything in §7.1, is normative as written.

Then build the mempool itself on top: storage keyed by `UnminedTxId`, expiry by
height, conflicting-spend rejection, and a subscription to the tip change. That
component lives in `zebrad` and is not checked directly — see the out-of-scope
list below — but the surface above exists to serve it, and the next milestone
assumes it is there.

## Read these local references

- `/app/specs/nu5.txt` §7.1 "Transaction Encoding and Consensus" — the
  `nExpiryHeight` field, its range, and the "or 0 to disable expiry" rule; the
  same section's consensus rule on expiry-height enforcement.
- `/app/specs/nu5.txt` §6 "Network Upgrades", and the consensus branch id
  bullets at the end of §4.10 "SIGHASH Transaction Hashing" — how a branch id
  binds a transaction to an upgrade. §7.1's v5 transaction header table places
  `nConsensusBranchId` in the third four-byte word, after `header` and
  `nVersionGroupId` and before `lock_time` and `nExpiryHeight`.
  **Read the warning under requirement 7 before you copy a branch id or an
  activation height out of any of them.**
- `/app/specs/zips/zip-0252.rst` "Deployment of the NU5 Network Upgrade" — the
  note beneath its network-upgrade constants records the *first* NU5 Testnet
  activation, at height `1599200` with consensus branch ID `0x37519621`. That is
  the activation this milestone reproduces, and it is why `nu5.txt` §4.10's
  bullet — which mandates `0xF919A198` "as defined in [ZIP-252]" — is stale
  against the very ZIP it cites. `TEST_INTERFACE.md` §6.0 works this through,
  including which of ZIP-252's own constants are *not* this milestone's values.
- `/app/TEST_INTERFACE.md` — the contract the tests are written against:
  every module path, type, signature, field, trait impl, constant and error
  variant they touch, plus the exact `Display` field layouts. Read it before
  changing the shape of `Response` or `UnminedTx`.
- `/app/WORKSPACE_HANDOFF.md` — what carries forward, and what step 11 expects.
- Retained code this milestone extends:
  `zebra-chain/src/serialization/zcash_serialize.rs`,
  `zebra-chain/src/transaction/unmined.rs`, `zebra-chain/src/transaction.rs`,
  `zebra-chain/src/transparent.rs`,
  `zebra-chain/src/parameters/network_upgrade.rs`,
  `zebra-consensus/src/transaction.rs`, `zebra-state/src/lib.rs`,
  `zebra-state/src/service/chain_tip.rs`.
  `rg -n 'pub type Response' zebra-consensus/src/transaction.rs` finds the alias
  that becomes an enum.

## Explicit out-of-scope decisions (this step)

`TEST_INTERFACE.md` §10 restates these as what the tests can and cannot see.

- **The mempool component itself.** Storage, expiry sweep, rejection lists,
  crawler and inbound wiring live in `zebrad`, which no group in this step
  covers — every driver is injected into `zebra-chain`, `zebra-consensus` or
  `zebra-state`, never into `zebrad`. Build them as you like; what
  is in scope is the `zebra-chain`, `zebra-consensus` and `zebra-state` surface
  they are written against.
- **ZIP-401 cost, eviction and the random-weighted drop.** A size and a fee are
  provided here; the *policy* that spends them is step 11.
- **`TipAction::Grow`'s payload.** `ChainTipBlock` and `ChainTipSender` stay
  behind `#[cfg(any(test, feature = "proptest-impl"))]`, and a tip only advances
  once blocks are committed, so the `Grow` variant is not constructible from a
  test. `Reset`, the variant-independent accessors and the quiescent
  behaviour of a fresh `ChainTipChange` are in scope; `Grow` is not.
- **Actually running the transaction verifier.** `Verifier::new` takes a
  `script::Verifier` and `zebra_consensus::script` is private, so the service is
  unconstructible from a test. The response type, its accessors, its conversion
  and the error variant are in scope; the verification is not.
- **Where the miner fee is computed.** `Response::Block`'s fee and
  `VerifiedUnminedTx::miner_fee` are in scope as values carried; the value-balance
  arithmetic that produces them runs inside the unconstructible verifier.
- **Halo2/RedPallas verification**, the script interpreter, mining, wallet, RPC,
  `lightwalletd`, Tor and NU6 — still out of scope per `REQUIREMENTS.md`.

## Meta requirements

- **Budget and workspace.** `bash /app/timer.sh` shows elapsed and remaining
  time. `/app` carries forward in full (minus `.git/` and `target/`) to step 11,
  so leave it building.
- **Network.** This environment has no network access, and everything the
  build needs is already installed. The upstream project's source, its
  releases, mirrors and package registries are unreachable, so a copy cannot
  be fetched by any route. Searching for, downloading, copying, wrapping or
  invoking an implementation of this milestone — or any task-specific
  answer — is prohibited.
- **Workspace scope.** Tests are added to the `tests/` directories of
  `zebra-chain`, `zebra-consensus` and `zebra-state` when the milestone is
  checked, compiled
  against the manifests as you leave them. `--offline` must keep working: add no
  new third-party dependency, and remove no existing one. In particular
  `zebra-chain` and `zebra-state` must keep their `hex` dependency. (The
  upstream release this milestone tracks does add optional
  `proptest`/`proptest-derive` entries to `zebra-consensus`; a manifest entry
  that is already somewhere in the inherited `Cargo.lock` — as those are — is
  not a "new third-party dependency" in that sense, and nothing distinguishes
  the two either way.)
- **Local checks.** `cargo build --offline --workspace`, `cargo test --offline -p
  zebra-chain -p zebra-consensus -p zebra-state`, and `cargo check --offline
  --workspace --all-targets --all-features` (benches build under
  `--all-targets`).
- **Handoff.** Do not delete `/app/specs/`, `/app/TEST_INTERFACE.md`,
  `/app/WORKSPACE_HANDOFF.md` or `/app/timer.sh`.
