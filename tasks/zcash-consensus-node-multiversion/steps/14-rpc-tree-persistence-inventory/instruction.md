# Step 14 — JSON-RPC server, ZIP-212 grace-period checkpoint, and extended chain-tip interface

## Implement this milestone

`/app` already builds and passes its own tests from steps 1–13. This step
adds three related capabilities:

1. **The `zebra-rpc` crate and its JSON-RPC server.** A new top-level crate
   exposes seven methods compatible with the zcashd RPC interface that
   `lightwalletd` depends on: `getinfo`, `getblockchaininfo`,
   `sendrawtransaction`, `getblock`, `getbestblockhash`, `getrawmempool`,
   and `getrawtransaction`. The server is backed by a Tower `Buffer<Mempool,
   ...>` service and a `ReadRequest`/`ReadResponse` state service.

2. **ZIP-212 grace-period mandatory checkpoint.** `Network::mandatory_checkpoint_height()`
   previously returned the bare Canopy activation height. At this milestone it
   must return that height plus the ZIP-212 grace period of 32 256 blocks, so
   that Zebra can skip ZIP-212 validation before the checkpoint without
   allowing a window where invalid notes are accepted.

3. **Extended `ChainTip` trait.** Four methods are added to the `ChainTip`
   trait in `zebra-chain`:
   - `best_tip_height_and_hash()` → `Option<(block::Height, block::Hash)>`
   - `best_tip_block_time()` → `Option<DateTime<Utc>>`
   - `best_tip_height_and_block_time()` → `Option<(block::Height, DateTime<Utc>)>`
   - `estimate_network_chain_tip_height(network, now)` → `Option<block::Height>`
     (default method; delegates to `best_tip_height_and_block_time`)

   `NoChainTip` implements all four by returning `None`.

   Additionally, `Network::bip70_network_name()` is added, returning `"main"`
   for `Mainnet` and `"test"` for `Testnet`, matching zcashd's BIP-70 naming.

## Read these local references

- `/app/specs/zips/zip-0212.rst` — ZIP-212: Receive Sapling/Orchard notes with
  diversified viewing keys. The grace period (§Specification) is 32 256 blocks
  after Canopy activation; outputs in that window are exempt from the new rule.
  The mandatory checkpoint at Canopy + 32 256 lets Zebra skip ZIP-212
  validation entirely before that point.
- `/app/TEST_INTERFACE.md` — the contract the tests are written against
  for this step. Read it before touching any public type or method.
- `/app/WORKSPACE_HANDOFF.md` — what carries forward from steps 1–13 and what
  the next milestone expects to find.
- `zebra-rpc/src/methods.rs`, `zebra-rpc/src/config.rs`,
  `zebra-rpc/src/server.rs` — the three files that make up this crate. The
  crate itself already exists at the predecessor as an empty stub, so this is a
  fill-in rather than a new workspace member; `Cargo.toml` depends on `zebra-chain`, `zebra-network`,
  `zebra-node-services`, `zebra-state`, `jsonrpc-core = "18"`,
  `jsonrpc-derive = "18"`, `jsonrpc-http-server = "18"`, `serde`,
  `serde_json`, `tokio`, `futures`, `chrono`, and `hyper`. Three of them need
  a feature flag that is not on by default, and the crate does not compile
  without them:

  ```toml
  hex      = { version = "0.4", features = ["serde"] }        # #[serde(with = "hex")]
  indexmap = { version = "1.8", features = ["serde"] }        # the `upgrades` map
  tower    = { version = "0.4", features = ["buffer", "util"] } # Buffer<Mempool, _>
  ```

  `serde_json` additionally needs `features = ["preserve_order"]` so the
  `upgrades` map keeps its insertion order on the wire.
- `zebra-node-services/` — a new crate added at this step that exposes the
  mempool service interface (`mempool::Request`, `mempool::Response`,
  `mempool::Gossip`) and `pub type BoxError`.
- `zebra-chain/src/chain_tip.rs` — extend the existing `ChainTip` trait.
- `zebra-chain/src/parameters/network.rs` — add `bip70_network_name` and
  update `mandatory_checkpoint_height`.
- `zebra-chain/src/transaction.rs` — rename the two `LockTime` timestamp
  constructors to `min_lock_time_timestamp()` and `max_lock_time_timestamp()`
  (from `min_lock_time()` / `max_lock_time()`); `unlocked()` and the variants
  are unchanged. `TEST_INTERFACE.md` §5 states the same requirement.
- `zebra-network/src/constants.rs` — bump `USER_AGENT` to this milestone's
  version string, `/Zebra:1.0.0-beta.6/`; two test cases assert that literal
  (`TEST_INTERFACE.md` §3 and §4).

## Add these new items

### `zebra-node-services` crate

```toml
# zebra-node-services/Cargo.toml
[package]
name = "zebra-node-services"
version = "1.0.0-beta.6"
edition = "2021"

[dependencies]
zebra-chain = { path = "../zebra-chain" }
```

```rust
// zebra_node_services lib.rs
pub mod mempool;
pub type BoxError = Box<dyn std::error::Error + Send + Sync + 'static>;
```

`mempool::Request` variants (all `pub`): `TransactionIds`,
`TransactionsById(HashSet<UnminedTxId>)`,
`TransactionsByMinedId(HashSet<Hash>)`,
`RejectedTransactionIds(HashSet<UnminedTxId>)`,
`Queue(Vec<Gossip>)`,
`CheckForVerifiedTransactions`.

`mempool::Response` variants (all `pub`): `TransactionIds(HashSet<UnminedTxId>)`,
`Transactions(Vec<UnminedTx>)`, `RejectedTransactionIds(HashSet<UnminedTxId>)`,
`Queued(Vec<Result<(), BoxError>>)`, `CheckedForVerifiedTransactions`.

`pub enum Gossip` (defined in `zebra-node-services`'s `mempool/gossip.rs`,
re-exported as `zebra_node_services::mempool::Gossip` — the re-export path is
the one the drivers use), in declaration order:
`Id(UnminedTxId)` then `Tx(UnminedTx)` — note the ID variant is named `Id`,
not `UnminedTxId`, and the full-transaction variant carries `UnminedTx`, not
`Arc<Transaction>`. It also provides `fn id(&self) -> UnminedTxId` and
`From<UnminedTxId>` / `From<UnminedTx>` impls.

### `zebra-chain` additions

```rust
// Network::mandatory_checkpoint_height (updated)
pub fn mandatory_checkpoint_height(&self) -> Height {
    let canopy_activation = Canopy
        .activation_height(*self)
        .expect("Canopy activation height must be present for both networks");
    (canopy_activation + ZIP_212_GRACE_PERIOD_DURATION)
        .expect("ZIP-212 grace period ends at a valid block height")
}
// const ZIP_212_GRACE_PERIOD_DURATION: i32 = 32_256;
```

Concrete values: Mainnet 1 046 400 + 32 256 = **1 078 656**;
Testnet 1 028 500 + 32 256 = **1 060 756**.

Two serialization wrappers are also new in `zebra-chain` at this tag — they
do not exist at the predecessor, and `zebra-rpc`'s response types wrap them:

```rust
// zebra_chain::block (block/serialize.rs)
pub struct SerializedBlock { /* private */ }
impl<B: Borrow<Block>> From<B> for SerializedBlock { ... }   // not From<Vec<u8>>

// zebra_chain::transaction (transaction/serialize.rs)
pub struct SerializedTransaction { /* private */ }
impl From<Vec<u8>> for SerializedTransaction { ... }
```

Note the asymmetry in how each is constructed; `TEST_INTERFACE.md §3` depends
on it.

```rust
// Network::bip70_network_name (new)
pub fn bip70_network_name(&self) -> String {
    match self {
        Network::Mainnet => "main".to_string(),
        Network::Testnet => "test".to_string(),
    }
}
```

```rust
// ChainTip trait additions (zebra_chain::chain_tip)
fn best_tip_height_and_hash(&self) -> Option<(block::Height, block::Hash)>;
fn best_tip_block_time(&self) -> Option<DateTime<Utc>>;
fn best_tip_height_and_block_time(&self) -> Option<(block::Height, DateTime<Utc>)>;
// default method:
fn estimate_network_chain_tip_height(
    &self,
    network: Network,
    now: DateTime<Utc>,
) -> Option<block::Height> { ... }
```

`NoChainTip` must implement all four, returning `None` for every
`Option`-returning method.

### `zebra-rpc` crate

The crate is structured as three source files:

- `src/config.rs` — `pub struct Config { pub listen_addr: Option<SocketAddr> }`.
  The field is an `Option`, not a bare `SocketAddr`: the RPC server is
  **disabled by default**, and `None` is what disables it. The struct derives
  `Default` (so the default is `None`) and carries
  `#[serde(deny_unknown_fields, default)]`. Not checked directly; must exist
  for the workspace to build.
- `src/server.rs` — `pub fn init(...)` that starts the HTTP/JSON-RPC listener.
  Not checked directly.
- `src/methods.rs` — the in-scope public API (see `TEST_INTERFACE.md`).

The `#[rpc(server)]` attribute from `jsonrpc-derive` generates an
`RpcServer` blanket impl; the `Rpc` trait (seven methods) and `RpcImpl`
(the concrete struct) are both `pub`.

`RpcImpl::new` signature:

```rust
pub fn new<Version: ToString>(
    app_version: Version,
    mempool: Buffer<Mempool, mempool::Request>,
    state: State,
    latest_chain_tip: Tip,
    network: Network,
) -> Self
```

where `Mempool: Service<mempool::Request, Response=mempool::Response,
Error=BoxError>`, `State: Service<zebra_state::ReadRequest,
Response=zebra_state::ReadResponse, Error=zebra_state::BoxError>`,
`Tip: ChainTip`.

## Explicit out-of-scope decisions (this step)

- **Inventory registry** (`zebra-network::peer_set`): `InventoryChange` and
  `InventoryStatus` are new at this tag; `InventoryRegistry` already exists at
  the predecessor and is reworked here. All three are declared `pub`, but the
  `peer_set` module itself is private (`zebra-network/src/lib.rs`:
  `mod peer_set;`), so no integration test can reach them. Documented in
  `TEST_INTERFACE.md §10` as negative space.
- **Shielded note commitment tree persistence** (`zebra-state`'s
  `ZebraDb`/`DiskDb` refactor): the disk-format rewrite is internal. The
  `ReadRequest` and `ReadResponse` enums, however, are **new public types**
  here — they do not exist at the predecessor at all — and `RpcImpl::new`
  above is generic over `Service<ReadRequest, Response = ReadResponse>`, so
  you must add both and re-export them from `zebra-state`'s crate root. Their
  variants are out of scope and no driver constructs one. No new requirement
  group is added for the persistence work itself, and the regression driver
  (`block-codec-regression`) does **not** reach it: that driver reads blocks, not
  the state service, and no behaviour of `zebra-state` is in scope anywhere in
  this step.
- **`copy-state` and `notfound` P2P messages**: added at the network layer but
  not reachable through the public `zebra-network` or `zebra-rpc` API in
  integration tests.
- Proof and signature verification, script interpreter, mining/wallet
  internals, lightwalletd, NU6 — unchanged from earlier steps' scope.

## Meta requirements

- **Budget.** `bash /app/timer.sh` shows elapsed and remaining time.
- **Cumulative workspace.** `/app` carries forward in full (minus `.git/`
  and `target/`) to any later milestone. Leave it building.
- **Network.** This environment has no network access, and everything the
  build needs is already installed. The upstream project's source, its
  releases, mirrors and package registries are unreachable, so a copy cannot
  be fetched by any route. Searching for, downloading, copying, wrapping or
  invoking an implementation of this milestone — or any task-specific
  answer — is prohibited.
- **Workspace scope.** Tests are added to `zebra-chain/tests/`,
  `zebra-rpc/tests/` and `zebra-node-services/tests/` when the milestone is
  checked,
  compiled against the manifests as you leave them. All three crates must be
  workspace members: a driver injected into a crate that is absent means its
  whole group cannot pass. `--offline` must keep working.
- **Local checks.** `cargo check --offline --workspace --all-targets
  --all-features` and `cargo test --offline -p zebra-chain -p zebra-rpc
  -p zebra-node-services` are the most useful local checks.
- **Handoff.** Do not delete `/app/specs/`, `/app/TEST_INTERFACE.md`,
  `/app/WORKSPACE_HANDOFF.md`, or `/app/timer.sh`.
