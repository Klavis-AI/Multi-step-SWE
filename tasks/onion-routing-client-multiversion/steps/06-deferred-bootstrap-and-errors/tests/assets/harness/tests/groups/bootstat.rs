//! Group `bootstat`: Bootstrap status reporting (inherited from step 05).
//!
//! instruction.md: "**Preserve** bootstrap status reporting and the selectable
//! TLS backends from step 5. It is re-checked here against the cumulative
//! workspace, so it has to keep working, not merely have worked."
//!
//! TEST_INTERFACE.md: `arti_client::status::BootstrapStatus`
//!
//! Two cases were removed from this group because they were not regression at
//! all. Both built their client through the manual bootstrap behaviour plus
//! `create_unbootstrapped()`, which this step INTRODUCES
//! (`arti-client/src/client.rs` at v0.1.0; absent at v0.0.4). As gate cases
//! they vetoed every candidate that had not yet implemented deferred
//! bootstrap, while paying nothing for implementing it. That capability is
//! priced by the scored groups `bootbehav` (18) and `bootdefer` (32). What
//! remains is the step-05 behaviour this group exists to protect, and all of
//! it holds against v0.0.4.

#[test]
fn bootstat__default_status_zero_progress() {
    use arti_client::status::BootstrapStatus;
    let status = BootstrapStatus::default();
    assert_eq!(status.as_frac(), 0.0);
}

#[test]
fn bootstat__default_status_not_ready() {
    use arti_client::status::BootstrapStatus;
    let status = BootstrapStatus::default();
    assert!(!status.ready_for_traffic());
}

#[test]
fn bootstat__default_status_no_blockage() {
    use arti_client::status::BootstrapStatus;
    let status = BootstrapStatus::default();
    assert!(status.blocked().is_none());
}

#[test]
fn bootstat__default_status_display_starts_with_zero_percent() {
    use arti_client::status::BootstrapStatus;
    let status = BootstrapStatus::default();
    let text = status.to_string();
    assert!(text.starts_with("0%: "), "Expected '0%: ...' but got {:?}", text);
}
