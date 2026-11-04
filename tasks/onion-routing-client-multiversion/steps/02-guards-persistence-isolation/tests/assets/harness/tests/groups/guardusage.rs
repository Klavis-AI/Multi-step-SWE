//! Group `guardusage` - how a request narrows the guard the manager may pick.
//!
//! `tor_guardmgr::GuardUsage` (with its `derive_builder` builder),
//! `GuardUsageKind`, `GuardRestriction` and `GuardFilter` are the knobs a
//! caller has. Graded on the defaults, the builder's optional restriction,
//! and the effect a restriction actually has on selection.

use tor_guardmgr::{
    GuardFilter, GuardMgr, GuardRestriction, GuardUsage, GuardUsageBuilder, GuardUsageKind,
    PickGuardError,
};
use tor_linkspec::ChanTarget;
use tor_persist::TestingStateMgr;

fn mgr() -> GuardMgr<crate::common::TestRt> {
    GuardMgr::new(crate::common::runtime(), TestingStateMgr::new()).expect("GuardMgr::new failed")
}

#[test]
fn guardusage__the_default_usage_is_a_data_circuit() {
    assert_eq!(GuardUsageKind::default(), GuardUsageKind::Data);
    let dbg = format!("{:?}", GuardUsage::default());
    assert!(
        dbg.contains("Data"),
        "the default GuardUsage is not a Data usage: {}",
        dbg
    );
    assert!(
        dbg.contains("None"),
        "the default GuardUsage carries a restriction: {}",
        dbg
    );
}

#[test]
fn guardusage__the_builder_selects_the_usage_kind() {
    let mut b = GuardUsageBuilder::new();
    b.kind(GuardUsageKind::OneHopDirectory);
    let built = b.build().expect("build failed");
    assert!(
        format!("{:?}", built).contains("OneHopDirectory"),
        "builder did not record the OneHopDirectory kind: {:?}",
        built
    );

    // An empty builder still builds: every field has a default.
    let empty = GuardUsageBuilder::new().build().expect("empty build failed");
    assert!(format!("{:?}", empty).contains("Data"));
    assert_ne!(GuardUsageKind::Data, GuardUsageKind::OneHopDirectory);
}

#[test]
fn guardusage__the_builders_restriction_setter_takes_a_bare_identity() {
    // `restriction` is `Option<GuardRestriction>` with `setter(strip_option)`,
    // so the setter takes the restriction itself, not an Option.
    let mut b = GuardUsageBuilder::new();
    b.restriction(GuardRestriction::AvoidId(crate::common::testnet_ed_id(38)));
    let built = b.build().expect("build failed");
    let dbg = format!("{:?}", built);
    assert!(
        dbg.contains("AvoidId") && dbg.contains("Some"),
        "builder did not record the restriction: {}",
        dbg
    );
}

#[test]
fn guardusage__a_restriction_excludes_that_identity_from_selection() {
    // Exactly one relay in this network may be a guard, so excluding it leaves
    // the manager with nothing it is allowed to hand out.
    let nd = crate::common::netdir_with_one_guard(38);
    let gm = mgr();
    gm.update_network(&nd);

    let (guard, mon, _u) = gm
        .select_guard(GuardUsage::default(), Some(&nd))
        .expect("select_guard failed");
    assert_eq!(guard.ed_identity(), &crate::common::testnet_ed_id(38));
    mon.attempt_abandoned();

    let mut b = GuardUsageBuilder::new();
    b.restriction(GuardRestriction::AvoidId(crate::common::testnet_ed_id(38)));
    let restricted = b.build().expect("build failed");

    let err = match gm.select_guard(restricted, Some(&nd)) {
        Ok(_) => panic!("a restricted request was answered with the excluded guard"),
        Err(e) => e,
    };
    assert!(
        matches!(err, PickGuardError::EveryoneIsDown),
        "expected EveryoneIsDown, got {:?}",
        err
    );
}

#[test]
fn guardusage__the_default_filter_is_unfiltered_and_keeps_guards_usable() {
    assert!(
        matches!(GuardFilter::default(), GuardFilter::Unfiltered),
        "the default GuardFilter is not Unfiltered"
    );
    assert!(matches!(GuardFilter::unfiltered(), GuardFilter::Unfiltered));

    let nd = crate::common::netdir_with_one_guard(38);
    let gm = mgr();
    gm.update_network(&nd);
    gm.set_filter(GuardFilter::unfiltered(), &nd);

    let (guard, mon, _u) = gm
        .select_guard(GuardUsage::default(), Some(&nd))
        .expect("an unfiltered filter made every guard unusable");
    assert_eq!(guard.ed_identity(), &crate::common::testnet_ed_id(38));
    mon.attempt_abandoned();
}
