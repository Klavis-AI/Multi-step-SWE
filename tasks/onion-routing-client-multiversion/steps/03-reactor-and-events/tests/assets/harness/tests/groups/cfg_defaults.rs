//! Group `cfg_defaults` - the revised built-in configuration file.
//!
//! instruction.md section 1: "a rewritten configuration system built around
//! builders"; Migration: "The configuration file format is revised, and
//! `${APP_*}` variables become `${ARTI_*}`".
//!
//! Graded item: `arti_config::load`, and the values of the built-in defaults it
//! merges in before any file or command line is considered.
//!
//! No file is read and no environment variable is consulted: `load` is called
//! with an empty file list, so every value asserted here comes from the
//! compiled-in defaults.

// The step-03 helpers live apart from `common` so that no gate group is
// forced to compile them; see the note at the top of common/current.rs.
use crate::current as common;

#[test]
fn cfg_defaults__socks_port_is_9150() {
    let cfg = common::load_defaults();
    assert_eq!(cfg.get_int("proxy.socks_port").expect("no proxy.socks_port"), 9150);
}

#[test]
fn cfg_defaults__logging_section() {
    let cfg = common::load_defaults();
    assert_eq!(
        cfg.get_str("logging.trace_filter").expect("no logging.trace_filter"),
        "debug"
    );
    assert_eq!(
        cfg.get_bool("logging.journald").expect("no logging.journald"),
        false
    );
}

#[test]
fn cfg_defaults__storage_uses_arti_variables() {
    // The migration: the storage directories are named with ARTI_ variables,
    // not the APP_ variables the previous configuration format used.
    let cfg = common::load_defaults();
    assert_eq!(
        cfg.get_str("storage.cache_dir").expect("no storage.cache_dir"),
        "${ARTI_CACHE}"
    );
    assert_eq!(
        cfg.get_str("storage.state_dir").expect("no storage.state_dir"),
        "${ARTI_LOCAL_DATA}"
    );
}

#[test]
fn cfg_defaults__bootstrap_retry_schedule() {
    let cfg = common::load_defaults();
    assert_eq!(
        cfg.get_int("download_schedule.retry_bootstrap.num_retries")
            .expect("no retry_bootstrap.num_retries"),
        128
    );
    assert_eq!(
        cfg.get_str("download_schedule.retry_bootstrap.initial_delay")
            .expect("no retry_bootstrap.initial_delay"),
        "1 sec"
    );
}

#[test]
fn cfg_defaults__document_retry_schedules() {
    let cfg = common::load_defaults();
    for key in [
        "download_schedule.retry_consensus",
        "download_schedule.retry_certs",
        "download_schedule.retry_microdescs",
    ] {
        assert_eq!(
            cfg.get_int(&format!("{}.num_retries", key))
                .unwrap_or_else(|_| panic!("no {}.num_retries", key)),
            3,
            "wrong num_retries for {}",
            key
        );
        assert_eq!(
            cfg.get_str(&format!("{}.initial_delay", key))
                .unwrap_or_else(|_| panic!("no {}.initial_delay", key)),
            "1 sec",
            "wrong initial_delay for {}",
            key
        );
    }
    assert_eq!(
        cfg.get_int("download_schedule.retry_microdescs.parallelism")
            .expect("no retry_microdescs.parallelism"),
        4
    );
}

#[test]
fn cfg_defaults__path_rules_subnet_prefixes() {
    let cfg = common::load_defaults();
    assert_eq!(
        cfg.get_int("path_rules.ipv4_subnet_family_prefix")
            .expect("no path_rules.ipv4_subnet_family_prefix"),
        16
    );
    assert_eq!(
        cfg.get_int("path_rules.ipv6_subnet_family_prefix")
            .expect("no path_rules.ipv6_subnet_family_prefix"),
        32
    );
}

#[test]
fn cfg_defaults__circuit_timing_section() {
    let cfg = common::load_defaults();
    assert_eq!(
        cfg.get_str("circuit_timing.max_dirtiness").expect("no max_dirtiness"),
        "10 minutes"
    );
    assert_eq!(
        cfg.get_str("circuit_timing.request_timeout").expect("no request_timeout"),
        "60 sec"
    );
    assert_eq!(
        cfg.get_int("circuit_timing.request_max_retries")
            .expect("no request_max_retries"),
        32
    );
    assert_eq!(
        cfg.get_str("circuit_timing.request_loyalty").expect("no request_loyalty"),
        "50 msec"
    );
}

#[test]
fn cfg_defaults__local_addresses_are_refused_by_default() {
    let cfg = common::load_defaults();
    assert_eq!(
        cfg.get_bool("address_filter.allow_local_addrs")
            .expect("no address_filter.allow_local_addrs"),
        false
    );
}

#[test]
fn cfg_defaults__default_config_file_is_arti_toml_under_arti_config() {
    use tor_config::CfgPath;

    // `default_config_file()` is the path `arti` reads when the user names no
    // file at all. It is the expansion of `${ARTI_CONFIG}/arti.toml`, and it
    // reports `None` exactly when that expansion fails - it does not fall back
    // to a hard-coded location and it does not panic.
    let via_cfgpath = CfgPath::new("${ARTI_CONFIG}/arti.toml".to_string()).path();
    match (arti_config::default_config_file(), via_cfgpath) {
        (Some(got), Ok(want)) => {
            assert_eq!(
                got, want,
                "default_config_file() must be the expansion of ${{ARTI_CONFIG}}/arti.toml"
            );
            assert_eq!(
                got.file_name().and_then(|s| s.to_str()),
                Some("arti.toml"),
                "the default configuration file is named arti.toml, got {:?}",
                got
            );
            // The migration: the file lives under the ARTI_ directory, so the
            // old APP_ spelling must not appear anywhere in the result.
            assert!(
                !got.to_string_lossy().contains("${"),
                "the returned path must already be expanded, got {:?}",
                got
            );
        }
        (None, Err(_)) => {
            // The base directories could not be located on this host; both
            // answers agree that there is no default file.
        }
        (got, want) => panic!(
            "default_config_file() = {:?} disagrees with ${{ARTI_CONFIG}}/arti.toml = {:?}",
            got, want
        ),
    }
}
