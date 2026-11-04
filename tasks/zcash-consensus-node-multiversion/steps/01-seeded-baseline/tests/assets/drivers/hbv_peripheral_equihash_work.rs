//! Proof of work: the Equihash solution blob and the compact difficulty field.
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! This is a peripheral breadth group: step 1 owns serialization, not consensus
//! validation, but it cannot serialize a header without getting these two
//! fields right. The Equihash solution is 1344 bytes behind a `fd 40 05`
//! compactSize prefix -- 1347 of the header's 1487 bytes -- and the difficulty
//! threshold is four bytes of floating-point "compact bits" that expand to the
//! 256-bit target the block hash has to meet.
//!
//! Both are checked against the seventeen real blocks in the corpus. The
//! expanded targets and work values asserted below were computed in Python
//! from the compact bits by the ZIP/specification arithmetic (mantissa times
//! 256^(exponent-3), and 2^256 / (target + 1)), not read out of this crate.
//!
//! `CompactDifficulty`'s inner `u32` is `pub(crate)` in this band, so it is
//! never read directly here: it is checked through `Debug`, through the
//! `to_expanded`/`to_compact` round trip, and against the little-endian u32
//! that sits at offset 104 of the header on the wire.

use std::convert::{TryFrom, TryInto};
use std::io::Cursor;

use zebra_chain::block::{Block, Header};
use zebra_chain::parameters::Network;
use zebra_chain::serialization::{ZcashDeserialize, ZcashSerialize};
use zebra_chain::work::difficulty::{
    CompactDifficulty, ExpandedDifficulty, PartialCumulativeWork, Work,
    INVALID_COMPACT_DIFFICULTY,
};
use zebra_chain::work::equihash::Solution;

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
//
// Header layout, by offset:
//
//     0..4     version
//     4..36    previous block hash
//     36..68   merkle root
//     68..100  reserved / final sapling root / chain history root
//     100..104 time
//     104..108 difficulty threshold ("compact bits")
//     108..140 nonce
//     140..143 compactSize(1344) == fd 40 05
//     143..1487 equihash solution

const HEADER_BYTES: usize = 1487;
const DIFFICULTY_OFFSET: usize = 104;
const SOLUTION_OFFSET: usize = 140;
const SOLUTION_BYTES: usize = 1344;

/// `(compact bits, expanded target as big-endian hex, work)`.
///
/// Derived in Python straight from the compact bits of the corpus blocks:
/// the target is `mantissa * 256^(exponent - 3)` printed big-endian the way
/// `ExpandedDifficulty`'s `Debug` prints it, and the work is
/// `floor(2^256 / (target + 1))`, which is `GetBlockProof()` in zcashd. Neither
/// number was produced by the interface under test.
static DIFFICULTY_TABLE: &[(u32, &str, u128)] = &[
    (
        469_901_453, // 0x1c02208d
        "0000000002208d00000000000000000000000000000000000000000000000000",
        516_893_899_343,
    ),
    (
        469_913_354, // 0x1c024f0a
        "00000000024f0a00000000000000000000000000000000000000000000000000",
        476_237_518_921,
    ),
    (
        469_928_528, // 0x1c028a50
        "00000000028a5000000000000000000000000000000000000000000000000000",
        432_830_334_201,
    ),
    (
        469_931_338, // 0x1c02954a
        "0000000002954a00000000000000000000000000000000000000000000000000",
        425_645_897_796,
    ),
    (
        470_017_084, // 0x1c03e43c
        "0000000003e43c00000000000000000000000000000000000000000000000000",
        282_538_912_302,
    ),
    (
        470_021_266, // 0x1c03f492
        "0000000003f49200000000000000000000000000000000000000000000000000",
        277_980_672_784,
    ),
    (
        470_025_806, // 0x1c04064e
        "0000000004064e00000000000000000000000000000000000000000000000000",
        273_195_861_501,
    ),
    (
        470_688_960, // 0x1c0e24c0
        "000000000e24c000000000000000000000000000000000000000000000000000",
        77_739_412_196,
    ),
    (
        470_709_155, // 0x1c0e73a3
        "000000000e73a300000000000000000000000000000000000000000000000000",
        76_081_788_053,
    ),
    (
        470_743_490, // 0x1c0ef9c2
        "000000000ef9c200000000000000000000000000000000000000000000000000",
        73_420_124_712,
    ),
    (
        507_066_102, // 0x1e3936f6
        "00003936f6000000000000000000000000000000000000000000000000000000",
        293_232,
    ),
    (
        520_246_921, // 0x1f025689
        "0002568900000000000000000000000000000000000000000000000000000000",
        28_030,
    ),
    (
        520_617_983, // 0x1f07ffff -- the mainnet PoWLimit
        "0007ffff00000000000000000000000000000000000000000000000000000000",
        8_192,
    ),
    (
        521_256_024, // 0x1f11bc58
        "0011bc5800000000000000000000000000000000000000000000000000000000",
        3_695,
    ),
    (
        537_395_199, // 0x2007ffff -- the testnet PoWLimit
        "07ffff0000000000000000000000000000000000000000000000000000000000",
        32,
    ),
];

fn expected_target_and_work(bits: u32) -> (&'static str, u128) {
    DIFFICULTY_TABLE
        .iter()
        .find(|entry| entry.0 == bits)
        .map(|entry| (entry.1, entry.2))
        .unwrap_or_else(|| {
            panic!(
                "no independently computed target for compact difficulty {:#010x}; \
                 the corpus has grown a block this driver's table does not cover",
                bits
            )
        })
}

fn network_of(fixture: &Fixture) -> Network {
    match fixture.network.as_str() {
        "Mainnet" => Network::Mainnet,
        "Testnet" => Network::Testnet,
        other => panic!("blocks.tsv has an unknown network {:?}", other),
    }
}

fn parse_header(fixture: &Fixture) -> Header {
    Header::zcash_deserialize(Cursor::new(&fixture.raw[..HEADER_BYTES]))
        .unwrap_or_else(|e| panic!("{}: header did not parse: {:?}", fixture.name, e))
}

fn parse_block(fixture: &Fixture) -> Block {
    Block::zcash_deserialize(Cursor::new(&fixture.raw[..]))
        .unwrap_or_else(|e| panic!("{}: block did not parse: {:?}", fixture.name, e))
}

fn serialize<T: ZcashSerialize>(value: &T) -> Vec<u8> {
    let mut buf = Vec::new();
    value
        .zcash_serialize(&mut buf)
        .expect("writing to a Vec is infallible");
    buf
}

/// The little-endian u32 the wire actually carries at `offset`, read without
/// the interface under test.
fn wire_u32(fixture: &Fixture, offset: usize) -> u32 {
    u32::from_le_bytes(
        fixture.raw[offset..offset + 4]
            .try_into()
            .expect("four bytes"),
    )
}

fn expanded(fixture: &Fixture, compact: CompactDifficulty) -> ExpandedDifficulty {
    compact.to_expanded().unwrap_or_else(|| {
        panic!(
            "{}: difficulty threshold {:#010x} does not expand to a target, \
             but a mined block's threshold always must",
            fixture.name, fixture.difficulty_threshold
        )
    })
}

// ------------------------------------------------------------------- tests

#[test]
fn every_equihash_solution_is_1344_bytes_behind_an_fd4005_prefix() {
    // 1344 needs the three-byte compactSize form, and `fd 40 05` is its only
    // canonical spelling. Those three bytes plus the blob are 1347 of the
    // header's 1487 bytes, which is why a header is the size it is.
    assert_eq!(
        Solution::INPUT_LENGTH,
        108,
        "the equihash input is the header without the nonce or the solution: \
         {} - 32 - 1347 = 108, but INPUT_LENGTH is {}",
        HEADER_BYTES,
        Solution::INPUT_LENGTH
    );
    assert_eq!(
        Solution::INPUT_LENGTH + 32 + 3 + SOLUTION_BYTES,
        HEADER_BYTES,
        "the header layout does not add up"
    );

    for fixture in blocks() {
        let prefix = &fixture.raw[SOLUTION_OFFSET..SOLUTION_OFFSET + 3];
        assert_eq!(
            prefix,
            &[0xfdu8, 0x40, 0x05][..],
            "{}: the solution length prefix is {:02x?}, expected fd 40 05",
            fixture.name,
            prefix
        );

        let blob = &fixture.raw[SOLUTION_OFFSET + 3..HEADER_BYTES];
        assert_eq!(blob.len(), SOLUTION_BYTES, "{}: blob length", fixture.name);

        let solution = Solution::zcash_deserialize(Cursor::new(
            &fixture.raw[SOLUTION_OFFSET..HEADER_BYTES],
        ))
        .unwrap_or_else(|e| panic!("{}: solution did not parse: {:?}", fixture.name, e));

        assert_eq!(
            &solution.0[..],
            blob,
            "{}: the parsed solution is not the bytes on the wire",
            fixture.name
        );
        assert_eq!(
            serialize(&solution),
            fixture.raw[SOLUTION_OFFSET..HEADER_BYTES].to_vec(),
            "{}: the solution does not re-serialize to its own bytes",
            fixture.name
        );
        assert!(
            format!("{:?}", solution).contains(&hex::encode(blob)),
            "{}: Debug does not show the solution bytes",
            fixture.name
        );

        // And it is a solution, not just 1344 bytes: every block in the corpus
        // was mined, so Equihash(200, 9) must accept it for this header.
        let header = parse_header(&fixture);
        header
            .solution
            .check(&header)
            .unwrap_or_else(|e| panic!("{}: equihash solution is invalid: {:?}", fixture.name, e));
    }
}

#[test]
fn the_difficulty_threshold_round_trips_through_its_expanded_target() {
    // `CompactDifficulty(pub(crate) u32)` hides its bits, so the value is
    // pinned from three directions instead: the u32 on the wire, the `Debug`
    // rendering, and the expanded target that the compact bits denote.
    for fixture in blocks() {
        let on_the_wire = wire_u32(&fixture, DIFFICULTY_OFFSET);
        assert_eq!(
            on_the_wire, fixture.difficulty_threshold,
            "{}: the corpus TSV disagrees with the corpus bytes",
            fixture.name
        );

        let compact = parse_header(&fixture).difficulty_threshold;
        let shown = format!("{:?}", compact);
        assert!(
            shown.starts_with(&format!("CompactDifficulty({:#010x},", on_the_wire)),
            "{}: Debug is {:?}, which does not start with the wire bits {:#010x}",
            fixture.name,
            shown,
            on_the_wire
        );

        let (expected_target, _) = expected_target_and_work(on_the_wire);
        let target = expanded(&fixture, compact);
        assert_eq!(
            format!("{:?}", target),
            format!("ExpandedDifficulty({:?})", expected_target),
            "{}: {:#010x} expands to the wrong target",
            fixture.name,
            on_the_wire
        );

        // ToCompact(ToTarget(bits)) == bits is a consensus rule in its own
        // right: the difficulty adjustment produces a target, and the header
        // must carry the one canonical compact spelling of it.
        assert_eq!(
            target.to_compact(),
            compact,
            "{}: {:#010x} does not survive the compact round trip",
            fixture.name,
            on_the_wire
        );
        assert_eq!(
            CompactDifficulty::from(target),
            compact,
            "{}: From<ExpandedDifficulty> disagrees with to_compact",
            fixture.name
        );
    }

    // The sign bit is set in 0xffffffff, and zcashd rejects negative compact
    // values before it ever looks at the hash.
    assert!(
        INVALID_COMPACT_DIFFICULTY.to_expanded().is_none(),
        "a compact difficulty with its sign bit set must not expand"
    );
    assert!(
        INVALID_COMPACT_DIFFICULTY.to_work().is_none(),
        "a compact difficulty with its sign bit set must have no work"
    );
}

#[test]
fn every_corpus_block_hash_meets_its_own_target_and_the_network_limit() {
    // The difficulty filter, from both sides. Every block here was accepted by
    // the network, so its hash -- read as a little-endian 256-bit integer -- is
    // at most the target its own header declares, and that target is at most
    // the network's PoWLimit.
    for fixture in blocks() {
        let block = parse_block(&fixture);
        let target = expanded(&fixture, block.header.difficulty_threshold);
        let hash = block.hash();

        assert_eq!(
            hash.to_string(),
            fixture.block_hash,
            "{}: block hash does not match the chain",
            fixture.name
        );
        assert!(
            hash <= target,
            "{}: hash {} does not meet its own target {:?}",
            fixture.name,
            fixture.block_hash,
            target
        );
        assert!(
            target <= ExpandedDifficulty::target_difficulty_limit(network_of(&fixture)),
            "{}: target {:?} is easier than the {} PoWLimit",
            fixture.name,
            target,
            fixture.network
        );
    }
}

#[test]
fn work_rises_as_the_target_falls_between_genesis_and_block_982681() {
    // Work is 2^256 / (target + 1): the expected number of hashes a miner has
    // to try. Mainnet genesis sits at the PoWLimit; block 982681 is nearly six
    // years of difficulty adjustment later, on the same network, so its target
    // must be lower and its work higher. (Testnet is deliberately not used for
    // the comparison: ZIP-208's minimum-difficulty rule lets a slow testnet
    // block drop straight back to the PoWLimit, and test-1028500 does exactly
    // that.)
    let corpus = blocks();

    for fixture in corpus.iter() {
        let compact = parse_header(fixture).difficulty_threshold;
        let (_, expected_work) = expected_target_and_work(fixture.difficulty_threshold);
        let work = compact.to_work().unwrap_or_else(|| {
            panic!(
                "{}: threshold {:#010x} has no work",
                fixture.name, fixture.difficulty_threshold
            )
        });
        assert_eq!(
            work.as_u128(),
            expected_work,
            "{}: work for {:#010x}",
            fixture.name,
            fixture.difficulty_threshold
        );
        assert_eq!(
            Work::try_from(expanded(fixture, compact)).expect("a real target has work"),
            work,
            "{}: work from the expanded target disagrees with work from the compact bits",
            fixture.name
        );
    }

    let early = corpus
        .iter()
        .find(|f| f.name == "main-0000000")
        .expect("the corpus should ship main-0000000");
    let late = corpus
        .iter()
        .find(|f| f.name == "main-0982681")
        .expect("the corpus should ship main-0982681");
    assert!(
        late.height > early.height,
        "the two blocks must be ordered by height"
    );

    let early_header = parse_header(early);
    let late_header = parse_header(late);
    let early_target = expanded(early, early_header.difficulty_threshold);
    let late_target = expanded(late, late_header.difficulty_threshold);

    assert!(
        late_target < early_target,
        "mainnet {} should be harder than mainnet {}: target {:?} is not below {:?}",
        late.height,
        early.height,
        late_target,
        early_target
    );
    let early_work = early_header
        .difficulty_threshold
        .to_work()
        .expect("genesis has work");
    let late_work = late_header
        .difficulty_threshold
        .to_work()
        .expect("block 982681 has work");
    assert!(
        late_work > early_work,
        "work did not rise with difficulty: {:?} is not above {:?}",
        late_work,
        early_work
    );

    // Cumulative work is how chains are compared, so adding and removing a
    // block's work has to be exact.
    let mut cumulative = PartialCumulativeWork::from(early_work);
    cumulative += late_work;
    assert_eq!(
        cumulative,
        PartialCumulativeWork::from(late_work) + early_work,
        "cumulative work is not commutative"
    );
    cumulative -= late_work;
    assert_eq!(
        cumulative,
        PartialCumulativeWork::from(early_work),
        "removing a block's work did not restore the previous total"
    );
}
