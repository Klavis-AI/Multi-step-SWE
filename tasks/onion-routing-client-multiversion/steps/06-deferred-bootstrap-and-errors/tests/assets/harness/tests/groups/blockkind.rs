//! Group `blockkind`: Bootstrap blockage detection (inherited from step 05).
//!
//! instruction.md: "**Preserve** bootstrap status reporting and the selectable
//! TLS backends from step 5. It is re-checked here against the cumulative
//! workspace, so it has to keep working, not merely have worked."
//!
//! TEST_INTERFACE.md: `arti_client::status::BlockageKind`, `tor_chanmgr::ConnBlockage`

use arti_client::status::BlockageKind;
use tor_chanmgr::ConnBlockage;

#[test]
fn blockkind__connblockage_notcp_display() {
    let blockage = ConnBlockage::NoTcp;
    assert_eq!(blockage.to_string(), "unable to connect to the internet");
}

#[test]
fn blockkind__connblockage_nohandshake_display() {
    let blockage = ConnBlockage::NoHandshake;
    assert_eq!(blockage.to_string(), "our internet connection seems to be filtered");
}

#[test]
fn blockkind__blockagekind_offline_display() {
    let kind = BlockageKind::Offline;
    assert_eq!(kind.to_string(), "We seem to be offline");
}

#[test]
fn blockkind__blockagekind_distinct_variants() {
    let offline = BlockageKind::Offline;
    let filtering = BlockageKind::Filtering;
    let cantreach = BlockageKind::CantReachTor;
    
    // All three variants are distinct.
    assert_ne!(offline.to_string(), filtering.to_string());
    assert_ne!(offline.to_string(), cantreach.to_string());
    assert_ne!(filtering.to_string(), cantreach.to_string());
}
