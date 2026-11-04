//! The outpoints a transaction spends, and the one-line renderings a mempool
//! logs its decisions with.
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! Two transactions conflict, for mempool purposes, when they spend the same
//! transparent outpoint. Answering that needs one thing the chain types did
//! not previously offer: the outpoints a transaction spends, as a plain
//! iterator, with coinbase inputs already filtered out -- a coinbase input
//! creates value rather than spending an outpoint, so it has nothing to
//! conflict with.
//!
//! The second half of this group is the reason the mempool's logs are
//! readable. `Transaction` and `transparent::Input` get `Display`
//! implementations that summarise a transaction in one line instead of
//! dumping every proof and signature the derived `Debug` would.
//!
//! The expected values here are read out of the corpus by pattern matching on
//! the public `Input::PrevOut` variant's own fields, never by calling the
//! accessor under test.

use std::io::Cursor;

use zebra_chain::block::{Block, Height};
use zebra_chain::parameters::NetworkUpgrade;
use zebra_chain::serialization::ZcashDeserialize;
use zebra_chain::transaction::{LockTime, Transaction};
use zebra_chain::transparent::{Input, OutPoint};

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

/// The outpoints this transaction's `PrevOut` inputs name, in input order,
/// read straight off the public variant fields rather than through the
/// accessor under test.
fn prevout_fields(transaction: &Transaction) -> Vec<OutPoint> {
    let mut out = Vec::new();
    for input in transaction.inputs().iter() {
        if let Input::PrevOut { outpoint, .. } = input {
            out.push(*outpoint);
        }
    }
    out
}

/// A v5 transaction on the NU5 branch with no inputs, outputs or bundles.
fn empty_v5() -> Transaction {
    Transaction::V5 {
        network_upgrade: NetworkUpgrade::Nu5,
        lock_time: LockTime::min_lock_time(),
        expiry_height: Height(0),
        inputs: Vec::new(),
        outputs: Vec::new(),
        sapling_shielded_data: None,
        orchard_shielded_data: None,
    }
}

/// Assert that `labels` appear in `shown` in the order given.
fn assert_fields_in_order(what: &str, shown: &str, labels: &[&str]) {
    let mut from = 0;
    for label in labels {
        match shown[from..].find(label) {
            Some(at) => from += at + label.len(),
            None => panic!(
                "{}: expected the field {:?} after offset {} in {:?}",
                what, label, from, shown
            ),
        }
    }
}

// ------------------------------------------------------------------- tests

#[test]
fn a_coinbase_transaction_spends_no_outpoints() {
    // The first transaction of every block is the coinbase, and a coinbase
    // input creates value instead of spending an outpoint, so it must not
    // appear as a spend that another transaction could conflict with.
    for fixture in blocks() {
        let block = parse(&fixture);
        let coinbase = &block.transactions[0];
        let spent: Vec<OutPoint> = coinbase.spent_outpoints().collect();
        assert!(
            spent.is_empty(),
            "{}: the coinbase transaction reported {} spent outpoints, expected none",
            fixture.name,
            spent.len()
        );
    }
}

#[test]
fn spent_outpoints_lists_every_prevout_input_in_order() {
    for fixture in blocks() {
        let block = parse(&fixture);
        for (i, transaction) in block.transactions.iter().enumerate() {
            let expected = prevout_fields(transaction);
            let spent: Vec<OutPoint> = transaction.spent_outpoints().collect();
            assert_eq!(
                spent.len(),
                expected.len(),
                "{} transaction {}: spent_outpoints yielded {} outpoints, \
                 the transaction has {} PrevOut inputs",
                fixture.name,
                i,
                spent.len(),
                expected.len()
            );
            for (j, (got, want)) in spent.iter().zip(expected.iter()).enumerate() {
                assert_eq!(
                    got.hash, want.hash,
                    "{} transaction {} outpoint {}: wrong previous transaction hash",
                    fixture.name, i, j
                );
                assert_eq!(
                    got.index, want.index,
                    "{} transaction {} outpoint {}: wrong output index",
                    fixture.name, i, j
                );
            }
        }
    }
}

#[test]
fn a_transaction_with_only_prevout_inputs_spends_one_outpoint_per_input() {
    let mut checked = 0;
    for fixture in blocks() {
        let block = parse(&fixture);
        for (i, transaction) in block.transactions.iter().enumerate() {
            let inputs = transaction.inputs().len();
            if inputs == 0 || transaction.has_any_coinbase_inputs() {
                continue;
            }
            assert_eq!(
                transaction.spent_outpoints().count(),
                inputs,
                "{} transaction {}: {} transparent inputs, none of them coinbase, \
                 but {} spent outpoints",
                fixture.name,
                i,
                inputs,
                transaction.spent_outpoints().count()
            );
            checked += 1;
        }
    }
    // Corpus assertion, not a code assertion: the corpus must actually carry
    // transactions that spend, or this test degenerates into a no-op.
    assert!(
        checked >= 30,
        "the corpus supplied {} spending transactions; the verifier expects at least 30",
        checked
    );
}

#[test]
fn the_corpus_spends_enough_outpoints_to_be_meaningful() {
    // Corpus assertion, not a code assertion. A conflicting-spend index is
    // only interesting over a corpus with many spends; 377 of the corpus's
    // transparent inputs are `PrevOut`s and 17 are coinbases.
    let mut spent = 0;
    for fixture in blocks() {
        let block = parse(&fixture);
        for transaction in block.transactions.iter() {
            spent += transaction.spent_outpoints().count();
        }
    }
    assert!(
        spent >= 300,
        "the corpus spends {} outpoints; the verifier expects at least 300",
        spent
    );
}

#[test]
fn display_for_a_transaction_names_its_version_and_its_component_counts() {
    for fixture in blocks() {
        let block = parse(&fixture);
        for (i, transaction) in block.transactions.iter().enumerate() {
            let shown = format!("{}", transaction);
            let what = format!("{} transaction {}", fixture.name, i);
            assert!(
                shown.starts_with("Transaction {"),
                "{}: Display began with {:?}, expected it to open with `Transaction {{`",
                what,
                &shown[..shown.len().min(40)]
            );
            assert_fields_in_order(
                &what,
                &shown,
                &[
                    "version: ",
                    "transparent_inputs: ",
                    "transparent_outputs: ",
                    "sprout_joinsplits: ",
                    "sapling_spends: ",
                    "sapling_outputs: ",
                    "orchard_actions: ",
                    "unmined_id: ",
                ],
            );
            let version = format!("version: {}", transaction.version());
            assert!(
                shown.contains(&version),
                "{}: Display does not contain {:?}",
                what,
                version
            );
            let inputs = format!("transparent_inputs: {}", transaction.inputs().len());
            assert!(
                shown.contains(&inputs),
                "{}: Display does not contain {:?}",
                what,
                inputs
            );
            let outputs = format!("transparent_outputs: {}", transaction.outputs().len());
            assert!(
                shown.contains(&outputs),
                "{}: Display does not contain {:?}",
                what,
                outputs
            );
        }
    }
}

#[test]
fn display_for_a_v1_to_v4_transaction_omits_the_network_upgrade() {
    // Only a v5 transaction carries a consensus branch id, so only a v5
    // transaction has a network upgrade to name.
    for fixture in blocks() {
        let block = parse(&fixture);
        for (i, transaction) in block.transactions.iter().enumerate() {
            let shown = format!("{}", transaction);
            assert!(
                !shown.contains("network_upgrade: "),
                "{} transaction {} is a v{} transaction, but its Display names a \
                 network upgrade: {:?}",
                fixture.name,
                i,
                transaction.version(),
                shown
            );
        }
    }
}

#[test]
fn display_for_a_v5_transaction_names_its_network_upgrade() {
    let shown = format!("{}", empty_v5());
    assert!(
        shown.starts_with("Transaction {"),
        "Display began with {:?}, expected it to open with `Transaction {{`",
        &shown[..shown.len().min(40)]
    );
    assert_fields_in_order(
        "an empty v5 transaction",
        &shown,
        &["version: 5", "network_upgrade: Nu5", "transparent_inputs: 0"],
    );
}

#[test]
fn display_for_a_coinbase_input_reports_its_height_and_data_length() {
    for fixture in blocks() {
        let block = parse(&fixture);
        let coinbase = &block.transactions[0];
        let input = &coinbase.inputs()[0];
        match input {
            Input::Coinbase { height, .. } => {
                let shown = format!("{}", input);
                assert!(
                    shown.starts_with("transparent::Input::Coinbase {"),
                    "{}: Display for a coinbase input began with {:?}",
                    fixture.name,
                    &shown[..shown.len().min(50)]
                );
                assert_fields_in_order(&fixture.name, &shown, &["height: ", "data_len: "]);
                let expected_height = format!("height: Height({})", height.0);
                assert!(
                    shown.contains(&expected_height),
                    "{}: Display for a coinbase input does not contain {:?}: {:?}",
                    fixture.name,
                    expected_height,
                    shown
                );
            }
            other => panic!(
                "{}: the first input of the coinbase transaction is not a coinbase input: {:?}",
                fixture.name, other
            ),
        }
    }
}

#[test]
fn display_for_a_prevout_input_reports_its_unlock_script_length_and_outpoint() {
    let mut checked = 0;
    for fixture in blocks() {
        let block = parse(&fixture);
        for transaction in block.transactions.iter() {
            for input in transaction.inputs().iter() {
                if let Input::PrevOut {
                    unlock_script,
                    outpoint,
                    ..
                } = input
                {
                    let shown = format!("{}", input);
                    assert!(
                        shown.starts_with("transparent::Input::PrevOut {"),
                        "{}: Display for a spending input began with {:?}",
                        fixture.name,
                        &shown[..shown.len().min(50)]
                    );
                    assert_fields_in_order(
                        &fixture.name,
                        &shown,
                        &["unlock_script_len: ", "outpoint: "],
                    );
                    let expected_len =
                        format!("unlock_script_len: {}", unlock_script.as_raw_bytes().len());
                    assert!(
                        shown.contains(&expected_len),
                        "{}: Display for a spending input does not contain {:?}: {:?}",
                        fixture.name,
                        expected_len,
                        shown
                    );
                    let expected_index = format!("index: {}", outpoint.index);
                    assert!(
                        shown.contains(&expected_index),
                        "{}: Display for a spending input does not contain {:?}: {:?}",
                        fixture.name,
                        expected_index,
                        shown
                    );
                    checked += 1;
                }
            }
        }
    }
    // Corpus assertion, not a code assertion.
    assert!(
        checked >= 300,
        "the corpus supplied {} spending inputs; the verifier expects at least 300",
        checked
    );
}
