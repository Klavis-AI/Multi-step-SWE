//! Group `persiststate` - the `tor_persist::StateMgr` contract itself.
//!
//! Graded through `tor_persist::TestingStateMgr` (the crate's own documented
//! in-memory manager, enabled by its `testing` feature) so the contract is
//! observed without touching the filesystem: lock states, the refusal to write
//! while unlocked, replacement semantics, error mapping, and the fact that a
//! `StateMgr` is a shared handle rather than an owned store.

use serde::{Deserialize, Serialize};
use tor_persist::{Error, LockStatus, StateMgr, TestingStateMgr};

#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
struct Counters {
    circs: u32,
    failures: u32,
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
struct Name {
    name: String,
}

#[test]
fn persiststate__a_fresh_manager_is_read_only() {
    let mgr = TestingStateMgr::new();
    assert!(!mgr.can_store());
    let err = mgr
        .store("k", &Counters { circs: 1, failures: 0 })
        .unwrap_err();
    assert!(matches!(err, Error::NoLock), "got {:?}", err);
    let got: Option<Counters> = mgr.load("k").expect("load failed");
    assert_eq!(got, None, "a refused store still changed the store");
}

#[test]
fn persiststate__lock_status_transitions_and_held() {
    let mgr = TestingStateMgr::new();
    assert_eq!(mgr.try_lock().expect("try_lock failed"), LockStatus::NewlyAcquired);
    assert!(mgr.can_store());
    assert_eq!(mgr.try_lock().expect("try_lock failed"), LockStatus::AlreadyHeld);

    assert!(LockStatus::NewlyAcquired.held());
    assert!(LockStatus::AlreadyHeld.held());
    assert!(!LockStatus::NoLock.held());
}

#[test]
fn persiststate__a_lock_held_elsewhere_blocks_writes_until_released() {
    let mgr = TestingStateMgr::new();
    mgr.block_lock_attempts();

    assert_eq!(mgr.try_lock().expect("try_lock failed"), LockStatus::NoLock);
    assert!(!mgr.can_store());
    let err = mgr.store("k", &Name { name: "x".into() }).unwrap_err();
    assert!(matches!(err, Error::NoLock), "got {:?}", err);

    mgr.unblock_lock_attempts();
    assert_eq!(mgr.try_lock().expect("try_lock failed"), LockStatus::NewlyAcquired);
    mgr.store("k", &Name { name: "x".into() }).expect("store failed");
}

#[test]
fn persiststate__store_replaces_the_previous_value_for_a_key() {
    let mgr = TestingStateMgr::new();
    mgr.try_lock().expect("try_lock failed");

    let first = Counters { circs: 1, failures: 2 };
    let second = Counters { circs: 9, failures: 0 };
    mgr.store("counters", &first).expect("store failed");
    mgr.store("counters", &second).expect("store failed");

    let got: Option<Counters> = mgr.load("counters").expect("load failed");
    assert_eq!(got, Some(second));
}

#[test]
fn persiststate__loading_a_key_as_the_wrong_type_is_a_json_error() {
    let mgr = TestingStateMgr::new();
    mgr.try_lock().expect("try_lock failed");
    mgr.store("counters", &Counters { circs: 1, failures: 2 })
        .expect("store failed");

    let err = mgr.load::<Name>("counters").unwrap_err();
    assert!(matches!(err, Error::JsonError(_)), "got {:?}", err);
    // The variant is the contract. Its rendering is a diagnostic and is not
    // graded; what is graded is that the underlying serde failure is still
    // reachable through `source()` rather than being swallowed.
    assert!(
        std::error::Error::source(&err).is_some(),
        "the JSON failure did not carry its underlying cause"
    );
}

#[test]
fn persiststate__a_clone_shares_one_underlying_store() {
    let mgr = TestingStateMgr::new();
    let other = mgr.clone();
    other.try_lock().expect("try_lock failed");

    // The lock taken through the clone is the same lock.
    assert!(mgr.can_store(), "clone and original disagree about the lock");

    other
        .store("shared", &Name { name: "guards".into() })
        .expect("store failed");
    let got: Option<Name> = mgr.load("shared").expect("load failed");
    assert_eq!(got, Some(Name { name: "guards".into() }));
}
