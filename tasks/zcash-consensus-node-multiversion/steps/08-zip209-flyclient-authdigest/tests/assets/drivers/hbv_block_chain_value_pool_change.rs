//! `Block::chain_value_pool_change`, and the transaction-level pool accessors it
//! is built from.
//!
//! The chain value pool change of a block is the *negation* of the total
//! transaction value balance of its transactions.  Getting that sign backwards is
//! the single easiest way to implement this milestone wrongly, so most of this
//! group is about direction: value that leaves the Sprout pool must make the
//! Sprout chain pool go DOWN, a coinbase must make the transparent chain pool go
//! UP, and a shielded coinbase must make the Sapling chain pool go UP.
//!
//! Expected values come from `$HBV_FIXTURES/value_pools.tsv`, which was derived in
//! Python straight from the block bytes by walking the transaction wire format --
//! `vpub_old`/`vpub_new` out of each JoinSplit description, `valueBalanceSapling`
//! out of each v4 transaction, and the `tx_out` values out of each output.  No
//! expected number in this file was produced by the interface under test.
//!
//! Blocks whose transactions spend earlier transparent outputs cannot have their
//! pool change computed without those UTXOs, which are not in this corpus.  Every
//! assertion below is therefore made against the sub-block holding only the
//! transactions that spend no transparent output -- which always includes the
//! coinbase.  `value_pools.tsv` reports its size in the `clean_txs` column.

use std::collections::BTreeMap;
use std::collections::HashMap;
use std::io::Cursor;
use std::path::PathBuf;
use std::sync::Arc;

use zebra_chain::amount::{Amount, NegativeAllowed, NonNegative};
use zebra_chain::block::Block;
use zebra_chain::serialization::ZcashDeserialize;
use zebra_chain::transaction::Transaction;
use zebra_chain::transparent;
use zebra_chain::value_balance::ValueBalance;

// ---- fixtures -------------------------------------------------------------

fn fixture_dir() -> PathBuf {
    PathBuf::from(std::env::var("HBV_FIXTURES").expect("the verifier must export HBV_FIXTURES"))
}

fn rows(file: &str) -> Vec<BTreeMap<String, String>> {
    let path = fixture_dir().join(file);
    let text = std::fs::read_to_string(&path)
        .unwrap_or_else(|e| panic!("missing fixture {:?}: {}", path, e));
    let mut lines = text.lines();
    let header: Vec<String> = lines
        .next()
        .unwrap_or_else(|| panic!("{} is empty", file))
        .trim_start_matches("# ")
        .split('\t')
        .map(|s| s.to_string())
        .collect();
    let mut out = Vec::new();
    for line in lines {
        if line.trim().is_empty() {
            continue;
        }
        let row: BTreeMap<String, String> = header
            .iter()
            .cloned()
            .zip(line.split('\t').map(|s| s.to_string()))
            .collect();
        out.push(row);
    }
    out
}

/// One row of `value_pools.tsv`: the pool deltas a correct implementation must
/// produce for the "clean" sub-block of `name`.
struct Expected {
    name: String,
    tx_count: usize,
    clean_txs: usize,
    transparent: i64,
    sprout: i64,
    sapling: i64,
    orchard: i64,
    sapling_txs: usize,
    orchard_txs: usize,
    vpub_old: i64,
    vpub_new: i64,
}

fn expectations() -> Vec<Expected> {
    rows("value_pools.tsv")
        .into_iter()
        .map(|row| {
            let field = |key: &str| -> String {
                row.get(key)
                    .unwrap_or_else(|| panic!("value_pools.tsv has no {} column", key))
                    .clone()
            };
            let number = |key: &str| -> i64 {
                field(key)
                    .parse()
                    .unwrap_or_else(|e| panic!("{} is not a number: {}", key, e))
            };
            Expected {
                name: field("name"),
                tx_count: number("tx_count") as usize,
                clean_txs: number("clean_txs") as usize,
                transparent: number("transparent"),
                sprout: number("sprout"),
                sapling: number("sapling"),
                orchard: number("orchard"),
                sapling_txs: number("sapling_txs") as usize,
                orchard_txs: number("orchard_txs") as usize,
                vpub_old: number("vpub_old"),
                vpub_new: number("vpub_new"),
            }
        })
        .collect()
}

fn block(name: &str) -> Block {
    let path = fixture_dir().join("blocks").join(format!("{}.hex", name));
    let text = std::fs::read_to_string(&path)
        .unwrap_or_else(|e| panic!("missing block fixture {:?}: {}", path, e));
    let raw = hex::decode(text.trim())
        .unwrap_or_else(|e| panic!("block fixture {} is not hex: {}", name, e));
    Block::zcash_deserialize(Cursor::new(&raw[..]))
        .unwrap_or_else(|e| panic!("block fixture {} did not parse: {:?}", name, e))
}

fn spends_transparent(tx: &Transaction) -> bool {
    tx.inputs()
        .iter()
        .any(|input| matches!(input, transparent::Input::PrevOut { .. }))
}

/// The sub-block of `full` holding only the transactions that spend no
/// transparent output.
fn clean(full: &Block) -> Block {
    let transactions: Vec<Arc<Transaction>> = full
        .transactions
        .iter()
        .filter(|tx| !spends_transparent(tx))
        .cloned()
        .collect();
    Block {
        header: full.header.clone(),
        transactions,
    }
}

fn no_utxos() -> HashMap<transparent::OutPoint, transparent::Utxo> {
    HashMap::new()
}

fn zatoshis<C>(amount: Amount<C>) -> i64 {
    i64::from_le_bytes(amount.to_bytes())
}

fn pools(balance: ValueBalance<NegativeAllowed>) -> (i64, i64, i64, i64) {
    (
        zatoshis(balance.transparent_amount()),
        zatoshis(balance.sprout_amount()),
        zatoshis(balance.sapling_amount()),
        zatoshis(balance.orchard_amount()),
    )
}

fn change_of(name: &str) -> (i64, i64, i64, i64) {
    let sub = clean(&block(name));
    pools(
        sub.chain_value_pool_change(&no_utxos())
            .unwrap_or_else(|e| panic!("{} has no computable pool change: {:?}", name, e)),
    )
}

// ---- the pool deltas ------------------------------------------------------

#[test]
fn the_chain_value_pool_change_matches_the_deltas_derived_from_the_block_bytes() {
    let expectations = expectations();
    assert_eq!(
        expectations.len(),
        17,
        "value_pools.tsv must cover all 17 corpus blocks"
    );

    for expected in &expectations {
        let full = block(&expected.name);
        assert_eq!(
            full.transactions.len(),
            expected.tx_count,
            "{} should hold {} transactions",
            expected.name,
            expected.tx_count
        );
        let sub = clean(&full);
        assert_eq!(
            sub.transactions.len(),
            expected.clean_txs,
            "{} should hold {} transactions that spend no transparent output",
            expected.name,
            expected.clean_txs
        );

        let change = sub
            .chain_value_pool_change(&no_utxos())
            .unwrap_or_else(|e| panic!("{} has no computable pool change: {:?}", expected.name, e));
        assert_eq!(
            pools(change),
            (
                expected.transparent,
                expected.sprout,
                expected.sapling,
                expected.orchard
            ),
            "{}: chain value pool change (transparent, sprout, sapling, orchard)",
            expected.name
        );
    }
}

#[test]
fn the_chain_value_pool_change_is_the_negation_of_the_transaction_value_balance() {
    // Rebuilt here without calling chain_value_pool_change: for a block whose
    // transactions spend no transparent output, the transaction value balance is
    //     transparent: 0 - (sum of tx_out values)
    //     sprout:      (sum of vpub_new) - (sum of vpub_old)
    // and the chain value pool change is its negation.
    for expected in expectations() {
        let sub = clean(&block(&expected.name));

        let outputs: Amount<NonNegative> = sub
            .transactions
            .iter()
            .flat_map(|tx| tx.outputs().iter().map(|output| output.value()))
            .sum::<Result<Amount<NonNegative>, _>>()
            .unwrap_or_else(|e| panic!("{}: output values overflowed: {:?}", expected.name, e));

        let to_sprout: i64 = sub
            .transactions
            .iter()
            .flat_map(|tx| tx.output_values_to_sprout())
            .map(|value| zatoshis(*value))
            .sum();
        let from_sprout: i64 = sub
            .transactions
            .iter()
            .flat_map(|tx| tx.input_values_from_sprout())
            .map(|value| zatoshis(*value))
            .sum();

        let change = sub
            .chain_value_pool_change(&no_utxos())
            .unwrap_or_else(|e| panic!("{}: no computable pool change: {:?}", expected.name, e));

        assert_eq!(
            zatoshis(change.transparent_amount()),
            zatoshis(outputs),
            "{}: every newly created transparent output is added to the \
             transparent chain pool, so the change is +outputs, not -outputs",
            expected.name
        );
        assert_eq!(
            zatoshis(change.sprout_amount()),
            to_sprout - from_sprout,
            "{}: the sprout chain pool change is vpub_old - vpub_new, the \
             negation of the transaction's sprout value balance",
            expected.name
        );
    }
}

#[test]
fn a_coinbase_only_block_changes_only_the_transparent_pool() {
    // For a block whose only transaction is the coinbase, the whole block and its
    // clean sub-block are the same thing, so no filtering is involved.
    let mut checked = 0;
    for expected in expectations() {
        if expected.tx_count != 1 {
            continue;
        }
        checked += 1;
        let full = block(&expected.name);
        let change = full
            .chain_value_pool_change(&no_utxos())
            .unwrap_or_else(|e| panic!("{}: no computable pool change: {:?}", expected.name, e));
        assert_eq!(
            pools(change),
            (expected.transparent, 0, 0, 0),
            "{} is coinbase-only: all of its value goes to the transparent pool",
            expected.name
        );
        assert!(
            expected.transparent >= 0,
            "{} mints value, so its transparent change cannot be negative",
            expected.name
        );
    }
    assert_eq!(
        checked, 9,
        "the corpus holds nine single-transaction blocks; \
         got {} -- the fixture or the filter changed",
        checked
    );

    // Both genesis blocks pay a zero-value coinbase, so they move nothing at all.
    assert_eq!(
        change_of("main-0000000"),
        (0, 0, 0, 0),
        "the mainnet genesis coinbase pays zero and changes no pool"
    );
    assert_eq!(
        change_of("test-0000000"),
        (0, 0, 0, 0),
        "the testnet genesis coinbase pays zero and changes no pool"
    );
}

#[test]
fn a_block_with_no_transactions_changes_no_pool() {
    let empty = Block {
        header: block("main-0000010").header.clone(),
        transactions: Vec::new(),
    };
    assert_eq!(
        pools(
            empty
                .chain_value_pool_change(&no_utxos())
                .expect("an empty block has a zero pool change")
        ),
        (0, 0, 0, 0),
        "a block with no transactions leaves every chain value pool alone"
    );
}

// ---- direction, pool by pool ----------------------------------------------

#[test]
fn spending_a_joinsplit_takes_value_out_of_the_sprout_chain_pool() {
    // Each of these blocks contains JoinSplits that unshield value: their
    // `vpub_new` totals are positive, so the Sprout chain pool must shrink.
    let cases: [(&str, i64); 4] = [
        ("main-0347499", -2_239_320_000),
        ("main-0347501", -133_380_000),
        ("main-0419199", -322_194_989_063),
        ("main-0419201", -133_720_000),
    ];

    for &(name, sprout) in cases.iter() {
        let change = change_of(name);
        assert_eq!(
            change.1, sprout,
            "{}: unshielding from Sprout must give a negative sprout pool change",
            name
        );
        assert!(
            change.0 > 0,
            "{}: the unshielded value reappears in the transparent pool, got {}",
            name,
            change.0
        );
    }

    // And a block with no JoinSplits at all leaves the Sprout pool untouched.
    assert_eq!(
        change_of("main-0653600").1,
        0,
        "main-0653600 has no JoinSplits, so its sprout pool change is zero"
    );
}

#[test]
fn a_shielded_coinbase_adds_its_output_to_the_sapling_chain_pool() {
    // Mainnet 982681 is post-Heartwood, where ZIP-213 lets the coinbase pay to a
    // shielded address.  Its coinbase pays 1.25 ZEC transparently and the rest
    // into Sapling.  Both parts are *additions* to their chain pools.
    let change = change_of("main-0982681");
    assert_eq!(
        change,
        (125_000_000, 0, 500_026_380, 0),
        "main-0982681's shielded coinbase credits both the transparent and the \
         sapling chain pools"
    );
    assert_eq!(
        change.0 + change.2,
        625_026_380,
        "the coinbase total is the post-Blossom 6.25 ZEC block subsidy plus \
         26380 zatoshi of fees"
    );
}

#[test]
fn a_sprout_to_sapling_migration_moves_value_between_the_two_shielded_pools() {
    // Mainnet 903000 contains migration transactions that unshield from Sprout and
    // reshield into Sapling.  One pool goes down by exactly as much as the other
    // goes up, less the fee that lands in the transparent pool.
    let (transparent, sprout, sapling, orchard) = change_of("main-0903000");
    assert_eq!(
        (sprout, sapling),
        (-610_000_000, 609_970_000),
        "main-0903000 moves 6.1 ZEC out of Sprout and 6.0997 ZEC into Sapling"
    );
    assert_eq!(
        sprout + sapling,
        -30_000,
        "the 30000 zatoshi difference between the two shielded pools is the fee"
    );
    assert_eq!(
        transparent, 625_201_380,
        "the migration fees and the block subsidy land in the transparent pool"
    );
    assert_eq!(orchard, 0, "there is no Orchard value before NU5");

    // The reverse direction: mainnet 903001 unshields 250 zatoshi from Sapling.
    assert_eq!(
        change_of("main-0903001"),
        (625_107_615, 0, -250, 0),
        "main-0903001 takes 250 zatoshi out of the Sapling chain pool"
    );
}

// ---- the transaction-level accessors --------------------------------------

#[test]
fn the_sprout_value_iterators_report_vpub_old_and_vpub_new_separately() {
    for expected in expectations() {
        let full = block(&expected.name);

        let to_sprout: i64 = full
            .transactions
            .iter()
            .flat_map(|tx| tx.output_values_to_sprout())
            .map(|value| zatoshis(*value))
            .sum();
        let from_sprout: i64 = full
            .transactions
            .iter()
            .flat_map(|tx| tx.input_values_from_sprout())
            .map(|value| zatoshis(*value))
            .sum();

        assert_eq!(
            to_sprout, expected.vpub_old,
            "{}: output_values_to_sprout must yield the JoinSplit vpub_old fields",
            expected.name
        );
        assert_eq!(
            from_sprout, expected.vpub_new,
            "{}: input_values_from_sprout must yield the JoinSplit vpub_new fields",
            expected.name
        );
    }

    // The two iterators are not the same iterator: mainnet 419199 has a nonzero
    // total in each of them, and they differ.
    let b = block("main-0419199");
    let old: i64 = b
        .transactions
        .iter()
        .flat_map(|tx| tx.output_values_to_sprout())
        .map(|v| zatoshis(*v))
        .sum();
    let new: i64 = b
        .transactions
        .iter()
        .flat_map(|tx| tx.input_values_from_sprout())
        .map(|v| zatoshis(*v))
        .sum();
    assert_eq!(old, 710_121_898, "main-0419199 shields 7.10121898 ZEC into Sprout");
    assert_eq!(
        new, 322_194_989_063,
        "main-0419199 unshields 3221.94989063 ZEC out of Sprout"
    );
}

#[test]
fn sapling_transactions_count_counts_transactions_with_sapling_spends_or_outputs() {
    let mut nonzero = 0;
    for expected in expectations() {
        let full = block(&expected.name);
        assert_eq!(
            full.sapling_transactions_count(),
            expected.sapling_txs as u64,
            "{}: sapling_transactions_count counts the transactions with at least \
             one Sapling spend or Sapling output",
            expected.name
        );
        if expected.sapling_txs > 0 {
            nonzero += 1;
        }
    }
    assert_eq!(
        nonzero, 4,
        "four corpus blocks contain Sapling transactions"
    );

    // Spot checks, so a count that is always zero or always the transaction count
    // cannot pass.
    assert_eq!(
        block("main-0419201").sapling_transactions_count(),
        5,
        "main-0419201 holds 10 transactions, 5 of them with Sapling data"
    );
    assert_eq!(
        block("main-0903000").sapling_transactions_count(),
        3,
        "main-0903000 holds 15 transactions, 3 of them with Sapling data"
    );
    assert_eq!(
        block("main-0419200").sapling_transactions_count(),
        0,
        "the Sapling activation block itself has no Sapling transactions"
    );
}

#[test]
fn no_pre_nu5_block_reports_orchard_transactions_or_orchard_shielded_data() {
    for expected in expectations() {
        let full = block(&expected.name);
        assert_eq!(
            full.orchard_transactions_count(),
            expected.orchard_txs as u64,
            "{}: no corpus block is at or after NU5, so it has no Orchard transactions",
            expected.name
        );
        for (index, tx) in full.transactions.iter().enumerate() {
            assert!(
                !tx.has_orchard_shielded_data(),
                "{} transaction {} is pre-NU5 and cannot carry an Orchard bundle",
                expected.name,
                index
            );
        }
    }
}
