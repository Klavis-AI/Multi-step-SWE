//! Group `client_cfg` - the `arti-client` half of the rewritten configuration.
//!
//! instruction.md section 1: "a rewritten configuration system built around
//! builders". `ArtiConfig` is the file format; `TorClientConfig` is what an
//! embedder builds directly, and every section of it is a builder too.
//!
//! Graded items: `arti_client::TorClientConfig`,
//! `arti_client::config::TorClientConfigBuilder` (including `from_directories`),
//! `arti_client::config::{ClientAddrConfig, StorageConfig}`, and the `circ` and
//! `dir` re-export modules.

use arti_client::config::{circ, dir, ClientAddrConfig, TorClientConfigBuilder};
use arti_client::TorClientConfig;
use tor_config::CfgPath;

#[test]
fn client_cfg__default_round_trips_through_its_builder() {
    let dflt = TorClientConfig::default();
    assert_eq!(
        dflt,
        TorClientConfig::builder().build().expect("empty builder must build")
    );

    let rebuilt = TorClientConfigBuilder::from(dflt.clone())
        .build()
        .expect("rebuild from a config must succeed");
    assert_eq!(dflt, rebuilt);
}

#[test]
fn client_cfg__from_directories_sets_literal_paths() {
    let built = TorClientConfigBuilder::from_directories("/var/tmp/state", "/var/tmp/cache")
        .build()
        .expect("from_directories must build");
    assert_ne!(built, TorClientConfig::default());

    // `from_directories` takes filesystem paths, so they are stored as literal
    // paths - not as strings that would be scanned for `${...}` variables.
    let mut expect = TorClientConfig::builder();
    expect
        .storage()
        .cache_dir(CfgPath::from_path("/var/tmp/cache"))
        .state_dir(CfgPath::from_path("/var/tmp/state"));
    assert_eq!(built, expect.build().expect("explicit builder must build"));

    // ... and the shell-style spelling is a different configuration.
    let mut shellish = TorClientConfig::builder();
    shellish
        .storage()
        .cache_dir(CfgPath::new("/var/tmp/cache".to_owned()))
        .state_dir(CfgPath::new("/var/tmp/state".to_owned()));
    assert_ne!(built, shellish.build().expect("builder must build"));
}

#[test]
fn client_cfg__address_filter_defaults_to_refusing_local_addresses() {
    let dflt = ClientAddrConfig::default();
    assert_eq!(
        dflt,
        ClientAddrConfig::builder()
            .build()
            .expect("ClientAddrConfigBuilder::build failed")
    );
    let permissive = ClientAddrConfig::builder()
        .allow_local_addrs(true)
        .build()
        .expect("ClientAddrConfigBuilder::build failed");
    assert_ne!(dflt, permissive);

    let mut bld = TorClientConfig::builder();
    bld.address_filter().allow_local_addrs(true);
    assert_ne!(
        bld.build().expect("build failed"),
        TorClientConfig::default()
    );
}

#[test]
fn client_cfg__circ_module_reexports_path_and_timing_config() {
    let timing = circ::CircuitTiming::default();
    assert_eq!(
        timing,
        circ::CircuitTimingBuilder::default()
            .max_dirtiness(std::time::Duration::from_secs(600))
            .request_timeout(std::time::Duration::from_secs(60))
            .request_max_retries(32)
            .request_loyalty(std::time::Duration::from_millis(50))
            .build()
            .expect("CircuitTimingBuilder::build failed")
    );

    let paths = circ::PathConfig::default();
    assert_eq!(
        paths,
        circ::PathConfigBuilder::default()
            .ipv4_subnet_family_prefix(16)
            .ipv6_subnet_family_prefix(32)
            .build()
            .expect("PathConfigBuilder::build failed")
    );
    assert_ne!(
        paths,
        circ::PathConfigBuilder::default()
            .ipv4_subnet_family_prefix(20)
            .build()
            .expect("PathConfigBuilder::build failed")
    );
}

#[test]
fn client_cfg__dir_module_reexports_download_schedules() {
    let sec = std::time::Duration::from_secs(1);
    let dflt = dir::DownloadScheduleConfig::default();
    assert_eq!(
        dflt,
        dir::DownloadScheduleConfigBuilder::default()
            .retry_bootstrap(dir::DownloadSchedule::new(128, sec, 1))
            .retry_consensus(dir::DownloadSchedule::new(3, sec, 1))
            .retry_certs(dir::DownloadSchedule::new(3, sec, 1))
            .retry_microdescs(dir::DownloadSchedule::new(3, sec, 4))
            .build()
            .expect("DownloadScheduleConfigBuilder::build failed")
    );

    assert_ne!(
        dflt,
        dir::DownloadScheduleConfigBuilder::default()
            .retry_bootstrap(dir::DownloadSchedule::new(10, sec, 1))
            .build()
            .expect("DownloadScheduleConfigBuilder::build failed")
    );
}

#[test]
fn client_cfg__network_builder_reports_inconsistent_sections() {
    // Overriding the authority list without also overriding the fallback list
    // is refused, and the refusal names both fields.
    let auth = dir::Authority::builder()
        .name("Fred")
        .v3ident([22; 20].into())
        .build()
        .expect("Authority::build failed");
    let err = dir::NetworkConfig::builder()
        .authorities(vec![auth])
        .build()
        .expect_err("authorities without fallbacks must be refused");
    let msg = err.to_string();
    assert!(
        msg.starts_with(r#"Fields ["authorities", "fallbacks"] are inconsistent: "#),
        "unexpected message {:?}",
        msg
    );
}
