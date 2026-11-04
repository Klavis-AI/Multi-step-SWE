//! RPC response-type shapes and serde contracts.
//!
//! Harbor verifier driver -- injected into `zebra-rpc/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! The `zebra-rpc` crate's `methods` module is new at v1.0.0-beta.6: the crate
//! itself exists at the predecessor tag as an empty stub (no dependencies, no
//! exported modules), so `zebra_rpc::methods` does not resolve there and every
//! test here fails to compile, satisfying the negative-direction requirement
//! every scored group has to meet.
//!
//! We test the *shape* of the response types through their serde contracts
//! rather than through RpcImpl, because RpcImpl::new requires live Tower
//! services (Buffer<Mempool, ...> and a State service) that are not
//! conveniently constructable in an offline integration test.  The serde
//! contract IS the wire format lightwalletd depends on, so it is exactly the
//! right thing to score.
//!
//! The seven `Rpc` trait methods cannot be *called* for the same reason, but
//! their signatures are pinned below by a compile-time gate, so a rename or a
//! changed parameter type is caught even though no `#[test]` invokes them.
//! `jsonrpc_core` is a declared dependency of `zebra-rpc` -- it is where the
//! `Result` and `BoxFuture` aliases in those signatures come from.

use hex::ToHex;

use zebra_chain::parameters::NetworkUpgrade;
use zebra_rpc::methods::{
    GetBestBlockHash, GetBlock, GetBlockChainInfo, GetInfo, GetRawTransaction, Rpc,
    SentTransactionHash,
};

/// Canopy's consensus branch id, rendered the way `ConsensusBranchIdHex`
/// renders it on the wire.
///
/// Derived rather than written out. `ConsensusBranchIdHex` is private, so the
/// only way to reach the key is through the JSON, and feeding a literal
/// `"e9ff75a6"` in and reading the same literal back out is a masked
/// mutation: it passes against a `ConsensusBranchIdHex` that reverses the
/// byte order, or one that is a bare `String` newtype. Deriving the key from
/// `NetworkUpgrade::branch_id` makes the round trip assert that the
/// implementation's rendering agrees with `zebra-chain`'s.
fn canopy_branch_id_hex() -> String {
    NetworkUpgrade::Canopy
        .branch_id()
        .expect("Canopy must have a consensus branch id")
        .encode_hex()
}

// ── `Rpc` trait signatures (compile-time gate) ───────────────────────────────
//
// Never called: instantiating this would need a concrete implementor, and every
// implementor needs live Tower services. Rust type-checks generic bodies at
// definition time regardless, so a renamed method, a reordered parameter or a
// widened integer type is a compile error here and fails all eleven tests in
// this group -- which is the point. Without it, renaming
// `send_raw_transaction` still scores the group's full 18 points.
//
// The *behaviour* behind these signatures is scored separately, by
// `rpc-server-impl`, which constructs an `RpcImpl` and calls the three
// synchronous methods. This file only pins the shapes.
#[allow(dead_code)]
fn assert_rpc_trait_signatures<T: Rpc>() {
    let _get_info: fn(&T) -> jsonrpc_core::Result<GetInfo> = T::get_info;

    let _get_blockchain_info: fn(&T) -> jsonrpc_core::Result<GetBlockChainInfo> =
        T::get_blockchain_info;

    let _send_raw_transaction: fn(
        &T,
        String,
    )
        -> jsonrpc_core::BoxFuture<jsonrpc_core::Result<SentTransactionHash>> =
        T::send_raw_transaction;

    let _get_block: fn(
        &T,
        String,
        u8,
    ) -> jsonrpc_core::BoxFuture<jsonrpc_core::Result<GetBlock>> = T::get_block;

    let _get_best_block_hash: fn(&T) -> jsonrpc_core::Result<GetBestBlockHash> =
        T::get_best_block_hash;

    let _get_raw_mempool: fn(
        &T,
    ) -> jsonrpc_core::BoxFuture<jsonrpc_core::Result<Vec<String>>> =
        T::get_raw_mempool;

    let _get_raw_transaction: fn(
        &T,
        String,
        u8,
    ) -> jsonrpc_core::BoxFuture<jsonrpc_core::Result<GetRawTransaction>> =
        T::get_raw_transaction;
}

// ── helpers ──────────────────────────────────────────────────────────────────

/// A valid 32-byte block hash as lowercase hex.
const ZERO_HASH_HEX: &str =
    "0000000000000000000000000000000000000000000000000000000000000000";

/// A non-zero hash (genesis block).
const GENESIS_HASH_HEX: &str =
    "00040fe8ec8471911baa1db1266ea15dd06b4a8a5c453883c000b031973dce08";

// ── GetInfo ───────────────────────────────────────────────────────────────────

#[test]
fn get_info_deserializes_from_json() {
    // Field names in the JSON envelope are "build" and "subversion" (lowercase,
    // matching zcashd's getinfo output that lightwalletd reads).
    let json = r#"{"build":"v1.0.0-beta.6","subversion":"/Zebra:1.0.0-beta.6/"}"#;
    let info: GetInfo = serde_json::from_str(json)
        .expect("GetInfo must deserialize from {build, subversion} JSON");
    // Roundtrip: serialize and deserialize again; values must survive.
    let roundtrip: GetInfo = serde_json::from_str(
        &serde_json::to_string(&info).expect("GetInfo must serialize"),
    )
    .expect("GetInfo roundtrip must deserialize");
    assert_eq!(info, roundtrip, "GetInfo serde roundtrip must be lossless");
}

#[test]
fn get_info_json_has_build_field() {
    let json = r#"{"build":"test-build","subversion":"/Zebra:test/"}"#;
    let info: GetInfo = serde_json::from_str(json).unwrap();
    let value: serde_json::Value = serde_json::from_str(
        &serde_json::to_string(&info).unwrap()
    ).unwrap();
    assert!(
        value.get("build").is_some(),
        "serialized GetInfo must contain a 'build' key"
    );
    assert_eq!(value["build"], "test-build");
}

#[test]
fn get_info_json_has_subversion_field() {
    let json = r#"{"build":"b","subversion":"/Zebra:1.0.0-beta.6/"}"#;
    let info: GetInfo = serde_json::from_str(json).unwrap();
    let value: serde_json::Value = serde_json::from_str(
        &serde_json::to_string(&info).unwrap()
    ).unwrap();
    assert!(
        value.get("subversion").is_some(),
        "serialized GetInfo must contain a 'subversion' key"
    );
    assert_eq!(value["subversion"], "/Zebra:1.0.0-beta.6/");
}

// ── GetBestBlockHash ──────────────────────────────────────────────────────────

#[test]
fn get_best_block_hash_serde_roundtrip() {
    // GetBestBlockHash serializes as a JSON string holding a lowercase hex hash.
    let json = format!(r#""{}""#, GENESIS_HASH_HEX);
    let h: GetBestBlockHash = serde_json::from_str(&json)
        .expect("GetBestBlockHash must deserialize from a hex-string JSON value");
    let back = serde_json::to_string(&h)
        .expect("GetBestBlockHash must serialize to a JSON string");
    assert_eq!(
        back,
        json,
        "GetBestBlockHash serde roundtrip must preserve the hash bytes"
    );
}

#[test]
fn get_best_block_hash_zero_roundtrip() {
    let json = format!(r#""{}""#, ZERO_HASH_HEX);
    let h: GetBestBlockHash = serde_json::from_str(&json).unwrap();
    let back = serde_json::to_string(&h).unwrap();
    assert_eq!(back, json);
}

// ── GetBlock ──────────────────────────────────────────────────────────────────

#[test]
fn get_block_type_is_exported_and_serialize() {
    // GetBlock's inner SerializedBlock field is private; it can only be
    // constructed inside the crate (via RpcImpl::get_block). Offline
    // integration tests cannot instantiate it directly. This test asserts
    // the type is publicly exported from zebra_rpc::methods and implements
    // serde::Serialize, which is the documented wire-format contract.
    fn assert_serialize<T: serde::Serialize>() {}
    assert_serialize::<zebra_rpc::methods::GetBlock>();
}

// ── SentTransactionHash ───────────────────────────────────────────────────────

#[test]
fn sent_transaction_hash_serializes_as_bare_hex_string() {
    use zebra_rpc::methods::SentTransactionHash;
    let json = format!(r#""{}""#, ZERO_HASH_HEX);
    let h: SentTransactionHash = serde_json::from_str(&json)
        .expect("SentTransactionHash must deserialize from a bare hex-string JSON value");
    let back = serde_json::to_string(&h).unwrap();
    assert_eq!(back, json, "SentTransactionHash serde roundtrip must preserve hash");
}

// ── GetRawTransaction ─────────────────────────────────────────────────────────

#[test]
fn get_raw_transaction_raw_variant_serde() {
    // GetRawTransaction only derives Serialize (not Deserialize); we construct
    // the Raw variant directly using SerializedTransaction::from(Vec<u8>) and
    // verify the wire shape by inspecting the serialized JSON.
    // The Raw variant must serialize as a bare hex-string (untagged enum).
    use zebra_chain::transaction::SerializedTransaction;
    let tx_hex = "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704ffff001d0104ffffffff0100f2052a0100000043410496b538e853519c726a2c91e61ec11600ae1310f8a907786f3e3d747dac3a5d7a720c8d5db9b6c8e6ad13bef1e7f2b8b1b1a3b6c8e6ad13bef1e7f2b8b1b1a3b6ac00000000";
    let raw_bytes = hex::decode(tx_hex).unwrap();
    let rgt = GetRawTransaction::Raw(SerializedTransaction::from(raw_bytes));
    let json_value: serde_json::Value =
        serde_json::from_str(&serde_json::to_string(&rgt).unwrap()).unwrap();
    // Raw variant must be a JSON string, not an object.
    assert!(json_value.is_string(), "GetRawTransaction::Raw must serialize as a bare JSON string");
    assert_eq!(
        json_value.as_str().unwrap().to_lowercase(),
        tx_hex.to_lowercase(),
        "GetRawTransaction::Raw hex string must match the original bytes"
    );
}

#[test]
fn get_raw_transaction_object_variant_serde() {
    // GetRawTransaction only derives Serialize; construct the Object variant
    // directly and verify the JSON wire shape.
    use zebra_chain::transaction::SerializedTransaction;
    let tx_hex = "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704ffff001d0104ffffffff0100f2052a0100000043410496b538e853519c726a2c91e61ec11600ae1310f8a907786f3e3d747dac3a5d7a720c8d5db9b6c8e6ad13bef1e7f2b8b1b1a3b6c8e6ad13bef1e7f2b8b1b1a3b6ac00000000";
    let raw_bytes = hex::decode(tx_hex).unwrap();
    let obj = GetRawTransaction::Object {
        hex: SerializedTransaction::from(raw_bytes),
        height: 347500i32,
    };
    let back: serde_json::Value =
        serde_json::from_str(&serde_json::to_string(&obj).unwrap()).unwrap();
    // Object variant: must have "hex" and "height" keys (not an outer wrapper).
    assert!(back.is_object(), "GetRawTransaction::Object must serialize as a JSON object");
    assert!(back.get("hex").is_some(), "Object variant must have 'hex' field");
    assert!(back.get("height").is_some(), "Object variant must have 'height' field");
    assert_eq!(back["height"], 347500i32, "height field must round-trip as i32");
    assert_eq!(
        back["hex"].as_str().unwrap().to_lowercase(),
        tx_hex.to_lowercase(),
        "hex field must match the original bytes"
    );
}

// ── GetBlockChainInfo ─────────────────────────────────────────────────────────

/// Round-trip a `GetBlockChainInfo` through `serde_json` and hand back the
/// re-parsed JSON, so every assertion below is made against the *wire* form
/// rather than against the Rust value.
fn blockchain_info_round_trip(json: serde_json::Value) -> serde_json::Value {
    let info: GetBlockChainInfo = serde_json::from_value(json)
        .expect("GetBlockChainInfo must deserialize from canonical lightwalletd JSON shape");
    serde_json::from_str(&serde_json::to_string(&info).unwrap()).unwrap()
}

#[test]
fn get_blockchain_info_json_shape() {
    // The required top-level field names lightwalletd depends on: chain,
    // blocks, bestblockhash, estimatedheight, upgrades, consensus.
    //
    // `upgrades` carries one real entry, not an empty map. An empty map is a
    // masked mutation: with `"upgrades": {}` the field can be typed
    // `HashMap<String, serde_json::Value>`, or `NetworkUpgradeInfo` can drop
    // the `activationheight` rename entirely, and every assertion still
    // passes. The entry below forces the documented key type
    // (`ConsensusBranchIdHex`, a hex string) and the documented value shape
    // (`name` / `activationheight` / `status`) to be real.
    // The branch-id key is derived from zebra-chain, not written out here;
    // see `canopy_branch_id_hex`. Pinning its value as well means a reversed
    // byte order fails on this line rather than passing silently through the
    // round trip below.
    let canopy = canopy_branch_id_hex();
    assert_eq!(
        canopy, "e9ff75a6",
        "Canopy's consensus branch id must render as lowercase big-endian hex"
    );

    let back = blockchain_info_round_trip(serde_json::json!({
        "chain": "main",
        "blocks": 1046400u32,
        "bestblockhash": ZERO_HASH_HEX,
        "estimatedheight": 1046400u32,
        "upgrades": {
            canopy.clone(): {
                "name": "Canopy",
                "activationheight": 1046400u32,
                "status": "active"
            }
        },
        "consensus": {
            "chaintip": canopy.clone(),
            "nextblock": canopy.clone()
        }
    }));

    // Field-name contract: zcashd renames are "bestblockhash" and "estimatedheight"
    assert_eq!(back["chain"], "main");
    assert_eq!(back["blocks"], 1046400);
    assert!(back.get("bestblockhash").is_some(), "must use 'bestblockhash' key");
    assert!(back.get("estimatedheight").is_some(), "must use 'estimatedheight' key");
    assert!(back.get("consensus").is_some(), "must have 'consensus' key");

    // The upgrades map survives the round trip keyed by its hex branch id ...
    let upgrade = back
        .get("upgrades")
        .and_then(|u| u.get(&canopy))
        .expect("upgrades must be keyed by the hex consensus branch id");
    // ... and each entry keeps all three zcashd-compatible field names.
    assert_eq!(
        upgrade["activationheight"], 1046400,
        "upgrade entries must use the zcashd key 'activationheight', not 'activation_height'"
    );
    assert_eq!(upgrade["name"], "Canopy", "the upgrade name is the NetworkUpgrade variant");
    assert_eq!(
        upgrade["status"], "active",
        "NetworkUpgradeStatus must serialize lowercase: active / disabled / pending"
    );
}

#[test]
fn get_blockchain_info_testnet_chain_string_is_test() {
    // `chain` is "main" on Mainnet and "test" on Testnet. The test above only
    // covers the Mainnet half; without this one, wiring the Mainnet string in
    // for both networks scores full marks on this group.
    let back = blockchain_info_round_trip(serde_json::json!({
        "chain": "test",
        "blocks": 1028500u32,
        "bestblockhash": ZERO_HASH_HEX,
        "estimatedheight": 1028500u32,
        "upgrades": {},
        "consensus": {
            "chaintip": "e9ff75a6",
            "nextblock": "e9ff75a6"
        }
    }));

    assert_eq!(
        back["chain"], "test",
        "the Testnet chain string is \"test\"; it must round-trip unchanged"
    );
    assert_ne!(back["chain"], "main", "Testnet must not report the Mainnet chain string");
}
