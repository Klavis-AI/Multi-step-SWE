//! Bootstrap-blockage detection: `DirBlockage`, the `DirBootstrapStatus`
//! accessors that report it, and the `BlockageKind` it becomes for clients.

#![allow(non_snake_case)]

use std::time::{Duration, SystemTime, UNIX_EPOCH};

use arti_client::status::{BlockageKind, BootstrapStatus};
use tor_chanmgr::ConnBlockage;
use tor_dirmgr::{DirBlockage, DirBootstrapStatus};

/// A fixed instant, so nothing in this group depends on the wall clock.
fn when() -> SystemTime {
    UNIX_EPOCH + Duration::from_secs(1_600_000_000)
}

#[test]
fn dirblockage__names_the_three_ways_bootstrap_can_stick() {
    assert_eq!(
        crate::common::shown(&DirBlockage::Stalled),
        "Can't make progress."
    );
    assert_eq!(
        crate::common::shown(&DirBlockage::TooManyErrors),
        "Too many errors without making progress."
    );
    assert_eq!(
        crate::common::shown(&DirBlockage::TooManyResets),
        "Had to reset bootstrapping too many times."
    );
}

#[test]
fn dirblockage__an_idle_status_reports_no_blockage() {
    let st = DirBootstrapStatus::default();
    assert!(
        st.blockage(when()).is_none(),
        "with no attempt in flight there is nothing to be blocked on"
    );
}

#[test]
fn dirblockage__an_idle_status_is_zero_and_unusable() {
    let st = DirBootstrapStatus::default();
    assert_eq!(st.frac_at(when()), 0.0, "no progress has been made");
    assert!(!st.usable_at(when()), "no directory is available yet");
}

#[test]
fn dirblockage__status_accessors_are_independent_of_the_instant() {
    let st = DirBootstrapStatus::default();
    let early = UNIX_EPOCH + Duration::from_secs(1);
    let late = UNIX_EPOCH + Duration::from_secs(4_000_000_000);
    assert_eq!(st.frac_at(early), st.frac_at(late));
    assert_eq!(st.usable_at(early), st.usable_at(late));
    assert!(st.blockage(early).is_none() && st.blockage(late).is_none());
}

#[test]
fn dirblockage__every_directory_blockage_becomes_cant_bootstrap() {
    for b in [
        DirBlockage::Stalled,
        DirBlockage::TooManyErrors,
        DirBlockage::TooManyResets,
    ] {
        let kind: BlockageKind = b.into();
        assert!(
            matches!(kind, BlockageKind::CantBootstrap),
            "a directory blockage is not a connection problem"
        );
    }
}

#[test]
fn dirblockage__cant_bootstrap_has_its_own_message() {
    assert_eq!(
        crate::common::shown(&BlockageKind::CantBootstrap),
        "Can't bootstrap a Tor directory."
    );
}

#[test]
fn dirblockage__connection_blockages_keep_their_own_kinds() {
    let offline: BlockageKind = ConnBlockage::NoTcp.into();
    let filtered: BlockageKind = ConnBlockage::NoHandshake.into();
    let skewed: BlockageKind = ConnBlockage::CertsExpired.into();
    assert!(matches!(offline, BlockageKind::Offline));
    assert!(matches!(filtered, BlockageKind::Filtering));
    assert!(matches!(skewed, BlockageKind::ClockSkewed));
    assert_eq!(crate::common::shown(&offline), "We seem to be offline");
    assert_eq!(
        crate::common::shown(&filtered),
        "Our internet connection seems filtered"
    );
    assert_eq!(crate::common::shown(&skewed), "Clock is skewed.");
}

#[test]
fn dirblockage__a_fresh_client_status_is_unbootstrapped_and_unblocked() {
    let st = BootstrapStatus::default();
    assert_eq!(st.as_frac(), 0.0);
    assert!(!st.ready_for_traffic());
    assert!(
        st.blocked().is_none(),
        "nothing has failed yet, so nothing is blocked"
    );
}
