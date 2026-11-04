//! ZIP-401 transaction cost and eviction weight on `VerifiedUnminedTx`.
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! ZIP-401 defines two quantities used to govern weighted random eviction from
//! the mempool when it is full:
//!
//!   cost(tx)           = max(serialized_size_bytes, 4000)
//!   eviction_weight(tx)= cost(tx) + low_fee_penalty
//!                        where low_fee_penalty = 16000 if fee < 1000 zatoshis
//!                                                     else 0
//!
//! Both methods live on `VerifiedUnminedTx` in `zebra_chain::transaction`.
//! `cost` takes `&self`; `eviction_weight` consumes `self`.
//!
//! NOTE: The shipped spec file `zip-0401.rst` states cost_threshold = 10000
//! and low_fee_penalty = 40000. The code at v1.0.0-beta.0 implements 4000 and
//! 16000 respectively (with a 1000-zatoshi conventional fee). The CODE is
//! normative for this step; the spec was updated in a later release.

use std::convert::TryFrom;
use std::sync::Arc;

use zebra_chain::amount::{Amount, NonNegative};
use zebra_chain::parameters::NetworkUpgrade;
use zebra_chain::serialization::ZcashSerialize;
use zebra_chain::transaction::{LockTime, Transaction, UnminedTx, VerifiedUnminedTx};
use zebra_chain::block;

// ----------------------------------------------------------------- helpers

/// Build a minimal v4 transaction with no inputs, no outputs, no shielded data.
/// This is structurally valid; the serialized size will be small and well-defined.
fn minimal_v4_tx() -> Transaction {
    Transaction::V4 {
        inputs: vec![],
        outputs: vec![],
        lock_time: LockTime::min_lock_time(),
        expiry_height: block::Height(0),
        joinsplit_data: None,
        sapling_shielded_data: None,
    }
}

/// Build a minimal v5 transaction.
fn minimal_v5_tx() -> Transaction {
    Transaction::V5 {
        network_upgrade: NetworkUpgrade::Nu5,
        lock_time: LockTime::min_lock_time(),
        expiry_height: block::Height(0),
        inputs: vec![],
        outputs: vec![],
        sapling_shielded_data: None,
        orchard_shielded_data: None,
    }
}

/// Compute the serialized size of a transaction independently of the interface.
fn serialized_size(tx: &Transaction) -> u64 {
    let mut buf = Vec::new();
    tx.zcash_serialize(&mut buf)
        .expect("in-memory serialization never fails");
    buf.len() as u64
}

/// Construct a `VerifiedUnminedTx` from a transaction and a fee in zatoshis.
fn verified(tx: Transaction, fee_zatoshis: u64) -> VerifiedUnminedTx {
    let fee = Amount::<NonNegative>::try_from(fee_zatoshis)
        .expect("fee fits in Amount<NonNegative>");
    let unmined: UnminedTx = tx.into();
    VerifiedUnminedTx::new(unmined, fee)
}

// The cost threshold constant as used in this step's code.
const COST_THRESHOLD: u64 = 4000;
// The conventional fee (zatoshis) below which the low-fee penalty applies.
const CONVENTIONAL_FEE: u64 = 1000;
// The low-fee penalty added when fee < conventional_fee.
const LOW_FEE_PENALTY: u64 = 16_000;

// ------------------------------------------------------------------- tests

#[test]
fn cost_of_small_transaction_is_the_threshold() {
    // A minimal v4 transaction serializes to well under 4000 bytes.
    // cost() must return max(serialized_size, 4000) = 4000.
    let tx = minimal_v4_tx();
    let sz = serialized_size(&tx);
    assert!(
        sz < COST_THRESHOLD,
        "test assumption failed: minimal v4 tx serialized to {} bytes, expected < {}",
        sz,
        COST_THRESHOLD
    );
    let vtx = verified(tx, 5000);
    assert_eq!(
        vtx.cost(),
        COST_THRESHOLD,
        "cost of a small tx must equal the 4000-byte threshold, not its actual {} bytes",
        sz
    );
}

#[test]
fn cost_of_large_transaction_is_its_size() {
    // Build a transaction whose serialized size exceeds the threshold by
    // padding with many outputs. Each output serializes as 8 (value) + 1 (script
    // compactSize) + script_len bytes. With a 50-byte script body that is 59
    // bytes per output. 150 outputs is ~8.6 kB, safely above 4000; the case
    // asserts the measured size rather than a pinned total.
    use zebra_chain::transparent;
    use zebra_chain::amount::Amount;

    // 50-byte dummy script so each output is at least 58 bytes.
    let script_body = [0x51u8; 50];
    let mut outputs = Vec::new();
    for _ in 0..150 {
        outputs.push(transparent::Output {
            value: Amount::zero(),
            lock_script: transparent::Script::new(&script_body[..]),
        });
    }
    let tx = Transaction::V4 {
        inputs: vec![],
        outputs,
        lock_time: LockTime::min_lock_time(),
        expiry_height: block::Height(0),
        joinsplit_data: None,
        sapling_shielded_data: None,
    };
    let sz = serialized_size(&tx);
    assert!(
        sz > COST_THRESHOLD,
        "test assumption failed: padded v4 tx serialized to {} bytes, expected > {}",
        sz,
        COST_THRESHOLD
    );
    let vtx = verified(tx, 5000);
    assert_eq!(
        vtx.cost(),
        sz,
        "cost of a large tx must equal its serialized size {}, got different value",
        sz
    );
}

#[test]
fn cost_equals_max_of_size_and_threshold() {
    // Invariant: cost() == max(size, 4000) for any transaction.
    let tx_small = minimal_v4_tx();
    let sz_small = serialized_size(&tx_small);
    let vtx_small = verified(tx_small, 0);
    assert_eq!(
        vtx_small.cost(),
        std::cmp::max(sz_small, COST_THRESHOLD),
        "cost must equal max(size={}, 4000)", sz_small
    );

    let tx_v5 = minimal_v5_tx();
    let sz_v5 = serialized_size(&tx_v5);
    let vtx_v5 = verified(tx_v5, 0);
    assert_eq!(
        vtx_v5.cost(),
        std::cmp::max(sz_v5, COST_THRESHOLD),
        "cost must equal max(size={}, 4000) for v5 tx", sz_v5
    );
}

#[test]
fn eviction_weight_with_zero_fee_adds_penalty() {
    // fee = 0 < 1000 (conventional_fee): low_fee_penalty = 16000 applies.
    let tx = minimal_v4_tx();
    let expected_cost = COST_THRESHOLD; // small tx
    let vtx = verified(tx, 0);
    let expected_weight = expected_cost + LOW_FEE_PENALTY;
    assert_eq!(
        vtx.eviction_weight(),
        expected_weight,
        "eviction_weight with zero fee must be cost + 16000 = {}",
        expected_weight
    );
}

#[test]
fn eviction_weight_with_fee_below_conventional_adds_penalty() {
    // fee = 999 < 1000: penalty applies.
    let tx = minimal_v4_tx();
    let vtx = verified(tx, 999);
    let expected = COST_THRESHOLD + LOW_FEE_PENALTY;
    assert_eq!(
        vtx.eviction_weight(),
        expected,
        "eviction_weight with fee=999 must add low-fee penalty; expected {}", expected
    );
}

#[test]
fn eviction_weight_at_conventional_fee_has_no_penalty() {
    // fee = 1000 == conventional_fee: no penalty.
    let tx = minimal_v4_tx();
    let vtx = verified(tx, 1000);
    let expected = COST_THRESHOLD; // no penalty
    assert_eq!(
        vtx.eviction_weight(),
        expected,
        "eviction_weight at conventional fee must equal cost with no penalty; expected {}", expected
    );
}

#[test]
fn eviction_weight_above_conventional_fee_has_no_penalty() {
    // fee = 50000 >> 1000: no penalty.
    let tx = minimal_v4_tx();
    let vtx = verified(tx, 50_000);
    let expected = COST_THRESHOLD;
    assert_eq!(
        vtx.eviction_weight(),
        expected,
        "eviction_weight with high fee must equal cost; expected {}", expected
    );
}

#[test]
fn eviction_weight_large_tx_low_fee() {
    // Large tx + low fee: weight = size + 16000.
    use zebra_chain::transparent;
    use zebra_chain::amount::Amount;
    let script_body = [0x51u8; 50];
    let mut outputs = Vec::new();
    for _ in 0..150 {
        outputs.push(transparent::Output {
            value: Amount::zero(),
            lock_script: transparent::Script::new(&script_body[..]),
        });
    }
    let tx = Transaction::V4 {
        inputs: vec![],
        outputs,
        lock_time: LockTime::min_lock_time(),
        expiry_height: block::Height(0),
        joinsplit_data: None,
        sapling_shielded_data: None,
    };
    let sz = serialized_size(&tx);
    assert!(sz > COST_THRESHOLD, "assumption: padded tx ({}b) > 4000 bytes", sz);
    let vtx = verified(tx, 0); // zero fee => penalty
    let expected = sz + LOW_FEE_PENALTY;
    assert_eq!(
        vtx.eviction_weight(),
        expected,
        "eviction_weight for large tx + zero fee must be {} + {} = {}",
        sz, LOW_FEE_PENALTY, expected
    );
}

#[test]
fn eviction_weight_large_tx_adequate_fee() {
    // Large tx + adequate fee: weight = size, no penalty.
    use zebra_chain::transparent;
    use zebra_chain::amount::Amount;
    let script_body = [0x51u8; 50];
    let mut outputs = Vec::new();
    for _ in 0..150 {
        outputs.push(transparent::Output {
            value: Amount::zero(),
            lock_script: transparent::Script::new(&script_body[..]),
        });
    }
    let tx = Transaction::V4 {
        inputs: vec![],
        outputs,
        lock_time: LockTime::min_lock_time(),
        expiry_height: block::Height(0),
        joinsplit_data: None,
        sapling_shielded_data: None,
    };
    let sz = serialized_size(&tx);
    assert!(sz > COST_THRESHOLD, "assumption: padded tx ({}b) > 4000 bytes", sz);
    let vtx = verified(tx, 1000); // exactly at conventional fee => no penalty
    assert_eq!(
        vtx.eviction_weight(),
        sz,
        "eviction_weight for large tx at conventional fee must equal size={}", sz
    );
}

#[test]
fn cost_does_not_consume_self() {
    // `cost` takes &self; calling it twice on the same binding must compile.
    let tx = minimal_v4_tx();
    let vtx = verified(tx, 5000);
    let c1 = vtx.cost();
    let c2 = vtx.cost();
    assert_eq!(c1, c2, "cost() must be idempotent");
}

#[test]
fn verified_unmined_tx_fields_are_accessible() {
    // `transaction` (UnminedTx) and `miner_fee` (Amount<NonNegative>) are pub.
    let tx = minimal_v4_tx();
    let sz = serialized_size(&tx);
    let vtx = verified(tx, 2000);
    // Access the pub fields directly.
    let _ = &vtx.transaction;   // UnminedTx
    let fee_u64: u64 = vtx.miner_fee.into();
    assert_eq!(fee_u64, 2000, "miner_fee field must hold the fee passed to new()");
    // The UnminedTx carries the size.
    assert_eq!(
        vtx.transaction.size as u64,
        sz,
        "UnminedTx.size must equal the serialized byte count"
    );
}
