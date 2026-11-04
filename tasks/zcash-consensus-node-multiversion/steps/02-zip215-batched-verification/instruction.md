# Step 2 — ZIP-215 batch verification, NU5 constants, and the v5 transaction skeleton

## Implement this milestone

`/app` already builds and passes its own tests: step 1 gave you the V1–V4
transaction codec, the block/header codec, and an infallible `RootHash`
enum for the block header's commitment field. This milestone upgrades that
baseline to the shape Zebra took for the Overwinter-through-Canopy releases,
plus the not-yet-activated start of NU5:

1. **Replace `RootHash` with a fallible `Commitment`.** The pre-Sapling and
   Heartwood-activation reserved fields must actually be checked: a non-zero
   reserved field is now a named error, not silently accepted. Add the fifth,
   NU5-only reading of the commitment field (a combined chain-history/
   transaction-auth commitment, per ZIP-244), even though no corpus block
   reaches NU5 height yet — see `TEST_INTERFACE.md` §3 for the exact variant
   and error shapes required.
2. **Add `NetworkUpgrade::Nu5`** as an eighth network-upgrade variant with a
   published consensus branch ID, but with **no activation height on either
   network**. `current()`/`next()` must never report Nu5 until an activation
   height exists; this is the "unscheduled upgrade" state, and it must stay
   correct at every height including `Height::MAX`.
3. **Add the `Transaction::V5` skeleton.** A version-5 transaction whose
   4-byte header sets the `fOverwintered` bit like V3 and V4 do — ZIP-225's
   header field is "`fOverwintered` flag (bit 31, always set)" plus the
   version in bits 30..0, so the header word is `5 | (1 << 31)` — carrying a
   transparent bundle exactly like V4, an unconditional `expiry_height`, and
   everything after the transparent outputs as an opaque, verbatim
   `rest: Vec<u8>`; the Sapling/Orchard body is deliberately not parsed yet
   (see out-of-scope below). `TEST_INTERFACE.md` §5's byte table is the
   governing layout — read it before you write the header, because this
   milestone's prefix is **not** ZIP-225's final one (see below).
4. **Generalize the Sapling shielded bundle over its anchor shape.** V4 gives
   each `Spend` its own anchor; V5 will share one anchor per transaction. Add
   the `AnchorVariant` type parameter (`PerSpendAnchor` for V4,
   `SharedAnchor` for the future V5 bundle) to `ShieldedData<AnchorV>` and
   `Spend<AnchorV>` so the two shapes are the same generic type, without
   changing V4's wire format or its existing accessors.
5. **Harden every attacker-controlled `Vec<T>` in the wire format against
   preallocation DoS.** Implement `TrustedPreallocate` for every type a
   compactSize-prefixed vector can hold in a block or transaction (headers,
   hashes, transparent inputs/outputs, Sapling spends/outputs in both anchor
   shapes, Sprout JoinSplits in both proof systems, whole transactions), each
   with the exact `max_allocation()` derived from `MAX_BLOCK_BYTES` and that
   type's minimum wire size — not from measuring real data.
6. **Wire up ZIP-215 Ed25519 verification and the checkpoint/error surface
   `zebra-consensus` needs.** `zebra-chain` already re-exports
   `ed25519_zebra` as `primitives::ed25519`; what it does **not** yet have is
   a version of that crate implementing ZIP-215. Your job is the dependency
   version — and keeping the re-export intact — not the cryptography. `zebra-consensus`
   gets the mandatory-checkpoint budget constants, a default `Config`, and an
   error enumeration that names the JoinSplit-signature and Sapling-binding-
   signature consensus rules and can recover a `TransactionError` from a
   boxed, type-erased batch-verifier failure.

## Explicit out-of-scope decisions (this step)

- Parsing, validating or even structurally defining the Sapling/Orchard
  portion of a v5 transaction body — it is exactly the opaque `rest` bytes
  this milestone leaves alone. (A future step gives it structure.)
- Any consensus rule that decides *whether* to invoke batched verification,
  the batching service itself, and the Groth16/Halo2/RedJubjub/RedPallas
  cryptographic internals — `zebra_consensus::error`'s enumeration of the
  rules is in scope; the tower services that apply them are not.
- Contextual/semantic validation of `Commitment::FinalSaplingRoot` and
  `Commitment::ChainHistoryRoot` against the actual Sapling tree or a real
  Merkle Mountain Range — post-Sapling, any 32 bytes is a structurally valid
  reading at this milestone; only the pre-Sapling and Heartwood-activation
  reserved fields are checked here.
- Giving NU5 an activation height, on either network. That is a future
  milestone's decision, made when NU5 actually ships.
- Sighash algorithm changes for v5 transactions (ZIP-244's transaction-digest
  algorithm) — the spend-authorization and binding-signature *consensus rule
  surface* is in scope (`TransactionError` variants), the v4 sighash algorithm
  you already have is unchanged, and no v5 sighash is computed this step.

## Read these local references

- `/app/specs/zips/zip-0215.rst` — ZIP-215, and the normative statement of
  the three Ed25519 validity rules: both point encodings on the curve, `S <
  ℓ`, and the **cofactored** equation `[8][S]B = [8]R + [8][k]A`, with the
  cofactorless `[S]B = R + [k]A` explicitly forbidden and non-canonical
  encodings explicitly permitted. This is what makes batch and single
  verification accept exactly the same signatures, which is the property this
  milestone's signature primitive must satisfy.
- `/app/specs/zips/zip-0225.rst` — the v5 transaction shape: the
  `fOverwintered` bit, the version group ID, and the field order. **Read its
  header table for the shape, not as this milestone's byte layout.** ZIP-225
  as published puts `nConsensusBranchId` between `nVersionGroupId` and
  `lock_time`, for a 20-byte common prefix; the release this milestone
  reconstructs predates that field order and writes a **16-byte** prefix —
  `header`, `nVersionGroupId`, `lock_time`, `nExpiryHeight`, with no
  `nConsensusBranchId` — before the transparent inputs. Where the two
  disagree, `TEST_INTERFACE.md` §5's byte table governs, and it is what the
  tests assert. (A later milestone in this ladder brings the layout up
  to ZIP-225 as published; this one does not.)
- `/app/specs/zips/zip-0200.rst` — the consensus branch ID mechanism; NU5's
  branch ID is published even though its activation height is not.
- `/app/specs/zips/zip-0244.rst`, `/app/specs/zips/zip-0221.rst` — the
  post-NU5 block/transaction commitment this milestone's fifth `Commitment`
  reading anticipates, and the chain-history-tree commitment it builds on.
- `/app/specs/nu5.txt` §4.10 (SIGHASH), §5.4.6 (Ed25519 — the same three
  rules in their "[Canopy onward]" form, plus `ℓ`, the base point and the
  signature encoding; note that its one *table*,
  `PreCanopyExcludedPointEncodings`, is the rejection list ZIP-215 abolishes
  and must **not** be implemented), Appendix B.3 (Ed25519 batch validation),
  §4.13 (the Sapling binding signature and its validating key `bvk`),
  §5.4.8.3 (the homomorphic Pedersen value commitment `bvk` is derived from),
  §4.15 (spend authorization signature), §7.1 (transaction encoding), §7.6
  (block header encoding). **`nu5.txt` is the *final* NU5 specification and
  its §6 says NU5 activated on Mainnet at height `1687104`; this milestone
  predates that, and `Nu5` has no activation height on either network — see
  `TEST_INTERFACE.md` §4.** The tests carry their own Ed25519 vectors,
  derived independently — there is no table in either document to copy.
- `/app/TEST_INTERFACE.md` — the contract the tests are written
  against; every module path, type, constant, byte offset and error variant
  they touch is named there. Read it before renaming `RootHash`, before
  touching `sapling::Spend` or `sapling::ShieldedData`, and before adding the
  `TrustedPreallocate` impls.
- `/app/WORKSPACE_HANDOFF.md` — what carries forward from step 1 and what the
  next milestone expects to find already in place.

## Meta requirements

- **Budget.** `bash /app/timer.sh` shows elapsed and remaining time.
- **Cumulative workspace.** `/app` carries forward in full (minus `.git/` and
  `target/`) to step 3. Leave it building.
- **Network.** This environment has no network access, and everything the
  build needs is already installed. The upstream project's source, its
  releases, mirrors and package registries are unreachable, so a copy cannot
  be fetched by any route. Searching for, downloading, copying, wrapping or
  invoking an implementation of this milestone — or any task-specific
  answer — is prohibited.
- **Workspace scope.** Tests are added to `zebra-chain/`'s and
  `zebra-consensus/`'s `tests/` directories when the milestone is checked,
  compiled against
  the manifests as you leave them, and they reach your code only through those
  crates' public API. Do not remove an existing dependency entry from either
  manifest, and do not add a new third-party crate beyond the one version bump
  below.
- **The one dependency change this milestone requires.** `zebra-chain`
  inherits `ed25519-zebra = "1"`, which predates ZIP-215. Raising it to
  `2.2.0` is part of goal 6, is expected, and does not count as "adding a new
  dependency" — the 2.x crate is already resolved in the workspace
  `Cargo.lock` you inherit. `TEST_INTERFACE.md` §8 states the constraint that
  comes with it: `zebra-chain`'s `rand_core` must stay in step with whichever
  `ed25519-zebra` you resolve, because the test supplies its own RNG.
- **Toolchain.** Your container has a newer Rust than the one that checks this
  step, and the gap is wide enough to matter — see `TEST_INTERFACE.md` §1.
  Keep every crate on edition 2018 and avoid anything stabilised after mid
  2021.
- **Local checks.** `cargo build --workspace` and
  `cargo test -p zebra-chain -p zebra-consensus` are the checks that matter
  most. `$CARGO_HOME` is pre-populated with this workspace's whole dependency
  graph and cargo resolves from it offline, so the first `cargo build
  --workspace` needs no network; compiling that graph is still the largest
  fixed cost in your budget, so run it early. `cargo build --offline --locked
  --workspace` is the check worth repeating, and it is close to the
  build the workspace must pass — see `WORKSPACE_HANDOFF.md` for the fallback
  ladder that applies when a manifest change forces a re-resolve.
- **Handoff.** Do not delete `/app/specs/`, `/app/TEST_INTERFACE.md`,
  `/app/WORKSPACE_HANDOFF.md` or `/app/timer.sh`.
