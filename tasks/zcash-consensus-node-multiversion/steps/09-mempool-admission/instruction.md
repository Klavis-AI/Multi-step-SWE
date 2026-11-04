# Step 9 — Mempool admission: unmined transaction identifiers and a tip-dependent verifier

## Implement this milestone

`/app` already builds and passes its own tests. Steps 1–8 gave you the V1–V5
transaction codec, Orchard bundles, contextual validation, the ZIP-221 history
tree, ZIP-209 value pools and the ZIP-244 `auth_digest` — but every transaction
the node has handled arrived *inside a block*. There is no place for an
unconfirmed one, no identifier that survives it being re-signed in transit, and
no way to ask "where is the chain tip, and what was in it?" without depending on
`zebra-state`.

This milestone builds the admission surface a mempool is written against. Its
theme: **an unconfirmed transaction's validity is relative to a chain tip**, and
a v5 transaction outside a block needs a *second* identifier, because ZIP-244
made the v5 txid commit to the transaction's effects only.

1. **Give transactions a witnessed identifier (ZIP-239).** Introduce
   `transaction::WtxId`: the 32-byte transaction ID followed by the 32-byte
   authorizing-data digest, 64 bytes, in that order. It converts to and from its
   byte array and its slice/`Vec` forms, prints and parses as 128 hex characters
   in Zcash's reversed display convention, and serializes as the bare 64 bytes.
   `AuthDigest` and `transaction::Hash` gain the public conversions, text forms
   and wire impls it is built out of; `SerializationError` gains two variants.
2. **Give the node one identifier type correct for every version.**
   `transaction::UnminedTxId` is `Legacy(Hash)` for v1–v4 and `Witnessed(WtxId)`
   for v5; `transaction::UnminedTx` pairs a transaction with the identifier it
   arrived under, computed once; `Transaction::unmined_id()` picks the variant
   from the version alone. This is the type a mempool is keyed by, so a legacy
   and a witnessed identifier over the same mined ID stay two distinct values.
3. **Expose the chain tip as a trait, not a service.** `zebra_chain::chain_tip`
   holds the `ChainTip` trait — tip height, tip hash, and the *mined* transaction
   IDs of the tip block's transactions — plus `NoChainTip`, the always-empty
   implementation handed to a component with no state to talk to. It lives in
   `zebra-chain` precisely so `zebra-network` and `zebrad` can be written against
   a tip without depending on `zebra-state`, and so `zebra-chain` need not depend
   on `tokio`. `NetworkUpgrade` also gains `is_activation_height`, so a consumer
   can recognise the heights at which unconfirmed transactions are discarded.
4. **Separate the two coinbase-input questions.** A block asks "is this the
   block's coinbase transaction?"; a mempool asks "does this transaction touch a
   coinbase input at all?". These are different predicates, and a mempool asking
   the first can be slipped past, so `Transaction::is_coinbase` and
   `contains_coinbase_input` become `has_valid_coinbase_transaction_inputs` and
   `has_any_coinbase_inputs`, with the meanings §4 of `TEST_INTERFACE.md` gives.
   Update every caller.
5. **Move the network service's unconfirmed half onto unmined IDs.** The
   `zebra_network::Request`/`Response` variants carrying unconfirmed transactions
   stop using mined hashes: downloading, advertising, pushing and listing all
   move to `UnminedTxId`/`UnminedTx`. The block half is unchanged — a block is not
   malleable the way a v5 transaction is. Internally the ZIP-239 `MSG_WTX`
   inventory code (`5`) must round-trip a real `WtxId`, not an opaque array.
6. **Make the transaction verifier tip-dependent.** It stops rejecting mempool
   requests as unimplemented. `Request::Mempool` carries an `UnminedTx` and the
   height of the *next* block; `Request::Block` carries an `Arc<Transaction>`,
   the block's own height and the UTXOs created earlier in that block. Only a
   block request has known UTXOs. The verifier answers with the transaction's
   `UnminedTxId` rather than its hash, so a caller that pipelined several can
   match replies to requests. A coinbase transaction is never admitted to the
   mempool, and that rejection gets its own error variant.

## Read these local references

- `/app/specs/zips/zip-0239.rst` — `MSG_WTX`, the witnessed transaction ID, and
  which versions use it on the wire; `/app/specs/zips/zip-0244.rst` — the v5
  txid and the `auth_digest` it concatenates.
- `/app/specs/nu5.txt` §3.4 "Transactions and Treestates", §7.1 "Transaction
  Encoding and Consensus" (version rules by height), §7.12 (genesis coinbase).
- `/app/TEST_INTERFACE.md` — the contract the tests are written against:
  every module path, type, signature, field, trait impl and error variant they
  touch. Read it before renaming `Transaction::is_coinbase` or `Request`.
- `/app/WORKSPACE_HANDOFF.md` — what carries forward, and what step 10 expects.
- Retained code this milestone extends:
  `zebra-chain/src/transaction/{hash,auth_digest}.rs`,
  `zebra-chain/src/parameters/network_upgrade.rs`,
  `zebra-network/src/protocol/internal/`, `zebra-consensus/src/transaction.rs`.
  `rg -n 'does not yet have a mempool' /app` finds the `unimplemented!` the
  verifier raises today for a mempool request.

## Explicit out-of-scope decisions (this step)

`TEST_INTERFACE.md` §9 restates these as what the tests can and cannot see.

- **The mempool component itself.** Crawler, download-and-verify pipeline,
  storage, tip-change subscription and inbound-service wiring all live in
  `zebrad`, which no group in this step covers — every driver is injected into
  `zebra-chain`, `zebra-consensus` or `zebra-network`, never into `zebrad`.
  Build them as you like; what is in scope is the `zebra-chain`,
  `zebra-network` and `zebra-consensus` surface they are written against.
- **The concrete chain-tip sender and receiver.** `LatestChainTip`,
  `ChainTipChange`, `ChainTipSender` and `TipAction` are constructed only inside
  the private `zebra_state::service`; only `ChainTip` and `NoChainTip` are in
  scope.
- **Actually running the transaction verifier.** `Verifier::new` takes a
  `script::Verifier` and `zebra_consensus::script` is private, so the service is
  unconstructible from a test. The request type, accessors, response alias and
  error variant are in scope; the verification is not.
- **The wire-level inventory encoding.** `InventoryHash`, `Message` and the
  `MSG_WTX` code live in the private `zebra_network::protocol::external`;
  implement the round trip (point 5 requires it), but no test sees it.
- **Mempool storage, rejection lists, expiry, eviction and fees.** What the
  mempool *keeps* is steps 10–11; this step provides the identifiers and the
  admission request, not the policy.
- **Halo2/RedPallas verification**, the script interpreter, mining, wallet, RPC,
  `lightwalletd`, Tor and NU6 — still out of scope per `REQUIREMENTS.md`.

## Meta requirements

- **Budget and workspace.** `bash /app/timer.sh` shows elapsed and remaining
  time. `/app` carries forward in full (minus `.git/` and `target/`) to step 10,
  so leave it building.
- **Network.** This environment has no network access, and everything the
  build needs is already installed. The upstream project's source, its
  releases, mirrors and package registries are unreachable, so a copy cannot
  be fetched by any route. Searching for, downloading, copying, wrapping or
  invoking an implementation of this milestone — or any task-specific
  answer — is prohibited.
- **Workspace scope.** Tests are added to the `tests/` directories of
  `zebra-chain`, `zebra-network` and `zebra-consensus` when the milestone is
  checked, compiled
  against the manifests as you leave them. `--offline` must keep working: add no
  new third-party dependency, and remove no existing one.
- **Local checks.** `cargo build --offline --workspace`, `cargo test --offline -p
  zebra-chain -p zebra-network -p zebra-consensus`, and `cargo check --offline
  --workspace --all-targets --all-features` (benches build under
  `--all-targets`).
- **Handoff.** Do not delete `/app/specs/`, `/app/TEST_INTERFACE.md`,
  `/app/WORKSPACE_HANDOFF.md` or `/app/timer.sh`.
