//! Group `guardretry`: when every guard or fallback has failed -- the
//! situation in which a skewed clock is diagnosed -- the failure says whether
//! and when retrying could help.
//!
//! Traces to instruction.md section 1: "Clock-skew inference from guards and
//! fallbacks, used to explain bootstrap failure".  Interface:
//! `tor_guardmgr::PickGuardError`, its `HasRetryTime` and `HasKind`
//! implementations, and `tor_guardmgr::fallback::FallbackList::choose`.

use std::time::{Duration, Instant};
use tor_error::{AbsRetryTime, ErrorKind, HasKind, HasRetryTime, RetryTime};
use tor_guardmgr::fallback::FallbackList;
use tor_guardmgr::PickGuardError;

#[test]
fn guardretry__a_known_retry_time_is_passed_through() {
    let when = Instant::now() + Duration::from_secs(600);
    let err = PickGuardError::AllGuardsDown {
        retry_at: Some(when),
    };
    assert_eq!(err.retry_time(), RetryTime::At(when));
    assert_eq!(
        err.abs_retry_time(Instant::now(), || Duration::from_secs(1)),
        AbsRetryTime::At(when)
    );
}

#[test]
fn guardretry__an_unknown_retry_time_becomes_a_random_backoff() {
    let err = PickGuardError::AllGuardsDown { retry_at: None };
    assert_eq!(err.retry_time(), RetryTime::AfterWaiting);
    let now = Instant::now();
    assert_eq!(
        err.abs_retry_time(now, || Duration::from_secs(9)),
        AbsRetryTime::At(now + Duration::from_secs(9))
    );
}

#[test]
fn guardretry__an_empty_or_unusable_universe_is_not_retriable() {
    assert_eq!(PickGuardError::NoGuardsUsable.retry_time(), RetryTime::Never);
    assert_eq!(
        PickGuardError::NoCandidatesAvailable.retry_time(),
        RetryTime::Never
    );
}

#[test]
fn guardretry__fallbacks_follow_the_same_rule_as_guards() {
    let when = Instant::now() + Duration::from_secs(60);
    assert_eq!(
        PickGuardError::AllFallbacksDown {
            retry_at: Some(when)
        }
        .retry_time(),
        RetryTime::At(when)
    );
    assert_eq!(
        PickGuardError::AllFallbacksDown { retry_at: None }.retry_time(),
        RetryTime::AfterWaiting
    );
}

#[test]
fn guardretry__the_error_kind_separates_access_failure_from_no_path() {
    assert_eq!(
        PickGuardError::AllGuardsDown { retry_at: None }.kind(),
        ErrorKind::TorAccessFailed
    );
    assert_eq!(
        PickGuardError::AllFallbacksDown { retry_at: None }.kind(),
        ErrorKind::TorAccessFailed
    );
    assert_eq!(PickGuardError::NoGuardsUsable.kind(), ErrorKind::NoPath);
    assert_eq!(
        PickGuardError::NoCandidatesAvailable.kind(),
        ErrorKind::NoPath
    );
}

#[test]
fn guardretry__an_empty_fallback_list_reports_that_all_fallbacks_are_down() {
    let list = FallbackList::default();
    assert!(list.is_empty());
    assert_eq!(list.len(), 0);

    let err = list
        .choose(&mut rand::thread_rng())
        .err()
        .expect("an empty list cannot yield a fallback");
    assert!(
        matches!(err, PickGuardError::AllFallbacksDown { retry_at: None }),
        "expected AllFallbacksDown, got {:?}",
        err
    );
    assert_eq!(err.to_string(), "All fallback directories are down");
    assert_eq!(err.retry_time(), RetryTime::AfterWaiting);
}
