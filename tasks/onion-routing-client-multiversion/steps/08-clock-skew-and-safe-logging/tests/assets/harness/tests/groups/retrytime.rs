//! Group `retrytime`: turning "when may I try again" advice into concrete
//! instants, which is how a stuck bootstrap decides whether to keep going.
//!
//! Traces to instruction.md section 1: clock skew is inferred from repeated
//! guard and fallback failures and is "used to explain bootstrap failure", so
//! the retry advice attached to those failures is graded here.  Interface:
//! `tor_error::{RetryTime, AbsRetryTime}`.

use std::cell::Cell;
use std::time::{Duration, Instant};
use tor_error::{AbsRetryTime, RetryTime};

/// A delay chooser that must never be called.
fn never_called() -> Duration {
    panic!("choose_delay should not have been called");
}

#[test]
fn retrytime__each_variant_becomes_an_absolute_time() {
    let now = Instant::now();
    let ten = Duration::from_secs(10);

    assert_eq!(
        RetryTime::Immediate.absolute(now, never_called),
        AbsRetryTime::Immediate
    );
    assert_eq!(
        RetryTime::After(ten).absolute(now, never_called),
        AbsRetryTime::At(now + ten)
    );
    assert_eq!(
        RetryTime::At(now + ten).absolute(now, never_called),
        AbsRetryTime::At(now + ten)
    );
    assert_eq!(
        RetryTime::Never.absolute(now, never_called),
        AbsRetryTime::Never
    );
}

#[test]
fn retrytime__after_waiting_uses_the_supplied_delay() {
    let now = Instant::now();
    assert_eq!(
        RetryTime::AfterWaiting.absolute(now, || Duration::from_secs(7)),
        AbsRetryTime::At(now + Duration::from_secs(7))
    );
    // A delay so large that it cannot be added to `now` means "never".
    assert_eq!(
        RetryTime::After(Duration::MAX).absolute(now, never_called),
        AbsRetryTime::Never
    );
}

#[test]
fn retrytime__the_earliest_absolute_time_wins() {
    let now = Instant::now();
    let items = vec![
        RetryTime::Never,
        RetryTime::At(now + Duration::from_secs(300)),
        RetryTime::After(Duration::from_secs(30)),
        RetryTime::At(now + Duration::from_secs(120)),
    ];
    assert_eq!(
        RetryTime::earliest_absolute(items.into_iter(), now, never_called),
        Some(AbsRetryTime::At(now + Duration::from_secs(30)))
    );

    // `Immediate` beats every dated alternative.
    let items = vec![
        RetryTime::At(now + Duration::from_secs(1)),
        RetryTime::Immediate,
    ];
    assert_eq!(
        RetryTime::earliest_absolute(items.into_iter(), now, never_called),
        Some(AbsRetryTime::Immediate)
    );
}

#[test]
fn retrytime__the_delay_is_chosen_at_most_once() {
    let now = Instant::now();
    let calls = Cell::new(0_u32);
    let choose = || {
        calls.set(calls.get() + 1);
        Duration::from_secs(5)
    };
    let items = vec![
        RetryTime::AfterWaiting,
        RetryTime::AfterWaiting,
        RetryTime::AfterWaiting,
    ];
    assert_eq!(
        RetryTime::earliest_absolute(items.into_iter(), now, choose),
        Some(AbsRetryTime::At(now + Duration::from_secs(5)))
    );
    assert_eq!(calls.get(), 1);

    // If no item needs a delay, the chooser is never consulted at all.
    let calls = Cell::new(0_u32);
    let choose = || {
        calls.set(calls.get() + 1);
        Duration::from_secs(5)
    };
    let items = vec![RetryTime::Never, RetryTime::Immediate];
    assert_eq!(
        RetryTime::earliest_absolute(items.into_iter(), now, choose),
        Some(AbsRetryTime::Immediate)
    );
    assert_eq!(calls.get(), 0);
}

#[test]
fn retrytime__an_empty_set_of_advice_yields_nothing() {
    let now = Instant::now();
    assert_eq!(
        RetryTime::earliest_absolute(std::iter::empty(), now, never_called),
        None
    );
    assert_eq!(RetryTime::earliest_approx(std::iter::empty()), None);
}

#[test]
fn retrytime__the_loose_order_runs_from_immediate_to_never() {
    use std::cmp::Ordering;
    let now = Instant::now();
    let sec = Duration::from_secs(1);
    let sorted = vec![
        RetryTime::Immediate,
        RetryTime::AfterWaiting,
        RetryTime::After(sec * 10),
        RetryTime::After(sec * 20),
        RetryTime::At(now),
        RetryTime::At(now + sec * 30),
        RetryTime::Never,
    ];
    for (i, a) in sorted.iter().enumerate() {
        for (j, b) in sorted.iter().enumerate() {
            let want = i.cmp(&j);
            assert_eq!(
                a.loose_cmp(b),
                want,
                "loose_cmp({:?}, {:?}) should be {:?}",
                a,
                b,
                want
            );
        }
    }
    assert_eq!(
        RetryTime::After(sec).loose_cmp(&RetryTime::After(sec)),
        Ordering::Equal
    );
}

#[test]
fn retrytime__the_approximate_earliest_follows_the_loose_order() {
    let now = Instant::now();
    let sec = Duration::from_secs(1);
    assert_eq!(
        RetryTime::earliest_approx(
            vec![
                RetryTime::Never,
                RetryTime::At(now),
                RetryTime::AfterWaiting,
            ]
            .into_iter()
        ),
        Some(RetryTime::AfterWaiting)
    );
    assert_eq!(
        RetryTime::earliest_approx(
            vec![RetryTime::After(sec * 30), RetryTime::After(sec * 5)].into_iter()
        ),
        Some(RetryTime::After(sec * 5))
    );
    assert_eq!(
        RetryTime::earliest_approx(vec![RetryTime::Never, RetryTime::Never].into_iter()),
        Some(RetryTime::Never)
    );
}
