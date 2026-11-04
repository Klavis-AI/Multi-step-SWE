//! ZIP-203 transaction expiry: `nExpiryHeight` of zero means "never expires".
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! A mempool has to drop transactions that can no longer be mined, which means
//! it has to ask each held transaction "at what height do you expire?". The
//! answer is not simply the `nExpiryHeight` field. The NU5 protocol
//! specification's transaction encoding tables (§7.1) describe the field as
//!
//!     nExpiryHeight  uint32  A block height after which the transaction will
//!                            expire, or 0 to disable expiry.  [ZIP-203]
//!
//! and the consensus rule is written to match: "[Overwinter onward] If a
//! transaction is not a coinbase transaction and its nExpiryHeight field is
//! *nonzero*, then it MUST NOT be mined at a block height greater than its
//! nExpiryHeight."
//!
//! So zero is a sentinel, not a height. Before this milestone
//! `Transaction::expiry_height` handed the raw field straight back, and a
//! caller that compared it against the chain tip would have discarded every
//! transaction that opted out of expiry -- which, in the shipped corpus, is 27
//! of the 58 transactions that have the field at all.
//!
//! The sentinel lives in the accessor, not in the codec: the zero must still
//! be written to and read from the wire, or the transaction's ID changes. The
//! two round-trip tests below are what separate those two requirements.

use std::io::Cursor;

use zebra_chain::block::{Block, Height};
use zebra_chain::parameters::NetworkUpgrade;
use zebra_chain::serialization::{ZcashDeserialize, ZcashSerialize};
use zebra_chain::transaction::{LockTime, Transaction};

// ---------------------------------------------------------------- fixtures
//
// The corpus is seventeen real blocks from the Zcash mainnet and testnet,
// spanning the genesis block and every network upgrade activation height up to
// Canopy, shipped as hex alongside a TSV of the values a correct
// implementation must reproduce. Every expected value in the TSV was derived
// from the block bytes themselves -- the block hash by SHA256d over the
// 1487-byte header, the transaction count by a compactSize reader -- and not
// from any Rust implementation of this interface.

use std::collections::BTreeMap;
use std::path::PathBuf;

#[allow(dead_code)]
pub struct Fixture {
    pub name: String,
    pub network: String,
    pub height: u32,
    /// The block's length in bytes, as recorded in the TSV.
    pub size: usize,
    pub block_hash: String,
    pub previous_block_hash: String,
    pub merkle_root: [u8; 32],
    pub root_bytes: [u8; 32],
    pub version: u32,
    pub time: i64,
    pub difficulty_threshold: u32,
    pub nonce: [u8; 32],
    pub tx_count: usize,
    pub note: String,
    pub raw: Vec<u8>,
}

fn fixture_dir() -> PathBuf {
    PathBuf::from(
        std::env::var("HBV_FIXTURES")
            .expect("HBV_FIXTURES must point at the shipped fixture directory"),
    )
}

fn cell<'a>(row: &BTreeMap<&'a str, &'a str>, key: &str) -> &'a str {
    *row.get(key)
        .unwrap_or_else(|| panic!("blocks.tsv has no column {}", key))
}

fn thirty_two(hex_str: &str) -> [u8; 32] {
    let bytes = hex::decode(hex_str).expect("fixture field is hex");
    assert_eq!(bytes.len(), 32, "fixture field is 32 bytes");
    let mut out = [0u8; 32];
    out.copy_from_slice(&bytes);
    out
}

/// Load the whole block corpus, in the order the TSV lists it.
#[allow(dead_code)]
pub fn blocks() -> Vec<Fixture> {
    let dir = fixture_dir();
    let tsv = std::fs::read_to_string(dir.join("blocks.tsv")).expect("blocks.tsv is readable");
    let mut lines = tsv.lines();
    let header: Vec<&str> = lines
        .next()
        .expect("blocks.tsv has a header row")
        .trim_start_matches("# ")
        .split('\t')
        .collect();

    let mut out = Vec::new();
    for line in lines {
        if line.is_empty() {
            continue;
        }
        let cells: Vec<&str> = line.split('\t').collect();
        assert_eq!(cells.len(), header.len(), "malformed row: {}", line);
        let row: BTreeMap<&str, &str> = header.iter().cloned().zip(cells).collect();

        let name = cell(&row, "name").to_string();
        let hex_text = std::fs::read_to_string(dir.join("blocks").join(format!("{}.hex", name)))
            .unwrap_or_else(|e| panic!("cannot read block {}: {}", name, e));
        let raw = hex::decode(hex_text.trim()).expect("block fixture is hex");

        out.push(Fixture {
            name,
            raw,
            network: cell(&row, "network").to_string(),
            height: cell(&row, "height").parse().expect("height"),
            size: cell(&row, "bytes").parse().expect("bytes"),
            block_hash: cell(&row, "block_hash").to_string(),
            previous_block_hash: cell(&row, "previous_block_hash").to_string(),
            merkle_root: thirty_two(cell(&row, "merkle_root_hex")),
            root_bytes: thirty_two(cell(&row, "root_bytes_hex")),
            version: cell(&row, "version").parse().expect("version"),
            time: cell(&row, "time").parse().expect("time"),
            difficulty_threshold: cell(&row, "difficulty_threshold")
                .parse()
                .expect("difficulty_threshold"),
            nonce: thirty_two(cell(&row, "nonce_hex")),
            tx_count: cell(&row, "tx_count").parse().expect("tx_count"),
            note: cell(&row, "note").to_string(),
        });
    }
    assert!(
        out.len() >= 17,
        "the shipped corpus has {} blocks; the verifier expects at least 17",
        out.len()
    );
    out
}

// ----------------------------------------------------------------- helpers

fn parse(fixture: &Fixture) -> Block {
    Block::zcash_deserialize(Cursor::new(&fixture.raw[..]))
        .unwrap_or_else(|e| panic!("{} did not parse: {:?}", fixture.name, e))
}

/// A transparent-only v3 transaction with no inputs and no outputs, carrying
/// exactly the `expiry_height` asked for.
fn v3(expiry_height: u32) -> Transaction {
    Transaction::V3 {
        inputs: Vec::new(),
        outputs: Vec::new(),
        lock_time: LockTime::min_lock_time(),
        expiry_height: Height(expiry_height),
        joinsplit_data: None,
    }
}

/// The same, as a v4 transaction.
fn v4(expiry_height: u32) -> Transaction {
    Transaction::V4 {
        inputs: Vec::new(),
        outputs: Vec::new(),
        lock_time: LockTime::min_lock_time(),
        expiry_height: Height(expiry_height),
        joinsplit_data: None,
        sapling_shielded_data: None,
    }
}

/// The same, as a v5 transaction on the NU5 consensus branch.
fn v5(expiry_height: u32) -> Transaction {
    Transaction::V5 {
        network_upgrade: NetworkUpgrade::Nu5,
        lock_time: LockTime::min_lock_time(),
        expiry_height: Height(expiry_height),
        inputs: Vec::new(),
        outputs: Vec::new(),
        sapling_shielded_data: None,
        orchard_shielded_data: None,
    }
}

fn v1() -> Transaction {
    Transaction::V1 {
        inputs: Vec::new(),
        outputs: Vec::new(),
        lock_time: LockTime::min_lock_time(),
    }
}

fn v2() -> Transaction {
    Transaction::V2 {
        inputs: Vec::new(),
        outputs: Vec::new(),
        lock_time: LockTime::min_lock_time(),
        joinsplit_data: None,
    }
}

// ------------------------------------------------------------------- tests

#[test]
fn a_zero_expiry_height_reports_no_expiry_in_every_version_that_has_the_field() {
    for (version, transaction) in &[("v3", v3(0)), ("v4", v4(0)), ("v5", v5(0))] {
        assert_eq!(
            transaction.expiry_height(),
            None,
            "{}: nExpiryHeight 0 disables expiry, so expiry_height() must be None, got {:?}",
            version,
            transaction.expiry_height()
        );
    }
}

#[test]
fn a_nonzero_expiry_height_is_reported_unchanged() {
    for &height in &[1u32, 2, 347_521, 499_999_999] {
        for (version, transaction) in &[
            ("v3", v3(height)),
            ("v4", v4(height)),
            ("v5", v5(height)),
        ] {
            assert_eq!(
                transaction.expiry_height(),
                Some(Height(height)),
                "{}: nExpiryHeight {} must be reported as Some(Height({})), got {:?}",
                version,
                height,
                height,
                transaction.expiry_height()
            );
        }
    }
}

#[test]
fn the_largest_permitted_expiry_height_is_reported() {
    // The consensus bound on the field is 499_999_999; the accessor reports it
    // rather than rejecting it, because the bound is checked elsewhere.
    assert_eq!(
        v4(499_999_999).expiry_height(),
        Some(Height(499_999_999)),
        "the largest permitted nExpiryHeight was not reported"
    );
}

#[test]
fn v1_and_v2_transactions_never_report_an_expiry_height() {
    // The field does not exist before Overwinter, so there is nothing to
    // report and nothing to compare against the tip.
    assert_eq!(
        v1().expiry_height(),
        None,
        "a v1 transaction reported an expiry height"
    );
    assert_eq!(
        v2().expiry_height(),
        None,
        "a v2 transaction reported an expiry height"
    );
}

#[test]
fn no_corpus_transaction_reports_an_expiry_height_of_zero() {
    // `Some(Height(0))` is the shape a mempool must never see: it would be
    // read as "expired before the genesis block".
    for fixture in blocks() {
        let block = parse(&fixture);
        for (i, transaction) in block.transactions.iter().enumerate() {
            assert!(
                transaction.expiry_height() != Some(Height(0)),
                "{} transaction {} (v{}) reported Some(Height(0)); zero disables expiry \
                 and must be reported as None",
                fixture.name,
                i,
                transaction.version()
            );
        }
    }
}

#[test]
fn every_corpus_transaction_before_overwinter_reports_no_expiry_height() {
    let mut checked = 0;
    for fixture in blocks() {
        let block = parse(&fixture);
        for (i, transaction) in block.transactions.iter().enumerate() {
            if transaction.version() > 2 {
                continue;
            }
            assert_eq!(
                transaction.expiry_height(),
                None,
                "{} transaction {} is a v{} transaction and has no nExpiryHeight field, \
                 but reported {:?}",
                fixture.name,
                i,
                transaction.version(),
                transaction.expiry_height()
            );
            checked += 1;
        }
    }
    // Corpus assertion, not a code assertion: the corpus must actually reach
    // back before Overwinter, or this test degenerates into a no-op.
    assert!(
        checked >= 10,
        "the corpus supplied {} v1/v2 transactions; the verifier expects at least 10",
        checked
    );
}

#[test]
fn the_corpus_holds_both_expiring_and_non_expiring_transactions() {
    // Corpus assertion, not a code assertion. Both populations have to be
    // non-trivial, or "always None" and "always Some" would each pass one half
    // of this group. In the shipped corpus 27 of the 58 Overwinter-or-later
    // transactions set nExpiryHeight to 0 and 31 set it to a real height.
    let mut never_expires = 0;
    let mut expires = 0;
    for fixture in blocks() {
        let block = parse(&fixture);
        for transaction in block.transactions.iter() {
            if transaction.version() <= 2 {
                continue;
            }
            match transaction.expiry_height() {
                None => never_expires += 1,
                Some(height) => {
                    assert!(
                        height.0 > 0,
                        "{}: a reported expiry height must be greater than zero",
                        fixture.name
                    );
                    expires += 1;
                }
            }
        }
    }
    assert!(
        never_expires >= 20,
        "{} Overwinter-or-later corpus transactions reported no expiry; \
         the verifier expects at least 20",
        never_expires
    );
    assert!(
        expires >= 20,
        "{} Overwinter-or-later corpus transactions reported an expiry height; \
         the verifier expects at least 20",
        expires
    );
}

#[test]
fn a_zero_expiry_height_still_travels_on_the_wire() {
    // The sentinel belongs to the accessor, not to the codec. A v4
    // transaction with nExpiryHeight 0 must serialize a four-byte zero in the
    // expiry position and read back as the same transaction -- if the codec
    // dropped or rewrote the field, the transaction's ID would change.
    let transaction = v4(0);
    let bytes = transaction
        .zcash_serialize_to_vec()
        .expect("a v4 transaction serializes");
    let again =
        Transaction::zcash_deserialize(Cursor::new(&bytes[..])).expect("it deserializes again");

    assert_eq!(
        again, transaction,
        "a v4 transaction with nExpiryHeight 0 did not round trip"
    );
    assert_eq!(
        again.expiry_height(),
        None,
        "after a round trip, nExpiryHeight 0 was reported as {:?}",
        again.expiry_height()
    );
}

#[test]
fn a_nonzero_expiry_height_still_travels_on_the_wire() {
    let transaction = v4(347_521);
    let bytes = transaction
        .zcash_serialize_to_vec()
        .expect("a v4 transaction serializes");
    let again =
        Transaction::zcash_deserialize(Cursor::new(&bytes[..])).expect("it deserializes again");

    assert_eq!(
        again, transaction,
        "a v4 transaction with nExpiryHeight 347521 did not round trip"
    );
    assert_eq!(
        again.expiry_height(),
        Some(Height(347_521)),
        "after a round trip, nExpiryHeight 347521 was reported as {:?}",
        again.expiry_height()
    );
}
