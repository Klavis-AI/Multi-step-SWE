//! The transparent value layer, and the coinbase height rule.
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! `transparent::{Input, Output, OutPoint, Script}` is the part of Zcash that
//! is still Bitcoin, and almost all of it is inert: an outpoint is 32 bytes of
//! txid and a 4-byte index, a script is a compactSize-prefixed blob nobody at
//! this layer interprets, an output is an 8-byte little-endian zatoshi count
//! and a script. The one place where the parser is forced to have an opinion
//! is the coinbase input, and that opinion is consensus-critical.
//!
//! A coinbase input is recognised by its null outpoint -- 32 zero bytes and
//! the index `0xffffffff` -- and its "unlock script" is not a script at all
//! but a BIP34 push of the block height followed by whatever the miner felt
//! like writing. Zebra refuses to represent that as an uninterpreted blob: it
//! parses the height out, which means a wrong reading of the push encoding
//! turns into a wrong block height, and a wrong block height turns into a
//! wrong subsidy, a wrong upgrade activation and a wrong chain.
//!
//! So the assertions here bite in three places. The height push is derived
//! from the rule inside this file -- a minimal little-endian *signed* integer,
//! with the `OP_1`..`OP_16` short forms for heights 1 through 16 and no height
//! at all for the genesis block -- and the crate has to reproduce it on
//! serialization. The genesis blocks' 77 bytes of `Zcash0b9c...` are the one
//! sanctioned exception, and they must still report height 0 and still round
//! trip. And the 100-byte limit, the `0xffffffff` index and the position of
//! the coinbase within the block are each checked for what the reference
//! actually does with them, which is not the same thing in all three cases.

use std::convert::TryInto;
use std::fmt::Debug;
use std::io::Cursor;
use std::str::FromStr;
use std::sync::Arc;

use zebra_chain::amount::MAX_MONEY;
use zebra_chain::block::{Block, Height};
use zebra_chain::parameters::Network;
use zebra_chain::serialization::{SerializationError, ZcashDeserialize, ZcashSerialize};
use zebra_chain::transaction::{self, LockTime, Transaction};
use zebra_chain::transparent::{Address, Input, OutPoint, Output};

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

// ----------------------------------------------------------------- corpus totals
//
// These four numbers were counted in Python, by a transaction walker written
// against the Zcash transaction format and run over the corpus bytes. They are
// stated here so that "every input is a PrevOut" cannot be satisfied by an
// implementation that quietly drops inputs, and so that a shrunken corpus
// fails loudly rather than weakening the group.

/// One coinbase input per block, and never more than one.
const COINBASE_INPUTS: usize = 17;
/// Transparent inputs across all 72 transactions of the corpus that are not
/// coinbase inputs.
const PREVOUT_INPUTS: usize = 377;
/// Transparent outputs across all 72 transactions of the corpus.
const TRANSPARENT_OUTPUTS: usize = 1706;
/// Blocks that contribute at least one `PrevOut`; the other nine hold only a
/// coinbase and, in some cases, purely shielded transactions.
const BLOCKS_WITH_PREVOUTS: usize = 8;

const HEADER_BYTES: usize = 1487;
const NULL_OUTPOINT_INDEX: u32 = 0xffff_ffff;

// ----------------------------------------------------------------- helpers

fn parse(fixture: &Fixture) -> Block {
    Block::zcash_deserialize(Cursor::new(&fixture.raw[..]))
        .unwrap_or_else(|e| panic!("{} did not parse: {:?}", fixture.name, e))
}

fn serialize<T: ZcashSerialize>(value: &T) -> Vec<u8> {
    let mut out = Vec::new();
    value
        .zcash_serialize(&mut out)
        .expect("writing to a Vec is infallible");
    out
}

fn expect_parse_error<T: Debug>(what: &str, result: Result<T, SerializationError>) {
    match result {
        Err(SerializationError::Parse(_)) => {}
        other => panic!("{} should be a parse error, got {:?}", what, other),
    }
}

/// A compactSize reader that shares no code with the interface under test, so
/// that the byte offsets this driver walks to are offsets into the chain's
/// bytes rather than wherever the candidate's reader happened to stop.
fn read_compactsize(bytes: &[u8], at: usize) -> (u64, usize) {
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

/// The canonical compactSize encoding of `n`, written out here rather than
/// taken from the crate.
fn write_compactsize(n: u64) -> Vec<u8> {
    let mut out = Vec::new();
    if n <= 0xfc {
        out.push(n as u8);
    } else if n <= 0xffff {
        out.push(0xfd);
        out.extend_from_slice(&(n as u16).to_le_bytes());
    } else if n <= 0xffff_ffff {
        out.push(0xfe);
        out.extend_from_slice(&(n as u32).to_le_bytes());
    } else {
        out.push(0xff);
        out.extend_from_slice(&n.to_le_bytes());
    }
    out
}

/// The BIP34 height push, derived from the rule rather than read out of the
/// crate.
///
/// The height is pushed onto the Bitcoin script stack as a minimally encoded
/// little-endian *signed* integer, so a payload whose most significant byte
/// would reach `0x80` -- which would read as negative -- gets one more zero
/// byte. Heights 1 through 16 use the one-byte `OP_1`..`OP_16` opcodes
/// (`0x51`..`0x60`) instead of a push, and height 0 is written with no height
/// at all, because the genesis block did not encode one.
///
/// The corpus exercises the no-height form (both genesis blocks), the `OP_N`
/// form (mainnet 10), the two-byte payload (mainnet 202, `02 ca 00`) and the
/// three-byte payload (the other thirteen blocks, e.g. `03 6b 4d 05` for
/// 347499). The one- and four-byte payloads are reached synthetically below.
fn bip34_height_push(height: u32) -> Vec<u8> {
    if height == 0 {
        return Vec::new();
    }
    if height <= 16 {
        return vec![0x50 + height as u8];
    }
    let mut payload = Vec::new();
    let mut remaining = height;
    while remaining > 0 {
        payload.push((remaining & 0xff) as u8);
        remaining >>= 8;
    }
    if payload[payload.len() - 1] >= 0x80 {
        payload.push(0x00);
    }
    let mut out = Vec::new();
    out.push(payload.len() as u8);
    out.extend_from_slice(&payload);
    out
}

/// A label for which BIP34 form a height uses, so a test can assert that the
/// corpus really does span more than one of them.
fn bip34_form(height: u32) -> &'static str {
    match bip34_height_push(height).len() {
        0 => "no-height",
        1 => "op_n",
        2 => "push1",
        3 => "push2",
        4 => "push3",
        _ => "push4",
    }
}

/// The wire form of a coinbase input, assembled from the pieces the protocol
/// names: a null outpoint, then a compactSize-counted script that is the
/// height push followed by the miner's free data, then the sequence number.
fn coinbase_input_bytes(height_push: &[u8], data: &[u8], sequence: u32) -> Vec<u8> {
    let mut out = Vec::new();
    out.extend_from_slice(&[0u8; 32]);
    out.extend_from_slice(&NULL_OUTPOINT_INDEX.to_le_bytes());
    out.extend_from_slice(&write_compactsize((height_push.len() + data.len()) as u64));
    out.extend_from_slice(height_push);
    out.extend_from_slice(data);
    out.extend_from_slice(&sequence.to_le_bytes());
    out
}

/// The `[start, end)` byte range of every transaction in the block, found by
/// walking forward from the transaction count and checking each transaction's
/// bytes against the block as it goes. Every later test that talks about an
/// offset gets it from here, so no assertion below is about "some bytes
/// somewhere in the block".
fn transaction_ranges(fixture: &Fixture, block: &Block) -> Vec<(usize, usize)> {
    let (count, used) = read_compactsize(&fixture.raw, HEADER_BYTES);
    assert_eq!(
        count as usize, fixture.tx_count,
        "{}: the corpus bytes and the corpus TSV disagree about the transaction count",
        fixture.name
    );
    assert_eq!(
        block.transactions.len(),
        fixture.tx_count,
        "{}: parsed {} transactions, the block holds {}",
        fixture.name,
        block.transactions.len(),
        fixture.tx_count
    );

    let mut at = HEADER_BYTES + used;
    let mut out = Vec::new();
    for (i, tx) in block.transactions.iter().enumerate() {
        let bytes = serialize(&**tx);
        let end = at + bytes.len();
        assert!(
            end <= fixture.raw.len(),
            "{}: transaction {} re-serialized to {} bytes, which runs {} bytes past the end \
             of the block",
            fixture.name,
            i,
            bytes.len(),
            end - fixture.raw.len()
        );
        assert_eq!(
            &fixture.raw[at..end],
            &bytes[..],
            "{}: transaction {} does not re-serialize to the bytes at offset {}",
            fixture.name,
            i,
            at
        );
        out.push((at, end));
        at = end;
    }
    assert_eq!(
        at,
        fixture.raw.len(),
        "{}: the transactions account for {} of {} bytes",
        fixture.name,
        at,
        fixture.raw.len()
    );
    out
}

/// Where a transaction's input vector begins, and how many inputs the bytes
/// claim: four bytes of version header, four more for the version group id
/// when the overwintered bit is set, then the compactSize count.
fn input_vector(raw: &[u8], tx_start: usize) -> (usize, u64) {
    let header = u32::from_le_bytes(raw[tx_start..tx_start + 4].try_into().unwrap());
    let mut at = tx_start + 4;
    if header & 0x8000_0000 != 0 {
        at += 4;
    }
    let (count, used) = read_compactsize(raw, at);
    (at + used, count)
}

/// One real `PrevOut` input, for tests that need to place a non-coinbase input
/// somewhere the chain never puts one.
fn a_real_prevout(corpus: &[Fixture]) -> Input {
    for fixture in corpus {
        let block = parse(fixture);
        for tx in block.transactions.iter() {
            for input in tx.inputs().iter() {
                if let Input::PrevOut { .. } = input {
                    return input.clone();
                }
            }
        }
    }
    panic!("no corpus block contains a PrevOut input");
}

// ------------------------------------------------------------------- tests

#[test]
fn every_block_has_exactly_one_coinbase_input_reporting_its_height() {
    for fixture in blocks() {
        let block = parse(&fixture);
        let first = block
            .transactions
            .get(0)
            .unwrap_or_else(|| panic!("{}: no transactions at all", fixture.name));
        assert_eq!(
            first.inputs().len(),
            1,
            "{}: the coinbase transaction has {} inputs, a coinbase has exactly one",
            fixture.name,
            first.inputs().len()
        );
        match &first.inputs()[0] {
            Input::Coinbase { height, data, .. } => {
                assert_eq!(
                    height.0, fixture.height,
                    "{}: the coinbase input reports height {}, the chain says {}",
                    fixture.name, height.0, fixture.height
                );
                let free_data: &[u8] = data.as_ref();
                let total = bip34_height_push(fixture.height).len() + free_data.len();
                assert!(
                    total <= 100,
                    "{}: the coinbase script is {} bytes, the limit is 100",
                    fixture.name,
                    total
                );
            }
            other => panic!(
                "{}: input 0 of the first transaction is {:?}, not a coinbase",
                fixture.name, other
            ),
        }
        assert!(
            first.is_coinbase(),
            "{}: the first transaction does not report itself as a coinbase",
            fixture.name
        );
        assert_eq!(
            block.coinbase_height(),
            Some(Height(fixture.height)),
            "{}: the block does not report the height its coinbase carries",
            fixture.name
        );
    }
}

#[test]
fn every_other_input_is_a_prevout_naming_a_real_transaction_and_index() {
    let mut coinbases = 0usize;
    let mut prevouts = 0usize;
    let mut blocks_with_prevouts = 0usize;

    for fixture in blocks() {
        let block = parse(&fixture);
        let mut here = 0usize;
        for (t, tx) in block.transactions.iter().enumerate() {
            for (i, input) in tx.inputs().iter().enumerate() {
                match input {
                    Input::Coinbase { .. } => {
                        assert!(
                            t == 0 && i == 0,
                            "{}: a coinbase input appears at transaction {} input {}; \
                             the chain only ever puts one at 0/0",
                            fixture.name,
                            t,
                            i
                        );
                        coinbases += 1;
                    }
                    Input::PrevOut {
                        outpoint,
                        unlock_script,
                        ..
                    } => {
                        assert_ne!(
                            outpoint.hash.0,
                            [0u8; 32],
                            "{}: transaction {} input {} is a PrevOut spending the null \
                             transaction, which is the coinbase marker",
                            fixture.name,
                            t,
                            i
                        );
                        // An outpoint is exactly a 32-byte txid in wire order
                        // followed by a little-endian 4-byte index, and
                        // nothing else.
                        let bytes = serialize(outpoint);
                        assert_eq!(
                            bytes.len(),
                            36,
                            "{}: transaction {} input {} serialized its outpoint to {} bytes, \
                             an outpoint is 36",
                            fixture.name,
                            t,
                            i,
                            bytes.len()
                        );
                        assert_eq!(
                            &bytes[..32],
                            &outpoint.hash.0[..],
                            "{}: transaction {} input {} wrote its outpoint hash in the wrong \
                             order",
                            fixture.name,
                            t,
                            i
                        );
                        assert_eq!(
                            u32::from_le_bytes(bytes[32..36].try_into().unwrap()),
                            outpoint.index,
                            "{}: transaction {} input {} wrote index {} as {}",
                            fixture.name,
                            t,
                            i,
                            outpoint.index,
                            hex::encode(&bytes[32..36])
                        );
                        assert_ne!(
                            outpoint.index, NULL_OUTPOINT_INDEX,
                            "{}: transaction {} input {} spends output {:#x}, which is the \
                             coinbase index",
                            fixture.name, t, i, outpoint.index
                        );
                        let _ = unlock_script;
                        prevouts += 1;
                        here += 1;
                    }
                }
            }
        }
        if here > 0 {
            blocks_with_prevouts += 1;
        }
    }

    assert_eq!(
        coinbases, COINBASE_INPUTS,
        "the corpus holds {} coinbase inputs, one per block, but {} were found",
        COINBASE_INPUTS, coinbases
    );
    assert_eq!(
        prevouts, PREVOUT_INPUTS,
        "the corpus holds {} non-coinbase transparent inputs, but {} were found",
        PREVOUT_INPUTS, prevouts
    );
    assert_eq!(
        blocks_with_prevouts, BLOCKS_WITH_PREVOUTS,
        "{} of the seventeen corpus blocks spend transparent outputs, but {} did",
        BLOCKS_WITH_PREVOUTS, blocks_with_prevouts
    );
}

#[test]
fn every_input_round_trips_against_the_bytes_in_the_block() {
    for fixture in blocks() {
        let block = parse(&fixture);
        let ranges = transaction_ranges(&fixture, &block);

        for (t, tx) in block.transactions.iter().enumerate() {
            let (mut at, claimed) = input_vector(&fixture.raw, ranges[t].0);
            assert_eq!(
                claimed as usize,
                tx.inputs().len(),
                "{}: transaction {} claims {} inputs on the wire but parsed {}",
                fixture.name,
                t,
                claimed,
                tx.inputs().len()
            );

            for (i, input) in tx.inputs().iter().enumerate() {
                let bytes = serialize(input);
                let end = at + bytes.len();
                assert!(
                    end <= ranges[t].1,
                    "{}: transaction {} input {} re-serialized past the end of its transaction",
                    fixture.name,
                    t,
                    i
                );
                assert_eq!(
                    &fixture.raw[at..end],
                    &bytes[..],
                    "{}: transaction {} input {} does not re-serialize to the bytes at \
                     offset {}",
                    fixture.name,
                    t,
                    i,
                    at
                );

                // The last four bytes of an input, whichever variant it is,
                // are the sequence number, little-endian. Read it out of the
                // block rather than out of the parsed value.
                let on_the_wire =
                    u32::from_le_bytes(fixture.raw[end - 4..end].try_into().unwrap());
                let parsed = match input {
                    Input::PrevOut { sequence, .. } => *sequence,
                    Input::Coinbase { sequence, .. } => *sequence,
                };
                assert_eq!(
                    parsed, on_the_wire,
                    "{}: transaction {} input {} reports sequence {:#010x}, the block says \
                     {:#010x}",
                    fixture.name, t, i, parsed, on_the_wire
                );

                // And the same bytes read back to the same value, consuming
                // all of them and none of what follows.
                let mut cursor = Cursor::new(&fixture.raw[at..end]);
                let again = Input::zcash_deserialize(&mut cursor).unwrap_or_else(|e| {
                    panic!(
                        "{}: transaction {} input {} did not re-parse: {:?}",
                        fixture.name, t, i, e
                    )
                });
                assert_eq!(
                    cursor.position() as usize,
                    bytes.len(),
                    "{}: transaction {} input {} left {} bytes unread",
                    fixture.name,
                    t,
                    i,
                    bytes.len() - cursor.position() as usize
                );
                assert!(
                    again == *input,
                    "{}: transaction {} input {} re-parsed to a different value: {:?} vs {:?}",
                    fixture.name,
                    t,
                    i,
                    again,
                    input
                );

                at = end;
            }
        }
    }
}

#[test]
fn every_output_round_trips_and_carries_a_little_endian_zatoshi_value() {
    let mut outputs = 0usize;

    for fixture in blocks() {
        let block = parse(&fixture);
        let ranges = transaction_ranges(&fixture, &block);

        for (t, tx) in block.transactions.iter().enumerate() {
            // Walk past the inputs to reach the output vector's count.
            let (mut at, _) = input_vector(&fixture.raw, ranges[t].0);
            for input in tx.inputs().iter() {
                at += serialize(input).len();
            }
            let (claimed, used) = read_compactsize(&fixture.raw, at);
            assert_eq!(
                claimed as usize,
                tx.outputs().len(),
                "{}: transaction {} claims {} outputs on the wire but parsed {}",
                fixture.name,
                t,
                claimed,
                tx.outputs().len()
            );
            at += used;

            for (o, output) in tx.outputs().iter().enumerate() {
                let bytes = serialize(output);
                let end = at + bytes.len();
                assert_eq!(
                    &fixture.raw[at..end],
                    &bytes[..],
                    "{}: transaction {} output {} does not re-serialize to the bytes at \
                     offset {}",
                    fixture.name,
                    t,
                    o,
                    at
                );

                // An output is an 8-byte little-endian zatoshi count followed
                // by a compactSize-prefixed lock script.
                let on_the_wire =
                    i64::from_le_bytes(fixture.raw[at..at + 8].try_into().unwrap());
                assert_eq!(
                    &output.value.to_bytes()[..],
                    &fixture.raw[at..at + 8],
                    "{}: transaction {} output {} reports a value of {:?}, the block carries \
                     {} zatoshi",
                    fixture.name,
                    t,
                    o,
                    output.value,
                    on_the_wire
                );
                assert!(
                    on_the_wire >= 0 && on_the_wire <= MAX_MONEY,
                    "{}: transaction {} output {} carries {} zatoshi, outside 0..={}",
                    fixture.name,
                    t,
                    o,
                    on_the_wire,
                    MAX_MONEY
                );
                let (script_len, script_used) = read_compactsize(&fixture.raw, at + 8);
                assert_eq!(
                    output.lock_script.0.len() as u64,
                    script_len,
                    "{}: transaction {} output {} has a {}-byte lock script, the block says {}",
                    fixture.name,
                    t,
                    o,
                    output.lock_script.0.len(),
                    script_len
                );
                assert_eq!(
                    bytes.len(),
                    8 + script_used + script_len as usize,
                    "{}: transaction {} output {} serialized to {} bytes, the wire form is {}",
                    fixture.name,
                    t,
                    o,
                    bytes.len(),
                    8 + script_used + script_len as usize
                );

                let again = Output::zcash_deserialize(Cursor::new(&bytes[..])).unwrap_or_else(
                    |e| {
                        panic!(
                            "{}: transaction {} output {} did not re-parse: {:?}",
                            fixture.name, t, o, e
                        )
                    },
                );
                assert_eq!(
                    again, *output,
                    "{}: transaction {} output {} re-parsed to a different value",
                    fixture.name, t, o
                );

                at = end;
                outputs += 1;
            }
        }
    }

    assert_eq!(
        outputs, TRANSPARENT_OUTPUTS,
        "the corpus holds {} transparent outputs, but {} were found",
        TRANSPARENT_OUTPUTS, outputs
    );
}

#[test]
fn the_coinbase_height_is_written_as_the_bip34_push_the_rule_specifies() {
    // Half of this test is the seventeen real coinbases: the whole input,
    // reassembled from the rule, must be the bytes the miner wrote. The other
    // half is the two BIP34 forms the corpus does not happen to contain, built
    // from the rule and pushed back through the reader.
    let mut forms: Vec<&'static str> = Vec::new();

    for fixture in blocks() {
        let block = parse(&fixture);
        let input = &block.transactions[0].inputs()[0];
        let (data, sequence) = match input {
            Input::Coinbase { data, sequence, .. } => (data, *sequence),
            other => panic!("{}: input 0/0 is {:?}, not a coinbase", fixture.name, other),
        };

        let push = bip34_height_push(fixture.height);
        let free_data: &[u8] = data.as_ref();
        let expected = coinbase_input_bytes(&push, free_data, sequence);
        let written = serialize(input);
        assert_eq!(
            hex::encode(&written),
            hex::encode(&expected),
            "{}: the coinbase input at height {} was written with the wrong height push; \
             the rule says the script starts {}",
            fixture.name,
            fixture.height,
            hex::encode(&push)
        );

        let form = bip34_form(fixture.height);
        if !forms.contains(&form) {
            forms.push(form);
        }
    }

    for form in ["no-height", "op_n", "push2", "push3"].iter() {
        assert!(
            forms.contains(form),
            "the corpus should exercise the {} height form, but only {:?} appeared",
            form,
            forms
        );
    }

    // The boundaries of every form, including the two the corpus misses.
    // 0x80 in the top payload byte would read as a negative height, so each
    // range stops one bit short of its byte width.
    let boundaries: [(u32, usize); 12] = [
        (1, 1),
        (16, 1),
        (17, 2),
        (127, 2),
        (128, 3),
        (32_767, 3),
        (32_768, 4),
        (347_499, 4),
        (8_388_607, 4),
        (8_388_608, 5),
        (100_000_000, 5),
        (499_999_999, 5),
    ];
    for &(height, push_len) in boundaries.iter() {
        let push = bip34_height_push(height);
        assert_eq!(
            push.len(),
            push_len,
            "the rule in this file encodes height {} in {} bytes, not {}",
            height,
            push.len(),
            push_len
        );
        let data = b"harbor";
        let bytes = coinbase_input_bytes(&push, &data[..], 0xfeed_face);
        let mut cursor = Cursor::new(&bytes[..]);
        let input = Input::zcash_deserialize(&mut cursor).unwrap_or_else(|e| {
            panic!(
                "a coinbase at height {} (script {}) did not parse: {:?}",
                height,
                hex::encode(&push),
                e
            )
        });
        assert_eq!(
            cursor.position() as usize,
            bytes.len(),
            "a coinbase at height {} left bytes unread",
            height
        );
        match &input {
            Input::Coinbase {
                height: parsed,
                data: parsed_data,
                sequence,
            } => {
                assert_eq!(
                    parsed.0, height,
                    "a coinbase whose script is {} was read as height {}, not {}",
                    hex::encode(&push),
                    parsed.0,
                    height
                );
                let parsed_bytes: &[u8] = parsed_data.as_ref();
                assert_eq!(
                    parsed_bytes,
                    &data[..],
                    "the free data after the height push at height {} came back as {}",
                    height,
                    hex::encode(parsed_bytes)
                );
                assert_eq!(*sequence, 0xfeed_face, "sequence at height {}", height);
            }
            other => panic!("height {} parsed as {:?}, not a coinbase", height, other),
        }
        assert_eq!(
            hex::encode(serialize(&input)),
            hex::encode(&bytes),
            "a coinbase at height {} did not round trip",
            height
        );
    }
}

#[test]
fn the_genesis_coinbase_is_the_seventy_seven_byte_special_case() {
    // Zcash's genesis blocks do not encode a BIP34 height; their coinbase
    // script is 77 bytes that happen to start with 0x04, so a naive reader
    // takes the next four bytes as a height of 520,617,983. The reference
    // special-cases exactly these 77 bytes, reports height 0, and keeps all 77
    // as free data -- which is what makes the round trip work.
    //
    // The expected bytes below are read out of the genesis blocks themselves:
    // 1487 header bytes, a one-byte transaction count, a version-1 transaction
    // header, a one-byte input count, the null outpoint, and the script's
    // compactSize length.
    const MESSAGE: &str = "Zcash0b9c4eef8b7cc417ee5001e3500984b6fea35683a7cac141a043c42064835d34";

    let mut seen = 0;
    for fixture in blocks() {
        if fixture.height != 0 {
            continue;
        }
        seen += 1;

        assert_eq!(
            &fixture.raw[HEADER_BYTES..HEADER_BYTES + 6],
            &[0x01, 0x01, 0x00, 0x00, 0x00, 0x01][..],
            "{}: genesis should hold one version-1 transaction with one input",
            fixture.name
        );
        let script_len_at = HEADER_BYTES + 6 + 32 + 4;
        let mut null_outpoint = vec![0u8; 32];
        null_outpoint.extend_from_slice(&NULL_OUTPOINT_INDEX.to_le_bytes());
        assert_eq!(
            &fixture.raw[HEADER_BYTES + 6..script_len_at],
            &null_outpoint[..],
            "{}: genesis coinbase should have a null outpoint",
            fixture.name
        );
        assert_eq!(
            fixture.raw[script_len_at], 77,
            "{}: the genesis coinbase script is {} bytes, the special case is 77",
            fixture.name, fixture.raw[script_len_at]
        );
        let script = &fixture.raw[script_len_at + 1..script_len_at + 1 + 77];
        assert_eq!(
            std::str::from_utf8(&script[8..]).expect("the genesis message is ascii"),
            MESSAGE,
            "{}: the genesis coinbase message is not the one on the chain",
            fixture.name
        );

        let block = parse(&fixture);
        match &block.transactions[0].inputs()[0] {
            Input::Coinbase { height, data, .. } => {
                assert_eq!(
                    height.0, 0,
                    "{}: the genesis coinbase reports height {}; 0x04 0xff 0xff 0x07 0x1f is \
                     not a height push, it is the start of the genesis message",
                    fixture.name, height.0
                );
                let free_data: &[u8] = data.as_ref();
                assert_eq!(
                    free_data.len(),
                    77,
                    "{}: the genesis coinbase kept {} of its 77 data bytes; nothing may be \
                     split off as a height",
                    fixture.name,
                    free_data.len()
                );
                assert_eq!(
                    hex::encode(free_data),
                    hex::encode(script),
                    "{}: the genesis coinbase data is not the script on the chain",
                    fixture.name
                );
            }
            other => panic!("{}: genesis input 0/0 is {:?}", fixture.name, other),
        }

        // No height push, so the whole 77 bytes come back unchanged.
        let written = serialize(&block.transactions[0].inputs()[0]);
        assert_eq!(
            hex::encode(&written),
            hex::encode(&fixture.raw[HEADER_BYTES + 6..HEADER_BYTES + 6 + 36 + 1 + 77 + 4]),
            "{}: the genesis coinbase input did not round trip",
            fixture.name
        );
        assert_eq!(
            block.coinbase_height(),
            Some(Height(0)),
            "{}: genesis does not report height 0",
            fixture.name
        );
    }
    assert_eq!(
        seen, 2,
        "the corpus should ship both genesis blocks, but {} were found",
        seen
    );
}

#[test]
fn a_coinbase_carrying_more_than_one_hundred_bytes_is_rejected() {
    // The height push and the miner's free data share one 100-byte budget, and
    // it is a consensus rule rather than a buffer size, so it has to bite at
    // exactly one place. The input below is a real one -- mainnet 347499's
    // coinbase, whose height needs a four-byte push -- with its free data
    // stretched to fill and then overflow the budget.
    let corpus = blocks();
    let fixture = corpus
        .iter()
        .find(|f| f.name == "main-0347499")
        .expect("the corpus ships main-0347499");
    let block = parse(fixture);
    let sequence = match &block.transactions[0].inputs()[0] {
        Input::Coinbase { sequence, .. } => *sequence,
        other => panic!("main-0347499 input 0/0 is {:?}", other),
    };
    let push = bip34_height_push(fixture.height);
    assert_eq!(push.len(), 4, "height 347499 needs a four-byte height push");

    let within = vec![0x5au8; 100 - push.len()];
    let bytes = coinbase_input_bytes(&push, &within, sequence);
    let input = Input::zcash_deserialize(Cursor::new(&bytes[..]))
        .expect("a coinbase script of exactly 100 bytes must parse");
    match &input {
        Input::Coinbase { height, data, .. } => {
            assert_eq!(height.0, fixture.height, "the 100-byte coinbase lost its height");
            let free_data: &[u8] = data.as_ref();
            assert_eq!(free_data.len(), within.len(), "the 100-byte coinbase lost data");
        }
        other => panic!("a 100-byte coinbase parsed as {:?}", other),
    }
    assert_eq!(
        hex::encode(serialize(&input)),
        hex::encode(&bytes),
        "a coinbase script of exactly 100 bytes did not round trip"
    );

    let over = vec![0x5au8; 101 - push.len()];
    expect_parse_error(
        "a coinbase script of 101 bytes",
        Input::zcash_deserialize(Cursor::new(
            &coinbase_input_bytes(&push, &over, sequence)[..],
        )),
    );
    // And far over, where a length-driven allocation would be visible.
    let mut absurd = Vec::new();
    absurd.extend_from_slice(&[0u8; 32]);
    absurd.extend_from_slice(&NULL_OUTPOINT_INDEX.to_le_bytes());
    absurd.extend_from_slice(&write_compactsize(0xffff_ffff_ffff_ffff));
    expect_parse_error(
        "a coinbase script claiming 18446744073709551615 bytes",
        Input::zcash_deserialize(Cursor::new(&absurd[..])),
    );
}

#[test]
fn a_null_outpoint_hash_with_any_index_but_ffffffff_is_rejected() {
    // A null outpoint hash is how a coinbase input is recognised, and the
    // index that goes with it is fixed at 0xffffffff. Any other index is a
    // transaction claiming to spend output N of the null transaction, which
    // does not exist, so the reference refuses it outright rather than
    // treating it as a spend of nothing.
    let push = bip34_height_push(347_499);
    let good = coinbase_input_bytes(&push, b"harbor", 0xffff_ffff);
    assert!(
        Input::zcash_deserialize(Cursor::new(&good[..])).is_ok(),
        "the control input, with index 0xffffffff, should parse"
    );

    for &index in [0u32, 1, 2, 0x0000_ffff, 0x7fff_ffff, 0xffff_fffe].iter() {
        let mut bytes = good.clone();
        bytes[32..36].copy_from_slice(&index.to_le_bytes());
        expect_parse_error(
            &format!("a null outpoint with index {:#010x}", index),
            Input::zcash_deserialize(Cursor::new(&bytes[..])),
        );
    }
}

#[test]
fn the_null_outpoint_round_trips_as_an_outpoint_but_reads_as_a_coinbase_input() {
    // The same 36 bytes are two different things depending on what the caller
    // asked for: `OutPoint` has no coinbase variant and will happily hold the
    // null reference a coinbase writes, while `Input` reads those same bytes as
    // the marker that the script which follows is a height push and not a
    // signature. Only the reader's type distinguishes them, so both readings
    // have to be exactly right.
    let mut wire = Vec::new();
    wire.extend_from_slice(&[0u8; 32]);
    wire.extend_from_slice(&NULL_OUTPOINT_INDEX.to_le_bytes());

    let outpoint = OutPoint::zcash_deserialize(Cursor::new(&wire[..]))
        .expect("the null outpoint is a well-formed OutPoint");
    assert_eq!(
        outpoint,
        OutPoint {
            hash: transaction::Hash([0u8; 32]),
            index: NULL_OUTPOINT_INDEX,
        },
        "the null outpoint parsed to {:?}",
        outpoint
    );
    assert_eq!(
        hex::encode(serialize(&outpoint)),
        hex::encode(&wire),
        "the null outpoint did not round trip"
    );

    // Read as an input, the same prefix means "coinbase".
    let push = bip34_height_push(903_000);
    let as_input = coinbase_input_bytes(&push, b"", 0xffff_ffff);
    assert_eq!(
        &as_input[..36],
        &wire[..],
        "a coinbase input must begin with the null outpoint"
    );
    match Input::zcash_deserialize(Cursor::new(&as_input[..])) {
        Ok(Input::Coinbase { height, .. }) => assert_eq!(
            height.0, 903_000,
            "the null-outpoint input was read as height {}",
            height.0
        ),
        other => panic!(
            "the null outpoint read as an Input gave {:?}, expected a coinbase",
            other
        ),
    }

    // Every real outpoint in the corpus round trips through the same reader.
    for fixture in blocks() {
        let block = parse(&fixture);
        for (t, tx) in block.transactions.iter().enumerate() {
            for (i, input) in tx.inputs().iter().enumerate() {
                if let Input::PrevOut { outpoint, .. } = input {
                    let bytes = serialize(outpoint);
                    let again = OutPoint::zcash_deserialize(Cursor::new(&bytes[..]))
                        .unwrap_or_else(|e| {
                            panic!(
                                "{}: outpoint {}/{} did not re-parse: {:?}",
                                fixture.name, t, i, e
                            )
                        });
                    assert_eq!(
                        again, *outpoint,
                        "{}: outpoint {}/{} re-parsed to {:?}",
                        fixture.name, t, i, again
                    );
                }
            }
        }
    }
}

#[test]
fn which_input_is_the_coinbase_is_a_property_of_the_model_not_the_codec() {
    // "The coinbase input must be the first input of the first transaction" is
    // a rule about position, and `Input`'s reader has no idea where it is: it
    // decides on the null outpoint alone. So the codec accepts a coinbase in
    // any slot, and it is the model above it that reports the position
    // faithfully -- `is_coinbase` insists on one input which is a coinbase,
    // `contains_coinbase_input` does not, and `Block::coinbase_height` reads
    // input 0 of transaction 0 and nothing else. A checkpoint verifier built on
    // a model that blurred those three would accept a block with a coinbase
    // buried in the middle of it.
    let corpus = blocks();
    let genesis = corpus
        .iter()
        .find(|f| f.name == "main-0000000")
        .expect("the corpus ships main-0000000");
    let block = parse(genesis);
    let coinbase = block.transactions[0].inputs()[0].clone();
    assert!(matches!(coinbase, Input::Coinbase { .. }));
    let prevout = a_real_prevout(&corpus);
    assert!(matches!(prevout, Input::PrevOut { .. }));

    // The codec keeps order and variant for either arrangement.
    for inputs in [
        vec![prevout.clone(), coinbase.clone()],
        vec![coinbase.clone(), prevout.clone()],
        vec![coinbase.clone(), coinbase.clone()],
    ]
    .iter()
    {
        let bytes = serialize(inputs);
        let again: Vec<Input> = Vec::zcash_deserialize(Cursor::new(&bytes[..]))
            .expect("a vector of inputs is a compactSize count and that many inputs");
        assert_eq!(
            again.len(),
            inputs.len(),
            "an input vector of {} came back with {}",
            inputs.len(),
            again.len()
        );
        assert!(
            &again == inputs,
            "the codec reordered or altered an input vector: {:?}",
            again
        );
    }

    let lock_time = LockTime::Height(Height(0));

    // A coinbase input in slot 1: the transaction contains one, but is not one.
    let misplaced = Transaction::V1 {
        inputs: vec![prevout.clone(), coinbase.clone()],
        outputs: block.transactions[0].outputs().to_vec(),
        lock_time,
    };
    assert!(
        misplaced.contains_coinbase_input(),
        "a transaction whose second input is a coinbase does contain a coinbase input"
    );
    assert!(
        !misplaced.is_coinbase(),
        "a transaction whose *second* input is a coinbase is not a coinbase transaction"
    );
    let misplaced_block = Block {
        header: block.header,
        transactions: vec![Arc::new(misplaced)],
    };
    assert_eq!(
        misplaced_block.coinbase_height(),
        None,
        "a block whose first input is a PrevOut has no coinbase height, whatever comes after it"
    );

    // A coinbase input in slot 0 with a second input after it: the height is
    // still readable, but the transaction is still not a coinbase.
    let padded = Transaction::V1 {
        inputs: vec![coinbase.clone(), prevout.clone()],
        outputs: block.transactions[0].outputs().to_vec(),
        lock_time,
    };
    assert!(
        !padded.is_coinbase(),
        "a coinbase transaction has exactly one input; this one has two"
    );
    let padded_block = Block {
        header: block.header,
        transactions: vec![Arc::new(padded)],
    };
    assert_eq!(
        padded_block.coinbase_height(),
        Some(Height(0)),
        "coinbase_height reads input 0 of transaction 0, which is genesis's coinbase"
    );

    // And a transaction with no inputs at all is neither.
    let empty = Transaction::V1 {
        inputs: Vec::new(),
        outputs: Vec::new(),
        lock_time,
    };
    assert!(!empty.is_coinbase(), "an inputless transaction is not a coinbase");
    assert!(
        !empty.contains_coinbase_input(),
        "an inputless transaction contains no coinbase input"
    );
    let empty_block = Block {
        header: block.header,
        transactions: vec![Arc::new(empty)],
    };
    assert_eq!(
        empty_block.coinbase_height(),
        None,
        "a block whose first transaction has no inputs has no coinbase height"
    );
}

#[test]
fn address_display_matches_an_independent_base58check_encoding() {
    // `bs58`, with its "check" feature, is a direct dependency of zebra-chain
    // (TEST_INTERFACE §6's own encoding is Base58Check); calling it here is an
    // independent computation, not the interface under test, so agreement is
    // evidence the interface is right rather than just self-consistent.
    let pub_key_hash = [0x11u8; 20];
    let script_hash = [0x22u8; 20];

    let cases: [(Address, [u8; 2]); 4] = [
        (
            Address::PayToPublicKeyHash {
                network: Network::Mainnet,
                pub_key_hash,
            },
            [0x1C, 0xB8],
        ),
        (
            Address::PayToScriptHash {
                network: Network::Mainnet,
                script_hash,
            },
            [0x1C, 0xBD],
        ),
        (
            Address::PayToPublicKeyHash {
                network: Network::Testnet,
                pub_key_hash,
            },
            [0x1D, 0x25],
        ),
        (
            Address::PayToScriptHash {
                network: Network::Testnet,
                script_hash,
            },
            [0x1C, 0xBA],
        ),
    ];

    for (address, prefix) in cases.iter() {
        let hash = match address {
            Address::PayToPublicKeyHash { pub_key_hash, .. } => *pub_key_hash,
            Address::PayToScriptHash { script_hash, .. } => *script_hash,
        };
        let mut payload = prefix.to_vec();
        payload.extend_from_slice(&hash);
        let expected = bs58::encode(payload).with_check().into_string();

        let shown = address.to_string();
        assert_eq!(
            shown, expected,
            "Display did not match an independent Base58Check encoding of prefix {:?} + hash {:?}",
            prefix, hash
        );

        let parsed = Address::from_str(&shown)
            .unwrap_or_else(|e| panic!("{} did not parse back: {:?}", shown, e));
        assert_eq!(
            parsed.to_string(),
            shown,
            "Address did not round trip through Display/FromStr"
        );
    }
}

#[test]
fn address_wire_format_is_the_prefix_and_hash_with_no_checksum() {
    let address = Address::PayToPublicKeyHash {
        network: Network::Mainnet,
        pub_key_hash: [0x33u8; 20],
    };

    let mut buf = Vec::new();
    address
        .zcash_serialize(&mut buf)
        .expect("writing to a Vec is infallible");
    assert_eq!(
        buf.len(),
        22,
        "the wire format is a 2-byte prefix and a 20-byte hash, no checksum"
    );
    assert_eq!(&buf[0..2], &[0x1C, 0xB8][..], "mainnet P2PKH prefix");
    assert_eq!(&buf[2..22], &[0x33u8; 20][..], "payload hash");

    let back = Address::zcash_deserialize(Cursor::new(&buf[..]))
        .expect("a well-formed address parses");
    assert_eq!(
        back.to_string(),
        address.to_string(),
        "the address did not round trip through ZcashSerialize/ZcashDeserialize"
    );
}
