//! Group `retrydelay`: the decorrelated-jitter retry schedule that replaced
//! the fixed guard/download retry timings.
//!
//! Graded item: `tor_basic_utils::retry::RetryDelay`.

use std::time::Duration;

use tor_basic_utils::retry::RetryDelay;

use crate::common::seeded_rng;

/// The floor on the lower bound, in milliseconds (dir-spec "decorrelated
/// jitter" uses a one-second base delay).
const FLOOR_MS: u64 = 1000;

#[test]
fn retrydelay__base_delay_below_floor_is_raised_to_one_second() {
    // With a low bound of 1000 the first draw is taken from [1000, 1001):
    // exactly one value, so this is independent of the RNG.
    let mut rng = seeded_rng(1);
    for base in [0u32, 1, 50, 999] {
        let mut rd = RetryDelay::from_msec(base);
        assert_eq!(
            rd.next_delay(&mut rng),
            Duration::from_millis(FLOOR_MS),
            "base delay {} should be clamped up to the 1000ms floor",
            base
        );
    }
}

#[test]
fn retrydelay__first_delay_equals_base_when_base_above_floor() {
    let mut rng = seeded_rng(2);
    for base in [1000u32, 2500, 5000, 60_000] {
        let mut rd = RetryDelay::from_msec(base);
        assert_eq!(
            rd.next_delay(&mut rng),
            Duration::from_millis(u64::from(base)),
            "the first delay is drawn from [base, base+1)"
        );
    }
}

#[test]
fn retrydelay__default_starts_at_the_floor() {
    let mut rng = seeded_rng(3);
    let mut rd = RetryDelay::default();
    assert_eq!(rd.next_delay(&mut rng), Duration::from_millis(FLOOR_MS));
}

#[test]
fn retrydelay__from_duration_uses_whole_milliseconds() {
    let mut rng = seeded_rng(4);
    let mut rd = RetryDelay::from_duration(Duration::new(1, 500_000_000));
    assert_eq!(rd.next_delay(&mut rng), Duration::from_millis(1500));

    let mut rd = RetryDelay::from_duration(Duration::from_millis(7_000));
    assert_eq!(rd.next_delay(&mut rng), Duration::from_millis(7_000));

    // Sub-millisecond bases still land on the floor.
    let mut rd = RetryDelay::from_duration(Duration::from_micros(500));
    assert_eq!(rd.next_delay(&mut rng), Duration::from_millis(FLOOR_MS));
}

#[test]
fn retrydelay__from_duration_saturates_at_the_maximum_low_bound() {
    // u32::MAX - 1 milliseconds is the largest representable lower bound.
    let mut rng = seeded_rng(5);
    let mut rd = RetryDelay::from_duration(Duration::from_secs(10_000_000));
    assert_eq!(
        rd.next_delay(&mut rng),
        Duration::from_millis(u64::from(u32::MAX - 1))
    );
}

#[test]
fn retrydelay__each_delay_lies_between_the_base_and_three_times_the_last() {
    let mut rng = seeded_rng(6);
    let base_ms: u64 = 1000;
    let mut rd = RetryDelay::from_msec(base_ms as u32);
    let mut last_ms: u64 = 0;
    for step in 0..40 {
        let got = rd.next_delay(&mut rng).as_millis() as u64;
        let high = std::cmp::max(base_ms + 1, last_ms.saturating_mul(3));
        assert!(
            got >= base_ms,
            "step {}: {}ms is below the base delay",
            step,
            got
        );
        assert!(
            got < high,
            "step {}: {}ms is not below 3x the previous delay ({}ms)",
            step,
            got,
            last_ms
        );
        last_ms = got;
    }
}

#[test]
fn retrydelay__delays_are_a_pure_function_of_the_supplied_rng() {
    // The schedule must take its randomness from the caller's Rng, not from a
    // private thread-local generator: two runs with identically seeded
    // generators have to agree exactly.
    let collect = || {
        let mut rng = seeded_rng(1234);
        let mut rd = RetryDelay::from_msec(2000);
        (0..12)
            .map(|_| rd.next_delay(&mut rng))
            .collect::<Vec<Duration>>()
    };
    let first = collect();
    let second = collect();
    assert_eq!(first, second);
    assert_eq!(first[0], Duration::from_millis(2000));
}

#[test]
fn retrydelay__the_schedule_is_jittered_rather_than_constant() {
    // Decorrelated jitter is not a fixed schedule: over a run the delays must
    // take more than one distinct value, and must grow beyond the base.
    let mut rng = seeded_rng(99);
    let mut rd = RetryDelay::from_msec(1000);
    let delays: Vec<u64> = (0..24)
        .map(|_| rd.next_delay(&mut rng).as_millis() as u64)
        .collect();
    let mut distinct = delays.clone();
    distinct.sort_unstable();
    distinct.dedup();
    assert!(
        distinct.len() > 4,
        "expected a jittered schedule, got {:?}",
        delays
    );
    assert!(
        delays.iter().any(|d| *d > 1000),
        "expected the schedule to back off above the base delay, got {:?}",
        delays
    );
}
