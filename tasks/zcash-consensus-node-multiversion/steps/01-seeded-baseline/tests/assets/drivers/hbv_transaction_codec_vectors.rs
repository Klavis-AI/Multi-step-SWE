//! The transaction wire format, v1 through v4, against real chain bytes.
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! A Zcash transaction has four incompatible encodings that all had to keep
//! working forever, because the chain still contains blocks written in each of
//! them. v1 is Bitcoin's: version, inputs, outputs, lock time. v2 appends a
//! Sprout JoinSplit bundle. v3 sets the `fOverwintered` bit in the top bit of
//! the version word, follows it with a version group id, and inserts an expiry
//! height after the lock time. v4 keeps all of that, adds a Sapling value
//! balance and spend/output descriptions, and -- uniquely -- puts the binding
//! signature at the very end of the transaction, *after* the JoinSplit bundle
//! that syntactically precedes it.
//!
//! The corpus exercises every one of those: 9 v1, 5 v2, 18 v3 and 40 v4
//! transactions across seventeen real blocks. Every expected value below is
//! read out of the block bytes by `scan`, a transaction-region walker written
//! from the protocol specification's encoding tables that shares no code with
//! the interface under test, so "this is a v3 transaction with the Overwinter
//! version group id and 2 inputs" is a claim about the chain rather than a
//! restatement of whatever the candidate's reader did.

use std::convert::{TryFrom, TryInto};
use std::io::Cursor;

use zebra_chain::transaction::{LockTime, Memo, Transaction};
use zebra_chain::transparent;
use zebra_chain::{
    block::Block,
    serialization::{SerializationError, ZcashDeserialize, ZcashDeserializeInto, ZcashSerialize},
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

// -------------------------------------------------------------- constants
//
// Normative values, restated here from the ZIPs and the protocol
// specification's encoding tables rather than read out of the crate.

/// ZIP-202: the version group id of an Overwinter (v3) transaction.
const OVERWINTER_VERSION_GROUP_ID: u32 = 0x03C4_8270;
/// ZIP-202: the version group id of a Sapling (v4) transaction.
const SAPLING_VERSION_GROUP_ID: u32 = 0x892F_2085;

/// Bitcoin's `LOCKTIME_THRESHOLD`: below it a lock time is a block height, at
/// or above it a lock time is a Unix timestamp.
const LOCKTIME_THRESHOLD: u32 = 500_000_000;

/// Protocol specification section 7.2, JoinSplit description encoding, summed
/// over everything except the zk proof: vpub_old 8, vpub_new 8, anchor 32,
/// two nullifiers 64, two commitments 64, ephemeralKey 32, randomSeed 32, two
/// vmacs 64, two 601-byte encCiphertexts 1202.
const JOINSPLIT_WITHOUT_PROOF: usize = 8 + 8 + 32 + 64 + 64 + 32 + 32 + 64 + 1202;
/// A BCTV14 proof, used by JoinSplits in v2 and v3 transactions.
const BCTV14_PROOF: usize = 296;
/// A Groth16 proof, used by JoinSplits in v4 transactions.
const GROTH16_PROOF: usize = 192;
/// joinSplitPubKey (32) followed by joinSplitSig (64), written once after a
/// non-empty JoinSplit bundle.
const JOINSPLIT_PUBKEY_AND_SIG: usize = 32 + 64;
/// Protocol specification section 7.3: cv 32, anchor 32, nullifier 32, rk 32,
/// zkproof 192, spendAuthSig 64.
const SAPLING_SPEND: usize = 32 + 32 + 32 + 32 + 192 + 64;
/// Protocol specification section 7.4: cv 32, cmu 32, ephemeralKey 32,
/// encCiphertext 580, outCiphertext 80, zkproof 192.
const SAPLING_OUTPUT: usize = 32 + 32 + 32 + 580 + 80 + 192;
/// bindingSig, written last in a v4 transaction that has any Sapling
/// spend or output description.
const BINDING_SIG: usize = 64;

/// The transaction region of a block starts after the 1487-byte header and
/// the compactSize transaction count.
const HEADER_BYTES: usize = 1487;

// -------------------------------------------- an independent region walker

/// Everything the wire says about one transaction, read without the interface
/// under test.
struct TxSpan {
    /// Offset of the transaction's first byte within the whole block.
    start: usize,
    /// Length of the transaction in bytes.
    len: usize,
    /// The low 31 bits of the four-byte header word.
    version: u32,
    /// The `fOverwintered` flag: the top bit of the header word.
    overwintered: bool,
    /// The four bytes that follow the header word on an overwintered
    /// transaction, and nothing at all otherwise.
    version_group_id: Option<u32>,
    input_count: u64,
    output_count: u64,
    /// Offset of the four lock time bytes within the whole block.
    lock_time_at: usize,
    lock_time: u32,
    expiry_height: Option<u32>,
    value_balance: Option<i64>,
    joinsplit_count: u64,
    spend_count: u64,
    output_description_count: u64,
}

fn le_u32(bytes: &[u8], at: usize) -> u32 {
    assert!(
        at + 4 <= bytes.len(),
        "the walker ran off the end of the block reading a u32 at {}",
        at
    );
    u32::from_le_bytes(bytes[at..at + 4].try_into().expect("four bytes"))
}

fn le_i64(bytes: &[u8], at: usize) -> i64 {
    assert!(
        at + 8 <= bytes.len(),
        "the walker ran off the end of the block reading an i64 at {}",
        at
    );
    i64::from_le_bytes(bytes[at..at + 8].try_into().expect("eight bytes"))
}

/// A compactSize reader that shares no code with the interface under test.
/// Returns the value and the width of the encoding.
fn independent_compactsize(bytes: &[u8], at: usize) -> (u64, usize) {
    assert!(at < bytes.len(), "no compactSize at offset {}", at);
    match bytes[at] {
        n @ 0x00..=0xfc => (u64::from(n), 1),
        0xfd => (
            u64::from(u16::from_le_bytes(
                bytes[at + 1..at + 3].try_into().expect("two bytes"),
            )),
            3,
        ),
        0xfe => (u64::from(le_u32(bytes, at + 1)), 5),
        _ => (
            u64::from_le_bytes(bytes[at + 1..at + 9].try_into().expect("eight bytes")),
            9,
        ),
    }
}

/// Walk the transaction region of a block, byte by byte, from the protocol
/// specification's field tables. The walk finishing exactly at the end of the
/// block is itself the proof that the walker understood every field it
/// stepped over.
fn scan(fixture: &Fixture) -> Vec<TxSpan> {
    let bytes = &fixture.raw[..];
    let (count, width) = independent_compactsize(bytes, HEADER_BYTES);
    let mut at = HEADER_BYTES + width;
    let mut spans = Vec::new();

    for index in 0..count {
        let start = at;
        let header_word = le_u32(bytes, at);
        at += 4;
        let version = header_word & 0x7fff_ffff;
        let overwintered = header_word >> 31 != 0;

        let version_group_id = if overwintered {
            let id = le_u32(bytes, at);
            at += 4;
            Some(id)
        } else {
            None
        };

        let (input_count, width) = independent_compactsize(bytes, at);
        at += width;
        for _ in 0..input_count {
            // 32-byte previous transaction hash, 4-byte output index, a
            // compactSize-prefixed script, a 4-byte sequence number. The
            // coinbase input uses the same shape with an all-zero hash.
            at += 32 + 4;
            let (script_len, width) = independent_compactsize(bytes, at);
            at += width + script_len as usize;
            at += 4;
        }

        let (output_count, width) = independent_compactsize(bytes, at);
        at += width;
        for _ in 0..output_count {
            // An 8-byte value, then a compactSize-prefixed script.
            at += 8;
            let (script_len, width) = independent_compactsize(bytes, at);
            at += width + script_len as usize;
        }

        let lock_time_at = at;
        let lock_time = le_u32(bytes, at);
        at += 4;

        let expiry_height = if version >= 3 {
            let h = le_u32(bytes, at);
            at += 4;
            Some(h)
        } else {
            None
        };

        let mut value_balance = None;
        let mut spend_count = 0u64;
        let mut output_description_count = 0u64;
        if version >= 4 {
            value_balance = Some(le_i64(bytes, at));
            at += 8;
            let (n, width) = independent_compactsize(bytes, at);
            at += width + (n as usize) * SAPLING_SPEND;
            spend_count = n;
            let (n, width) = independent_compactsize(bytes, at);
            at += width + (n as usize) * SAPLING_OUTPUT;
            output_description_count = n;
        }

        let mut joinsplit_count = 0u64;
        if version >= 2 {
            let (n, width) = independent_compactsize(bytes, at);
            at += width;
            joinsplit_count = n;
            if n > 0 {
                let proof = if version >= 4 {
                    GROTH16_PROOF
                } else {
                    BCTV14_PROOF
                };
                at += (n as usize) * (JOINSPLIT_WITHOUT_PROOF + proof);
                at += JOINSPLIT_PUBKEY_AND_SIG;
            }
        }

        // The binding signature is the last field of a v4 transaction, and it
        // is present exactly when the Sapling bundle is non-empty.
        if version >= 4 && (spend_count > 0 || output_description_count > 0) {
            at += BINDING_SIG;
        }

        assert!(
            at <= bytes.len(),
            "{}: the walker ran past the end of the block on transaction {}",
            fixture.name,
            index
        );
        spans.push(TxSpan {
            start,
            len: at - start,
            version,
            overwintered,
            version_group_id,
            input_count,
            output_count,
            lock_time_at,
            lock_time,
            expiry_height,
            value_balance,
            joinsplit_count,
            spend_count,
            output_description_count,
        });
    }

    assert_eq!(
        at,
        fixture.raw.len(),
        "{}: the independent walker finished at offset {} but the block is {} bytes; \
         the walker, not the crate, is wrong",
        fixture.name,
        at,
        fixture.raw.len()
    );
    assert_eq!(
        spans.len(),
        fixture.tx_count,
        "{}: the walker found {} transactions, the TSV records {}",
        fixture.name,
        spans.len(),
        fixture.tx_count
    );
    spans
}

// ----------------------------------------------------------------- helpers

fn parse_block(fixture: &Fixture) -> Block {
    Block::zcash_deserialize(Cursor::new(&fixture.raw[..]))
        .unwrap_or_else(|e| panic!("{} did not parse: {:?}", fixture.name, e))
}

fn fixture_named(name: &str) -> Fixture {
    blocks()
        .into_iter()
        .find(|f| f.name == name)
        .unwrap_or_else(|| panic!("the corpus ships {}", name))
}

fn slice_of<'a>(fixture: &'a Fixture, span: &TxSpan) -> &'a [u8] {
    &fixture.raw[span.start..span.start + span.len]
}

fn serialize_tx(tx: &Transaction) -> Vec<u8> {
    let mut out = Vec::new();
    tx.zcash_serialize(&mut out)
        .expect("a Vec never fails to write");
    out
}

/// The version number the crate assigned, as a plain integer.
fn crate_version(tx: &Transaction) -> u32 {
    match tx {
        Transaction::V1 { .. } => 1,
        Transaction::V2 { .. } => 2,
        Transaction::V3 { .. } => 3,
        Transaction::V4 { .. } => 4,
    }
}

fn expect_parse_error<T: std::fmt::Debug>(what: &str, result: Result<T, SerializationError>) {
    match result {
        Err(SerializationError::Parse(_)) => {}
        Err(other) => panic!("{}: expected a parse error, got {:?}", what, other),
        Ok(value) => panic!(
            "{}: expected a parse error, but it parsed as {:?}",
            what, value
        ),
    }
}

/// The number of v1, v2, v3 and v4 transactions in each corpus block. Counted
/// from the chain bytes with a Python compactSize walker while the corpus was
/// assembled, not with any Rust implementation of this interface.
fn expected_version_counts(name: &str) -> [usize; 4] {
    match name {
        "main-0000000" => [1, 0, 0, 0],
        "main-0000010" => [1, 0, 0, 0],
        "main-0000202" => [1, 0, 0, 0],
        "main-0347499" => [5, 5, 0, 0],
        "main-0347500" => [0, 0, 1, 0],
        "main-0347501" => [0, 0, 4, 0],
        "main-0419199" => [0, 0, 10, 0],
        "main-0419200" => [0, 0, 0, 1],
        "main-0419201" => [0, 0, 0, 10],
        "main-0653600" => [0, 0, 0, 1],
        "main-0903000" => [0, 0, 0, 15],
        "main-0903001" => [0, 0, 0, 8],
        "main-0982681" => [0, 0, 0, 3],
        "test-0000000" => [1, 0, 0, 0],
        "test-0207500" => [0, 0, 3, 0],
        "test-0280000" => [0, 0, 0, 1],
        "test-1028500" => [0, 0, 0, 1],
        other => panic!("no expected version counts recorded for {}", other),
    }
}

/// Which transaction versions a block at this height may contain, from
/// ZIP-200's deployment table: v1 and v2 before Overwinter, v3 from Overwinter
/// until Sapling, v4 from Sapling onwards. Restated here so that the table
/// above is checked against the protocol rather than merely transcribed.
fn versions_allowed_at(network: &str, height: u32) -> &'static [u32] {
    let (overwinter, sapling) = match network {
        "Mainnet" => (347_500, 419_200),
        "Testnet" => (207_500, 280_000),
        other => panic!("unknown network {:?}", other),
    };
    if height >= sapling {
        &[4]
    } else if height >= overwinter {
        &[3]
    } else {
        &[1, 2]
    }
}

// ------------------------------------------------------------------- tests

#[test]
fn every_transaction_reserializes_to_its_slice_of_the_block() {
    // The sharpest assertion available at this layer, made one transaction at
    // a time so a failure names the transaction rather than an offset in a
    // 47 kB blob: whatever the reader built out of bytes i..j must write i..j
    // back out. Nothing but a structure that remembers every field it read --
    // including the fields it has no use for -- can do that.
    let mut checked = 0usize;
    for fixture in blocks() {
        let block = parse_block(&fixture);
        let spans = scan(&fixture);
        assert_eq!(
            block.transactions.len(),
            spans.len(),
            "{}: the crate found {} transactions, the wire has {}",
            fixture.name,
            block.transactions.len(),
            spans.len()
        );

        for (i, tx) in block.transactions.iter().enumerate() {
            let span = &spans[i];
            let expected = slice_of(&fixture, span);
            let written = serialize_tx(tx);
            assert_eq!(
                written.len(),
                expected.len(),
                "{} transaction {} (v{}): re-serialized to {} bytes, the block gives it {}",
                fixture.name,
                i,
                span.version,
                written.len(),
                expected.len()
            );
            if written != expected {
                let at = written
                    .iter()
                    .zip(expected.iter())
                    .position(|(a, b)| a != b)
                    .expect("lengths already matched, so a difference must exist");
                panic!(
                    "{} transaction {} (v{}): re-serialized bytes differ at offset {} within \
                     the transaction (block offset {}): wrote {:#04x}, the chain has {:#04x}",
                    fixture.name,
                    i,
                    span.version,
                    at,
                    span.start + at,
                    written[at],
                    expected[at]
                );
            }
            checked += 1;
        }

        // The concatenation is also complete: the last transaction ends where
        // the block ends, with no trailer and no padding.
        let last = spans.last().expect("every block has a coinbase");
        assert_eq!(
            last.start + last.len,
            fixture.raw.len(),
            "{}: the transaction region does not reach the end of the block",
            fixture.name
        );
    }
    assert_eq!(
        checked, 72,
        "the corpus holds 72 transactions; {} were checked",
        checked
    );
}

#[test]
fn the_version_word_on_the_wire_is_the_variant_the_crate_chose() {
    // The first four bytes are a little-endian u32 whose top bit is
    // `fOverwintered` and whose low 31 bits are the version. Read that word
    // here, independently, and require the crate to have reached the same
    // conclusion -- and require the flag and the version to agree, since a
    // v1 or v2 transaction must never be overwintered and a v3 or v4 always
    // must be.
    for fixture in blocks() {
        let block = parse_block(&fixture);
        let spans = scan(&fixture);
        for (i, tx) in block.transactions.iter().enumerate() {
            let span = &spans[i];

            let word = le_u32(&fixture.raw, span.start);
            assert_eq!(
                word & 0x7fff_ffff,
                span.version,
                "{} transaction {}: the walker and the raw header word disagree",
                fixture.name,
                i
            );
            assert_eq!(
                span.overwintered,
                span.version >= 3,
                "{} transaction {}: version {} has fOverwintered = {}",
                fixture.name,
                i,
                span.version,
                span.overwintered
            );
            assert_eq!(
                crate_version(tx),
                span.version,
                "{} transaction {}: the crate parsed the header word {:#010x} as a v{} \
                 transaction, but the low 31 bits say v{}",
                fixture.name,
                i,
                word,
                crate_version(tx),
                span.version
            );
        }
    }
}

#[test]
fn each_network_upgrade_era_carries_the_transaction_versions_it_should() {
    let mut totals = [0usize; 4];
    for fixture in blocks() {
        let expected = expected_version_counts(&fixture.name);
        let allowed = versions_allowed_at(&fixture.network, fixture.height);

        // The recorded table is not arbitrary: every version it claims is a
        // version the deployment schedule permits at that height.
        for v in 0..4usize {
            if expected[v] > 0 {
                let version = (v + 1) as u32;
                assert!(
                    allowed.contains(&version),
                    "{} is at height {} on {}, where only {:?} are legal, but the corpus \
                     records {} v{} transactions in it",
                    fixture.name,
                    fixture.height,
                    fixture.network,
                    allowed,
                    expected[v],
                    version
                );
            }
        }

        let mut from_wire = [0usize; 4];
        for span in scan(&fixture).iter() {
            assert!(
                span.version >= 1 && span.version <= 4,
                "{}: a transaction claims version {}",
                fixture.name,
                span.version
            );
            from_wire[(span.version - 1) as usize] += 1;
        }
        assert_eq!(
            from_wire, expected,
            "{}: the block bytes hold {:?} v1/v2/v3/v4 transactions, the corpus records {:?}",
            fixture.name, from_wire, expected
        );

        let mut from_crate = [0usize; 4];
        for tx in parse_block(&fixture).transactions.iter() {
            from_crate[(crate_version(tx) - 1) as usize] += 1;
        }
        assert_eq!(
            from_crate, expected,
            "{}: the crate parsed {:?} v1/v2/v3/v4 transactions, the block holds {:?}",
            fixture.name, from_crate, expected
        );

        for v in 0..4usize {
            totals[v] += expected[v];
        }
    }
    assert_eq!(
        totals,
        [9usize, 5, 18, 40],
        "the corpus should exercise 9 v1, 5 v2, 18 v3 and 40 v4 transactions"
    );
}

#[test]
fn version_group_ids_are_the_normative_overwinter_and_sapling_constants() {
    // The version group id is a second, redundant discriminant: ZIP-202 fixes
    // one value per upgrade so that a reader can reject a transaction from a
    // consensus branch it does not implement before parsing any of its body.
    let mut v3 = 0usize;
    let mut v4 = 0usize;
    for fixture in blocks() {
        let spans = scan(&fixture);
        let block = parse_block(&fixture);

        for (i, span) in spans.iter().enumerate() {
            match span.version {
                1 | 2 => assert!(
                    span.version_group_id.is_none() && !span.overwintered,
                    "{} transaction {}: a v{} transaction must carry no version group id",
                    fixture.name,
                    i,
                    span.version
                ),
                3 => {
                    assert_eq!(
                        span.version_group_id,
                        Some(OVERWINTER_VERSION_GROUP_ID),
                        "{} transaction {}: wrong version group id on a v3 transaction",
                        fixture.name,
                        i
                    );
                    v3 += 1;
                }
                4 => {
                    assert_eq!(
                        span.version_group_id,
                        Some(SAPLING_VERSION_GROUP_ID),
                        "{} transaction {}: wrong version group id on a v4 transaction",
                        fixture.name,
                        i
                    );
                    v4 += 1;
                }
                other => panic!("{} transaction {}: version {}", fixture.name, i, other),
            }
        }

        // And the writer puts the same constant back, in the same place.
        for (i, tx) in block.transactions.iter().enumerate() {
            let span = &spans[i];
            let written = serialize_tx(tx);
            match span.version_group_id {
                Some(id) => {
                    assert!(
                        written.len() >= 8,
                        "{} transaction {}: too short to hold a version group id",
                        fixture.name,
                        i
                    );
                    assert_eq!(
                        le_u32(&written, 4),
                        id,
                        "{} transaction {}: the writer emitted a different version group id",
                        fixture.name,
                        i
                    );
                }
                None => assert_eq!(
                    written[4], fixture.raw[span.start + 4],
                    "{} transaction {}: the writer inserted something where a v{} \
                     transaction's input count belongs",
                    fixture.name, i, span.version
                ),
            }
        }
    }
    assert!(
        v3 > 0 && v4 > 0,
        "the corpus should hold both v3 and v4 transactions, found {} and {}",
        v3,
        v4
    );
}

#[test]
fn a_wrong_version_group_id_is_rejected() {
    // Built by lifting a real transaction out of a corpus block and patching
    // only the four version-group-id bytes, so the rest of the encoding is
    // known-good and the rejection can only be about the id.
    let cases: [(&str, u32, [u32; 5]); 2] = [
        (
            "main-0347500",
            3,
            [
                0,
                SAPLING_VERSION_GROUP_ID,
                OVERWINTER_VERSION_GROUP_ID + 1,
                OVERWINTER_VERSION_GROUP_ID - 1,
                0xffff_ffff,
            ],
        ),
        (
            "main-0419200",
            4,
            [
                0,
                OVERWINTER_VERSION_GROUP_ID,
                SAPLING_VERSION_GROUP_ID + 1,
                SAPLING_VERSION_GROUP_ID - 1,
                0xffff_ffff,
            ],
        ),
    ];

    for &(name, version, ref wrong_ids) in cases.iter() {
        let fixture = fixture_named(name);
        let spans = scan(&fixture);
        assert_eq!(spans.len(), 1, "{} should hold a single transaction", name);
        let span = &spans[0];
        assert_eq!(
            span.version, version,
            "{} should hold a v{} transaction",
            name, version
        );
        let good = slice_of(&fixture, span).to_vec();

        // The control: unpatched, these bytes are a transaction.
        let parsed = Transaction::zcash_deserialize(Cursor::new(&good[..]))
            .unwrap_or_else(|e| panic!("{}: the unpatched transaction did not parse: {:?}", name, e));
        assert_eq!(crate_version(&parsed), version);

        for &wrong in wrong_ids.iter() {
            let mut bytes = good.clone();
            bytes[4..8].copy_from_slice(&wrong.to_le_bytes());
            expect_parse_error(
                &format!(
                    "{}: a v{} transaction with version group id {:#010x}",
                    name, version, wrong
                ),
                Transaction::zcash_deserialize(Cursor::new(&bytes[..])),
            );
        }
    }
}

#[test]
fn the_shape_of_a_transaction_is_fixed_by_its_version() {
    // v1 and v2 have no version group id, no expiry height and no value
    // balance; v3 gains an expiry height; v4 gains a value balance, a Sapling
    // bundle and a trailing binding signature. Every one of those is checked
    // against the offset the independent walker stepped over.
    let mut seen = [0usize; 4];
    for fixture in blocks() {
        let block = parse_block(&fixture);
        let spans = scan(&fixture);

        for (i, tx) in block.transactions.iter().enumerate() {
            let span = &spans[i];
            let site = format!("{} transaction {}", fixture.name, i);

            match &**tx {
                Transaction::V1 { .. } => {
                    seen[0] += 1;
                    assert!(span.version_group_id.is_none(), "{}: v1 group id", site);
                    assert!(span.value_balance.is_none(), "{}: v1 value balance", site);
                    assert!(
                        tx.expiry_height().is_none(),
                        "{}: a v1 transaction has no expiry height, the crate reported {:?}",
                        site,
                        tx.expiry_height()
                    );
                }
                Transaction::V2 { joinsplit_data, .. } => {
                    seen[1] += 1;
                    assert!(span.version_group_id.is_none(), "{}: v2 group id", site);
                    assert!(span.value_balance.is_none(), "{}: v2 value balance", site);
                    assert!(
                        tx.expiry_height().is_none(),
                        "{}: a v2 transaction has no expiry height, the crate reported {:?}",
                        site,
                        tx.expiry_height()
                    );
                    let n = joinsplit_data
                        .as_ref()
                        .map(|d| d.joinsplits().count() as u64)
                        .unwrap_or(0);
                    assert_eq!(n, span.joinsplit_count, "{}: JoinSplit count", site);
                }
                Transaction::V3 {
                    expiry_height,
                    joinsplit_data,
                    ..
                } => {
                    seen[2] += 1;
                    assert!(span.value_balance.is_none(), "{}: v3 value balance", site);
                    assert_eq!(
                        Some(expiry_height.0),
                        span.expiry_height,
                        "{}: expiry height",
                        site
                    );
                    assert_eq!(
                        tx.expiry_height().map(|h| h.0),
                        span.expiry_height,
                        "{}: expiry_height() disagrees with the field",
                        site
                    );
                    let n = joinsplit_data
                        .as_ref()
                        .map(|d| d.joinsplits().count() as u64)
                        .unwrap_or(0);
                    assert_eq!(n, span.joinsplit_count, "{}: JoinSplit count", site);
                }
                Transaction::V4 {
                    expiry_height,
                    value_balance,
                    joinsplit_data,
                    shielded_data,
                    ..
                } => {
                    seen[3] += 1;
                    assert_eq!(
                        Some(expiry_height.0),
                        span.expiry_height,
                        "{}: expiry height",
                        site
                    );
                    assert_eq!(
                        Some(i64::from(*value_balance)),
                        span.value_balance,
                        "{}: value balance",
                        site
                    );
                    let n = joinsplit_data
                        .as_ref()
                        .map(|d| d.joinsplits().count() as u64)
                        .unwrap_or(0);
                    assert_eq!(n, span.joinsplit_count, "{}: JoinSplit count", site);

                    let bundle_is_empty =
                        span.spend_count == 0 && span.output_description_count == 0;
                    assert_eq!(
                        shielded_data.is_some(),
                        !bundle_is_empty,
                        "{}: the wire has {} spends and {} outputs, the crate {} a Sapling bundle",
                        site,
                        span.spend_count,
                        span.output_description_count,
                        if shielded_data.is_some() {
                            "built"
                        } else {
                            "did not build"
                        }
                    );
                    if let Some(sd) = shielded_data {
                        assert_eq!(
                            sd.spends().count() as u64,
                            span.spend_count,
                            "{}: Sapling spend count",
                            site
                        );
                        assert_eq!(
                            sd.outputs().count() as u64,
                            span.output_description_count,
                            "{}: Sapling output count",
                            site
                        );
                        // The binding signature is the last 64 bytes of the
                        // transaction, after the JoinSplit bundle that
                        // syntactically precedes it.
                        let end = span.start + span.len;
                        let on_the_wire = &fixture.raw[end - BINDING_SIG..end];
                        let held = <[u8; 64]>::from(sd.binding_sig);
                        assert_eq!(
                            &held[..],
                            on_the_wire,
                            "{}: the binding signature is not the last 64 bytes of the \
                             transaction",
                            site
                        );
                    }
                }
            }
        }
    }
    assert!(
        seen[0] > 0 && seen[1] > 0 && seen[2] > 0 && seen[3] > 0,
        "the corpus should exercise all four versions, saw {:?}",
        seen
    );
}

#[test]
fn lock_time_distinguishes_block_heights_from_timestamps() {
    // Every lock time in the corpus is below Bitcoin's LOCKTIME_THRESHOLD, so
    // the corpus alone cannot show that the timestamp form is read at all.
    // Check the corpus first, then patch the four lock time bytes of a real
    // transaction to walk both sides of the threshold.
    let mut heights = 0usize;
    for fixture in blocks() {
        let block = parse_block(&fixture);
        let spans = scan(&fixture);
        for (i, tx) in block.transactions.iter().enumerate() {
            let span = &spans[i];
            assert!(
                span.lock_time < LOCKTIME_THRESHOLD,
                "{} transaction {}: the corpus is expected to hold only height-form lock \
                 times, this one is {}",
                fixture.name,
                i,
                span.lock_time
            );
            match tx.lock_time() {
                LockTime::Height(h) => {
                    assert_eq!(
                        h.0, span.lock_time,
                        "{} transaction {}: lock time read back wrong",
                        fixture.name, i
                    );
                    heights += 1;
                }
                LockTime::Time(t) => panic!(
                    "{} transaction {}: lock time {} is below {} and must be a height, \
                     the crate read it as the timestamp {}",
                    fixture.name,
                    i,
                    span.lock_time,
                    LOCKTIME_THRESHOLD,
                    t.timestamp()
                ),
            }
        }
    }
    assert_eq!(heights, 72, "every corpus transaction has a height lock time");

    let fixture = fixture_named("main-0000010");
    let spans = scan(&fixture);
    let span = &spans[0];
    let base = slice_of(&fixture, span).to_vec();
    let at = span.lock_time_at - span.start;

    // 499_999_999 is the largest height the crate's `block::Height` admits and
    // the last value below LOCKTIME_THRESHOLD; 500_000_000 is the first
    // timestamp.
    let cases: [(u32, bool); 6] = [
        (0, true),
        (1, true),
        (419_200, true),
        (499_999_999, true),
        (500_000_000, false),
        (u32::max_value(), false),
    ];
    for &(raw, is_height) in cases.iter() {
        let mut bytes = base.clone();
        bytes[at..at + 4].copy_from_slice(&raw.to_le_bytes());
        let tx = Transaction::zcash_deserialize(Cursor::new(&bytes[..]))
            .unwrap_or_else(|e| panic!("lock time {} made the transaction unparseable: {:?}", raw, e));

        match (tx.lock_time(), is_height) {
            (LockTime::Height(h), true) => assert_eq!(
                h.0, raw,
                "lock time {} should be the height {}, got {}",
                raw, raw, h.0
            ),
            (LockTime::Time(t), false) => assert_eq!(
                t.timestamp(),
                raw as i64,
                "lock time {} should be the timestamp {}, got {}",
                raw,
                raw,
                t.timestamp()
            ),
            (got, _) => panic!(
                "lock time {} should be read as a {}, got {:?}",
                raw,
                if is_height { "height" } else { "timestamp" },
                got
            ),
        }

        assert_eq!(
            serialize_tx(&tx),
            bytes,
            "a transaction with lock time {} did not write its lock time back unchanged",
            raw
        );
    }

    // The two named bounds. Both are *timestamps*, not heights: they bracket
    // the `Time` variant only, and say nothing about the `Height` variant that
    // occupies everything below LOCKTIME_THRESHOLD. `min_lock_time()` is
    // therefore `Time(MIN_TIMESTAMP)` -- the first value that is a timestamp
    // at all -- and not the smallest representable lock time, which is
    // `Height(Height(0))` and is smaller than it.
    assert_eq!(
        LockTime::MIN_TIMESTAMP, 500_000_000,
        "the first timestamp is the LOCKTIME_THRESHOLD, this crate says {}",
        LockTime::MIN_TIMESTAMP
    );
    assert_eq!(
        LockTime::MAX_TIMESTAMP,
        u32::max_value() as i64,
        "lock times are a u32 on the wire, so the last timestamp is u32::MAX"
    );
    assert_eq!(
        LockTime::MIN_TIMESTAMP as u32,
        LOCKTIME_THRESHOLD,
        "the threshold this driver scans with and the crate's constant must agree"
    );
    match LockTime::min_lock_time() {
        LockTime::Time(t) => assert_eq!(
            t.timestamp(),
            LockTime::MIN_TIMESTAMP,
            "min_lock_time() should be Time(MIN_TIMESTAMP)"
        ),
        other => panic!(
            "min_lock_time() should be a Time, not {:?} -- the minimum *timestamp* is \
             not the minimum lock time",
            other
        ),
    }
    match LockTime::max_lock_time() {
        LockTime::Time(t) => assert_eq!(
            t.timestamp(),
            LockTime::MAX_TIMESTAMP,
            "max_lock_time() should be Time(MAX_TIMESTAMP)"
        ),
        other => panic!("max_lock_time() should be a Time, not {:?}", other),
    }

    // And they agree with what the wire produces at those two values, which is
    // the only thing that makes them useful as bounds.
    let mut lowest = base.clone();
    lowest[at..at + 4].copy_from_slice(&LOCKTIME_THRESHOLD.to_le_bytes());
    let lowest_tx = Transaction::zcash_deserialize(Cursor::new(&lowest[..]))
        .expect("a transaction locked at the first timestamp parses");
    assert_eq!(
        lowest_tx.lock_time(),
        LockTime::min_lock_time(),
        "the first timestamp on the wire should read back as min_lock_time()"
    );
    let mut highest = base.clone();
    highest[at..at + 4].copy_from_slice(&u32::max_value().to_le_bytes());
    let highest_tx = Transaction::zcash_deserialize(Cursor::new(&highest[..]))
        .expect("a transaction locked at the last timestamp parses");
    assert_eq!(
        highest_tx.lock_time(),
        LockTime::max_lock_time(),
        "the last timestamp on the wire should read back as max_lock_time()"
    );
}

#[test]
fn input_and_output_counts_and_is_coinbase_match_the_wire() {
    for fixture in blocks() {
        let block = parse_block(&fixture);
        let spans = scan(&fixture);

        for (i, tx) in block.transactions.iter().enumerate() {
            let span = &spans[i];
            let site = format!("{} transaction {}", fixture.name, i);

            assert_eq!(
                tx.inputs().len() as u64,
                span.input_count,
                "{}: the crate has {} inputs, the wire counts {}",
                site,
                tx.inputs().len(),
                span.input_count
            );
            assert_eq!(
                tx.outputs().len() as u64,
                span.output_count,
                "{}: the crate has {} outputs, the wire counts {}",
                site,
                tx.outputs().len(),
                span.output_count
            );

            // Exactly one coinbase per block, and it is the first transaction.
            let should_be_coinbase = i == 0;
            assert_eq!(
                tx.is_coinbase(),
                should_be_coinbase,
                "{}: is_coinbase() is {}",
                site,
                tx.is_coinbase()
            );
            assert_eq!(
                tx.contains_coinbase_input(),
                should_be_coinbase,
                "{}: contains_coinbase_input() is {}",
                site,
                tx.contains_coinbase_input()
            );

            if should_be_coinbase {
                assert_eq!(span.input_count, 1, "{}: a coinbase has one input", site);
                match &tx.inputs()[0] {
                    transparent::Input::Coinbase { height, .. } => assert_eq!(
                        height.0, fixture.height,
                        "{}: the coinbase input carries height {}, the block is at {}",
                        site, height.0, fixture.height
                    ),
                    other => panic!("{}: the first input is {:?}", site, other),
                }
            } else {
                for (j, input) in tx.inputs().iter().enumerate() {
                    match input {
                        transparent::Input::PrevOut { .. } => {}
                        transparent::Input::Coinbase { .. } => panic!(
                            "{}: input {} is a coinbase input outside the coinbase transaction",
                            site, j
                        ),
                    }
                }
            }
        }
    }
}

#[test]
fn a_zero_version_or_a_misplaced_overwintered_flag_is_rejected() {
    // The version word is the first thing a reader sees, and it decides the
    // shape of everything after it. Version 0 does not exist; versions 1 and 2
    // predate Overwinter and must never have the flag; versions 3 and 4 are
    // defined only with it; anything above 4 is a consensus branch this band
    // does not implement.
    let fixture = fixture_named("main-0000010");
    let spans = scan(&fixture);
    let span = &spans[0];
    assert_eq!(span.version, 1, "main-0000010 holds a v1 coinbase");
    let good = slice_of(&fixture, span).to_vec();

    // The control.
    let parsed = Transaction::zcash_deserialize(Cursor::new(&good[..]))
        .expect("the unpatched v1 coinbase parses");
    assert_eq!(crate_version(&parsed), 1);

    let rejected: [(u32, &str); 10] = [
        (0x0000_0000, "version 0"),
        (0x8000_0000, "version 0 with fOverwintered"),
        (0x8000_0001, "version 1 with fOverwintered"),
        (0x8000_0002, "version 2 with fOverwintered"),
        (0x0000_0003, "version 3 without fOverwintered"),
        (0x0000_0004, "version 4 without fOverwintered"),
        (0x0000_0005, "version 5 without fOverwintered"),
        (0x8000_0005, "version 5 with fOverwintered"),
        (0x7fff_ffff, "version 0x7fffffff"),
        (0xffff_ffff, "version 0x7fffffff with fOverwintered"),
    ];
    for &(word, what) in rejected.iter() {
        let mut bytes = good.clone();
        bytes[0..4].copy_from_slice(&word.to_le_bytes());
        expect_parse_error(
            &format!("a transaction whose header word is {:#010x} ({})", word, what),
            Transaction::zcash_deserialize(Cursor::new(&bytes[..])),
        );
    }
}

#[test]
fn a_transaction_parses_from_its_own_bytes_and_stops_at_its_own_end() {
    // Transactions arrive one per network message as well as packed into
    // blocks, so the reader has to be self-delimiting: given the transaction
    // followed by anything at all, it must consume exactly the transaction.
    for fixture in blocks() {
        let block = parse_block(&fixture);
        let spans = scan(&fixture);

        for (i, tx) in block.transactions.iter().enumerate() {
            let span = &spans[i];
            let site = format!("{} transaction {}", fixture.name, i);

            // Exactly its own bytes.
            let mut cursor = Cursor::new(slice_of(&fixture, span));
            let alone: Transaction = (&mut cursor)
                .zcash_deserialize_into()
                .unwrap_or_else(|e| panic!("{}: did not parse on its own: {:?}", site, e));
            assert_eq!(
                cursor.position() as usize,
                span.len,
                "{}: parsing alone consumed {} of {} bytes",
                site,
                cursor.position(),
                span.len
            );
            assert_eq!(
                &alone,
                &**tx,
                "{}: parsed differently on its own than inside the block",
                site
            );

            // And with the rest of the block still ahead of it.
            let mut cursor = Cursor::new(&fixture.raw[span.start..]);
            let _: Transaction = (&mut cursor)
                .zcash_deserialize_into()
                .unwrap_or_else(|e| panic!("{}: did not parse with a tail: {:?}", site, e));
            assert_eq!(
                cursor.position() as usize,
                span.len,
                "{}: parsing with {} trailing bytes available consumed {}, not {}",
                site,
                fixture.raw.len() - (span.start + span.len),
                cursor.position(),
                span.len
            );
        }
    }
}

#[test]
fn memo_pads_short_input_and_rejects_long_input() {
    // `Memo`'s payload is private and it does not implement `PartialEq`, so
    // the only thing an external caller can observe is which side of 512
    // bytes an input falls on (TEST_INTERFACE §5): at or under is accepted
    // (and zero-padded internally), over is an error, and neither case may
    // panic.
    assert!(
        Memo::try_from(&[0x11u8; 512][..]).is_ok(),
        "exactly 512 bytes must be accepted"
    );
    assert!(
        Memo::try_from(&b""[..]).is_ok(),
        "an empty memo must be accepted -- it is entirely padding"
    );
    assert!(
        Memo::try_from(&[0x11u8; 10][..]).is_ok(),
        "a short memo must be zero-padded, not rejected"
    );
    assert!(
        Memo::try_from(&[0x11u8; 511][..]).is_ok(),
        "511 bytes, one short of the limit, must be accepted"
    );
    assert!(
        Memo::try_from(&[0x11u8; 513][..]).is_err(),
        "513 bytes, one over the limit, must be rejected, not truncated"
    );
    assert!(
        Memo::try_from(&[0x11u8; 1024][..]).is_err(),
        "a memo twice the limit must also be rejected"
    );
}
