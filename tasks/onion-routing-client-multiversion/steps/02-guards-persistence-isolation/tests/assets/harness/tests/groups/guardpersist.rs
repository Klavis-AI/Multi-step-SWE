//! Group `guardpersist` - the guard sample as shared on-disk state.
//!
//! The guard manager keeps its sample under the `default_guards` key of the
//! `tor_persist::StateMgr` it was built with. Graded on the key, the shape of
//! the stored document, the refusal to write without the lock, and the fact
//! that a second manager picks up the same sample.

use tor_guardmgr::{GuardMgr, GuardMgrError, GuardUsage};
use tor_linkspec::ChanTarget;
use tor_persist::{StateMgr, TestingStateMgr};

/// The state key the guard manager is documented to use.
const KEY: &str = "default_guards";

fn stored(mgr: &TestingStateMgr) -> serde_json::Value {
    mgr.load(KEY)
        .expect("load failed")
        .expect("nothing stored under the default_guards key")
}

#[test]
fn guardpersist__storing_without_the_lock_is_refused() {
    let nd = crate::common::testnet();
    let state = TestingStateMgr::new();
    let gm = GuardMgr::new(crate::common::runtime(), state.clone()).expect("GuardMgr::new failed");
    gm.update_network(&nd);

    let err = gm
        .store_persistent_state()
        .expect_err("guard state was written without holding the lock");
    assert!(
        matches!(err, GuardMgrError::State(tor_persist::Error::NoLock)),
        "expected a NoLock state error, got {:?}",
        err
    );
    let nothing: Option<serde_json::Value> = state.load(KEY).expect("load failed");
    assert_eq!(nothing, None);
}

#[test]
fn guardpersist__the_sample_is_stored_under_the_default_guards_key() {
    let nd = crate::common::testnet();
    let state = TestingStateMgr::new();
    state.try_lock().expect("try_lock failed");
    let gm = GuardMgr::new(crate::common::runtime(), state.clone()).expect("GuardMgr::new failed");
    gm.update_network(&nd);
    gm.store_persistent_state().expect("store failed");

    let doc = stored(&state);
    let obj = doc.as_object().expect("stored guard state is not an object");
    assert!(
        obj.contains_key("guards") && obj.contains_key("confirmed"),
        "stored guard state has keys {:?}",
        obj.keys().collect::<Vec<_>>()
    );
    let guards = obj["guards"].as_array().expect("`guards` is not an array");
    assert!(!guards.is_empty(), "no guards were sampled from the network");
    assert_eq!(
        obj["confirmed"].as_array().map(|a| a.len()),
        Some(0),
        "a guard was recorded as confirmed before it was ever used"
    );
}

#[test]
fn guardpersist__every_persisted_guard_names_a_guard_flagged_relay() {
    let nd = crate::common::testnet();
    let state = TestingStateMgr::new();
    state.try_lock().expect("try_lock failed");
    let gm = GuardMgr::new(crate::common::runtime(), state.clone()).expect("GuardMgr::new failed");
    gm.update_network(&nd);
    gm.store_persistent_state().expect("store failed");

    let doc = stored(&state);
    let guards = doc["guards"].as_array().expect("`guards` is not an array");
    for g in guards {
        let id = &g["id"];
        assert!(
            id["ed25519"].is_string() && id["rsa"].is_string(),
            "a persisted guard has no identity pair: {}",
            g
        );
        assert!(
            g["added_at"].is_string(),
            "a persisted guard has no `added_at` timestamp: {}",
            g
        );
        assert_eq!(
            g["confirmed_at"],
            serde_json::Value::Null,
            "an unused guard was persisted as confirmed"
        );
        assert_eq!(
            g["unlisted_since"],
            serde_json::Value::Null,
            "a currently-listed guard was persisted as unlisted"
        );
        let orports = g["orports"]
            .as_array()
            .expect("a persisted guard has no `orports` list");
        assert!(!orports.is_empty(), "a persisted guard has no OR address");
    }
}

#[test]
fn guardpersist__a_second_manager_starts_from_the_stored_sample() {
    let nd = crate::common::netdir_with_one_guard(38);
    let state = TestingStateMgr::new();
    state.try_lock().expect("try_lock failed");

    {
        let gm = GuardMgr::new(crate::common::runtime(), state.clone())
            .expect("GuardMgr::new failed");
        gm.update_network(&nd);
        gm.store_persistent_state().expect("store failed");
    }
    let first = stored(&state);
    assert_eq!(
        first["guards"].as_array().map(|a| a.len()),
        Some(1),
        "expected exactly one sampled guard: {}",
        first
    );

    // A brand-new manager over the same storage, which never sees a directory.
    // Flushing it back must reproduce the sample it was constructed with: an
    // implementation that ignores stored state would write an empty sample.
    let gm2 =
        GuardMgr::new(crate::common::runtime(), state.clone()).expect("GuardMgr::new failed");
    gm2.store_persistent_state().expect("store failed");
    let second = stored(&state);
    assert_eq!(
        second["guards"].as_array().map(|a| a.len()),
        Some(1),
        "the second manager did not start from the stored sample: {}",
        second
    );
    assert_eq!(
        second["guards"][0]["id"]["ed25519"].as_str(),
        Some(format!("{}", crate::common::testnet_ed_id(38)).as_str())
    );
    assert_eq!(first, second, "the reloaded sample was not preserved");
}

#[test]
fn guardpersist__reload_and_upgrade_adopt_state_written_elsewhere() {
    let nd = crate::common::netdir_with_one_guard(38);
    let state = TestingStateMgr::new();
    state.try_lock().expect("try_lock failed");

    // A manager that never sees a directory starts out with an empty sample.
    let gm = GuardMgr::new(crate::common::runtime(), state.clone()).expect("GuardMgr::new failed");
    assert!(gm.select_guard(GuardUsage::default(), None).is_err());

    // Something else writes a sample to the shared storage.
    {
        let other = GuardMgr::new(crate::common::runtime(), state.clone())
            .expect("GuardMgr::new failed");
        other.update_network(&nd);
        other.store_persistent_state().expect("store failed");
    }

    gm.reload_persistent_state().expect("reload failed");
    let (guard, mon, _usable) = gm
        .select_guard(GuardUsage::default(), None)
        .expect("reload_persistent_state did not adopt the stored sample");
    assert_eq!(guard.ed_identity(), &crate::common::testnet_ed_id(38));
    mon.attempt_abandoned();

    gm.upgrade_to_owned_persistent_state()
        .expect("upgrade_to_owned_persistent_state failed");
    let (guard, mon, _usable) = gm
        .select_guard(GuardUsage::default(), None)
        .expect("the upgraded manager lost its sample");
    assert_eq!(guard.ed_identity(), &crate::common::testnet_ed_id(38));
    mon.attempt_abandoned();
}
