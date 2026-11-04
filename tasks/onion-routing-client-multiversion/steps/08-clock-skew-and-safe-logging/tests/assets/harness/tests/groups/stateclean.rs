//! Group `stateclean`: obsolete files left in the state directory by older
//! versions are removed, but only once we hold the lock and only once they are
//! old enough to be safe to delete.
//!
//! Traces to instruction.md section 1: "removal of obsolete state files".
//! Interface: `tor_persist::FsStateMgr::{from_path, path}` and
//! `tor_persist::StateMgr::try_lock`.

use crate::common::{Tree, DAY};
use std::time::Duration;
use tor_persist::{FsStateMgr, LockStatus, StateMgr};

/// Create a state directory under `tree` and return a manager for it.
fn open(tree: &Tree) -> FsStateMgr {
    FsStateMgr::from_path(tree.root()).expect("could not open the state directory")
}

/// Create `state/<name>` and back-date it by `age`.
fn aged_file(tree: &Tree, name: &str, age: Duration) {
    let rel = format!("state/{}", name);
    tree.file(&rel);
    tree.age(&rel, age);
}

#[test]
fn stateclean__obsolete_toml_files_older_than_the_cutoff_are_deleted() {
    let tree = Tree::new();
    tree.dir("state");
    aged_file(&tree, "arti.toml", 5 * DAY);
    aged_file(&tree, "circuit_timeouts.toml", 10 * DAY);

    let mgr = open(&tree);
    assert_eq!(
        mgr.try_lock().expect("could not lock the state directory"),
        LockStatus::NewlyAcquired
    );

    assert!(!tree.exists("state/arti.toml"));
    assert!(!tree.exists("state/circuit_timeouts.toml"));
}

#[test]
fn stateclean__obsolete_files_newer_than_the_cutoff_are_kept() {
    let tree = Tree::new();
    tree.dir("state");
    aged_file(&tree, "recent.toml", 3 * DAY);
    aged_file(&tree, "brand_new.toml", Duration::from_secs(60));

    let mgr = open(&tree);
    assert_eq!(
        mgr.try_lock().expect("could not lock the state directory"),
        LockStatus::NewlyAcquired
    );

    // The cutoff is four days; three days is not old enough to remove.
    assert!(tree.exists("state/recent.toml"));
    assert!(tree.exists("state/brand_new.toml"));
}

#[test]
fn stateclean__the_old_guard_file_is_obsolete_whatever_its_extension() {
    let tree = Tree::new();
    tree.dir("state");
    aged_file(&tree, "default_guards", 10 * DAY);
    aged_file(&tree, "default_guards.json", 10 * DAY);

    let mgr = open(&tree);
    assert_eq!(
        mgr.try_lock().expect("could not lock the state directory"),
        LockStatus::NewlyAcquired
    );

    assert!(!tree.exists("state/default_guards"));
    assert!(!tree.exists("state/default_guards.json"));
}

#[test]
fn stateclean__current_state_files_are_never_deleted() {
    let tree = Tree::new();
    tree.dir("state");
    aged_file(&tree, "guards.json", 30 * DAY);
    aged_file(&tree, "circuit_timeouts.json", 30 * DAY);
    aged_file(&tree, "notes.txt", 30 * DAY);

    let mgr = open(&tree);
    assert_eq!(
        mgr.try_lock().expect("could not lock the state directory"),
        LockStatus::NewlyAcquired
    );

    assert!(tree.exists("state/guards.json"));
    assert!(tree.exists("state/circuit_timeouts.json"));
    assert!(tree.exists("state/notes.txt"));
}

#[test]
fn stateclean__a_file_dated_in_the_future_is_kept() {
    let tree = Tree::new();
    tree.dir("state");
    tree.file("state/from_the_future.toml");
    tree.date_in_future("state/from_the_future.toml", 10 * DAY);

    let mgr = open(&tree);
    assert_eq!(
        mgr.try_lock().expect("could not lock the state directory"),
        LockStatus::NewlyAcquired
    );

    // A negative age is not "older than the cutoff".
    assert!(tree.exists("state/from_the_future.toml"));
}

#[test]
fn stateclean__opening_the_directory_without_locking_deletes_nothing() {
    let tree = Tree::new();
    tree.dir("state");
    aged_file(&tree, "stale.toml", 30 * DAY);

    let mgr = open(&tree);
    // Reading does not require the lock, and must not clean.
    let loaded: Option<serde_json::Value> = mgr.load("guards").expect("load should succeed");
    assert!(loaded.is_none());
    assert!(!mgr.can_store());
    assert!(tree.exists("state/stale.toml"));

    // Only taking the lock triggers the cleanup.
    assert_eq!(
        mgr.try_lock().expect("could not lock the state directory"),
        LockStatus::NewlyAcquired
    );
    assert!(!tree.exists("state/stale.toml"));
}

#[test]
fn stateclean__the_state_directory_is_created_private() {
    let tree = Tree::new();
    let mgr = open(&tree);

    assert!(tree.exists("state"));
    assert_eq!(tree.mode("state"), 0o700);
    // `path()` reports the directory that was handed to `from_path`, i.e. the
    // parent of the `state` subdirectory.
    assert_eq!(
        mgr.path().canonicalize().expect("state path must exist"),
        tree.canonical_root()
    );
}

#[test]
fn stateclean__lock_status_reports_whether_we_hold_the_lock() {
    // Three distinct outcomes, and one predicate over them: `held()` is true
    // for both ways of holding the lock and false only for `NoLock`.
    assert!(!LockStatus::NoLock.held());
    assert!(LockStatus::AlreadyHeld.held());
    assert!(LockStatus::NewlyAcquired.held());
    assert_ne!(LockStatus::NoLock, LockStatus::AlreadyHeld);
    assert_ne!(LockStatus::AlreadyHeld, LockStatus::NewlyAcquired);
    assert_ne!(LockStatus::NoLock, LockStatus::NewlyAcquired);

    // A live manager takes the lock once and then reports that it already has
    // it; both answers say the lock is held, and only the first one cleans.
    let tree = Tree::new();
    tree.dir("state");
    aged_file(&tree, "stale.toml", 30 * DAY);
    let mgr = open(&tree);

    let first = mgr.try_lock().expect("could not lock the state directory");
    assert_eq!(first, LockStatus::NewlyAcquired);
    assert!(first.held());
    assert!(!tree.exists("state/stale.toml"));

    let second = mgr.try_lock().expect("the lock is already ours");
    assert_eq!(second, LockStatus::AlreadyHeld);
    assert!(second.held());
}
