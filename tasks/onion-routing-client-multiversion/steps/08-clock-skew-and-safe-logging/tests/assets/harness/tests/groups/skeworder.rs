//! Group `skeworder`: reported clock skews order by their *signed* distance
//! from the current time, which is what lets an estimator take a median.
//!
//! Traces to instruction.md section 1: "Clock-skew inference from guards and
//! fallbacks".  Interface: `impl Ord for tor_proto::ClockSkew`.

use std::cmp::Ordering;
use std::time::Duration;
use tor_proto::ClockSkew;

/// `n` seconds.
fn s(n: u64) -> Duration {
    Duration::from_secs(n)
}

#[test]
fn skeworder__slow_sorts_before_none_and_fast() {
    assert_eq!(ClockSkew::Slow(s(1)).cmp(&ClockSkew::None), Ordering::Less);
    assert_eq!(
        ClockSkew::Slow(s(1)).cmp(&ClockSkew::Fast(s(1))),
        Ordering::Less
    );
}

#[test]
fn skeworder__a_larger_slow_value_sorts_first() {
    // A clock that is slow by an hour is further below "now" than one that is
    // slow by a minute, so it sorts earlier.
    assert_eq!(
        ClockSkew::Slow(s(3600)).cmp(&ClockSkew::Slow(s(60))),
        Ordering::Less
    );
    assert_eq!(
        ClockSkew::Slow(s(60)).cmp(&ClockSkew::Slow(s(3600))),
        Ordering::Greater
    );
    assert_eq!(
        ClockSkew::Slow(s(60)).cmp(&ClockSkew::Slow(s(60))),
        Ordering::Equal
    );
}

#[test]
fn skeworder__a_larger_fast_value_sorts_last() {
    assert_eq!(
        ClockSkew::Fast(s(3600)).cmp(&ClockSkew::Fast(s(60))),
        Ordering::Greater
    );
    assert_eq!(
        ClockSkew::Fast(s(60)).cmp(&ClockSkew::Fast(s(3600))),
        Ordering::Less
    );
}

#[test]
fn skeworder__none_sits_between_slow_and_fast() {
    assert_eq!(
        ClockSkew::None.cmp(&ClockSkew::Slow(s(1))),
        Ordering::Greater
    );
    assert_eq!(ClockSkew::None.cmp(&ClockSkew::Fast(s(1))), Ordering::Less);
    assert_eq!(ClockSkew::None.cmp(&ClockSkew::None), Ordering::Equal);
}

#[test]
fn skeworder__sorting_yields_signed_order() {
    let mut v = vec![
        ClockSkew::Fast(s(3600)),
        ClockSkew::Slow(s(60)),
        ClockSkew::None,
        ClockSkew::Fast(s(60)),
        ClockSkew::Slow(s(3600)),
    ];
    v.sort();
    assert_eq!(
        v,
        vec![
            ClockSkew::Slow(s(3600)),
            ClockSkew::Slow(s(60)),
            ClockSkew::None,
            ClockSkew::Fast(s(60)),
            ClockSkew::Fast(s(3600)),
        ]
    );
    // The signed seconds of the sorted list are monotonically increasing.
    let secs: Vec<f64> = v.iter().map(|sk| sk.as_secs_f64()).collect();
    assert_eq!(secs, vec![-3600.0, -60.0, 0.0, 60.0, 3600.0]);
}
