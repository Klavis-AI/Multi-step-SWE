//! Inherited: `tor_checkable`'s timeliness wrapper still has to refuse to
//! unwrap an object outside its validity interval.
//!
//! Determinism: every case below builds its bounds as fixed offsets from a
//! single named instant, and that instant is a constant rather than
//! `SystemTime::now()`. The assertions never depended on the clock's *value* --
//! each one compares an instant derived from `now` against a bound derived from
//! the same `now`, so the reading cancelled out -- but a gate case that reads
//! the clock at all is a case whose determinism has to be argued rather than
//! seen. Pinning the instant removes the argument. No API used here consults
//! the clock internally either; `check_valid_at_opt(None)`, which would, is not
//! exercised. See NOTES.md, determinism audit.

#![allow(non_snake_case)]
use std::time::{Duration, SystemTime};
use tor_checkable::timed::TimerangeBound;
use tor_checkable::{TimeValidityError, Timebound};

#[test]
fn timebound__new() {
    // A fixed instant, not the host's clock: see the note at the top of the file.
    let now = SystemTime::UNIX_EPOCH + Duration::from_secs(1_700_000_000);
    let start = now;
    let end = now + Duration::from_secs(100);
    let trb = TimerangeBound::new(42u32, start..end);
    // `is_valid_at` reports timeliness as a Result, not a bool: Ok(()) is timely.
    assert!(trb.is_valid_at(&(now + Duration::from_secs(50))).is_ok());
}

#[test]
fn timebound__is_valid_at_before() {
    // A fixed instant, not the host's clock: see the note at the top of the file.
    let now = SystemTime::UNIX_EPOCH + Duration::from_secs(1_700_000_000);
    let start = now + Duration::from_secs(100);
    let end = start + Duration::from_secs(100);
    let trb = TimerangeBound::new(42u32, start..end);
    assert!(matches!(
        trb.is_valid_at(&now),
        Err(TimeValidityError::NotYetValid(_))
    ));
}

#[test]
fn timebound__is_valid_at_after() {
    // A fixed instant, not the host's clock: see the note at the top of the file.
    let now = SystemTime::UNIX_EPOCH + Duration::from_secs(1_700_000_000);
    let start = now;
    let end = now + Duration::from_secs(100);
    let trb = TimerangeBound::new(42u32, start..end);
    assert!(matches!(
        trb.is_valid_at(&(end + Duration::from_secs(200))),
        Err(TimeValidityError::Expired(_))
    ));
}

#[test]
fn timebound__dangerously_assume_timely() {
    // A fixed instant, not the host's clock: see the note at the top of the file.
    let now = SystemTime::UNIX_EPOCH + Duration::from_secs(1_700_000_000);
    let trb = TimerangeBound::new(42u32, now..(now + Duration::from_secs(100)));
    let val = trb.dangerously_assume_timely();
    assert_eq!(val, 42);
}

#[test]
fn timebound__check_valid_at_expired() {
    // A fixed instant, not the host's clock: see the note at the top of the file.
    let now = SystemTime::UNIX_EPOCH + Duration::from_secs(1_700_000_000);
    let start = now;
    let end = now + Duration::from_secs(10);
    let trb = TimerangeBound::new(42u32, start..end);
    let e = trb.check_valid_at(&(end + Duration::from_secs(50))).unwrap_err();
    assert!(matches!(e, TimeValidityError::Expired(_)));
}

#[test]
fn timebound__check_valid_at_not_yet_valid() {
    // A fixed instant, not the host's clock: see the note at the top of the file.
    let now = SystemTime::UNIX_EPOCH + Duration::from_secs(1_700_000_000);
    let start = now + Duration::from_secs(100);
    let end = start + Duration::from_secs(100);
    let trb = TimerangeBound::new(42u32, start..end);
    let e = trb.check_valid_at(&now).unwrap_err();
    assert!(matches!(e, TimeValidityError::NotYetValid(_)));
}
