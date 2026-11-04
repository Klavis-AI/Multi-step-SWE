//! `Transaction`'s new header and value-pool accessors, against real blocks.
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! This milestone adds six read-only accessors to `Transaction`:
//! `is_overwintered`, `version`, `network_upgrade`, `sprout_pool_added_values`,
//! `has_sapling_shielded_data` and `orchard_flags`. None of them existed on
//! the predecessor milestone's `Transaction` at all, so a driver that calls
//! any of them does not compile against that milestone -- there is no wrong
//! answer to fall back on, only a missing method.
//!
//! The corpus predates NU5 (it stops at Canopy), so every transaction in it
//! is v1 through v4. That is itself a fact this group checks: `version()`
//! never reports 5, and `network_upgrade()` -- which the type signature says
//! is `Some` only for a v5 transaction -- is `None` everywhere. The positive
//! side of a v5 transaction's shape (an actual `Some`) is left to the
//! milestone that introduces the first v5 transactions into this corpus; see
//! `TEST_INTERFACE.md` for why that boundary is drawn here.
//!
//! `sprout_pool_added_values` returns the `vpub_old` of every JoinSplit in a
//! transaction, regardless of version -- the exact quantity ZIP-211 (Canopy)
//! requires to be zero from its activation height onward. This corpus's last
//! Sprout-era block (mainnet height 347499) predates that rule and is used
//! here to show the accessor surfacing a real, nonzero, pre-rule value; the
//! Canopy activation block on testnet is used to show the boundary the other
//! side, where the corpus itself carries no JoinSplits at all.

use std::collections::BTreeMap;
use std::io::Cursor;
use std::path::PathBuf;

use zebra_chain::block::Block;
use zebra_chain::serialization::ZcashDeserialize;

// ---------------------------------------------------------------- fixtures

struct Fixture {
    name: String,
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

/// Load the whole block corpus, in the order the TSV lists it.
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
        assert_eq!(cells.len(), header.len(), "malformed row: {}", line);
        let row: BTreeMap<&str, &str> = header.iter().cloned().zip(cells).collect();
        let name = cell(&row, "name").to_string();
        let hex_text = std::fs::read_to_string(dir.join("blocks").join(format!("{}.hex", name)))
            .unwrap_or_else(|e| panic!("cannot read block {}: {}", name, e));
        out.push(Fixture {
            name,
            raw: hex::decode(hex_text.trim()).expect("block fixture is hex"),
        });
    }
    assert!(
        out.len() >= 17,
        "the shipped corpus has {} blocks; the verifier expects at least 17",
        out.len()
    );
    out
}

fn named(name: &str) -> Fixture {
    blocks()
        .into_iter()
        .find(|f| f.name == name)
        .unwrap_or_else(|| panic!("blocks.tsv has no fixture named {}", name))
}

fn parse(fixture: &Fixture) -> Block {
    Block::zcash_deserialize(Cursor::new(&fixture.raw[..]))
        .unwrap_or_else(|e| panic!("{} did not parse: {:?}", fixture.name, e))
}

// ------------------------------------------------------------------- tests

#[test]
fn every_transaction_in_this_pre_nu5_corpus_reports_a_version_below_five_and_no_network_upgrade() {
    // The corpus stops at Canopy, so v5 -- and the NU5 network upgrade that
    // `network_upgrade()` reports for it -- do not appear anywhere in it.
    let mut seen_versions = std::collections::BTreeSet::new();
    for fixture in blocks() {
        let block = parse(&fixture);
        for tx in &block.transactions {
            let version = tx.version();
            assert!(
                (1..=4).contains(&version),
                "{}: a transaction reports version {}, expected 1..=4",
                fixture.name,
                version
            );
            assert_eq!(
                tx.network_upgrade(),
                None,
                "{}: a v{} transaction reports a network upgrade, only v5 should",
                fixture.name,
                version
            );
            seen_versions.insert(version);
        }
    }
    assert!(
        seen_versions.len() >= 3,
        "expected the corpus to exercise at least three transaction versions, saw {:?}",
        seen_versions
    );
}

#[test]
fn the_overwinter_activation_blocks_transactions_are_version_three_and_overwintered() {
    let fixture = named("main-0347501");
    let block = parse(&fixture);
    assert!(
        !block.transactions.is_empty(),
        "{}: expected at least one transaction",
        fixture.name
    );
    for tx in &block.transactions {
        assert_eq!(
            tx.version(),
            3,
            "{}: expected a v3 transaction at the Overwinter, v3 transactions fixture",
            fixture.name
        );
        assert!(
            tx.is_overwintered(),
            "{}: a v3 transaction reports is_overwintered() == false",
            fixture.name
        );
    }
}

#[test]
fn blocks_before_overwinter_activation_are_not_overwintered() {
    for name in ["main-0000010", "main-0000202", "main-0347499"] {
        let fixture = named(name);
        let block = parse(&fixture);
        for tx in &block.transactions {
            assert!(
                !tx.is_overwintered(),
                "{}: a transaction before Overwinter activation reports is_overwintered() == true",
                fixture.name
            );
            assert!(
                tx.version() <= 2,
                "{}: a pre-Overwinter transaction reports version {}, expected 1 or 2",
                fixture.name,
                tx.version()
            );
        }
    }
}

#[test]
fn the_sapling_v4_blocks_transactions_are_version_four() {
    let fixture = named("main-0419201");
    let block = parse(&fixture);
    assert!(
        !block.transactions.is_empty(),
        "{}: expected at least one transaction",
        fixture.name
    );
    for tx in &block.transactions {
        assert_eq!(
            tx.version(),
            4,
            "{}: expected a v4 transaction at the Sapling, v4 transactions fixture",
            fixture.name
        );
    }
}

#[test]
fn has_sapling_shielded_data_agrees_with_the_spend_and_output_iterators_across_the_corpus() {
    // `has_sapling_shielded_data` is new; `sapling_spends_per_anchor` and
    // `sapling_outputs` are not. The two must agree on every transaction in
    // the corpus, or the new accessor is answering a different question than
    // the iterators it is supposed to summarize.
    let mut saw_true = 0usize;
    let mut saw_false = 0usize;
    for fixture in blocks() {
        let block = parse(&fixture);
        for tx in &block.transactions {
            let has_any = tx.sapling_spends_per_anchor().next().is_some()
                || tx.sapling_outputs().next().is_some();
            assert_eq!(
                tx.has_sapling_shielded_data(),
                has_any,
                "{}: has_sapling_shielded_data() disagrees with the spend/output iterators",
                fixture.name
            );
            if has_any {
                saw_true += 1;
            } else {
                saw_false += 1;
            }
        }
    }
    assert!(
        saw_true > 0 && saw_false > 0,
        "expected the corpus to have both shielded and non-shielded transactions, saw {} and {}",
        saw_true,
        saw_false
    );
}

#[test]
fn sprout_pool_added_values_has_exactly_one_entry_per_joinsplit_across_the_corpus() {
    // The accessor is documented to return the vpub_old of every JoinSplit,
    // regardless of version. joinsplit_count() is not new, and counts the
    // same JoinSplits by a different route (matching on the same fields);
    // the two must always agree on how many there are.
    let mut total_joinsplits = 0usize;
    for fixture in blocks() {
        let block = parse(&fixture);
        for tx in &block.transactions {
            let added_count = tx.sprout_pool_added_values().count();
            assert_eq!(
                added_count,
                tx.joinsplit_count(),
                "{}: sprout_pool_added_values() yielded {} entries, joinsplit_count() says {}",
                fixture.name,
                added_count,
                tx.joinsplit_count()
            );
            total_joinsplits += added_count;
        }
    }
    assert!(
        total_joinsplits > 0,
        "expected at least one JoinSplit somewhere in the Sprout-era corpus"
    );
}

#[test]
fn the_last_overwinter_block_moves_a_nonzero_value_into_the_sprout_pool() {
    // ZIP-211 (deployed at Canopy) requires vpub_old to be zero from the
    // activation height on. This block is the last one before Sapling --
    // long before that rule exists -- and is used here to show the accessor
    // surfacing real, historical, nonzero values: the exact quantity the rule
    // will later forbid. Four of its seven JoinSplits move value into the
    // pool; the other three move value only out, which is why the accessor
    // has to report a per-JoinSplit value and not just a count.
    let fixture = named("main-0419199");
    let block = parse(&fixture);
    let added: Vec<i64> = block
        .transactions
        .iter()
        .flat_map(|tx| tx.sprout_pool_added_values())
        .map(|amount| i64::from(*amount))
        .collect();

    assert_eq!(
        added.len(),
        7,
        "{}: the accessor yielded {} values for a block with seven JoinSplits",
        fixture.name,
        added.len()
    );
    assert_eq!(
        added.iter().filter(|value| **value > 0).count(),
        4,
        "{}: four of this block's seven JoinSplits have a nonzero vpub_old, the accessor \
         reports {:?}",
        fixture.name,
        added
    );
    assert_eq!(
        added.iter().sum::<i64>(),
        710_121_898,
        "{}: the total vpub_old this block adds to the Sprout pool has changed, the accessor \
         reports {:?}",
        fixture.name,
        added
    );
}

#[test]
fn the_testnet_canopy_activation_block_adds_no_value_to_the_sprout_pool() {
    // At the boundary itself: this corpus's Canopy activation block carries
    // no JoinSplits at all, so the accessor reports nothing to add -- the
    // same value the ZIP-211 rule would require, for a different reason than
    // the rule itself.
    let fixture = named("test-1028500");
    let block = parse(&fixture);
    let total: i64 = block
        .transactions
        .iter()
        .flat_map(|tx| tx.sprout_pool_added_values())
        .map(|amount| i64::from(*amount))
        .sum();
    assert_eq!(
        total, 0,
        "{}: expected no value added to the sprout pool at the Canopy activation block, got {}",
        fixture.name, total
    );
}

#[test]
fn orchard_flags_and_orchard_shielded_data_agree_and_are_absent_everywhere_in_this_corpus() {
    // orchard_flags is new; orchard_shielded_data is not. No v5 transaction
    // exists anywhere in this pre-NU5 corpus, so both must be absent on every
    // transaction, and must agree with each other about it.
    for fixture in blocks() {
        let block = parse(&fixture);
        for tx in &block.transactions {
            assert_eq!(
                tx.orchard_shielded_data().is_some(),
                tx.orchard_flags().is_some(),
                "{}: orchard_flags() and orchard_shielded_data() disagree about presence",
                fixture.name
            );
            assert_eq!(
                tx.orchard_flags(),
                None,
                "{}: a pre-NU5 transaction reports Orchard flags",
                fixture.name
            );
        }
    }
}
