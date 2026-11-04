# Step 12 — Halo2 proof service, ZIP-155 addrv2, ZIP-212 coinbase rules, block subsidy and funding streams

## Implement this milestone

`/app` already builds and passes its own tests: steps 1–11 gave you the V5
transaction codec, Orchard data structures and nullifier indexing, ZIP-244
sighash, the mempool, peer connection management, and transparent and Sapling
validation. This milestone names six remaining consensus and networking
capabilities. All six are required, and they are checked in different ways,
which is worth knowing before you plan your time:

- **Capabilities 3, 4 and 6** are checked on **behaviour**. Tests call the
  functions you write, with inputs they construct, and assert both the
  accepting and the rejecting result.
- **Capability 5** is checked on **shape only** — the presence, signature and
  `Display` text of the error types it introduces. Its arithmetic lives in a
  private module that no integration test can call.
- **Capabilities 1 and 2** are checked only by
  `cargo check --workspace --all-targets --all-features` compiling them, for
  the same reason. They carry no direct test and they are not optional: step 13
  builds on both, and leaving either unimplemented does not cost you here but
  will cost you there.

`TEST_INTERFACE.md §8` gives the reason for each, one capability at a time.
"Explicit out-of-scope decisions" below lists what is genuinely out of scope:

1. **Halo2 proof verification service (V5 transactions).** A V5 transaction
   carrying an Orchard bundle must have its Halo2 proof verified. Wire the
   `BatchVerifier` service for Orchard proofs using `orchard::circuit::Proof`
   and `orchard::circuit::Instance`, backed by a pre-built `VerifyingKey`.
   The verifier lives in `zebra-consensus::primitives::halo2` and exposes a
   `tower::Service`-compatible interface. Batch fallback to a synchronous
   verifier is required when the batch pipeline is unavailable.

2. **ZIP-155 `addrv2` network message.** Implement the `addrv2` wire format
   per ZIP-155: a `addr_v2` message carrying a vector of `AddrV2` entries,
   each with a `network_id` byte, an address byte-vector whose maximum length
   is 512 bytes, a port, and a services field. IPv4 (`network_id = 0x01`,
   4-byte address) and IPv6 (`network_id = 0x02`, 16-byte address) are the
   minimum supported network IDs. The type lives in
   `zebra-network::protocol::external::addr`.

3. **ZIP-212 coinbase output decryptability.** The consensus rule: from the
   Heartwood activation height onward, all Sapling and Orchard outputs *of a
   coinbase transaction* must be decryptable to a valid note plaintext using
   the all-zeros outgoing viewing key, and from Canopy activation the note
   plaintext lead byte must be `0x02`. Implement the check in
   `zebra-consensus::transaction::check` as `coinbase_outputs_are_decryptable`
   and raise `TransactionError::CoinbaseOutputsNotDecryptable` on failure.

   **The function does not test coinbase-ness itself.** Its only height
   condition is Heartwood: below the Heartwood activation height for the given
   network it returns `Ok(())`, and at or above it the outputs must decrypt or
   it returns `Err`. Restricting it to coinbase transactions is wrong here, not
   merely redundant — the caller in `zebra-consensus::block` is what applies it
   to the coinbase only, and the same function is expected to reject an
   ordinary transaction's shielded outputs, which is how the all-zero ovk path
   is exercised. Do not add an `is_coinbase()` guard.

4. **Orchard flags consensus rule.** For any V5 transaction with
   `nActionsOrchard > 0`, at least one of the `enableSpendsOrchard` or
   `enableOutputsOrchard` flags must be set (per
   `/app/specs/nu5.txt`, "Transaction Consensus Rules"). Add
   `Transaction::has_enough_orchard_flags`
   to `zebra-chain` and the consensus check
   `zebra_consensus::transaction::check::has_enough_orchard_flags` that
   raises `TransactionError::NotEnoughFlags` on failure.

5. **Block subsidy and ZIP-214 funding streams.** Validate that each coinbase
   transaction pays the correct block subsidy and includes the required ZIP-214
   funding stream outputs (to ECC, Zcash Foundation, and Major Grants receivers)
   when the block height falls within the ZIP-214 active range on Mainnet
   (`Height(1_046_400)` inclusive to `Height(2_726_400)` exclusive). Raise
   `SubsidyError::FundingStreamNotFound` when the expected output is absent.
   Add `BlockError::TooManyTransparentSignatureOperations` for blocks that
   exceed `MAX_BLOCK_SIGOPS = 20_000` legacy transparent signature operations.

6. **Sprout note commitment tree.** Replace the no-op stub `append` in
   `zebra_chain::sprout::tree::NoteCommitmentTree` with a live
   `incrementalmerkletree::bridgetree::Frontier`-backed implementation.
   `append` must now return `Result<(), NoteCommitmentTreeError>`, and
   `count()` (new at this tag) must return the number of appended leaves.

## Read these local references

- `/app/specs/zips/zip-0155.rst` — addrv2 wire format and network IDs.
- `/app/specs/zips/zip-0212.rst` — coinbase output decryptability rules and
  the `0x02` lead byte.
- `/app/specs/zips/zip-0207.rst` — "Funding Streams": the general funding-stream
  mechanism (the concrete receivers, numerators, denominator and height ranges
  live in ZIP 214, which this corpus does not ship; `TEST_INTERFACE.md` §5
  restates the numbers a test case uses).
- `/app/specs/nu5.txt` §7.1 (transaction encoding), §3.11 (coinbase
  transactions), §7.5 (Orchard action encoding), §3.4 and §5.4.1.3 (Sprout
  treestates and the Merkle tree hash function).
  This is the NU5 protocol specification as text; `/app/specs/` ships
  `nu5.txt`, `zips/zip-NNNN.rst` and `PROVENANCE.md`, alongside the `nu5.pdf`
  carried forward from step 1.
- `/app/TEST_INTERFACE.md` — the contract the tests are written against.
- `zebra-consensus/src/primitives/halo2.rs` — the Halo2 batch verifier types;
  the module is private in the crate's `lib.rs` and not accessible from
  integration tests.
- `zebra-chain/src/sprout/tree.rs` — the Sprout note commitment tree with the
  real `incrementalmerkletree` backing.

## Explicit out-of-scope decisions (this step)

- Halo2 verifier internals (`BatchVerifier`, `Verifier`, `VERIFYING_KEY`,
  `VERIFIER`) — the module `zebra_consensus::primitives::halo2` is private
  in `lib.rs`; it is wired into the consensus pipeline but not accessible from
  integration tests. Not checked directly (see `TEST_INTERFACE.md §8`).
- ZIP-155 `AddrV2` internal type — `pub(in super::super)` visibility; not
  accessible from integration tests outside `zebra-network`. Not checked.
- ZIP-214 funding stream calculation functions (`funding_stream_values`,
  `funding_stream_address`) — in the private `block::subsidy::funding_streams`
  module. Not checked directly; the error variant they raise is in scope.
- Duplicate-spend detection — unchanged from step 11.
- Orchard proof validity for non-coinbase V5 transactions — Halo2 is wired
  but not exercised in the in-scope surface.
- `NoteCommitmentTree::append` returning `Err(FullTree)` — the tree has depth
  29, so provoking the full case needs 2^29 appends. Write the error arm; no
  test can reach it in bounded time. Its `Display` and `Debug` are in scope.
- The `static_assertions::const_assert!` in `Action::max_allocation` — a
  compile-time check internal to `zebra-chain` with no effect observable from
  an integration test.

## Meta requirements

- **Budget.** `bash /app/timer.sh` shows elapsed and remaining time.
- **Cumulative workspace.** `/app` carries forward in full (minus `.git/` and
  `target/`) to step 13.
- **Network.** This environment has no network access, and everything the
  build needs is already installed. The upstream project's source, its
  releases, mirrors and package registries are unreachable, so a copy cannot
  be fetched by any route. Searching for, downloading, copying, wrapping or
  invoking an implementation of this milestone — or any task-specific
  answer — is prohibited.
- **Dependencies.** This milestone is a dependency migration as well as a
  consensus one, and `WORKSPACE_HANDOFF.md`'s "do not add a third-party
  dependency" rule is explicitly overridden by the versions named here. You
  will need to add, to `zebra-chain`: `orchard = "0.0"`, `static_assertions =
  "1.1.0"`, and `zcash_note_encryption` as a git dependency on
  `https://github.com/zcash/librustzcash.git` at rev
  `53d0a51d33a421cb76d3e3124d1e4c1c9036068e`; and to `zebra-consensus`:
  `halo2 = "=0.1.0-beta.1"` and `orchard = "0.0.0"`; and to `zebra-consensus`
  `[dev-dependencies]`: `hex = "0.4.3"`. Those five are already
  present in the inherited `Cargo.lock` as transitive dependencies, so naming
  them in a manifest does not change the resolved graph. The bumps that *do*
  move the lock are `aes 0.7.5`, `bls12_381 0.6.0`, `fpe 0.5.1`, `jubjub
  0.8.0`, `x25519-dalek 1.2.0` in `zebra-chain`, and `bellman 0.11.1`,
  `bls12_381 0.6.0`, `jubjub 0.8.0`, `metrics 0.17.0`, `tokio 1.14.0`,
  `tower 0.4.9` in `zebra-consensus`. Each is required; naming them here is
  what makes them permitted.
  **The bumps to `tower` and `metrics` also require a change at the workspace
  root, which no per-crate manifest edit can make:** the inherited
  `[patch.crates-io]` section pins `tower`, `metrics`,
  `metrics-exporter-prometheus` and `hyper` to git revisions, and those
  entries shadow the registry versions the bumps ask for. Remove those four
  entries, and repoint the `orchard` patch entry from
  `https://github.com/zcash/orchard.git` at rev
  `2c8241f25b943aa05203eacf9905db117c69bd29` to
  `https://github.com/ZcashFoundation/orchard.git` at rev
  `568e24cd5f129158375d7ac7d98c89ebff4f982f`, keeping the
  `incrementalmerkletree` / `zcash_note_encryption` / `zcash_primitives`
  entries as they are. `cargo metadata --offline` names whichever entry is
  still stale. Nothing checks the section directly; leaving it stale stops the
  workspace resolving.
- **Workspace scope.** Tests are injected into `zebra-chain/tests/`
  and `zebra-consensus/tests/` when the milestone is checked. `--offline` must
  keep working
  after your first fetch: the build image's cargo cache is populated from
  the dependency graph the versions above resolve to, so a workspace that
  matches it builds offline. The build tries `--offline --locked` first and
  falls back through `--offline`, `--locked` and finally no flags, recording
  which one resolved; a fallback is recorded and accepted, but a workspace
  that only resolves with a fresh network re-resolve is not the graph this task
  pinned. Run `cargo metadata --offline --format-version 1 >/dev/null` after
  any manifest change — it fails loudly when the workspace can no longer
  resolve without the network — and keep `Cargo.lock` accurate.
- **Local checks.** `cargo build --offline --workspace` and
  `cargo test --offline -p zebra-chain -p zebra-consensus` are the checks
  that matter most.
- **Handoff.** Do not delete `/app/specs/`, `/app/TEST_INTERFACE.md`,
  `/app/WORKSPACE_HANDOFF.md`, or `/app/timer.sh`. This is checked: a gate
  group asserts that all four still exist and are non-empty, and step 13
  starts from this workspace, so losing them is a whole-step failure.
