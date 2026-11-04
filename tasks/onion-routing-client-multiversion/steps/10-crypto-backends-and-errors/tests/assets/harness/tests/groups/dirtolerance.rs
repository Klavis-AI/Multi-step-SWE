//! Directory tolerance configuration, and the `download_tolerance` ->
//! `directory_tolerance` rename.

#![allow(non_snake_case)]

use std::time::Duration;

use arti_client::config::{dir::DirTolerance, TorClientConfig};
use tor_config::config_crate as cfg;

const DAY: u64 = 24 * 60 * 60;

/// Turn a TOML fragment into the configuration tree the resolver consumes.
fn tree(toml: &str) -> cfg::Config {
    cfg::Config::builder()
        .add_source(cfg::File::from_str(toml, cfg::FileFormat::Toml))
        .build()
        .expect("the fragment is valid TOML")
}

/// Resolve a client configuration, returning the keys nothing claimed.
fn resolve(toml: &str) -> (TorClientConfig, Vec<String>) {
    let (config, unrecognized) =
        tor_config::resolve_return_unrecognized::<TorClientConfig>(tree(toml))
            .expect("the fragment is well typed");
    (
        config,
        unrecognized.iter().map(|k| k.to_string()).collect(),
    )
}

#[test]
fn dirtolerance__defaults_to_one_day_before_and_three_days_after() {
    let explicit = DirTolerance::builder()
        .pre_valid_tolerance(Duration::from_secs(DAY))
        .post_valid_tolerance(Duration::from_secs(3 * DAY))
        .build()
        .expect("an explicit tolerance builds");
    assert_eq!(
        DirTolerance::default(),
        explicit,
        "the defaults are 1 day before and 3 days after validity"
    );
}

#[test]
fn dirtolerance__a_different_tolerance_is_not_the_default() {
    let other = DirTolerance::builder()
        .post_valid_tolerance(Duration::from_secs(7 * DAY))
        .build()
        .expect("builds");
    assert_ne!(DirTolerance::default(), other);
    let same = DirTolerance::builder()
        .post_valid_tolerance(Duration::from_secs(3 * DAY))
        .build()
        .expect("builds");
    assert_eq!(
        DirTolerance::default(),
        same,
        "restating a default value must not change the result"
    );
}

#[test]
fn dirtolerance__the_client_config_field_is_called_directory_tolerance() {
    let mut b = TorClientConfig::builder();
    b.directory_tolerance()
        .post_valid_tolerance(Duration::from_secs(10 * DAY));
    let tuned = b.build().expect("a client config builds");
    assert_ne!(
        tuned,
        TorClientConfig::default(),
        "setting the tolerance must reach the built configuration"
    );
}

#[test]
fn dirtolerance__setting_the_default_value_reproduces_the_default_config() {
    let mut b = TorClientConfig::builder();
    b.directory_tolerance()
        .pre_valid_tolerance(Duration::from_secs(DAY))
        .post_valid_tolerance(Duration::from_secs(3 * DAY));
    assert_eq!(b.build().expect("builds"), TorClientConfig::default());
}

#[test]
fn dirtolerance__directory_tolerance_is_a_recognized_config_key() {
    let (config, unrecognized) = resolve(
        "[directory_tolerance]\npost_valid_tolerance = \"10 days\"\n",
    );
    assert!(
        unrecognized.is_empty(),
        "directory_tolerance must be consumed by the client config; got {:?}",
        unrecognized
    );
    assert_ne!(config, TorClientConfig::default());
}

#[test]
fn dirtolerance__download_tolerance_is_no_longer_recognized() {
    let (config, unrecognized) =
        resolve("[download_tolerance]\npost_valid_tolerance = \"10 days\"\n");
    assert!(
        unrecognized
            .iter()
            .any(|k| k.starts_with("download_tolerance")),
        "the old key must be reported as unrecognized; got {:?}",
        unrecognized
    );
    assert_eq!(
        config,
        TorClientConfig::default(),
        "an unrecognized key must not silently configure anything"
    );
}

#[test]
fn dirtolerance__download_schedule_is_still_a_separate_recognized_key() {
    let (_config, unrecognized) =
        resolve("[download_schedule]\nretry_bootstrap = { attempts = 5 }\n");
    assert!(
        unrecognized.is_empty(),
        "download_schedule was not renamed; got {:?}",
        unrecognized
    );
}

#[test]
fn dirtolerance__an_empty_configuration_resolves_to_the_default() {
    let (config, unrecognized) = resolve("");
    assert!(unrecognized.is_empty());
    assert_eq!(config, TorClientConfig::default());
}
