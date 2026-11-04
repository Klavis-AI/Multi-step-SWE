//! Group `circmgr_live` — a running circuit manager: how it is assembled from
//! the three configuration sections, which directories its guard manager
//! vetoes, and which settings it refuses to change while it runs.

// The synthetic directory fixtures. They are included here rather than in
// `tests/common/mod.rs` so that a candidate who has not built
// `tor_netdir::testnet` loses only the groups that need a directory; see
// that file's header.
#[path = "netdir_support.rs"]
mod netdir_support;
use std::sync::Arc;
use std::time::Duration;
use tor_chanmgr::ChanMgr;
use tor_circmgr::{CircMgr, CircMgrConfig, PreemptiveCircuitConfig};
use tor_config::{Reconfigure, ReconfigureError};
use tor_netdir::testnet::NodeBuilders;
use tor_netdir::NetDir;
use tor_persist::TestingStateMgr;
use tor_rtcompat::Runtime;

/// The configuration the manager under test starts from.
fn base_config() -> CircMgrConfig {
    CircMgrConfig::builder()
        .preemptive_circuits(
            PreemptiveCircuitConfig::builder()
                .initial_predicted_ports(vec![80, 443])
                .prediction_lifetime(Duration::from_secs(600))
                .build()
                .expect("preemptive config should build"),
        )
        .build()
        .expect("circuit manager config should build")
}

/// The same configuration with a different set of initially predicted ports.
/// That list is only consulted at startup, so it cannot be changed later.
fn config_with_other_initial_ports() -> CircMgrConfig {
    CircMgrConfig::builder()
        .preemptive_circuits(
            PreemptiveCircuitConfig::builder()
                .initial_predicted_ports(vec![80, 443, 8080])
                .prediction_lifetime(Duration::from_secs(600))
                .build()
                .expect("preemptive config should build"),
        )
        .build()
        .expect("circuit manager config should build")
}

/// The same initial ports, but other preemptive settings changed. Those are
/// changeable while the client runs.
fn config_with_other_lifetime() -> CircMgrConfig {
    CircMgrConfig::builder()
        .preemptive_circuits(
            PreemptiveCircuitConfig::builder()
                .initial_predicted_ports(vec![80, 443])
                .prediction_lifetime(Duration::from_secs(90))
                .min_exit_circs_for_port(5)
                .build()
                .expect("preemptive config should build"),
        )
        .build()
        .expect("circuit manager config should build")
}

/// Build a circuit manager on `rt` from [`base_config`].
fn circmgr_on<R: Runtime>(rt: &R) -> Arc<CircMgr<R>> {
    let chanmgr = Arc::new(ChanMgr::new(rt.clone()));
    CircMgr::new(base_config(), TestingStateMgr::new(), rt, chanmgr)
        .expect("circuit manager should be constructible")
}

/// A directory whose only guard-flagged directory caches are relays 30 and 32,
/// further customised by `func`.
fn pinned_netdir<F>(mut func: F) -> NetDir
where
    F: FnMut(usize, &mut NodeBuilders),
{
    netdir_support::netdir_custom(move |idx, nb| {
        netdir_support::only_these_are_dir_caches(&[30, 32], idx, nb);
        func(idx, nb);
    })
}

#[test]
fn circmgr_live__is_assembled_from_the_three_configuration_sections() {
    tor_rtcompat::tokio::test_with_runtime(|rt| async move {
        let circmgr = circmgr_on(&rt);
        let netdir = pinned_netdir(|_idx, _nb| {});
        // Before any directory has been seen there are no primary guards, so
        // nothing can be missing and every directory is acceptable.
        assert!(circmgr.netdir_is_sufficient(&netdir));
        circmgr.update_network(&netdir);
        assert!(circmgr.netdir_is_sufficient(&netdir));
        // Re-applying the very configuration it was built from is a no-op that
        // every reconfiguration mode accepts.
        assert!(circmgr.reconfigure(&base_config(), Reconfigure::AllOrNothing).is_ok());
    });
}

#[test]
fn circmgr_live__a_directory_missing_a_primary_guards_microdescriptor_is_vetoed() {
    tor_rtcompat::tokio::test_with_runtime(|rt| async move {
        let circmgr = circmgr_on(&rt);
        let netdir = pinned_netdir(|_idx, _nb| {});
        let without_mds = pinned_netdir(|idx, nb| {
            if idx == 30 || idx == 32 {
                nb.omit_md = true;
            }
        });
        circmgr.update_network(&netdir);
        assert!(circmgr.netdir_is_sufficient(&netdir));
        assert!(!circmgr.netdir_is_sufficient(&without_mds));
    });
}

#[test]
fn circmgr_live__reconfiguring_refuses_a_change_to_the_initial_predicted_ports() {
    tor_rtcompat::tokio::test_with_runtime(|rt| async move {
        let circmgr = circmgr_on(&rt);
        match circmgr.reconfigure(&config_with_other_initial_ports(), Reconfigure::AllOrNothing) {
            Err(ReconfigureError::CannotChange { field }) => {
                assert_eq!(field, "preemptive_circuits.initial_predicted_ports");
            }
            other => panic!("expected a refusal, got {:?}", other),
        }
    });
}

#[test]
fn circmgr_live__a_check_only_reconfiguration_reports_the_same_refusal() {
    tor_rtcompat::tokio::test_with_runtime(|rt| async move {
        let circmgr = circmgr_on(&rt);
        match circmgr.reconfigure(
            &config_with_other_initial_ports(),
            Reconfigure::CheckAllOrNothing,
        ) {
            Err(ReconfigureError::CannotChange { field }) => {
                assert_eq!(field, "preemptive_circuits.initial_predicted_ports");
            }
            other => panic!("expected a refusal, got {:?}", other),
        }
        // A check applies nothing, so the identical change is refused again.
        assert!(matches!(
            circmgr.reconfigure(&config_with_other_initial_ports(), Reconfigure::AllOrNothing),
            Err(ReconfigureError::CannotChange { .. })
        ));
    });
}

#[test]
fn circmgr_live__warning_mode_accepts_the_unchangeable_change() {
    tor_rtcompat::tokio::test_with_runtime(|rt| async move {
        let circmgr = circmgr_on(&rt);
        assert!(circmgr
            .reconfigure(
                &config_with_other_initial_ports(),
                Reconfigure::WarnOnFailures
            )
            .is_ok());
    });
}

#[test]
fn circmgr_live__changes_to_the_changeable_settings_are_accepted() {
    tor_rtcompat::tokio::test_with_runtime(|rt| async move {
        let circmgr = circmgr_on(&rt);
        for how in [
            Reconfigure::CheckAllOrNothing,
            Reconfigure::AllOrNothing,
            Reconfigure::WarnOnFailures,
        ] {
            assert!(
                circmgr.reconfigure(&config_with_other_lifetime(), how).is_ok(),
                "a changeable setting was refused under {:?}",
                how
            );
            assert!(circmgr.reconfigure(&base_config(), how).is_ok());
        }
    });
}
