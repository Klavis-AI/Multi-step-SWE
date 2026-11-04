//! Regression guard: whole blocks still round-trip, against the same
//! seventeen real ones step 1 shipped.
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! This milestone changes the `Transaction` enum (a new Orchard field on
//! `V5`) and `transparent::Script` (its field becomes private, gaining
//! `new`/`as_raw_bytes`), both of which the block codec depends on for every
//! transaction inside every block. A regression here would not necessarily
//! show up in an Orchard-specific test: it could just as easily break parsing
//! of the pre-Orchard v1-v4 transactions the corpus is full of. This is a
//! trimmed copy of step 1's `block-codec-vectors` group, kept to the
//! assertions that most directly guard the byte layout the previous
//! milestones established: round trip, block hash, coinbase height,
//! transaction count, the merkle root, the serialized length, and the
//! 2,000,000-byte block-size limit.

use std::convert::TryInto;
use std::io::Cursor;

use zebra_chain::block::merkle;
use zebra_chain::block::Block;
use zebra_chain::serialization::{ZcashDeserialize, ZcashSerialize};

// ---------------------------------------------------------------- fixtures
//
// The corpus is the same seventeen real blocks used at step 1, shipped again
// under this step's own `fixtures/` directory (byte-identical copies). Every
// expected value in the TSV was derived from the block bytes themselves, not
// from any Rust implementation of this interface.

use std::collections::BTreeMap;
use std::path::PathBuf;

#[allow(dead_code)]
pub struct Fixture {
    pub name: String,
    pub network: String,
    pub height: u32,
    pub size: usize,
    pub block_hash: String,
    pub previous_block_hash: String,
    pub merkle_root: [u8; 32],
    pub tx_count: usize,
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
            tx_count: cell(&row, "tx_count").parse().expect("tx_count"),
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
fn the_merkle_root_is_the_root_of_the_transaction_hashes() {
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
    let fixture = blocks()
        .into_iter()
        .find(|f| f.name == "main-0000010")
        .expect("the corpus ships main-0000010, a one-transaction block");
    assert_eq!(fixture.tx_count, 1, "main-0000010 should hold one transaction");

    let header = &fixture.raw[..1487];
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
