# Step 6 — ZIP-244 transaction identifiers and the v5 signature hash

## Implement this milestone

`/app` already builds and passes its own tests: steps 1-5 gave you the V1-V5
transaction codec, ZIP-225's Sapling/Orchard v5 layout, Orchard nullifiers
retrievable from state, an async batched RedPallas verifier, the ZIP-211
Sprout pool freeze, and a block header whose `commitment()` can already
*decode* a ZIP-221 chain history root. None of that lets you identify a v5
transaction, sign one, or build the root a Heartwood-or-later header commits
to — this milestone adds all three.

1. **Split the v5 transaction identifier in two, per ZIP-244.** Every
   transaction's identifier has so far been SHA256d over its own wire
   serialization. `Transaction::hash()` must keep doing exactly that for
   versions 1-4, but for v5 switch to ZIP-244's construction: a digest over
   the transaction's *effects* (header, transparent inputs/outputs, the
   Sapling and Orchard bundles) that deliberately excludes proofs and
   signatures, so that authorizing data cannot change a transaction's own
   identity. This effects-only digest is the txid. ZIP-244 also names a
   second digest over the excluded authorizing data, the `auth_digest`; this
   milestone's public surface does not ask you to compute one (see
   `TEST_INTERFACE.md`'s negative-space section).
2. **Make `Transaction::sighash` produce a v5 digest.** The same function
   that already signs v1-v4 transactions under ZIP-143/ZIP-243 must now also
   handle v5, using the signature-hash construction ZIP-244 defines
   alongside the txid: the six `nHashType` variants — `ALL`, `NONE`,
   `SINGLE`, each with and without `ANYONECANPAY` — for a v5 transaction that
   names a transparent input, and a well-defined `SIGHASH_ALL` (equal to the
   transaction's own txid) for one that does not. Rewiring this must not
   change a single bit of what the function already returns for a v1-v4
   transaction; ZIP-143 and ZIP-243 are unchanged specifications.
3. **Add `HistoryTree`, the runtime for the ZIP-221 root a header already
   decodes.** A block header has been able to report a `ChainHistoryRoot`
   commitment since an earlier milestone, but nothing could compute one from
   block data. Add `zebra_chain::history_tree::HistoryTree`: seed it from a
   single block with `from_block`, extend it one block at a time with
   `push` or several at once with `try_extend`, and read its committed root
   back with `hash`. A tree seeded from the Heartwood activation block must
   reproduce the chain history root the very next real block's own mined
   header commits to.

This release also teaches the network layer to parse (and otherwise ignore)
`MSG_WTX`, ZIP-239's inventory type for a witnessed transaction — but nothing
observable is asked of you for it here; see the out-of-scope note below.

## Read these local references

- `/app/specs/zips/zip-0244.rst` — the txid/auth_digest split and the v5
  signature hash algorithm; both are this milestone's core. Its txid and
  auth-digest sections describe what this release implements, but its "S.2"
  section describes a **later** revision of the signature digest than the one
  the published vectors this milestone is checked against encode — read
  `TEST_INTERFACE.md` §4 before implementing `sighash` for v5, because there
  this milestone's contract is the normative statement.
- `/app/specs/zips/zip-0143.rst`, `/app/specs/zips/zip-0243.rst` — the
  Overwinter and Sapling signature hashes your `sighash` rewrite must
  continue to reproduce unchanged.
- `/app/specs/zips/zip-0221.rst` — the chain history Merkle Mountain Range
  `HistoryTree` builds.
- `/app/specs/zips/zip-0239.rst` — `MSG_WTX`'s wire shape, for context on
  what this release parses (see out-of-scope note below).
- `/app/TEST_INTERFACE.md` — the contract the tests are written
  against; every type, method, field, constant and byte layout they touch is
  named there. Read it before touching `sighash.rs` or adding
  `history_tree.rs`.
- `/app/WORKSPACE_HANDOFF.md` — what carries forward from steps 1-5 and what
  step 7 expects to find in place.
- `zebra-chain/src/transaction.rs`, `zebra-chain/src/transaction/sighash.rs`,
  `zebra-chain/src/block/commitment.rs` — the retained modules this
  milestone extends; `history_tree.rs` does not exist yet and is yours to
  add next to them.

## Explicit out-of-scope decisions (this step)

- **ZIP-244's `auth_digest`.** Computing it is not part of this milestone's
  in-scope surface; it appears only as a published value in ZIP-244's own test
  vectors.
- **ZIP-239's `MSG_WTX` inventory type.** This release parses it and takes no
  further action; there is no behavior beyond "does not error" to build.
- **Orchard spend-authorization signature verification, and full contextual
  validation of a v5 transaction carrying Sapling data.** Both live inside
  `zebra-consensus`'s asynchronous verifier and still depend on the pinned
  RedPallas/Groth16 primitives that are out of scope everywhere else in this
  corpus; nothing here asks you to change or expose that pipeline.
- **Duplicate Sprout nullifier rejection and rejected-parent/child pruning in
  `zebra-state`.** Real behavior in this upstream release; keep it working,
  but no new public entry point for it is required by this milestone.

## Meta requirements

- **Budget.** `bash /app/timer.sh` shows elapsed and remaining time.
- **Cumulative workspace.** `/app` carries forward in full (minus `.git/`
  and `target/`) to step 7. Leave it building.
- **Network.** This environment has no network access, and everything the
  build needs is already installed. The upstream project's source, its
  releases, mirrors and package registries are unreachable, so a copy cannot
  be fetched by any route. Searching for, downloading, copying, wrapping or
  invoking an implementation of this milestone — or any task-specific
  answer — is prohibited.
- **Workspace scope.** Tests are added to `zebra-chain/tests/` when the
  milestone is checked, compiled against the manifest as you leave it.
  `--offline`
  must keep working; do not add a new third-party dependency.
- **Local checks.** `cargo build --offline -p zebra-chain --tests` and
  `cargo check --offline --workspace --all-targets --all-features` are the
  two whole-workspace checks that matter — the `--all-features` is load
  bearing, not optional: `zebra-chain/benches/block.rs` unconditionally
  reaches the optional `zebra-test` dependency and a `#[cfg]`-gated test
  helper, both only enabled behind a feature, so a bare `--all-targets`
  fails to build the *retained* bench even before you change anything. Also
  run `cargo test --offline -p zebra-chain`.
- **Handoff.** Do not delete `/app/specs/`, `/app/TEST_INTERFACE.md`,
  `/app/WORKSPACE_HANDOFF.md` or `/app/timer.sh`.
