//! Group `bootstat` — the client-level bootstrap status.
//!
//! `BootstrapStatus` is the single value a caller polls to learn how far the
//! client has got. It combines the connection phase and the directory phase,
//! reports whether traffic can flow yet, and reports a blockage when progress
//! has stalled for a diagnosable reason.

use arti_client::status::{BootstrapEvents, BootstrapStatus};

#[test]
fn bootstat__default_fraction_is_zero() {
    crate::common::assert_frac(BootstrapStatus::default().as_frac(), 0.0);
}

#[test]
fn bootstat__default_is_not_ready_for_traffic() {
    assert!(
        !BootstrapStatus::default().ready_for_traffic(),
        "a client that has not bootstrapped cannot carry traffic"
    );
}

#[test]
fn bootstat__default_reports_no_blockage() {
    // "Not started" and "stuck" are different states and must render
    // differently; only the latter carries a blockage.
    assert!(BootstrapStatus::default().blocked().is_none());
}

#[test]
fn bootstat__default_display_combines_connection_and_directory_phases() {
    assert_eq!(
        BootstrapStatus::default().to_string(),
        "0%: connecting to the internet; directory is fetching a consensus"
    );
}

#[test]
fn bootstat__status_is_cloneable_and_preserves_display() {
    let st = BootstrapStatus::default();
    let copy = st.clone();
    assert_eq!(copy.to_string(), st.to_string());
    crate::common::assert_frac(copy.as_frac(), st.as_frac());
    assert_eq!(copy.ready_for_traffic(), st.ready_for_traffic());
}

#[test]
fn bootstat__debug_names_the_status_type() {
    let debugged = format!("{:?}", BootstrapStatus::default());
    assert!(
        debugged.contains("BootstrapStatus"),
        "the debug rendering should name the type: {debugged:?}"
    );
    crate::common::assert_send_sync::<BootstrapStatus>();
}

#[test]
fn bootstat__query_signatures_are_pinned() {
    let frac: fn(&BootstrapStatus) -> f32 = BootstrapStatus::as_frac;
    let ready: fn(&BootstrapStatus) -> bool = BootstrapStatus::ready_for_traffic;
    let st = BootstrapStatus::default();
    crate::common::assert_frac(frac(&st), 0.0);
    assert!(!ready(&st));
    // The event stream carries exactly this type.
    assert_eq!(
        std::any::type_name::<<BootstrapEvents as futures::Stream>::Item>(),
        std::any::type_name::<BootstrapStatus>()
    );
}
