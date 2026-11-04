//! Group `dirmgrcfg`: directory manager configuration. At step 07 the
//! download schedule is backed by the decorrelated-jitter `RetryDelay`
//! living in `tor-basic-utils`, and `DirMgrExtensions` is a
//! `non_exhaustive` extension point.
//!
//! Graded items: `tor_dirmgr::{DirMgrConfig, DirMgrExtensions,
//! NetworkConfig, NetworkConfigBuilder, DownloadSchedule,
//! DownloadScheduleConfig, DownloadScheduleConfigBuilder}`.

use std::time::Duration;

use tor_basic_utils::retry::RetryDelay;
use tor_dirmgr::config::DirMgrExtensions;
use tor_dirmgr::{
    DirMgrConfig, DownloadSchedule, DownloadScheduleConfig, NetworkConfig,
    NetworkConfigBuilder,
};
use tor_netdoc::doc::netstatus::NetParams;

fn cfg_with_schedule(schedule: DownloadScheduleConfig, extensions: DirMgrExtensions) -> DirMgrConfig {
    DirMgrConfig {
        cache_path: std::path::PathBuf::from("/nonexistent/step07-dirmgrcfg"),
        network_config: NetworkConfig::default(),
        schedule_config: schedule,
        override_net_params: NetParams::new(),
        extensions,
    }
}

#[test]
fn dirmgrcfg__public_fields_and_accessors_agree() {
    let path = std::path::PathBuf::from("/tmp/step07-cache");
    let cfg = DirMgrConfig {
        cache_path: path.clone(),
        network_config: NetworkConfig::default(),
        schedule_config: DownloadScheduleConfig::default(),
        override_net_params: NetParams::new(),
        extensions: DirMgrExtensions::default(),
    };
    assert_eq!(cfg.cache_path(), path.as_path());
    // `NetworkConfig`'s own fields are `pub(crate)`, so an out-of-crate caller
    // reaches the authority and fallback lists only through `DirMgrConfig`'s
    // accessors and through `NetworkConfig::fallback_caches`.  That asymmetry
    // is the interface, so it is what the case checks.
    assert_eq!(
        cfg.fallbacks().len(),
        cfg.network_config.fallback_caches().len()
    );
    // The shipped defaults are not empty: a client with no authorities and no
    // fallbacks could never bootstrap, so an empty default would be a bug.
    assert!(!cfg.authorities().is_empty());
    assert!(!cfg.fallbacks().is_empty());
    // `NetParams` has no `is_empty`; emptiness is observable through `iter`.
    assert!(cfg.override_net_params().iter().next().is_none());
    assert_eq!(cfg.schedule(), &cfg.schedule_config);
}

#[test]
fn dirmgrcfg__extensions_default_is_constructible() {
    let cfg = cfg_with_schedule(DownloadScheduleConfig::default(), DirMgrExtensions::default());
    let _ = &cfg.extensions;
    assert_eq!(cfg.schedule().clone(), DownloadScheduleConfig::default());
}

#[test]
fn dirmgrcfg__default_bootstrap_schedule_is_128_attempts_of_one_second() {
    let default = DownloadScheduleConfig::default();
    let explicit = DownloadScheduleConfig::builder()
        .retry_bootstrap(DownloadSchedule::new(128, Duration::from_secs(1), 1))
        .retry_consensus(DownloadSchedule::default())
        .retry_certs(DownloadSchedule::default())
        .retry_microdescs(DownloadSchedule::new(3, Duration::from_secs(1), 4))
        .build()
        .unwrap();
    assert_eq!(default, explicit);
}

#[test]
fn dirmgrcfg__schedule_config_round_trips_through_default_equality() {
    let a = DownloadScheduleConfig::default();
    let b = DownloadScheduleConfig::default();
    assert_eq!(a, b);
    let c = DownloadScheduleConfig::builder()
        .retry_consensus(DownloadSchedule::new(7, Duration::from_secs(2), 3))
        .build()
        .unwrap();
    assert_ne!(a, c);
}

#[test]
fn dirmgrcfg__schedule_backed_by_decorrelated_jitter_retrydelay() {
    // The step-07 migration: `DownloadSchedule::schedule()` yields the
    // `tor_basic_utils::retry::RetryDelay` driving decorrelated jitter.
    let sched = DownloadSchedule::new(3, Duration::from_millis(500), 4);
    let mut delay: RetryDelay = sched.schedule();
    // First draw is over a range of size 1, so it is RNG-independent and
    // lands exactly on the (clamped) initial delay.
    let first = delay.next_delay(&mut rand::rngs::mock::StepRng::new(0, 1));
    assert_eq!(first, Duration::from_millis(1000));
}

#[test]
fn dirmgrcfg__network_config_builder_rejects_partial_override() {
    let ok = NetworkConfig::builder().build();
    assert!(ok.is_ok(), "default network config must build");

    let err = NetworkConfigBuilder::default()
        .authorities(vec![])
        .build()
        .err()
        .expect("partial override must be rejected");
    let rendered = err.to_string();
    assert!(
        rendered.contains("authorities") && rendered.contains("fallbacks"),
        "unexpected message: {rendered}"
    );

    let ok = NetworkConfigBuilder::default()
        .authorities(vec![])
        .fallback_caches(tor_guardmgr::fallback::FallbackList::from(Vec::new()))
        .build();
    assert!(ok.is_ok(), "overriding both halves must build");
}
