//! Inherited: `tor_checkable`'s timeliness wrapper.  Directory objects and
//! certificates are only handed out through `Timebound`, so the bound has to
//! keep refusing to unwrap an object outside its validity interval.
//!
//! All instants here are fixed offsets from the UNIX epoch; nothing reads the
//! wall clock.

#![allow(non_snake_case)]

use std::time::{Duration, SystemTime};

use tor_checkable::{timed::TimerangeBound, TimeValidityError, Timebound};

/// A fixed instant: epoch + `secs` seconds.
fn t(secs: u64) -> SystemTime {
    SystemTime::UNIX_EPOCH + Duration::from_secs(secs)
}

/// A bound valid over [1000, 2000).
fn bounded() -> TimerangeBound<&'static str> {
    TimerangeBound::new("payload", t(1000)..t(2000))
}

#[test]
fn timebound__an_object_inside_its_interval_unwraps() {
    assert_eq!(bounded().check_valid_at(&t(1500)), Ok("payload"));
    assert_eq!(bounded().check_valid_at(&t(1001)), Ok("payload"));
    assert_eq!(bounded().check_valid_at(&t(1999)), Ok("payload"));
    // `x..y` and `x..=y` are not distinguished; both endpoints themselves are
    // reported as out of range, with a zero-length overshoot.
    assert_eq!(
        bounded().check_valid_at(&t(1000)),
        Err(TimeValidityError::NotYetValid(Duration::ZERO))
    );
    assert_eq!(
        bounded().check_valid_at(&t(2000)),
        Err(TimeValidityError::Expired(Duration::ZERO))
    );
    assert_eq!(bounded().is_valid_at(&t(1500)), Ok(()));
}

#[test]
fn timebound__an_object_before_its_interval_is_not_yet_valid() {
    let e = bounded()
        .check_valid_at(&t(940))
        .expect_err("60 seconds early");
    assert_eq!(e, TimeValidityError::NotYetValid(Duration::from_secs(60)));
    assert_eq!(
        bounded().is_valid_at(&t(0)),
        Err(TimeValidityError::NotYetValid(Duration::from_secs(1000)))
    );
}

#[test]
fn timebound__an_object_after_its_interval_is_expired() {
    let e = bounded()
        .check_valid_at(&t(2060))
        .expect_err("60 seconds late");
    assert_eq!(e, TimeValidityError::Expired(Duration::from_secs(60)));
    assert_eq!(
        bounded().is_valid_at(&t(12_000)),
        Err(TimeValidityError::Expired(Duration::from_secs(10_000)))
    );
}

#[test]
fn timebound__an_open_ended_range_is_only_bounded_on_the_side_it_names() {
    let only_start = TimerangeBound::new("p", t(1000)..);
    assert_eq!(only_start.is_valid_at(&t(999_999)), Ok(()));
    assert_eq!(only_start.is_valid_at(&t(1001)), Ok(()));
    assert_eq!(
        only_start.is_valid_at(&t(0)),
        Err(TimeValidityError::NotYetValid(Duration::from_secs(1000)))
    );

    let only_end = TimerangeBound::new("p", ..t(2000));
    assert_eq!(only_end.is_valid_at(&t(0)), Ok(()));
    assert_eq!(
        only_end.is_valid_at(&t(2001)),
        Err(TimeValidityError::Expired(Duration::from_secs(1)))
    );

    let unbounded = TimerangeBound::new("p", ..);
    assert_eq!(unbounded.is_valid_at(&t(0)), Ok(()));
    assert_eq!(unbounded.is_valid_at(&t(u32::MAX as u64)), Ok(()));
}

#[test]
fn timebound__tolerance_widens_the_interval_in_one_direction_each() {
    let late = bounded().extend_tolerance(Duration::from_secs(100));
    assert_eq!(
        late.is_valid_at(&t(2100)),
        Err(TimeValidityError::Expired(Duration::ZERO))
    );
    assert_eq!(late.check_valid_at(&t(2099)), Ok("payload"));
    // Tolerance at the far end does not move the near end.
    assert_eq!(
        bounded()
            .extend_tolerance(Duration::from_secs(100))
            .is_valid_at(&t(998)),
        Err(TimeValidityError::NotYetValid(Duration::from_secs(2)))
    );

    let early = bounded().extend_pre_tolerance(Duration::from_secs(100));
    assert_eq!(
        early.is_valid_at(&t(900)),
        Err(TimeValidityError::NotYetValid(Duration::ZERO))
    );
    assert_eq!(early.check_valid_at(&t(901)), Ok("payload"));
    assert_eq!(
        bounded()
            .extend_pre_tolerance(Duration::from_secs(100))
            .is_valid_at(&t(2001)),
        Err(TimeValidityError::Expired(Duration::from_secs(1)))
    );
}

#[test]
fn timebound__the_unchecked_escape_hatches_are_explicitly_named() {
    // `dangerously_assume_timely` hands the object over with no check at all,
    // which is exactly why it has to be spelled out.
    assert_eq!(bounded().dangerously_assume_timely(), "payload");
    // The `check_valid_at_opt(None)` arm is deliberately NOT asserted here.
    // `None` falls back to `check_valid_now()`, which reads the host's wall
    // clock, and a gate case may not depend on it: a machine with no RTC boots
    // at the epoch, and there the 1970 range this would have used is still
    // open rather than expired. The `Some(..)` arm below covers the dispatch
    // without consulting the clock. See NOTES.md, determinism audit.
    assert_eq!(
        TimerangeBound::new("p", t(1000)..t(2000)).check_valid_at_opt(Some(t(1500))),
        Ok("p")
    );
}
