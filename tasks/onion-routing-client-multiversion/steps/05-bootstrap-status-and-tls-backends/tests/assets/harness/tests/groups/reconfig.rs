//! Group `reconfig` (inherited from step 4) — live reconfiguration primitives.
//!
//! Two things have to keep working across this step: the policy enum that
//! decides what happens when a caller tries to change a field that cannot
//! change on a running client, and the mutable configuration holder that
//! carries a configuration between the thing that changes it and the things
//! that read it.

use std::sync::Arc;

use tor_config::{MutCfg, Reconfigure, ReconfigureError};

#[test]
fn reconfig__all_or_nothing_refuses_a_locked_field() {
    let err = Reconfigure::AllOrNothing
        .cannot_change("storage.cache_dir")
        .err()
        .expect("an all-or-nothing reconfiguration must refuse a locked field");
    assert!(matches!(err, ReconfigureError::CannotChange { .. }));
}

#[test]
fn reconfig__check_all_or_nothing_refuses_a_locked_field() {
    // The dry-run mode must reach the same verdict as the real one, otherwise
    // checking before applying is worthless.
    let err = Reconfigure::CheckAllOrNothing
        .cannot_change("storage.cache_dir")
        .err()
        .expect("the check-only mode must reach the same verdict");
    assert_eq!(
        err.to_string(),
        Reconfigure::AllOrNothing
            .cannot_change("storage.cache_dir")
            .err()
            .expect("refusal")
            .to_string()
    );
}

#[test]
fn reconfig__warn_on_failures_allows_a_locked_field() {
    assert!(
        Reconfigure::WarnOnFailures
            .cannot_change("storage.cache_dir")
            .is_ok(),
        "the lenient mode must proceed rather than fail"
    );
}

#[test]
fn reconfig__error_message_names_the_field() {
    let err = Reconfigure::AllOrNothing
        .cannot_change("the_laws_of_physics")
        .err()
        .expect("refusal");
    assert_eq!(
        err.to_string(),
        "Cannot change the_laws_of_physics on a running client."
    );
    match err {
        ReconfigureError::CannotChange { field } => assert_eq!(field, "the_laws_of_physics"),
        other => panic!("unexpected reconfiguration error: {other:?}"),
    }
}

#[test]
fn reconfig__reconfigure_modes_are_distinct_and_copyable() {
    let how = Reconfigure::WarnOnFailures;
    let copied = how;
    assert_eq!(how, copied);
    assert_ne!(Reconfigure::AllOrNothing, Reconfigure::WarnOnFailures);
    assert_ne!(Reconfigure::AllOrNothing, Reconfigure::CheckAllOrNothing);
    assert_ne!(Reconfigure::WarnOnFailures, Reconfigure::CheckAllOrNothing);
    assert_eq!(format!("{:?}", Reconfigure::AllOrNothing), "AllOrNothing");
}

#[test]
fn reconfig__mut_cfg_replaces_its_contents() {
    let cell = MutCfg::new(100_u32);
    let before = cell.get();
    cell.replace(101);
    // The handle taken before the swap still sees the old configuration, so a
    // reader mid-request is never torn.
    assert_eq!(*before, 100);
    assert_eq!(*cell.get(), 101);
    let from_value: MutCfg<u32> = 7_u32.into();
    assert_eq!(*from_value.get(), 7);
    assert_eq!(*MutCfg::<u32>::default().get(), 0);
}

#[test]
fn reconfig__mut_cfg_check_and_replace_detects_a_stale_handle() {
    let cell = MutCfg::new(100_u32);
    let stale: Arc<u32> = Arc::new(100);
    assert!(
        !cell.check_and_replace(&stale, 200),
        "an equal-but-distinct handle is still a stale handle"
    );
    assert_eq!(*cell.get(), 100);
    let current = cell.get();
    assert!(cell.check_and_replace(&current, 200));
    assert_eq!(*cell.get(), 200);
}

#[test]
fn reconfig__mut_cfg_map_and_replace_transforms_the_value() {
    let cell = MutCfg::new(100_u32);
    let before = cell.get();
    cell.map_and_replace(|old| **old * 20);
    assert_eq!(*cell.get(), 2000);
    assert_eq!(*before, 100);
    assert!(format!("{cell:?}").contains("2000"));
}
