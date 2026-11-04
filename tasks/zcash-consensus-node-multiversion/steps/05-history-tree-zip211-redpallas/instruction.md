# Step 5 — ZIP-221 chain history, ZIP-211 Sprout freeze, batched RedPallas

## Implement this milestone

`/app` already builds and passes its own tests. Steps 1–4 gave you the V1–V5
transaction codec, the ZIP-225 Sapling-in-V5 layout, the Orchard bundle
ser/de with its accessors, Orchard nullifiers indexed in `zebra-state`, and
the `scriptCode` single-prefix fix.

What you inherit still treats a block and a transaction as isolated objects.
A header's 32-byte commitment field can be *read*, but the chain history it
commits to cannot be *built*; a V5 transaction carries `nConsensusBranchId`,
but nothing compares it to the block it arrived in; RedPallas signatures can
be verified one at a time, which is roughly an order of magnitude slower than
a node validating Orchard at chain speed can afford. Your milestone:

1. **Build the ZIP-221 chain history MMR.** Add
   `zebra_chain::primitives::zcash_history`, wrapping `zcash_history::Tree`
   from `librustzcash`. That crate is **not** in the workspace you inherit, so
   this milestone **adds** it, and the node arithmetic is typed over `bigint`;
   §1 of
   `TEST_INTERFACE.md` gives the manifest lines verbatim, including the git URL
   and rev — pin them exactly as written. Add no *other* new third-party
   dependency. It must turn a block plus its Sapling note commitment tree
   root into an MMR leaf node carrying the ZIP-221 node fields, append and
   truncate leaves, and produce the tree's root as the
   `hashChainHistoryRoot` the header commits to. Make
   `block::ChainHistoryMmrRootHash` constructible from the 32 bytes that root
   is, so a caller holding a root can compare it to a header's commitment
   (§3 of `TEST_INTERFACE.md`). One tree holds one network upgrade's history:
   ZIP-221 restarts the MMR at each activation. Note that the tree's own
   operations have no public entry point an external test can call at this
   tag, so **what this milestone requires is the root type and its relationship
   to a header's commitment, not the MMR's internals** (`TEST_INTERFACE.md`
   §3.1 and §8 say so explicitly); the module is required because the next
   milestones build their own public history-tree surface on it.
2. **Check a V5 transaction against its block's network upgrade.** Add
   `Block::check_transaction_network_upgrade_consistency`. A V5 transaction's
   `network_upgrade` field is derived from `nConsensusBranchId`, and ZIP-244
   §7.1 requires it to match the branch id the block's own height implies —
   otherwise a transaction signed for one upgrade can be replayed inside
   another's blocks. Give `Transaction` the header and pool accessors §4
   names, which this check and the two consensus rules below all read.
3. **Freeze the Sprout value pool (ZIP-211).** From Canopy onward, `vpub_old`
   MUST be zero in every JoinSplit, so no new value can enter the Sprout
   pool. Expose the `vpub_old` values a transaction moves into the pool
   (§4), and enumerate the rule in `zebra_consensus::error::TransactionError`
   (§6) together with the rule that rejects a V5 transaction before NU5
   activates.
4. **Verify RedPallas signatures in batches.** Add
   `zebra_chain::primitives::redpallas::batch`, a batch verifier that takes
   `SpendAuth` and `Binding` items in one queue and settles them with a
   single randomized multiscalar multiplication (§5), and wire it into
   `zebra-consensus` as an asynchronous verifier service alongside the
   existing Groth16, Ed25519 and RedJubjub ones.
5. **Return errors from state update methods.** `zebra-state`'s internal
   update paths should return `Result` rather than assert, so the contextual
   validation the next milestones add has somewhere to report to. No public
   request or response type changes, and none of it is checked directly — it is
   preparation the next step depends on.

Orchard *proof* validity stays out of scope; so does actually storing the
history tree in `zebra-state`. See the negative-space list below.

## Read these local references

- `/app/specs/zips/zip-0221.rst` — the chain history MMR: the node field
  layout, what a leaf commits to, and the rule that the MMR restarts at each
  network upgrade with the activation block's commitment field reserved.
- `/app/specs/zips/zip-0211.rst` — disabling addition of new value to the
  Sprout value pool, from Canopy.
- `/app/specs/nu5.txt` §5.4.7 "RedDSA, RedJubjub, and RedPallas" — the
  scheme, its two signature roles and the randomizer; **appendix B.1 "RedDSA
  batch validation"** — the batch verification equation goal 4 implements.
- `/app/specs/nu5.txt` §7.1 "Transaction Encoding and Consensus" — the
  `nConsensusBranchId` field and the rule that it MUST match the consensus
  branch id used for sighash (also stated in
  `/app/specs/zips/zip-0244.rst`).
- `/app/TEST_INTERFACE.md` — the contract the tests are written
  against. Every module path, type, method, field, constant, error variant
  and message string they touch is named there. Read it before you start.
- `/app/WORKSPACE_HANDOFF.md` — what carries forward from steps 1–4 and what
  step 6 expects to find.
- `zebra-chain/src/primitives/`, `zebra-chain/src/block/commitment.rs`,
  `zebra-consensus/src/transaction/check.rs` — the retained code this
  milestone extends. `rg -n 'ChainHistoryMmrRootHash' /app/zebra-chain`
  finds the type the MMR's root has to become.

## Explicit out-of-scope decisions (this step)

- **A persistent, public history tree.** The MMR is a primitive here, not
  state: nothing stores it, reloads it, or exposes it as a long-lived
  `HistoryTree`. Step 6 makes it a public runtime type; step 8 versions it on
  disk (`REQUIREMENTS.md`).
- **Validating a header's chain history commitment against a computed root.**
  This milestone must be able to *produce* a root and *read* a commitment; the
  consensus rule that the two agree is FlyClient work at step 8.
- **ZIP-244 TxId and the V5 sighash.** The `nConsensusBranchId` consistency
  check is in scope; the new digest algorithm it will eventually feed is step
  6's.
- **Halo2 and Orchard spend-auth verification.** This step adds the batch
  *machinery* for RedPallas. Running real Orchard spend-auth signatures
  through it is step 6; Halo2 proof verification is step 12.
- **Contextual validation of anything.** Nullifier double-spends, UTXO
  double-spends, anchors, value-pool bounds and coinbase maturity are all
  later steps. The state's error-returning refactor (goal 5) is groundwork
  only.
- Everything already listed under `REQUIREMENTS.md`'s "Explicit out-of-scope
  decisions" (script interpreter internals, proving-system internals,
  mining, wallet, RPC, `lightwalletd`, Tor, NU6) remains out of scope.

## Meta requirements

- **Budget.** `bash /app/timer.sh` shows elapsed and remaining time.
- **Cumulative workspace.** `/app` carries forward in full (minus `.git/` and
  `target/`) to step 6. Leave it building.
- **Network.** This environment has no network access, and everything the
  build needs is already installed. The upstream project's source, its
  releases, mirrors and package registries are unreachable, so a copy cannot
  be fetched by any route. Searching for, downloading, copying, wrapping or
  invoking an implementation of this milestone — or any task-specific
  answer — is prohibited.
- **Workspace scope.** Tests are added to `zebra-chain/`'s and
  `zebra-consensus/`'s `tests/` directories when the milestone is checked,
  compiled against
  the manifests as you leave them. `--offline` must keep working; do not add
  a new third-party dependency.
- **Local checks.** `cargo build --offline --workspace` and
  `cargo check --offline --workspace --all-targets --all-features` are the
  checks that matter most. Note the `--all-features`: `zebra-chain`'s
  `benches/block.rs` uses `block::tests::generate`, which is gated behind
  `#[cfg(any(test, feature = "bench"))]`, so a bare `--all-targets` check
  does not compile the benches.
- **Handoff.** Do not delete `/app/specs/`, `/app/TEST_INTERFACE.md`,
  `/app/WORKSPACE_HANDOFF.md` or `/app/timer.sh`.
