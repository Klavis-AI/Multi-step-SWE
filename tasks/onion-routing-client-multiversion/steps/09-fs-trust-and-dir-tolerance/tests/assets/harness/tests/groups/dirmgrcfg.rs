//! `tor_dirmgr::DirMgrConfig` grew two fields at this checkpoint: `cache_trust`
//! (the `fs_mistrust::Mistrust` used for the cache directory) and `tolerance`
//! (the clock-skew allowance).  The struct has no public builder and no
//! non-test `Default`, so it is built by struct literal, which is exactly how
//! `arti-client` builds it.
#![allow(non_snake_case)]
use fs_mistrust::Mistrust;
use std::path::PathBuf;
use std::time::Duration;
use tor_dirmgr::{DirMgrConfig, DirSkewTolerance, DownloadScheduleConfig, NetworkConfig};
use tor_netdoc::doc::netstatus::NetParams;

/// Build a config with the given cache trust and tolerance; everything else
/// takes the compiled-in defaults.
fn config(path: &str, trust: Mistrust, tolerance: DirSkewTolerance) -> DirMgrConfig {
    DirMgrConfig {
        cache_path: PathBuf::from(path),
        cache_trust: trust,
        network: NetworkConfig::builder().build().unwrap(),
        schedule: DownloadScheduleConfig::default(),
        tolerance,
        override_net_params: NetParams::new(),
        extensions: Default::default(),
    }
}

#[test]
fn dirmgrcfg__cache_trust_field_holds_a_mistrust() {
    let cfg = config(
        "/nonexistent/cache",
        Mistrust::new_dangerously_trust_everyone(),
        DirSkewTolerance::default(),
    );
    assert_eq!(cfg.cache_trust, Mistrust::new_dangerously_trust_everyone());
    assert_ne!(cfg.cache_trust, Mistrust::new());
    assert_eq!(cfg.cache_path, PathBuf::from("/nonexistent/cache"));
}

#[test]
fn dirmgrcfg__tolerance_field_holds_a_skew_tolerance() {
    let custom = DirSkewTolerance::builder()
        .post_valid_tolerance(Duration::from_secs(1234))
        .build()
        .unwrap();
    let a = config("/c", Mistrust::new(), custom.clone());
    let b = config("/c", Mistrust::new(), DirSkewTolerance::default());
    assert_eq!(a.tolerance, custom);
    assert_ne!(a.tolerance, b.tolerance);
    assert_eq!(b.tolerance, DirSkewTolerance::default());
}

#[test]
fn dirmgrcfg__override_net_params_is_readable() {
    let mut params = NetParams::new();
    params.set("circwindow".to_string(), 555_i32);
    let cfg = DirMgrConfig {
        override_net_params: params,
        ..config("/c", Mistrust::new(), DirSkewTolerance::default())
    };
    assert_eq!(cfg.override_net_params.get("circwindow"), Some(&555));
    assert_eq!(cfg.override_net_params.get("cbtmincircs"), None);
}

#[test]
fn dirmgrcfg__builtin_authorities_and_fallbacks_are_present() {
    let cfg = config("/c", Mistrust::new(), DirSkewTolerance::default());
    // dir-spec requires a client to know the authority set up front; arti ships
    // a compiled-in list, and a fallback list to reach before it has a cache.
    assert!(cfg.authorities().len() >= 3, "{}", cfg.authorities().len());
    assert!(cfg.fallbacks().len() >= 3, "{}", cfg.fallbacks().len());
}

#[test]
fn dirmgrcfg__arti_client_reexports_the_same_dir_types() {
    // `arti_client::config::dir` must name the very same types, because that is
    // the only path by which an embedder can see the new fields.
    let trust = Mistrust::new_dangerously_trust_everyone();
    let tolerance: DirSkewTolerance = arti_client::config::dir::DirSkewTolerance::builder()
        .pre_valid_tolerance(Duration::from_secs(7))
        .build()
        .unwrap();
    let via_reexport: arti_client::config::dir::DirMgrConfig = DirMgrConfig {
        cache_trust: trust.clone(),
        tolerance: tolerance.clone(),
        ..config("/c", Mistrust::new(), DirSkewTolerance::default())
    };
    let plain: DirMgrConfig = via_reexport;
    assert_eq!(plain.cache_trust, trust);
    assert_eq!(plain.tolerance, tolerance);
    assert_ne!(plain.tolerance, DirSkewTolerance::default());
}
