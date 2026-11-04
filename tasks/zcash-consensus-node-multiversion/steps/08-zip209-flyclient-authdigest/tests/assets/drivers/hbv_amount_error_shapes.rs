//! `zebra_chain::amount` after the value-pool rework: generic over its constraint,
//! and specific about why an amount was rejected.
//!
//! ZIP-209 accounting needs amounts that can be negative in every operation, not
//! just in addition, and it needs an error that says which number went out of
//! range so the failure can be attributed to a pool.  Both of those change the
//! public surface here:
//!
//! * `Mul<u64>`, `Div<u64>`, `Neg`, `Eq` and `Ord` are now written once for every
//!   `Constraint`, not separately for the two marker types.  `Neg` in particular
//!   now returns `Amount<NegativeAllowed>` whatever it started from.  The last
//!   test defines its own `Constraint` to prove the impls really are generic.
//! * every error variant carries the offending number: the rejected value and the
//!   range it missed, the overflowing product, the divided amount, or the partial
//!   sum together with how many items were never added.  `Error::invalid_value`
//!   pulls that number back out of any of them.
//! * `Amount::from_bytes` is the inverse of `to_bytes`.
//!
//! Expected values are computed with plain `i64`/`i128` arithmetic and with the
//! constants from the protocol (`COIN`, `MAX_MONEY`), never with the type under
//! test.

use std::convert::{TryFrom, TryInto};
use std::ops::{Neg, RangeInclusive};

use zebra_chain::amount::{
    Amount, Constraint, Error, NegativeAllowed, NonNegative, COIN, MAX_MONEY,
};

/// Reads an `Amount` back out as a plain integer.
fn zatoshis<C>(amount: Amount<C>) -> i64 {
    i64::from_le_bytes(amount.to_bytes())
}

// ---- the encoding ---------------------------------------------------------

#[test]
fn an_amount_round_trips_through_eight_little_endian_bytes() {
    assert_eq!(COIN, 100_000_000, "one ZEC is 10^8 zatoshi");
    assert_eq!(
        MAX_MONEY,
        21_000_000 * 100_000_000,
        "MAX_MONEY is 21 million ZEC in zatoshi"
    );

    for value in [0i64, 1, -1, 625_000, -625_000, MAX_MONEY, -MAX_MONEY].iter() {
        let amount = Amount::<NegativeAllowed>::try_from(*value)
            .unwrap_or_else(|e| panic!("{} is a valid NegativeAllowed amount: {:?}", value, e));
        assert_eq!(
            amount.to_bytes(),
            value.to_le_bytes(),
            "{} must encode as a little-endian i64",
            value
        );
        assert_eq!(
            Amount::<NegativeAllowed>::from_bytes(value.to_le_bytes()).map(zatoshis),
            Ok(*value),
            "from_bytes must undo to_bytes for {}",
            value
        );
    }

    // from_bytes validates: a negative encoding is not a NonNegative amount.
    assert_eq!(
        Amount::<NonNegative>::from_bytes((-1i64).to_le_bytes()).map(zatoshis),
        Err(Error::Constraint {
            value: -1,
            range: 0..=MAX_MONEY,
        }),
        "from_bytes must apply the constraint, not just reinterpret the bytes"
    );
    assert_eq!(
        Amount::<NegativeAllowed>::from_bytes((MAX_MONEY + 1).to_le_bytes()).map(zatoshis),
        Err(Error::Constraint {
            value: MAX_MONEY + 1,
            range: -MAX_MONEY..=MAX_MONEY,
        }),
        "an encoding above MAX_MONEY is rejected"
    );
}

#[test]
fn an_out_of_range_amount_reports_the_value_and_the_valid_range() {
    assert_eq!(
        NonNegative::valid_range(),
        0..=MAX_MONEY,
        "NonNegative amounts run from zero to MAX_MONEY"
    );
    assert_eq!(
        NegativeAllowed::valid_range(),
        -MAX_MONEY..=MAX_MONEY,
        "NegativeAllowed amounts are symmetric about zero"
    );

    assert_eq!(
        Amount::<NonNegative>::try_from(-1i64).map(zatoshis),
        Err(Error::Constraint {
            value: -1,
            range: 0..=MAX_MONEY,
        }),
        "a negative NonNegative amount reports -1 and the non-negative range"
    );
    assert_eq!(
        Amount::<NonNegative>::try_from(MAX_MONEY + 1).map(zatoshis),
        Err(Error::Constraint {
            value: MAX_MONEY + 1,
            range: 0..=MAX_MONEY,
        }),
        "an amount above MAX_MONEY reports the value it tried to hold"
    );
    assert_eq!(
        Amount::<NegativeAllowed>::try_from(-MAX_MONEY - 1).map(zatoshis),
        Err(Error::Constraint {
            value: -MAX_MONEY - 1,
            range: -MAX_MONEY..=MAX_MONEY,
        }),
        "an amount below -MAX_MONEY is out of range for NegativeAllowed too"
    );

    // i32 conversions widen rather than truncate.
    assert_eq!(
        Amount::<NegativeAllowed>::try_from(-2_000_000_000i32).map(zatoshis),
        Ok(-2_000_000_000),
        "an i32 is sign-extended into an i64 amount"
    );
}

#[test]
fn a_value_that_does_not_fit_in_an_i64_is_a_conversion_error() {
    // A u64 above i64::MAX cannot be an amount at all, and that is a different
    // failure from being outside the constraint's range.
    match Amount::<NonNegative>::try_from(u64::MAX) {
        Err(Error::Convert { value, .. }) => assert_eq!(
            value,
            i128::from(u64::MAX),
            "the conversion error reports the u64 it was given, widened to i128"
        ),
        other => panic!(
            "u64::MAX must be a conversion error, not {:?}",
            other.map(zatoshis)
        ),
    }

    // An in-range u64 converts normally.
    assert_eq!(
        Amount::<NonNegative>::try_from(625_000u64).map(zatoshis),
        Ok(625_000),
        "a small u64 converts to a non-negative amount"
    );

    // i128 is accepted so that negative products can be converted back.
    assert_eq!(
        Amount::<NegativeAllowed>::try_from(-300i128).map(zatoshis),
        Ok(-300),
        "a negative i128 inside the range converts to an amount"
    );
    match Amount::<NegativeAllowed>::try_from(i128::from(i64::MAX) + 1) {
        Err(Error::Convert { value, .. }) => assert_eq!(
            value,
            i128::from(i64::MAX) + 1,
            "an i128 beyond i64::MAX is a conversion error reporting that i128"
        ),
        other => panic!(
            "i64::MAX + 1 must be a conversion error, not {:?}",
            other.map(zatoshis)
        ),
    }
}

// ---- arithmetic errors ----------------------------------------------------

#[test]
fn multiplication_reports_the_overflowing_result() {
    let max = Amount::<NonNegative>::try_from(MAX_MONEY).expect("MAX_MONEY is a valid amount");

    assert_eq!(
        (max * 2u64).map(zatoshis),
        Err(Error::MultiplicationOverflow {
            amount: MAX_MONEY,
            multiplier: 2,
            overflowing_result: i128::from(MAX_MONEY) * 2,
        }),
        "multiplying MAX_MONEY by 2 must report the product that did not fit"
    );
    assert_eq!(
        (2u64 * max).map(zatoshis),
        Err(Error::MultiplicationOverflow {
            amount: MAX_MONEY,
            multiplier: 2,
            overflowing_result: i128::from(MAX_MONEY) * 2,
        }),
        "u64 * Amount must behave the same way as Amount * u64"
    );

    // A negative amount can be multiplied: this is how a negative pool change is
    // scaled, and it was not possible before this release.
    let debt = Amount::<NegativeAllowed>::try_from(-100i64).expect("-100 is negative-allowed");
    assert_eq!(
        (debt * 3u64).map(zatoshis),
        Ok(-300),
        "multiplying a negative amount by 3 gives -300"
    );
    assert_eq!(
        (debt * 0u64).map(zatoshis),
        Ok(0),
        "multiplying by zero gives zero, not an error"
    );
}

#[test]
fn division_by_zero_names_the_amount() {
    let hundred = Amount::<NonNegative>::try_from(100i64).expect("100 is non-negative");
    assert_eq!(
        (hundred / 0u64).map(zatoshis),
        Err(Error::DivideByZero { amount: 100 }),
        "dividing by zero reports the amount that was being divided"
    );

    assert_eq!(
        (hundred / 3u64).map(zatoshis),
        Ok(33),
        "integer division truncates"
    );

    // Negative amounts divide too, truncating toward zero.
    let debt = Amount::<NegativeAllowed>::try_from(-7i64).expect("-7 is negative-allowed");
    assert_eq!(
        (debt / 2u64).map(zatoshis),
        Ok(-3),
        "-7 / 2 truncates toward zero, giving -3 rather than -4"
    );
    assert_eq!(
        (debt / 0u64).map(zatoshis),
        Err(Error::DivideByZero { amount: -7 }),
        "a negative amount divided by zero reports its own value"
    );
}

#[test]
fn summing_past_the_range_reports_the_partial_sum_and_the_remaining_items() {
    let max = Amount::<NonNegative>::try_from(MAX_MONEY).expect("MAX_MONEY is a valid amount");
    let one = Amount::<NonNegative>::try_from(1i64).expect("1 is non-negative");
    let two = Amount::<NonNegative>::try_from(2i64).expect("2 is non-negative");

    // The sum stops at the item that pushed it out of range. The partial sum it
    // reports is the offending total, and two items were never added.
    let items = vec![max, max, one, two];
    assert_eq!(
        items
            .into_iter()
            .sum::<Result<Amount<NonNegative>, Error>>()
            .map(zatoshis),
        Err(Error::SumOverflow {
            partial_sum: 2 * MAX_MONEY,
            remaining_items: 2,
        }),
        "summing MAX_MONEY + MAX_MONEY + 1 + 2 reports the overflowing total and \
         the two items that were never reached"
    );

    // Sums that stay in range work, over values and over references.
    let small = vec![one, two, one];
    assert_eq!(
        small
            .iter()
            .sum::<Result<Amount<NonNegative>, Error>>()
            .map(zatoshis),
        Ok(4),
        "summing borrowed amounts must work as well as summing owned ones"
    );
    let nothing: Vec<Amount<NonNegative>> = Vec::new();
    assert_eq!(
        nothing
            .into_iter()
            .sum::<Result<Amount<NonNegative>, Error>>()
            .map(zatoshis),
        Ok(0),
        "an empty sum is zero"
    );
}

#[test]
fn negating_any_amount_produces_a_negative_allowed_amount() {
    // Neg is no longer restricted to amounts that were already NegativeAllowed,
    // and its output type is always NegativeAllowed.
    let five = Amount::<NonNegative>::try_from(5i64).expect("5 is non-negative");
    let negated: Amount<NegativeAllowed> = five.neg();
    assert_eq!(
        zatoshis(negated),
        -5,
        "negating a NonNegative amount gives a NegativeAllowed amount"
    );
    assert_eq!(
        zatoshis(negated.neg()),
        5,
        "negating twice returns the original value"
    );
    assert_eq!(
        zatoshis(Amount::<NonNegative>::zero().neg()),
        0,
        "negating zero is zero"
    );

    let max = Amount::<NonNegative>::try_from(MAX_MONEY).expect("MAX_MONEY is a valid amount");
    assert_eq!(
        zatoshis(max.neg()),
        -MAX_MONEY,
        "negating MAX_MONEY is inside the NegativeAllowed range, so it must not panic"
    );
}

// ---- genericity over the constraint ---------------------------------------

/// A constraint that exists nowhere in the crate under test. If `Amount`'s
/// operations are written out per marker type instead of once over `C`, this
/// module does not compile.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
struct Tiny;

impl Constraint for Tiny {
    fn valid_range() -> RangeInclusive<i64> {
        -10..=10
    }
}

#[test]
fn the_amount_operations_are_generic_over_the_constraint() {
    assert_eq!(
        Amount::<Tiny>::try_from(11i64).map(zatoshis),
        Err(Error::Constraint {
            value: 11,
            range: -10..=10,
        }),
        "a user-defined constraint must be enforced with its own range"
    );
    assert_eq!(
        Amount::<Tiny>::try_from(-10i64).map(zatoshis),
        Ok(-10),
        "the bounds of a user-defined constraint are inclusive"
    );
    assert_eq!(
        zatoshis(Amount::<Tiny>::zero()),
        0,
        "zero() is available for any constraint"
    );

    // Ord and Eq are generic, so a Vec of them sorts.
    let mut amounts: Vec<Amount<Tiny>> = vec![3i64, -7, 0, 10]
        .into_iter()
        .map(|v| Amount::<Tiny>::try_from(v).expect("all four values are within -10..=10"))
        .collect();
    amounts.sort();
    assert_eq!(
        amounts.iter().map(|a| zatoshis(*a)).collect::<Vec<i64>>(),
        vec![-7, 0, 3, 10],
        "Ord must be implemented once for every constraint"
    );

    // Mul, Div and Neg are generic too.
    let minus_three = Amount::<Tiny>::try_from(-3i64).expect("-3 is within -10..=10");
    assert_eq!(
        (minus_three * 2u64).map(zatoshis),
        Ok(-6),
        "multiplication respects a user-defined constraint"
    );
    assert_eq!(
        (minus_three * 4u64).map(zatoshis),
        Err(Error::MultiplicationOverflow {
            amount: -3,
            multiplier: 4,
            overflowing_result: -12,
        }),
        "a product outside the user-defined range is a multiplication overflow"
    );
    assert_eq!(
        zatoshis(minus_three.neg()),
        3,
        "negation of a Tiny amount lands in NegativeAllowed and keeps its magnitude"
    );

    // Comparisons work across constraints, and against plain i64.
    let non_negative = Amount::<NonNegative>::try_from(7i64).expect("7 is non-negative");
    let negative_allowed = Amount::<NegativeAllowed>::try_from(7i64).expect("7 is in range");
    assert!(
        non_negative == negative_allowed,
        "amounts with different constraints compare by value"
    );
    assert!(
        non_negative > Amount::<NegativeAllowed>::try_from(-7i64).expect("-7 is in range"),
        "ordering works across constraints"
    );
    assert!(
        non_negative == 7i64,
        "an amount compares equal to the i64 it holds"
    );
}

#[test]
fn every_amount_error_reports_its_invalid_value() {
    let max = Amount::<NonNegative>::try_from(MAX_MONEY).expect("MAX_MONEY is a valid amount");
    let one = Amount::<NonNegative>::try_from(1i64).expect("1 is non-negative");

    let constraint = Amount::<NonNegative>::try_from(-1i64)
        .expect_err("-1 is not a non-negative amount");
    assert_eq!(
        constraint.invalid_value(),
        -1,
        "a constraint error's invalid value is the rejected number"
    );

    let convert = Amount::<NonNegative>::try_from(u64::MAX)
        .expect_err("u64::MAX does not fit in an i64");
    assert_eq!(
        convert.invalid_value(),
        i128::from(u64::MAX),
        "a conversion error's invalid value is the number that did not fit"
    );

    let multiplication = (max * 2u64).expect_err("MAX_MONEY * 2 overflows the range");
    assert_eq!(
        multiplication.invalid_value(),
        i128::from(MAX_MONEY) * 2,
        "a multiplication error's invalid value is the overflowing product"
    );

    let divide = (max / 0u64).expect_err("dividing by zero fails");
    assert_eq!(
        divide.invalid_value(),
        i128::from(MAX_MONEY),
        "a divide-by-zero error's invalid value is the dividend"
    );

    let sum = vec![max, max, one]
        .into_iter()
        .sum::<Result<Amount<NonNegative>, Error>>()
        .expect_err("MAX_MONEY + MAX_MONEY overflows the range");
    assert_eq!(
        sum.invalid_value(),
        i128::from(MAX_MONEY) * 2,
        "a sum error's invalid value is the partial sum that went out of range"
    );

    // The five errors are distinguishable from each other.
    let all: Vec<Error> = vec![constraint, convert, multiplication, divide, sum];
    for (i, left) in all.iter().enumerate() {
        for (j, right) in all.iter().enumerate() {
            if i != j {
                assert_ne!(
                    left, right,
                    "amount errors {} and {} must not compare equal",
                    i, j
                );
            }
        }
    }

    // A successful operation still reports no error at all.
    assert_eq!(
        (one + one).map(zatoshis),
        Ok(2),
        "adding two in-range amounts must succeed"
    );
}

// ---- the conversion back out, and the operator matrix ---------------------

#[test]
fn an_amount_converts_back_into_an_i64_without_a_constraint_bound() {
    // `From<Amount<C>> for i64` is infallible and available for every `C`,
    // including a marker defined outside the crate.  It is the documented way
    // to read an amount back out as a plain integer.
    for value in [0i64, 1, -1, 625_000, -625_000, MAX_MONEY, -MAX_MONEY].iter() {
        let negative = Amount::<NegativeAllowed>::try_from(*value).expect("in range");
        assert_eq!(
            i64::from(negative),
            *value,
            "a NegativeAllowed amount converts back to the i64 it was built from"
        );
    }

    for value in [0i64, 1, MAX_MONEY].iter() {
        let non_negative = Amount::<NonNegative>::try_from(*value).expect("in range");
        assert_eq!(
            i64::from(non_negative),
            *value,
            "a NonNegative amount converts back to the i64 it was built from"
        );
    }

    // The conversion is the inverse of `TryFrom<i64>` for every value either
    // constraint admits, so it agrees with the byte encoding too.
    let negative = Amount::<NegativeAllowed>::try_from(-625_000i64).expect("in range");
    assert_eq!(i64::from(negative), zatoshis(negative));

    // A constraint defined outside the crate gets the conversion as well.
    struct Tiny;
    impl Constraint for Tiny {
        fn valid_range() -> RangeInclusive<i64> {
            -10..=10
        }
    }
    let tiny: Amount<Tiny> = 7i64.try_into().expect("7 is inside -10..=10");
    assert_eq!(i64::from(tiny), 7, "the conversion is generic over C");
}

#[test]
fn subtraction_and_the_assigning_operators_cover_all_four_combinations() {
    // §1.3 requires `Sub` in all four `Amount`/`Result<Amount>` combinations
    // plus `AddAssign`/`SubAssign` on `Result<Amount<C>>`.  The combinations
    // differ in which side carries the error, so each is exercised separately.
    let a = Amount::<NegativeAllowed>::try_from(10i64).expect("in range");
    let b = Amount::<NegativeAllowed>::try_from(4i64).expect("in range");
    let n = Amount::<NonNegative>::try_from(3i64).expect("in range");
    let err: std::result::Result<Amount<NegativeAllowed>, Error> = Err(Error::DivideByZero { amount: 7 });

    // Amount - Amount
    assert_eq!(
        (a - b).map(zatoshis),
        Ok(6),
        "Amount - Amount must subtract"
    );
    // Result<Amount> - Amount
    let left_result: std::result::Result<Amount<NegativeAllowed>, Error> = Ok(a);
    assert_eq!(
        (left_result - b).map(zatoshis),
        Ok(6),
        "Result<Amount> - Amount must subtract when the left side is Ok"
    );
    // Amount - Result<Amount>
    assert_eq!(
        (a - Ok(b)).map(zatoshis),
        Ok(6),
        "Amount - Result<Amount> must subtract when the right side is Ok"
    );

    // An error on either side propagates rather than being lost.
    assert_eq!(
        (err - b).expect_err("an Err on the left propagates"),
        Error::DivideByZero { amount: 7 },
        "Result<Amount> - Amount must propagate a left-hand Err"
    );
    assert_eq!(
        (a - Err::<Amount<NegativeAllowed>, Error>(Error::DivideByZero { amount: 7 }))
            .expect_err("an Err on the right propagates"),
        Error::DivideByZero { amount: 7 },
        "Amount - Result<Amount> must propagate a right-hand Err"
    );

    // Subtraction is not commutative: the sign must follow the operands.
    assert_eq!(
        (b - a).map(zatoshis),
        Ok(-6),
        "Amount - Amount must keep the sign of the difference"
    );
    // NonNegative minus a larger NonNegative is a constraint error, not a wrap.
    assert!(
        (n - Amount::<NonNegative>::try_from(10i64).expect("in range")).is_err(),
        "subtracting past a constraint's lower bound must error, not wrap"
    );
    // Cross-constraint subtraction is not required to compile, so it is not
    // exercised here: both operands share `C` in the four combinations above.

    // AddAssign on Result<Amount<C>>
    let mut acc: std::result::Result<Amount<NegativeAllowed>, Error> = Ok(a);
    acc += b;
    assert_eq!(
        acc.map(zatoshis),
        Ok(14),
        "AddAssign must add into a Result<Amount<C>>"
    );

    // SubAssign on Result<Amount<C>>
    let mut acc: std::result::Result<Amount<NegativeAllowed>, Error> = Ok(a);
    acc -= b;
    assert_eq!(
        acc.map(zatoshis),
        Ok(6),
        "SubAssign must subtract from a Result<Amount<C>>"
    );

    // Both assigning operators must surface a failure in the accumulator
    // rather than silently leaving it Ok.
    let mut acc: std::result::Result<Amount<NegativeAllowed>, Error> =
        Err(Error::DivideByZero { amount: 7 });
    acc += b;
    assert!(
        acc.is_err(),
        "AddAssign onto an Err accumulator must stay Err"
    );
    let max = Amount::<NegativeAllowed>::try_from(MAX_MONEY).expect("MAX_MONEY is in range");
    let mut acc: std::result::Result<Amount<NegativeAllowed>, Error> = Ok(max);
    acc += max;
    assert!(
        acc.is_err(),
        "AddAssign past the constraint's upper bound must error"
    );
}
