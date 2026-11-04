# Step 11 — ZIP-401 eviction weights, a transaction size ceiling, and connection limits

## Implement this milestone

`/app` already builds and passes its own tests. Step 10 gave the node a mempool
that can *keep* a transaction: an entry knows its serialized size, carries the
fee it paid, expires at a height, and can be told when the chain tip moves. What
it still cannot do is **choose which entry to lose**.

A mempool is a fixed-size resource and demand for it is unbounded, so every
node that holds one needs a total order on its contents. This milestone supplies
ZIP-401's order — a *cost* and an *eviction weight* — plus the two defences the
release adds around it: a hard ceiling on how many bytes a single transaction
may be decoded from, and a bound on how many peers the node will talk to at
once.

1. **Give every verified transaction a cost.**
   `VerifiedUnminedTx::cost(&self) -> u64` returns ZIP-401's cost function:
   the larger of the transaction's serialized size and the mempool cost
   threshold. `VerifiedUnminedTx::eviction_weight(self) -> u64` — note it
   **consumes** `self` — returns the cost plus a low-fee penalty charged when
   the transaction paid less than the conventional fee. The threshold and the
   penalty are values pinned in `TEST_INTERFACE.md` §2.3 (as upstream has them,
   inline in the accessor; their visibility is not part of the requirement, only
   their values are).

   > **The shipped specification disagrees with this milestone here, and this
   > milestone wins.** `/app/specs/zips/zip-0401.rst` is the *current* ZIP-401: it
   > sets the cost threshold at `10000`, the low-fee penalty at `40000`, and
   > describes the fee threshold as having been *lowered* to `1000` zatoshis
   > "as part of the deployment of ZIP 313 in `zcashd` v4.2.0". This milestone
   > reproduces a release made between those two revisions, and it implements
   > the **older** constants: threshold `4000`, penalty `16000`, fee threshold
   > `1000` zatoshis. The release's own source comments quote the older text.
   > For these three numbers, take the values from `TEST_INTERFACE.md` §2.3, not
   > from the specification. Everything else the spec says about the shape of
   > the algorithm — `cost = max(size, threshold)`,
   > `eviction_weight = cost + low_fee_penalty`, the penalty applying strictly
   > *below* the fee threshold — is normative as written.
2. **Cap what a transaction is decoded from.** `Transaction::zcash_deserialize`
   must read through a reader limited to `block::MAX_BLOCK_BYTES` (2,000,000
   bytes), so a transaction that claims to be longer than a whole block fails
   with an I/O error instead of reading to the end of the stream. The cap
   belongs on the transaction reader itself and must not disturb the existing
   protocol message limits, which stay where they are.
3. **Name the four ways a transaction spends something twice.**
   `zebra_consensus::TransactionError` gains `DuplicateTransparentSpend`,
   `DuplicateSproutNullifier`, `DuplicateSaplingNullifier` and
   `DuplicateOrchardNullifier`, each carrying the outpoint or nullifier it
   complains about, each with the exact `Display` string given in
   `TEST_INTERFACE.md` §4.3. A new private `spend_conflicts` check in
   `zebra-consensus` is what will raise them; the variants are what the rest of
   the node matches on.
4. **Bound the peer set.** `zebra_network::Config` gains three methods —
   `peerset_inbound_connection_limit`, `peerset_outbound_connection_limit` and
   `peerset_total_connection_limit` — derived from the existing
   `peerset_initial_target_size` field, with the outbound side biased above the
   inbound side by a new public
   `zebra_network::constants::OUTBOUND_PEER_BIAS_DENOMINATOR`. The bias exists
   so that a node is more likely to be connected to peers *it* chose than to
   peers that dialled it, which an attacker can generate for free.

The block codec from step 10 is carried forward unchanged and is checked again as
a regression guard — this milestone touches `Transaction`'s deserializer and the
`zebra-network` config, and a candidate that damages block parsing while doing
so must not go unnoticed.

## Read these local references

- `/app/specs/zips/zip-0401.rst` — the cost and eviction-weight definitions, the
  rationale for a floor at all, and the "RecentlyEvicted" set. **Read the
  warning under requirement 1 before you copy a constant out of it**: the two
  numbers this milestone uses are the pre-revision ones, not the ones printed
  there.
- `/app/specs/nu5.txt` §7.1 "Transaction Encoding and Consensus" — the encoding
  whose length the cost function measures, and the `MAX_BLOCK_BYTES` scale the
  new decode cap is taken from.
- `/app/TEST_INTERFACE.md` — the contract the tests are written against:
  every module path, type, signature, field, constant, error variant and exact
  `Display` string they touch. Read §2.3 and §3.3 before choosing the threshold.
- `/app/WORKSPACE_HANDOFF.md` — what carries forward, and what step 12 expects.
- Retained code this milestone extends:
  `zebra-chain/src/transaction/unmined.rs`,
  `zebra-chain/src/transaction/serialize.rs`,
  `zebra-chain/src/block.rs`,
  `zebra-consensus/src/error.rs`,
  `zebra-consensus/src/transaction/check.rs`,
  `zebra-network/src/config.rs`, `zebra-network/src/constants.rs`.
  `rg -n 'MEMPOOL_TRANSACTION_COST_THRESHOLD|mempooltxcostlimit' zebra-chain
  zebra-consensus` finds where the constant is declared and who reads it.

## Explicit out-of-scope decisions (this step)

`TEST_INTERFACE.md` §6 restates these as what the tests can and cannot see.

- **The eviction policy itself.** The random-weighted drop out of the
  "RecentlyEvicted" set, the `mempooltxcostlimit` accounting and the sweep that
  runs it live in `zebrad`, which **no group in this step covers**: every
  driver is injected into `zebra-chain`, `zebra-consensus` or `zebra-network`,
  so nothing here reaches `zebrad`. Build them as you like; what is in scope is
  the `zebra-chain` accessors they are written against.
- **`spend_conflicts` itself.** `zebra-consensus/src/transaction/check.rs` is a
  private module, so the function that *produces* the four new errors is not
  callable from a test. The error variants, their payload types, their `Display`
  strings and their `PartialEq` are in scope; the conflict detection logic is
  not.
- **Anywhere the transaction size is actually bounded in practice.** The new
  reader cap is checked through `Transaction::zcash_deserialize`'s observable
  behaviour on an oversized input; the `OutboundMessage` framing and the
  `MAX_PROTOCOL_MESSAGE_LEN` limit are unchanged and out of scope.
- **Anything that consumes the connection limits.** The peer-set builder, the
  accept loop, the handshake and the connection tracker are all
  `pub(crate)`-or-lower inside `zebra-network`; only the `Config` arithmetic and
  the public constant are reachable.
- **Halo2/RedPallas verification**, the script interpreter, mining, wallet, RPC,
  `lightwalletd`, Tor and NU6 — still out of scope per `REQUIREMENTS.md`.

## Meta requirements

- **Budget and workspace.** `bash /app/timer.sh` shows elapsed and remaining
  time. `/app` carries forward in full (minus `.git/` and `target/`) to step 12,
  so leave it building.
- **Network.** This environment has no network access, and everything the
  build needs is already installed. The upstream project's source, its
  releases, mirrors and package registries are unreachable, so a copy cannot
  be fetched by any route. Searching for, downloading, copying, wrapping or
  invoking an implementation of this milestone — or any task-specific
  answer — is prohibited.
- **Workspace scope.** Tests are added to the `tests/` directories of
  `zebra-chain`, `zebra-consensus` and `zebra-network` when the milestone is
  checked, compiled
  against the manifests as you leave them. `--offline` must keep working: add no
  new third-party dependency, and remove no existing one. In particular
  `zebra-chain` must keep its `hex` dependency and `zebra-network` must keep
  `hex`; if `zebra-network` has no `tests/` directory yet, create one.
- **Local checks.** `cargo build --offline --workspace`, `cargo test --offline
  -p zebra-chain -p zebra-consensus -p zebra-network`, and `cargo check --offline
  --workspace --all-targets --all-features`. The last one is not optional here:
  `zebra-chain/benches/block.rs` reaches
  `zebra_chain::block::tests::generate`, which is gated behind
  `#[cfg(any(test, feature = "bench"))]`, so a bench target only compiles with
  `--all-features`.
- **Handoff.** Do not delete `/app/specs/`, `/app/TEST_INTERFACE.md`,
  `/app/WORKSPACE_HANDOFF.md` or `/app/timer.sh`.
