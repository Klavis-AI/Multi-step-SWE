# Step 13 — Anchors, JoinSplit Groth16 proofs, external parameters, miner fees, lock times, and expiry heights

## Implement this milestone

`/app` already builds and passes its own tests: steps 1–12 gave you the V5
transaction codec, Orchard data structures and nullifier indexing, ZIP-244
sighash, the mempool, peer connection management, block subsidy and funding
streams, Halo2 proof verification, and the commitment-tree machinery. This
milestone adds the remaining consensus-check layer for pre-NU5 shielded
transactions and the infrastructure for external Groth16 JoinSplit parameters:

1. **Validate Sapling, Orchard, and Sprout anchors.** Every Sapling `spend`
   and `output` description carries an `anchor` field that must name a root in
   the note-commitment tree at the current chain tip (or, for Sprout, at each
   interstitial anchor within the block). Add the three anchor-validation
   functions and their corresponding `ValidateContextError` variants to
   `zebra-state`. The new variants are `UnknownSproutAnchor`,
   `UnknownSaplingAnchor`, and `UnknownOrchardAnchor`; all three are
   `#[non_exhaustive]` struct variants on the existing
   `ValidateContextError` enum. Three companion variants —
   `SproutNoteCommitmentTreeError`, `SaplingNoteCommitmentTreeError`, and
   `OrchardNoteCommitmentTreeError` — are added as `#[from]` tuple variants
   to propagate note-commitment-tree overflow errors.

2. **Wire JoinSplit Groth16 proofs.** V2/V3/V4 transactions that carry
   JoinSplit descriptions require Groth16 proof verification. Introduce a
   `Groth16Parameters` struct in `zebra-consensus::groth16` that locates the
   external parameter files at runtime via a deterministic directory path and
   provides a human-readable `failure_hint()` when files are missing. The
   parameters are **not** linked into the binary; `GROTH16_PARAMETERS` is a
   static that loads them lazily from disk.

3. **Add lock-time and expiry-height check functions.** Four public check
   functions are added to `zebra-consensus::transaction::check`:
   `lock_time_has_passed`, `joinsplit_has_vpub_zero`,
   `coinbase_expiry_height`, and `non_coinbase_expiry_height`. These enforce
   the Zcash protocol rules for transaction finality and expiry, and they
   require `Transaction::lock_time()` to change its return type: it is
   `pub fn lock_time(&self) -> Option<LockTime>` from this milestone on,
   `None` whenever the stored lock time cannot lock the transaction
   (`TEST_INTERFACE.md` §4 states the exact rule). The
   `Height::MAX_EXPIRY_HEIGHT` constant is introduced in `zebra-chain` as
   `Height(499_999_999)`.

4. **Validate and surface miner fees.** Add
   `BlockError::SummingMinerFees` to `zebra-consensus::error` and wire the
   miner-fee aggregation call into the block verifier. Add
   `SubsidyError::InvalidMinerFees` and `SubsidyError::SumOverflow` for
   downstream subsidy-validation errors.

5. **Error surface for `TransactionError`.** Seven variants are new and must be
   added to `zebra_consensus::error::TransactionError`:
   `LockedUntilAfterBlockHeight`, `LockedUntilAfterBlockTime`,
   `CoinbaseExpiryBlockHeight`, `MaximumExpiryHeight`,
   `ExpiredTransaction`, `MalformedGroth16`, and `BothVPubsNonZero`.

   `Groth16` is the eighth, and it is **not** new — it already exists in the
   inherited tree as a unit variant (`Groth16,`). Change it in place to carry a
   payload: `Groth16(String)`. Adding a second `Groth16` alongside the existing
   one is a duplicate-variant compile error that fails `workspace-checks` and
   every other group with it. The exact signatures for all eight are in
   `TEST_INTERFACE.md` §6.

## Read these local references

- `/app/specs/zips/zip-0205.rst` — Sapling transaction structure (its anchor
  fields are described in ZIP-0225 and in `nu5.txt` §3.4/§5.4.1.3, which this
  milestone's anchor checks follow).
- `/app/specs/zips/zip-0213.rst` — ZIP-213 Heartwood coinbase outputs
  (background for the coinbase rules; the expiry-height rules themselves are in
  `nu5.txt` §3.11 and §7.1).
- `/app/specs/zips/zip-0225.rst` — V5 transaction format; anchor fields
  in shielded spends are laid out there.
- `/app/TEST_INTERFACE.md` — the contract the tests are written
  against for this step; every type, method, constant, and error variant
  they touch is named there.
- `/app/WORKSPACE_HANDOFF.md` — what carries forward from steps 1–12 and
  what the next milestone expects to find.
- `zebra-state/src/error.rs` — the existing `ValidateContextError` enum;
  add the new variants listed in `TEST_INTERFACE.md` §5 there (four of the six
  it prints are new at this milestone; the two Sapling/Orchard tree-error
  variants already exist from step 07).
- `zebra-consensus/src/error.rs` — add the new `TransactionError` variants,
  `SubsidyError` variants, and `BlockError::SummingMinerFees` there.
- `zebra-consensus/src/transaction/check.rs` — the home for the four new
  check functions.
- `zebra-chain/src/block/height.rs` — add `MAX_EXPIRY_HEIGHT` alongside the
  existing `MAX` constant.

## Explicit out-of-scope decisions (this step)

- **Full anchor validation against a live state service.** The tests verify
  only the error variants' existence and their constructability
  via `From<NoteCommitmentTreeError>`. The runtime check that actually queries
  the committed tree and returns `UnknownXxxAnchor` is implementation-internal
  and is not exercised by the drivers.
- **Proof verification for JoinSplits at test time.** The drivers test only
  the `Groth16Parameters` public surface — directory path and failure hint.
  They deliberately do not access `GROTH16_PARAMETERS` because that would
  trigger a ~700 MB download. Correctness of the actual proof verification is
  out of scope for this milestone.
- **Block subsidy and funding-stream correctness.** Inherited from step 12.
  This milestone adds `SubsidyError` variants and `BlockError::SummingMinerFees`
  to the error surface but does not check the subsidy arithmetic itself.
- Everything already out of scope per `REQUIREMENTS.md`'s
  "Explicit out-of-scope decisions" list remains out of scope.

## Meta requirements

- **Budget.** `bash /app/timer.sh` shows elapsed and remaining time.
- **Cumulative workspace.** `/app` carries forward in full (minus `.git/` and
  `target/`) to step 14. Leave it building.
- **Network.** This environment has no network access, and everything the
  build needs is already installed. The upstream project's source, its
  releases, mirrors and package registries are unreachable, so a copy cannot
  be fetched by any route. Searching for, downloading, copying, wrapping or
  invoking an implementation of this milestone — or any task-specific
  answer — is prohibited.
- **Workspace scope.** Tests are injected into `zebra-chain/`,
  `zebra-consensus/`, and `zebra-state/`'s `tests/` directories when the
  milestone is checked. `--offline` must keep working; do not add a new
  third-party dependency.
- **Local checks.** `cargo build --offline --workspace --all-targets --all-features`
  and `cargo check --offline --workspace --all-targets --all-features` are
  the checks that matter most.
- **Handoff.** Do not delete `/app/specs/`, `/app/TEST_INTERFACE.md`,
  `/app/WORKSPACE_HANDOFF.md`, or `/app/timer.sh`.
