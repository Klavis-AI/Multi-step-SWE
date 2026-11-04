//! The v5 transaction skeleton: the variant, its header, and its transparent bundle.
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! NU5 introduces transaction version 5, whose body is laid out completely
//! differently from v4: the Sapling bundle moves out from behind the JoinSplit
//! data, the anchor becomes a single per-bundle field, and an Orchard bundle
//! appears at the end. This milestone lands the *skeleton* of that format --
//! the enum variant, the ZIP-225 header word, the version group ID, and the
//! transparent bundle -- and parks everything after the transparent outputs in
//! an opaque `rest` field for a later milestone to take apart.
//!
//! What is worth pinning now is the part that is already normative and cannot
//! change later: a v5 transaction's first eight bytes are the header word
//! `5` with the `fOverwintered` bit set, followed by the v5 version group ID,
//! and its transparent inputs and outputs are written exactly as every earlier
//! version writes them. The same reader must keep rejecting the header words
//! that were never legal.

use std::io::Cursor;

use zebra_chain::block::{Block, Height};
use zebra_chain::parameters::{
    OVERWINTER_VERSION_GROUP_ID, SAPLING_VERSION_GROUP_ID, TX_V5_VERSION_GROUP_ID,
};
use zebra_chain::serialization::{SerializationError, ZcashDeserialize, ZcashSerialize};
use zebra_chain::transaction::{LockTime, Transaction};
use zebra_chain::transparent;

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

fn write<T: ZcashSerialize>(value: &T) -> Vec<u8> {
    let mut out = Vec::new();
    value
        .zcash_serialize(&mut out)
        .expect("a Vec never fails to write");
    out
}

/// A compactSize writer that shares no code with the interface under test.
fn compactsize(n: u64) -> Vec<u8> {
    if n < 0xfd {
        vec![n as u8]
    } else if n <= 0xffff {
        let mut v = vec![0xfd];
        v.extend_from_slice(&(n as u16).to_le_bytes());
        v
    } else if n <= 0xffff_ffff {
        let mut v = vec![0xfe];
        v.extend_from_slice(&(n as u32).to_le_bytes());
        v
    } else {
        let mut v = vec![0xff];
        v.extend_from_slice(&n.to_le_bytes());
        v
    }
}

/// Every transaction in the corpus, tagged with the block it came from.
fn corpus_transactions() -> Vec<(String, Transaction)> {
    let mut out = Vec::new();
    for fixture in blocks() {
        let block = parse(&fixture);
        for (index, tx) in block.transactions.iter().enumerate() {
            out.push((format!("{}[{}]", fixture.name, index), (**tx).clone()));
        }
    }
    assert!(
        out.len() > 50,
        "the corpus should hold more than fifty transactions, found {}",
        out.len()
    );
    out
}

/// Borrow a real transparent bundle so that the v5 transactions built below
/// contain inputs and outputs a peer could actually have sent. The corpus is
/// searched rather than indexed, so the choice does not depend on the order
/// transactions happen to appear in a block.
fn spending_bundle() -> (Vec<transparent::Input>, Vec<transparent::Output>) {
    for (_, tx) in corpus_transactions() {
        if !tx.contains_coinbase_input() && !tx.inputs().is_empty() && tx.outputs().len() >= 2 {
            return (tx.inputs().to_vec(), tx.outputs().to_vec());
        }
    }
    panic!("the corpus should hold a transaction that spends an outpoint into two outputs");
}

/// The transparent bundle of a coinbase transaction: exactly one input, which
/// is a `transparent::Input::Coinbase`.
fn coinbase_bundle() -> (Vec<transparent::Input>, Vec<transparent::Output>) {
    for (_, tx) in corpus_transactions() {
        if tx.is_coinbase() && !tx.outputs().is_empty() {
            return (tx.inputs().to_vec(), tx.outputs().to_vec());
        }
    }
    panic!("every block in the corpus starts with a coinbase transaction");
}

/// A v4 transaction from the corpus, in its wire form.
fn a_v4_transaction() -> Vec<u8> {
    for (_, tx) in corpus_transactions() {
        let wire = write(&tx);
        if u32::from_le_bytes([wire[0], wire[1], wire[2], wire[3]]) == 4 | (1 << 31) {
            return wire;
        }
    }
    panic!("the corpus spans Sapling, so it holds a v4 transaction");
}

fn v5(
    inputs: Vec<transparent::Input>,
    outputs: Vec<transparent::Output>,
    expiry: u32,
    rest: Vec<u8>,
) -> Transaction {
    Transaction::V5 {
        lock_time: LockTime::min_lock_time(),
        expiry_height: Height(expiry),
        inputs,
        outputs,
        rest,
    }
}

// -------------------------------------------------------------------- tests

#[test]
fn a_v5_transaction_starts_with_the_zip_225_header_and_version_group_id() {
    let (inputs, outputs) = spending_bundle();
    let wire = write(&v5(inputs, outputs, 0, Vec::new()));

    assert_eq!(
        &wire[0..4],
        &[0x05, 0x00, 0x00, 0x80][..],
        "a v5 transaction's header word is version 5 with the fOverwintered bit set, written \
         little-endian"
    );
    assert_eq!(
        u32::from_le_bytes([wire[0], wire[1], wire[2], wire[3]]),
        5 | (1 << 31),
        "the header word must be 0x80000005"
    );
    assert_eq!(
        &wire[4..8],
        &TX_V5_VERSION_GROUP_ID.to_le_bytes()[..],
        "the version group ID follows the header word, little-endian"
    );
    assert_eq!(&wire[4..8], &[0x0a, 0x27, 0xa7, 0x26][..]);
}

#[test]
fn a_v5_transaction_writes_its_fields_in_the_documented_order() {
    let (inputs, outputs) = spending_bundle();
    assert!(!inputs.is_empty() && !outputs.is_empty());
    let tx = v5(inputs.clone(), outputs.clone(), 0x0009_dc10, Vec::new());
    let wire = write(&tx);

    let mut expected = Vec::new();
    expected.extend_from_slice(&(5u32 | (1 << 31)).to_le_bytes());
    expected.extend_from_slice(&TX_V5_VERSION_GROUP_ID.to_le_bytes());
    expected.extend_from_slice(&write(&LockTime::min_lock_time()));
    expected.extend_from_slice(&0x0009_dc10u32.to_le_bytes());
    expected.extend_from_slice(&compactsize(inputs.len() as u64));
    for input in inputs.iter() {
        expected.extend_from_slice(&write(input));
    }
    expected.extend_from_slice(&compactsize(outputs.len() as u64));
    for output in outputs.iter() {
        expected.extend_from_slice(&write(output));
    }

    assert_eq!(
        wire, expected,
        "a v5 transaction is header, version group ID, lock time, expiry height, inputs, \
         outputs, and then whatever `rest` holds"
    );
    assert_eq!(
        &wire[8..12],
        &write(&LockTime::min_lock_time())[..],
        "the lock time is four bytes, immediately after the version group ID"
    );
    assert_eq!(
        &wire[12..16],
        &0x0009_dc10u32.to_le_bytes()[..],
        "the expiry height is four little-endian bytes, immediately after the lock time"
    );
}

#[test]
fn the_rest_field_is_written_verbatim_after_the_transparent_bundle() {
    // `rest` stands in for the Sapling and Orchard bundles this milestone does
    // not parse yet. It must survive untouched, in order and in full.
    let (inputs, outputs) = spending_bundle();
    let bare = write(&v5(inputs.clone(), outputs.clone(), 7, Vec::new()));

    let tail: Vec<u8> = (0u8..=255).collect();
    let with_tail = write(&v5(inputs, outputs, 7, tail.clone()));

    assert_eq!(
        with_tail.len(),
        bare.len() + tail.len(),
        "`rest` must be written with no length prefix and no padding"
    );
    assert_eq!(&with_tail[..bare.len()], &bare[..]);
    assert_eq!(&with_tail[bare.len()..], &tail[..]);
}

#[test]
fn v5_accessors_expose_the_transparent_bundle_and_the_timing_fields() {
    let (inputs, outputs) = spending_bundle();
    let tx = v5(inputs.clone(), outputs.clone(), 55_555, vec![0xde, 0xad]);

    assert_eq!(tx.inputs(), &inputs[..]);
    assert_eq!(tx.outputs(), &outputs[..]);
    assert_eq!(tx.lock_time(), LockTime::min_lock_time());
    assert_eq!(
        tx.expiry_height(),
        Some(Height(55_555)),
        "a v5 transaction always has an expiry height"
    );
    assert!(
        !tx.is_coinbase(),
        "a transaction whose single input spends an outpoint is not a coinbase"
    );
    assert!(!tx.contains_coinbase_input());
}

#[test]
fn a_v5_transaction_can_be_a_coinbase() {
    let (inputs, outputs) = coinbase_bundle();
    assert_eq!(inputs.len(), 1, "a coinbase transaction has one input");
    let tx = v5(inputs, outputs, 0, Vec::new());

    assert!(
        tx.is_coinbase(),
        "a v5 transaction with a single coinbase input is a coinbase transaction"
    );
    assert!(tx.contains_coinbase_input());
}

#[test]
fn changing_any_v5_field_changes_the_transaction_hash() {
    let (inputs, outputs) = spending_bundle();
    let base = v5(inputs.clone(), outputs.clone(), 100, vec![0x01]);

    let variants = vec![
        v5(inputs.clone(), outputs.clone(), 101, vec![0x01]),
        v5(inputs.clone(), outputs.clone(), 100, vec![0x02]),
        v5(inputs.clone(), outputs.clone(), 100, Vec::new()),
        v5(inputs.clone(), Vec::new(), 100, vec![0x01]),
        v5(Vec::new(), outputs.clone(), 100, vec![0x01]),
    ];

    for (index, other) in variants.iter().enumerate() {
        assert_ne!(
            base.hash(),
            other.hash(),
            "variant {} hashes the same as the transaction it differs from; the hash must cover \
             every serialized field",
            index
        );
    }
    assert_eq!(
        base.hash(),
        v5(inputs, outputs, 100, vec![0x01]).hash(),
        "two identical v5 transactions must hash the same"
    );
}

#[test]
fn only_v3_v4_and_v5_transactions_carry_an_expiry_height() {
    let mut seen_without = 0;
    let mut seen_with = 0;
    for (name, tx) in corpus_transactions() {
        let wire = write(&tx);
        let header = u32::from_le_bytes([wire[0], wire[1], wire[2], wire[3]]);
        let version = header & ((1 << 31) - 1);
        match version {
            1 | 2 => {
                assert_eq!(
                    tx.expiry_height(),
                    None,
                    "{} is a v{} transaction, which has no expiry height field",
                    name,
                    version
                );
                seen_without += 1;
            }
            3 | 4 => {
                assert!(
                    tx.expiry_height().is_some(),
                    "{} is a v{} transaction, so it has an expiry height",
                    name,
                    version
                );
                seen_with += 1;
            }
            other => panic!("{} has unexpected version {}", name, other),
        }
    }
    assert!(seen_without > 0 && seen_with > 0, "the corpus spans both");

    let (inputs, outputs) = spending_bundle();
    assert_eq!(v5(inputs, outputs, 9, Vec::new()).expiry_height(), Some(Height(9)));
}

#[test]
fn every_corpus_transaction_writes_the_version_group_id_for_its_version() {
    for (name, tx) in corpus_transactions() {
        let wire = write(&tx);
        let header = u32::from_le_bytes([wire[0], wire[1], wire[2], wire[3]]);
        let version = header & ((1 << 31) - 1);
        let overwintered = header >> 31 != 0;

        match version {
            1 | 2 => assert!(
                !overwintered,
                "{}: the fOverwintered bit must not be set on a v{} transaction",
                name, version
            ),
            3 => {
                assert!(overwintered, "{}: v3 sets fOverwintered", name);
                assert_eq!(
                    &wire[4..8],
                    &OVERWINTER_VERSION_GROUP_ID.to_le_bytes()[..],
                    "{}: a v3 transaction carries the Overwinter version group ID",
                    name
                );
            }
            4 => {
                assert!(overwintered, "{}: v4 sets fOverwintered", name);
                assert_eq!(
                    &wire[4..8],
                    &SAPLING_VERSION_GROUP_ID.to_le_bytes()[..],
                    "{}: a v4 transaction carries the Sapling version group ID",
                    name
                );
            }
            other => panic!("{} has unexpected version {}", name, other),
        }
    }
}

#[test]
fn a_transaction_with_the_wrong_version_group_id_is_rejected() {
    let mut wire = a_v4_transaction();

    // The group ID binds a transaction to the upgrade whose rules it was built
    // under, so a v4 transaction carrying the Overwinter ID must be refused.
    wire[4..8].copy_from_slice(&OVERWINTER_VERSION_GROUP_ID.to_le_bytes());
    match Transaction::zcash_deserialize(Cursor::new(&wire[..])) {
        Err(SerializationError::Parse(_)) => {}
        other => panic!(
            "a v4 transaction with the Overwinter version group ID must be a parse error, got \
             {:?}",
            other.map(|_| "a transaction")
        ),
    }
}

#[test]
fn transaction_headers_that_were_never_legal_are_rejected() {
    // Four bytes are enough: the reader must reject these before it looks for
    // anything else, so a truncated body cannot be the reason it fails.
    let cases: Vec<(&str, [u8; 4])> = vec![
        ("version 0", [0x00, 0x00, 0x00, 0x00]),
        ("version 1 with fOverwintered set", [0x01, 0x00, 0x00, 0x80]),
        ("version 2 with fOverwintered set", [0x02, 0x00, 0x00, 0x80]),
        ("version 3 without fOverwintered", [0x03, 0x00, 0x00, 0x00]),
        ("version 4 without fOverwintered", [0x04, 0x00, 0x00, 0x00]),
        ("version 6", [0x06, 0x00, 0x00, 0x80]),
        ("version 0x7fffffff", [0xff, 0xff, 0xff, 0xff]),
    ];

    for (name, header) in cases {
        match Transaction::zcash_deserialize(Cursor::new(&header[..])) {
            Err(SerializationError::Parse(_)) => {}
            other => panic!(
                "a transaction header for {} must be rejected as a parse error, got {:?}",
                name,
                other.map(|_| "a transaction")
            ),
        }
    }
}
