//! Whole blocks, against seventeen real ones.
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! A block is a header followed by a compactSize-counted vector of
//! transactions, and nothing else: no trailer, no padding, no length prefix of
//! its own. That makes the round trip the sharpest single assertion available
//! at this layer -- parse the 1692 bytes of mainnet genesis (the fixture file
//! is 3385 bytes because it holds them as hex text), serialize, and the bytes
//! must come back identical, which can only happen if every transaction
//! version, every script, every JoinSplit and every Sapling bundle inside them
//! parsed into a structure that remembers exactly what it read.
//!
//! The corpus is chosen so that "every transaction version" is not
//! hypothetical: it spans v1 coinbases, Sprout JoinSplits, Overwinter v3,
//! Sapling v4 with shielded spends and outputs, and the four different
//! readings of the header's 32 reserved bytes across the Sapling, Heartwood
//! and Canopy upgrades.
//!
//! This group is the regression guard for step 10: it is carried over
//! unchanged from the predecessor milestone and asserts nothing that is new at
//! this tag. Every symbol it touches -- `Block`, `merkle::Root`,
//! `transparent::Input::Coinbase`, `ZcashSerialize`/`ZcashDeserialize` and
//! `SerializationError::Io` -- has existed since step 01, so a tree that
//! stopped at the previous milestone still passes it. Its job is to catch a
//! candidate that breaks the block codec while adding the mempool types.

use std::io::Cursor;

use zebra_chain::block::merkle;
use zebra_chain::block::Block;
use zebra_chain::serialization::{
    SerializationError, ZcashDeserialize, ZcashDeserializeInto, ZcashSerialize,
};

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

fn serialize(block: &Block) -> Vec<u8> {
    let mut out = Vec::new();
    block
        .zcash_serialize(&mut out)
        .expect("a Vec never fails to write");
    out
}

/// The offset of the transaction count within a block: a fixed 1487-byte
/// header, and nothing between the two.
const TX_COUNT_OFFSET: usize = 1487;

// ------------------------------------------------------------------- tests

#[test]
fn every_block_round_trips_byte_for_byte() {
    for fixture in blocks() {
        let block = parse(&fixture);
        let again = serialize(&block);
        assert_eq!(
            again.len(),
            fixture.raw.len(),
            "{}: re-serialized to {} bytes, corpus block is {}",
            fixture.name,
            again.len(),
            fixture.raw.len()
        );
        if again != fixture.raw {
            let at = again
                .iter()
                .zip(fixture.raw.iter())
                .position(|(a, b)| a != b)
                .expect("lengths already matched, so a difference must exist");
            panic!(
                "{}: re-serialized bytes differ from the corpus at offset {} \
                 (wrote {:#04x}, corpus has {:#04x})",
                fixture.name, at, again[at], fixture.raw[at]
            );
        }
    }
}

#[test]
fn parsing_consumes_the_whole_block_and_no_more() {
    for fixture in blocks() {
        let mut cursor = Cursor::new(&fixture.raw[..]);
        let _block: Block = (&mut cursor)
            .zcash_deserialize_into()
            .unwrap_or_else(|e| panic!("{} did not parse: {:?}", fixture.name, e));
        assert_eq!(
            cursor.position() as usize,
            fixture.raw.len(),
            "{}: parsing stopped {} bytes short of the end of the block",
            fixture.name,
            fixture.raw.len() - cursor.position() as usize
        );
    }
}

#[test]
fn block_hash_matches_the_corpus() {
    for fixture in blocks() {
        let block = parse(&fixture);
        assert_eq!(
            block.hash().to_string(),
            fixture.block_hash,
            "{}: block hash does not match the chain",
            fixture.name
        );
    }
}

#[test]
fn coinbase_height_matches_the_corpus() {
    for fixture in blocks() {
        let block = parse(&fixture);
        let height = block
            .coinbase_height()
            .unwrap_or_else(|| panic!("{}: no height in the coinbase input", fixture.name));
        assert_eq!(
            height.0, fixture.height,
            "{}: coinbase height read back wrong",
            fixture.name
        );
    }
}


#[test]
fn the_first_transaction_of_every_block_is_a_coinbase() {
    for fixture in blocks() {
        let block = parse(&fixture);
        let first = block
            .transactions
            .get(0)
            .unwrap_or_else(|| panic!("{}: no transactions", fixture.name));
        match first.inputs().get(0) {
            Some(zebra_chain::transparent::Input::Coinbase { .. }) => {}
            other => panic!(
                "{}: the first input of the first transaction is not a coinbase: {:?}",
                fixture.name, other
            ),
        }
    }
}

#[test]
fn the_merkle_root_is_the_root_of_the_transaction_hashes() {
    // This is the assertion that ties the transaction layer to the header: the
    // root in the header was fixed by miners years ago, so it can only be
    // reproduced if every transaction in the block hashes to the txid the rest
    // of the network computed for it, in order.
    for fixture in blocks() {
        let block = parse(&fixture);
        let root: merkle::Root = block.transactions.iter().map(|tx| tx.hash()).collect();
        assert_eq!(
            root.0, fixture.merkle_root,
            "{}: the recomputed merkle root is not the one in the corpus",
            fixture.name
        );
        assert_eq!(
            root.0, block.header.merkle_root.0,
            "{}: the merkle root of the transaction hashes is not the root in the header",
            fixture.name
        );
    }
}


#[test]
fn an_absurd_transaction_count_is_an_error_not_an_allocation() {
    // The count is attacker-supplied, so it must never be used as an
    // allocation size. Four billion transactions do not fit in a 2 MB block,
    // and the nine bytes below are the whole message: if this test allocates
    // rather than failing, it does not come back.
    let fixture = blocks().remove(0);
    let mut stream = fixture.raw[..TX_COUNT_OFFSET].to_vec();
    stream.extend_from_slice(&[0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff]);

    let result = Block::zcash_deserialize(Cursor::new(&stream[..]));
    assert!(
        result.is_err(),
        "a block claiming 18446744073709551615 transactions parsed successfully"
    );
}

#[test]
fn a_truncated_block_is_an_io_error() {
    for fixture in blocks() {
        let short = &fixture.raw[..fixture.raw.len() - 1];
        match Block::zcash_deserialize(Cursor::new(short)) {
            Err(SerializationError::Io(_)) => {}
            Err(other) => panic!(
                "{}: a block one byte short gave {:?}, expected an io error",
                fixture.name, other
            ),
            Ok(_) => panic!("{}: a block one byte short parsed successfully", fixture.name),
        }
    }
}
