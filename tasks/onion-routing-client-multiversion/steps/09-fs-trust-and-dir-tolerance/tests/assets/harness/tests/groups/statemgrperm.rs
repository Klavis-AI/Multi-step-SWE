//! `tor_persist::FsStateMgr` under a `Mistrust`: the on-disk state directory is
//! created privately, an unsafe one is a startup error rather than a warning,
//! and writes still require the lock.
#![allow(non_snake_case)]
use crate::common::{mode_of, Tree, BAD_DIR_WORLD_W, GOOD_DIR};
use fs_mistrust::{Mistrust, TrustedGroup, TrustedUser};
use std::path::Path;
use tor_error::{ErrorKind, HasKind};
use tor_persist::{Error as PersistError, FsStateMgr, LockStatus, StateMgr};

fn strict(root: &Path) -> Mistrust {
    let mut b = Mistrust::builder();
    b.ignore_prefix(root)
        .trust_user(TrustedUser::Current)
        .trust_group(TrustedGroup::None);
    b.build().unwrap()
}

/// A locked manager rooted at `<tree>/st`.
fn locked(t: &Tree) -> FsStateMgr {
    let mgr = FsStateMgr::from_path_and_mistrust(t.path("st"), &strict(t.root())).unwrap();
    assert_eq!(mgr.try_lock().unwrap(), LockStatus::NewlyAcquired);
    mgr
}

#[test]
fn statemgrperm__creates_a_private_state_subdirectory() {
    let t = Tree::new();
    let dir = t.path("st");
    let mgr = FsStateMgr::from_path_and_mistrust(&dir, &strict(t.root())).unwrap();

    // The manager reports the directory it was given ...
    assert_eq!(mgr.path(), dir.as_path());
    // ... and keeps its data in a `state` subdirectory it created itself.
    let state = dir.join("state");
    assert!(state.is_dir());
    assert_eq!(mode_of(&state) & 0o777, 0o700);
    assert_eq!(mode_of(&dir) & 0o777, 0o700);
}

#[test]
fn statemgrperm__world_writable_parent_is_a_startup_error() {
    // The migration note for this step: an unsafe state directory now stops
    // startup instead of being tolerated.
    let t = Tree::new();
    let open = t.dir("open", BAD_DIR_WORLD_W);
    let e = FsStateMgr::from_path_and_mistrust(&open, &strict(t.root())).unwrap_err();
    match &e {
        PersistError::Permissions(inner) => {
            assert!(inner.is_bad_permission(), "{inner:?}");
            assert_eq!(inner.path(), Some(open.as_path()));
        }
        other => panic!("unexpected: {other:?}"),
    }
    assert_eq!(e.kind(), ErrorKind::FsPermissions);
    assert!(!open.join("state").exists(), "state dir created anyway");
}

#[test]
fn statemgrperm__world_writable_state_directory_is_rejected() {
    let t = Tree::new();
    let dir = t.dir("st", GOOD_DIR);
    let state = t.dir("st/state", BAD_DIR_WORLD_W);
    let e = FsStateMgr::from_path_and_mistrust(&dir, &strict(t.root())).unwrap_err();
    match &e {
        PersistError::Permissions(inner) => assert_eq!(inner.path(), Some(state.as_path())),
        other => panic!("unexpected: {other:?}"),
    }
    assert_eq!(e.kind(), ErrorKind::FsPermissions);
}

#[test]
fn statemgrperm__writable_contents_are_rejected_too() {
    // The check is recursive: a file inside the state directory that anyone
    // may rewrite is just as disqualifying as the directory itself.
    let t = Tree::new();
    let dir = t.dir("st", GOOD_DIR);
    t.dir("st/state", GOOD_DIR);
    let junk = t.file("st/state/guards.json", b"[]", 0o666);

    let e = FsStateMgr::from_path_and_mistrust(&dir, &strict(t.root())).unwrap_err();
    match &e {
        PersistError::Permissions(inner) => {
            assert_eq!(inner.path(), Some(junk.as_path()));
            assert!(inner.is_bad_permission(), "{inner:?}");
        }
        other => panic!("unexpected: {other:?}"),
    }
    assert_eq!(e.kind(), ErrorKind::FsPermissions);

    // Tightening exactly that file is enough to let startup proceed.
    crate::common::chmod(&junk, 0o600);
    FsStateMgr::from_path_and_mistrust(&dir, &strict(t.root())).unwrap();
}

#[test]
fn statemgrperm__trusting_everyone_accepts_an_unsafe_directory() {
    // The escape hatch has to actually work, or the env-var override in `arti`
    // would be useless.
    let t = Tree::new();
    let open = t.dir("open", BAD_DIR_WORLD_W);
    let mgr = FsStateMgr::from_path_and_mistrust(
        &open,
        &Mistrust::new_dangerously_trust_everyone(),
    )
    .unwrap();
    assert_eq!(mgr.path(), open.as_path());
    assert!(open.join("state").is_dir());
}

#[test]
fn statemgrperm__storing_requires_the_lock() {
    let t = Tree::new();
    let mgr = FsStateMgr::from_path_and_mistrust(t.path("st"), &strict(t.root())).unwrap();

    assert!(!mgr.can_store());
    let e = mgr.store("k", &1_u32).unwrap_err();
    assert!(matches!(e, PersistError::NoLock), "{e:?}");

    let st = mgr.try_lock().unwrap();
    assert_eq!(st, LockStatus::NewlyAcquired);
    assert!(st.held());
    assert!(mgr.can_store());
    assert_eq!(mgr.try_lock().unwrap(), LockStatus::AlreadyHeld);
    mgr.store("k", &1_u32).unwrap();

    mgr.unlock().unwrap();
    assert!(!mgr.can_store());
    assert!(!LockStatus::NoLock.held());
}

#[test]
fn statemgrperm__store_and_load_round_trip() {
    let t = Tree::new();
    let mgr = locked(&t);

    let absent: Option<u32> = mgr.load("never-written").unwrap();
    assert!(absent.is_none());

    mgr.store("counters", &vec![4_u8, 2]).unwrap();
    let back: Option<Vec<u8>> = mgr.load("counters").unwrap();
    assert_eq!(back, Some(vec![4, 2]));

    mgr.store("counters", &vec![7_u8]).unwrap();
    let back: Option<Vec<u8>> = mgr.load("counters").unwrap();
    assert_eq!(back, Some(vec![7]));
}

#[test]
fn statemgrperm__state_is_written_privately_and_survives_reopen() {
    let t = Tree::new();
    {
        let mgr = locked(&t);
        mgr.store("counters", &42_u32).unwrap();
        let f = t.path("st/state/counters.json");
        assert!(f.is_file(), "expected {}", f.display());
        assert_eq!(mode_of(&f) & 0o777, 0o600);
        mgr.unlock().unwrap();
    }

    // The lock file is created with the process umask rather than by the
    // checked-directory accessors, so tighten everything before re-checking:
    // this case is about reloading state, not about the umask of the host.
    for ent in std::fs::read_dir(t.path("st/state")).unwrap() {
        let p = ent.unwrap().path();
        if p.is_file() {
            crate::common::chmod(&p, 0o600);
        }
    }

    // A second manager over the same directory sees the stored value, and the
    // permission check passes over the files the first one wrote.
    let again = FsStateMgr::from_path_and_mistrust(t.path("st"), &strict(t.root())).unwrap();
    let v: Option<u32> = again.load("counters").unwrap();
    assert_eq!(v, Some(42));
}
