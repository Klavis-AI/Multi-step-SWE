//! `tor_persist::Error` is a struct that reports what went wrong, what we were
//! doing, and what we were doing it to, and exposes the underlying source.

#![allow(non_snake_case)]
use tor_error::{ErrorKind, HasKind};
use tor_persist::{FsStateMgr, StateMgr};

/// A state manager rooted in a fresh temporary directory.
///
/// Permission checking is switched off on purpose: this group grades the error
/// *shape*; `fs-mistrust` behaviour is graded by its own group.
fn mgr() -> (tempfile::TempDir, FsStateMgr) {
    let dir = tempfile::TempDir::new().expect("temp dir");
    let m = FsStateMgr::from_path_and_mistrust(
        dir.path(),
        &fs_mistrust::Mistrust::new_dangerously_trust_everyone(),
    )
    .expect("state manager in a fresh directory");
    (dir, m)
}

#[test]
fn persisterr__store_without_the_lock_reports_not_locked() {
    let (_d, m) = mgr();
    assert!(!m.can_store(), "a new manager does not hold the lock");
    let err = m
        .store("conformance", &"value".to_string())
        .expect_err("storing without the lock must fail");
    assert_eq!(
        crate::common::shown(err.source()),
        "Storage not locked",
        "Error::source() must expose the underlying cause"
    );
}

#[test]
fn persisterr__not_locked_is_bad_api_usage() {
    let (_d, m) = mgr();
    let err = m.store("conformance", &1_u32).expect_err("no lock held");
    assert_eq!(err.kind(), ErrorKind::BadApiUsage);
}

#[test]
fn persisterr__display_names_source_action_and_resource() {
    let (_d, m) = mgr();
    let err = m.store("conformance", &1_u32).expect_err("no lock held");
    let msg = crate::common::shown(&err);
    assert!(
        msg.starts_with("Storage not locked while storing persistent data on "),
        "expected `<source> while <action> on <resource>`, got {:?}",
        msg
    );
    assert!(
        msg.ends_with("on persistent storage manager"),
        "the resource is the store as a whole for a lock failure; got {:?}",
        msg
    );
}

#[test]
fn persisterr__locking_makes_storing_succeed_and_round_trip() {
    let (_d, m) = mgr();
    let status = m.try_lock().expect("locking a fresh directory");
    assert!(status.held(), "the lock must be acquired");
    assert!(m.can_store());
    m.store("conformance", &"persisted".to_string())
        .expect("storing under the lock");
    let got: Option<String> = m.load("conformance").expect("loading back");
    assert_eq!(got.as_deref(), Some("persisted"));
}

#[test]
fn persisterr__unlocking_restores_the_not_locked_error() {
    let (_d, m) = mgr();
    assert!(m.try_lock().expect("lock").held());
    m.unlock().expect("releasing the lock");
    assert!(!m.can_store(), "after unlock the manager is read-only again");
    let err = m.store("conformance", &1_u32).expect_err("no lock held");
    assert_eq!(crate::common::shown(err.source()), "Storage not locked");
    assert_eq!(err.kind(), ErrorKind::BadApiUsage);
}

#[test]
fn persisterr__missing_key_loads_as_none_not_an_error() {
    let (_d, m) = mgr();
    let got: Option<String> = m.load("never-written").expect("absence is not an error");
    assert!(got.is_none());
}

#[test]
fn persisterr__unparsable_state_is_reported_as_corrupted() {
    let (dir, m) = mgr();
    assert!(m.try_lock().expect("lock").held());
    m.store("conformance", &"persisted".to_string())
        .expect("store");
    let path = dir.path().join("state").join("conformance.json");
    std::fs::write(&path, b"{ this is not json").expect("clobber the state file");
    let err = m
        .load::<String>("conformance")
        .expect_err("unparsable state must be an error");
    assert_eq!(crate::common::shown(err.source()), "JSON error");
    assert_eq!(
        err.kind(),
        ErrorKind::PersistentStateCorrupted,
        "a bad document on disk is corruption, not an API misuse"
    );
}

#[test]
fn persisterr__not_locked_has_no_further_cause() {
    let (_d, m) = mgr();
    let err = m.store("conformance", &1_u32).expect_err("no lock held");
    let as_std: &dyn std::error::Error = &err;
    assert!(
        as_std.source().is_none(),
        "the `Storage not locked` source has no inner error of its own"
    );
}
