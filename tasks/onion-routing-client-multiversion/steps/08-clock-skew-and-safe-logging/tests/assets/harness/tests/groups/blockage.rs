//! Group `blockage`: a connection failure that looks like an expired
//! certificate is reported to the user as a clock problem rather than as a
//! generic "cannot reach Tor".
//!
//! Traces to instruction.md section 1: clock skew "used to explain bootstrap
//! failure".  Interface: `tor_chanmgr::ConnBlockage`,
//! `arti_client::status::BlockageKind`, and
//! `From<ConnBlockage> for BlockageKind`.

use arti_client::status::BlockageKind;
use tor_chanmgr::ConnBlockage;

#[test]
fn blockage__expired_certificates_are_reported_as_clock_skew() {
    let kind: BlockageKind = ConnBlockage::CertsExpired.into();
    assert!(
        matches!(kind, BlockageKind::ClockSkewed),
        "expected ClockSkewed, got {:?}",
        kind
    );
}

#[test]
fn blockage__no_tcp_is_reported_as_being_offline() {
    let kind: BlockageKind = ConnBlockage::NoTcp.into();
    assert!(
        matches!(kind, BlockageKind::Offline),
        "expected Offline, got {:?}",
        kind
    );
}

#[test]
fn blockage__a_failed_handshake_is_reported_as_filtering() {
    let kind: BlockageKind = ConnBlockage::NoHandshake.into();
    assert!(
        matches!(kind, BlockageKind::Filtering),
        "expected Filtering, got {:?}",
        kind
    );
}

#[test]
fn blockage__each_blockage_kind_has_its_own_message() {
    assert_eq!(BlockageKind::Offline.to_string(), "We seem to be offline");
    assert_eq!(
        BlockageKind::Filtering.to_string(),
        "Our internet connection seems filtered"
    );
    assert_eq!(
        BlockageKind::CantReachTor.to_string(),
        "Can't reach the Tor network"
    );
    assert_eq!(BlockageKind::ClockSkewed.to_string(), "Clock is skewed.");
}

#[test]
fn blockage__the_certificate_blockage_names_expired_certificates() {
    assert_eq!(
        ConnBlockage::CertsExpired.to_string(),
        "relays all seem to be using expired certificates"
    );
    assert_eq!(
        ConnBlockage::NoTcp.to_string(),
        "unable to connect to the internet"
    );
    assert_eq!(
        ConnBlockage::NoHandshake.to_string(),
        "our internet connection seems to be filtered"
    );
    // The three are distinct values, not aliases.
    assert_ne!(ConnBlockage::CertsExpired, ConnBlockage::NoHandshake);
}
