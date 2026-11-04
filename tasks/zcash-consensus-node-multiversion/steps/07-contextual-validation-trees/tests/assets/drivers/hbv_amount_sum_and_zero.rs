//! Summing zatoshi amounts without lying about the overflow.
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! Contextual validation adds up money: the value of every transparent input
//! a transaction spends, the value of every output it creates, the balance of
//! every shielded bundle. Before this milestone `Amount` had checked binary
//! `Add`/`Sub` and nothing else, so every caller wrote its own fold. This
//! milestone gives `Amount` a `Sum` implementation, a `zero()` constructor
//! and a `Neg` impl for the signed constraint, which is what lets
//! `Transaction::value_balance` be written as four one-line folds.
//!
//! The interesting part is the failure mode. There are *two* distinct ways a
//! sum of `Amount`s can fail, and conflating them hides a real bug:
//!
//!   * the running total leaves `-MAX_MONEY..=MAX_MONEY` -- a consensus-level
//!     out-of-range value, reported as `Error::Contains` with the offending
//!     total; and
//!   * the running total overflows `i64` itself -- which cannot be reported
//!     as `Contains`, because there is no `i64` to report. That is what the
//!     new `Error::SumOverflow` variant is for.
//!
//! An implementation that folds with `+` and `?` gets the first case right and
//! the second case wrong (it stops at the first out-of-range partial sum); one
//! that folds with wrapping `i64` arithmetic gets the second case wrong in the
//! other direction. Both are caught below. Every expected value is computed
//! with plain `i64`/`i128` arithmetic in this file.

use std::convert::TryFrom;

use zebra_chain::amount::{Amount, Error as AmountError, NegativeAllowed, NonNegative, MAX_MONEY};

// ---------------------------------------------------------------- helpers

type SumResult<C> = Result<Amount<C>, AmountError>;

fn amt(value: i64) -> Amount<NegativeAllowed> {
    Amount::<NegativeAllowed>::try_from(value)
        .unwrap_or_else(|e| panic!("{} is a valid NegativeAllowed amount: {:?}", value, e))
}

/// `i64::MAX / MAX_MONEY`, the largest number of `MAX_MONEY` amounts whose
/// integer total still fits in an `i64`. One more than this overflows.
fn max_money_amounts_that_fit_in_i64() -> usize {
    let fits = i64::MAX / MAX_MONEY;
    usize::try_from(fits).expect("i64::MAX / MAX_MONEY is a small positive number")
}

// ---------------------------------------------------------------- zero

#[test]
fn zero_is_the_additive_identity_for_both_constraints() {
    let zero_signed: Amount<NegativeAllowed> = Amount::zero();
    let zero_unsigned: Amount<NonNegative> = Amount::zero();

    assert_eq!(
        i64::from(zero_signed),
        0,
        "Amount::<NegativeAllowed>::zero() must be 0, got {}",
        i64::from(zero_signed)
    );
    assert_eq!(
        i64::from(zero_unsigned),
        0,
        "Amount::<NonNegative>::zero() must be 0, got {}",
        i64::from(zero_unsigned)
    );

    for value in [-MAX_MONEY, -1, 0, 1, 4_999_999, MAX_MONEY].iter() {
        let amount = amt(*value);
        let plus_zero = (amount + zero_signed).expect("adding zero never leaves the range");
        let minus_zero = (amount - zero_signed).expect("subtracting zero never leaves the range");

        assert_eq!(
            i64::from(plus_zero),
            *value,
            "{} + zero must be {}, got {}",
            value,
            value,
            i64::from(plus_zero)
        );
        assert_eq!(
            i64::from(minus_zero),
            *value,
            "{} - zero must be {}, got {}",
            value,
            value,
            i64::from(minus_zero)
        );
    }
}

#[test]
fn the_debug_format_names_the_constraint_the_amount_was_built_with() {
    // Two `Amount`s with the same value but different constraints are
    // different types with different validity rules, and the Debug output
    // says which one it is. Before this milestone both printed `Amount(5)`.
    let unsigned: Amount<NonNegative> = Amount::try_from(5).expect("5 is nonnegative");
    let signed: Amount<NegativeAllowed> = Amount::try_from(-5).expect("-5 is allowed");

    let unsigned_debug = format!("{:?}", unsigned);
    let signed_debug = format!("{:?}", signed);

    assert!(
        unsigned_debug.starts_with("Amount<"),
        "the Debug output must open with `Amount<`, got {:?}",
        unsigned_debug
    );
    assert!(
        unsigned_debug.contains("NonNegative"),
        "the Debug output of an Amount<NonNegative> must name NonNegative, got {:?}",
        unsigned_debug
    );
    assert!(
        unsigned_debug.ends_with("(5)"),
        "the Debug output must end with the zatoshi value in parentheses, got {:?}",
        unsigned_debug
    );

    assert!(
        signed_debug.contains("NegativeAllowed"),
        "the Debug output of an Amount<NegativeAllowed> must name NegativeAllowed, got {:?}",
        signed_debug
    );
    assert!(
        signed_debug.ends_with("(-5)"),
        "the Debug output must end with the zatoshi value in parentheses, got {:?}",
        signed_debug
    );
}

// ---------------------------------------------------------------- negation

#[test]
fn negating_a_signed_amount_flips_its_sign() {
    // The sapling and orchard value balances are *negated* when they are put
    // into a ValueBalance, so this impl is on the consensus path.
    for value in [-MAX_MONEY, -7654321, -1, 0, 1, 7654321, MAX_MONEY].iter() {
        let negated = -amt(*value);
        assert_eq!(
            i64::from(negated),
            -*value,
            "-({}) must be {}, got {}",
            value,
            -*value,
            i64::from(negated)
        );

        // Negation is its own inverse, and never leaves the range, because
        // NegativeAllowed is symmetric about zero.
        assert_eq!(
            i64::from(-negated),
            *value,
            "negating {} twice must return it unchanged",
            value
        );
    }

    let zero: Amount<NegativeAllowed> = Amount::zero();
    assert_eq!(i64::from(-zero), 0, "negating zero must stay zero");
}

// ---------------------------------------------------------------- summing

#[test]
fn an_empty_sum_is_zero() {
    let empty: Vec<Amount<NegativeAllowed>> = Vec::new();

    let by_value: SumResult<NegativeAllowed> = empty.clone().into_iter().sum();
    let by_reference: SumResult<NegativeAllowed> = empty.iter().sum();

    assert_eq!(
        i64::from(by_value.expect("an empty sum is always valid")),
        0,
        "summing no amounts by value must give zero"
    );
    assert_eq!(
        i64::from(by_reference.expect("an empty sum is always valid")),
        0,
        "summing no amounts by reference must give zero"
    );
}

#[test]
fn a_sum_in_range_matches_the_plain_integer_sum() {
    let cases: [&[i64]; 5] = [
        &[1],
        &[1, -1, 0],
        &[100, 200, 300, -50],
        &[-MAX_MONEY, MAX_MONEY],
        &[MAX_MONEY / 2, MAX_MONEY / 2, -MAX_MONEY / 2],
    ];

    for values in cases.iter() {
        // The expected value, computed here, in plain i64.
        let mut expected: i64 = 0;
        for v in values.iter() {
            expected += *v;
        }

        let amounts: Vec<Amount<NegativeAllowed>> = values.iter().map(|v| amt(*v)).collect();
        let summed: SumResult<NegativeAllowed> = amounts.into_iter().sum();
        let summed = summed.unwrap_or_else(|e| {
            panic!("summing {:?} must succeed, got {:?}", values, e)
        });

        assert_eq!(
            i64::from(summed),
            expected,
            "summing {:?} must give {}, got {}",
            values,
            expected,
            i64::from(summed)
        );
    }
}

#[test]
fn summing_by_reference_agrees_with_summing_by_value() {
    let values: [i64; 6] = [123, -456, 789, 0, MAX_MONEY, -MAX_MONEY];
    let amounts: Vec<Amount<NegativeAllowed>> = values.iter().map(|v| amt(*v)).collect();

    let by_reference: SumResult<NegativeAllowed> = amounts.iter().sum();
    let by_value: SumResult<NegativeAllowed> = amounts.clone().into_iter().sum();

    let by_reference = by_reference.expect("the reference sum is in range");
    let by_value = by_value.expect("the by-value sum is in range");

    assert_eq!(
        i64::from(by_reference),
        i64::from(by_value),
        "the two Sum impls must agree: by reference {} vs by value {}",
        i64::from(by_reference),
        i64::from(by_value)
    );
    assert_eq!(
        i64::from(by_value),
        456,
        "123 - 456 + 789 + 0 + MAX_MONEY - MAX_MONEY is 456, got {}",
        i64::from(by_value)
    );

    // Summing an iterator must not consume or reorder the source: the vector
    // is still usable and unchanged.
    assert_eq!(
        amounts.len(),
        6,
        "summing by reference must leave the source collection intact"
    );
}

#[test]
fn a_total_outside_max_money_is_a_range_error_naming_the_total() {
    // Three amounts, each individually valid, whose total is MAX_MONEY + 1.
    let values: [i64; 3] = [MAX_MONEY, MAX_MONEY, 1 - MAX_MONEY];
    let expected_total: i64 = MAX_MONEY + 1;

    let amounts: Vec<Amount<NegativeAllowed>> = values.iter().map(|v| amt(*v)).collect();

    let by_value: SumResult<NegativeAllowed> = amounts.clone().into_iter().sum();
    let by_reference: SumResult<NegativeAllowed> = amounts.iter().sum();

    assert_eq!(
        by_value, by_reference,
        "both Sum impls must report the same error"
    );

    match by_value {
        Err(AmountError::Contains { range, value }) => {
            assert_eq!(
                range,
                -MAX_MONEY..=MAX_MONEY,
                "the reported range must be the NegativeAllowed range"
            );
            assert_eq!(
                value, expected_total,
                "the error must report the whole i64 total {}, not a partial sum, got {}",
                expected_total, value
            );
        }
        other => panic!(
            "a total of MAX_MONEY + 1 must be Error::Contains, got {:?}",
            other
        ),
    }

    // The same, below the bottom of the range.
    let low: Vec<Amount<NegativeAllowed>> =
        [-MAX_MONEY, -MAX_MONEY, MAX_MONEY - 1].iter().map(|v| amt(*v)).collect();
    let low_sum: SumResult<NegativeAllowed> = low.into_iter().sum();
    match low_sum {
        Err(AmountError::Contains { value, .. }) => assert_eq!(
            value,
            -MAX_MONEY - 1,
            "the error must report the whole i64 total, got {}",
            value
        ),
        other => panic!(
            "a total of -MAX_MONEY - 1 must be Error::Contains, got {:?}",
            other
        ),
    }
}

#[test]
fn a_total_that_overflows_i64_is_a_sum_overflow_error() {
    // MAX_MONEY is 2_100_000_000_000_000 zatoshi, so i64::MAX / MAX_MONEY of
    // them still fit in an i64 and one more does not. The running total is
    // out of Amount range long before that, but the error a caller must see
    // is the one that says the i64 itself overflowed -- there is no total to
    // report.
    let fits = max_money_amounts_that_fit_in_i64();
    assert!(
        (fits as i128) * (MAX_MONEY as i128) <= i64::MAX as i128,
        "{} amounts of MAX_MONEY must still fit in an i64",
        fits
    );
    assert!(
        ((fits + 1) as i128) * (MAX_MONEY as i128) > i64::MAX as i128,
        "{} amounts of MAX_MONEY must overflow an i64",
        fits + 1
    );

    let overflowing: Vec<Amount<NegativeAllowed>> =
        std::iter::repeat(amt(MAX_MONEY)).take(fits + 1).collect();

    let by_value: SumResult<NegativeAllowed> = overflowing.clone().into_iter().sum();
    let by_reference: SumResult<NegativeAllowed> = overflowing.iter().sum();

    assert_eq!(
        by_value,
        Err(AmountError::SumOverflow),
        "{} amounts of MAX_MONEY must be Error::SumOverflow, got {:?}",
        fits + 1,
        by_value
    );
    assert_eq!(
        by_reference,
        Err(AmountError::SumOverflow),
        "the by-reference Sum impl must report SumOverflow too, got {:?}",
        by_reference
    );

    // And in the negative direction.
    let underflowing: Vec<Amount<NegativeAllowed>> =
        std::iter::repeat(amt(-MAX_MONEY)).take(fits + 1).collect();
    let underflow: SumResult<NegativeAllowed> = underflowing.into_iter().sum();
    assert_eq!(
        underflow,
        Err(AmountError::SumOverflow),
        "{} amounts of -MAX_MONEY must be Error::SumOverflow, got {:?}",
        fits + 1,
        underflow
    );

    assert_eq!(
        AmountError::SumOverflow.to_string(),
        "i64 overflow when summing i64 amounts",
        "Error::SumOverflow must display its documented message"
    );
}
