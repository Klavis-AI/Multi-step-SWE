//! Group `persist_fsstate` (inherited) - on-disk persistent state.
//!
//! instruction.md section 1: "**Preserve** guards, persistence and circuit
//! isolation from step 2. It is re-checked here against the cumulative
//! workspace, so it has to keep working".
//!
//! Graded items: `tor_persist::FsStateMgr::from_path`, the `tor_persist::StateMgr`
//! trait (`try_lock`, `can_store`, `store`, `load`, `create_handle`),
//! `tor_persist::LockStatus`, `tor_persist::StorageHandle`, and
//! `tor_persist::Error::NoLock`.
//!
//! Each case owns a fresh `tempfile::TempDir`, so no case can see another's
//! files and no lock is ever contended.

use std::collections::BTreeMap;
use tor_persist::{FsStateMgr, LockStatus, StateMgr};

/// A deterministic, JSON-serializable payload.
fn payload() -> BTreeMap<String, String> {
    let mut m = BTreeMap::new();
    m.insert("hello".to_string(), "world".to_string());
    m.insert("quokka".to_string(), "wombat".to_string());
    m
}

#[test]
fn persist_fsstate__stored_value_reads_back() {
    let dir = tempfile::tempdir().expect("tempdir");
    let store = FsStateMgr::from_path(dir.path()).expect("from_path failed");
    assert_eq!(store.try_lock().expect("try_lock failed"), LockStatus::NewlyAcquired);

    let want = payload();
    store.store("xyz", &want).expect("store failed");
    let got: Option<BTreeMap<String, String>> = store.load("xyz").expect("load failed");
    assert_eq!(got, Some(want));
}

#[test]
fn persist_fsstate__absent_key_loads_as_none() {
    let dir = tempfile::tempdir().expect("tempdir");
    let store = FsStateMgr::from_path(dir.path()).expect("from_path failed");
    store.try_lock().expect("try_lock failed");
    store.store("xyz", &payload()).expect("store failed");

    let got: Option<BTreeMap<String, String>> = store.load("abc").expect("load failed");
    assert_eq!(got, None);
}

#[test]
fn persist_fsstate__storing_without_the_lock_is_refused() {
    let dir = tempfile::tempdir().expect("tempdir");
    let store = FsStateMgr::from_path(dir.path()).expect("from_path failed");
    assert_eq!(store.can_store(), false);

    let err = store
        .store("xyz", &payload())
        .expect_err("an unlocked store must refuse to write");
    assert_eq!(err.to_string(), "Storage not locked");

    // Nothing was written.
    let got: Option<BTreeMap<String, String>> = store.load("xyz").expect("load failed");
    assert_eq!(got, None);
}

#[test]
fn persist_fsstate__lock_status_is_reported() {
    let dir = tempfile::tempdir().expect("tempdir");
    let store = FsStateMgr::from_path(dir.path()).expect("from_path failed");
    assert_eq!(store.can_store(), false);
    assert_eq!(LockStatus::NoLock.held(), false);

    let first = store.try_lock().expect("try_lock failed");
    assert_eq!(first, LockStatus::NewlyAcquired);
    assert!(first.held());
    assert_eq!(store.can_store(), true);

    // Asking again while we already hold it says so.
    let second = store.try_lock().expect("try_lock failed");
    assert_eq!(second, LockStatus::AlreadyHeld);
    assert!(second.held());
}

#[test]
fn persist_fsstate__typed_handle_round_trips() {
    let dir = tempfile::tempdir().expect("tempdir");
    let store = FsStateMgr::from_path(dir.path()).expect("from_path failed");
    store.try_lock().expect("try_lock failed");

    let handle: tor_persist::DynStorageHandle<BTreeMap<String, String>> =
        store.clone().create_handle("thing");
    assert_eq!(handle.can_store(), true);
    assert_eq!(handle.load().expect("handle load failed"), None);

    let want = payload();
    handle.store(&want).expect("handle store failed");
    assert_eq!(handle.load().expect("handle load failed"), Some(want.clone()));

    // The handle and the manager address the same key.
    let direct: Option<BTreeMap<String, String>> = store.load("thing").expect("load failed");
    assert_eq!(direct, Some(want));
}
