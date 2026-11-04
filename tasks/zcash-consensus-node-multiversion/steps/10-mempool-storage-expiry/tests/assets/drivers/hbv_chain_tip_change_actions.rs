//! Watching the chain tip for the two things a mempool has to react to.
//!
//! Harbor verifier driver -- injected into `zebra-state/tests/` at grading
//! time. Public API only; no `zebra_test`, no proptest.
//!
//! A mempool cannot decide anything on its own. Whether a transaction is still
//! admissible depends on the height of the next block, and whether the
//! transactions it already holds are still unmined depends on which block was
//! mined last. The previous milestone gave the state a read-only tip
//! (`LatestChainTip`); this one adds the other half -- a *change* stream, and
//! the vocabulary for describing a change:
//!
//! ```text
//! TipAction::Grow  { block }          // the tip advanced by one child block
//! TipAction::Reset { height, hash }   // the tip jumped: rollback, skip, or
//!                                     // a network upgrade activation
//! ```
//!
//! The distinction is exactly the distinction a mempool needs. A `Grow` means
//! "remove the transactions this one block mined"; a `Reset` means "the chain
//! you were reasoning about is gone, clear everything".
//!
//! Both types are re-exported from the crate root, so `zebra_state::TipAction`
//! and `zebra_state::ChainTipChange` name them without reaching into a module
//! path. `TipAction` answers three questions regardless of which variant it
//! is: was this a reset, what is the tip hash now, and what is the tip height
//! now. `ChainTipChange::last_tip_change` is the non-blocking half of the
//! stream: it reports a change if one has happened since it was last called,
//! and `None` otherwise.
//!
//! `TipAction::Grow` carries a `ChainTipBlock`, which this tag exports only
//! under `#[cfg(any(test, feature = "proptest-impl"))]`, and a tip only
//! advances once blocks have been committed through the state service. So the
//! `Grow` variant cannot be constructed from an integration test, and this
//! driver scores what is reachable: the `Reset` variant, the three
//! variant-independent accessors, and the quiescent behaviour of a freshly
//! created `ChainTipChange`. See TEST_INTERFACE for the gap.
//!
//! Expected values: the shipped seventeen-block corpus. Heights and hashes
//! come from `blocks.tsv`, never from the action under test.

use std::str::FromStr;

use zebra_chain::block::{self, Height};
use zebra_chain::parameters::Network;

use zebra_state::{ChainTipChange, Config, TipAction};

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

/// The block hash the corpus records for `fixture`, parsed from the
/// display-order hex the TSV stores.
fn corpus_hash(fixture: &Fixture) -> block::Hash {
    block::Hash::from_str(&fixture.block_hash)
        .unwrap_or_else(|e| panic!("{}: block_hash is not a hash: {:?}", fixture.name, e))
}

// ------------------------------------------------------------------- tests

#[test]
fn a_reset_action_reports_the_height_the_corpus_records() {
    for fixture in blocks() {
        let action = TipAction::Reset {
            height: Height(fixture.height),
            hash: corpus_hash(&fixture),
        };
        assert_eq!(
            action.best_tip_height(),
            Height(fixture.height),
            "{}: a reset to height {} reported {:?}",
            fixture.name,
            fixture.height,
            action.best_tip_height()
        );
    }
}

#[test]
fn a_reset_action_reports_the_hash_the_corpus_records() {
    for fixture in blocks() {
        let action = TipAction::Reset {
            height: Height(fixture.height),
            hash: corpus_hash(&fixture),
        };
        assert_eq!(
            action.best_tip_hash().to_string(),
            fixture.block_hash,
            "{}: a reset to {} reported {}",
            fixture.name,
            fixture.block_hash,
            action.best_tip_hash()
        );
    }
}

#[test]
fn a_reset_action_says_that_it_is_a_reset() {
    // `is_reset` is how a mempool decides between dropping the transactions
    // one block mined and clearing itself completely.
    for fixture in blocks() {
        let action = TipAction::Reset {
            height: Height(fixture.height),
            hash: corpus_hash(&fixture),
        };
        assert!(
            action.is_reset(),
            "{}: a Reset action does not report itself as a reset",
            fixture.name
        );
        match action {
            TipAction::Reset { height, hash } => {
                assert_eq!(
                    height,
                    Height(fixture.height),
                    "{}: the Reset variant's own height field is wrong",
                    fixture.name
                );
                assert_eq!(
                    hash.to_string(),
                    fixture.block_hash,
                    "{}: the Reset variant's own hash field is wrong",
                    fixture.name
                );
            }
            other => panic!(
                "{}: a Reset action matched another variant: {:?}",
                fixture.name, other
            ),
        }
    }
}

#[test]
fn two_reset_actions_are_equal_exactly_when_their_tips_match() {
    let corpus = blocks();
    // Corpus assertion, not a code assertion: two distinct tips are needed.
    assert!(
        corpus.len() >= 2,
        "the corpus supplied {} blocks; the verifier needs at least 2",
        corpus.len()
    );
    let first = &corpus[0];
    let second = corpus
        .iter()
        .find(|f| f.block_hash != first.block_hash)
        .unwrap_or_else(|| panic!("the corpus has only one distinct block hash"));

    let action = TipAction::Reset {
        height: Height(first.height),
        hash: corpus_hash(first),
    };

    assert_eq!(
        action.clone(),
        action,
        "{}: a reset action is not equal to a clone of itself",
        first.name
    );
    assert_eq!(
        action,
        TipAction::Reset {
            height: Height(first.height),
            hash: corpus_hash(first),
        },
        "{}: two resets to the same tip are not equal",
        first.name
    );
    assert_ne!(
        action,
        TipAction::Reset {
            height: Height(first.height + 1),
            hash: corpus_hash(first),
        },
        "{}: two resets to different heights compared equal",
        first.name
    );
    assert_ne!(
        action,
        TipAction::Reset {
            height: Height(first.height),
            hash: corpus_hash(second),
        },
        "{}: two resets to different hashes compared equal",
        first.name
    );
}

#[test]
fn a_fresh_chain_tip_change_reports_no_change_until_a_block_is_committed() {
    // `init` hands back the change stream alongside the service and the
    // read-only tip. Nothing has been committed to an ephemeral state, so the
    // tip has never changed and the non-blocking read must say so -- and must
    // keep saying so however often it is called.
    let (_service, _latest_chain_tip, mut chain_tip_change) =
        zebra_state::init(Config::ephemeral(), Network::Mainnet);

    for call in 0..5 {
        assert_eq!(
            chain_tip_change.last_tip_change(),
            None,
            "call {}: a state with no committed blocks reported a tip change",
            call
        );
    }

    let mut cloned = chain_tip_change.clone();
    assert_eq!(
        cloned.last_tip_change(),
        None,
        "a clone of a quiescent change stream reported a tip change"
    );
}

#[test]
fn the_chain_tip_change_types_are_reachable_from_the_crate_root() {
    // These coercions fail to compile unless the crate root exports both
    // types under these names, with exactly these signatures.
    let is_reset: fn(&TipAction) -> bool = TipAction::is_reset;
    let best_tip_hash: fn(&TipAction) -> block::Hash = TipAction::best_tip_hash;
    let best_tip_height: fn(&TipAction) -> Height = TipAction::best_tip_height;
    let last_tip_change: fn(&mut ChainTipChange) -> Option<TipAction> =
        ChainTipChange::last_tip_change;

    let fixture = blocks()
        .into_iter()
        .next()
        .unwrap_or_else(|| panic!("the corpus is empty"));
    let action = TipAction::Reset {
        height: Height(fixture.height),
        hash: corpus_hash(&fixture),
    };

    assert!(is_reset(&action), "{}: is_reset said false", fixture.name);
    assert_eq!(
        best_tip_hash(&action).to_string(),
        fixture.block_hash,
        "{}: best_tip_hash returned the wrong hash",
        fixture.name
    );
    assert_eq!(
        best_tip_height(&action),
        Height(fixture.height),
        "{}: best_tip_height returned the wrong height",
        fixture.name
    );

    let (_service, _latest_chain_tip, mut chain_tip_change) =
        zebra_state::init(Config::ephemeral(), Network::Mainnet);
    assert_eq!(
        last_tip_change(&mut chain_tip_change),
        None,
        "a state with no committed blocks reported a tip change"
    );
}
