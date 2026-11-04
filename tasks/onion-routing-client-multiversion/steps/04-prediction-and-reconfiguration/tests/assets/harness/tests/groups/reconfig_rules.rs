//! Group `reconfig_rules`: the three-way policy that decides what happens when
//! a live reconfiguration touches a field that cannot change.
//!
//! `instruction.md` section 1: "live reconfiguration".  `tor_config::Reconfigure`
//! is the argument every `reconfigure` method takes, and
//! `tor_config::ReconfigureError` is what it reports.

use tor_config::{Reconfigure, ReconfigureError};

#[test]
fn reconfig_rules__all_or_nothing_refuses_an_unchangeable_field() {
    let err = Reconfigure::AllOrNothing
        .cannot_change("storage.cache_path")
        .expect_err("AllOrNothing must refuse");
    match err {
        ReconfigureError::CannotChange { field } => assert_eq!(field, "storage.cache_path"),
        other => panic!("wrong error variant: {:?}", other),
    }
}

#[test]
fn reconfig_rules__check_all_or_nothing_refuses_without_applying() {
    let err = Reconfigure::CheckAllOrNothing
        .cannot_change("network.authorities")
        .expect_err("CheckAllOrNothing must refuse");
    match err {
        ReconfigureError::CannotChange { field } => assert_eq!(field, "network.authorities"),
        other => panic!("wrong error variant: {:?}", other),
    }
}

#[test]
fn reconfig_rules__warn_on_failures_accepts_an_unchangeable_field() {
    assert!(Reconfigure::WarnOnFailures.cannot_change("anything").is_ok());
    assert!(Reconfigure::WarnOnFailures
        .cannot_change(String::from("storage.cache_path"))
        .is_ok());
}

#[test]
fn reconfig_rules__error_message_names_the_field() {
    let err = Reconfigure::AllOrNothing
        .cannot_change("the_laws_of_physics")
        .expect_err("must refuse");
    assert_eq!(
        err.to_string(),
        "Cannot change the_laws_of_physics on a running client."
    );
}

#[test]
fn reconfig_rules__cannot_change_accepts_any_string_like_field_name() {
    // The field name is taken by `AsRef<str>`, so callers can pass either a
    // literal or an owned, computed name.
    let owned = format!("{}.{}", "preemptive_circuits", "initial_predicted_ports");
    let err = Reconfigure::AllOrNothing
        .cannot_change(&owned)
        .expect_err("must refuse");
    match err {
        ReconfigureError::CannotChange { field } => {
            assert_eq!(field, "preemptive_circuits.initial_predicted_ports");
        }
        other => panic!("wrong error variant: {:?}", other),
    }
}

#[test]
fn reconfig_rules__the_three_modes_are_distinct_and_copyable() {
    let modes = [
        Reconfigure::AllOrNothing,
        Reconfigure::WarnOnFailures,
        Reconfigure::CheckAllOrNothing,
    ];
    for (i, a) in modes.iter().enumerate() {
        for (j, b) in modes.iter().enumerate() {
            assert_eq!(i == j, a == b, "modes {} and {} compared wrongly", i, j);
        }
    }

    // `Reconfigure` is `Copy`, so a caller can pass it down to each subsystem
    // in turn without cloning.
    fn pass_down(how: Reconfigure) -> bool {
        how.cannot_change("f").is_ok()
    }
    let how = Reconfigure::AllOrNothing;
    assert!(!pass_down(how));
    assert!(!pass_down(how));
    assert!(pass_down(Reconfigure::WarnOnFailures));
}

#[test]
fn reconfig_rules__check_mode_is_distinguishable_from_the_mode_that_applies() {
    // A caller implementing "all or nothing" first runs the check pass and then
    // the real pass; the two must not be the same value, or the check pass
    // would have side effects.
    assert_ne!(Reconfigure::AllOrNothing, Reconfigure::CheckAllOrNothing);
    // Both refuse, so a dry run reports the same problem the real run would.
    let a = Reconfigure::AllOrNothing.cannot_change("x");
    let b = Reconfigure::CheckAllOrNothing.cannot_change("x");
    assert_eq!(
        a.expect_err("refuse").to_string(),
        b.expect_err("refuse").to_string()
    );
}
