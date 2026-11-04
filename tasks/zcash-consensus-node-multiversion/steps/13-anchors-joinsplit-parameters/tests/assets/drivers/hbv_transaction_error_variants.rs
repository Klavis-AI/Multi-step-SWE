//! Transaction error variant surface — step 13.
//!
//! Harbor verifier driver -- injected into `zebra-consensus/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! Tests that every new error variant added at step 13 exists with the correct
//! name, payload type, and Display text, and that existing variants are still
//! present. No network I/O, no proof verification.

use zebra_consensus::error::{BlockError, SubsidyError, TransactionError};

// ------------------------------------------------------------------- helpers

fn is_transaction_error<E: std::error::Error + 'static>() {}
fn is_send_sync<T: Send + Sync + 'static>() {}

// ------------------------------------------------------------------- tests

#[test]
fn subsidy_error_invalid_miner_fees_exists() {
    let e = SubsidyError::InvalidMinerFees;
    assert!(e.to_string().contains("miner fees"));
}

#[test]
fn subsidy_error_sum_overflow_exists() {
    let e = SubsidyError::SumOverflow;
    assert!(e.to_string().contains("sum") || e.to_string().contains("overflow"));
}

#[test]
fn transaction_error_locked_until_after_block_height() {
    let h = zebra_chain::block::Height(500_000);
    let e = TransactionError::LockedUntilAfterBlockHeight(h);
    let s = e.to_string();
    assert!(s.contains("500000"), "expected height in message, got: {}", s);
}

#[test]
fn transaction_error_locked_until_after_block_time() {
    use chrono::{TimeZone, Utc};
    let t = Utc.timestamp(1_600_000_000, 0);
    let e = TransactionError::LockedUntilAfterBlockTime(t);
    let s = e.to_string();
    assert!(!s.is_empty());
}

#[test]
fn transaction_error_coinbase_expiry_block_height() {
    let h = zebra_chain::block::Height(800_000);
    let e = TransactionError::CoinbaseExpiryBlockHeight {
        expiry_height: Some(h),
        block_height: h,
        transaction_hash: zebra_chain::transaction::Hash::from([0u8; 32]),
    };
    assert!(e.to_string().contains("expiry") || e.to_string().contains("coinbase"));
}

#[test]
fn transaction_error_maximum_expiry_height() {
    let h = zebra_chain::block::Height(500_000_001);
    let bh = zebra_chain::block::Height(400_000);
    let e = TransactionError::MaximumExpiryHeight {
        expiry_height: h,
        is_coinbase: false,
        block_height: bh,
        transaction_hash: zebra_chain::transaction::Hash::from([1u8; 32]),
    };
    let s = e.to_string();
    assert!(s.contains("expiry") || s.contains("maximum"), "got: {}", s);
}

#[test]
fn transaction_error_expired_transaction() {
    let expiry = zebra_chain::block::Height(300_000);
    let block = zebra_chain::block::Height(400_000);
    let e = TransactionError::ExpiredTransaction {
        expiry_height: expiry,
        block_height: block,
        transaction_hash: zebra_chain::transaction::Hash::from([2u8; 32]),
    };
    let s = e.to_string();
    assert!(s.contains("expir") || s.contains("mined"), "got: {}", s);
}

#[test]
fn transaction_error_groth16_is_string_payload() {
    let e = TransactionError::Groth16("test error".to_string());
    let s = e.to_string();
    assert!(!s.is_empty());
}

#[test]
fn transaction_error_malformed_groth16_is_string_payload() {
    let e = TransactionError::MalformedGroth16("io error".to_string());
    let s = e.to_string();
    assert!(s.contains("Groth16") || s.contains("malformed") || !s.is_empty());
}

#[test]
fn transaction_error_both_vpubs_nonzero() {
    let e = TransactionError::BothVPubsNonZero;
    let s = e.to_string();
    assert!(s.contains("vpub") || s.contains("zero"), "got: {}", s);
}

#[test]
fn block_error_summing_miner_fees() {
    use zebra_chain::amount;
    let source = amount::Error::Constraint {
        value: 0,
        range: 0..=2_100_000_000_000_000_i64,
    };
    let e = BlockError::SummingMinerFees {
        height: zebra_chain::block::Height(1),
        hash: zebra_chain::block::Hash([0u8; 32]),
        source,
    };
    let s = e.to_string();
    assert!(s.contains("miner") || s.contains("summing") || s.contains("fee"), "got: {}", s);
}

#[test]
fn block_error_from_transaction_error_roundtrip() {
    let inner = TransactionError::BothVPubsNonZero;
    let outer = BlockError::from(inner.clone());
    match outer {
        BlockError::Transaction(ref got) => assert_eq!(got, &inner),
        other => panic!("expected Transaction variant, got {:?}", other),
    }
}

#[test]
fn transaction_error_is_clone_and_partialeq() {
    let e1 = TransactionError::BothVPubsNonZero;
    let e2 = e1.clone();
    assert_eq!(e1, e2);
    let e3 = TransactionError::Groth16("a".to_string());
    let e4 = TransactionError::Groth16("a".to_string());
    assert_eq!(e3, e4);
    let e5 = TransactionError::MalformedGroth16("b".to_string());
    let e6 = TransactionError::MalformedGroth16("b".to_string());
    assert_eq!(e5, e6);
}
