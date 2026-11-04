//! Group `dirstatus` — the directory manager's bootstrap-progress value types.
//!
//! `DirStatus` and `DirBootstrapStatus` become part of the public surface at
//! this step so that the client-level status can be assembled from them. Both
//! are queried without any directory having been fetched, which is the state
//! every client starts in and the only one reachable offline.

use std::time::{Duration, SystemTime};

use tor_dirmgr::{DirBootstrapStatus, DirStatus};

#[test]
fn dirstatus__default_single_status_displays_consensus_fetch() {
    assert_eq!(DirStatus::default().to_string(), "fetching a consensus");
}

#[test]
fn dirstatus__default_bootstrap_status_displays_directory_phase() {
    // With no "next" directory in flight the rendering names only the current
    // one, prefixed so it reads as part of a longer client-status line.
    assert_eq!(
        DirBootstrapStatus::default().to_string(),
        "directory is fetching a consensus"
    );
}

#[test]
fn dirstatus__default_fraction_is_zero_at_any_time() {
    let st = DirBootstrapStatus::default();
    let base = SystemTime::UNIX_EPOCH + Duration::from_secs(1_600_000_000);
    // The fraction is a function of the caller-supplied instant, not of the
    // wall clock, so several fixed instants must all agree.
    for offset in [0u64, 3_600, 86_400, 10_000_000] {
        crate::common::assert_frac(st.frac_at(base + Duration::from_secs(offset)), 0.0);
    }
}

#[test]
fn dirstatus__default_is_not_usable_at_any_time() {
    let st = DirBootstrapStatus::default();
    let base = SystemTime::UNIX_EPOCH + Duration::from_secs(1_600_000_000);
    for offset in [0u64, 3_600, 86_400] {
        assert!(
            !st.usable_at(base + Duration::from_secs(offset)),
            "a directory that has not been fetched is never usable"
        );
    }
}

#[test]
fn dirstatus__clone_preserves_display_and_fraction() {
    let st = DirBootstrapStatus::default();
    let copy = st.clone();
    let when = SystemTime::UNIX_EPOCH + Duration::from_secs(1_600_000_000);
    assert_eq!(copy.to_string(), st.to_string());
    crate::common::assert_frac(copy.frac_at(when), st.frac_at(when));
    crate::common::assert_clone::<DirStatus>();
}

#[test]
fn dirstatus__default_has_no_next_directory() {
    // A second directory only appears once the first is usable and ageing, so
    // the freshly created value must not mention one.
    let rendered = DirBootstrapStatus::default().to_string();
    assert!(
        !rendered.contains("next directory"),
        "unexpected next-directory phase in {rendered:?}"
    );
    let debugged = format!("{:?}", DirBootstrapStatus::default());
    assert!(
        debugged.contains("DirBootstrapStatus"),
        "the debug rendering should name the type: {debugged:?}"
    );
}

#[test]
fn dirstatus__accessor_signatures_are_time_parameterised() {
    // Both queries take the instant to evaluate against, which is what makes
    // them testable without a wall clock.
    let frac: fn(&DirBootstrapStatus, SystemTime) -> f32 = DirBootstrapStatus::frac_at;
    let usable: fn(&DirBootstrapStatus, SystemTime) -> bool = DirBootstrapStatus::usable_at;
    let st = DirBootstrapStatus::default();
    let when = SystemTime::UNIX_EPOCH;
    crate::common::assert_frac(frac(&st, when), 0.0);
    assert!(!usable(&st, when));
}
