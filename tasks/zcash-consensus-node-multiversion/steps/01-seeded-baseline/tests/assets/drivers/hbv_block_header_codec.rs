//! The 1487-byte Zcash block header.
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! The header is the one fixed-size object in the protocol, and its length is
//! load-bearing: 4 + 32 + 32 + 32 + 4 + 4 + 32 = 140 bytes of fields, then the
//! Equihash solution as a compactSize-prefixed 1344-byte blob, 3 + 1344 = 1347
//! more. A header that serializes to anything but 1487 bytes cannot hash to
//! the block hash the rest of the network computes.

use std::fmt::Debug;
use std::io::Cursor;

use chrono::Duration;

use zebra_chain::block::{BlockTimeError, CountedHeader, Hash, Header, Height};
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

// ---------------------------------------------------------------- helpers

const HEADER_BYTES: usize = 1487;

fn serialize<T: ZcashSerialize>(value: &T) -> Vec<u8> {
    let mut buf = Vec::new();
    value
        .zcash_serialize(&mut buf)
        .expect("writing to a Vec is infallible");
    buf
}

fn parse_header(bytes: &[u8]) -> Header {
    Header::zcash_deserialize(Cursor::new(bytes)).expect("a real header parses")
}

/// Assert that deserialization rejected malformed data, rather than accepting
/// it or merely running out of input.
fn expect_parse_error<T: Debug>(what: &str, result: Result<T, SerializationError>) {
    match result {
        Err(SerializationError::Parse(_)) => {}
        other => panic!("{} should be a parse error, got {:?}", what, other),
    }
}

/// Assert that deserialization ran out of input, rather than rejecting it as
/// malformed. The distinction matters to a caller reading from a socket.
fn expect_io_error<T: Debug>(what: &str, result: Result<T, SerializationError>) {
    match result {
        Err(SerializationError::Io(_)) => {}
        other => panic!("{} should be an io error, got {:?}", what, other),
    }
}

/// A well-formed header whose individual fields can be mutated.
fn sample_header_bytes() -> Vec<u8> {
    blocks()[0].raw[..HEADER_BYTES].to_vec()
}

// ---------------------------------------------------------------- cases

#[test]
fn every_header_serializes_to_exactly_1487_bytes() {
    for fixture in blocks() {
        let header = parse_header(&fixture.raw);
        assert_eq!(
            serialize(&header).len(),
            HEADER_BYTES,
            "{} header is the wrong length",
            fixture.name
        );
    }
}

#[test]
fn header_round_trips_byte_for_byte() {
    for fixture in blocks() {
        let header = parse_header(&fixture.raw);
        assert_eq!(
            serialize(&header),
            fixture.raw[..HEADER_BYTES].to_vec(),
            "{} header did not re-serialize to its own bytes",
            fixture.name
        );
    }
}

#[test]
fn version_and_time_match_the_corpus() {
    for fixture in blocks() {
        let header = parse_header(&fixture.raw);
        assert_eq!(header.version, fixture.version, "{} version", fixture.name);
        assert_eq!(
            header.time.timestamp(),
            fixture.time,
            "{} time",
            fixture.name
        );
    }
}

#[test]
fn thirty_two_byte_header_fields_match_the_corpus() {
    for fixture in blocks() {
        let header = parse_header(&fixture.raw);
        // merkle_root, root_bytes and nonce are stored and hashed in the order
        // they appear on the wire; only block and transaction *hashes* are
        // displayed reversed.
        assert_eq!(
            header.merkle_root.0, fixture.merkle_root,
            "{} merkle_root",
            fixture.name
        );
        assert_eq!(
            header.root_bytes, fixture.root_bytes,
            "{} root_bytes",
            fixture.name
        );
        assert_eq!(header.nonce, fixture.nonce, "{} nonce", fixture.name);
    }
}

#[test]
fn previous_block_hash_links_the_corpus_into_chains() {
    let corpus = blocks();
    for fixture in &corpus {
        let header = parse_header(&fixture.raw);
        assert_eq!(
            header.previous_block_hash.to_string(),
            fixture.previous_block_hash,
            "{} previous_block_hash",
            fixture.name
        );
    }
    // The corpus contains consecutive blocks either side of three activation
    // heights; those really do link up.
    let mut links = 0;
    for window in corpus.windows(2) {
        let (earlier, later) = (&window[0], &window[1]);
        if earlier.network == later.network && later.height == earlier.height + 1 {
            assert_eq!(
                later.previous_block_hash, earlier.block_hash,
                "{} should name {} as its parent",
                later.name, earlier.name
            );
            links += 1;
        }
    }
    assert!(links >= 3, "the corpus should contain consecutive blocks");
}

#[test]
fn the_equihash_solution_is_a_1344_byte_length_prefixed_blob() {
    for fixture in blocks() {
        let header = parse_header(&fixture.raw);
        assert_eq!(header.solution.0.len(), 1344, "{} solution", fixture.name);
        // 140 bytes of fields, then compactSize(1344) == fd 40 05.
        assert_eq!(
            &fixture.raw[140..143],
            &[0xfd, 0x40, 0x05][..],
            "{} solution length prefix",
            fixture.name
        );
        assert_eq!(
            &header.solution.0[..],
            &fixture.raw[143..HEADER_BYTES],
            "{} solution bytes",
            fixture.name
        );
    }
}

#[test]
fn a_version_below_four_is_rejected() {
    let mut bytes = sample_header_bytes();
    for &version in &[0u32, 1, 2, 3] {
        bytes[0..4].copy_from_slice(&version.to_le_bytes());
        expect_parse_error(
            &format!("version {}", version),
            Header::zcash_deserialize(Cursor::new(&bytes[..])),
        );
    }
}

#[test]
fn a_version_with_the_high_bit_set_is_rejected() {
    // zcashd reads the version field as signed, so a header claiming a
    // negative version is a header from the future, not a header with a large
    // version. Accepting one would make this node follow a chain the rest of
    // the network will not.
    let mut bytes = sample_header_bytes();
    for &version in &[0x8000_0004u32, 0xffff_ffff] {
        bytes[0..4].copy_from_slice(&version.to_le_bytes());
        expect_parse_error(
            &format!("version {:#x}", version),
            Header::zcash_deserialize(Cursor::new(&bytes[..])),
        );
    }
}

#[test]
fn a_truncated_header_is_an_io_error() {
    let bytes = sample_header_bytes();
    for &cut in &[0usize, 4, 100, 140, 142, HEADER_BYTES - 1] {
        expect_io_error(
            &format!("a header truncated to {} bytes", cut),
            Header::zcash_deserialize(Cursor::new(&bytes[..cut])),
        );
    }
}

#[test]
fn a_wrong_solution_length_prefix_is_rejected() {
    let mut bytes = sample_header_bytes();
    // compactSize(1343) and compactSize(1345) are both *canonical* encodings
    // of the wrong size, so this is a size check and not a compactSize check.
    for &(lo, hi) in &[(0x3fu8, 0x05u8), (0x41, 0x05)] {
        bytes[141] = lo;
        bytes[142] = hi;
        expect_parse_error(
            &format!("solution length {:#04x}{:02x}", hi, lo),
            Header::zcash_deserialize(Cursor::new(&bytes[..])),
        );
    }
}

#[test]
fn counted_headers_carry_the_transaction_count() {
    // `CountedHeader` is what a peer sends in a `headers` message: the header,
    // then the number of transactions in the block as a compactSize. A block's
    // own bytes begin with exactly that pair, so every fixture doubles as a
    // counted-header vector.
    for fixture in blocks() {
        let parsed: CountedHeader = Cursor::new(&fixture.raw[..])
            .zcash_deserialize_into()
            .unwrap_or_else(|e| panic!("{} counted header: {:?}", fixture.name, e));
        assert_eq!(
            parsed.transaction_count, fixture.tx_count,
            "{} transaction count",
            fixture.name
        );
        let reserialized = serialize(&parsed);
        assert_eq!(
            reserialized.len(),
            HEADER_BYTES + compactsize_len(fixture.tx_count as u64),
            "{} counted header length",
            fixture.name
        );
        assert_eq!(
            reserialized,
            fixture.raw[..reserialized.len()].to_vec(),
            "{} counted header round trip",
            fixture.name
        );
    }
}

#[test]
fn time_is_valid_at_enforces_the_two_hour_future_limit() {
    // The rule (TEST_INTERFACE §4) is a plain comparison against the caller's
    // clock, not against anything in the block itself, so it can be checked
    // directly with real corpus headers and an arbitrary reference time --
    // no independent reimplementation needed.
    for fixture in blocks() {
        let header = parse_header(&fixture.raw);
        let height = Height(fixture.height);
        let hash = Hash::from(&header);

        // `now` comfortably ahead of the header's time: accepted.
        let comfortably_now = header.time - Duration::hours(1);
        assert!(
            header.time_is_valid_at(comfortably_now, &height, &hash).is_ok(),
            "{}: should be valid when now is only 1 hour behind the header's time",
            fixture.name
        );

        // The boundary itself -- header time is exactly 2 hours ahead of
        // `now` -- is accepted; the rule is `<=`, not `<`.
        let exactly_at_the_boundary = header.time - Duration::hours(2);
        assert!(
            header
                .time_is_valid_at(exactly_at_the_boundary, &height, &hash)
                .is_ok(),
            "{}: should be valid exactly at the 2-hour boundary",
            fixture.name
        );

        // One second further back: the header is now just over 2 hours ahead
        // of `now` and must be rejected, naming the header's own time, and
        // the height and hash the call was made with, in the error.
        let one_second_past_the_boundary = exactly_at_the_boundary - Duration::seconds(1);
        match header.time_is_valid_at(one_second_past_the_boundary, &height, &hash) {
            Err(BlockTimeError::InvalidBlockTime(bad_time, bad_height, bad_hash, _)) => {
                assert_eq!(
                    bad_time, header.time,
                    "{}: error should name the header's own time",
                    fixture.name
                );
                assert_eq!(
                    bad_height, height,
                    "{}: error should name the height it was called with",
                    fixture.name
                );
                assert_eq!(
                    bad_hash, hash,
                    "{}: error should name the hash it was called with",
                    fixture.name
                );
            }
            other => panic!(
                "{}: should be invalid just over 2 hours past now, got {:?}",
                fixture.name, other
            ),
        }
    }
}

/// The number of bytes the canonical compactSize encoding of `n` occupies.
fn compactsize_len(n: u64) -> usize {
    if n <= 0xfc {
        1
    } else if n <= 0xffff {
        3
    } else if n <= 0xffff_ffff {
        5
    } else {
        9
    }
}
