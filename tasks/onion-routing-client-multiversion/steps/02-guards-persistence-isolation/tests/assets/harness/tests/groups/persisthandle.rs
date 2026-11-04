//! Group `persisthandle` - the typed, object-safe view of persistent state.
//!
//! `tor_persist::StateMgr::create_handle` is how the guard manager and the
//! circuit manager reach shared on-disk state without being generic over the
//! manager. Graded through `DynStorageHandle<T>` and the `StorageHandle<T>`
//! trait: key binding, lock propagation, and independence between handles.

use serde::{Deserialize, Serialize};
use tor_persist::{DynStorageHandle, Error, StateMgr, TestingStateMgr};

#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
struct Sample {
    relays: Vec<String>,
}

fn sample(n: &str) -> Sample {
    Sample {
        relays: vec![n.to_string()],
    }
}

#[test]
fn persisthandle__load_is_none_before_anything_is_stored() {
    let mgr = TestingStateMgr::new();
    let h: DynStorageHandle<Sample> = mgr.create_handle("guards");
    assert_eq!(h.load().expect("load failed"), None);
}

#[test]
fn persisthandle__store_through_a_handle_needs_the_lock() {
    let mgr = TestingStateMgr::new();
    let h: DynStorageHandle<Sample> = mgr.clone().create_handle("guards");

    assert!(!h.can_store(), "handle claims writability without the lock");
    let err = h.store(&sample("a")).unwrap_err();
    assert!(matches!(err, Error::NoLock), "got {:?}", err);

    mgr.try_lock().expect("try_lock failed");
    assert!(h.can_store(), "handle did not see the lock taken on its manager");
    h.store(&sample("a")).expect("store failed");
}

#[test]
fn persisthandle__a_handle_reads_and_writes_the_key_it_was_made_with() {
    let mgr = TestingStateMgr::new();
    mgr.try_lock().expect("try_lock failed");
    let h: DynStorageHandle<Sample> = mgr.clone().create_handle("default_guards");

    h.store(&sample("relay-1")).expect("store failed");

    // Same key, read through the manager rather than the handle.
    let direct: Option<Sample> = mgr.load("default_guards").expect("load failed");
    assert_eq!(direct, Some(sample("relay-1")));
    assert_eq!(h.load().expect("load failed"), Some(sample("relay-1")));
}

#[test]
fn persisthandle__handles_on_different_keys_are_independent() {
    let mgr = TestingStateMgr::new();
    mgr.try_lock().expect("try_lock failed");
    let a: DynStorageHandle<Sample> = mgr.clone().create_handle("guards_a");
    let b: DynStorageHandle<Sample> = mgr.create_handle("guards_b");

    a.store(&sample("a")).expect("store failed");

    assert_eq!(a.load().expect("load failed"), Some(sample("a")));
    assert_eq!(b.load().expect("load failed"), None);

    b.store(&sample("b")).expect("store failed");
    assert_eq!(a.load().expect("load failed"), Some(sample("a")));
    assert_eq!(b.load().expect("load failed"), Some(sample("b")));
}

#[test]
fn persisthandle__dyn_storage_handles_are_object_safe_and_shareable() {
    let mgr = TestingStateMgr::new();
    mgr.try_lock().expect("try_lock failed");

    // The whole point of DynStorageHandle is that it erases the manager type,
    // so several of them can live in one collection and be sent across tasks.
    let handles: Vec<DynStorageHandle<Sample>> = vec![
        mgr.clone().create_handle("k0"),
        mgr.clone().create_handle("k1"),
    ];
    fn assert_send_sync<T: Send + Sync>(_: &T) {}
    assert_send_sync(&handles[0]);

    for (i, h) in handles.iter().enumerate() {
        h.store(&sample(&format!("r{}", i))).expect("store failed");
    }
    assert_eq!(handles[0].load().expect("load failed"), Some(sample("r0")));
    assert_eq!(handles[1].load().expect("load failed"), Some(sample("r1")));

    let cloned: DynStorageHandle<Sample> = std::sync::Arc::clone(&handles[1]);
    assert_eq!(cloned.load().expect("load failed"), Some(sample("r1")));
}
