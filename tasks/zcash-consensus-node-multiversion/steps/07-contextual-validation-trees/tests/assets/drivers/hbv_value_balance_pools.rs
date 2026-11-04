//! The four Zcash value pools, held together in one `ValueBalance`.
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! This milestone is where validation stops being a property of a single
//! serialized object and starts being a property of the chain around it. The
//! first piece of that is bookkeeping: a transaction moves value between four
//! pools -- transparent, Sprout, Sapling and Orchard -- and the node has to be
//! able to say, for any transaction, how much value each pool gained or lost.
//!
//! `ValueBalance<C>` is that ledger. It is four `Amount<C>`s with no public
//! fields, so every assertion here goes through the constructors, the four
//! getters, the four setters, and the `Add`/`Sub` impls. The assertions bite
//! because a plausible wrong implementation is *easy* here: share one pool
//! between two constructors, let a setter clobber a neighbour, add pools
//! pairwise but drop the overflow, or compute the remaining transaction value
//! with the shielded pools added instead of subtracted. Each of those is
//! caught below by an expected value computed with plain `i64` arithmetic in
//! this file, never by calling the interface under test twice.
//!
//! The consensus rule the whole type exists to serve is the last one:
//!
//!   "The remaining value in the transparent transaction value pool MUST be
//!    nonnegative."  -- Zcash protocol spec, section 3.4 "Transactions and
//!    Treestates" (line 917 of the shipped specs/nu5.txt)
//!
//! which is `transparent - (sprout + sapling + orchard)`, constrained to
//! `NonNegative`. The sign convention matters and is asserted directly.

use std::convert::TryFrom;

use zebra_chain::amount::{Amount, Error as AmountError, NegativeAllowed, NonNegative, MAX_MONEY};
use zebra_chain::value_balance::{ValueBalance, ValueBalanceError};

// ---------------------------------------------------------------- helpers

/// A `NegativeAllowed` amount, or a panic naming the value that was refused.
fn amt(value: i64) -> Amount<NegativeAllowed> {
    Amount::<NegativeAllowed>::try_from(value)
        .unwrap_or_else(|e| panic!("{} is a valid NegativeAllowed zatoshi amount: {:?}", value, e))
}

/// A `NonNegative` amount, or a panic naming the value that was refused.
fn nonneg(value: i64) -> Amount<NonNegative> {
    Amount::<NonNegative>::try_from(value)
        .unwrap_or_else(|e| panic!("{} is a valid NonNegative zatoshi amount: {:?}", value, e))
}

/// Build a `ValueBalance` from four zatoshi values, using only the four
/// single-pool constructors and `Add`. If any of those is wrong this helper
/// is wrong too, so no test uses it to produce its own *expected* value --
/// expected values below are always plain `i64`s.
fn balance(
    transparent: i64,
    sprout: i64,
    sapling: i64,
    orchard: i64,
) -> ValueBalance<NegativeAllowed> {
    let sum = ValueBalance::from_transparent_amount(amt(transparent))
        + ValueBalance::from_sprout_amount(amt(sprout))
        + ValueBalance::from_sapling_amount(amt(sapling))
        + ValueBalance::from_orchard_amount(amt(orchard));

    sum.expect("four in-range single-pool balances always add")
}

/// The four pools of a `ValueBalance`, read back out as plain integers.
fn pools(value_balance: &ValueBalance<NegativeAllowed>) -> (i64, i64, i64, i64) {
    (
        i64::from(value_balance.transparent_amount()),
        i64::from(value_balance.sprout_amount()),
        i64::from(value_balance.sapling_amount()),
        i64::from(value_balance.orchard_amount()),
    )
}

// ---------------------------------------------------------------- the zero balance

#[test]
fn a_zero_value_balance_is_zero_in_all_four_pools() {
    let zero = ValueBalance::<NegativeAllowed>::zero();

    assert_eq!(
        pools(&zero),
        (0, 0, 0, 0),
        "ValueBalance::zero() must leave every pool at zero, got {:?}",
        pools(&zero)
    );

    // The same must hold for the NonNegative constraint, which is the one the
    // chain value pools themselves are tracked in.
    let zero_nonneg = ValueBalance::<NonNegative>::zero();
    assert_eq!(
        i64::from(zero_nonneg.transparent_amount()),
        0,
        "ValueBalance::<NonNegative>::zero() must have a zero transparent pool"
    );
    assert_eq!(
        i64::from(zero_nonneg.sprout_amount()),
        0,
        "ValueBalance::<NonNegative>::zero() must have a zero sprout pool"
    );
    assert_eq!(
        i64::from(zero_nonneg.sapling_amount()),
        0,
        "ValueBalance::<NonNegative>::zero() must have a zero sapling pool"
    );
    assert_eq!(
        i64::from(zero_nonneg.orchard_amount()),
        0,
        "ValueBalance::<NonNegative>::zero() must have a zero orchard pool"
    );
}

// ---------------------------------------------------------------- constructors

#[test]
fn each_single_pool_constructor_fills_exactly_its_own_pool() {
    // A distinct value per pool, so that a constructor writing into the wrong
    // field cannot coincidentally produce the right answer.
    let cases: [(&str, ValueBalance<NegativeAllowed>, (i64, i64, i64, i64)); 4] = [
        (
            "from_transparent_amount",
            ValueBalance::from_transparent_amount(amt(-11)),
            (-11, 0, 0, 0),
        ),
        (
            "from_sprout_amount",
            ValueBalance::from_sprout_amount(amt(22)),
            (0, 22, 0, 0),
        ),
        (
            "from_sapling_amount",
            ValueBalance::from_sapling_amount(amt(-33)),
            (0, 0, -33, 0),
        ),
        (
            "from_orchard_amount",
            ValueBalance::from_orchard_amount(amt(44)),
            (0, 0, 0, 44),
        ),
    ];

    for (name, value_balance, expected) in cases.iter() {
        assert_eq!(
            pools(value_balance),
            *expected,
            "{} must set only its own pool: expected {:?}, got {:?}",
            name,
            expected,
            pools(value_balance)
        );
    }
}

// ---------------------------------------------------------------- setters

#[test]
fn each_setter_replaces_only_its_own_pool() {
    let base = balance(1, 2, 3, 4);

    // set_transparent_value_balance takes a whole ValueBalance and copies only
    // the transparent field out of it. The other pools of the argument, set to
    // deliberately wrong values here, must be ignored.
    let mut transparent_set = base;
    transparent_set.set_transparent_value_balance(balance(100, -9, -9, -9));
    assert_eq!(
        pools(&transparent_set),
        (100, 2, 3, 4),
        "set_transparent_value_balance must take only the transparent pool from its argument, got {:?}",
        pools(&transparent_set)
    );

    let mut sprout_set = base;
    sprout_set.set_sprout_value_balance(balance(-9, 200, -9, -9));
    assert_eq!(
        pools(&sprout_set),
        (1, 200, 3, 4),
        "set_sprout_value_balance must take only the sprout pool from its argument, got {:?}",
        pools(&sprout_set)
    );

    let mut sapling_set = base;
    sapling_set.set_sapling_value_balance(balance(-9, -9, 300, -9));
    assert_eq!(
        pools(&sapling_set),
        (1, 2, 300, 4),
        "set_sapling_value_balance must take only the sapling pool from its argument, got {:?}",
        pools(&sapling_set)
    );

    let mut orchard_set = base;
    orchard_set.set_orchard_value_balance(balance(-9, -9, -9, 400));
    assert_eq!(
        pools(&orchard_set),
        (1, 2, 3, 400),
        "set_orchard_value_balance must take only the orchard pool from its argument, got {:?}",
        pools(&orchard_set)
    );

    // The original is untouched: the setters take `&mut self` and the type is
    // `Copy`, so the bindings above are independent copies.
    assert_eq!(
        pools(&base),
        (1, 2, 3, 4),
        "ValueBalance is Copy; assigning it must not alias the original"
    );
}

// ---------------------------------------------------------------- arithmetic

#[test]
fn addition_and_subtraction_act_pool_by_pool() {
    let left = balance(10, -20, 30, -40);
    let right = balance(-1, 2, -3, 4);

    let sum = (left + right).expect("both operands are far inside the valid range");
    assert_eq!(
        pools(&sum),
        (9, -18, 27, -36),
        "addition must add each pool independently, got {:?}",
        pools(&sum)
    );

    let difference = (left - right).expect("both operands are far inside the valid range");
    assert_eq!(
        pools(&difference),
        (11, -22, 33, -44),
        "subtraction must subtract each pool independently, got {:?}",
        pools(&difference)
    );
}

#[test]
fn add_and_sub_chain_through_the_result_type() {
    // `Result<ValueBalance, _> + ValueBalance` and `- ValueBalance` exist so
    // that a fold over many transactions does not need a `?` at every step.
    let one = balance(1, 1, 1, 1);
    let two = balance(2, 2, 2, 2);
    let three = balance(3, 3, 3, 3);

    let chained = one + two + three;
    let chained = chained.expect("three small balances always add");
    assert_eq!(
        pools(&chained),
        (6, 6, 6, 6),
        "(a + b) + c must chain through Result, got {:?}",
        pools(&chained)
    );

    let chained_sub = one + two - three;
    let chained_sub = chained_sub.expect("three small balances always combine");
    assert_eq!(
        pools(&chained_sub),
        (0, 0, 0, 0),
        "(a + b) - c must chain through Result, got {:?}",
        pools(&chained_sub)
    );

    // An error at the head of the chain must survive to the end rather than
    // being swallowed by the later operands.
    let overflowing = ValueBalance::from_sprout_amount(amt(MAX_MONEY))
        + ValueBalance::from_sprout_amount(amt(MAX_MONEY))
        + one;
    assert!(
        overflowing.is_err(),
        "an error produced by the first addition must propagate through the chain"
    );
}

#[test]
fn a_pool_leaving_the_valid_range_is_a_value_balance_amount_error() {
    let max = ValueBalance::from_sapling_amount(amt(MAX_MONEY));
    let one = ValueBalance::from_sapling_amount(amt(1));

    let result = max + one;
    match result {
        Err(ValueBalanceError::AmountError(AmountError::Contains { range, value })) => {
            assert_eq!(
                range,
                -MAX_MONEY..=MAX_MONEY,
                "a NegativeAllowed pool has the range -MAX_MONEY..=MAX_MONEY"
            );
            assert_eq!(
                value,
                MAX_MONEY + 1,
                "the error must report the out-of-range sum, got {}",
                value
            );
        }
        other => panic!(
            "MAX_MONEY + 1 in the sapling pool must be a ValueBalanceError::AmountError(Contains), got {:?}",
            other
        ),
    }

    // Subtraction past the bottom of the range is the same kind of error.
    let min = ValueBalance::from_orchard_amount(amt(-MAX_MONEY));
    let below = min - ValueBalance::from_orchard_amount(amt(1));
    assert!(
        matches!(below, Err(ValueBalanceError::AmountError(_))),
        "-MAX_MONEY - 1 in the orchard pool must be a ValueBalanceError::AmountError, got {:?}",
        below
    );

    assert_eq!(
        ValueBalanceError::AmountError(AmountError::SumOverflow).to_string(),
        "value balance contains invalid amounts",
        "ValueBalanceError::AmountError must display its own message, not the inner one"
    );
}

// ---------------------------------------------------------------- the consensus rule

#[test]
fn the_remaining_transaction_value_is_transparent_minus_the_shielded_pools() {
    // A transaction that brings 1000 zatoshi in through transparent inputs
    // beyond what its transparent outputs take out, and moves 100 + 200 + 300
    // of that into the three shielded pools. 1000 - 600 = 400 is the fee left
    // in the transparent transaction value pool.
    let value_balance = balance(1000, 100, 200, 300);

    let remaining = value_balance
        .remaining_transaction_value()
        .expect("1000 - (100 + 200 + 300) = 400 is nonnegative");

    assert_eq!(
        i64::from(remaining),
        400,
        "remaining value must be transparent - (sprout + sapling + orchard), got {}",
        i64::from(remaining)
    );

    // Shielded pools with a negative balance move value *out* of the shielded
    // side and so increase the remaining transparent value.
    let reverse = balance(1000, -100, -200, -300);
    let remaining_reverse = reverse
        .remaining_transaction_value()
        .expect("1000 + 600 = 1600 is nonnegative");
    assert_eq!(
        i64::from(remaining_reverse),
        1600,
        "negative shielded pools must add to the remaining value, got {}",
        i64::from(remaining_reverse)
    );

    // Exactly zero remaining is allowed: the rule is nonnegative, not positive.
    let exact = balance(600, 100, 200, 300);
    let remaining_exact = exact
        .remaining_transaction_value()
        .expect("a remaining value of exactly zero satisfies the rule");
    assert_eq!(
        i64::from(remaining_exact),
        0,
        "a remaining value of exactly zero must be accepted"
    );
}

#[test]
fn a_negative_remaining_transaction_value_is_rejected() {
    // 600 in, 601 out of the transparent pool and into the shielded pools:
    // the transaction spends one zatoshi it does not have.
    let value_balance = balance(600, 100, 200, 301);

    let remaining = value_balance.remaining_transaction_value();
    match remaining {
        Err(AmountError::Contains { range, value }) => {
            assert_eq!(
                range,
                0..=MAX_MONEY,
                "the remaining value is constrained to NonNegative, whose range is 0..=MAX_MONEY"
            );
            assert_eq!(
                value, -1,
                "the error must report the negative remainder, got {}",
                value
            );
        }
        other => panic!(
            "a remaining transparent value of -1 must be rejected with Error::Contains, got {:?}",
            other
        ),
    }

    // The rule is about the *remaining* value, not about any single pool: a
    // transaction whose pools are individually huge but whose remainder is
    // nonnegative is fine.
    let large = balance(MAX_MONEY, MAX_MONEY / 2, MAX_MONEY / 4, MAX_MONEY / 8);
    let expected = MAX_MONEY - (MAX_MONEY / 2 + MAX_MONEY / 4 + MAX_MONEY / 8);
    let remaining_large = large
        .remaining_transaction_value()
        .expect("the remainder of the large case is nonnegative");
    assert_eq!(
        i64::from(remaining_large),
        expected,
        "the remaining value of the large case must be {}, got {}",
        expected,
        i64::from(remaining_large)
    );
}

#[test]
fn a_nonnegative_value_balance_still_computes_a_remaining_value() {
    // `remaining_transaction_value` is defined for every `Constraint`, not
    // just `NegativeAllowed`: the chain value pools themselves are tracked as
    // `ValueBalance<NonNegative>`.
    let value_balance = ValueBalance::<NonNegative>::from_transparent_amount(nonneg(900))
        + ValueBalance::<NonNegative>::from_sapling_amount(nonneg(250));
    let value_balance = value_balance.expect("two small nonnegative pools always add");

    let remaining = value_balance
        .remaining_transaction_value()
        .expect("900 - 250 = 650 is nonnegative");

    assert_eq!(
        i64::from(remaining),
        650,
        "the NonNegative instantiation must use the same formula, got {}",
        i64::from(remaining)
    );

    assert_eq!(
        i64::from(value_balance.transparent_amount()),
        900,
        "reading a NonNegative pool back must give the value it was built from"
    );
}
