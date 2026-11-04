//! `RpcImpl` behaviour — the JSON-RPC server's synchronous methods.
//!
//! Harbor verifier driver -- injected into `zebra-rpc/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! This group exists because `rpc-response-types` scores only the *shapes* of
//! the response structs. A `methods.rs` holding six serde structs and a bare
//! seven-method `pub trait Rpc` declaration satisfies all of it: nothing there
//! needs `RpcImpl` to exist, let alone to implement the trait. This driver
//! reaches the implementation.
//!
//! How it constructs one without a node, and without a Tokio runtime:
//!
//!   * `RpcImpl::new` takes a concrete `Buffer<Mempool, mempool::Request>`.
//!     `Buffer::new` spawns its worker with `tokio::spawn` and so needs a
//!     runtime, but `Buffer::pair` does not spawn -- it hands the worker back
//!     instead. The three methods exercised here never route a request to the
//!     mempool, so the worker is dropped and the buffer is only ever a
//!     placeholder that makes the types line up.
//!   * `State` and `Tip` are ordinary generic parameters, so a driver-local
//!     `tower::Service` stub and a driver-local `ChainTip` implementor are
//!     enough.
//!
//! `get_info`, `get_blockchain_info` and `get_best_block_hash` all return
//! `jsonrpc_core::Result<T>` synchronously; the other four return `BoxFuture`
//! and are left to the compile-time signature gate in `rpc-response-types`.
//!
//! Negative direction: `zebra-rpc` at the predecessor tag (app-13) is an empty
//! stub with no dependencies and no modules, so `zebra_rpc::methods::RpcImpl`
//! does not resolve there and every test in this file fails to compile.

use std::future::{self, Ready};
use std::sync::Arc;
use std::task::{Context, Poll};

use chrono::{DateTime, TimeZone, Utc};
use hex::ToHex;
use tower::{buffer::Buffer, Service};

use zebra_chain::{
    block::{self, Height},
    chain_tip::{ChainTip, NoChainTip},
    parameters::{Network, NetworkUpgrade},
    transaction,
};
use zebra_node_services::{mempool, BoxError};
use zebra_rpc::methods::{Rpc, RpcImpl};

// ── driver-local Tower service stubs ─────────────────────────────────────────
//
// Neither is ever called: of the seven methods, only the three synchronous ones
// are exercised here, and none of them touches the mempool or the state. They
// exist to satisfy `RpcImpl`'s generic bounds, which is itself part of what is
// being asserted -- the bounds in `RpcImpl::new` and in the `Rpc` impl must
// stay as documented or this file does not compile.

/// A mempool service that satisfies the bound and answers nothing.
#[derive(Clone, Debug)]
struct StubMempool;

impl Service<mempool::Request> for StubMempool {
    type Response = mempool::Response;
    type Error = BoxError;
    type Future = Ready<Result<mempool::Response, BoxError>>;

    fn poll_ready(&mut self, _cx: &mut Context<'_>) -> Poll<Result<(), Self::Error>> {
        Poll::Ready(Ok(()))
    }

    fn call(&mut self, _request: mempool::Request) -> Self::Future {
        future::ready(Err("this driver never routes a request to the mempool".into()))
    }
}

/// A state service that satisfies the bound and answers nothing.
///
/// Deliberately does not name a `ReadResponse` variant: the enums must exist
/// and be publicly re-exported, but their variants are out of scope.
#[derive(Clone, Debug)]
struct StubState;

impl Service<zebra_state::ReadRequest> for StubState {
    type Response = zebra_state::ReadResponse;
    type Error = zebra_state::BoxError;
    type Future = Ready<Result<zebra_state::ReadResponse, zebra_state::BoxError>>;

    fn poll_ready(&mut self, _cx: &mut Context<'_>) -> Poll<Result<(), Self::Error>> {
        Poll::Ready(Ok(()))
    }

    fn call(&mut self, _request: zebra_state::ReadRequest) -> Self::Future {
        future::ready(Err("this driver never routes a request to the state".into()))
    }
}

// ── driver-local chain tip ───────────────────────────────────────────────────

/// A chain tip that is always present, at a fixed height, hash and block time.
struct FixedTip {
    height: Height,
    hash: block::Hash,
    block_time: DateTime<Utc>,
}

impl ChainTip for FixedTip {
    fn best_tip_height(&self) -> Option<Height> {
        Some(self.height)
    }
    fn best_tip_hash(&self) -> Option<block::Hash> {
        Some(self.hash)
    }
    fn best_tip_height_and_hash(&self) -> Option<(Height, block::Hash)> {
        Some((self.height, self.hash))
    }
    fn best_tip_block_time(&self) -> Option<DateTime<Utc>> {
        Some(self.block_time)
    }
    fn best_tip_height_and_block_time(&self) -> Option<(Height, DateTime<Utc>)> {
        Some((self.height, self.block_time))
    }
    fn best_tip_mined_transaction_ids(&self) -> Arc<[transaction::Hash]> {
        Arc::new([])
    }
    // `estimate_network_chain_tip_height` is deliberately not overridden: the
    // `estimatedheight` field below therefore comes from the trait's own
    // default body.
}

// ── helpers ──────────────────────────────────────────────────────────────────

/// The `app_version` handed to `RpcImpl::new`; it must reach `GetInfo::build`.
const APP_VERSION: &str = "step-14-verifier";

/// Mainnet Canopy activation height, from the public activation list.
const MAINNET_CANOPY: u32 = 1_046_400;

/// An arbitrary but fixed 32-byte tip hash.
const TIP_HASH_BYTES: [u8; 32] = [
    0x00, 0x04, 0x0f, 0xe8, 0xec, 0x84, 0x71, 0x91, 0x1b, 0xaa, 0x1d, 0xb1, 0x26, 0x6e, 0xa1, 0x5d,
    0xd0, 0x6b, 0x4a, 0x8a, 0x5c, 0x45, 0x38, 0x83, 0xc0, 0x00, 0xb0, 0x31, 0x97, 0x3d, 0xce, 0x08,
];

/// A block time shortly after mainnet Canopy activation.
fn tip_block_time() -> DateTime<Utc> {
    Utc.timestamp(1_605_000_000, 0)
}

/// Build an `RpcImpl` over driver-local stubs, with a tip at `height`.
///
/// `Buffer::pair` is used rather than `Buffer::new` because `new` calls
/// `tokio::spawn` and would need a runtime. The worker is dropped straight
/// away; nothing here ever sends on the buffer.
fn rpc_at(network: Network, height: u32) -> RpcImpl<StubMempool, StubState, FixedTip> {
    let (mempool, _worker) = Buffer::pair(StubMempool, 1);

    RpcImpl::new(
        APP_VERSION,
        mempool,
        StubState,
        FixedTip {
            height: Height(height),
            hash: block::Hash(TIP_HASH_BYTES),
            block_time: tip_block_time(),
        },
        network,
    )
}

/// Build an `RpcImpl` whose chain tip is empty.
fn rpc_without_tip(network: Network) -> RpcImpl<StubMempool, StubState, NoChainTip> {
    let (mempool, _worker) = Buffer::pair(StubMempool, 1);

    RpcImpl::new(APP_VERSION, mempool, StubState, NoChainTip, network)
}

/// Re-read a response through its own `Serialize` impl, so every assertion
/// below is made against the wire form `lightwalletd` sees.
fn wire<T: serde::Serialize>(response: &T) -> serde_json::Value {
    serde_json::from_str(&serde_json::to_string(response).expect("response must serialize"))
        .expect("a serialized response must be valid JSON")
}

/// Canopy's consensus branch id, in the hex form `zebra-chain` produces.
fn canopy_branch_id_hex() -> String {
    NetworkUpgrade::Canopy
        .branch_id()
        .expect("Canopy must have a consensus branch id")
        .encode_hex()
}

// ── the trait is actually implemented ────────────────────────────────────────

#[test]
fn rpc_impl_implements_the_rpc_trait() {
    // `rpc-response-types` pins the seven signatures inside a generic function
    // that is never instantiated, so a bare `pub trait Rpc { .. }` with no
    // implementor at all satisfies it. Instantiating the bound at a concrete
    // type is what forces `RpcImpl` to exist and to implement the trait.
    fn assert_implements_rpc<T: Rpc>() {}
    assert_implements_rpc::<RpcImpl<StubMempool, StubState, FixedTip>>();
    assert_implements_rpc::<RpcImpl<StubMempool, StubState, NoChainTip>>();

    // And it is constructable through the documented `new`.
    let rpc = rpc_at(Network::Mainnet, MAINNET_CANOPY);
    assert!(
        rpc.get_info().is_ok(),
        "RpcImpl::get_info must succeed on a freshly constructed server"
    );
}

// ── getinfo ──────────────────────────────────────────────────────────────────

#[test]
fn get_info_reports_the_app_version_and_the_user_agent() {
    // `subversion` is not an input here: it comes from the implementation,
    // which must source it from `zebra_network::constants::USER_AGENT`. A
    // driver that round-trips a `GetInfo` built from its own literal cannot
    // tell a bumped USER_AGENT from an inherited one.
    assert_eq!(
        zebra_network::constants::USER_AGENT,
        "/Zebra:1.0.0-beta.6/",
        "USER_AGENT must be bumped to this milestone's version"
    );

    let rpc = rpc_at(Network::Mainnet, MAINNET_CANOPY);
    let info = wire(&rpc.get_info().expect("get_info must succeed"));

    assert_eq!(
        info["build"], APP_VERSION,
        "GetInfo::build must carry the app_version passed to RpcImpl::new"
    );
    assert_eq!(
        info["subversion"], "/Zebra:1.0.0-beta.6/",
        "GetInfo::subversion must be the USER_AGENT constant, not a literal"
    );
}

// ── getblockchaininfo ────────────────────────────────────────────────────────

#[test]
fn get_blockchain_info_mainnet_reports_the_tip() {
    let rpc = rpc_at(Network::Mainnet, MAINNET_CANOPY);
    let info = wire(
        &rpc.get_blockchain_info()
            .expect("get_blockchain_info must succeed when a tip is present"),
    );

    assert_eq!(
        info["chain"], "main",
        "the chain string must come from Network::bip70_network_name()"
    );
    assert_eq!(
        info["blocks"], MAINNET_CANOPY,
        "blocks must be the chain tip height read from the ChainTip"
    );
    assert_eq!(
        info["bestblockhash"],
        block::Hash(TIP_HASH_BYTES).encode_hex::<String>(),
        "bestblockhash must be the chain tip hash read from the ChainTip"
    );

    // `estimatedheight` comes from the ChainTip trait's default
    // `estimate_network_chain_tip_height`, projected from the tip's block time
    // to the current wall clock. The exact value moves with the clock, so
    // assert the relation the field must always satisfy rather than a number.
    let estimated = info["estimatedheight"]
        .as_u64()
        .expect("estimatedheight must be a JSON number");
    assert!(
        estimated >= u64::from(MAINNET_CANOPY),
        "estimatedheight ({}) must never be below the tip height ({})",
        estimated,
        MAINNET_CANOPY
    );
}

#[test]
fn get_blockchain_info_testnet_chain_string_is_test() {
    let rpc = rpc_at(Network::Testnet, 1_028_500);
    let info = wire(
        &rpc.get_blockchain_info()
            .expect("get_blockchain_info must succeed when a tip is present"),
    );

    assert_eq!(
        info["chain"], "test",
        "the Testnet chain string is \"test\""
    );
    assert_ne!(
        info["chain"], "main",
        "Testnet must not report the Mainnet chain string"
    );
}

#[test]
fn get_blockchain_info_upgrades_are_keyed_by_the_consensus_branch_id() {
    // The whole `upgrades` map is built by the implementation from
    // `NetworkUpgrade::activation_list`, so nothing in it is a driver input:
    // the key, the name, the activation height and the status all have to be
    // derived. The key in particular pins the byte order of the
    // `ConsensusBranchId` hex encoding against a literal -- a ToHex/FromHex
    // pair that consistently reverses the four bytes round-trips cleanly but
    // emits wire output lightwalletd rejects, and cannot reach this key.
    let canopy_hex = canopy_branch_id_hex();
    assert_eq!(
        canopy_hex, "e9ff75a6",
        "Canopy's consensus branch id is 0xe9ff75a6, hex-encoded in display order"
    );

    let active = wire(
        &rpc_at(Network::Mainnet, MAINNET_CANOPY)
            .get_blockchain_info()
            .expect("get_blockchain_info must succeed when a tip is present"),
    );
    let canopy = active
        .get("upgrades")
        .and_then(|upgrades| upgrades.get(&canopy_hex))
        .unwrap_or_else(|| panic!("upgrades must be keyed by the hex branch id {}", canopy_hex));

    assert_eq!(
        canopy["name"], "Canopy",
        "the upgrade name is the NetworkUpgrade variant name"
    );
    assert_eq!(
        canopy["activationheight"], MAINNET_CANOPY,
        "upgrade entries must use the zcashd key 'activationheight' and the real activation height"
    );
    assert_eq!(
        canopy["status"], "active",
        "a tip at the activation height makes that upgrade active"
    );

    // The consensus object carries the same branch id at this tip.
    assert_eq!(
        active["consensus"]["chaintip"], canopy_hex,
        "consensus.chaintip is the branch id in force at the tip height"
    );

    // One block below activation the same entry must be pending, so a
    // hard-wired "active" does not pass.
    let pending = wire(
        &rpc_at(Network::Mainnet, MAINNET_CANOPY - 1)
            .get_blockchain_info()
            .expect("get_blockchain_info must succeed when a tip is present"),
    );
    assert_eq!(
        pending["upgrades"][&canopy_hex]["status"], "pending",
        "an upgrade whose activation height is above the tip is pending"
    );
}

// ── getbestblockhash ─────────────────────────────────────────────────────────

#[test]
fn get_best_block_hash_returns_the_chain_tip_hash() {
    let rpc = rpc_at(Network::Mainnet, MAINNET_CANOPY);
    let best = wire(
        &rpc.get_best_block_hash()
            .expect("get_best_block_hash must succeed when a tip is present"),
    );

    assert_eq!(
        best,
        block::Hash(TIP_HASH_BYTES).encode_hex::<String>(),
        "getbestblockhash must serialize the chain tip hash as a bare hex string"
    );

    // And it must agree with the same hash reported by getblockchaininfo.
    let info = wire(&rpc.get_blockchain_info().expect("get_blockchain_info must succeed"));
    assert_eq!(
        best, info["bestblockhash"],
        "getbestblockhash and getblockchaininfo must report the same tip hash"
    );
}

// ── the rejection direction ──────────────────────────────────────────────────

#[test]
fn tip_dependent_methods_fail_when_there_is_no_chain_tip() {
    // Every other assertion in this file is a success case. `NoChainTip`
    // answers `None` to everything, and the two methods that read the tip must
    // report a failure rather than inventing a height or a hash. Only the
    // direction is asserted -- not the error code and not its message.
    let rpc = rpc_without_tip(Network::Mainnet);

    assert!(
        rpc.get_blockchain_info().is_err(),
        "get_blockchain_info must fail when the chain tip is empty"
    );
    assert!(
        rpc.get_best_block_hash().is_err(),
        "get_best_block_hash must fail when the chain tip is empty"
    );

    // getinfo reads neither the tip nor a service, so it still succeeds.
    assert!(
        rpc.get_info().is_ok(),
        "get_info does not read the chain tip and must still succeed"
    );
}
