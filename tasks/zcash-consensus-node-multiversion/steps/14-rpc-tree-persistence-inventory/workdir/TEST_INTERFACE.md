# TEST_INTERFACE.md — milestone 14, JSON-RPC server, ZIP-212 grace-period checkpoint, extended chain tip

This document is the contract the tests are written against. Every
module path, type, method, field, constant, error variant, and byte layout
the verifier depends on is named here. If a behaviour is not stated here or
in a specification named by `instruction.md`, it is not in scope.

## 1. How the verifier reaches your code

The verifier's **integration test groups** run test files injected into three
crates' `tests/` directories:

```
cargo test --offline --locked --jobs 4 -p zebra-chain          --test <name> -- --test-threads=1
cargo test --offline --locked --jobs 4 -p zebra-rpc            --test <name> -- --test-threads=1
cargo test --offline --locked --jobs 4 -p zebra-node-services  --test <name> -- --test-threads=1
```

(If `--offline --locked` cannot resolve, the verifier retries with `--offline`
alone, then `--locked` alone, then neither.) The remaining groups run
whole-workspace commands instead of a test file: `cargo build --offline -p zebra-chain
--tests` and `cargo check --offline --workspace --all-targets --all-features`.

Consequences you must respect:

- Tests reach your code **only through the public API** of whichever crate
  they are compiled into, as an external consumer. `pub(crate)`, `pub(super)`,
  and private items are invisible.
- Each test file is compiled against that crate's `Cargo.toml` as you leave
  it. Do not remove an existing `[dependencies]` or `[dev-dependencies]` entry.
- The workspace must keep building **offline**; do not add a new third-party
  dependency to any manifest without also adding it to the lock file.
- A group whose driver fails to compile fails for that group.
- Tests run with `--test-threads=1` and must not depend on execution order.

### How this step is checked

The gate groups are `build`, `workspace-checks` and `block-codec-regression`.
They add no new requirement and only veto: if any one of them fails, the step
is not satisfied no matter how much of the new work you completed.

The requirement groups carry this step's new capability and its peripheral
breadth; every case in them is in scope.

The practical consequence: preserving the inherited behaviour the gate
checks is necessary but adds nothing by itself, and doing nothing else
leaves all of the new work undone.

## 2. Where the implementation goes

| Module | What this milestone adds |
| --- | --- |
| `zebra_chain::chain_tip` | Four new methods on `ChainTip` trait; `NoChainTip` implements all of them |
| `zebra_chain::parameters::Network` | `bip70_network_name()` and updated `mandatory_checkpoint_height()` |
| `zebra_chain::block` | `SerializedBlock` — **new** at this tag, absent from the predecessor; the payload of `GetBlock` |
| `zebra_chain::transaction` | `SerializedTransaction` — **new** at this tag, absent from the predecessor; the payload of `GetRawTransaction` |
| `zebra_chain::parameters::ConsensusBranchId` | reachable through `hex::ToHex` — `encode_hex()` renders the id as four bytes, most significant first (§4) |
| `zebra_chain::block::Hash`, `zebra_chain::transaction::Hash` | reachable through `hex::ToHex` — `encode_hex()` renders the hash in the same big-endian display order as `Display` (§4) |
| `zebra_node_services` | New crate: mempool service interface (`Request`, `Response`, `Gossip`), `BoxError` |
| `zebra_rpc` | Crate exists at the predecessor as an empty stub; this milestone adds the `methods`, `config` and `server` modules and the seven-method RPC server |

Internal file layout is yours; only the names below, at the paths given, are
in scope.

## 3. RPC response-type shapes — group `rpc-response-types` (`zebra-rpc`)

This group tests the serde contracts of the five public response types in
`zebra_rpc::methods`. It does not construct `RpcImpl` directly (which
requires live Tower services); it checks type shapes through serialization and
deserialization.

**`GetInfo`** — response to `getinfo`:

```rust
#[derive(Clone, Debug, Eq, PartialEq, serde::Serialize, serde::Deserialize)]
pub struct GetInfo {
    build: String,      // private field, accessible only through serde
    subversion: String, // private field, accessible only through serde
}
```

Serialized JSON shape: `{"build":"<app_version>","subversion":"<user_agent>"}`.
The `subversion` value is `"/Zebra:1.0.0-beta.6/"` (the `USER_AGENT` constant
from `zebra_network::constants`).

**`GetBestBlockHash`** — response to `getbestblockhash`:

```rust
#[derive(Copy, Clone, Debug, Eq, PartialEq, serde::Deserialize, serde::Serialize)]
pub struct GetBestBlockHash(#[serde(with = "hex")] block::Hash);
```

Serialized JSON: a bare lowercase hex string (no wrapper object), e.g.
`"00040fe8ec8471911baa1db1266ea15dd06b4a8a5c453883c000b031973dce08"`.

**`GetBlock`** — response to `getblock` (non-verbose, the only mode
`lightwalletd` uses):

```rust
#[derive(Clone, Debug, Eq, PartialEq, serde::Serialize)]
pub struct GetBlock(#[serde(with = "hex")] SerializedBlock);
```

`SerializedBlock` is `zebra_chain::block::SerializedBlock`. It implements
`From<B: Borrow<Block>>` (not `From<Vec<u8>>`), so the canonical constructor is:

```rust
use zebra_chain::block::SerializedBlock;
let sb = SerializedBlock::from(&block); // block: &Block or Arc<Block>
```

`GetBlock`'s inner `SerializedBlock` field is private; there is no public
constructor callable from outside the crate. Integration tests cannot instantiate
`GetBlock` directly. The test verifies that `GetBlock` is exported from
`zebra_rpc::methods` and implements `serde::Serialize` (a trait-bound compile
check). No `Deserialize` derive exists on `GetBlock`.

Serializes as a bare hex string. Deserialize is not derived (not needed for
the tests).

**`GetRawTransaction`** — response to `getrawtransaction`:

```rust
#[derive(Clone, Debug, Eq, PartialEq, serde::Serialize)]
#[serde(untagged)]
pub enum GetRawTransaction {
    Raw(#[serde(with = "hex")] SerializedTransaction),
    Object {
        #[serde(with = "hex")]
        hex: SerializedTransaction,
        height: i32,   // -1 if not in a mined block
    },
}
```

`SerializedTransaction` is `zebra_chain::transaction::SerializedTransaction`.
It implements `From<Vec<u8>>`, so the canonical constructor is:

```rust
use zebra_chain::transaction::SerializedTransaction;
let st = SerializedTransaction::from(raw_bytes); // raw_bytes: Vec<u8>
```

`Raw` serializes as a bare hex string. `Object` serializes as
`{"hex":"<hex>","height":<i32>}`. The `#[serde(untagged)]` means the JSON
shape alone determines the wire form — `Raw` emits a bare JSON string and
`Object` a two-key JSON object, with no variant tag anywhere. Note that only
`Serialize` is derived on this enum (see the derive list above): nothing ever
deserializes *into* a `GetRawTransaction`, so `untagged` affects the output
shape only.

**`GetBlockChainInfo`** — response to `getblockchaininfo`:

```rust
#[derive(Clone, Debug, Eq, PartialEq, serde::Serialize, serde::Deserialize)]
pub struct GetBlockChainInfo {
    chain: String,
    blocks: u32,
    #[serde(rename = "bestblockhash")]
    best_block_hash: GetBestBlockHash,
    #[serde(rename = "estimatedheight")]
    estimated_height: u32,
    upgrades: IndexMap<ConsensusBranchIdHex, NetworkUpgradeInfo>,
    consensus: TipConsensusBranch,
}
```

All fields are private. The `serde(rename)` attributes mean the JSON keys
are `"bestblockhash"` and `"estimatedheight"` (no underscore, matching
zcashd). `chain` is `"main"` for Mainnet and `"test"` for Testnet.

The `upgrades` value is an `IndexMap` keyed by `ConsensusBranchIdHex` (a
struct wrapping a `ConsensusBranchId` serialized as lowercase hex, e.g.
`"e9ff75a6"` for Canopy). Each entry is a `NetworkUpgradeInfo` with fields:
`name: NetworkUpgrade` (serialized as the upgrade's string name),
`activationheight: Height` (u32), `status: NetworkUpgradeStatus` (one of
`"active"`, `"disabled"`, `"pending"`).

The `consensus` value is a `TipConsensusBranch` with fields:
`chaintip: ConsensusBranchIdHex`, `nextblock: ConsensusBranchIdHex`.

**Other public types used in `methods.rs`**:

```rust
#[derive(Copy, Clone, Debug, Eq, PartialEq, serde::Serialize, serde::Deserialize)]
pub struct SentTransactionHash(#[serde(with = "hex")] transaction::Hash);
```

Serializes as a bare hex string (the transaction hash).

**The `Rpc` trait** (seven methods, all `pub`):

```rust
#[rpc(server)]
pub trait Rpc {
    #[rpc(name = "getinfo")]
    fn get_info(&self) -> Result<GetInfo>;

    #[rpc(name = "getblockchaininfo")]
    fn get_blockchain_info(&self) -> Result<GetBlockChainInfo>;

    #[rpc(name = "sendrawtransaction")]
    fn send_raw_transaction(&self, raw_transaction_hex: String) -> BoxFuture<Result<SentTransactionHash>>;

    #[rpc(name = "getblock")]
    fn get_block(&self, height: String, verbosity: u8) -> BoxFuture<Result<GetBlock>>;

    #[rpc(name = "getbestblockhash")]
    fn get_best_block_hash(&self) -> Result<GetBestBlockHash>;

    #[rpc(name = "getrawmempool")]
    fn get_raw_mempool(&self) -> BoxFuture<Result<Vec<String>>>;

    #[rpc(name = "getrawtransaction")]
    fn get_raw_transaction(&self, txid_hex: String, verbose: u8) -> BoxFuture<Result<GetRawTransaction>>;
}
```

`Result<T>` is `jsonrpc_core::Result<T>`. `BoxFuture<T>` is
`jsonrpc_core::BoxFuture<T>`.

## 4. The RPC server implementation — group `rpc-server-impl` (`zebra-rpc`)

§3 checks the *shapes* of the response types. This group checks the thing that
produces them. A `methods.rs` holding six serde structs and a bare
seven-method `pub trait Rpc` declaration satisfies every assertion in §3
without `RpcImpl` existing at all; this group's driver constructs one and
calls it.

`RpcImpl` is a public generic struct with a public constructor:

```rust
pub struct RpcImpl<Mempool, State, Tip>
where
    Mempool: Service<mempool::Request, Response = mempool::Response, Error = BoxError>,
    State: Service<
            zebra_state::ReadRequest,
            Response = zebra_state::ReadResponse,
            Error = zebra_state::BoxError,
        > + Clone + Send + Sync + 'static,
    Tip: ChainTip,
{ /* all fields private */ }

impl<Mempool, State, Tip> RpcImpl<Mempool, State, Tip> /* same bounds */ {
    pub fn new<Version>(
        app_version: Version,
        mempool: Buffer<Mempool, mempool::Request>,
        state: State,
        latest_chain_tip: Tip,
        network: Network,
    ) -> Self
    where
        Version: ToString;
}
```

Five things in that signature are load-bearing; the driver does not compile if
any of them moves:

- the **parameter order** `(app_version, mempool, state, latest_chain_tip,
  network)`;
- `app_version` is generic over `Version: ToString`, not a `String`;
- `mempool` is a **`tower::buffer::Buffer<Mempool, mempool::Request>`**, not a
  bare `Mempool`: the caller wraps the service, the constructor does not;
- the two error types are the two *distinct* boxed aliases named above —
  `zebra_node_services::BoxError` for the mempool and `zebra_state::BoxError`
  for the state — not one shared type;
- `Tip` is any `ChainTip` implementor, with no `Clone` bound on it.

`RpcImpl` must then implement the `Rpc` trait of §3, under bounds a
driver-supplied stub can satisfy:

```rust
impl<Mempool, State, Tip> Rpc for RpcImpl<Mempool, State, Tip>
where
    Mempool: Service<mempool::Request, Response = mempool::Response, Error = BoxError> + 'static,
    Mempool::Future: Send,
    State: Service<
            zebra_state::ReadRequest,
            Response = zebra_state::ReadResponse,
            Error = zebra_state::BoxError,
        > + Clone + Send + Sync + 'static,
    State::Future: Send,
    Tip: ChainTip + Send + Sync + 'static,
{ /* the seven methods */ }
```

**How the driver builds one — no node, and no Tokio runtime.** The mempool
argument comes from `tower::buffer::Buffer::pair(stub, 1)`: `Buffer::new`
calls `tokio::spawn` and would panic outside a runtime, but `Buffer::pair`
hands the worker back to the caller instead of spawning it. The state is a
driver-local `tower::Service<zebra_state::ReadRequest>` stub and the tip is a
driver-local `ChainTip` implementor returning fixed values. None of the three
is ever called: the driver exercises only the three methods that return
`jsonrpc_core::Result<T>` synchronously. The four `BoxFuture` methods are left
to the compile-time signature check in §3.

**What is asserted:**

- `RpcImpl<_, _, Tip>` implements `Rpc`, instantiated through a generic
  `fn assert_implements_rpc<T: Rpc>()` at both a driver-local tip type and at
  `NoChainTip`.
- `get_info()` returns `build` equal to the `app_version` handed to `new`, and
  `subversion` equal to `zebra_network::constants::USER_AGENT`, which at this
  tag is exactly `"/Zebra:1.0.0-beta.6/"`. `subversion` is not a driver input:
  the implementation must source it from that constant, and the driver
  asserts the constant's value separately.
- `get_blockchain_info()` reports the tip that the `Tip` parameter supplies:
  `chain` is `network.bip70_network_name()` (§9), `blocks` is the tip height,
  `bestblockhash` is the tip hash as lowercase hex, and `estimatedheight` is
  at least `blocks`. (`estimatedheight` is projected from the tip's block time
  to the current wall clock, so only that relation is asserted, never an exact
  number.)
- `get_blockchain_info()`'s `upgrades` map is keyed by each upgrade's
  **consensus branch id** in lowercase hex. The driver derives Canopy's key
  independently — `NetworkUpgrade::Canopy.branch_id()` rendered through
  `hex::ToHex` — asserts it is `"e9ff75a6"`, then looks that key up in the map
  the implementation built and checks `name`, `activationheight` and `status`,
  and that `consensus.chaintip` agrees with the tip's upgrade. A tip one block
  below Canopy activation must give Canopy `status: "pending"`.
- `get_best_block_hash()` returns the tip hash.
- With `NoChainTip`, whose accessors all return `None`,
  `get_blockchain_info()` and `get_best_block_hash()` return `Err`. Only
  `is_err()` is asserted — the JSON-RPC error code and message are **not**
  part of this contract and you may choose them freely.

**The `hex::ToHex` implementations both drivers compile against**, and the
reason they are asserted rather than assumed. Both drivers do `use hex::ToHex;`
and call `.encode_hex::<String>()` directly, on values they construct
themselves — a `ConsensusBranchId` from `NetworkUpgrade::branch_id()`, a
`block::Hash` from a literal 32-byte array. So the trait has to be reachable
on all three types, from `zebra-chain`, on the **owned** value (the receiver
is a value, not a reference, so `impl ToHex for &T` alone is not enough — the
`for T` impl is what resolves), and the string it produces has to be the one
the RPC responses carry:

```rust
// zebra_chain::parameters::network_upgrade — `ConsensusBranchId(u32)`, private field
impl hex::ToHex for ConsensusBranchId;
impl hex::ToHex for &ConsensusBranchId;  // the reference form, as upstream writes the pair
// zebra_chain::block::Hash / zebra_chain::transaction::Hash
impl hex::ToHex for Hash;
impl hex::ToHex for &Hash;
```

- **`ConsensusBranchId` renders four bytes, most significant first.**
  `NetworkUpgrade::Canopy.branch_id()` must encode to exactly `"e9ff75a6"` —
  this is the one hex string in this step asserted against a literal, and it
  is also the key the `upgrades` map must use, so a `ToHex` that reverses the
  four bytes fails both. The type's `u32` field is private and it has no byte
  accessor at the predecessor, so the impl belongs in `zebra-chain` beside the
  type; `hex` is already one of its dependencies.
- **A hash renders in the display order `Display` already produces** — the 32
  bytes reversed relative to the internal field, i.e. exactly the bytes
  `Block::hash().to_string()` prints. The driver takes the tip hash it handed
  the `Tip` stub as raw bytes, renders it with this impl, and compares the
  result to the JSON the implementation emitted for `bestblockhash` and
  `getbestblockhash`; the two must be the same string, so the impl and the
  response serialization have to agree on that order.

`hex` 0.4 supplies `ToHex` for any type that implements `AsRef<[u8]>`, so an
`impl AsRef<[u8]>` yielding the display-order bytes is an equally valid route
on these types — but it must yield the display order, not the internal field
order, because §3's `GetBestBlockHash` example (the genesis hash,
`"00040fe8…"`) and the `Display` impl it agrees with are both display order.
Note the two routes are exclusive on a given type: `hex`'s blanket impl means
a type with `AsRef<[u8]>` *already* has `ToHex`, and an explicit `impl ToHex`
alongside it is a coherence error. So pick per type — on the hashes, whose
`AsRef` may already exist for another reason, make that `AsRef` yield the
display order; on `ConsensusBranchId`, which has no byte view at all, write
the impl out.
`ToHex` is also what `#[serde(with = "hex")]` calls: for
`SentTransactionHash`'s `transaction::Hash` (whose deserialize half needs
`FromHex` instead — no driver calls it, but §3's round-trip test needs it), and
for the `SerializedBlock`/`SerializedTransaction` wrappers, which get it
through their own `AsRef<[u8]>`.

Because the driver names `zebra_state::{ReadRequest, ReadResponse, BoxError}`
in order to write its state stub, those three must be publicly re-exported
from `zebra-state` (§10). It never names a `ReadRequest` or `ReadResponse`
*variant*, so the variant sets remain out of scope.

`zebra-rpc` at the predecessor tag is an empty stub with no `[dependencies]`
and no modules, so `zebra_rpc::methods::RpcImpl` does not resolve there and
the whole file fails to compile against app-13.

## 5. Mempool service interface — group `node-services-mempool-interface` (`zebra-node-services`)

`zebra-node-services` is a whole new crate at this tag, with `zebra-chain` as
its only dependency. It holds the service interface that `zebra-rpc` and
`zebrad` both talk to, so that neither has to depend on the mempool
implementation. The driver for this group is injected into
`zebra-node-services/tests/`; the crate must therefore be a workspace member,
or the group records zero with "crate `zebra-node-services` is absent from the
candidate workspace".

```rust
// zebra_node_services/src/lib.rs
pub type BoxError = Box<dyn std::error::Error + Send + Sync + 'static>;
pub mod mempool;
```

```rust
// zebra_node_services/src/mempool.rs
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum Gossip {
    Id(UnminedTxId),
    Tx(UnminedTx),
}

impl Gossip {
    pub fn id(&self) -> UnminedTxId;
}

impl From<UnminedTxId> for Gossip { /* -> Gossip::Id */ }
impl From<UnminedTx>   for Gossip { /* -> Gossip::Tx  */ }

#[derive(Debug, Eq, PartialEq)]
pub enum Request {
    TransactionIds,
    TransactionsById(HashSet<UnminedTxId>),
    TransactionsByMinedId(HashSet<transaction::Hash>),
    RejectedTransactionIds(HashSet<UnminedTxId>),
    Queue(Vec<Gossip>),
    CheckForVerifiedTransactions,
}

#[derive(Debug)]
pub enum Response {
    TransactionIds(HashSet<UnminedTxId>),
    Transactions(Vec<UnminedTx>),
    RejectedTransactionIds(HashSet<UnminedTxId>),
    Queued(Vec<Result<(), BoxError>>),
    CheckedForVerifiedTransactions,
}
```

`UnminedTx`, `UnminedTxId` and `transaction::Hash` are
`zebra_chain::transaction` types that already exist in the inherited tree.

**One inherited type is renamed by this milestone.** `transaction::LockTime`
keeps its variants (`Height`, `Time`) and its `unlocked()` constructor, but the
two timestamp constructors are **renamed**:

```rust
impl LockTime {
    pub fn unlocked() -> LockTime;                     // unchanged
    pub fn min_lock_time_timestamp() -> LockTime;      // was `min_lock_time()`
    pub fn max_lock_time_timestamp() -> LockTime;      // was `max_lock_time()`
}
```

The driver builds one of its two fixture transactions with
`LockTime::min_lock_time_timestamp()`, so a tree that does not carry the new
name does not compile the group; keeping the old name alongside it is
harmless, since nothing asserts its absence. This is the only `LockTime` requirement this step
adds; `LockTime` itself is otherwise unchanged from the steps that introduced
it.

What the driver asserts:

- `Gossip::Id(id).id() == id`, and `Gossip::Tx(tx).id() == tx.id` — the `Tx`
  arm returns the wrapped transaction's own id rather than recomputing or
  defaulting one. The two variants do not compare equal even when they
  describe the same transaction.
- Both `From` conversions land on the variant named above: `From<UnminedTxId>`
  gives `Id`, `From<UnminedTx>` gives `Tx`, and the converted value keeps the
  transaction's id.
- All six `Request` variants are constructable with the payload types above,
  and `Eq`/`PartialEq` are derived. `TransactionsById` and
  `RejectedTransactionIds` carry the same payload type but must **not** compare
  equal — collapsing them into one variant fails this group.
- All five `Response` variants are constructable and carry the payloads above.
  `Response` derives only `Debug`, so the driver reads its payloads back by
  matching rather than comparing; do not remove that `Debug`.
- `BoxError` is the boxed-trait-object alias exactly as written: it accepts a
  `&str` through `into()`, accepts any `std::error::Error + Send + Sync +
  'static`, and is itself `Send + Sync`. A crate-local error enum in its place
  fails both this group and `RpcImpl`'s bounds in §4.

`zebra-node-services` is not a workspace member at the predecessor tag and no
directory of that name exists there, so this group is guaranteed to fail on a
no-op continuation.

## 6. ZIP-212 grace-period mandatory checkpoint — group `zip212-mandatory-checkpoint` (`zebra-chain`)

`Network::mandatory_checkpoint_height(&self) -> Height` returns:

```
Canopy activation height + ZIP_212_GRACE_PERIOD_DURATION
```

where `ZIP_212_GRACE_PERIOD_DURATION = 32_256` (a private `const i32` in
`zebra_chain::parameters::network`). **Do not expose this constant publicly;**
the driver computes its own copy from the activation heights.

Concrete values (computed from the public activation heights):

| Network | Canopy activation | Mandatory checkpoint |
| --- | --- | --- |
| Mainnet | 1 046 400 | **1 078 656** |
| Testnet | 1 028 500 | **1 060 756** |

The predecessor tree (app-13) returned the bare Canopy activation height for
`mandatory_checkpoint_height`. The assertions above fail on it.

Three `NetworkUpgrade` associated functions are used by this group's driver
and must keep their names, argument order and return types:

```rust
// zebra_chain::parameters::NetworkUpgrade
pub fn activation_height(&self, network: Network) -> Option<block::Height>;
pub fn current(network: Network, height: block::Height) -> NetworkUpgrade;
pub fn is_activation_height(network: Network, height: block::Height) -> bool;
```

`NetworkUpgrade::Canopy.activation_height(network)` is the correct way to read
the activation height; it is safe to unwrap for both networks. Note the
receiver difference: `activation_height` takes `&self`, while `current` and
`is_activation_height` are associated functions taking the network and height
as arguments. The driver uses `current` to assert the mandatory checkpoint
falls *inside* the Canopy epoch rather than at its boundary, and
`is_activation_height` to assert it is not itself an activation height.

`Height` is a public tuple struct — `pub struct Height(pub u32)` — so the
driver both constructs heights as `Height(n)` and reads them back as `.0`.
Both must keep working, and `Height` must keep deriving `Debug` and `PartialEq`
so `assert_eq!` can compare and print it.

## 7. Extended ChainTip trait — group `chain-tip-extended` (`zebra-chain`)

`zebra_chain::chain_tip::ChainTip` gains four methods (declared here in full,
including the `chrono` dependency that `zebra-chain` already carries):

```rust
use chrono::{DateTime, Utc};
use crate::{block, parameters::Network};

pub trait ChainTip {
    // --- pre-existing methods (unchanged) ---
    fn best_tip_height(&self) -> Option<block::Height>;
    fn best_tip_hash(&self) -> Option<block::Hash>;
    fn best_tip_mined_transaction_ids(&self) -> Arc<[transaction::Hash]>;

    // --- new at this milestone ---
    fn best_tip_height_and_hash(&self) -> Option<(block::Height, block::Hash)>;
    fn best_tip_block_time(&self) -> Option<DateTime<Utc>>;
    fn best_tip_height_and_block_time(&self) -> Option<(block::Height, DateTime<Utc>)>;

    /// Default method — implemented in terms of best_tip_height_and_block_time.
    fn estimate_network_chain_tip_height(
        &self,
        network: Network,
        now: DateTime<Utc>,
    ) -> Option<block::Height> { /* ... */ }
}
```

`NoChainTip` implements all four new methods, returning `None` for the three
`Option`-returning methods and `None` for `estimate_network_chain_tip_height`
(because `best_tip_height_and_block_time` returns `None`).

The trait must remain object-safe: all four new methods are callable through
a `&dyn ChainTip` reference.

### What `estimate_network_chain_tip_height` must actually do

`NoChainTip` is not enough to specify this method: every one of its answers is
`None`, so a trait that declares the four new methods with `{ None }` bodies
satisfies all of it. The default body is checked on a present tip as well, by a
driver-local `ChainTip` implementor:

- It reads `self.best_tip_height_and_block_time()`. If that is `None`, the
  estimate is `None`.
- Otherwise it projects forward from the tip's **block time** to the `now`
  argument using the network's target spacings, and returns the projected
  height. At `now == the tip's own block time` the projection has advanced by
  zero, so the estimate equals the tip height exactly.
- The estimate is monotonic in `now`: a later `now` must not yield a lower
  height. One day of mainnet wall-clock is at least 576 blocks (mainnet target
  spacing is at most 150 s), which the driver asserts as a floor rather than an
  exact count, so the precise spacing schedule stays free.
- It must remain a **provided (default) method** on the trait, not something
  each implementor writes: the driver's implementor deliberately does not
  override it, so a trait that only declares it fails to link.

`chrono` is reachable from a `zebra-chain` integration test — `zebra-chain`
already declares it in `[dependencies]` — so the driver names
`chrono::{DateTime, Duration, TimeZone, Utc}` directly. Do not remove that
entry.

`Arc<[transaction::Hash]>` is the return type of
`best_tip_mined_transaction_ids`, and the driver's implementor returns
`Arc::new([])` for it, so `std::sync::Arc` and `zebra_chain::transaction::Hash`
are both part of this group's surface.

The predecessor tree (app-13) did not declare `best_tip_height_and_hash`,
`best_tip_block_time`, or `best_tip_height_and_block_time` on the trait;
drivers calling those methods fail to compile against it.

## 8. Block codec regression guard — group `block-codec-regression` (GATE — adds no requirement, vetoes the step if it fails; `zebra-chain`)

A copy of step 1's block-codec driver, run against the same 17-block corpus
(`fixtures/blocks.tsv` + `fixtures/blocks/*.hex`, unchanged since step 1):
every block round-trips byte-for-byte; `Block::hash()`,
`Block::coinbase_height()`, `transactions.len()`, the Merkle root computed
from `Transaction::hash()`, and the re-serialized length all match the corpus;
and `MAX_BLOCK_BYTES = 2_000_000` is still enforced on deserialize.

Three rejection and boundary behaviours that sentence does not cover are also
asserted, and they gate the step just as the round-trip does: a block one byte
short must fail with `SerializationError::Io(_)` **specifically** — a
different error variant fails the group just as `Ok` does; a block header
followed by a nine-byte compactsize claiming `u64::MAX` transactions must be
rejected as an error rather than used as an allocation size; and
deserialization must consume exactly the block's own bytes and stop, so that
`Cursor::position()` lands on the block boundary and two blocks written back
to back both parse off a single reader.

The `fmt::Display` implementation for `Block` at this tag produces exactly:

```
Block { height: Height(<n>), transactions: <count>, hash: <hex> }
```

where `<n>` is the coinbase height, `<count>` is `transactions.len()`, and
`<hex>` is the lowercase block hash. The `transactions` field is present in this milestone's inherited tree; the
driver asserts it as a regression guard. The driver asserts this
exact string byte-for-byte for all 17 corpus blocks.

`Block::commitment()` (renamed from `Block::root_hash()` at an earlier milestone) is present in the inherited tree. The
driver calls this method; its full signature is:

```rust
pub fn commitment(&self, network: Network) -> Result<Commitment, CommitmentError>
```

The driver imports, in full:

```rust
use std::collections::BTreeMap;
use std::convert::TryInto;
use std::io::Cursor;
use std::path::PathBuf;

use zebra_chain::block::merkle;
use zebra_chain::block::{Block, Commitment};
use zebra_chain::transparent::Input as TInput;
use zebra_chain::parameters::Network;
use zebra_chain::serialization::{
    SerializationError, ZcashDeserialize, ZcashDeserializeInto, ZcashSerialize,
};
```

Five of those are `zebra_chain` paths, and every one of them must stay public
and reachable. This is a *regression* group: none of it is new at this tag, and
`build-package.sh` writes a fresh interface document for each step, so this list
is the only description of that surface you will see. Narrowing `zebra-chain`'s
public API to the names used by the new work — making `block::merkle` private,
dropping the `serialization` re-exports, or restricting `transparent::Input` —
fails `block-codec-regression` at compile time. That group is a gate group, so
it fails the whole step outright.

Beyond the imports, the driver depends on:

```rust
// zebra_chain::block
pub struct Block { pub header: Header, pub transactions: Vec<Arc<Transaction>>, /* ... */ }
pub struct Header { pub merkle_root: merkle::Root, /* ... */ }
pub struct Height(pub u32);      // constructed as `Height(n)` and read as `.0`

// zebra_chain::block::merkle
pub struct Root(pub [u8; 32]);
impl std::iter::FromIterator<zebra_chain::transaction::Hash> for Root;

// zebra_chain::transaction
impl Transaction { pub fn inputs(&self) -> &[zebra_chain::transparent::Input]; }

// zebra_chain::transparent
pub enum Input { PrevOut { .. }, Coinbase { height, data, sequence } }

// zebra_chain::serialization
pub enum SerializationError { Io(#[from] std::io::Error), /* ... */ }

// zebra_chain::sapling::tree
impl From<Root> for [u8; 32];    // the driver uses it as `Into<[u8; 32]>`
```

`hex` is reachable from this driver: `hex = "0.4.3"` appears in **both**
`[dependencies]` and `[dev-dependencies]` of `zebra-chain`, and an integration
test links against both sets. (The same is true of `chrono`, noted below for
`chain-tip-extended`.)

`Debug` and `PartialEq` must be derived on `Network`, `NetworkUpgrade`, `Height`
and `Commitment`: the drivers compare them with `assert_eq!`, which needs both.
They are derived in the reference tree, so this is a constraint to preserve, not
new work.

The four variants of `Commitment` matched in the driver are:

| Variant | Associated data | Era matched |
| --- | --- | --- |
| `Commitment::PreSaplingReserved([u8; 32])` | 32 reserved bytes | Pre-Sapling and Overwinter |
| `Commitment::FinalSaplingRoot(sapling::tree::Root)` | Sapling treestate root | Sapling and Blossom |
| `Commitment::ChainHistoryActivationReserved` | *(unit — no data)* | Heartwood activation block only |
| `Commitment::ChainHistoryRoot(ChainHistoryMmrRootHash)` | MMR root hash | Heartwood+ and Canopy |

This group exists because this milestone adds `zebra-rpc` and extends
`ChainTip`, neither of which changes the block codec — the regression guard
confirms that.

## 9. BIP-70 network name — group `bip70-network-name` (`zebra-chain`)

```rust
impl Network {
    pub fn bip70_network_name(&self) -> String {
        match self {
            Network::Mainnet => "main".to_string(),
            Network::Testnet => "test".to_string(),
        }
    }
}
```

Exact return values: `"main"` for `Mainnet`, `"test"` for `Testnet` (as
defined in
[BIP-70](https://github.com/bitcoin/bips/blob/master/bip-0070.mediawiki#paymentdetailspaymentrequest)
and matching zcashd's `getblockchaininfo` response). The method is absent from
the predecessor tree; drivers calling it fail to compile against app-13.

## 10. Negative space — what this milestone does not ask for

- **Inventory registry.** `InventoryChange` and `InventoryStatus` are new at
  this tag; `InventoryRegistry` is **not** — it already exists as a `pub
  struct` at the predecessor and is reworked here. All three are declared
  `pub`, but they live inside `zebra-network`'s private `peer_set` module
  (`zebra-network/src/lib.rs`: `mod peer_set;`), so none is reachable from an
  integration test. Declare them `pub` as upstream does; do not add a
  `pub use` that lifts them into `zebra-network`'s public API.

- **Shielded note commitment tree persistence.** The `zebra-state` crate
  undergoes a significant internal refactor at this tag (`zebra_db`,
  `disk_db`, `disk_format` sub-modules), adding Sapling and Orchard note
  commitment tree storage to the finalized state. That refactor is entirely
  crate-internal.

  `ReadRequest` and `ReadResponse`, however, are **new public types** at this
  tag — absent from the predecessor tree entirely, and re-exported from
  `zebra-state/src/lib.rs` alongside `Request` and `Response`. You must add
  them, because `RpcImpl::new` is generic over a
  `Service<ReadRequest, Response = ReadResponse>` (see §4). Their *variants*
  are not checked and no driver constructs one, but the two enums must exist
  and be publicly re-exported or `zebra-rpc` will not compile.

  No driver is compiled into `zebra-state`: `groups.txt` column 4 names only
  `zebra-chain`, `zebra-rpc` and `zebra-node-services`. One driver does
  *name* `zebra_state::{ReadRequest, ReadResponse, BoxError}`, because §4's
  state stub has to spell out the `Service` bound `RpcImpl` requires — that
  is the whole of the dependency. It never names a variant of either enum,
  and no behaviour of `zebra-state` is checked anywhere in this step.

- **`copy-state` subcommand and the `notfound` message handler.** `copy-state`
  is a new `zebrad` CLI subcommand at this tag
  (`zebrad/src/commands/copy_state.rs`), reachable only by running the binary,
  not from a library integration test. `Message::NotFound` is *not* new here --
  it already exists at the predecessor
  (`zebra-network/src/protocol/external/message.rs`); what this tag changes is
  how the peer set responds to it. Either way `zebra-network/src/lib.rs:148`
  declares `mod protocol;` privately, so `Message` is unreachable from outside
  the crate and no integration test can assert on it.

- **A running JSON-RPC server.** `server::init` binds a socket and starts a
  `jsonrpc-http-server`; nothing in scope calls it, no driver opens a port, and
  no driver speaks HTTP or frames a JSON-RPC envelope. §4 constructs
  `RpcImpl` in-process with stub services and calls its methods directly, so
  the three synchronous methods are checked behaviourally and the transport is
  not checked at all. The four `BoxFuture` methods are checked only as
  signatures (§3): no driver drives one to completion, so no test
  needs a Tokio runtime and `RpcImpl` need not be usable from an async
  context to pass.

- **Proof and signature verification, script interpreter, mining/wallet
  internals, lightwalletd, Tor, NU6** — unchanged from earlier steps.

## 11. Handoff

**Inherited from steps 1–13**: V1–V5 transaction codec, Sapling/Orchard
shielded data, history tree, transparent output indexing, nullifier indexing,
RedPallas verification, the 17-block corpus — all unchanged.

**What step 14 leaves**: a `zebra-rpc` crate serving seven methods that
`lightwalletd` depends on; `ChainTip` extended with block-time and
height-and-hash accessors; `mandatory_checkpoint_height` that accounts for
the ZIP-212 grace period; and `bip70_network_name` for the network enum.
These items are the final capability milestone in this fourteen-step ladder.
