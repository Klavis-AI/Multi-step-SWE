//! Block codec regression — step 13 (inherited from step 1).
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! A trimmed copy of step 1's block-codec driver confirming the 17-block
//! corpus still round-trips after the step 13 changes (anchor validation,
//! Groth16 parameters surface, expiry height constant).

use std::convert::TryInto;
use std::io::Cursor;

use zebra_chain::block::merkle;
use zebra_chain::block::Block;
use zebra_chain::parameters::Network;
use zebra_chain::serialization::{
    ZcashDeserialize, ZcashSerialize,
};

use std::collections::BTreeMap;
use std::path::PathBuf;

#[allow(dead_code)]
pub struct Fixture {
    pub name: String,
    pub network: String,
    pub height: u32,
    pub size: usize,
    pub block_hash: String,
    pub tx_count: usize,
    pub merkle_root: [u8; 32],
    pub root_bytes: [u8; 32],
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
    assert_eq!(bytes.len(), 32);
    let mut out = [0u8; 32];
    out.copy_from_slice(&bytes);
    out
}

fn blocks() -> Vec<Fixture> {
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
        let row: BTreeMap<&str, &str> = header.iter().cloned().zip(cells).collect();

        let name = cell(&row, "name").to_string();
        let hex_text = std::fs::read_to_string(dir.join("blocks").join(format!("{}.hex", name)))
            .unwrap_or_else(|e| panic!("cannot read block {}: {}", name, e));
        let raw = hex::decode(hex_text.trim()).expect("block fixture is hex");

        out.push(Fixture {
            raw,
            network: cell(&row, "network").to_string(),
            height: cell(&row, "height").parse().expect("height"),
            size: cell(&row, "bytes").parse().expect("bytes"),
            block_hash: cell(&row, "block_hash").to_string(),
            merkle_root: thirty_two(cell(&row, "merkle_root_hex")),
            root_bytes: thirty_two(cell(&row, "root_bytes_hex")),
            tx_count: cell(&row, "tx_count").parse().expect("tx_count"),
            name,
        });
    }
    assert!(out.len() >= 17, "corpus has {} blocks; expected at least 17", out.len());
    out
}

fn network_of(f: &Fixture) -> Network {
    match f.network.as_str() {
        "Mainnet" => Network::Mainnet,
        "Testnet" => Network::Testnet,
        other => panic!("unknown network {:?}", other),
    }
}

fn parse(f: &Fixture) -> Block {
    Block::zcash_deserialize(Cursor::new(&f.raw[..]))
        .unwrap_or_else(|e| panic!("{} did not parse: {:?}", f.name, e))
}

fn serialize(block: &Block) -> Vec<u8> {
    let mut out = Vec::new();
    block.zcash_serialize(&mut out).expect("vec never fails");
    out
}

fn independent_compactsize(bytes: &[u8], at: usize) -> (u64, usize) {
    match bytes[at] {
        n @ 0x00..=0xfc => (u64::from(n), 1),
        0xfd => (u64::from(u16::from_le_bytes(bytes[at+1..at+3].try_into().unwrap())), 3),
        0xfe => (u64::from(u32::from_le_bytes(bytes[at+1..at+5].try_into().unwrap())), 5),
        _ => (u64::from_le_bytes(bytes[at+1..at+9].try_into().unwrap()), 9),
    }
}

// ------------------------------------------------------------------- tests

#[test]
fn every_block_round_trips_byte_for_byte() {
    for f in blocks() {
        let block = parse(&f);
        let again = serialize(&block);
        assert_eq!(again, f.raw,
            "{}: re-serialized bytes differ; lengths {}/{}", f.name, again.len(), f.raw.len());
    }
}

#[test]
fn block_hash_matches_corpus() {
    for f in blocks() {
        let block = parse(&f);
        assert_eq!(block.hash().to_string(), f.block_hash, "{}: hash mismatch", f.name);
    }
}

#[test]
fn transaction_count_matches_corpus() {
    for f in blocks() {
        let block = parse(&f);
        assert_eq!(block.transactions.len(), f.tx_count, "{}: tx count mismatch", f.name);
        let (on_wire, _) = independent_compactsize(&f.raw, 1487);
        assert_eq!(on_wire as usize, f.tx_count, "{}: wire count vs TSV mismatch", f.name);
    }
}

#[test]
fn serialized_length_matches_corpus() {
    for f in blocks() {
        let block = parse(&f);
        assert_eq!(serialize(&block).len(), f.size, "{}: length mismatch", f.name);
    }
}

#[test]
fn merkle_root_matches_transaction_hashes() {
    for f in blocks() {
        let block = parse(&f);
        let root: merkle::Root = block.transactions.iter().map(|tx| tx.hash()).collect();
        assert_eq!(root.0, block.header.merkle_root.0,
            "{}: merkle root mismatch", f.name);
    }
}

#[test]
fn coinbase_height_matches_corpus() {
    for f in blocks() {
        let block = parse(&f);
        let h = block.coinbase_height()
            .unwrap_or_else(|| panic!("{}: no coinbase height", f.name));
        assert_eq!(h.0, f.height, "{}: coinbase height mismatch", f.name);
    }
}

#[test]
fn first_tx_is_coinbase_rest_are_not() {
    for f in blocks() {
        let block = parse(&f);
        let first = block.transactions.get(0)
            .unwrap_or_else(|| panic!("{}: no transactions", f.name));
        assert!(first.has_valid_coinbase_transaction_inputs(), "{}: first tx not coinbase", f.name);
        for (i, tx) in block.transactions.iter().enumerate().skip(1) {
            assert!(!tx.has_any_coinbase_inputs(), "{}: tx {} is a second coinbase", f.name, i);
        }
    }
}

#[test]
fn two_megabyte_limit_enforced() {
    let fixture = blocks().into_iter()
        .find(|f| f.name == "main-0000010")
        .expect("corpus ships main-0000010");
    assert_eq!(fixture.tx_count, 1);
    let header = &fixture.raw[..1487];
    let tx = &fixture.raw[1488..];

    fn build(header: &[u8], tx: &[u8], count: u16) -> Vec<u8> {
        let mut out = Vec::with_capacity(header.len() + 3 + tx.len() * count as usize);
        out.extend_from_slice(header);
        out.push(0xfd);
        out.extend_from_slice(&count.to_le_bytes());
        for _ in 0..count { out.extend_from_slice(tx); }
        out
    }

    let count = ((2_000_000 - 1487 - 3) / tx.len()) as u16;
    let within = build(header, tx, count);
    assert!(within.len() <= 2_000_000);
    Block::zcash_deserialize(Cursor::new(&within[..])).expect("≤2MB block must parse");

    let over = build(header, tx, count + 1);
    assert!(over.len() > 2_000_000);
    assert!(Block::zcash_deserialize(Cursor::new(&over[..])).is_err(),
        "block of {} bytes parsed, but limit is 2MB", over.len());
}
