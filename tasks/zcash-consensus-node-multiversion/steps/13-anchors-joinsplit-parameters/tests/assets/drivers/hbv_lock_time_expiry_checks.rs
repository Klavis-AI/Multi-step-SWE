//! Lock-time and expiry-height check functions — step 13.
//!
//! Harbor verifier driver -- injected into `zebra-consensus/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! Tests the four public check functions added in step 13:
//!   lock_time_has_passed, joinsplit_has_vpub_zero,
//!   coinbase_expiry_height, non_coinbase_expiry_height.
//! Also tests LockTime::unlocked() from zebra-chain.

use chrono::{TimeZone, Utc};

use zebra_chain::{
    block::Height,
    parameters::{Network, NetworkUpgrade},
    transaction::{LockTime, Transaction},
};
use zebra_consensus::transaction::check::{
    coinbase_expiry_height, joinsplit_has_vpub_zero, lock_time_has_passed, non_coinbase_expiry_height,
};
use zebra_consensus::error::TransactionError;

// ----------------------------------------------------------------- helpers

/// Build a minimal V1 transaction whose lock_time() returns None
/// (all transparent inputs have sequence == 0xffffffff, meaning "finalized").
fn tx_finalized_by_sequence() -> Transaction {
    use std::io::Cursor;
    use std::path::PathBuf;
    use zebra_chain::serialization::ZcashDeserialize;
    // A real mainnet genesis coinbase loaded from the shipped fixture directory.
    // Its single input has sequence = 0xffffffff, so lock_time() returns None
    // regardless of the lock_time field.
    let fixtures = PathBuf::from(
        std::env::var("HBV_FIXTURES")
            .expect("HBV_FIXTURES must point at the shipped fixture directory"),
    );
    let genesis_hex = std::fs::read_to_string(fixtures.join("blocks").join("main-0000000.hex"))
        .expect("genesis fixture is readable");
    let raw = hex::decode(genesis_hex.trim()).expect("genesis hex decodes");
    let block = zebra_chain::block::Block::zcash_deserialize(Cursor::new(&raw[..]))
        .expect("genesis parses");
    // Return the coinbase; it is finalized by sequence
    block.transactions[0].as_ref().clone()
}

/// Build a minimal coinbase-like V3 transaction with the given expiry height.
/// We use a raw-bytes approach: V3 (version=3, versionGroupId=0x3C48270F,
/// vin=0 coinbase, vout=0, locktime=0, expiry=N).
fn build_v3_coinbase(expiry_height: u32) -> Transaction {
    use std::io::Cursor;
    use zebra_chain::serialization::ZcashDeserialize;
    let mut raw: Vec<u8> = Vec::new();
    // version = 3, overwinter bit set → 0x80000003 little-endian
    raw.extend_from_slice(&0x80000003u32.to_le_bytes());
    // nVersionGroupId for Overwinter = 0x03C48270
    raw.extend_from_slice(&0x03C4_8270u32.to_le_bytes());
    // vin count = 1 (coinbase)
    raw.push(1);
    // outpoint hash = [0; 32], index = 0xFFFFFFFF
    raw.extend_from_slice(&[0u8; 32]);
    raw.extend_from_slice(&0xFFFFFFFFu32.to_le_bytes());
    // script = 1 byte: OP_1 (0x51) encodes height 1 in BIP34 (valid range 1-16 for OP_N)
    raw.push(1);   // script length
    raw.push(0x51); // OP_1 = height 1
    // sequence = 0xFFFFFFFF
    raw.extend_from_slice(&0xFFFFFFFFu32.to_le_bytes());
    // vout count = 1
    raw.push(1);
    // value = 1250000000 zatoshi (little-endian i64)
    raw.extend_from_slice(&1_250_000_000i64.to_le_bytes());
    // script length = 0
    raw.push(0);
    // nLockTime = 0
    raw.extend_from_slice(&0u32.to_le_bytes());
    // nExpiryHeight
    raw.extend_from_slice(&expiry_height.to_le_bytes());
    // nJoinSplit = 0
    raw.push(0);
    Transaction::zcash_deserialize(Cursor::new(&raw[..])).expect("v3 coinbase tx decodes")
}

/// Build a V3 transaction with a single **PrevOut** input whose sequence number
/// is 0, and the given raw `nLockTime`.
///
/// Both parts matter. `Transaction::lock_time()` returns `None` -- and so
/// `lock_time_has_passed` short-circuits to `Ok(())` -- unless the lock time is
/// something other than `LockTime::unlocked()` *and* at least one transparent
/// input carries a sequence number below `u32::MAX`. `build_v3_coinbase` above
/// satisfies neither condition, so it can never reach either error branch.
///
/// `lock_time_raw` is interpreted by `LockTime`'s deserializer: values up to
/// `Height::MAX` (499_999_999) become `LockTime::Height`, anything above becomes
/// `LockTime::Time`.
fn build_v3_prevout(lock_time_raw: u32, expiry_height: u32) -> Transaction {
    use std::io::Cursor;
    use zebra_chain::serialization::ZcashDeserialize;
    let mut raw: Vec<u8> = Vec::new();
    // version = 3, overwinter bit set -> 0x80000003 little-endian
    raw.extend_from_slice(&0x80000003u32.to_le_bytes());
    // nVersionGroupId for Overwinter = 0x03C48270
    raw.extend_from_slice(&0x03C4_8270u32.to_le_bytes());
    // vin count = 1
    raw.push(1);
    // outpoint hash: non-zero, so this parses as PrevOut rather than Coinbase
    raw.extend_from_slice(&[1u8; 32]);
    // outpoint index = 0
    raw.extend_from_slice(&0u32.to_le_bytes());
    // unlock script length = 0
    raw.push(0);
    // sequence = 0: below u32::MAX, so the lock time is enabled
    raw.extend_from_slice(&0u32.to_le_bytes());
    // vout count = 1
    raw.push(1);
    // value = 1 zatoshi (little-endian i64)
    raw.extend_from_slice(&1i64.to_le_bytes());
    // lock script length = 0
    raw.push(0);
    // nLockTime
    raw.extend_from_slice(&lock_time_raw.to_le_bytes());
    // nExpiryHeight
    raw.extend_from_slice(&expiry_height.to_le_bytes());
    // nJoinSplit = 0
    raw.push(0);
    Transaction::zcash_deserialize(Cursor::new(&raw[..])).expect("v3 prevout tx decodes")
}

// needed for zcash_deserialize
use zebra_chain::serialization::ZcashDeserialize;

// ------------------------------------------------------------------- tests

#[test]
fn lock_time_unlocked_constructor() {
    let lt = LockTime::unlocked();
    match lt {
        LockTime::Height(h) => assert_eq!(h, Height(0)),
        other => panic!("expected Height(0), got {:?}", other),
    }
}

#[test]
fn lock_time_min_and_max() {
    let min = LockTime::min_lock_time();
    let max = LockTime::max_lock_time();
    match min {
        LockTime::Time(t) => assert_eq!(t.timestamp(), LockTime::MIN_TIMESTAMP),
        _ => panic!("min_lock_time should be Time"),
    }
    match max {
        LockTime::Time(t) => assert_eq!(t.timestamp(), LockTime::MAX_TIMESTAMP),
        _ => panic!("max_lock_time should be Time"),
    }
}

#[test]
fn lock_time_has_passed_finalized_by_sequence_returns_ok() {
    let tx = tx_finalized_by_sequence();
    let now = Utc.timestamp(2_000_000_000, 0);
    assert_eq!(lock_time_has_passed(&tx, Height(1_000_000), now), Ok(()));
}

#[test]
fn lock_time_has_passed_height_locked_err() {
    // A height lock only bites when `lock_time()` is `Some(Height(_))`, which
    // needs both a non-zero lock time and a sequence number below u32::MAX.
    let tx = build_v3_prevout(1_000_000, 400_000);
    assert_eq!(
        tx.lock_time(),
        Some(LockTime::Height(Height(1_000_000))),
        "the fixture must be height-locked, otherwise this test proves nothing"
    );

    let now = Utc.timestamp(1_600_000_000, 0);

    // The rule is `block_height > unlock_height`, so the unlock height itself
    // is still locked -- an off-by-one that `>=` would silently pass.
    assert_eq!(
        lock_time_has_passed(&tx, Height(1_000_000), now),
        Err(TransactionError::LockedUntilAfterBlockHeight(Height(1_000_000)))
    );
    assert_eq!(
        lock_time_has_passed(&tx, Height(999_999), now),
        Err(TransactionError::LockedUntilAfterBlockHeight(Height(1_000_000)))
    );

    // One block later it is spendable.
    assert_eq!(lock_time_has_passed(&tx, Height(1_000_001), now), Ok(()));
}

#[test]
fn lock_time_has_passed_time_locked_err() {
    // Raw lock times above Height::MAX (499_999_999) deserialize as
    // `LockTime::Time`, which selects the block-time branch instead.
    let unlock_secs: u32 = 1_600_000_000;
    let unlock_time = Utc.timestamp(unlock_secs as i64, 0);
    let tx = build_v3_prevout(unlock_secs, 400_000);
    assert_eq!(
        tx.lock_time(),
        Some(LockTime::Time(unlock_time)),
        "the fixture must be time-locked, otherwise this test proves nothing"
    );

    // The height argument is irrelevant on this branch; a high one would mask
    // an implementation that compared heights instead of times.
    let height = Height(1_000_000);

    assert_eq!(
        lock_time_has_passed(&tx, height, unlock_time),
        Err(TransactionError::LockedUntilAfterBlockTime(unlock_time))
    );
    assert_eq!(
        lock_time_has_passed(&tx, height, unlock_time - chrono::Duration::seconds(1)),
        Err(TransactionError::LockedUntilAfterBlockTime(unlock_time))
    );
    assert_eq!(
        lock_time_has_passed(&tx, height, unlock_time + chrono::Duration::seconds(1)),
        Ok(())
    );
}

#[test]
fn joinsplit_has_vpub_zero_on_non_joinsplit_tx_returns_ok() {
    // A non-joinsplit tx (V1 or genesis coinbase) has no JoinSplits
    // so joinsplit_has_vpub_zero must return Ok
    let tx = tx_finalized_by_sequence();
    assert_eq!(joinsplit_has_vpub_zero(&tx), Ok(()));
}

#[test]
fn coinbase_expiry_height_valid_overwinter() {
    // V3 coinbase at block height 350_000 with expiry = 400_000 (<= 499_999_999)
    // Pre-NU5, so the rule is: expiry <= MAX_EXPIRY_HEIGHT
    let coinbase = build_v3_coinbase(400_000);
    let bh = Height(350_000);
    let result = coinbase_expiry_height(&bh, &coinbase, Network::Mainnet);
    // expiry=400_000 <= 499_999_999 → Ok
    assert_eq!(result, Ok(()));
}

#[test]
fn coinbase_expiry_height_too_large_returns_err() {
    // expiry = 500_000_000 > MAX_EXPIRY_HEIGHT (499_999_999) → MaximumExpiryHeight
    let coinbase = build_v3_coinbase(500_000_000);
    let bh = Height(350_000);
    let result = coinbase_expiry_height(&bh, &coinbase, Network::Mainnet);
    match result {
        Err(TransactionError::MaximumExpiryHeight { .. }) => {}
        other => panic!("expected MaximumExpiryHeight, got {:?}", other),
    }
}

#[test]
fn non_coinbase_expiry_height_valid_returns_ok() {
    // V3 tx with expiry = 400_000 mined at height 350_000: valid
    let tx = build_v3_coinbase(400_000); // also works for non-coinbase surface test
    let result = non_coinbase_expiry_height(&Height(350_000), &tx);
    assert_eq!(result, Ok(()));
}

#[test]
fn non_coinbase_expiry_height_expired_returns_err() {
    // V3 tx with expiry = 200_000 mined at height 300_000: expired
    let tx = build_v3_coinbase(200_000);
    let result = non_coinbase_expiry_height(&Height(300_000), &tx);
    match result {
        Err(TransactionError::ExpiredTransaction { .. }) => {}
        other => panic!("expected ExpiredTransaction, got {:?}", other),
    }
}

#[test]
fn non_coinbase_expiry_height_too_large_returns_err() {
    // V3 tx with expiry = 500_000_001 > MAX_EXPIRY_HEIGHT
    let tx = build_v3_coinbase(500_000_001);
    let result = non_coinbase_expiry_height(&Height(100_000), &tx);
    match result {
        Err(TransactionError::MaximumExpiryHeight { .. }) => {}
        other => panic!("expected MaximumExpiryHeight, got {:?}", other),
    }
}

#[test]
fn max_expiry_height_constant_value() {
    use zebra_chain::block::Height;
    // The spec mandates nExpiryHeight <= 499_999_999
    assert_eq!(Height::MAX_EXPIRY_HEIGHT, Height(499_999_999));
}
