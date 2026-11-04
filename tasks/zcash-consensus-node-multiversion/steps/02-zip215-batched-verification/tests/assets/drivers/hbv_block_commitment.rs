//! `Commitment`: the header's 32 reserved bytes, read according to the upgrade.
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! The same 32 bytes of every Zcash block header have meant five different
//! things over the chain's history, and the only way to know which is to know
//! the network and the height. Before Sapling they are a reserved field that
//! MUST be zero; from Sapling through Blossom they are the final Sapling
//! treestate root; in the Heartwood activation block alone they are reserved
//! again and ZIP-221 says they MUST NOT be read as a root; from the block after
//! Heartwood activation through Canopy they are the root of the chain history
//! Merkle Mountain Range; and from NU5 they additionally commit to this block's
//! transaction authorising data.
//!
//! This milestone replaces the `RootHash` enum, which returned an `Option` and
//! silently mapped a non-zero reserved field onto a reserved variant, with
//! `Commitment` and a `Result`: a reserved field that is not all zeroes is now
//! a named consensus error, and a block with no coinbase height is another.
//! Seventeen real blocks, five of them chosen to sit on an activation
//! boundary, are enough to pin every arm of that dispatch.

use std::io::Cursor;

use zebra_chain::block::{Block, Commitment, CommitmentError};
use zebra_chain::parameters::Network;
use zebra_chain::serialization::{ZcashDeserialize, ZcashSerialize};

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

fn network_of(fixture: &Fixture) -> Network {
    match fixture.network.as_str() {
        "Mainnet" => Network::Mainnet,
        "Testnet" => Network::Testnet,
        other => panic!("blocks.tsv has an unknown network {:?}", other),
    }
}

fn parse(fixture: &Fixture) -> Block {
    Block::zcash_deserialize(Cursor::new(&fixture.raw[..]))
        .unwrap_or_else(|e| panic!("{} did not parse: {:?}", fixture.name, e))
}

/// The network upgrade activation heights, restated here from ZIP-200's
/// deployment table rather than read out of the crate, so that the dispatch
/// test does not check the candidate's constants against the candidate's own
/// constants.
fn era(network: &str, height: u32) -> &'static str {
    let (overwinter, sapling, blossom, heartwood, canopy) = match network {
        "Mainnet" => (347_500, 419_200, 653_600, 903_000, 1_046_400),
        "Testnet" => (207_500, 280_000, 584_000, 903_800, 1_028_500),
        other => panic!("unknown network {:?}", other),
    };
    if height >= canopy {
        "Canopy"
    } else if height > heartwood {
        "Heartwood"
    } else if height == heartwood {
        "HeartwoodActivation"
    } else if height >= blossom {
        "Blossom"
    } else if height >= sapling {
        "Sapling"
    } else if height >= overwinter {
        "Overwinter"
    } else {
        "PreOverwinter"
    }
}

fn named(name: &str) -> Fixture {
    blocks()
        .into_iter()
        .find(|f| f.name == name)
        .unwrap_or_else(|| panic!("the corpus ships {}", name))
}

/// Re-encode a parsed block, with its commitment field replaced, and parse it
/// back. Going through the wire format rather than mutating the struct keeps
/// the test honest about what a peer could actually send.
fn with_commitment_bytes(fixture: &Fixture, bytes: [u8; 32]) -> Block {
    let mut block = parse(fixture);
    block.header.commitment_bytes = bytes;
    let mut wire = Vec::new();
    block
        .zcash_serialize(&mut wire)
        .expect("a Vec never fails to write");
    assert_eq!(
        &wire[68..100],
        &bytes[..],
        "the commitment field is not at header offset 68"
    );
    Block::zcash_deserialize(Cursor::new(&wire[..]))
        .expect("an edited block must still parse")
}

// -------------------------------------------------------------------- tests

#[test]
fn every_block_in_the_corpus_has_a_commitment() {
    for fixture in blocks() {
        let block = parse(&fixture);
        block.commitment(network_of(&fixture)).unwrap_or_else(|e| {
            panic!(
                "{} is a real block accepted by the network, but its commitment was rejected \
                 with {:?}",
                fixture.name, e
            )
        });
    }
}

#[test]
fn the_upgrade_decides_which_of_the_five_readings_applies() {
    for fixture in blocks() {
        let block = parse(&fixture);
        let commitment = block
            .commitment(network_of(&fixture))
            .unwrap_or_else(|e| panic!("{}: {:?}", fixture.name, e));

        match (era(&fixture.network, fixture.height), commitment) {
            ("PreOverwinter", Commitment::PreSaplingReserved)
            | ("Overwinter", Commitment::PreSaplingReserved) => {}
            ("Sapling", Commitment::FinalSaplingRoot(_))
            | ("Blossom", Commitment::FinalSaplingRoot(_)) => {}
            ("HeartwoodActivation", Commitment::ChainHistoryActivationReserved) => {}
            ("Heartwood", Commitment::ChainHistoryRoot(_))
            | ("Canopy", Commitment::ChainHistoryRoot(_)) => {}
            (expected, got) => panic!(
                "{} is at height {} in the {} era, but its commitment was read as {:?}",
                fixture.name, fixture.height, expected, got
            ),
        }
    }
}

#[test]
fn blocks_before_sapling_carry_the_pre_sapling_reserved_commitment() {
    let mut seen = 0;
    for fixture in blocks() {
        let e = era(&fixture.network, fixture.height);
        if e != "PreOverwinter" && e != "Overwinter" {
            continue;
        }
        seen += 1;
        let block = parse(&fixture);
        assert_eq!(
            block.commitment(network_of(&fixture)),
            Ok(Commitment::PreSaplingReserved),
            "{} is before Sapling, so its commitment field is a reserved field",
            fixture.name
        );
        assert_eq!(
            block.header.commitment_bytes, [0u8; 32],
            "{}: a pre-Sapling reserved field must be all zeroes on the wire",
            fixture.name
        );
    }
    assert!(
        seen >= 7,
        "the corpus should hold at least seven pre-Sapling blocks, found {}",
        seen
    );
}

#[test]
fn sapling_and_blossom_blocks_carry_the_final_sapling_root() {
    let mut seen = 0;
    for fixture in blocks() {
        let e = era(&fixture.network, fixture.height);
        if e != "Sapling" && e != "Blossom" {
            continue;
        }
        seen += 1;
        let block = parse(&fixture);
        match block.commitment(network_of(&fixture)) {
            Ok(Commitment::FinalSaplingRoot(root)) => assert_eq!(
                root.0, fixture.root_bytes,
                "{}: the Sapling root must be the header field verbatim",
                fixture.name
            ),
            other => panic!(
                "{} is in the {} era, so it must carry a final Sapling root, got {:?}",
                fixture.name, e, other
            ),
        }
    }
    assert!(
        seen >= 4,
        "the corpus should hold at least four Sapling or Blossom blocks, found {}",
        seen
    );
}

#[test]
fn the_heartwood_activation_block_is_reserved_and_not_a_root() {
    let fixture = named("main-0903000");
    assert_eq!(
        fixture.height, 903_000,
        "main-0903000 is the mainnet Heartwood activation block"
    );
    let block = parse(&fixture);
    assert_eq!(
        block.commitment(Network::Mainnet),
        Ok(Commitment::ChainHistoryActivationReserved),
        "ZIP-221: the commitment field of the Heartwood activation block MUST NOT be read as a \
         chain history root"
    );
    assert_eq!(
        block.header.commitment_bytes, [0u8; 32],
        "the Heartwood activation reserved field must be all zeroes on the wire"
    );
}

#[test]
fn blocks_after_heartwood_activation_carry_a_chain_history_root() {
    for name in ["main-0903001", "main-0982681"].iter() {
        let fixture = named(name);
        let block = parse(&fixture);
        match block.commitment(Network::Mainnet) {
            Ok(Commitment::ChainHistoryRoot(_)) => {}
            other => panic!(
                "{} is one block or more past Heartwood activation, so it commits to a chain \
                 history root, got {:?}",
                name, other
            ),
        }
        assert_ne!(
            block.header.commitment_bytes, [0u8; 32],
            "{}: a chain history root is not all zeroes",
            name
        );
    }
}

#[test]
fn the_canopy_activation_block_carries_a_chain_history_root() {
    // The reserved reading belongs to the Heartwood activation block alone.
    // Canopy activation inherits the chain history root, so a rule written as
    // "any activation block is reserved" is wrong here.
    let fixture = named("test-1028500");
    assert_eq!(fixture.height, 1_028_500);
    let block = parse(&fixture);
    match block.commitment(Network::Testnet) {
        Ok(Commitment::ChainHistoryRoot(_)) => {}
        other => panic!(
            "the testnet Canopy activation block must carry a chain history root, got {:?}",
            other
        ),
    }
}

#[test]
fn the_commitment_is_read_from_the_headers_commitment_bytes_field() {
    for fixture in blocks() {
        let block = parse(&fixture);
        assert_eq!(
            block.header.commitment_bytes, fixture.root_bytes,
            "{}: Header::commitment_bytes must hold the 32 raw bytes from the wire",
            fixture.name
        );
    }
}

#[test]
fn the_commitment_field_occupies_header_bytes_68_through_100() {
    // version (4) + previous_block_hash (32) + merkle_root (32) = 68.
    for fixture in blocks() {
        assert_eq!(
            &fixture.raw[68..100],
            &fixture.root_bytes[..],
            "{}: the commitment field is not at offset 68 of the header",
            fixture.name
        );
    }
}

#[test]
fn the_same_block_is_read_differently_on_the_two_networks() {
    // Heartwood activates at mainnet 903,000 and testnet 903,800. Height
    // 903,000 is therefore the activation block on one network and an ordinary
    // Blossom block on the other, from the same 32 bytes.
    let fixture = named("main-0903000");
    let block = parse(&fixture);

    assert_eq!(
        block.commitment(Network::Mainnet),
        Ok(Commitment::ChainHistoryActivationReserved)
    );
    match block.commitment(Network::Testnet) {
        Ok(Commitment::FinalSaplingRoot(root)) => assert_eq!(root.0, [0u8; 32]),
        other => panic!(
            "height 903,000 is before testnet Heartwood activation, so on Testnet it must be \
             read as a final Sapling root, got {:?}",
            other
        ),
    }
}

#[test]
fn a_non_zero_pre_sapling_reserved_field_is_a_named_error() {
    let fixture = named("main-0000010");
    let mut bytes = [0u8; 32];
    bytes[7] = 0x01;
    let block = with_commitment_bytes(&fixture, bytes);

    assert_eq!(
        block.commitment(Network::Mainnet),
        Err(CommitmentError::InvalidPreSaplingReserved { actual: bytes }),
        "a pre-Sapling reserved field that is not all zeroes must be rejected, and the error \
         must carry the bytes that were found"
    );
}

#[test]
fn a_non_zero_heartwood_activation_reserved_field_is_a_named_error() {
    let fixture = named("main-0903000");
    let mut bytes = [0xffu8; 32];
    bytes[0] = 0x00;
    let block = with_commitment_bytes(&fixture, bytes);

    assert_eq!(
        block.commitment(Network::Mainnet),
        Err(CommitmentError::InvalidChainHistoryActivationReserved { actual: bytes }),
        "the Heartwood activation reserved field must be rejected when it is not all zeroes"
    );
}

#[test]
fn a_block_with_no_coinbase_height_has_no_commitment() {
    // The reading depends on the height, and the height comes from the
    // coinbase input, so a block without one cannot be read at all.
    let fixture = named("main-0000010");
    let mut block = parse(&fixture);
    block.transactions.clear();
    let hash = block.hash();

    assert_eq!(
        block.commitment(Network::Mainnet),
        Err(CommitmentError::MissingBlockHeight { block_hash: hash }),
        "a block with no coinbase height must report MissingBlockHeight, naming the block"
    );
}

#[test]
fn after_sapling_any_32_bytes_are_accepted_as_a_root() {
    // From Sapling onwards the field is a commitment, not a reserved field:
    // nothing in `Commitment` checks its value, so the parser must hand back
    // whatever the header carried, including all zeroes.
    let fixture = named("main-0419201");
    for bytes in [[0u8; 32], [0xffu8; 32]].iter() {
        let block = with_commitment_bytes(&fixture, *bytes);
        match block.commitment(Network::Mainnet) {
            Ok(Commitment::FinalSaplingRoot(root)) => assert_eq!(
                root.0, *bytes,
                "the Sapling root must be the header bytes verbatim"
            ),
            other => panic!(
                "a Sapling-era block must yield a final Sapling root for any 32 bytes, got {:?}",
                other
            ),
        }
    }
}

#[test]
fn sapling_activation_is_the_height_where_the_reading_changes() {
    // 419,199 and 419,200 are consecutive mainnet blocks, and the second is
    // the Sapling activation block. Overwinter activation, by contrast, does
    // not change the reading at all.
    let before = parse(&named("main-0419199"));
    let at = parse(&named("main-0419200"));
    assert_eq!(
        before.commitment(Network::Mainnet),
        Ok(Commitment::PreSaplingReserved),
        "the block before Sapling activation still carries the reserved field"
    );
    match at.commitment(Network::Mainnet) {
        Ok(Commitment::FinalSaplingRoot(_)) => {}
        other => panic!(
            "the Sapling activation block must carry a final Sapling root, got {:?}",
            other
        ),
    }

    for name in ["main-0347499", "main-0347500", "main-0347501"].iter() {
        assert_eq!(
            parse(&named(name)).commitment(Network::Mainnet),
            Ok(Commitment::PreSaplingReserved),
            "{}: Overwinter does not change how the commitment field is read",
            name
        );
    }
}
