//! Group `guard_api` (inherited) - the guard-selection request vocabulary.
//!
//! instruction.md section 1: "**Preserve** guards, persistence and circuit
//! isolation from step 2", and "warnings on guard restrictions" - the warning
//! itself is internal, but the restriction it warns about is the public type
//! graded here.
//!
//! Graded items: `tor_guardmgr::{GuardUsage, GuardUsageBuilder, GuardUsageKind,
//! GuardRestriction, GuardStatus, GuardFilter, GuardMgrError}`.
//!
//! `GuardUsage` has no accessors and no `PartialEq`, so what was configured is
//! read back through its derived `Debug` representation.

use tor_guardmgr::{
    GuardFilter, GuardMgrError, GuardRestriction, GuardStatus, GuardUsage, GuardUsageBuilder,
    GuardUsageKind,
};
use tor_llcrypto::pk::ed25519::Ed25519Identity;

#[test]
fn guard_api__usage_defaults_to_a_data_circuit() {
    assert_eq!(GuardUsageKind::default(), GuardUsageKind::Data);
    assert_ne!(GuardUsageKind::Data, GuardUsageKind::OneHopDirectory);

    let dflt = GuardUsage::default();
    let repr = format!("{:?}", dflt);
    assert!(repr.contains("kind: Data"), "unexpected default usage {:?}", repr);
    assert!(
        repr.contains("restriction: None"),
        "a default usage must carry no restriction, got {:?}",
        repr
    );
}

#[test]
fn guard_api__builder_records_the_usage_kind() {
    let built = GuardUsageBuilder::new()
        .kind(GuardUsageKind::OneHopDirectory)
        .build()
        .expect("GuardUsageBuilder::build failed");
    let repr = format!("{:?}", built);
    assert!(
        repr.contains("OneHopDirectory"),
        "the usage kind was not recorded, got {:?}",
        repr
    );
    assert!(repr.contains("restriction: None"), "unexpected {:?}", repr);

    // An untouched builder yields the default usage.
    let empty = GuardUsageBuilder::new()
        .build()
        .expect("GuardUsageBuilder::build failed");
    assert_eq!(
        format!("{:?}", empty),
        format!("{:?}", GuardUsage::default())
    );
}

#[test]
fn guard_api__restriction_names_the_identity_to_avoid() {
    let id = Ed25519Identity::new([7; 32]);
    let built = GuardUsageBuilder::new()
        .kind(GuardUsageKind::Data)
        .restriction(GuardRestriction::AvoidId(id))
        .build()
        .expect("GuardUsageBuilder::build failed");
    let repr = format!("{:?}", built);
    assert!(
        repr.contains("AvoidId"),
        "the restriction was not recorded, got {:?}",
        repr
    );
    // The identity itself is carried through, not discarded.
    assert!(
        repr.contains(&format!("{:?}", id)),
        "the avoided identity was not recorded, got {:?}",
        repr
    );
    assert_eq!(Ed25519Identity::from_bytes(&[7; 32]), Some(id));
    assert_eq!(Ed25519Identity::from_bytes(&[7; 31]), None);
}

#[test]
fn guard_api__status_and_filter_vocabulary() {
    // Every outcome a circuit attempt can report about its guard.
    for (status, name) in [
        (GuardStatus::Success, "Success"),
        (GuardStatus::Failure, "Failure"),
        (GuardStatus::Indeterminate, "Indeterminate"),
        (GuardStatus::AttemptAbandoned, "AttemptAbandoned"),
    ] {
        assert_eq!(format!("{:?}", status), name);
    }

    assert_eq!(
        format!("{:?}", GuardFilter::unfiltered()),
        format!("{:?}", GuardFilter::default())
    );
}

#[test]
fn guard_api__error_messages() {
    let from_state = GuardMgrError::from(tor_persist::Error::NoLock);
    assert_eq!(from_state.to_string(), "Problem accessing persistent state");

    let from_spawn = GuardMgrError::from(futures::task::SpawnError::shutdown());
    assert_eq!(from_spawn.to_string(), "Unable to spawn task");
}
