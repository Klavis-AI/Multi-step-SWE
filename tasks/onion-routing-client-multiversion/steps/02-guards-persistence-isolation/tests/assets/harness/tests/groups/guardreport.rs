//! Group `guardreport` - handing a guard out, and the lightweight path-bias
//! bookkeeping that goes with it.
//!
//! Every guard the manager hands out comes with a `GuardMonitor` that the
//! caller must report on and a `GuardUsable` future that says whether the
//! resulting circuit may carry traffic. Graded on the usability answer for a
//! primary guard, on selection staying inside the small primary set, on the
//! four reportable outcomes, and on what a new consensus does to a guard that
//! is no longer listed.

use tor_guardmgr::{GuardMgr, GuardStatus, GuardUsage};
use tor_linkspec::ChanTarget;
use tor_persist::{StateMgr, TestingStateMgr};

fn mgr_with(state: TestingStateMgr) -> GuardMgr<crate::common::TestRt> {
    GuardMgr::new(crate::common::runtime(), state).expect("GuardMgr::new failed")
}

#[test]
fn guardreport__a_primary_guard_is_usable_without_waiting() {
    let nd = crate::common::testnet();
    let gm = mgr_with(TestingStateMgr::new());
    gm.update_network(&nd);

    let (_guard, mon, usable) = gm
        .select_guard(GuardUsage::default(), Some(&nd))
        .expect("select_guard failed");
    // A primary guard needs no permission from anyone, so this future is
    // already resolved and blocking on it cannot deadlock.
    let answer = futures::executor::block_on(usable).expect("usability future was cancelled");
    assert!(
        answer,
        "a circuit through a primary guard was reported as unusable"
    );
    mon.succeeded();
}

#[test]
fn guardreport__repeated_data_requests_stay_inside_the_primary_guards() {
    let nd = crate::common::testnet();
    let gm = mgr_with(TestingStateMgr::new());
    gm.update_network(&nd);

    let mut seen: Vec<String> = Vec::new();
    for _ in 0..12 {
        let (guard, mon, _u) = gm
            .select_guard(GuardUsage::default(), Some(&nd))
            .expect("select_guard failed");
        let id = guard.ed_identity().to_string();
        if !seen.contains(&id) {
            seen.push(id);
        }
        let relay = guard.get_relay(&nd).expect("guard left the directory");
        assert!(relay.is_flagged_guard());
        mon.attempt_abandoned();
    }
    // The network has twenty Guard-flagged relays; the algorithm keeps a
    // handful of primary guards and does not spread requests over all of them.
    assert!(
        seen.len() <= 3,
        "twelve data requests used {} different guards; guards are not sticky",
        seen.len()
    );
}

#[test]
fn guardreport__there_are_four_reportable_outcomes() {
    let all = [
        GuardStatus::Success,
        GuardStatus::Failure,
        GuardStatus::Indeterminate,
        GuardStatus::AttemptAbandoned,
    ];
    let mut names: Vec<String> = all.iter().map(|s| format!("{:?}", s)).collect();
    names.sort();
    names.dedup();
    assert_eq!(names.len(), 4, "GuardStatus outcomes collide: {:?}", names);
    assert_eq!(
        names,
        vec![
            "AttemptAbandoned".to_string(),
            "Failure".to_string(),
            "Indeterminate".to_string(),
            "Success".to_string()
        ]
    );

    // GuardStatus is Copy: a status can be kept after being reported.
    let s = GuardStatus::Indeterminate;
    let copied = s;
    assert_eq!(format!("{:?}", s), format!("{:?}", copied));
}

#[test]
fn guardreport__a_handed_out_guard_is_in_the_persisted_sample() {
    let nd = crate::common::netdir_with_one_guard(38);
    let state = TestingStateMgr::new();
    state.try_lock().expect("try_lock failed");
    let gm = mgr_with(state.clone());
    gm.update_network(&nd);

    let (guard, mon, _u) = gm
        .select_guard(GuardUsage::default(), Some(&nd))
        .expect("select_guard failed");
    let ed = *guard.ed_identity();
    mon.attempt_abandoned();

    gm.store_persistent_state().expect("store failed");
    let doc: serde_json::Value = state
        .load("default_guards")
        .expect("load failed")
        .expect("nothing stored");
    let guards = doc["guards"].as_array().expect("no guards array");
    let ed_text = format!("{}", ed);
    assert!(
        guards
            .iter()
            .any(|g| g["id"]["ed25519"].as_str() == Some(ed_text.as_str())),
        "the guard that was handed out is not in the persisted sample: {}",
        doc
    );
}

#[test]
fn guardreport__a_guard_dropped_from_the_consensus_is_marked_unlisted() {
    let with_38 = crate::common::netdir_with_one_guard(38);
    let with_36 = crate::common::netdir_with_one_guard(36);

    let state = TestingStateMgr::new();
    state.try_lock().expect("try_lock failed");
    let gm = mgr_with(state.clone());
    gm.update_network(&with_38);
    gm.store_persistent_state().expect("store failed");

    let doc: serde_json::Value = state.load("default_guards").expect("load").expect("stored");
    let guards = doc["guards"].as_array().expect("no guards array");
    assert_eq!(guards.len(), 1, "expected one sampled guard: {}", doc);
    assert_eq!(guards[0]["unlisted_since"], serde_json::Value::Null);

    // A new consensus in which relay 38 no longer carries the Guard flag.
    gm.update_network(&with_36);
    gm.store_persistent_state().expect("store failed");

    let doc: serde_json::Value = state.load("default_guards").expect("load").expect("stored");
    let guards = doc["guards"].as_array().expect("no guards array");
    let old = guards
        .iter()
        .find(|g| {
            g["id"]["ed25519"].as_str()
                == Some(format!("{}", crate::common::testnet_ed_id(38)).as_str())
        })
        .expect("the old guard vanished from the sample instead of being marked");
    assert!(
        !old["unlisted_since"].is_null(),
        "a guard that lost its Guard flag was not marked unlisted: {}",
        old
    );
}
