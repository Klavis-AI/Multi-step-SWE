//! Constructing a chain history root by hand, and comparing it to the real one.
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! ZIP-221 replaces the block header's `hashFinalSaplingRoot` field with a
//! commitment to the root of a Merkle Mountain Range over the chain's history,
//! from the block after a network upgrade's activation through to Canopy. The
//! predecessor milestone already taught `Block::commitment` to read that field
//! and return it as `Commitment::ChainHistoryRoot(ChainHistoryMmrRootHash)` --
//! but it could only ever be *produced* by the crate's own private parser, so
//! the strongest thing a caller could assert about it was which enum variant
//! it was, never which 32 bytes it held. A test that only checks the variant
//! would still pass if the crate swapped in the wrong root, so long as it kept
//! the shape right.
//!
//! This milestone adds `impl From<[u8; 32]> for ChainHistoryMmrRootHash`,
//! which is a public, infallible way to build that type from bytes the caller
//! already has -- such as the `root_bytes_hex` column of this corpus's own
//! `blocks.tsv`, read independently of the crate. That closes the loophole:
//! a driver can now assert the *value*, not just the shape.

use std::collections::BTreeMap;
use std::io::Cursor;
use std::path::PathBuf;

use zebra_chain::block::{Block, ChainHistoryMmrRootHash, Commitment};
use zebra_chain::parameters::Network;
use zebra_chain::serialization::ZcashDeserialize;

// ---------------------------------------------------------------- fixtures

#[allow(dead_code)]
struct Fixture {
    name: String,
    network: Network,
    root_bytes: [u8; 32],
    raw: Vec<u8>,
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

fn network_named(name: &str) -> Network {
    match name {
        "Mainnet" => Network::Mainnet,
        "Testnet" => Network::Testnet,
        other => panic!("blocks.tsv names an unknown network {:?}", other),
    }
}

fn named(name: &str) -> Fixture {
    let dir = fixture_dir();
    let tsv = std::fs::read_to_string(dir.join("blocks.tsv")).expect("blocks.tsv is readable");
    let mut lines = tsv.lines();
    let header: Vec<&str> = lines
        .next()
        .expect("blocks.tsv has a header row")
        .trim_start_matches("# ")
        .split('\t')
        .collect();

    for line in lines {
        if line.is_empty() {
            continue;
        }
        let cells: Vec<&str> = line.split('\t').collect();
        assert_eq!(cells.len(), header.len(), "malformed row: {}", line);
        let row: BTreeMap<&str, &str> = header.iter().cloned().zip(cells).collect();
        if cell(&row, "name") != name {
            continue;
        }
        let hex_text = std::fs::read_to_string(dir.join("blocks").join(format!("{}.hex", name)))
            .unwrap_or_else(|e| panic!("cannot read block {}: {}", name, e));
        return Fixture {
            name: name.to_string(),
            network: network_named(cell(&row, "network")),
            root_bytes: thirty_two(cell(&row, "root_bytes_hex")),
            raw: hex::decode(hex_text.trim()).expect("block fixture is hex"),
        };
    }
    panic!("blocks.tsv has no fixture named {}", name);
}

// ----------------------------------------------------------------- helpers

fn parse(fixture: &Fixture) -> Block {
    Block::zcash_deserialize(Cursor::new(&fixture.raw[..]))
        .unwrap_or_else(|e| panic!("{} did not parse: {:?}", fixture.name, e))
}

/// The three corpus blocks whose header commits to a real Merkle Mountain
/// Range root: the block right after Heartwood activation (which is the
/// earliest height a `ChainHistoryRoot` can appear -- the activation block
/// itself carries the reserved all-zero value instead, per ZIP-221), a later
/// Heartwood-era block, and testnet's Canopy activation block.
const CHAIN_HISTORY_ROOT_FIXTURES: &[&str] = &["main-0903001", "main-0982681", "test-1028500"];

// ------------------------------------------------------------------- tests

#[test]
fn heartwood_and_canopy_blocks_carry_the_exact_chain_history_root_from_the_corpus() {
    for name in CHAIN_HISTORY_ROOT_FIXTURES {
        let fixture = named(name);
        let block = parse(&fixture);
        let actual = block
            .commitment(fixture.network)
            .unwrap_or_else(|e| panic!("{}: commitment did not parse: {:?}", fixture.name, e));

        // Built entirely from the TSV's root_bytes_hex column, independently
        // of the block bytes and of the crate's own commitment parser.
        let built = Commitment::ChainHistoryRoot(ChainHistoryMmrRootHash::from(fixture.root_bytes));

        assert_eq!(
            built, actual,
            "{}: a ChainHistoryMmrRootHash built from the corpus's root_bytes_hex \
             does not equal the commitment this block actually carries",
            fixture.name
        );
    }
}

#[test]
fn constructing_from_arbitrary_bytes_does_not_equal_a_real_root() {
    // Guards against a vacuous or reflexive PartialEq: the conversion must be
    // a faithful byte copy, not a constant or a comparison that always
    // succeeds.
    let fixture = named("main-0903001");
    let block = parse(&fixture);
    let actual = block
        .commitment(fixture.network)
        .unwrap_or_else(|e| panic!("{}: commitment did not parse: {:?}", fixture.name, e));

    let wrong = Commitment::ChainHistoryRoot(ChainHistoryMmrRootHash::from([0xab; 32]));
    assert_ne!(
        wrong, actual,
        "{}: an arbitrary 32-byte value equals this block's real chain history root",
        fixture.name
    );
}

#[test]
fn the_three_chain_history_roots_in_the_corpus_are_pairwise_distinct() {
    // Three different real blocks; a correct MMR commits to a different
    // history at each of them, so the roots must differ pairwise. If this
    // failed, either the corpus stopped exercising distinct heights or the
    // conversion is collapsing distinct byte strings onto the same value.
    let built: Vec<(String, ChainHistoryMmrRootHash)> = CHAIN_HISTORY_ROOT_FIXTURES
        .iter()
        .map(|name| {
            let fixture = named(name);
            (fixture.name.clone(), ChainHistoryMmrRootHash::from(fixture.root_bytes))
        })
        .collect();

    for i in 0..built.len() {
        for j in (i + 1)..built.len() {
            assert_ne!(
                Commitment::ChainHistoryRoot(built[i].1),
                Commitment::ChainHistoryRoot(built[j].1),
                "{} and {} were built with equal chain history roots",
                built[i].0,
                built[j].0
            );
        }
    }
}

#[test]
fn the_heartwood_activation_blocks_reserved_field_is_never_a_chain_history_root_even_when_reconstructed_by_hand() {
    // ZIP-221: the Heartwood activation block's commitment field is reserved
    // and must be all zeroes -- it is not a root, and must not be read as
    // one. Even handing the conversion exactly those all-zero bytes, and
    // wrapping them as a ChainHistoryRoot by hand, must not equal what this
    // activation block's own commitment actually is (which is the
    // ChainHistoryActivationReserved variant, a different case entirely).
    let fixture = named("main-0903000");
    let block = parse(&fixture);
    let actual = block
        .commitment(fixture.network)
        .unwrap_or_else(|e| panic!("{}: commitment did not parse: {:?}", fixture.name, e));

    assert_eq!(
        fixture.root_bytes,
        [0u8; 32],
        "{}: the Heartwood activation block's reserved field is not all zero in the corpus",
        fixture.name
    );
    let hand_built = Commitment::ChainHistoryRoot(ChainHistoryMmrRootHash::from(fixture.root_bytes));
    assert_ne!(
        hand_built, actual,
        "{}: treating the Heartwood activation block's reserved field as a chain \
         history root equals its real (reserved) commitment",
        fixture.name
    );
}
