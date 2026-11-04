//! Group `skewsecs`: conversion between a reported clock skew and a signed
//! number of seconds, including the degenerate float cases.
//!
//! Traces to instruction.md section 1: "Clock-skew inference from guards and
//! fallbacks".  Interface: `tor_proto::ClockSkew::{as_secs_f64,
//! from_secs_f64}`.

use std::time::Duration;
use tor_proto::ClockSkew;

#[test]
fn skewsecs__slow_is_negative_and_fast_is_positive() {
    assert_eq!(ClockSkew::Slow(Duration::from_secs(90)).as_secs_f64(), -90.0);
    assert_eq!(ClockSkew::Fast(Duration::from_secs(90)).as_secs_f64(), 90.0);
}

#[test]
fn skewsecs__none_is_zero() {
    assert_eq!(ClockSkew::None.as_secs_f64(), 0.0);
}

#[test]
fn skewsecs__nan_is_rejected() {
    assert_eq!(ClockSkew::from_secs_f64(f64::NAN), None);
}

#[test]
fn skewsecs__zero_becomes_an_unskewed_value() {
    assert_eq!(ClockSkew::from_secs_f64(0.0), Some(ClockSkew::None));
    assert_eq!(ClockSkew::from_secs_f64(-0.0), Some(ClockSkew::None));
    // Subnormals are too small to be meaningful and are treated the same way.
    assert_eq!(
        ClockSkew::from_secs_f64(f64::MIN_POSITIVE / 2.0),
        Some(ClockSkew::None)
    );
}

#[test]
fn skewsecs__values_at_or_below_two_seconds_are_not_skew() {
    for secs in [1.0_f64, 1.999, 2.0] {
        assert_eq!(
            ClockSkew::from_secs_f64(secs),
            Some(ClockSkew::None),
            "{} seconds fast should not count as skew",
            secs
        );
        assert_eq!(
            ClockSkew::from_secs_f64(-secs),
            Some(ClockSkew::None),
            "{} seconds slow should not count as skew",
            secs
        );
    }
}

#[test]
fn skewsecs__values_above_two_seconds_round_trip() {
    let fast = ClockSkew::from_secs_f64(2.5).expect("2.5s is a usable skew");
    assert_eq!(fast, ClockSkew::Fast(Duration::from_millis(2500)));
    assert_eq!(fast.as_secs_f64(), 2.5);

    let slow = ClockSkew::from_secs_f64(-2.5).expect("-2.5s is a usable skew");
    assert_eq!(slow, ClockSkew::Slow(Duration::from_millis(2500)));
    assert_eq!(slow.as_secs_f64(), -2.5);
}

#[test]
fn skewsecs__infinities_saturate_at_the_largest_duration() {
    assert_eq!(
        ClockSkew::from_secs_f64(f64::INFINITY),
        Some(ClockSkew::Fast(Duration::MAX))
    );
    assert_eq!(
        ClockSkew::from_secs_f64(f64::NEG_INFINITY),
        Some(ClockSkew::Slow(Duration::MAX))
    );
}
