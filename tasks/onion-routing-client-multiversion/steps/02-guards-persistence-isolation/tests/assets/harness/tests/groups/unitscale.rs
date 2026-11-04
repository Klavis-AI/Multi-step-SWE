//! Group `unitscale` - the bounded, unit-carrying integers the parameters use.
//!
//! Graded through `tor_units::{BoundedInt32, Percentage, IntegerMilliseconds,
//! IntegerSeconds, IntegerDays, SendMeVersion, Error}`. Consensus parameters
//! arrive as bare `i32`s; these wrappers are what stop an out-of-range or
//! wrongly scaled value from reaching the guard and timeout logic.

use std::convert::{TryFrom, TryInto};
use std::time::Duration;
use tor_units::{
    BoundedInt32, Error, IntegerDays, IntegerMilliseconds, IntegerSeconds, Percentage,
    SendMeVersion,
};

#[test]
fn unitscale__bounded_integers_clamp_to_their_declared_range() {
    type B = BoundedInt32<10, 20>;
    assert_eq!(B::LOWER, 10);
    assert_eq!(B::UPPER, 20);

    assert_eq!(B::saturating_new(5).get(), 10, "a low value did not clamp up");
    assert_eq!(B::saturating_new(25).get(), 20, "a high value did not clamp down");
    assert_eq!(B::saturating_new(15).get(), 15, "an in-range value moved");
    assert_eq!(B::saturating_new(10).get(), 10);
    assert_eq!(B::saturating_new(20).get(), 20);

    // The same clamping applies when the value comes from a string.
    assert_eq!(
        B::saturating_from_str("3").expect("a numeric string failed to parse").get(),
        10
    );
    assert!(
        B::saturating_from_str("not a number").is_err(),
        "a non-numeric string was accepted"
    );
}

#[test]
fn unitscale__checked_construction_names_the_bound_that_was_missed() {
    type B = BoundedInt32<10, 20>;
    match B::checked_new(9) {
        Err(Error::BelowLowerBound(v, b)) => {
            assert_eq!((v, b), (9, 10), "the error did not carry value and bound");
        }
        other => panic!("expected BelowLowerBound, got {:?}", other.map(|v| v.get())),
    }
    match B::checked_new(21) {
        Err(Error::AboveUpperBound(v, b)) => {
            assert_eq!((v, b), (21, 20));
        }
        other => panic!("expected AboveUpperBound, got {:?}", other.map(|v| v.get())),
    }
    assert_eq!(B::checked_new(20).expect("20 is in range").get(), 20);

    // `TryFrom<i32>` is the same check.
    assert!(B::try_from(21).is_err());
    assert_eq!(B::try_from(11).expect("11 is in range").get(), 11);
}

#[test]
fn unitscale__percentages_report_both_a_percent_and_a_fraction() {
    let quarter = Percentage::new(BoundedInt32::<0, 100>::checked_new(25).expect("25 in range"));
    assert_eq!(quarter.as_percent().get(), 25);
    assert!((quarter.as_fraction() - 0.25).abs() < 1e-12);

    let whole = Percentage::new(BoundedInt32::<0, 100>::checked_new(100).expect("100 in range"));
    assert!((whole.as_fraction() - 1.0).abs() < 1e-12);

    // Building one straight from an i32 applies the bound check first.
    let p: Percentage<BoundedInt32<10, 99>> =
        80.try_into().expect("80 is inside 10..=99");
    assert_eq!(p.as_percent().get(), 80);
    let bad: Result<Percentage<BoundedInt32<10, 99>>, Error> = 5.try_into();
    assert!(bad.is_err(), "a percentage below its bound was accepted");
}

#[test]
fn unitscale__each_integer_unit_scales_to_the_right_duration() {
    let ms = IntegerMilliseconds::new(BoundedInt32::<0, 100_000>::saturating_new(1_500));
    let s = IntegerSeconds::new(BoundedInt32::<0, 100_000>::saturating_new(90));
    let d = IntegerDays::new(BoundedInt32::<0, 3_650>::saturating_new(2));

    assert_eq!(
        Duration::try_from(ms).expect("milliseconds did not convert"),
        Duration::from_millis(1_500)
    );
    assert_eq!(
        Duration::try_from(s).expect("seconds did not convert"),
        Duration::from_secs(90)
    );
    assert_eq!(
        Duration::try_from(d).expect("days did not convert"),
        Duration::from_secs(2 * 86_400),
        "a day must be 86400 seconds"
    );

    // The three wrappers are distinct scales, not interchangeable numbers.
    let one_ms = IntegerMilliseconds::new(BoundedInt32::<0, 10>::saturating_new(1));
    let one_s = IntegerSeconds::new(BoundedInt32::<0, 10>::saturating_new(1));
    assert_ne!(
        Duration::try_from(one_ms).expect("ms"),
        Duration::try_from(one_s).expect("s")
    );
}

#[test]
fn unitscale__narrow_bounds_convert_to_the_matching_primitive() {
    assert!(bool::from(BoundedInt32::<0, 1>::saturating_new(1)));
    assert!(!bool::from(BoundedInt32::<0, 1>::saturating_new(0)));
    assert!(
        !bool::from(BoundedInt32::<0, 1>::saturating_new(-4)),
        "a negative flag did not clamp to false"
    );

    assert_eq!(u8::from(BoundedInt32::<0, 255>::saturating_new(255)), 255);
    assert_eq!(u8::from(BoundedInt32::<0, 255>::saturating_new(300)), 255);

    let n: u64 = BoundedInt32::<0, 1000>::saturating_new(7)
        .try_into()
        .expect("a non-negative bounded value did not convert to u64");
    assert_eq!(n, 7);
    let neg: Result<u64, Error> = BoundedInt32::<{ -10 }, 10>::saturating_new(-3).try_into();
    assert!(
        matches!(neg, Err(Error::Negative)),
        "a negative value converted to an unsigned type"
    );

    assert_eq!(SendMeVersion::new(3).get(), 3);
    assert_eq!(
        SendMeVersion::try_from(255).expect("255 is a valid version").get(),
        255
    );
    assert!(
        SendMeVersion::try_from(256).is_err(),
        "a version above 255 was accepted"
    );
}
