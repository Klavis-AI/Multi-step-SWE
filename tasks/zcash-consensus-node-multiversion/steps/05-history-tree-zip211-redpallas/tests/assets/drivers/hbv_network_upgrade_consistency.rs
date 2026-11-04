//! `Block::check_transaction_network_upgrade_consistency`, against real blocks
//! with a hand-appended transaction.
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! A v5 transaction carries its own `network_upgrade` field -- derived from
//! `nConsensusBranchId` on the wire -- and this milestone adds a method that
//! checks it against the network upgrade the block's own height implies. Get
//! this wrong and a v5 transaction signed for one upgrade's consensus branch
//! id could be replayed inside a block belonging to another.
//!
//! `Block` and `Transaction::V5`'s fields are both public, so a block from
//! this corpus can be extended with a hand-built v5 transaction without
//! going through wire serialization at all: clone a real, parsed block (which
//! supplies a genuine coinbase transaction, so `coinbase_height` -- and so
//! the block's own idea of "current network upgrade" -- resolves the same way
//! it would for the untouched block), and push a second transaction whose
//! `network_upgrade` field is chosen by this file, not read off the wire.
//!
//! `check_transaction_network_upgrade_consistency`'s `Result` uses an error
//! type from a private module (`zebra_chain::block::error::BlockError`), so
//! it cannot be named or matched here -- only `Result::is_ok`/`is_err` and
//! its `Display` text, which is quoted directly from the source below, are
//! observable. That is the same private-type-in-a-public-signature shape
//! `chain-history-commitment` and `tx-header-and-pool-fields` already rely
//! on elsewhere in this step.

use std::collections::BTreeMap;
use std::io::Cursor;
use std::path::PathBuf;
use std::sync::Arc;

use zebra_chain::block::Block;
use zebra_chain::parameters::{Network, NetworkUpgrade};
use zebra_chain::serialization::ZcashDeserialize;
use zebra_chain::transaction::{LockTime, Transaction};

// ---------------------------------------------------------------- fixtures

struct Fixture {
    name: String,
    network: Network,
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

fn network_named(name: &str) -> Network {
    match name {
        "Mainnet" => Network::Mainnet,
        "Testnet" => Network::Testnet,
        other => panic!("blocks.tsv names an unknown network {:?}", other),
    }
}

/// The one fixture named `name` in `blocks.tsv`.
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

/// A minimal, syntactically valid v5 transaction claiming `network_upgrade`,
/// with no inputs, outputs or shielded data of its own. What this file checks
/// never looks past the `network_upgrade` field, so nothing else about the
/// transaction's shape matters.
fn v5_claiming(network_upgrade: NetworkUpgrade) -> Transaction {
    Transaction::V5 {
        network_upgrade,
        lock_time: LockTime::Height(zebra_chain::block::Height(0)),
        expiry_height: zebra_chain::block::Height(0),
        inputs: Vec::new(),
        outputs: Vec::new(),
        sapling_shielded_data: None,
        orchard_shielded_data: None,
    }
}

/// The two base fixtures this file extends: one from each network, each
/// already past its own Heartwood-equivalent activation so `coinbase_height`
/// resolves to a height with a definite, non-Genesis current network
/// upgrade. `(fixture name, the network upgrade active at that height)`.
const BASES: &[(&str, NetworkUpgrade)] = &[
    ("main-0903001", NetworkUpgrade::Heartwood),
    ("test-1028500", NetworkUpgrade::Canopy),
];

/// Every `NetworkUpgrade` variant, restated here rather than iterated out of
/// the crate, so a mismatch test does not depend on the enum under test to
/// enumerate its own values.
const ALL_UPGRADES: &[NetworkUpgrade] = &[
    NetworkUpgrade::Genesis,
    NetworkUpgrade::BeforeOverwinter,
    NetworkUpgrade::Overwinter,
    NetworkUpgrade::Sapling,
    NetworkUpgrade::Blossom,
    NetworkUpgrade::Heartwood,
    NetworkUpgrade::Canopy,
    NetworkUpgrade::Nu5,
];

const WRONG_CONSENSUS_BRANCH_ID_MESSAGE: &str =
    "transaction has wrong consensus branch id for block network upgrade";

// ------------------------------------------------------------------- tests

#[test]
fn an_unmodified_pre_nu5_block_is_trivially_consistent() {
    // Every transaction in these corpus blocks predates v5, so
    // `network_upgrade()` is `None` everywhere and the check has nothing to
    // compare -- it must not manufacture a failure out of an empty search.
    for (name, _) in BASES {
        let fixture = named(name);
        let block = parse(&fixture);
        block
            .check_transaction_network_upgrade_consistency(fixture.network)
            .unwrap_or_else(|e| {
                panic!(
                    "{}: a block with no v5 transactions must be consistent, got {}",
                    fixture.name, e
                )
            });
    }
}

#[test]
fn a_v5_transaction_matching_the_blocks_current_upgrade_is_consistent() {
    for (name, current) in BASES {
        let fixture = named(name);
        let mut block = parse(&fixture);
        block.transactions.push(Arc::new(v5_claiming(*current)));

        block
            .check_transaction_network_upgrade_consistency(fixture.network)
            .unwrap_or_else(|e| {
                panic!(
                    "{}: appending a v5 transaction that claims this block's own current \
                     upgrade {:?} must be consistent, got {}",
                    fixture.name, current, e
                )
            });
    }
}

#[test]
fn a_v5_transaction_with_a_mismatched_network_upgrade_is_rejected() {
    for (name, current) in BASES {
        let fixture = named(name);
        let wrong = if *current == NetworkUpgrade::Nu5 {
            NetworkUpgrade::Canopy
        } else {
            NetworkUpgrade::Nu5
        };
        assert_ne!(wrong, *current, "the fixture must pick an actual mismatch");

        let mut block = parse(&fixture);
        block.transactions.push(Arc::new(v5_claiming(wrong)));

        let result = block.check_transaction_network_upgrade_consistency(fixture.network);
        assert!(
            result.is_err(),
            "{}: a v5 transaction claiming {:?} inside a block whose current upgrade is {:?} \
             must be rejected",
            fixture.name,
            wrong,
            current
        );
        assert_eq!(
            result.unwrap_err().to_string(),
            WRONG_CONSENSUS_BRANCH_ID_MESSAGE,
            "{}: the rejection's message text has changed",
            fixture.name
        );
    }
}

#[test]
fn every_other_network_upgrade_is_rejected_at_the_heartwood_block() {
    let fixture = named("main-0903001");
    for upgrade in ALL_UPGRADES {
        if *upgrade == NetworkUpgrade::Heartwood {
            continue;
        }
        let mut block = parse(&fixture);
        block.transactions.push(Arc::new(v5_claiming(*upgrade)));

        assert!(
            block
                .check_transaction_network_upgrade_consistency(fixture.network)
                .is_err(),
            "{}: a v5 transaction claiming {:?} must be rejected at a block whose current \
             upgrade is Heartwood",
            fixture.name,
            upgrade
        );
    }
}

#[test]
fn one_bad_v5_transaction_among_good_ones_still_rejects_the_whole_block() {
    let fixture = named("main-0903001");
    let mut block = parse(&fixture);
    block
        .transactions
        .push(Arc::new(v5_claiming(NetworkUpgrade::Heartwood)));
    block
        .transactions
        .push(Arc::new(v5_claiming(NetworkUpgrade::Heartwood)));
    // The one inconsistent transaction, sandwiched between two consistent ones.
    block.transactions.push(Arc::new(v5_claiming(NetworkUpgrade::Nu5)));
    block
        .transactions
        .push(Arc::new(v5_claiming(NetworkUpgrade::Heartwood)));

    let result = block.check_transaction_network_upgrade_consistency(fixture.network);
    assert!(
        result.is_err(),
        "a block with three consistent v5 transactions and one inconsistent one must still be \
         rejected as a whole"
    );
    assert_eq!(
        result.unwrap_err().to_string(),
        WRONG_CONSENSUS_BRANCH_ID_MESSAGE
    );
}
