//! Group `skewmag`: the sign, magnitude and "is this worth reporting" rules of
//! the reported clock-skew value type.
//!
//! Traces to instruction.md section 1: "Clock-skew inference from guards and
//! fallbacks".  Interface: `tor_proto::ClockSkew`.

use std::time::Duration;
use tor_proto::ClockSkew;

/// The floor below which a reported difference is not treated as skew.
const FLOOR: Duration = Duration::from_secs(2);

/// The threshold above which a skew is worth telling the user about.
///
/// `tor-guardmgr`'s estimator publishes
/// `ClockSkew::from_secs_f64(mean).if_above(REPORTABLE)`, so this is the second
/// of the two filters every published estimate passes through.  The estimator
/// itself cannot be driven from outside its crate (see `TEST_INTERFACE.md`
/// section 14.1); the two filters it applies can be, and they are here.
const REPORTABLE: Duration = Duration::from_secs(15 * 60);

#[test]
fn skewmag__slow_and_fast_report_their_magnitude() {
    assert_eq!(
        ClockSkew::Slow(Duration::from_secs(747)).magnitude(),
        Duration::from_secs(747)
    );
    assert_eq!(
        ClockSkew::Fast(Duration::from_secs(333)).magnitude(),
        Duration::from_secs(333)
    );
    // The magnitude is unsigned: a slow clock and a fast clock of the same size
    // have the same magnitude even though they are different values.
    let slow = ClockSkew::Slow(Duration::from_secs(60));
    let fast = ClockSkew::Fast(Duration::from_secs(60));
    assert_eq!(slow.magnitude(), fast.magnitude());
    assert_ne!(slow, fast);
}

#[test]
fn skewmag__none_has_zero_magnitude() {
    assert_eq!(ClockSkew::None.magnitude(), Duration::from_secs(0));
}

#[test]
fn skewmag__is_skewed_is_false_only_for_none() {
    assert!(!ClockSkew::None.is_skewed());
    assert!(ClockSkew::Slow(Duration::from_secs(3)).is_skewed());
    assert!(ClockSkew::Fast(Duration::from_secs(3)).is_skewed());
    // Even a zero-sized Slow/Fast is "skewed": it is `if_above` that collapses
    // insignificant values, not the constructor.
    assert!(ClockSkew::Fast(Duration::from_secs(0)).is_skewed());
}

#[test]
fn skewmag__if_above_is_strict() {
    // Exactly at the threshold is not "above" it.
    assert_eq!(ClockSkew::Fast(FLOOR).if_above(FLOOR), ClockSkew::None);
    assert_eq!(ClockSkew::Slow(FLOOR).if_above(FLOOR), ClockSkew::None);

    let just_over = FLOOR + Duration::from_nanos(1);
    assert_eq!(
        ClockSkew::Fast(just_over).if_above(FLOOR),
        ClockSkew::Fast(just_over)
    );
    assert_eq!(
        ClockSkew::Slow(just_over).if_above(FLOOR),
        ClockSkew::Slow(just_over)
    );
}

#[test]
fn skewmag__if_above_applies_to_the_magnitude_not_the_sign() {
    let limit = Duration::from_secs(20);
    // A "slow" value is negative on the signed scale, but `if_above` compares
    // the unsigned magnitude, so a large slow value survives.
    assert_eq!(
        ClockSkew::Slow(Duration::from_secs(30)).if_above(limit),
        ClockSkew::Slow(Duration::from_secs(30))
    );
    assert_eq!(
        ClockSkew::Slow(Duration::from_secs(10)).if_above(limit),
        ClockSkew::None
    );
    // `None` never becomes skewed, whatever the limit.
    assert_eq!(
        ClockSkew::None.if_above(Duration::from_secs(0)),
        ClockSkew::None
    );
}

#[test]
fn skewmag__zero_sized_skew_is_distinct_from_none() {
    let zero = Duration::from_secs(0);
    assert_ne!(ClockSkew::Fast(zero), ClockSkew::None);
    assert_ne!(ClockSkew::Slow(zero), ClockSkew::None);
    // ... but collapses as soon as it is filtered against any floor.
    assert_eq!(ClockSkew::Fast(zero).if_above(zero), ClockSkew::None);
    assert_eq!(ClockSkew::Slow(zero).if_above(zero), ClockSkew::None);
}

#[test]
fn skewmag__the_reporting_threshold_is_fifteen_minutes() {
    // At or below fifteen minutes, an estimate is published as "not skewed"
    // rather than as a small skew.
    for quiet in [
        Duration::from_secs(0),
        Duration::from_secs(120),
        Duration::from_secs(14 * 60 + 59),
        REPORTABLE,
    ] {
        assert_eq!(ClockSkew::Slow(quiet).if_above(REPORTABLE), ClockSkew::None);
        assert_eq!(ClockSkew::Fast(quiet).if_above(REPORTABLE), ClockSkew::None);
        assert!(!ClockSkew::Slow(quiet).if_above(REPORTABLE).is_skewed());
        assert!(!ClockSkew::Fast(quiet).if_above(REPORTABLE).is_skewed());
    }

    // One second over is over.
    let loud = REPORTABLE + Duration::from_secs(1);
    assert_eq!(ClockSkew::Slow(loud).if_above(REPORTABLE), ClockSkew::Slow(loud));
    assert_eq!(ClockSkew::Fast(loud).if_above(REPORTABLE), ClockSkew::Fast(loud));
    assert!(ClockSkew::Fast(loud).if_above(REPORTABLE).is_skewed());

    // The two filters are independent and the reporting threshold is much the
    // coarser: a value that survives the two-second floor can still be too
    // small to report.
    let three = ClockSkew::Fast(Duration::from_secs(3));
    assert!(three.if_above(FLOOR).is_skewed());
    assert_eq!(three.if_above(REPORTABLE), ClockSkew::None);
}

#[test]
fn skewmag__a_mean_observation_becomes_a_reportable_estimate() {
    // Exactly the arithmetic an estimator performs on the mean of the skews it
    // has retained: convert the signed mean, then filter it.
    let reportable = ClockSkew::from_secs_f64(-1027.0)
        .expect("a finite mean is not degenerate")
        .if_above(REPORTABLE);
    assert_eq!(reportable, ClockSkew::Slow(Duration::from_secs(1027)));
    assert!(reportable.is_skewed());
    assert_eq!(reportable.magnitude().as_secs(), 1027);
    assert_eq!(reportable.as_secs_f64(), -1027.0);

    // A five-minute mean is genuine skew by the type's own floor, and still
    // not worth reporting.
    let quiet = ClockSkew::from_secs_f64(-300.0).expect("a finite mean is not degenerate");
    assert_eq!(quiet, ClockSkew::Slow(Duration::from_secs(300)));
    assert!(quiet.is_skewed());
    assert_eq!(quiet.if_above(REPORTABLE), ClockSkew::None);

    // A fast clock is symmetric.
    let fast = ClockSkew::from_secs_f64(3600.0)
        .expect("a finite mean is not degenerate")
        .if_above(REPORTABLE);
    assert_eq!(fast, ClockSkew::Fast(Duration::from_secs(3600)));
    assert_eq!(fast.as_secs_f64(), 3600.0);
}
