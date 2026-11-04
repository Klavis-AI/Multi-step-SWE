//! `UnminedTx`'s serialized size, and the fee that `VerifiedUnminedTx` carries.
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! A mempool has to answer two questions about a transaction it is holding
//! that a block never has to answer: how many bytes does it occupy, and what
//! fee does it pay? Step 9 gave the node `UnminedTx`, a transaction plus the
//! identifier it arrived under. This milestone adds the byte count to that
//! pair, and adds a second type on top of it -- `VerifiedUnminedTx` -- which
//! is what a mempool stores once the verifier has returned a fee.
//!
//! The size assertions bite because the expected byte counts come from the
//! `bytes` column of `blocks.tsv`, produced in Python from the chain bytes.
//! A single-transaction block fixes its lone transaction's size exactly
//! (block bytes, minus the 1487-byte header, minus the one compactSize byte),
//! and the summed identity over every block pins the rest. The fee assertions
//! bite because `Amount<NonNegative>` refuses to hold a negative value, so a
//! `VerifiedUnminedTx` built the wrong way round cannot be constructed at all.

use std::convert::TryFrom;

use zebra_chain::amount::{Amount, NonNegative};
use zebra_chain::block::Block;
use zebra_chain::serialization::ZcashDeserialize;
use zebra_chain::transaction::{Transaction, UnminedTx, UnminedTxId, VerifiedUnminedTx};

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
    Block::zcash_deserialize(std::io::Cursor::new(&fixture.raw[..]))
        .unwrap_or_else(|e| panic!("{} did not parse: {:?}", fixture.name, e))
}

/// A Zcash block header is a fixed 1487 bytes, and the transaction count's
/// compactSize starts immediately after it.
const HEADER_BYTES: usize = 1487;

/// How many bytes a canonical compactSize encoding of `n` occupies.
///
/// Re-implemented here from the wire format rather than taken from the crate.
fn compactsize_len(n: u64) -> usize {
    if n < 0xfd {
        1
    } else if n <= 0xffff {
        3
    } else if n <= 0xffff_ffff {
        5
    } else {
        9
    }
}

fn fee(zatoshis: i64) -> Amount<NonNegative> {
    Amount::try_from(zatoshis)
        .unwrap_or_else(|e| panic!("{} zatoshis is not a valid fee: {:?}", zatoshis, e))
}

/// The first transaction of the first corpus block, as an owned value.
fn a_corpus_transaction() -> Transaction {
    let fixture = blocks().remove(0);
    let block = parse(&fixture);
    (*block.transactions[0]).clone()
}

// ------------------------------------------------------------------- tests

#[test]
fn unmined_tx_size_is_the_block_size_minus_its_framing() {
    // Corpus assertion, not a code assertion: the corpus must actually contain
    // single-transaction blocks, or this test degenerates into a no-op.
    let mut checked = 0;
    for fixture in blocks() {
        if fixture.tx_count != 1 {
            continue;
        }
        let block = parse(&fixture);
        let expected = fixture.size - HEADER_BYTES - 1;
        let unmined = UnminedTx::from(block.transactions[0].clone());
        assert_eq!(
            unmined.size, expected,
            "{}: UnminedTx::size is {}; the block is {} bytes, so its only \
             transaction must be {}",
            fixture.name, unmined.size, fixture.size, expected
        );
        checked += 1;
    }
    assert!(
        checked >= 8,
        "the corpus supplied {} single-transaction blocks; the verifier expects at least 8",
        checked
    );
}

#[test]
fn unmined_tx_sizes_and_the_block_framing_add_up_to_the_block_size() {
    for fixture in blocks() {
        let block = parse(&fixture);
        let mut total = HEADER_BYTES + compactsize_len(block.transactions.len() as u64);
        for transaction in block.transactions.iter() {
            total += UnminedTx::from(transaction).size;
        }
        assert_eq!(
            total, fixture.size,
            "{}: 1487 header bytes + the transaction count prefix + the {} \
             UnminedTx sizes add up to {}, but the block is {} bytes",
            fixture.name,
            block.transactions.len(),
            total,
            fixture.size
        );
    }
}

#[test]
fn all_four_conversions_produce_the_same_unmined_transaction() {
    for fixture in blocks() {
        let block = parse(&fixture);
        for (i, arc) in block.transactions.iter().enumerate() {
            let owned: Transaction = (**arc).clone();

            let from_value = UnminedTx::from(owned.clone());
            let from_ref = UnminedTx::from(&owned);
            let from_arc = UnminedTx::from(arc.clone());
            let from_arc_ref = UnminedTx::from(arc);

            for (what, built) in &[
                ("&Transaction", &from_ref),
                ("Arc<Transaction>", &from_arc),
                ("&Arc<Transaction>", &from_arc_ref),
            ] {
                assert_eq!(
                    built.size, from_value.size,
                    "{} transaction {}: From<{}> reported size {}, From<Transaction> reported {}",
                    fixture.name, i, what, built.size, from_value.size
                );
                assert_eq!(
                    built.id, from_value.id,
                    "{} transaction {}: From<{}> produced a different id",
                    fixture.name, i, what
                );
                assert_eq!(
                    built.transaction, from_value.transaction,
                    "{} transaction {}: From<{}> produced a different transaction",
                    fixture.name, i, what
                );
            }
        }
    }
}

#[test]
fn the_identifier_of_a_v1_to_v4_transaction_is_its_legacy_mined_id() {
    // Every transaction in the corpus is v1-v4 -- the corpus stops at Canopy,
    // before NU5 -- so every identifier must be the `Legacy` variant wrapping
    // the same mined hash the merkle root of the block was built from.
    for fixture in blocks() {
        let block = parse(&fixture);
        for (i, arc) in block.transactions.iter().enumerate() {
            let unmined = UnminedTx::from(arc);
            match unmined.id {
                UnminedTxId::Legacy(hash) => assert_eq!(
                    hash,
                    arc.hash(),
                    "{} transaction {}: the legacy identifier is not the transaction hash",
                    fixture.name,
                    i
                ),
                other => panic!(
                    "{} transaction {}: a v1-v4 transaction got a witnessed identifier: {:?}",
                    fixture.name, i, other
                ),
            }
        }
    }
}

#[test]
fn unmined_tx_display_reports_the_serialized_size() {
    for fixture in blocks() {
        let block = parse(&fixture);
        let unmined = UnminedTx::from(&block.transactions[0]);
        let shown = format!("{}", unmined);
        assert!(
            shown.starts_with("UnminedTx {"),
            "{}: Display for UnminedTx began with {:?}, expected it to open \
             with `UnminedTx {{`",
            fixture.name,
            &shown[..shown.len().min(40)]
        );
        let expected_field = format!("serialized_size: {}", unmined.size);
        assert!(
            shown.contains(&expected_field),
            "{}: Display for UnminedTx does not contain {:?}",
            fixture.name,
            expected_field
        );
    }
}

#[test]
fn verified_unmined_tx_new_keeps_its_transaction_and_its_fee() {
    let unmined = UnminedTx::from(a_corpus_transaction());
    let verified = VerifiedUnminedTx::new(unmined.clone(), fee(1_234));

    assert_eq!(
        verified.transaction, unmined,
        "VerifiedUnminedTx::new changed the transaction it was given"
    );
    assert_eq!(
        verified.transaction.size, unmined.size,
        "VerifiedUnminedTx::new lost the serialized size: {} vs {}",
        verified.transaction.size, unmined.size
    );
    assert_eq!(
        i64::from(verified.miner_fee),
        1_234,
        "VerifiedUnminedTx::new stored a fee of {} zatoshis, expected 1234",
        i64::from(verified.miner_fee)
    );
}

#[test]
fn verified_unmined_tx_display_reports_the_miner_fee() {
    let verified = VerifiedUnminedTx::new(UnminedTx::from(a_corpus_transaction()), fee(10_000));
    let shown = format!("{}", verified);
    assert!(
        shown.starts_with("VerifiedUnminedTx {"),
        "Display for VerifiedUnminedTx began with {:?}, expected it to open \
         with `VerifiedUnminedTx {{`",
        &shown[..shown.len().min(40)]
    );
    assert!(
        shown.contains("transaction: "),
        "Display for VerifiedUnminedTx has no `transaction` field: {:?}",
        &shown[..shown.len().min(120)]
    );
    assert!(
        shown.contains("miner_fee: "),
        "Display for VerifiedUnminedTx has no `miner_fee` field: {:?}",
        &shown[..shown.len().min(120)]
    );
}

#[test]
fn verified_unmined_transactions_compare_by_transaction_and_fee() {
    let unmined = UnminedTx::from(a_corpus_transaction());
    let one = VerifiedUnminedTx::new(unmined.clone(), fee(1_000));
    let same = VerifiedUnminedTx::new(unmined.clone(), fee(1_000));
    let dearer = VerifiedUnminedTx::new(unmined, fee(1_001));

    assert_eq!(
        one,
        same.clone(),
        "two verified transactions with the same transaction and fee compared unequal"
    );
    assert!(
        one != dearer,
        "two verified transactions differing only in their fee compared equal"
    );
}

#[test]
fn a_miner_fee_cannot_be_negative() {
    // The fee's type is the constraint: a mempool never stores a transaction
    // that pays out more than it takes in, so the fee is `Amount<NonNegative>`
    // and the conversion from a negative i64 fails rather than wrapping.
    let attempt: Result<Amount<NonNegative>, _> = Amount::try_from(-1i64);
    assert!(
        attempt.is_err(),
        "Amount::<NonNegative>::try_from(-1) succeeded, so a fee could be negative"
    );
}

#[test]
fn a_zero_miner_fee_is_representable() {
    // Zero is a legal fee, so the mempool's fee type must accept it; only
    // negative values are excluded.
    let verified = VerifiedUnminedTx::new(UnminedTx::from(a_corpus_transaction()), fee(0));
    assert_eq!(
        i64::from(verified.miner_fee),
        0,
        "a zero fee did not survive VerifiedUnminedTx::new"
    );
}
