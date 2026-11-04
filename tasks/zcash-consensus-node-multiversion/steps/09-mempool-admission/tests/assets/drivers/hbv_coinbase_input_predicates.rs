//! The two coinbase-input predicates a mempool needs to tell apart.
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! Before this milestone the node only ever saw transactions that were already
//! inside a block, and one question was enough: "is this the block's coinbase
//! transaction?". A mempool asks a second, different question. A coinbase
//! transaction may never be gossiped or admitted to the mempool, and the rule
//! that enforces that is not "does this look like a well-formed coinbase
//! transaction" but "does this transaction touch a coinbase input at all" --
//! because a malformed transaction that merely *contains* a coinbase input is
//! exactly the thing an attacker would submit to slip past a check written the
//! first way.
//!
//! So `Transaction` carries two predicates at this milestone, and they are not
//! the same function:
//!
//!   * `has_valid_coinbase_transaction_inputs()` -- this transaction has
//!     exactly one input and that input is a coinbase input. This is the block
//!     question: the shape a block's first transaction must have.
//!   * `has_any_coinbase_inputs()` -- at least one of this transaction's
//!     inputs is a coinbase input, whatever else is in the list. This is the
//!     mempool question.
//!
//! They agree on every transaction that has ever been mined, which is why the
//! corpus alone cannot separate them; the constructed cases below do, by
//! reusing a real coinbase input and a real spending input from the corpus in
//! the same input list.
//!
//! Expected values come from the shipped corpus of seventeen real blocks and
//! from the published ZIP-244 v5 vectors, read with an input-list walk written
//! here that shares no code with the predicates under test.

use std::collections::BTreeMap;
use std::io::Cursor;
use std::path::PathBuf;

use zebra_chain::block::Block;
use zebra_chain::serialization::ZcashDeserialize;
use zebra_chain::transaction::{LockTime, Transaction};
use zebra_chain::transparent;

// ---------------------------------------------------------------- fixtures

fn fixture_dir() -> PathBuf {
    PathBuf::from(
        std::env::var("HBV_FIXTURES")
            .expect("HBV_FIXTURES must point at the shipped fixture directory"),
    )
}

/// Every row of a shipped TSV, as a column-name -> value map.
fn rows(file: &str) -> Vec<BTreeMap<String, String>> {
    let text = std::fs::read_to_string(fixture_dir().join(file))
        .unwrap_or_else(|e| panic!("cannot read {}: {}", file, e));
    let mut lines = text.lines();
    let header: Vec<&str> = lines
        .next()
        .unwrap_or_else(|| panic!("{} has no header row", file))
        .trim_start_matches("# ")
        .split('\t')
        .collect();

    let mut out = Vec::new();
    for line in lines {
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        let cells: Vec<&str> = line.split('\t').collect();
        assert_eq!(cells.len(), header.len(), "{}: malformed row {}", file, line);
        let row: BTreeMap<&str, &str> = header.iter().cloned().zip(cells).collect();
        out.push(
            row.into_iter()
                .map(|(k, v)| (k.to_string(), v.to_string()))
                .collect(),
        );
    }
    assert!(!out.is_empty(), "{} has no data rows", file);
    out
}

/// One real block from the shipped corpus.
struct BlockFixture {
    name: String,
    block: Block,
}

fn corpus() -> Vec<BlockFixture> {
    let dir = fixture_dir();
    let mut out = Vec::new();
    for row in rows("blocks.tsv") {
        let name = row
            .get("name")
            .expect("blocks.tsv has a name column")
            .clone();
        let text = std::fs::read_to_string(dir.join("blocks").join(format!("{}.hex", name)))
            .unwrap_or_else(|e| panic!("cannot read block {}: {}", name, e));
        let raw = hex::decode(text.trim()).expect("block fixture is hex");
        let block = Block::zcash_deserialize(Cursor::new(&raw[..]))
            .unwrap_or_else(|e| panic!("{} did not parse: {:?}", name, e));
        out.push(BlockFixture { name, block });
    }
    // Anti-degeneracy guard only: this is a property of the shipped corpus, not
    // of the code under test. A failure here means blocks.tsv changed.
    assert_eq!(
        out.len(),
        17,
        "the corpus has {} blocks; the verifier expects the shipped 17",
        out.len()
    );
    let multi = out
        .iter()
        .filter(|f| f.block.transactions.len() > 1)
        .count();
    assert!(
        multi >= 5,
        "only {} corpus blocks have more than one transaction; \
         without them the two predicates are never asked about a non-coinbase transaction",
        multi
    );
    out
}

/// One published ZIP-244 v5 vector.
struct V5Fixture {
    name: String,
    tx: Transaction,
}

fn v5_vectors() -> Vec<V5Fixture> {
    let mut out = Vec::new();
    for row in rows("zip244.tsv") {
        let name = row
            .get("name")
            .expect("zip244.tsv has a name column")
            .clone();
        let raw = hex::decode(row.get("tx_hex").expect("zip244.tsv has a tx_hex column"))
            .unwrap_or_else(|e| panic!("{}: tx_hex is not hex: {}", name, e));
        let tx = Transaction::zcash_deserialize(Cursor::new(&raw[..]))
            .unwrap_or_else(|e| panic!("{}: the v5 vector did not parse: {:?}", name, e));
        out.push(V5Fixture { name, tx });
    }
    // Anti-degeneracy guard only; see above.
    assert_eq!(
        out.len(),
        10,
        "zip244.tsv has {} rows; the verifier expects the shipped 10",
        out.len()
    );
    out
}

// ----------------------------------------------------------------- helpers

/// An independent reading of an input list: how many of these inputs are
/// coinbase inputs. Shares no code with the predicates under test -- it only
/// matches on the public `transparent::Input` variants.
fn count_coinbase_inputs(inputs: &[transparent::Input]) -> usize {
    inputs
        .iter()
        .filter(|input| match input {
            transparent::Input::Coinbase { .. } => true,
            transparent::Input::PrevOut { .. } => false,
        })
        .count()
}

/// The first coinbase input in the corpus, cloned. Every block's first
/// transaction has one, so this always succeeds.
fn a_real_coinbase_input() -> transparent::Input {
    for fixture in corpus() {
        for tx in fixture.block.transactions.iter() {
            for input in tx.inputs().iter() {
                if let transparent::Input::Coinbase { .. } = input {
                    return input.clone();
                }
            }
        }
    }
    panic!("the corpus contains no coinbase input");
}

/// The first spending (`PrevOut`) input in the corpus, cloned.
fn a_real_prevout_input() -> transparent::Input {
    for fixture in corpus() {
        for tx in fixture.block.transactions.iter() {
            for input in tx.inputs().iter() {
                if let transparent::Input::PrevOut { .. } = input {
                    return input.clone();
                }
            }
        }
    }
    panic!("the corpus contains no spending input");
}

/// A transparent v1 transaction with exactly the given inputs and no outputs.
fn v1_with_inputs(inputs: Vec<transparent::Input>) -> Transaction {
    Transaction::V1 {
        inputs,
        outputs: Vec::new(),
        lock_time: LockTime::Height(zebra_chain::block::Height(0)),
    }
}

// ------------------------------------------------------------------- tests

#[test]
fn the_first_transaction_of_every_block_is_the_only_one_with_coinbase_inputs() {
    for fixture in corpus() {
        for (index, tx) in fixture.block.transactions.iter().enumerate() {
            let valid = tx.has_valid_coinbase_transaction_inputs();
            let any = tx.has_any_coinbase_inputs();

            if index == 0 {
                assert!(
                    valid,
                    "{}: transaction 0 does not have valid coinbase transaction inputs",
                    fixture.name
                );
                assert!(
                    any,
                    "{}: transaction 0 does not report any coinbase inputs",
                    fixture.name
                );
            } else {
                assert!(
                    !valid,
                    "{}: transaction {} was reported as a valid coinbase transaction",
                    fixture.name, index
                );
                assert!(
                    !any,
                    "{}: transaction {} was reported as containing a coinbase input",
                    fixture.name, index
                );
            }
        }
    }
}

#[test]
fn both_predicates_match_an_independent_walk_of_the_input_list() {
    for fixture in corpus() {
        for (index, tx) in fixture.block.transactions.iter().enumerate() {
            let inputs = tx.inputs();
            let coinbase_inputs = count_coinbase_inputs(inputs);

            let expected_valid = inputs.len() == 1 && coinbase_inputs == 1;
            let expected_any = coinbase_inputs > 0;

            assert_eq!(
                tx.has_valid_coinbase_transaction_inputs(),
                expected_valid,
                "{}#{}: has_valid_coinbase_transaction_inputs disagrees with an independent \
                 walk ({} inputs, {} of them coinbase)",
                fixture.name,
                index,
                inputs.len(),
                coinbase_inputs
            );
            assert_eq!(
                tx.has_any_coinbase_inputs(),
                expected_any,
                "{}#{}: has_any_coinbase_inputs disagrees with an independent walk \
                 ({} inputs, {} of them coinbase)",
                fixture.name,
                index,
                inputs.len(),
                coinbase_inputs
            );
        }
    }
}

#[test]
fn a_lone_coinbase_input_satisfies_both_predicates() {
    let tx = v1_with_inputs(vec![a_real_coinbase_input()]);

    assert!(
        tx.has_valid_coinbase_transaction_inputs(),
        "a transaction with exactly one coinbase input is a valid coinbase transaction"
    );
    assert!(
        tx.has_any_coinbase_inputs(),
        "a transaction with exactly one coinbase input contains a coinbase input"
    );
}

#[test]
fn a_coinbase_input_beside_a_spending_input_separates_the_two_predicates() {
    // The case the mempool rule exists for. `zcashd` and Zebra both reject
    // this transaction outright, but only the second predicate can see it.
    let coinbase = a_real_coinbase_input();
    let prevout = a_real_prevout_input();

    let coinbase_first = v1_with_inputs(vec![coinbase.clone(), prevout.clone()]);
    let coinbase_second = v1_with_inputs(vec![prevout.clone(), coinbase.clone()]);

    for (label, tx) in &[
        ("coinbase input first", &coinbase_first),
        ("coinbase input second", &coinbase_second),
    ] {
        assert!(
            !tx.has_valid_coinbase_transaction_inputs(),
            "{}: a two-input transaction must not be a valid coinbase transaction",
            label
        );
        assert!(
            tx.has_any_coinbase_inputs(),
            "{}: a transaction with a coinbase input among its inputs must report one",
            label
        );
    }
}

#[test]
fn two_coinbase_inputs_are_not_a_valid_coinbase_transaction() {
    let coinbase = a_real_coinbase_input();
    let tx = v1_with_inputs(vec![coinbase.clone(), coinbase.clone()]);

    assert!(
        !tx.has_valid_coinbase_transaction_inputs(),
        "a transaction with two coinbase inputs must not be a valid coinbase transaction: \
         the rule is exactly one input, not at least one"
    );
    assert!(
        tx.has_any_coinbase_inputs(),
        "a transaction with two coinbase inputs contains a coinbase input"
    );
}

#[test]
fn a_transaction_with_only_spending_inputs_satisfies_neither_predicate() {
    let prevout = a_real_prevout_input();

    for count in 1..4usize {
        let mut inputs = Vec::new();
        for _ in 0..count {
            inputs.push(prevout.clone());
        }
        let tx = v1_with_inputs(inputs);

        assert!(
            !tx.has_valid_coinbase_transaction_inputs(),
            "{} spending inputs must not be a valid coinbase transaction",
            count
        );
        assert!(
            !tx.has_any_coinbase_inputs(),
            "{} spending inputs must not report a coinbase input",
            count
        );
    }
}

#[test]
fn a_transaction_with_no_inputs_at_all_satisfies_neither_predicate() {
    // A fully shielded transaction has an empty transparent input list. It is
    // not a coinbase transaction, and it does not touch a coinbase input.
    let tx = v1_with_inputs(Vec::new());

    assert!(
        !tx.has_valid_coinbase_transaction_inputs(),
        "a transaction with no inputs must not be a valid coinbase transaction"
    );
    assert!(
        !tx.has_any_coinbase_inputs(),
        "a transaction with no inputs must not report a coinbase input"
    );
}

#[test]
fn no_published_v5_vector_touches_a_coinbase_input() {
    // A corpus property, checked here so a later failure is read as a fixture
    // change: none of the ten published ZIP-244 vectors has a null-prevout
    // input, and six of them have no transparent inputs at all.
    let mut inputless = 0usize;
    for fixture in v5_vectors() {
        let inputs = fixture.tx.inputs();
        if inputs.is_empty() {
            inputless += 1;
        }
        assert_eq!(
            count_coinbase_inputs(inputs),
            0,
            "{}: the published v5 vectors have no coinbase inputs",
            fixture.name
        );
        assert!(
            !fixture.tx.has_valid_coinbase_transaction_inputs(),
            "{}: a published v5 vector was reported as a valid coinbase transaction",
            fixture.name
        );
        assert!(
            !fixture.tx.has_any_coinbase_inputs(),
            "{}: a published v5 vector was reported as containing a coinbase input",
            fixture.name
        );
    }
    assert!(
        inputless >= 5,
        "only {} published vectors have an empty transparent input list; \
         the empty-input case is no longer covered",
        inputless
    );
}

#[test]
fn the_genesis_coinbase_is_still_recognised_as_a_coinbase_transaction() {
    // The 77-byte genesis coinbase is the odd one out at every other layer, so
    // it is checked separately: its single input is still a coinbase input.
    for fixture in corpus() {
        if fixture.block.coinbase_height() != Some(zebra_chain::block::Height(0)) {
            continue;
        }
        let genesis_coinbase = &fixture.block.transactions[0];
        assert!(
            genesis_coinbase.has_valid_coinbase_transaction_inputs(),
            "{}: the genesis coinbase is not a valid coinbase transaction",
            fixture.name
        );
        assert!(
            genesis_coinbase.has_any_coinbase_inputs(),
            "{}: the genesis coinbase does not report a coinbase input",
            fixture.name
        );
        assert_eq!(
            genesis_coinbase.inputs().len(),
            1,
            "{}: the genesis coinbase has more than one input",
            fixture.name
        );
    }
}
