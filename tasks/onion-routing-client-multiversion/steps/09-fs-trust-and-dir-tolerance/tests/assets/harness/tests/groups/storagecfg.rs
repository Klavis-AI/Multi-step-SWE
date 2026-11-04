#![allow(non_snake_case)]
use arti_client::config::circ::PathConfigBuilder;
use arti_client::config::dir::{
    DownloadScheduleConfig, DownloadScheduleConfigBuilder, NetworkConfig, NetworkConfigBuilder,
};
use arti_client::config::ClientAddrConfigBuilder;
use arti_client::config::TorClientConfigBuilder;
use arti_client::TorClientConfig;
use tor_config::CfgPath;

/// `TorClientConfig` is `Eq`, so every case below can check that what it set
/// through a sub-builder actually reached the built configuration: a config
/// that carries the setting must differ from `TorClientConfig::default()`, two
/// builders given the same setting must build equal configs, and two given
/// different settings must not. Where the value is a string the harness chose
/// (a path, a net-param name), the `Debug` rendering is also checked for that
/// value's *content*, not merely for the field's name.
#[test]
fn storagecfg__builder_from_directories() {
    let cfg = TorClientConfigBuilder::from_directories("/state", "/cache")
        .build()
        .unwrap();

    // Documented as exactly "a defaulted builder with `storage().state_dir(..)`
    // and `.cache_dir(..)` set from `CfgPath::new_literal`, everything else left
    // at its default", so the hand-built equivalent must compare equal.
    let mut hand = TorClientConfig::builder();
    hand.storage()
        .state_dir(CfgPath::new_literal("/state"))
        .cache_dir(CfgPath::new_literal("/cache"));
    assert_eq!(cfg, hand.build().unwrap());

    // Both directories really reached the configuration, and they are not
    // interchangeable: the first argument is the state dir.
    assert_ne!(cfg, TorClientConfig::default());
    let s = format!("{:?}", cfg);
    assert!(s.contains("/state") && s.contains("/cache"), "{s}");
    let swapped = TorClientConfigBuilder::from_directories("/cache", "/state")
        .build()
        .unwrap();
    assert_ne!(cfg, swapped, "the two arguments are not interchangeable");
}

#[test]
fn storagecfg__storage_sub_builder() {
    let mut b = TorClientConfig::builder();
    b.storage().cache_dir(CfgPath::new_literal("/tmp/arti-cache-7f31"));
    let cfg = b.build().unwrap();
    let s = format!("{:?}", cfg);

    // The literal path is the only readback `StorageConfig` offers, and it is
    // the value that was set, not just the field's name.
    assert!(s.contains("/tmp/arti-cache-7f31"), "{s}");
    assert_ne!(cfg, TorClientConfig::default());
    // Setting `cache_dir` left `state_dir` at its `${ARTI_LOCAL_DATA}` default.
    assert!(s.contains("ARTI_LOCAL_DATA"), "{s}");
    let mut b2 = TorClientConfig::builder();
    b2.storage()
        .cache_dir(CfgPath::new_literal("/tmp/arti-cache-7f31"));
    assert_eq!(cfg, b2.build().unwrap());
}

#[test]
fn storagecfg__download_tolerance_sub_builder() {
    use std::time::Duration;
    let mut b = TorClientConfig::builder();
    b.download_tolerance()
        .post_valid_tolerance(Duration::from_secs(1000));
    let cfg = b.build().unwrap();

    assert_ne!(
        cfg,
        TorClientConfig::default(),
        "the tolerance never reached the built configuration"
    );
    let mut b2 = TorClientConfig::builder();
    b2.download_tolerance()
        .post_valid_tolerance(Duration::from_secs(1000));
    assert_eq!(cfg, b2.build().unwrap());
    let mut b3 = TorClientConfig::builder();
    b3.download_tolerance()
        .post_valid_tolerance(Duration::from_secs(1001));
    assert_ne!(
        cfg,
        b3.build().unwrap(),
        "two different tolerances must not build equal configurations"
    );
    // The field holds the `Duration` it was given, so the interval itself shows
    // up in the rendering.
    let s = format!("{:?}", cfg);
    assert!(s.contains("1000s"), "{s}");
}

#[test]
fn storagecfg__tor_network_sub_builder() {
    let mut b = TorClientConfig::builder();
    // The accessor takes no argument and hands back the nested builder itself;
    // `config::dir` re-exports the identical `tor_dirmgr` type, so this binding
    // is the type pin.
    let nested: &mut NetworkConfigBuilder = b.tor_network();
    let _ = nested;
    let cfg = b.build().unwrap();

    // Reaching a sub-builder is not itself a mutation: with nothing set through
    // it the configuration is still the default one ...
    assert_eq!(
        cfg,
        TorClientConfig::default(),
        "reaching .tor_network() must not change the configuration"
    );
    // ... and the field it reaches holds the compiled-in network defaults,
    // authority and fallback lists included.
    let net = NetworkConfig::builder().build().unwrap();
    assert!(
        format!("{:?}", cfg).contains(&format!("{:?}", net)),
        "the built config does not carry the compiled-in NetworkConfig defaults"
    );
}

#[test]
fn storagecfg__override_net_params() {
    let mut b = TorClientConfig::builder();
    // Here the "sub-builder" is the map itself, converted into a
    // `NetParams<i32>` at `build()`.
    b.override_net_params().insert("circwindow".to_string(), 999);
    let cfg = b.build().unwrap();

    assert_ne!(
        cfg,
        TorClientConfig::default(),
        "the override never reached the built configuration"
    );
    let s = format!("{:?}", cfg);
    assert!(s.contains("circwindow"), "{s}");
    assert!(s.contains("999"), "{s}");
    let mut b2 = TorClientConfig::builder();
    b2.override_net_params().insert("circwindow".to_string(), 1000);
    assert_ne!(
        cfg,
        b2.build().unwrap(),
        "the map carries the value, not just the key"
    );
}

#[test]
fn storagecfg__download_schedule_sub_builder() {
    let mut b = TorClientConfig::builder();
    // As with `.tor_network()`, the accessor returns the nested builder itself,
    // so its type is the thing to pin: a plumbing that went through a different
    // type would not compile here.
    let nested: &mut DownloadScheduleConfigBuilder = b.download_schedule();
    let _ = nested;
    let cfg = b.build().unwrap();

    assert_eq!(
        cfg,
        TorClientConfig::default(),
        "reaching .download_schedule() must not change the configuration"
    );
    let sched = DownloadScheduleConfig::default();
    assert!(
        format!("{:?}", cfg).contains(&format!("{:?}", sched)),
        "the built config does not carry the DownloadScheduleConfig defaults"
    );
}

#[test]
fn storagecfg__path_rules_sub_builder() {
    let mut b = TorClientConfig::builder();
    let nested: &mut PathConfigBuilder = b.path_rules();
    nested.ipv4_subnet_family_prefix(24);
    let cfg = b.build().unwrap();

    // 24 is not the builder default (16), so the setting is visible as a
    // difference from the default configuration.
    assert_ne!(
        cfg,
        TorClientConfig::default(),
        "the subnet-family prefix never reached the built configuration"
    );
    let mut b2 = TorClientConfig::builder();
    b2.path_rules().ipv4_subnet_family_prefix(24);
    assert_eq!(cfg, b2.build().unwrap());
    let mut b3 = TorClientConfig::builder();
    b3.path_rules().ipv4_subnet_family_prefix(25);
    assert_ne!(
        cfg,
        b3.build().unwrap(),
        "two different prefixes must not build equal configurations"
    );
}

#[test]
fn storagecfg__address_filter_sub_builder() {
    let mut b = TorClientConfig::builder();
    let nested: &mut ClientAddrConfigBuilder = b.address_filter();
    nested.allow_local_addrs(true);
    let cfg = b.build().unwrap();

    // The option is off by default, so turning it on must change the config ...
    assert_ne!(
        cfg,
        TorClientConfig::default(),
        "allow_local_addrs never reached the built configuration"
    );
    // ... and setting it back to its default value must not.
    let mut b2 = TorClientConfig::builder();
    b2.address_filter().allow_local_addrs(false);
    assert_eq!(b2.build().unwrap(), TorClientConfig::default());
    assert_ne!(cfg, b2.build().unwrap());
}

/// The deployment escape hatch: filesystem permission checking is disabled by
/// the mere PRESENCE of `ARTI_FS_DISABLE_PERMISSION_CHECKS` in the
/// environment. The variable's value is never parsed, so `"0"` and the empty
/// string disable the checks exactly as `"1"` does; only an unset variable
/// leaves them on.
#[test]
fn storagecfg__permission_checks_disabled_via_env() {
    use arti_client::config::fs_permissions_checks_disabled_via_env as disabled;
    use crate::common::with_env_var;

    const V: &str = "ARTI_FS_DISABLE_PERMISSION_CHECKS";

    assert!(!with_env_var(V, None, disabled), "unset must leave checks on");
    assert!(with_env_var(V, Some("1"), disabled), "set must turn checks off");
    assert!(
        with_env_var(V, Some("0"), disabled),
        "the value is not parsed: \"0\" still turns checks off"
    );
    assert!(
        with_env_var(V, Some(""), disabled),
        "the value is not parsed: the empty string still turns checks off"
    );

    // The helper restores the previous setting, so the process is left as we
    // found it and the other cases in this group are unaffected.
    assert!(!with_env_var(V, None, disabled));
}
