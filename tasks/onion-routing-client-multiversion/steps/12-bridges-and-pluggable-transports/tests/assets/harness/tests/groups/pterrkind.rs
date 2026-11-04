//! Group: pterrkind — the classification the PT errors carry.
//!
//! `PtError` is what the channel manager sees when a managed proxy misbehaves,
//! so its `ErrorKind` and `RetryTime` are part of the contract: a
//! LocalProtocolViolation is never retried, an ExternalToolFailed is.  Every
//! error here is produced by a real scripted launch, not hand-constructed, so
//! the mapping is observed end to end.

// The step-12 helpers this group needs live in `current`, apart from `common`
// so that no gate group is forced to compile them; see the note at the top of
// common/current.rs. current.rs re-exports common, so one path reaches both.
use crate::current::launch_script;
use tor_error::{ErrorKind, HasKind, HasRetryTime, RetryTime};
use tor_ptmgr::err::PtError;

#[test]
fn pterrkind__unsupported_version_is_a_local_protocol_violation() {
    let (_d, rv) = launch_script("pterrkind_ver", "version_error", &["obfs4"]);
    let e = rv.expect_err("VERSION-ERROR must fail");
    assert_eq!(e.kind(), ErrorKind::LocalProtocolViolation);
}

#[test]
fn pterrkind__unsupported_version_is_never_retried() {
    let (_d, rv) = launch_script("pterrkind_vernr", "version_error", &["obfs4"]);
    let e = rv.expect_err("VERSION-ERROR must fail");
    assert_eq!(e.retry_time(), RetryTime::Never);
}

#[test]
fn pterrkind__protocol_violation_is_a_local_protocol_violation() {
    let (_d, rv) = launch_script("pterrkind_pv", "extra_transport", &["obfs4"]);
    let e = rv.expect_err("an unwanted CMETHOD must fail");
    assert_eq!(e.kind(), ErrorKind::LocalProtocolViolation);
}

#[test]
fn pterrkind__timeout_is_an_external_tool_failure() {
    let (_d, rv) = launch_script("pterrkind_to", "silent", &["obfs4"]);
    let e = rv.expect_err("a silent binary must fail");
    assert_eq!(e.kind(), ErrorKind::ExternalToolFailed);
}

#[test]
fn pterrkind__timeout_may_be_retried_after_waiting() {
    let (_d, rv) = launch_script("pterrkind_tor", "silent", &["obfs4"]);
    let e = rv.expect_err("a silent binary must fail");
    assert_eq!(e.retry_time(), RetryTime::AfterWaiting);
}

#[test]
fn pterrkind__unsupported_transports_is_a_configuration_error() {
    let (_d, rv) = launch_script("pterrkind_unsup", "no_cmethods", &["obfs4"]);
    let e = rv.expect_err("an unlaunched transport must fail");
    assert_eq!(e.kind(), ErrorKind::InvalidConfig);
    assert_eq!(e.retry_time(), RetryTime::Never);
}

#[test]
fn pterrkind__missing_binary_is_never_retried() {
    use tor_ptmgr::ipc::{PluggableTransport, PtParameters};
    let dir = crate::current::scratch_dir("pterrkind_missing");
    let params = PtParameters::builder()
        .state_location(dir.clone())
        .transports(vec!["obfs4".parse().unwrap()])
        .timeout(Some(crate::current::pt_deadline()))
        .build()
        .unwrap();
    let missing = dir.join("no-such-binary");
    let rt = tor_rtcompat::tokio::TokioNativeTlsRuntime::create().unwrap();
    let mut pt = PluggableTransport::new(missing, vec![], params);
    let rt2 = rt.clone();
    let e = {
        use tor_rtcompat::BlockOn;
        rt.block_on(async move { pt.launch(rt2).await })
            .expect_err("a missing binary must fail")
    };
    assert!(matches!(e, PtError::ChildSpawnFailed { .. }), "got {:?}", e);
    assert_eq!(e.retry_time(), RetryTime::Never);
}
