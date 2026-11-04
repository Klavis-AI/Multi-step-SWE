//! Group `persistfs` - the on-disk half of "persistent shared on-disk state".
//!
//! Graded through `tor_persist::FsStateMgr` and the `tor_persist::StateMgr`
//! trait it implements: directory layout, lock discipline, the JSON document
//! that lands on disk, and the fact that a key can never escape the state
//! directory.

use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
use tor_persist::{Error, FsStateMgr, LockStatus, StateMgr};

/// A small serializable record, so the cases assert on a real document.
#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
struct Rec {
    guard: String,
    confirmed: u32,
}

fn rec() -> Rec {
    Rec {
        guard: "$AAAA".to_string(),
        confirmed: 3,
    }
}

#[test]
fn persistfs__creates_state_dir_and_lockfile() {
    let dir = crate::common::tempdir();
    let root = dir.path().join("nested").join("place");
    assert!(!root.exists());

    let _mgr = FsStateMgr::from_path(&root).expect("from_path failed");

    assert!(root.join("state").is_dir(), "no `state` directory created");
    assert!(
        root.join("state.lock").exists(),
        "no `state.lock` lockfile created"
    );
}

#[test]
fn persistfs__state_dir_is_not_group_or_world_accessible() {
    use std::os::unix::fs::PermissionsExt;
    let dir = crate::common::tempdir();
    let _mgr = FsStateMgr::from_path(dir.path()).expect("from_path failed");

    let mode = std::fs::metadata(dir.path().join("state"))
        .expect("no state directory")
        .permissions()
        .mode();
    assert_eq!(
        mode & 0o077,
        0,
        "state directory mode {:o} grants group/other access",
        mode & 0o7777
    );
}

#[test]
fn persistfs__store_without_the_lock_is_refused() {
    let dir = crate::common::tempdir();
    let mgr = FsStateMgr::from_path(dir.path()).expect("from_path failed");

    assert!(!mgr.can_store(), "a fresh FsStateMgr claimed to be writable");
    let err = mgr.store("guards", &rec()).unwrap_err();
    assert!(
        matches!(err, Error::NoLock),
        "expected Error::NoLock, got {:?}",
        err
    );
    assert!(
        std::fs::read_dir(dir.path().join("state"))
            .expect("no state dir")
            .next()
            .is_none(),
        "a refused store still wrote a file"
    );
}

#[test]
fn persistfs__locked_store_writes_pretty_json_named_for_the_key() {
    let dir = crate::common::tempdir();
    let mgr = FsStateMgr::from_path(dir.path()).expect("from_path failed");
    assert_eq!(mgr.try_lock().expect("try_lock failed"), LockStatus::NewlyAcquired);
    assert!(mgr.can_store());

    mgr.store("default_guards", &rec()).expect("store failed");

    let path = dir.path().join("state").join("default_guards.json");
    let text = std::fs::read_to_string(&path).expect("no default_guards.json in the state dir");
    assert!(
        text.contains('\n') && text.contains("  \""),
        "stored document is not pretty-printed JSON: {:?}",
        text
    );
    let parsed: BTreeMap<String, serde_json::Value> =
        serde_json::from_str(&text).expect("stored document is not a JSON object");
    assert_eq!(parsed["guard"], serde_json::json!("$AAAA"));
    assert_eq!(parsed["confirmed"], serde_json::json!(3));

    let back: Option<Rec> = mgr.load("default_guards").expect("load failed");
    assert_eq!(back, Some(rec()));
}

#[test]
fn persistfs__load_of_an_absent_key_is_none_not_an_error() {
    let dir = crate::common::tempdir();
    let mgr = FsStateMgr::from_path(dir.path()).expect("from_path failed");

    let got: Option<Rec> = mgr.load("never_written").expect("load of missing key errored");
    assert_eq!(got, None);
}

#[test]
fn persistfs__key_with_path_separators_stays_inside_the_state_dir() {
    let dir = crate::common::tempdir();
    let mgr = FsStateMgr::from_path(dir.path()).expect("from_path failed");
    mgr.try_lock().expect("try_lock failed");

    mgr.store("../../escape", &rec()).expect("store failed");

    let strays: Vec<_> = std::fs::read_dir(dir.path())
        .expect("no root dir")
        .map(|e| e.expect("bad dir entry").file_name())
        .filter(|n| n != "state" && n != "state.lock")
        .collect();
    assert!(
        strays.is_empty(),
        "a key with path separators wrote outside the state dir: {:?}",
        strays
    );
    let inside: Vec<_> = std::fs::read_dir(dir.path().join("state"))
        .expect("no state dir")
        .map(|e| e.expect("bad dir entry").file_name())
        .collect();
    assert_eq!(
        inside.len(),
        1,
        "expected exactly one state file, found {:?}",
        inside
    );
    let name = inside[0].to_string_lossy().into_owned();
    assert!(
        name.ends_with(".json") && !name.contains('/'),
        "unexpected state file name {:?}",
        name
    );
}

#[test]
fn persistfs__value_survives_reopen_and_the_reopened_manager_is_unlocked() {
    let dir = crate::common::tempdir();
    {
        let mgr = FsStateMgr::from_path(dir.path()).expect("from_path failed");
        mgr.try_lock().expect("try_lock failed");
        assert_eq!(mgr.try_lock().expect("try_lock failed"), LockStatus::AlreadyHeld);
        mgr.store("default_guards", &rec()).expect("store failed");
    }

    let reopened = FsStateMgr::from_path(dir.path()).expect("second from_path failed");
    let got: Option<Rec> = reopened.load("default_guards").expect("load failed");
    assert_eq!(got, Some(rec()), "state did not survive a reopen");
    assert!(
        !reopened.can_store(),
        "a freshly reopened manager already claims the write lock"
    );
}
