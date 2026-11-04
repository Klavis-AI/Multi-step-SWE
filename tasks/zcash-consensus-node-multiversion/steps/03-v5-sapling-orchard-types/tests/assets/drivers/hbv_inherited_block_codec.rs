//! Whole blocks, against seventeen real ones.
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! `inherited` regression guard, trimmed from step 1's fourteen-test
//! `block-codec-vectors` driver to the ten tests below: the byte-for-byte
//! round trip, hashing, coinbase height, transaction count, the coinbase
//! placement rule, the merkle root, the serialized length, the 2 MB limit
//! and truncation handling. The V5 transaction format this step adds must
//! not disturb any of these pre-existing guarantees about the blocks that
//! contain v1..v4 transactions. Four of step 1's fourteen tests (the
//! allocation-DoS check, the trailing-bytes check, `Display`, and the
//! per-era reserved-header-field check) are dropped here, not because they
//! stopped holding, but to keep this group's weight proportional to its
//! role as a regression guard rather than a second full copy of step 1's
//! baseline group.
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

use std::convert::TryInto;
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
    block.zcash_serialize(&mut out).expect("a Vec never fails to write");
    out
}

/// A compactSize reader that shares no code with the interface under test, so
/// that "the transaction count is N" is an independent claim about the bytes
/// rather than a restatement of whatever the candidate's reader did.
fn independent_compactsize(bytes: &[u8], at: usize) -> (u64, usize) {
    match bytes[at] {
        n @ 0x00..=0xfc => (u64::from(n), 1),
        0xfd => (
            u64::from(u16::from_le_bytes(bytes[at + 1..at + 3].try_into().unwrap())),
            3,
        ),
        0xfe => (
            u64::from(u32::from_le_bytes(bytes[at + 1..at + 5].try_into().unwrap())),
            5,
        ),
        _ => (
            u64::from_le_bytes(bytes[at + 1..at + 9].try_into().unwrap()),
            9,
        ),
    }
}

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
fn transaction_count_matches_the_corpus() {
    for fixture in blocks() {
        let block = parse(&fixture);
        assert_eq!(
            block.transactions.len(),
            fixture.tx_count,
            "{}: wrong number of transactions",
            fixture.name
        );
        // ... and the count the wire actually carries, read without the
        // interface under test, agrees with both.
        let (on_the_wire, _) = independent_compactsize(&fixture.raw, 1487);
        assert_eq!(
            on_the_wire as usize, fixture.tx_count,
            "{}: the corpus TSV disagrees with the corpus bytes",
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
            .unwrap_or_else(|| panic!("{}: no transactions at all", fixture.name));
        assert!(
            first.is_coinbase(),
            "{}: the first transaction is not a coinbase",
            fixture.name
        );
        for (i, tx) in block.transactions.iter().enumerate().skip(1) {
            assert!(
                !tx.is_coinbase(),
                "{}: transaction {} is a second coinbase",
                fixture.name,
                i
            );
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
            root.0, block.header.merkle_root.0,
            "{}: the merkle root of the transaction hashes is not the root in the header",
            fixture.name
        );
    }
}

#[test]
fn serialized_length_matches_the_corpus() {
    for fixture in blocks() {
        let block = parse(&fixture);
        assert_eq!(
            serialize(&block).len(),
            fixture.size,
            "{}: serialized length disagrees with the TSV",
            fixture.name
        );
    }
}

#[test]
fn the_two_megabyte_block_limit_is_enforced_from_both_sides() {
    // The limit is a consensus rule, not a buffer size, so it has to bite at
    // exactly one place: a block of 2,000,000 bytes is valid and a block of
    // 2,000,001 is not. Neither bound is reachable with a real block from the
    // corpus, so the test builds both out of one real one -- a header, a
    // compactSize count, and that many copies of a single real transaction.
    // Repeating a transaction is not a valid block, but validity is not what
    // is under test here; the reader is, and the reader has no opinion about
    // duplicates.
    let fixture = blocks()
        .into_iter()
        .find(|f| f.name == "main-0000010")
        .expect("the corpus ships main-0000010, a one-transaction block");
    assert_eq!(fixture.tx_count, 1, "main-0000010 should hold one transaction");

    let header = &fixture.raw[..1487];
    // 1487 bytes of header, then the one-byte count, then the transaction.
    let tx = &fixture.raw[1488..];

    fn build(header: &[u8], tx: &[u8], count: u16) -> Vec<u8> {
        let mut out = Vec::with_capacity(header.len() + 3 + tx.len() * count as usize);
        out.extend_from_slice(header);
        out.push(0xfd);
        out.extend_from_slice(&count.to_le_bytes());
        for _ in 0..count {
            out.extend_from_slice(tx);
        }
        out
    }

    // The largest count that still fits: 1487 header bytes, a three-byte
    // count, and as many transactions as the remaining budget allows.
    let count = ((2_000_000 - 1487 - 3) / tx.len()) as u16;
    assert!(
        count > 0xfc,
        "the count must need the three-byte compactSize form for the arithmetic above"
    );

    let within = build(header, tx, count);
    assert!(within.len() <= 2_000_000, "the test built the wrong block");
    let block = Block::zcash_deserialize(Cursor::new(&within[..]))
        .expect("a block of at most 2,000,000 bytes must parse");
    assert_eq!(block.transactions.len(), count as usize);

    let over = build(header, tx, count + 1);
    assert!(over.len() > 2_000_000, "the test built the wrong block");
    assert!(
        Block::zcash_deserialize(Cursor::new(&over[..])).is_err(),
        "a block of {} bytes parsed, but the limit is 2,000,000",
        over.len()
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
            Ok(_) => panic!(
                "{}: a block one byte short parsed successfully",
                fixture.name
            ),
        }
    }
}

