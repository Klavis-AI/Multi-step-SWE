//! `ZcashSerialize::zcash_serialized_size` and the `FakeWriter` it counts with.
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! A mempool has to know how many bytes a transaction occupies before it
//! decides whether to keep it, and it has to know that without allocating a
//! second copy of the transaction. That is what this milestone adds: a
//! `zcash_serialized_size` method on the `ZcashSerialize` trait itself, backed
//! by `FakeWriter`, a `std::io::Write` that stores nothing and only counts.
//!
//! The assertions here bite because every expected byte count comes from
//! outside the crate: the `bytes` column of `blocks.tsv`, which was produced
//! in Python from the chain bytes, and the length of the shipped hex itself.
//! A size method that agreed with `zcash_serialize_to_vec` but disagreed with
//! the chain would still fail. The per-transaction identity
//! `1487 + compactSize(tx_count) + sum(tx sizes) == block size` pins the
//! individual transaction sizes too, using a compactSize length rule
//! re-implemented in this file rather than read from the crate.

use std::io::Write;

use zebra_chain::block::Block;
use zebra_chain::serialization::{FakeWriter, ZcashDeserialize, ZcashSerialize};

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

/// The offset of the transaction count within a block: a fixed 1487-byte
/// header, and nothing between the two.
const HEADER_BYTES: usize = 1487;

/// How many bytes a canonical compactSize encoding of `n` occupies.
///
/// Re-implemented here from the wire format rather than taken from the crate,
/// so that the block-framing identity below is an independent check.
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

fn size_of<T: ZcashSerialize>(what: &str, value: &T) -> usize {
    value
        .zcash_serialized_size()
        .unwrap_or_else(|e| panic!("{}: zcash_serialized_size returned an error: {:?}", what, e))
}

// ------------------------------------------------------------------- tests

#[test]
fn every_block_reports_the_byte_count_the_corpus_records() {
    for fixture in blocks() {
        let block = parse(&fixture);
        let size = size_of(&fixture.name, &block);
        assert_eq!(
            size, fixture.size,
            "{}: zcash_serialized_size reported {} bytes, the corpus records {}",
            fixture.name, size, fixture.size
        );
        assert_eq!(
            size,
            fixture.raw.len(),
            "{}: zcash_serialized_size reported {} bytes, the shipped block is {} bytes long",
            fixture.name,
            size,
            fixture.raw.len()
        );
    }
}

#[test]
fn every_header_reports_exactly_1487_bytes() {
    for fixture in blocks() {
        let block = parse(&fixture);
        let size = size_of(&fixture.name, &block.header);
        assert_eq!(
            size, HEADER_BYTES,
            "{}: the header reported {} bytes, every Zcash block header is {}",
            fixture.name, size, HEADER_BYTES
        );
    }
}

#[test]
fn a_block_with_one_transaction_reports_the_block_size_minus_its_framing() {
    // A single-transaction block frames its transaction vector with one
    // compactSize byte (`0x01`), so the lone transaction's size is fixed by
    // the corpus alone: block bytes, minus the 1487-byte header, minus one.
    //
    // Corpus assertion, not a code assertion: the corpus must actually contain
    // blocks like this, or the test degenerates into a no-op.
    let mut checked = 0;
    for fixture in blocks() {
        if fixture.tx_count != 1 {
            continue;
        }
        let block = parse(&fixture);
        let expected = fixture.size - HEADER_BYTES - 1;
        let size = size_of(&fixture.name, &block.transactions[0]);
        assert_eq!(
            size, expected,
            "{}: its only transaction reported {} bytes; the block is {} bytes, \
             so the transaction must be {}",
            fixture.name, size, fixture.size, expected
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
fn the_transaction_sizes_and_the_block_framing_add_up_to_the_block_size() {
    // Per-transaction sizes are pinned collectively: nothing else can make the
    // sum come out at the byte count the chain recorded for the block.
    for fixture in blocks() {
        let block = parse(&fixture);
        let prefix = compactsize_len(block.transactions.len() as u64);
        let mut total = HEADER_BYTES + prefix;
        for (i, transaction) in block.transactions.iter().enumerate() {
            total += size_of(&format!("{} transaction {}", fixture.name, i), transaction);
        }
        assert_eq!(
            total, fixture.size,
            "{}: 1487 header bytes + {} compactSize bytes + the {} transaction sizes \
             add up to {}, but the block is {} bytes",
            fixture.name,
            prefix,
            block.transactions.len(),
            total,
            fixture.size
        );
    }
}

#[test]
fn a_vector_of_transactions_reports_its_compactsize_prefix_plus_its_items() {
    for fixture in blocks() {
        let block = parse(&fixture);
        let vector = size_of(&fixture.name, &block.transactions);
        let expected = fixture.size - HEADER_BYTES;
        assert_eq!(
            vector, expected,
            "{}: the transaction vector reported {} bytes; the block is {} bytes \
             and its header is {}, so the vector must be {}",
            fixture.name, vector, fixture.size, HEADER_BYTES, expected
        );
    }
}

#[test]
fn every_transaction_in_the_corpus_reports_a_nonzero_size() {
    let mut seen = 0;
    for fixture in blocks() {
        let block = parse(&fixture);
        for (i, transaction) in block.transactions.iter().enumerate() {
            let size = size_of(&format!("{} transaction {}", fixture.name, i), transaction);
            assert!(
                size > 0,
                "{} transaction {}: reported a size of zero bytes",
                fixture.name,
                i
            );
            seen += 1;
        }
    }
    // Corpus assertion: the corpus is 17 real blocks and carries far more than
    // 17 transactions, so a drop below this means the corpus changed.
    assert!(
        seen >= 17,
        "the corpus supplied {} transactions; the verifier expects at least 17",
        seen
    );
}

#[test]
fn fake_writer_accumulates_the_length_of_every_write() {
    let mut writer = FakeWriter(0);
    assert_eq!(
        writer.write(&[1u8, 2, 3]).expect("FakeWriter never fails"),
        3,
        "FakeWriter::write must report that it accepted the whole buffer"
    );
    assert_eq!(writer.0, 3, "FakeWriter counted {} bytes, expected 3", writer.0);

    writer.write_all(&[0u8; 100]).expect("FakeWriter never fails");
    assert_eq!(
        writer.0, 103,
        "FakeWriter counted {} bytes after 3 + 100, expected 103",
        writer.0
    );

    writer.write_all(&[]).expect("FakeWriter never fails");
    assert_eq!(
        writer.0, 103,
        "an empty write changed the count to {}, expected it to stay at 103",
        writer.0
    );

    writer.flush().expect("FakeWriter::flush never fails");
    assert_eq!(
        writer.0, 103,
        "flush changed the count to {}, expected it to stay at 103",
        writer.0
    );
}

#[test]
fn fake_writer_starts_from_the_count_it_is_constructed_with() {
    // The counter is the tuple struct's single public field, readable and
    // writable by a caller, so it does not have to start at zero.
    let mut writer = FakeWriter(7);
    writer.write_all(&[0u8; 5]).expect("FakeWriter never fails");
    assert_eq!(
        writer.0, 12,
        "FakeWriter(7) after 5 bytes counted {}, expected 12",
        writer.0
    );
}

#[test]
fn serializing_into_a_fake_writer_reports_the_corpus_block_size() {
    for fixture in blocks() {
        let block = parse(&fixture);
        let mut writer = FakeWriter(0);
        block
            .zcash_serialize(&mut writer)
            .expect("writing to a FakeWriter never fails");
        assert_eq!(
            writer.0, fixture.size,
            "{}: serializing into a FakeWriter counted {} bytes, the corpus records {}",
            fixture.name, writer.0, fixture.size
        );
    }
}
