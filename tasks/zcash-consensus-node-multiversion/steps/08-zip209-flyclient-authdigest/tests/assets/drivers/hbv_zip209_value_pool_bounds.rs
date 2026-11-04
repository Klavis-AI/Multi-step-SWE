//! ZIP-209: the chain value pools, and the rule that none of them may go negative.
//!
//! This group scores `zebra_chain::value_balance::ValueBalance` as a *four pool*
//! accumulator rather than the thin transparent-versus-shielded wrapper it used to
//! be.  Three things bite here, and all three are new at this tag:
//!
//! * `remaining_transaction_value` is now the plain SUM of the four pools.  The
//!   previous release computed `transparent - (sprout + sapling + orchard)`, so a
//!   transaction with shielded outputs comes out with a completely different
//!   remaining value under the two conventions.  The tests below pin the sum.
//! * `constrain`, `to_bytes` and `from_bytes` must attribute every failure to the
//!   pool that caused it, through the four `ValueBalanceError` variants.  An
//!   implementation that returns a bare `amount::Error`, or that always blames the
//!   transparent pool, fails.
//! * `update_with_block` must reject a block whose chain value pool change would
//!   drive a pool below zero -- the actual ZIP-209 consensus rule.  The block used
//!   for that is a real mainnet block whose Sapling pool change is negative, so the
//!   rejection is not a synthetic one.
//!
//! All four pools are driven through that one `update_with_block` path, because an
//! implementation that checks three of them and forgets the fourth is the exact
//! failure the rule guards against.  Sprout and Sapling come from real mainnet
//! blocks.  The transparent and Orchard pools do not: no block in the shipped
//! corpus has a negative transparent chain pool change, and the corpus stops at
//! testnet Canopy activation so none of them carries an Orchard bundle at all.
//! Those two are driven by a block holding a single ZIP-244 test-vector
//! transaction, with the expected delta taken from the UTXO values this file
//! chooses or read straight out of the vector's wire bytes.
//!
//! Nothing here recomputes an expected value with the interface under test: the
//! pool deltas come from the shipped block corpus, and the byte layout is rebuilt
//! from `i64::to_le_bytes` directly.

use std::collections::{BTreeMap, HashMap};
use std::convert::{TryFrom, TryInto};
use std::io::Cursor;
use std::path::PathBuf;
use std::sync::Arc;

use zebra_chain::amount::{
    self, Amount, Constraint, NegativeAllowed, NonNegative, COIN, MAX_MONEY,
};
use zebra_chain::block::{Block, Height};
use zebra_chain::serialization::ZcashDeserialize;
use zebra_chain::transaction::Transaction;
use zebra_chain::transparent;
use zebra_chain::value_balance::{ValueBalance, ValueBalanceError};

// ---- fixtures -------------------------------------------------------------

fn fixture_dir() -> PathBuf {
    PathBuf::from(
        std::env::var("HBV_FIXTURES").expect("the verifier must export HBV_FIXTURES"),
    )
}

/// Reads one block of the shipped corpus by its fixture name, e.g. `main-0903001`.
fn block(name: &str) -> Block {
    let path = fixture_dir().join("blocks").join(format!("{}.hex", name));
    let text = std::fs::read_to_string(&path)
        .unwrap_or_else(|e| panic!("missing block fixture {:?}: {}", path, e));
    let raw = hex::decode(text.trim())
        .unwrap_or_else(|e| panic!("block fixture {} is not hex: {}", name, e));
    Block::zcash_deserialize(Cursor::new(&raw[..]))
        .unwrap_or_else(|e| panic!("block fixture {} did not parse: {:?}", name, e))
}

/// The sub-block of `name` holding only the transactions that spend no transparent
/// output, so that its chain value pool change is computable with an empty UTXO set.
///
/// The coinbase always survives this filter, so the result is still a well formed
/// block for value-pool purposes.
fn shielded_only(name: &str) -> Block {
    let full = block(name);
    let transactions: Vec<Arc<Transaction>> = full
        .transactions
        .iter()
        .filter(|tx| {
            !tx.inputs()
                .iter()
                .any(|input| matches!(input, transparent::Input::PrevOut { .. }))
        })
        .cloned()
        .collect();
    assert!(
        !transactions.is_empty(),
        "{} should keep at least its coinbase",
        name
    );
    Block {
        header: full.header.clone(),
        transactions,
    }
}

fn no_utxos() -> HashMap<transparent::OutPoint, transparent::Utxo> {
    HashMap::new()
}

/// The raw bytes of one ZIP-244 test-vector transaction, by fixture name.
///
/// The shipped corpus stops at testnet Canopy activation, so these vectors are the
/// only V5 transactions available at this tag, and the only source of Orchard value
/// movement anywhere in the fixture set.
fn v5_bytes(name: &str) -> Vec<u8> {
    let path = fixture_dir().join("zip244.tsv");
    let text = std::fs::read_to_string(&path)
        .unwrap_or_else(|e| panic!("missing fixture {:?}: {}", path, e));
    let mut lines = text.lines();
    let header: Vec<&str> = lines
        .next()
        .expect("zip244.tsv is empty")
        .trim_start_matches("# ")
        .split('\t')
        .collect();
    for line in lines {
        if line.trim().is_empty() {
            continue;
        }
        let row: BTreeMap<&str, &str> = header.iter().cloned().zip(line.split('\t')).collect();
        if row.get("name") == Some(&name) {
            let tx_hex = row.get("tx_hex").expect("zip244.tsv has a tx_hex column");
            return hex::decode(tx_hex)
                .unwrap_or_else(|e| panic!("{} tx_hex is not hex: {}", name, e));
        }
    }
    panic!("zip244.tsv has no row for {}", name);
}

/// A block holding exactly one ZIP-244 test-vector transaction.
///
/// `chain_value_pool_change` reads the transaction list and nothing else, so the
/// header is carried over unchanged from a real block and is never inspected.
fn one_transaction_block(name: &str) -> Block {
    let raw = v5_bytes(name);
    let transaction = Transaction::zcash_deserialize(Cursor::new(&raw[..]))
        .unwrap_or_else(|e| panic!("{} did not parse as a transaction: {:?}", name, e));
    Block {
        header: block("main-0903001").header,
        transactions: vec![Arc::new(transaction)],
    }
}

/// A UTXO set that gives every transparent output `block` spends the same value.
///
/// For a block whose transactions create no transparent outputs, the transparent
/// chain pool change is then exactly minus that value times the number of inputs --
/// a figure this file chose, not one it read back out of the type under test.
fn utxos_worth(
    block: &Block,
    value: i64,
) -> HashMap<transparent::OutPoint, transparent::Utxo> {
    let mut utxos = HashMap::new();
    for transaction in block.transactions.iter() {
        for input in transaction.inputs() {
            if let transparent::Input::PrevOut { outpoint, .. } = input {
                utxos.insert(
                    *outpoint,
                    transparent::Utxo {
                        output: transparent::Output {
                            value: zats(value),
                            lock_script: transparent::Script::new(&[]),
                        },
                        height: Height(1),
                        from_coinbase: false,
                    },
                );
            }
        }
    }
    utxos
}

// ---- amount helpers -------------------------------------------------------

fn zats<C: Constraint>(value: i64) -> Amount<C> {
    Amount::try_from(value).unwrap_or_else(|e| panic!("{} is not a valid Amount: {:?}", value, e))
}

/// Reads an `Amount` back out as an `i64` without using any arithmetic of the
/// type under test.
fn zatoshis<C>(amount: Amount<C>) -> i64 {
    i64::from_le_bytes(amount.to_bytes())
}

/// Describes a whole `ValueBalance` as four plain integers, for assertions and
/// for failure messages.
fn pools<C: Constraint + Copy>(balance: ValueBalance<C>) -> (i64, i64, i64, i64) {
    (
        zatoshis(balance.transparent_amount()),
        zatoshis(balance.sprout_amount()),
        zatoshis(balance.sapling_amount()),
        zatoshis(balance.orchard_amount()),
    )
}

/// Builds a `ValueBalance` from four pool amounts using only the four single-pool
/// constructors and addition.
fn balance<C: Constraint + Copy>(
    transparent: i64,
    sprout: i64,
    sapling: i64,
    orchard: i64,
) -> ValueBalance<C> {
    let sum = ValueBalance::<C>::from_transparent_amount(zats(transparent))
        + ValueBalance::<C>::from_sprout_amount(zats(sprout))
        + ValueBalance::<C>::from_sapling_amount(zats(sapling))
        + ValueBalance::<C>::from_orchard_amount(zats(orchard));
    sum.unwrap_or_else(|e| {
        panic!(
            "({}, {}, {}, {}) should be a valid ValueBalance: {:?}",
            transparent, sprout, sapling, orchard, e
        )
    })
}

// ---- the arithmetic -------------------------------------------------------

#[test]
fn the_remaining_transaction_value_is_the_sum_of_the_four_pools() {
    // A transaction that moves 1000 zatoshi of transparent input into the three
    // shielded pools and keeps 400 as a fee.  Under the summing convention the
    // remaining value is 1000 - 250 - 300 - 50 = 400.  Under the *old* convention
    // (transparent minus the shielded total) the same balance would come out as
    // 1000 + 600 = 1600, so this assertion distinguishes the two.
    let spending: ValueBalance<NegativeAllowed> = balance(1000, -250, -300, -50);
    assert_eq!(
        spending.remaining_transaction_value().map(zatoshis),
        Ok(400),
        "transparent 1000 with shielded balances -250/-300/-50 leaves 400, \
         not 1600: remaining_transaction_value is the sum of all four pools"
    );

    // And the mirror image: a transaction that unshields.  Here the shielded
    // balances are positive and the transparent balance is negative.
    let unshielding: ValueBalance<NegativeAllowed> = balance(-9_000, 4_000, 5_000, 250);
    assert_eq!(
        unshielding.remaining_transaction_value().map(zatoshis),
        Ok(250),
        "-9000 transparent against +4000/+5000/+250 shielded leaves 250"
    );

    // An all-zero balance has nothing remaining, and nothing missing either.
    assert_eq!(
        ValueBalance::<NonNegative>::zero()
            .remaining_transaction_value()
            .map(zatoshis),
        Ok(0),
        "the zero value balance has zero remaining transaction value"
    );
}

#[test]
fn a_transaction_whose_pools_sum_to_less_than_zero_has_no_remaining_value() {
    // Consensus rule: the remaining value in the transparent transaction value
    // pool MUST be non-negative.  A one-zatoshi shortfall is rejected, and the
    // error reports the shortfall itself together with the non-negative range.
    let short: ValueBalance<NegativeAllowed> = balance(100, -50, -51, 0);
    assert_eq!(
        short.remaining_transaction_value(),
        Err(amount::Error::Constraint {
            value: -1,
            range: 0..=MAX_MONEY,
        }),
        "a balance summing to -1 must be rejected as out of the non-negative range"
    );

    let very_short: ValueBalance<NegativeAllowed> = balance(0, 0, -21_000, 0);
    assert_eq!(
        very_short.remaining_transaction_value(),
        Err(amount::Error::Constraint {
            value: -21_000,
            range: 0..=MAX_MONEY,
        }),
        "the rejected value is the sum itself, not one of the individual pools"
    );
}

#[test]
fn each_pool_constructor_leaves_the_other_three_pools_at_zero() {
    assert_eq!(
        pools(ValueBalance::<NonNegative>::from_transparent_amount(zats(
            7
        ))),
        (7, 0, 0, 0),
        "from_transparent_amount must only set the transparent pool"
    );
    assert_eq!(
        pools(ValueBalance::<NonNegative>::from_sprout_amount(zats(7))),
        (0, 7, 0, 0),
        "from_sprout_amount must only set the sprout pool"
    );
    assert_eq!(
        pools(ValueBalance::<NonNegative>::from_sapling_amount(zats(7))),
        (0, 0, 7, 0),
        "from_sapling_amount must only set the sapling pool"
    );
    assert_eq!(
        pools(ValueBalance::<NonNegative>::from_orchard_amount(zats(7))),
        (0, 0, 0, 7),
        "from_orchard_amount must only set the orchard pool"
    );
    assert_eq!(
        pools(ValueBalance::<NonNegative>::zero()),
        (0, 0, 0, 0),
        "zero() must set all four pools to zero"
    );
}

#[test]
fn negation_flips_the_sign_of_every_pool() {
    use std::ops::Neg;

    // Negation is how a transaction value balance becomes a chain value pool
    // change, so it must hit all four pools and it must land in NegativeAllowed
    // even when it starts in NonNegative.
    let positive: ValueBalance<NonNegative> = balance(5, 6, 7, 8);
    let negated: ValueBalance<NegativeAllowed> = positive.neg();
    assert_eq!(
        pools(negated),
        (-5, -6, -7, -8),
        "negating a value balance negates each pool independently"
    );

    let mixed: ValueBalance<NegativeAllowed> = balance(-5, 6, -7, 0);
    assert_eq!(
        pools(mixed.neg()),
        (5, -6, 7, 0),
        "negation of a mixed-sign balance flips each pool's sign"
    );
    assert_eq!(
        pools(mixed.neg().neg()),
        pools(mixed),
        "negating twice is the identity"
    );
}

#[test]
fn summing_value_balances_adds_each_pool_independently() {
    let parts: Vec<ValueBalance<NonNegative>> =
        vec![balance(1, 10, 100, 1000), balance(2, 20, 200, 2000), balance(4, 40, 400, 4000)];

    let total: Result<ValueBalance<NonNegative>, ValueBalanceError> = parts.iter().sum();
    assert_eq!(
        pools(total.expect("three small balances sum without overflow")),
        (7, 70, 700, 7000),
        "Sum must add the four pools pairwise"
    );

    let empty: Vec<ValueBalance<NonNegative>> = Vec::new();
    let nothing: Result<ValueBalance<NonNegative>, ValueBalanceError> =
        empty.into_iter().sum();
    assert_eq!(
        pools(nothing.expect("an empty sum is zero")),
        (0, 0, 0, 0),
        "summing no value balances gives the zero value balance"
    );

    // Overflow in one pool is reported against that pool, and only that pool.
    let huge: Vec<ValueBalance<NonNegative>> = vec![
        ValueBalance::from_sapling_amount(zats(MAX_MONEY)),
        ValueBalance::from_sapling_amount(zats(MAX_MONEY)),
    ];
    let overflowed: Result<ValueBalance<NonNegative>, ValueBalanceError> =
        huge.into_iter().sum();
    match overflowed {
        Err(ValueBalanceError::Sapling(_)) => {}
        other => panic!(
            "summing two MAX_MONEY sapling pools must fail as ValueBalanceError::Sapling, got {:?}",
            other.map(pools)
        ),
    }
}

// ---- the byte encoding ----------------------------------------------------

#[test]
fn a_value_balance_serializes_as_transparent_sprout_sapling_orchard() {
    let vb: ValueBalance<NegativeAllowed> = balance(1, -2, 3, -4);

    // Rebuilt from scratch: four little-endian i64s, in pool order.
    let mut expected = Vec::with_capacity(32);
    for value in &[1i64, -2, 3, -4] {
        expected.extend_from_slice(&value.to_le_bytes());
    }
    let expected: [u8; 32] = expected
        .try_into()
        .expect("four i64s are exactly 32 bytes");

    assert_eq!(
        vb.to_bytes(),
        expected,
        "a ValueBalance encodes as transparent, sprout, sapling, orchard, \
         each as a little-endian i64"
    );

    assert_eq!(
        ValueBalance::<NegativeAllowed>::from_bytes(expected).map(pools),
        Ok((1, -2, 3, -4)),
        "from_bytes must undo to_bytes, pool for pool"
    );
    assert_eq!(
        ValueBalance::<NonNegative>::from_bytes([0u8; 32]).map(pools),
        Ok((0, 0, 0, 0)),
        "the all-zero encoding is the zero value balance"
    );

    // Round trip of a large, realistic balance.
    let big: ValueBalance<NonNegative> = balance(MAX_MONEY, 0, 610_000_000, 0);
    assert_eq!(
        ValueBalance::<NonNegative>::from_bytes(big.to_bytes()).map(pools),
        Ok((MAX_MONEY, 0, 610_000_000, 0)),
        "a MAX_MONEY transparent pool round trips through 32 bytes"
    );
}

#[test]
fn from_bytes_names_the_pool_whose_amount_is_out_of_range() {
    // One negative zatoshi in slot n must be blamed on pool n, not on the first
    // pool and not on an unnamed amount error.
    let slots: [(usize, &str); 4] = [
        (0, "transparent"),
        (8, "sprout"),
        (16, "sapling"),
        (24, "orchard"),
    ];

    for &(offset, name) in slots.iter() {
        let mut bytes = [0u8; 32];
        bytes[offset..offset + 8].copy_from_slice(&(-1i64).to_le_bytes());

        let expected_inner = amount::Error::Constraint {
            value: -1,
            range: 0..=MAX_MONEY,
        };
        let expected = match offset {
            0 => ValueBalanceError::Transparent(expected_inner),
            8 => ValueBalanceError::Sprout(expected_inner),
            16 => ValueBalanceError::Sapling(expected_inner),
            _ => ValueBalanceError::Orchard(expected_inner),
        };

        assert_eq!(
            ValueBalance::<NonNegative>::from_bytes(bytes).map(pools),
            Err(expected),
            "a -1 zatoshi amount at byte offset {} belongs to the {} pool",
            offset,
            name
        );
    }

    // Above MAX_MONEY is out of range for both constraints.
    let mut too_much = [0u8; 32];
    too_much[8..16].copy_from_slice(&(MAX_MONEY + 1).to_le_bytes());
    match ValueBalance::<NegativeAllowed>::from_bytes(too_much) {
        Err(ValueBalanceError::Sprout(amount::Error::Constraint { value, range })) => {
            assert_eq!(value, MAX_MONEY + 1, "the rejected value is reported");
            assert_eq!(
                range,
                -MAX_MONEY..=MAX_MONEY,
                "a NegativeAllowed pool has the symmetric range"
            );
        }
        other => panic!(
            "MAX_MONEY + 1 in the sprout slot must be a ValueBalanceError::Sprout, got {:?}",
            other.map(pools)
        ),
    }
}

#[test]
fn constraining_to_non_negative_names_the_negative_pool() {
    let cases: [(&str, ValueBalance<NegativeAllowed>); 4] = [
        ("transparent", balance(-1, 0, 0, 0)),
        ("sprout", balance(0, -1, 0, 0)),
        ("sapling", balance(0, 0, -1, 0)),
        ("orchard", balance(0, 0, 0, -1)),
    ];

    for &(name, negative) in cases.iter() {
        let expected_inner = amount::Error::Constraint {
            value: -1,
            range: 0..=MAX_MONEY,
        };
        let expected = match name {
            "transparent" => ValueBalanceError::Transparent(expected_inner),
            "sprout" => ValueBalanceError::Sprout(expected_inner),
            "sapling" => ValueBalanceError::Sapling(expected_inner),
            _ => ValueBalanceError::Orchard(expected_inner),
        };
        assert_eq!(
            negative.constrain::<NonNegative>().map(pools),
            Err(expected),
            "constraining a -1 {} pool to NonNegative must blame the {} pool",
            name,
            name
        );
    }

    // Widening always succeeds and preserves every pool.
    let non_negative: ValueBalance<NonNegative> = balance(3, 4, 5, 6);
    assert_eq!(
        non_negative.constrain::<NegativeAllowed>().map(pools),
        Ok((3, 4, 5, 6)),
        "NonNegative to NegativeAllowed is always valid and changes no pool"
    );
}

// ---- the ZIP-209 consensus rule -------------------------------------------

#[test]
fn a_coinbase_only_block_credits_its_subsidy_to_the_transparent_pool() {
    // Mainnet 10 is a pre-Sapling coinbase-only block paying 0.00625 ZEC in this
    // corpus; mainnet 347500 and 653600 are later coinbase-only blocks.  Applying
    // them to an empty chain moves value into the transparent pool and nowhere else.
    let cases: [(&str, i64); 3] = [
        ("main-0000010", 625_000),
        ("main-0347500", 1_250_000_000),
        ("main-0653600", 625_000_000),
    ];

    for &(name, subsidy) in cases.iter() {
        let updated = ValueBalance::<NonNegative>::zero()
            .update_with_block(&block(name), &no_utxos())
            .unwrap_or_else(|e| panic!("{} should be applicable to an empty chain: {:?}", name, e));
        assert_eq!(
            pools(updated),
            (subsidy, 0, 0, 0),
            "{} must credit its whole coinbase to the transparent pool",
            name
        );
    }

    // Two blocks in a row accumulate.
    let after_one = ValueBalance::<NonNegative>::zero()
        .update_with_block(&block("main-0347500"), &no_utxos())
        .expect("main-0347500 applies to an empty chain");
    let after_two = after_one
        .update_with_block(&block("main-0653600"), &no_utxos())
        .expect("main-0653600 applies on top");
    assert_eq!(
        pools(after_two),
        (1_250_000_000 + 625_000_000, 0, 0, 0),
        "successive blocks accumulate in the chain value pool"
    );
}

#[test]
fn a_block_that_would_drive_a_shielded_pool_negative_is_rejected() {
    // Mainnet 903001's fully shielded transactions take 250 zatoshi out of the
    // Sapling pool: their Sapling chain value pool change is -250.  ZIP-209 says a
    // node MUST reject a block that would make a shielded chain pool balance
    // negative, so applying it to a chain with no Sapling value must fail, and the
    // failure must be attributed to the Sapling pool.
    let sapling_spender = shielded_only("main-0903001");

    assert_eq!(
        ValueBalance::<NonNegative>::zero()
            .update_with_block(&sapling_spender, &no_utxos())
            .map(pools),
        Err(ValueBalanceError::Sapling(amount::Error::Constraint {
            value: -250,
            range: 0..=MAX_MONEY,
        })),
        "a -250 zatoshi Sapling pool change must be rejected against an empty Sapling pool"
    );

    // One zatoshi short is still short.
    match balance::<NonNegative>(0, 0, 249, 0)
        .update_with_block(&sapling_spender, &no_utxos())
    {
        Err(ValueBalanceError::Sapling(_)) => {}
        other => panic!(
            "249 zatoshi is not enough to cover a -250 Sapling change, got {:?}",
            other.map(pools)
        ),
    }

    // Exactly enough is accepted, and empties the pool.
    let funded = balance::<NonNegative>(0, 0, 250, 0)
        .update_with_block(&sapling_spender, &no_utxos())
        .expect("a 250 zatoshi Sapling pool exactly covers the block");
    assert_eq!(
        pools(funded),
        (625_107_615, 0, 0, 0),
        "the Sapling pool drains to zero and the coinbase lands in the transparent pool"
    );

    // The same rule guards the Sprout pool.  Mainnet 347501's JoinSplit-only
    // transactions remove 1.3338 ZEC from Sprout.
    let sprout_spender = shielded_only("main-0347501");
    match ValueBalance::<NonNegative>::zero().update_with_block(&sprout_spender, &no_utxos()) {
        Err(ValueBalanceError::Sprout(amount::Error::Constraint { value, .. })) => {
            assert_eq!(
                value, -133_380_000,
                "main-0347501 removes 133380000 zatoshi from the Sprout pool"
            );
        }
        other => panic!(
            "an unfunded Sprout pool must reject main-0347501, got {:?}",
            other.map(pools)
        ),
    }
    let funded_sprout = balance::<NonNegative>(0, 133_380_000, 0, 0)
        .update_with_block(&sprout_spender, &no_utxos())
        .expect("a funded Sprout pool accepts main-0347501");
    assert_eq!(
        pools(funded_sprout),
        (1_383_390_000, 0, 0, 0),
        "the Sprout pool drains and its value reappears in the transparent pool, less fees"
    );
}

#[test]
fn the_transparent_pool_bound_is_enforced_through_the_same_path() {
    // ZIP-209 names the three shielded pools.  Zebra applies the same range check
    // to the transparent pool through the same `update_with_block` call, so it is
    // graded here rather than assumed: an implementation that guards three pools
    // and forgets the fourth is exactly what this catches.
    //
    // `zip244-06` has two transparent inputs, no transparent outputs and nothing
    // shielded, so a block holding only it moves the whole value of the two
    // outputs it spends out of the transparent chain pool and touches nothing
    // else.  The expected delta is the value assigned to those two UTXOs below.
    const UTXO_VALUE: i64 = 700_000_000;

    let spender = one_transaction_block("zip244-06");
    assert!(
        spender.transactions[0].outputs().is_empty(),
        "zip244-06 should create no transparent outputs"
    );
    let utxos = utxos_worth(&spender, UTXO_VALUE);
    assert_eq!(
        utxos.len(),
        2,
        "zip244-06 should spend two distinct transparent outputs"
    );

    assert_eq!(
        ValueBalance::<NonNegative>::zero()
            .update_with_block(&spender, &utxos)
            .map(pools),
        Err(ValueBalanceError::Transparent(amount::Error::Constraint {
            value: -2 * UTXO_VALUE,
            range: 0..=MAX_MONEY,
        })),
        "moving 2 x {} zatoshi out of an empty transparent chain pool must be \
         rejected, and blamed on the transparent pool",
        UTXO_VALUE
    );

    // A pool that exactly covers the spend drains to zero, and the other three
    // pools are left alone.
    assert_eq!(
        balance::<NonNegative>(2 * UTXO_VALUE, 0, 0, 0)
            .update_with_block(&spender, &utxos)
            .map(pools),
        Ok((0, 0, 0, 0)),
        "a transparent pool holding exactly 2 x {} zatoshi covers the spend and \
         empties, leaving the three shielded pools at zero",
        UTXO_VALUE
    );
}

#[test]
fn the_orchard_pool_bound_is_enforced_through_the_same_path() {
    // The fourth pool.  The shipped block corpus stops at testnet Canopy
    // activation, so no block in it carries an Orchard bundle and the only
    // Orchard value movement available is a ZIP-244 test vector.  `zip244-07`
    // spends no transparent output, so the UTXO set stays empty, and its Sapling
    // balance is zero, so Orchard is the only shielded pool that moves.
    //
    // `valueBalanceOrchard` is a signed little-endian 64-bit field of the V5 wire
    // format.  In this vector it is the eight bytes at offset 2496 of its
    // 3065-byte encoding; reading it out of the fixture keeps the expected value
    // clear of the type under test.
    let raw = v5_bytes("zip244-07");
    assert_eq!(raw.len(), 3065, "zip244-07 should be 3065 bytes");
    let value_balance_orchard = i64::from_le_bytes(
        raw[2496..2504]
            .try_into()
            .expect("eight bytes make an i64"),
    );
    assert_eq!(
        value_balance_orchard, 651_214_746_701_461,
        "zip244-07's valueBalanceOrchard sits at byte offset 2496"
    );

    let spender = one_transaction_block("zip244-07");
    assert!(
        spender.transactions[0].inputs().is_empty(),
        "zip244-07 should spend no transparent output"
    );

    // A positive transaction-side Orchard balance is value leaving the Orchard
    // chain pool, so the chain pool change is its negation.
    assert_eq!(
        ValueBalance::<NonNegative>::zero()
            .update_with_block(&spender, &no_utxos())
            .map(pools),
        Err(ValueBalanceError::Orchard(amount::Error::Constraint {
            value: -value_balance_orchard,
            range: 0..=MAX_MONEY,
        })),
        "an empty Orchard chain pool cannot cover a transaction that takes {} \
         zatoshi out of it",
        value_balance_orchard
    );

    // One zatoshi short is still short.
    match balance::<NonNegative>(0, 0, 0, value_balance_orchard - 1)
        .update_with_block(&spender, &no_utxos())
    {
        Err(ValueBalanceError::Orchard(_)) => {}
        other => panic!(
            "an Orchard pool one zatoshi short must still reject, got {:?}",
            other.map(pools)
        ),
    }

    // Exactly enough drains the Orchard pool to zero.  Only Orchard is asserted:
    // the transparent total this vector leaves behind is not this test's subject.
    let funded = balance::<NonNegative>(0, 0, 0, value_balance_orchard)
        .update_with_block(&spender, &no_utxos())
        .expect("an Orchard pool holding exactly the spend covers it");
    assert_eq!(
        zatoshis(funded.orchard_amount()),
        0,
        "the Orchard pool drains to exactly zero"
    );
}

#[test]
fn the_error_type_is_a_std_error_and_renders_its_pool() {
    // §2.4 requires `Debug + Clone + PartialEq + Eq` plus an `Error`/`Display`
    // impl, and §2.3 requires the error to *name the failing pool* — which the
    // variant does. The rendered wording is not pinned anywhere, so this case
    // checks the variant and that the `Display`/`Error` impls exist and render
    // something; it does not assert what the text says.
    // A balance that holds a negative transparent pool cannot be constrained
    // to `NonNegative`; that is what produces the error under test.
    let mut bytes = [0u8; 32];
    bytes[0..8].copy_from_slice(&(-1i64).to_le_bytes());
    let err = ValueBalance::<NegativeAllowed>::from_bytes(bytes)
        .expect("from_bytes does not re-apply the constraint")
        .constrain::<NonNegative>()
        .expect_err("a negative transparent pool is not NonNegative");

    assert!(
        matches!(err, ValueBalanceError::Transparent(_)),
        "the failing pool must be named by the variant, got {:?}",
        err
    );

    // It is a real `std::error::Error`, so it can be boxed and rendered through
    // the trait object rather than only through its own Display.
    let boxed: Box<dyn std::error::Error> = Box::new(err.clone());
    assert!(
        !boxed.to_string().is_empty(),
        "a boxed ValueBalanceError must render as a non-empty string"
    );
    assert!(
        !format!("{}", err).is_empty(),
        "the inherent Display must render a non-empty string"
    );

    // `Clone` produces an equal value; `PartialEq` distinguishes the pools.
    assert_eq!(err.clone(), err, "a cloned ValueBalanceError compares equal");
    let mut sprout_bytes = [0u8; 32];
    sprout_bytes[8..16].copy_from_slice(&(-1i64).to_le_bytes());
    let other = ValueBalance::<NegativeAllowed>::from_bytes(sprout_bytes)
        .expect("from_bytes does not re-apply the constraint")
        .constrain::<NonNegative>()
        .expect_err("a negative Sprout pool is not NonNegative");
    assert_ne!(
        err, other,
        "errors naming different pools must not compare equal"
    );
}

#[test]
fn the_assigning_operators_accumulate_pool_wise() {
    // §2.5 requires `AddAssign`/`SubAssign` on `Result<ValueBalance<C>>`.  Each
    // pool must move independently and a failure must be reported with the pool
    // that caused it.
    let mut acc: std::result::Result<ValueBalance<NegativeAllowed>, ValueBalanceError> =
        Ok(ValueBalance::<NegativeAllowed>::zero());
    let addend = ValueBalance::<NegativeAllowed>::from_transparent_amount(
        Amount::<NegativeAllowed>::try_from(COIN).expect("COIN is in range"),
    );
    acc += addend;
    assert_eq!(
        zatoshis(acc.as_ref().expect("in range").transparent_amount()),
        COIN,
        "AddAssign must add into the transparent pool"
    );
    // The other three pools are untouched by a transparent-only addend.
    assert_eq!(
        zatoshis(acc.as_ref().expect("in range").sprout_amount()),
        0
    );
    assert_eq!(
        zatoshis(acc.as_ref().expect("in range").sapling_amount()),
        0
    );
    assert_eq!(
        zatoshis(acc.as_ref().expect("in range").orchard_amount()),
        0
    );

    // SubAssign removes it again, pool-wise and independently.
    acc -= addend;
    assert_eq!(
        zatoshis(acc.as_ref().expect("in range").transparent_amount()),
        0,
        "SubAssign must subtract from the transparent pool"
    );

    // A failure in one pool must leave the accumulator in `Err` and name that
    // pool, not silently stay `Ok`.  `MAX_MONEY` is `Amount<NonNegative>`'s
    // upper bound, so one more zatoshi into the Sprout pool is the point at
    // which the balance stops satisfying its own constraint.
    let max_sprout = ValueBalance::<NonNegative>::from_sprout_amount(
        Amount::<NonNegative>::try_from(MAX_MONEY).expect("MAX_MONEY is in range"),
    );
    let one_more = ValueBalance::<NonNegative>::from_sprout_amount(
        Amount::<NonNegative>::try_from(1).expect("1 is in range"),
    );
    let mut narrow: std::result::Result<ValueBalance<NonNegative>, ValueBalanceError> =
        Ok(max_sprout);
    narrow += one_more;
    let failed = narrow.expect_err("a Sprout pool above MAX_MONEY must be rejected");
    assert!(
        matches!(failed, ValueBalanceError::Sprout(_)),
        "the failure must be attributed to the Sprout pool, got {:?}",
        failed
    );

    // A transparent-only `SubAssign` that drives the pool out of range is
    // reported against the transparent pool, not swallowed.
    let mut negative: std::result::Result<ValueBalance<NonNegative>, ValueBalanceError> =
        Ok(ValueBalance::<NonNegative>::zero());
    negative -= ValueBalance::<NonNegative>::from_transparent_amount(
        Amount::<NonNegative>::try_from(1).expect("1 is in range"),
    );
    let failed = negative.expect_err("a negative transparent pool must be rejected");
    assert!(
        matches!(failed, ValueBalanceError::Transparent(_)),
        "the failure must be attributed to the transparent pool, got {:?}",
        failed
    );

    // An `Err` accumulator stays `Err` through a further `AddAssign`.
    let mut errored = Err(ValueBalanceError::Transparent(amount::Error::DivideByZero {
        amount: 1,
    }));
    errored += ValueBalance::<NegativeAllowed>::zero();
    assert!(
        errored.is_err(),
        "AddAssign onto an Err accumulator must stay Err"
    );
}
