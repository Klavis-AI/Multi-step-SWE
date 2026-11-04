//! What one transaction does to each of the four Zcash value pools.
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! `ValueBalance` is the ledger type; this is the thing that fills it in.
//! Before this milestone a `Transaction` could tell you its inputs, its
//! outputs and its nullifiers, but it could not tell you how much value it
//! moved, because three of the four answers are not in the transaction on its
//! own: the transparent one needs the UTXOs the inputs spend, which live in
//! the chain state. `Transaction::value_balance(&utxos)` is the first method
//! on a transaction that takes chain context as an argument, and that is what
//! makes this milestone "contextual".
//!
//! The assertions bite because each pool has its own sign convention and they
//! do not agree with each other:
//!
//!   * transparent  =  (value of the UTXOs spent) - (value of outputs created)
//!   * sprout       =  + sum over joinsplits of (vpub_old - vpub_new)
//!   * sapling      =  - sum of the `valueBalanceSapling` fields
//!   * orchard      =  - sum of the `valueBalanceOrchard` fields
//!
//! Two of the four are negated and two are not. An implementation that
//! negates all four, or none, still produces a plausible-looking
//! `ValueBalance` and still satisfies every property that only looks at one
//! transaction's transparent pool. So every pool is recomputed here from a
//! *different* public entry point than the one under test -- the transparent
//! pool from `Input::outpoint`/`Output::value` and the supplied UTXO map, the
//! Sprout pool from `JoinSplitData::value_balance`, the Sapling pool from the
//! public `ShieldedData::value_balance` field reached through the public
//! `Transaction::V4`/`V5` variants, and the Orchard pool from
//! `Transaction::orchard_shielded_data` -- and then compared with what
//! `Transaction::value_balance` reports. No expected number in this file is
//! produced by calling `value_balance` twice.
//!
//! The corpus is the seventeen real mainnet and testnet blocks shipped in
//! `$HBV_FIXTURES`. The UTXOs the transparent inputs spend are not in those
//! blocks -- they are in earlier blocks that are not in the corpus -- so they
//! are synthesised here with chosen values. That is deliberate: it makes the
//! transparent expectation exact arithmetic on numbers this file picked,
//! rather than a value read back out of the implementation.

use std::collections::{BTreeMap, HashMap};
use std::convert::TryFrom;
use std::path::PathBuf;

use zebra_chain::amount::{Amount, NegativeAllowed, NonNegative, MAX_MONEY};
use zebra_chain::block::{Block, Height};
use zebra_chain::serialization::ZcashDeserialize;
use zebra_chain::transaction::{LockTime, Transaction};
use zebra_chain::transparent::{self, OutPoint, Output, Script, Utxo};

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
fn fixtures() -> Vec<Fixture> {
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
        out.push(Fixture { name, raw });
    }
    assert!(
        out.len() >= 17,
        "the shipped corpus has {} blocks; the verifier expects at least 17",
        out.len()
    );
    out
}

/// Every transaction in the corpus, tagged with its block and its index there.
fn corpus() -> Vec<(String, usize, Transaction)> {
    let mut out = Vec::new();
    for fixture in fixtures() {
        let block = Block::zcash_deserialize(&fixture.raw[..]).unwrap_or_else(|e| {
            panic!(
                "fixture {} must deserialize as a block: {:?}",
                fixture.name, e
            )
        });
        for (index, transaction) in block.transactions.iter().enumerate() {
            out.push((fixture.name.clone(), index, (**transaction).clone()));
        }
    }
    assert!(
        out.len() >= 60,
        "the corpus must hold at least sixty transactions, found {}",
        out.len()
    );
    out
}

// ---------------------------------------------------------------- helpers

fn amt(value: i64) -> Amount<NegativeAllowed> {
    Amount::<NegativeAllowed>::try_from(value)
        .unwrap_or_else(|e| panic!("{} is a valid signed zatoshi amount: {:?}", value, e))
}

fn nonneg(value: i64) -> Amount<NonNegative> {
    Amount::<NonNegative>::try_from(value)
        .unwrap_or_else(|e| panic!("{} is a valid unsigned zatoshi amount: {:?}", value, e))
}

/// A UTXO holding exactly `value` zatoshi, spendable by an empty script.
///
/// The script is irrelevant to a value balance: nothing here runs it.
fn utxo_worth(value: i64, height: u32, from_coinbase: bool) -> Utxo {
    Utxo {
        output: Output {
            value: nonneg(value),
            lock_script: Script::new(&[]),
        },
        height: Height(height),
        from_coinbase,
    }
}

/// Synthesise one UTXO for each transparent input of `transaction`.
///
/// Values are `base`, `base + 1`, `base + 2`, ... so that swapping two inputs,
/// dropping one, or counting one twice changes the total.
fn utxos_for(transaction: &Transaction, base: i64) -> HashMap<OutPoint, Utxo> {
    let mut utxos = HashMap::new();
    for (i, input) in transaction.inputs().iter().enumerate() {
        if let Some(outpoint) = input.outpoint() {
            utxos.insert(outpoint, utxo_worth(base + i as i64, 1, false));
        }
    }
    utxos
}

/// The transparent pool change, recomputed from the input outpoints, the
/// supplied UTXO map and the output values -- never from `value_balance`.
fn expected_transparent(transaction: &Transaction, utxos: &HashMap<OutPoint, Utxo>) -> i64 {
    let mut total: i64 = 0;
    for input in transaction.inputs() {
        match input.outpoint() {
            // A coinbase input creates value rather than spending it, so it
            // contributes nothing to the transparent pool.
            None => {}
            Some(outpoint) => {
                let utxo = utxos.get(&outpoint).unwrap_or_else(|| {
                    panic!("the synthesised UTXO set must cover {:?}", outpoint)
                });
                total += i64::from(utxo.output.value);
            }
        }
    }
    for output in transaction.outputs() {
        total -= i64::from(output.value);
    }
    total
}

/// The Sprout pool change, recomputed through `JoinSplitData::value_balance`.
///
/// Sprout is *not* negated: value flows into the transparent pool when
/// `vpub_old` is spent out of the shielded pool.
fn expected_sprout(transaction: &Transaction) -> i64 {
    let balance = match transaction {
        Transaction::V2 { joinsplit_data, .. } | Transaction::V3 { joinsplit_data, .. } => {
            joinsplit_data.as_ref().map(|data| {
                data.value_balance()
                    .expect("a mined transaction's joinsplit total is in range")
            })
        }
        Transaction::V4 { joinsplit_data, .. } => joinsplit_data.as_ref().map(|data| {
            data.value_balance()
                .expect("a mined transaction's joinsplit total is in range")
        }),
        // V1 predates Sprout; V5 forbids JoinSplits (ZIP-211).
        Transaction::V1 { .. } | Transaction::V5 { .. } => None,
    };
    balance.map_or(0, i64::from)
}

/// The Sapling pool change, recomputed through the public `value_balance`
/// field of `sapling::ShieldedData`.
///
/// Sapling *is* negated: a positive `valueBalanceSapling` moves value out of
/// the Sapling pool and into the transparent one.
fn expected_sapling(transaction: &Transaction) -> i64 {
    let field = match transaction {
        Transaction::V4 {
            sapling_shielded_data,
            ..
        } => sapling_shielded_data
            .as_ref()
            .map_or(0, |data| i64::from(data.value_balance)),
        Transaction::V5 {
            sapling_shielded_data,
            ..
        } => sapling_shielded_data
            .as_ref()
            .map_or(0, |data| i64::from(data.value_balance)),
        Transaction::V1 { .. } | Transaction::V2 { .. } | Transaction::V3 { .. } => 0,
    };
    -field
}

/// The Orchard pool change, recomputed through
/// `Transaction::orchard_shielded_data`. Negated, like Sapling.
fn expected_orchard(transaction: &Transaction) -> i64 {
    -transaction
        .orchard_shielded_data()
        .map_or(0, |data| i64::from(data.value_balance()))
}

// ---------------------------------------------------------------- tests

#[test]
fn a_coinbase_transaction_only_moves_the_transparent_pool_and_needs_no_utxos() {
    let mut checked = 0;
    let mut paying = 0;
    let mut shielded_coinbases = 0;

    for fixture in fixtures() {
        let block = Block::zcash_deserialize(&fixture.raw[..])
            .unwrap_or_else(|e| panic!("fixture {} deserializes: {:?}", fixture.name, e));
        let coinbase = &block.transactions[0];
        assert!(
            coinbase.is_coinbase(),
            "the first transaction of {} must be the coinbase",
            fixture.name
        );

        // A coinbase transaction spends nothing, so an empty UTXO set must be
        // enough: `value_balance` must not look up a coinbase input.
        let empty: HashMap<OutPoint, Utxo> = HashMap::new();
        let balance = coinbase
            .value_balance(&empty)
            .unwrap_or_else(|e| panic!("{} coinbase value balance: {:?}", fixture.name, e));

        let created: i64 = coinbase
            .outputs()
            .iter()
            .map(|output| i64::from(output.value))
            .sum();

        assert_eq!(
            balance.transparent_amount(),
            amt(-created),
            "the coinbase of {} creates {} zatoshi, so the transparent pool must fall by that \
             much, got {:?}",
            fixture.name,
            created,
            balance.transparent_amount()
        );
        // A coinbase transaction never has JoinSplits: Sprout outputs are
        // not reachable from a coinbase in any network upgrade.
        assert_eq!(
            coinbase.joinsplit_count(),
            0,
            "a coinbase transaction must carry no JoinSplits ({})",
            fixture.name
        );
        assert_eq!(
            balance.sprout_amount(),
            amt(0),
            "a coinbase transaction must not move the Sprout pool ({})",
            fixture.name
        );

        // From Heartwood on, a coinbase MAY pay its reward to a Sapling
        // address (ZIP-213), so the Sapling pool of a coinbase is whatever its
        // shielded data says and not automatically zero.
        let shielded = expected_sapling(coinbase) + expected_orchard(coinbase);
        assert_eq!(
            balance.sapling_amount(),
            amt(expected_sapling(coinbase)),
            "the Sapling pool change of the coinbase of {} must be the negation of its \
             valueBalanceSapling",
            fixture.name
        );
        assert_eq!(
            balance.orchard_amount(),
            amt(expected_orchard(coinbase)),
            "the Orchard pool change of the coinbase of {} must be the negation of its \
             valueBalanceOrchard",
            fixture.name
        );
        if coinbase.has_sapling_shielded_data() {
            shielded_coinbases += 1;
        }

        // transparent - (sprout + sapling + orchard) is negative whenever the
        // coinbase actually pays out, so the remaining transparent value rule
        // must reject a coinbase transaction considered on its own.
        if created + shielded > 0 {
            assert!(
                balance.remaining_transaction_value().is_err(),
                "the remaining transparent value of a coinbase transaction that pays out {} \
                 transparent and {} shielded zatoshi is negative and must be rejected ({})",
                created,
                shielded,
                fixture.name
            );
            paying += 1;
        }

        checked += 1;
    }

    assert!(checked >= 17, "every corpus block must be checked");
    assert!(
        paying >= 10,
        "at least ten corpus coinbases must pay out a nonzero reward, found {}",
        paying
    );
    assert!(
        shielded_coinbases > 0,
        "the corpus must contain at least one Heartwood-era coinbase that pays a shielded \
         reward, otherwise the ZIP-213 case above is never exercised"
    );
}

#[test]
fn the_transparent_pool_change_is_the_utxos_spent_minus_the_outputs_created() {
    let mut with_spends = 0;

    for (block_name, index, transaction) in corpus() {
        // Values chosen here, not read back from the implementation.
        let base = 100_000 + (index as i64) * 1_000;
        let utxos = utxos_for(&transaction, base);

        let balance = transaction.value_balance(&utxos).unwrap_or_else(|e| {
            panic!(
                "{} transaction {} value balance: {:?}",
                block_name, index, e
            )
        });

        let expected = expected_transparent(&transaction, &utxos);
        assert_eq!(
            balance.transparent_amount(),
            amt(expected),
            "{} transaction {}: the transparent pool must change by (UTXOs spent - outputs \
             created) = {}, got {:?}",
            block_name,
            index,
            expected,
            balance.transparent_amount()
        );

        // The other three pools are independent of the UTXO set.
        assert_eq!(
            balance.sprout_amount(),
            amt(expected_sprout(&transaction)),
            "{} transaction {}: Sprout pool",
            block_name,
            index
        );
        assert_eq!(
            balance.sapling_amount(),
            amt(expected_sapling(&transaction)),
            "{} transaction {}: Sapling pool",
            block_name,
            index
        );
        assert_eq!(
            balance.orchard_amount(),
            amt(expected_orchard(&transaction)),
            "{} transaction {}: Orchard pool",
            block_name,
            index
        );

        if !utxos.is_empty() {
            with_spends += 1;
        }
    }

    assert!(
        with_spends >= 10,
        "the corpus must contain at least ten transactions that spend transparent outputs, \
         found {}",
        with_spends
    );
}

#[test]
fn the_sprout_pool_change_is_vpub_old_minus_vpub_new_over_every_joinsplit() {
    let mut with_joinsplits = 0;
    let mut nonzero = 0;

    for (block_name, index, transaction) in corpus() {
        let utxos = utxos_for(&transaction, 500_000);
        let balance = transaction
            .value_balance(&utxos)
            .unwrap_or_else(|e| panic!("{} transaction {}: {:?}", block_name, index, e));

        let expected = expected_sprout(&transaction);
        assert_eq!(
            balance.sprout_amount(),
            amt(expected),
            "{} transaction {}: the Sprout pool change is the sum over joinsplits of \
             (vpub_old - vpub_new) = {}, got {:?}",
            block_name,
            index,
            expected,
            balance.sprout_amount()
        );

        if transaction.joinsplit_count() > 0 {
            with_joinsplits += 1;
            if expected != 0 {
                nonzero += 1;
            }
        } else {
            assert_eq!(
                balance.sprout_amount(),
                amt(0),
                "{} transaction {} has no JoinSplits, so it cannot move the Sprout pool",
                block_name,
                index
            );
        }
    }

    assert!(
        with_joinsplits > 0,
        "the corpus must contain at least one JoinSplit-bearing transaction"
    );
    assert!(
        nonzero > 0,
        "the corpus must contain at least one JoinSplit-bearing transaction that actually moves \
         the Sprout pool, otherwise a Sprout balance stuck at zero would pass"
    );
}

#[test]
fn the_sapling_pool_change_is_the_negated_sum_of_the_value_balance_fields() {
    let mut with_sapling = 0;
    let mut nonzero = 0;

    for (block_name, index, transaction) in corpus() {
        let utxos = utxos_for(&transaction, 700_000);
        let balance = transaction
            .value_balance(&utxos)
            .unwrap_or_else(|e| panic!("{} transaction {}: {:?}", block_name, index, e));

        let expected = expected_sapling(&transaction);
        assert_eq!(
            balance.sapling_amount(),
            amt(expected),
            "{} transaction {}: the Sapling pool change is the negation of the sum of the \
             valueBalanceSapling fields = {}, got {:?}",
            block_name,
            index,
            expected,
            balance.sapling_amount()
        );

        if transaction.has_sapling_shielded_data() {
            with_sapling += 1;
            if expected != 0 {
                nonzero += 1;
            }
        } else {
            assert_eq!(
                balance.sapling_amount(),
                amt(0),
                "{} transaction {} has no Sapling shielded data, so it cannot move the Sapling \
                 pool",
                block_name,
                index
            );
        }
    }

    assert!(
        with_sapling > 0,
        "the corpus must contain at least one Sapling-bearing transaction"
    );
    assert!(
        nonzero > 0,
        "the corpus must contain at least one Sapling transaction with a nonzero \
         valueBalanceSapling, otherwise the sign convention would be untested"
    );
}

#[test]
fn the_orchard_pool_is_untouched_by_every_transaction_before_nu5() {
    // The corpus stops at Canopy, so no transaction in it carries Orchard
    // shielded data. That makes this a check that the Orchard pool defaults to
    // zero rather than to the Sapling or transparent value -- a real failure
    // mode for an implementation that fills the four pools from one loop.
    let mut checked = 0;

    for (block_name, index, transaction) in corpus() {
        let utxos = utxos_for(&transaction, 900_000);
        let balance = transaction
            .value_balance(&utxos)
            .unwrap_or_else(|e| panic!("{} transaction {}: {:?}", block_name, index, e));

        assert!(
            transaction.orchard_shielded_data().is_none(),
            "{} transaction {} predates NU5 and must carry no Orchard shielded data",
            block_name,
            index
        );
        assert_eq!(
            balance.orchard_amount(),
            amt(expected_orchard(&transaction)),
            "{} transaction {}: the Orchard pool must be zero for a transaction with no Orchard \
             shielded data, got {:?}",
            block_name,
            index,
            balance.orchard_amount()
        );
        assert_eq!(balance.orchard_amount(), amt(0));

        checked += 1;
    }

    assert!(checked >= 60, "every corpus transaction must be checked");
}

#[test]
fn an_input_is_valued_at_the_utxo_it_spends_and_a_coinbase_input_at_zero() {
    let mut prevouts = 0;
    let mut coinbases = 0;

    for (block_name, index, transaction) in corpus() {
        let utxos = utxos_for(&transaction, 250_000);

        for (i, input) in transaction.inputs().iter().enumerate() {
            match input.outpoint() {
                Some(outpoint) => {
                    let utxo = &utxos[&outpoint];
                    assert_eq!(
                        input.value(&utxos),
                        amt(i64::from(utxo.output.value)),
                        "{} transaction {} input {} spends {:?} and must be valued at the \
                         output that outpoint names",
                        block_name,
                        index,
                        i,
                        outpoint
                    );
                    prevouts += 1;
                }
                None => {
                    assert_eq!(
                        input.value(&utxos),
                        amt(0),
                        "{} transaction {} input {} is a coinbase input and must be valued at \
                         zero",
                        block_name,
                        index,
                        i
                    );
                    coinbases += 1;
                }
            }
        }

        for output in transaction.outputs() {
            // `Output::value` is the signed view of the stored unsigned amount.
            assert_eq!(
                output.value(),
                amt(i64::from(output.value)),
                "{} transaction {}: Output::value must be the stored amount, widened to the \
                 signed domain",
                block_name,
                index
            );
            assert!(
                i64::from(output.value()) >= 0,
                "a transparent output value is never negative"
            );
        }
    }

    assert!(
        prevouts >= 10,
        "the corpus must exercise at least ten prevout inputs, found {}",
        prevouts
    );
    assert!(
        coinbases >= 17,
        "every corpus block contributes one coinbase input, found {}",
        coinbases
    );
}

#[test]
fn a_transparent_input_total_above_the_money_supply_is_an_amount_error() {
    // Built here rather than taken from the corpus, so the overflow is exact:
    // two inputs, each holding the entire money supply.
    let outpoint_one = OutPoint {
        hash: zebra_chain::transaction::Hash([1u8; 32]),
        index: 0,
    };
    let outpoint_two = OutPoint {
        hash: zebra_chain::transaction::Hash([2u8; 32]),
        index: 7,
    };

    let transaction = Transaction::V1 {
        inputs: vec![
            transparent::Input::PrevOut {
                outpoint: outpoint_one,
                unlock_script: Script::new(&[]),
                sequence: 0,
            },
            transparent::Input::PrevOut {
                outpoint: outpoint_two,
                unlock_script: Script::new(&[]),
                sequence: 0,
            },
        ],
        outputs: vec![Output {
            value: nonneg(1),
            lock_script: Script::new(&[]),
        }],
        lock_time: LockTime::Height(Height(0)),
    };

    let mut utxos: HashMap<OutPoint, Utxo> = HashMap::new();
    utxos.insert(outpoint_one, utxo_worth(MAX_MONEY, 1, false));
    utxos.insert(outpoint_two, utxo_worth(MAX_MONEY, 1, false));

    let result = transaction.value_balance(&utxos);
    assert!(
        result.is_err(),
        "spending two UTXOs of MAX_MONEY each totals {} zatoshi, which is more than the money \
         supply of {}, and must be an amount error rather than a wrapped total; got {:?}",
        2i64 * MAX_MONEY,
        MAX_MONEY,
        result
    );

    // The same transaction with in-range UTXOs must succeed, so the rejection
    // above is about the total and not about the shape of the transaction.
    let mut small: HashMap<OutPoint, Utxo> = HashMap::new();
    small.insert(outpoint_one, utxo_worth(10, 1, false));
    small.insert(outpoint_two, utxo_worth(5, 1, false));

    let balance = transaction
        .value_balance(&small)
        .expect("10 + 5 - 1 zatoshi is well within range");
    assert_eq!(
        balance.transparent_amount(),
        amt(14),
        "10 + 5 spent minus 1 created is 14 zatoshi, got {:?}",
        balance.transparent_amount()
    );
    assert_eq!(
        balance.remaining_transaction_value().ok(),
        Some(nonneg(14)),
        "with no shielded pools in play the remaining transparent value is the transparent \
         pool change itself"
    );
}
