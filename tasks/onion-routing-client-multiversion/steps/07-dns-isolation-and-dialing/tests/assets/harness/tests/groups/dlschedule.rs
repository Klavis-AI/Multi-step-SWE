//! Group `dlschedule`: directory download retries now hand out a
//! decorrelated-jitter `RetryDelay` from `tor-basic-utils` instead of the
//! fixed schedule the previous checkpoint used.
//!
//! Graded items: `tor_dirmgr::DownloadSchedule` and the type of its
//! `schedule()` return value.

use std::time::Duration;

use tor_basic_utils::retry::RetryDelay;
use tor_dirmgr::DownloadSchedule;

use crate::common::seeded_rng;

/// First delay handed out by a schedule, in milliseconds.
fn first_delay_ms(sched: &DownloadSchedule) -> u64 {
    let mut rng = seeded_rng(7);
    // The annotation is load-bearing: the retry schedule must come from
    // `tor_basic_utils::retry`, not from a crate-local copy.
    let mut delay: RetryDelay = sched.schedule();
    delay.next_delay(&mut rng).as_millis() as u64
}

#[test]
fn dlschedule__schedule_returns_a_basic_utils_retry_delay() {
    let sched = DownloadSchedule::new(3, Duration::from_secs(4), 1);
    let delay: RetryDelay = sched.schedule();
    // The lower bound comes from the configured initial delay.
    let mut rng = seeded_rng(8);
    let mut delay = delay;
    assert_eq!(delay.next_delay(&mut rng), Duration::from_millis(4000));
}

#[test]
fn dlschedule__default_is_three_attempts_one_second_serial() {
    let sched = DownloadSchedule::default();
    assert_eq!(sched.n_attempts(), 3);
    assert_eq!(sched.attempts().collect::<Vec<u32>>(), vec![0, 1, 2]);
    assert_eq!(sched.parallelism(), 1);
    assert_eq!(first_delay_ms(&sched), 1000);
}

#[test]
fn dlschedule__zero_attempts_and_zero_parallelism_are_clamped_to_one() {
    let sched = DownloadSchedule::new(0, Duration::from_secs(1), 0);
    assert_eq!(sched.n_attempts(), 1);
    assert_eq!(sched.attempts().collect::<Vec<u32>>(), vec![0]);
    assert_eq!(sched.parallelism(), 1);
}

#[test]
fn dlschedule__attempt_count_and_parallelism_are_reported_back() {
    let sched = DownloadSchedule::new(5, Duration::from_secs(2), 4);
    assert_eq!(sched.n_attempts(), 5);
    assert_eq!(sched.attempts().collect::<Vec<u32>>(), vec![0, 1, 2, 3, 4]);
    assert_eq!(sched.parallelism(), 4);
}

#[test]
fn dlschedule__initial_delay_below_the_floor_is_raised() {
    let sched = DownloadSchedule::new(3, Duration::from_millis(10), 1);
    assert_eq!(first_delay_ms(&sched), 1000);
}

#[test]
fn dlschedule__initial_delay_above_the_floor_is_preserved() {
    let sched = DownloadSchedule::new(3, Duration::from_secs(7), 1);
    assert_eq!(first_delay_ms(&sched), 7000);

    // Equal schedules compare equal, and differing initial delays do not.
    assert_eq!(sched, DownloadSchedule::new(3, Duration::from_secs(7), 1));
    assert_ne!(sched, DownloadSchedule::new(3, Duration::from_secs(8), 1));
}
