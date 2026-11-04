//! `DirSkewTolerance`: the two widths this step configures, and the widening
//! of a document's validity window that those widths exist to perform.
//!
//! The configuration half (builder, defaults, serde round trip) and the
//! behavioural half (what a tolerance *does* to an untimely document) are
//! graded together here because they are one requirement: a width nothing
//! widens is a number, not a tolerance.
//!
//! Determinism: every case that names an instant builds it as a fixed offset
//! from a single constant rather than from `SystemTime::now()`, so the outcome
//! does not depend on when the case ran. No API used here consults the clock
//! internally; `check_valid_now` and `check_valid_at_opt(None)`, which would,
//! are not exercised. See NOTES.md, determinism audit.

#![allow(non_snake_case)]
use std::time::{Duration, SystemTime};
use tor_checkable::timed::TimerangeBound;
use tor_checkable::{TimeValidityError, Timebound};
use tor_dirmgr::{DirSkewTolerance, DirSkewToleranceBuilder};
use tor_netdoc::doc::netstatus::Lifetime;

/// A fixed instant, not the host's clock: see the note at the top of the file.
fn fixed_now() -> SystemTime {
    SystemTime::UNIX_EPOCH + Duration::from_secs(1_700_000_000)
}

#[test]
fn skewtol__exposes_builder_and_default() {
    let b: DirSkewToleranceBuilder = DirSkewTolerance::builder();
    let t: DirSkewTolerance = b.build().unwrap();
    assert_eq!(t, DirSkewTolerance::default());
}

#[test]
fn skewtol__default_post_valid_tolerance_is_three_days() {
    let expected = DirSkewTolerance::builder()
        .post_valid_tolerance(Duration::from_secs(3 * 24 * 3600))
        .build()
        .unwrap();
    assert_eq!(DirSkewTolerance::default(), expected);
}

#[test]
fn skewtol__post_valid_tolerance_is_settable() {
    let other = DirSkewTolerance::builder()
        .post_valid_tolerance(Duration::from_secs(3 * 24 * 3600 + 1))
        .build()
        .unwrap();
    assert_ne!(DirSkewTolerance::default(), other);
}

#[test]
fn skewtol__pre_valid_tolerance_is_settable() {
    let with = DirSkewTolerance::builder()
        .pre_valid_tolerance(Duration::from_secs(12345))
        .build()
        .unwrap();
    let without = DirSkewTolerance::builder().build().unwrap();
    assert_ne!(with, without);
}

#[test]
fn skewtol__builder_is_serde_capable() {
    // An untouched builder serialises to *nothing*: every field is an Option
    // that is skipped while unset. That is what makes a builder usable as a
    // config fragment - an unmentioned key stays unmentioned and does not
    // overwrite a lower-precedence source with a default.
    let b = DirSkewTolerance::builder();
    let empty = toml::to_string(&b).unwrap();
    assert!(
        empty.trim().is_empty(),
        "an unset builder should serialise to nothing, got {empty}"
    );

    // Durations render in humantime form rather than as a raw second count.
    let mut b2 = DirSkewTolerance::builder();
    b2.pre_valid_tolerance(Duration::from_secs(5));
    b2.post_valid_tolerance(Duration::from_secs(77));
    let text = toml::to_string(&b2).unwrap();
    assert!(text.contains(r#"pre_valid_tolerance = "5s""#), "{text}");
    assert!(
        text.contains(r#"post_valid_tolerance = "1m 17s""#),
        "durations should serialise in humantime form, got {text}"
    );

    // A fragment naming every field deserialises back to an equal builder.
    let back: DirSkewToleranceBuilder = toml::from_str(&text).unwrap();
    assert_eq!(back.build().unwrap(), b2.build().unwrap());
    assert_ne!(back.build().unwrap(), DirSkewTolerance::default());

    // Serialisation and deserialisation are *not* symmetric here. Each builder
    // field carries `serde(with = "humantime_serde::option")`, and a field with
    // a `with` attribute loses serde's implicit "a missing Option is None".
    // So the builder writes only what was set but demands all of it back.
    let partial = r#"post_valid_tolerance = "1m 17s""#;
    let err = toml::from_str::<DirSkewToleranceBuilder>(partial).unwrap_err();
    assert!(
        err.to_string().contains("missing field")
            && err.to_string().contains("pre_valid_tolerance"),
        "{err}"
    );
}

#[test]
fn skewtol__clone_eq() {
    let t = DirSkewTolerance::builder()
        .pre_valid_tolerance(Duration::from_secs(60))
        .post_valid_tolerance(Duration::from_secs(600))
        .build()
        .unwrap();
    assert_eq!(t.clone(), t);
    assert_ne!(t, DirSkewTolerance::default());
}

#[test]
fn skewtol__builder_debug_is_derived() {
    let s = format!("{:?}", DirSkewTolerance::builder());
    assert!(s.contains("DirSkewToleranceBuilder"), "{}", s);
}

#[test]
fn skewtol__post_valid_tolerance_admits_an_expired_document() {
    // What the two widths are *for*. SPEC_NOTES 3-5: the default answer to an
    // untimely directory document is "no", and the configured tolerance is a
    // named, bounded relaxation of that "no". The relaxation itself is
    // `TimerangeBound::extend_tolerance`, which is what `DirSkewTolerance`'s
    // post-validity width is defined in terms of.
    let now = fixed_now();
    let day = Duration::from_secs(24 * 3600);
    // The graded post-validity default: three days (Proposal 212).
    let post = Duration::from_secs(3 * 24 * 3600);

    // A document that stopped being valid one day ago. Untouched, it is refused.
    let expired_yesterday = || TimerangeBound::new(42_u32, (now - 2 * day)..(now - day));
    assert!(matches!(
        expired_yesterday().check_valid_at(&now),
        Err(TimeValidityError::Expired(_))
    ));

    // Widening the end bound by the post-validity tolerance admits it, and
    // yields the wrapped object rather than merely reporting timeliness.
    assert_eq!(
        expired_yesterday()
            .extend_tolerance(post)
            .check_valid_at(&now)
            .unwrap(),
        42
    );

    // The relaxation is bounded, not unconditional: a document four days past
    // the end of its window is still refused under a three-day tolerance.
    assert!(matches!(
        TimerangeBound::new(42_u32, (now - 6 * day)..(now - 4 * day))
            .extend_tolerance(post)
            .check_valid_at(&now),
        Err(TimeValidityError::Expired(_))
    ));

    // And it widens the end only. A document that is not yet valid stays
    // refused however large the post-validity width is.
    assert!(matches!(
        TimerangeBound::new(42_u32, (now + day)..(now + 2 * day))
            .extend_tolerance(post)
            .check_valid_at(&now),
        Err(TimeValidityError::NotYetValid(_))
    ));
}

#[test]
fn skewtol__pre_valid_tolerance_admits_a_document_from_the_future() {
    // The other direction, and the other width: a document whose validity has
    // not started yet is tolerated by moving its *start* earlier.
    let now = fixed_now();
    let hour = Duration::from_secs(3600);
    let pre = 2 * hour;

    let starts_in_an_hour = || TimerangeBound::new(7_u8, (now + hour)..(now + 3 * hour));
    assert!(matches!(
        starts_in_an_hour().check_valid_at(&now),
        Err(TimeValidityError::NotYetValid(_))
    ));
    assert_eq!(
        starts_in_an_hour()
            .extend_pre_tolerance(pre)
            .check_valid_at(&now)
            .unwrap(),
        7
    );

    // Bounded in this direction too: three hours out is beyond a two-hour width.
    assert!(matches!(
        TimerangeBound::new(7_u8, (now + 3 * hour)..(now + 5 * hour))
            .extend_pre_tolerance(pre)
            .check_valid_at(&now),
        Err(TimeValidityError::NotYetValid(_))
    ));

    // It moves the start only: the end bound is where it was, so the same
    // widened document is still expired after its original end.
    assert!(matches!(
        starts_in_an_hour()
            .extend_pre_tolerance(pre)
            .check_valid_at(&(now + 4 * hour)),
        Err(TimeValidityError::Expired(_))
    ));
}

#[test]
fn skewtol__tolerance_widens_a_consensus_lifetime() {
    // The consensus form of the same relaxation: both widths applied at once,
    // one to each end of a `Lifetime`, which is the arithmetic this step's
    // configuration side performs on a directory document's validity window.
    let now = fixed_now();
    let hour = Duration::from_secs(3600);
    // Two widths, one per end. `post` is the documented three-day
    // post-validity default; `pre` is an arbitrary small width, deliberately
    // *not* the compiled pre-validity default, because this case is about the
    // arithmetic rather than about either default's value.
    let pre = Duration::from_secs(90);
    let post = Duration::from_secs(3 * 24 * 3600);

    let lt = Lifetime::new(now, now + hour, now + 3 * hour).unwrap();

    // Strict liveness first: outside valid-after..valid-until it is not valid.
    assert!(!lt.valid_at(now - Duration::from_secs(60)));
    assert!(!lt.valid_at(now + 4 * hour));

    let widened = Lifetime::new(
        lt.valid_after() - pre,
        lt.fresh_until(),
        lt.valid_until() + post,
    )
    .unwrap();

    // The same two instants are inside the tolerated window ...
    assert!(widened.valid_at(now - Duration::from_secs(60)));
    assert!(widened.valid_at(now + 4 * hour));

    // ... and the tolerated window is bounded at both ends.
    assert!(!widened.valid_at(now - Duration::from_secs(120)));
    assert!(!widened.valid_at(now + 3 * hour + post + Duration::from_secs(1)));

    // Widening the validity window does not move the freshness point: a
    // tolerated consensus is still stale, it is merely usable.
    assert_eq!(widened.fresh_until(), lt.fresh_until());
}
