//! Group `guardsel` - the guard algorithm's selection step.
//!
//! Graded through `tor_guardmgr::GuardMgr`: an empty sample is reported rather
//! than papered over, a selected guard is always a `Guard`-flagged relay that
//! is still in the directory, and the identity handed back really does name
//! that relay.

use tor_guardmgr::{GuardMgr, GuardUsage, GuardUsageBuilder, GuardUsageKind, PickGuardError};
use tor_linkspec::ChanTarget;
use tor_persist::TestingStateMgr;

fn mgr() -> GuardMgr<crate::common::TestRt> {
    GuardMgr::new(crate::common::runtime(), TestingStateMgr::new()).expect("GuardMgr::new failed")
}

#[test]
fn guardsel__selection_without_a_directory_is_a_pick_error() {
    // A manager that has never seen a directory has nothing to sample from,
    // and `None` gives it no way to fix that, so the request must fail rather
    // than invent a guard.
    let gm = mgr();
    let err = match gm.select_guard(GuardUsage::default(), None) {
        Ok(_) => panic!("a guard manager with no directory handed out a guard"),
        Err(e) => e,
    };
    // Both documented variants describe "no guard available"; either is a
    // correct answer here, but the reported reason has to be one of them.
    // The variant is what is graded; the wording of the message is not.
    assert!(
        matches!(
            err,
            PickGuardError::EveryoneIsDown | PickGuardError::SampleIsEmpty
        ),
        "unexpected PickGuardError variant: {:?}",
        err
    );
    assert!(
        !err.to_string().is_empty(),
        "the refusal rendered as an empty string"
    );
}

#[test]
fn guardsel__a_selected_guard_is_a_guard_flagged_relay() {
    let nd = crate::common::testnet();
    let gm = mgr();
    gm.update_network(&nd);

    let (guard, mon, _usable) = gm
        .select_guard(GuardUsage::default(), Some(&nd))
        .expect("select_guard failed against the test network");
    let relay = guard
        .get_relay(&nd)
        .expect("the selected guard is not in the directory it came from");
    assert!(
        relay.is_flagged_guard(),
        "selected a relay without the Guard flag"
    );
    mon.attempt_abandoned();
}

#[test]
fn guardsel__the_guard_identity_names_the_same_relay_as_the_guard() {
    let nd = crate::common::testnet();
    let gm = mgr();
    gm.update_network(&nd);

    let (guard, mon, _usable) = gm
        .select_guard(GuardUsage::default(), Some(&nd))
        .expect("select_guard failed");

    let by_id = guard
        .id()
        .get_relay(&nd)
        .expect("GuardId::get_relay found nothing");
    let by_guard = guard.get_relay(&nd).expect("Guard::get_relay found nothing");
    assert_eq!(by_id.id(), by_guard.id());
    assert_eq!(by_id.rsa_id(), by_guard.rsa_id());

    // The ChanTarget view of a guard has to agree with the directory, or the
    // channel manager would dial the wrong relay.
    assert_eq!(guard.ed_identity(), by_id.id());
    assert_eq!(guard.rsa_identity(), by_id.rsa_id());
    assert_eq!(guard.addrs(), by_id.addrs());
    assert!(!guard.addrs().is_empty(), "guard has no OR addresses");
    mon.attempt_abandoned();
}

#[test]
fn guardsel__the_sample_is_drawn_only_from_guard_flagged_relays() {
    // In this network exactly one relay carries the Guard flag, so there is
    // only one correct answer and no randomness left to observe.
    let nd = crate::common::netdir_with_one_guard(38);
    let gm = mgr();
    gm.update_network(&nd);

    let want = crate::common::testnet_ed_id(38);
    for _ in 0..5 {
        let (guard, mon, _usable) = gm
            .select_guard(GuardUsage::default(), Some(&nd))
            .expect("select_guard failed");
        assert_eq!(
            guard.ed_identity(),
            &want,
            "selected a relay that does not carry the Guard flag"
        );
        mon.attempt_abandoned();
    }
}

#[test]
fn guardsel__one_hop_directory_usage_selects_from_the_same_sample() {
    let nd = crate::common::netdir_with_one_guard(38);
    let gm = mgr();
    gm.update_network(&nd);

    let mut b = GuardUsageBuilder::new();
    b.kind(GuardUsageKind::OneHopDirectory);
    let usage = b.build().expect("GuardUsageBuilder::build failed");

    let (guard, mon, _usable) = gm
        .select_guard(usage, Some(&nd))
        .expect("select_guard failed for a one-hop directory request");
    assert_eq!(guard.ed_identity(), &crate::common::testnet_ed_id(38));
    mon.attempt_abandoned();
}

#[test]
fn guardsel__a_directory_supplied_at_selection_time_seeds_the_sample() {
    // No update_network call at all: the netdir passed to select_guard has to
    // be enough to extend an empty sample.
    let nd = crate::common::netdir_with_one_guard(38);
    let gm = mgr();

    let (guard, mon, _usable) = gm
        .select_guard(GuardUsage::default(), Some(&nd))
        .expect("select_guard could not seed a sample from its netdir argument");
    assert_eq!(guard.ed_identity(), &crate::common::testnet_ed_id(38));
    mon.attempt_abandoned();
}
