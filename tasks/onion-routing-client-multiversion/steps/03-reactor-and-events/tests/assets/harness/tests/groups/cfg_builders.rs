//! Group `cfg_builders` - the builder half of the configuration system.
//!
//! instruction.md section 1: "a rewritten configuration system built around
//! builders".
//!
//! Graded items: `arti_config::ArtiConfig::builder`,
//! `arti_config::ArtiConfigBuilder` and its per-section accessors,
//! `ArtiConfigBuilder::build`, `From<ArtiConfig> for ArtiConfigBuilder`,
//! `arti_config::{LoggingConfig, ProxyConfig}::builder`.


use arti_config::{ArtiConfig, ArtiConfigBuilder, LoggingConfig, ProxyConfig};
use tor_config::CfgPath;

#[test]
fn cfg_builders__empty_builder_reproduces_the_defaults() {
    let built = ArtiConfig::builder()
        .build()
        .expect("an untouched ArtiConfigBuilder must build");
    assert_eq!(built, ArtiConfig::default());
}

#[test]
fn cfg_builders__proxy_section_is_settable() {
    let mut bld = ArtiConfig::builder();
    bld.proxy().socks_port(Some(9999));
    let built = bld.build().expect("build failed");
    assert_eq!(built.proxy().socks_port(), Some(9999));
    assert_ne!(built, ArtiConfig::default());

    // The same section builder is reachable standalone.
    let standalone = ProxyConfig::builder()
        .socks_port(Some(9999))
        .build()
        .expect("ProxyConfigBuilder::build failed");
    assert_eq!(&standalone, built.proxy());
}

#[test]
fn cfg_builders__logging_section_is_settable() {
    let mut bld = ArtiConfig::builder();
    bld.logging().journald(true).trace_filter("warn".to_owned());
    let built = bld.build().expect("build failed");
    assert_eq!(built.logging().journald, true);
    assert_eq!(built.logging().trace_filter, "warn");

    let standalone = LoggingConfig::builder()
        .journald(true)
        .trace_filter("warn".to_owned())
        .build()
        .expect("LoggingConfigBuilder::build failed");
    assert_eq!(&standalone, built.logging());
}

#[test]
fn cfg_builders__round_trip_through_the_builder_is_the_identity() {
    let mut bld = ArtiConfig::builder();
    bld.proxy().socks_port(Some(1080));
    bld.logging().journald(true);
    bld.path_rules().ipv4_subnet_family_prefix(20);
    bld.address_filter().allow_local_addrs(true);
    bld.override_net_params().insert("circwindow".to_owned(), 1000);
    let first = bld.build().expect("build failed");

    let rebuilt = ArtiConfigBuilder::from(first.clone())
        .build()
        .expect("rebuild failed");
    assert_eq!(first, rebuilt);
    assert_ne!(first, ArtiConfig::default());
}

#[test]
fn cfg_builders__storage_section_is_settable() {
    let mut bld = ArtiConfig::builder();
    bld.storage()
        .cache_dir(CfgPath::new("/var/tmp/cache".to_owned()))
        .state_dir(CfgPath::new("/var/tmp/state".to_owned()));
    let built = bld.build().expect("build failed");
    assert_ne!(built, ArtiConfig::default());

    // The storage section is carried into the client configuration.
    let client = built.tor_client_config().expect("tor_client_config failed");
    let mut expect = arti_client::TorClientConfig::builder();
    expect
        .storage()
        .cache_dir(CfgPath::new("/var/tmp/cache".to_owned()))
        .state_dir(CfgPath::new("/var/tmp/state".to_owned()));
    assert_eq!(client, expect.build().expect("client build failed"));
}

#[test]
fn cfg_builders__circuit_timing_section_reaches_the_client_config() {
    let sec = std::time::Duration::from_secs(1);
    let mut bld = ArtiConfig::builder();
    bld.circuit_timing()
        .max_dirtiness(90 * sec)
        .request_timeout(10 * sec)
        .request_max_retries(22);
    let built = bld.build().expect("build failed");
    let client = built.tor_client_config().expect("tor_client_config failed");

    let mut expect = arti_client::TorClientConfig::builder();
    expect
        .circuit_timing()
        .max_dirtiness(90 * sec)
        .request_timeout(10 * sec)
        .request_max_retries(22);
    assert_eq!(client, expect.build().expect("client build failed"));
    assert_ne!(client, arti_client::TorClientConfig::default());
}
