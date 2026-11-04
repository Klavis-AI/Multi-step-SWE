# Step 3 — Sapling-in-V5 (ZIP-225), Orchard data types, and RedPallas

## Implement this milestone

`/app` already builds and passes its own tests: step 2 gave you the
`Transaction::V5` *skeleton* — a 16-byte header (`header`,
`nVersionGroupId`, `lockTime`, `expiryHeight`; there is no
`nConsensusBranchId` field yet, see `TEST_INTERFACE.md` §3), a transparent
bundle
identical to V4's, and everything past the transparent outputs captured
verbatim as an opaque `rest: Vec<u8>`, with no `network_upgrade` field and no
structure at all for the Sapling or Orchard body. This milestone gives that
tail its real shape for Sapling, and lays the Orchard/RedPallas groundwork a
later milestone wires in:

1. **Replace `rest: Vec<u8>` with a real Sapling shielded-data field.**
   `Transaction::V5` gains `sapling_shielded_data:
   Option<sapling::ShieldedData<sapling::SharedAnchor>>`, laid out per
   ZIP-225's split-array encoding: `nSpendsSapling`/`nOutputsSapling` and
   their "prefix" arrays (`cv`/`nullifier`/`rk`; `cv`/`cm_u`/`ephemeral_key`/
   ciphertexts) come first, and the proofs, spend-auth signatures and
   binding signature that used to travel *with* each V4 spend/output now
   follow later in the transaction as their own arrays, each counted only by
   the earlier prefix array's length — never by their own compactSize. After
   the Sapling bundle, the Orchard section must still be exactly a single
   zero compactSize byte; a real Orchard bundle is a later milestone (see
   out-of-scope below), and any non-zero Orchard section must be rejected,
   not silently accepted or truncated.
2. **Add the `network_upgrade` field to `Transaction::V5`**, derived from
   the transaction's own `nConsensusBranchId` via a new
   `NetworkUpgrade::from_branch_id(u32) -> Option<NetworkUpgrade>` — the
   inverse of the `branch_id()` method you already have. A consensus branch
   id with no assigned network upgrade (including the three version-group-id
   constants, which are never valid branch ids) must fail to parse, not
   default to some upgrade.
3. **Add the externally-counted vector primitives ZIP-225 needs.**
   `serialization::AtLeastOne<T>` (a non-empty `Vec<T>` wrapper, entered only
   through `TryFrom`) models the Sapling bundle's spend/output arrays, which
   the wire format guarantees are non-empty whenever they're present at all.
   `serialization::{zcash_serialize_external_count,
   zcash_deserialize_external_count}` serialize and deserialize a `Vec<T>`
   with no compactSize of its own, using a count supplied by the caller (an
   earlier array's length) instead — and must reject a count above
   `T::max_allocation()` before reading a single byte, exactly like a
   compactSize-prefixed `Vec<T>` already does.
4. **Add Orchard's data types and the RedPallas signature primitives**,
   without wiring either into `Transaction::V5`'s parser. `orchard::Action`
   is a complete, serializable ZIP-225 §7.5 record; the companion types
   `orchard::{NoteCommitment, ValueCommitment, keys::EphemeralPublicKey}`
   wrap a `pallas::Affine` point and validate canonical Pallas-point encoding
   via `TryFrom<[u8; 32]>`; `orchard::{EncryptedNote, Nullifier,
   keys::Diversifier}` are plain fixed-size byte wrappers with no point
   validation. `primitives::redpallas::{SigningKey,
   VerificationKey, VerificationKeyBytes, Signature, Error}`, generic over
   marker types `SpendAuth`/`Binding`, mirror the `redjubjub` API you already
   have for Sapling, but over the Pallas curve and with RedPallas's own
   small-order rejection rule. None of this is reachable from a
   `Transaction` value yet — see out-of-scope below.

## Read these local references

- `/app/specs/zips/zip-0225.rst` — the V5 transaction format this milestone
  gives Sapling structure for: the header fields, the split spend/output
  arrays, and the Orchard section this step still requires to be empty.
- `/app/specs/zips/zip-0200.rst` — the consensus branch ID mechanism
  `from_branch_id` inverts.
- `/app/specs/nu5.txt` §3.6 (Orchard notes, nullifiers, key material), §7.1
  (transaction encoding), §7.5 (the Orchard `Action` encoding your data
  types anticipate but do not yet parse from a transaction).
- `/app/TEST_INTERFACE.md` — the contract the tests are written
  against for this step; every type, method, field, byte offset and error
  string they touch is named there. Read it before touching
  `Transaction::V5`, `sapling::ShieldedData`, or adding the `orchard` module.
- `/app/WORKSPACE_HANDOFF.md` — what carries forward from steps 1-2 and what
  the next milestone expects to find already in place.
- `zebra-chain/src/transaction/serialize.rs`, `zebra-chain/src/sapling/`,
  `zebra-chain/src/parameters/network_upgrade.rs` — the retained code this
  milestone extends. The V5 match arm's current `rest` field and its
  `// TODO: Parse orchard into structs` comment are the exact spot this
  milestone's parser change replaces.

## Explicit out-of-scope decisions (this step)

- **Wiring a populated Orchard bundle into `Transaction::V5`.** The parser
  must still reject any `nActionsOrchard != 0` with a named error; parsing a
  real Orchard bundle, and giving `Transaction`/`ShieldedData` accessors over
  it, is a future milestone (`REQUIREMENTS.md` step 4). `orchard::Action` and
  its companion types exist as freestanding, independently serializable
  types this step, not as a field any `Transaction` variant carries.
- **RedPallas and Halo2 cryptographic verification.** `VerificationKey`'s
  small-order rejection is an encoding-validity check, not verification of a
  signature over a real message; no consensus rule invokes RedPallas or
  Halo2 this step. `primitives::proofs::Halo2Proof` (a raw-bytes wrapper,
  already present as of this milestone) is not exercised by any in-scope
  behaviour here.
- **ZIP-244's transaction digest algorithm for V5.** Every transaction
  version, V5 included, still hashes the same way it already does (a
  version-agnostic double-SHA256 of the serialized transaction). This
  milestone does not add a V5-specific sighash or TxId algorithm; that is
  `REQUIREMENTS.md` step 6's job.
- **Contextual or activation-height validation.** `from_branch_id` is a
  pure, context-free table lookup; nothing here checks whether a given
  network upgrade's branch id is the *correct* one for a transaction's block
  height, or enforces NU5 activation.

## Meta requirements

- **Budget.** `bash /app/timer.sh` shows elapsed and remaining time.
- **Cumulative workspace.** `/app` carries forward in full (minus `.git/`
  and `target/`) to step 4. Leave it building.
- **Network.** This environment has no network access, and everything the
  build needs is already installed. The upstream project's source, its
  releases, mirrors and package registries are unreachable, so a copy cannot
  be fetched by any route. Searching for, downloading, copying, wrapping or
  invoking an implementation of this milestone — or any task-specific
  answer — is prohibited.
- **Workspace scope.** Tests are added to `zebra-chain/`'s
  `tests/` directory when the milestone is checked, compiled against the
  manifest as you
  leave it. `--offline` must keep working. This milestone **adds** exactly two
  third-party dependencies to `zebra-chain`, and adding them is part of the
  milestone: `group` and a pinned `halo2` git dependency, which Orchard's
  Pallas-curve arithmetic needs and `redjubjub` alone does not supply. Neither
  is in the workspace you inherit. `TEST_INTERFACE.md` §1 gives both manifest
  lines verbatim, including the git URL and rev — put both in
  `[dependencies]` and pin them exactly as written. That pair is what the
  milestone's in-scope surface needs, and it is sufficient: every other manifest
  change the upstream release makes at this tag (`aes`, `fpe`, an `itertools`
  line, the `funty = "=1.1.0"` pin, the `criterion`/`rand_chacha`
  dev-dependencies, an optional `zebra-test`, and four raised version
  requirements) belongs to code, targets and lock-graph repairs this milestone
  does not require. Do not add any third-party dependency beyond the two named
  above.
- **Toolchain.** This milestone is built and checked with a newer pinned Rust
  compiler than the previous one. At least one build-dependency reached
  through the `Cargo.lock` you inherit does not compile under it, so
  re-resolving `Cargo.lock` **within the existing semver ranges** is part of
  this milestone. That is an ordinary lock update, not the "do not bump a
  dependency's version requirement" case `WORKSPACE_HANDOFF.md` prohibits.
- **Local checks.** `cargo build --offline --workspace` and
  `cargo test --offline -p zebra-chain` are the checks that matter most.
  This milestone also introduces feature-gated code (a `bench` feature and
  its `zebra-chain/benches/block.rs` target, on top of the existing
  `proptest-impl` feature), so a check across the whole workspace must pass
  with every feature enabled, not just the default set:
  `cargo check --offline --workspace --all-targets --all-features`.
- **Handoff.** Do not delete `/app/specs/`, `/app/TEST_INTERFACE.md`,
  `/app/WORKSPACE_HANDOFF.md` or `/app/timer.sh`.
