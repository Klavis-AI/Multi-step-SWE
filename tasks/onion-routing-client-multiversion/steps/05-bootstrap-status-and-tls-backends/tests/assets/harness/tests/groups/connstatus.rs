//! Group `connstatus` — the connection-status value a channel manager publishes.
//!
//! Graded observables: `ConnStatus::usable`, `ConnStatus::frac`,
//! `ConnStatus::blockage`, the `Display` phrasing of both `ConnStatus` and
//! `ConnBlockage`, and the fact that `ConnBlockage` is a comparable, cloneable
//! public enum. See `TEST_INTERFACE.md`, section "tor-chanmgr::event".

use tor_chanmgr::{ConnBlockage, ConnStatus};

#[test]
fn connstatus__default_is_not_usable() {
    let st = ConnStatus::default();
    assert!(
        !st.usable(),
        "a freshly created connection status reports no working connection"
    );
}

#[test]
fn connstatus__default_fraction_is_zero() {
    crate::common::assert_frac(ConnStatus::default().frac(), 0.0);
}

#[test]
fn connstatus__default_reports_no_blockage() {
    // Nothing has been attempted yet, so "no progress" must not be reported as
    // "blocked": the two are distinguishable states.
    assert!(ConnStatus::default().blockage().is_none());
}

#[test]
fn connstatus__default_display_describes_connecting() {
    assert_eq!(
        ConnStatus::default().to_string(),
        "connecting to the internet"
    );
}

#[test]
fn connstatus__no_tcp_blockage_display() {
    assert_eq!(
        ConnBlockage::NoTcp.to_string(),
        "unable to connect to the internet"
    );
}

#[test]
fn connstatus__no_handshake_blockage_display() {
    assert_eq!(
        ConnBlockage::NoHandshake.to_string(),
        "our internet connection seems to be filtered"
    );
}

#[test]
fn connstatus__blockage_variants_compare_and_clone() {
    let a = ConnBlockage::NoTcp;
    let b = ConnBlockage::NoHandshake;
    assert_eq!(a.clone(), ConnBlockage::NoTcp);
    assert_ne!(a, b);
    assert_eq!(format!("{:?}", b), "NoHandshake");
    crate::common::assert_clone::<ConnBlockage>();
    crate::common::assert_debug::<ConnBlockage>();
    crate::common::assert_display::<ConnBlockage>();
}
