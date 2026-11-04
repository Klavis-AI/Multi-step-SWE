//! Group `cfg_arti_options` - the `ArtiConfig` structure the defaults parse into.
//!
//! instruction.md section 1: "a rewritten configuration system built around
//! builders".
//!
//! Graded items: `arti_config::ArtiConfig` and its accessors
//! (`logging`, `proxy`, `tor_client_config`), `arti_config::LoggingConfig`,
//! `arti_config::ProxyConfig::socks_port`.

// The step-03 helpers live apart from `common` so that no gate group is
// forced to compile them; see the note at the top of common/current.rs.
use crate::current as common;

use arti_config::ArtiConfig;
use arti_client::TorClientConfig;

#[test]
fn cfg_arti_options__defaults_parse_into_arti_config() {
    let cfg: ArtiConfig = common::load_defaults()
        .try_into()
        .expect("built-in defaults do not deserialize into ArtiConfig");
    assert_eq!(cfg.proxy().socks_port(), Some(9150));
    assert_eq!(cfg.logging().trace_filter, "debug");
    assert_eq!(cfg.logging().journald, false);
}

#[test]
fn cfg_arti_options__default_impl_matches_the_defaults_file() {
    let parsed: ArtiConfig = common::load_defaults()
        .try_into()
        .expect("built-in defaults do not deserialize into ArtiConfig");
    assert_eq!(parsed, ArtiConfig::default());
}

#[test]
fn cfg_arti_options__unknown_section_is_rejected() {
    let cfg = arti_config::load(
        common::NO_CONFIG_FILES,
        vec!["wombat.quokka=3".to_string()],
    )
    .expect("load itself should accept an unknown key");
    let parsed: Result<ArtiConfig, _> = cfg.try_into();
    assert!(
        parsed.is_err(),
        "an unrecognized configuration section must not deserialize silently"
    );
}

#[test]
fn cfg_arti_options__socks_port_override_is_visible() {
    let cfg: ArtiConfig = common::load_with_opts(&["proxy.socks_port=9999"])
        .try_into()
        .expect("overridden config does not deserialize");
    assert_eq!(cfg.proxy().socks_port(), Some(9999));
    // Untouched sections keep their defaults.
    assert_eq!(cfg.logging().trace_filter, "debug");
}

#[test]
fn cfg_arti_options__logging_override_is_visible() {
    let cfg: ArtiConfig = common::load_with_opts(&["logging.journald=true", "logging.trace_filter=warn"])
        .try_into()
        .expect("overridden config does not deserialize");
    assert_eq!(cfg.logging().journald, true);
    assert_eq!(cfg.logging().trace_filter, "warn");
    assert_ne!(cfg, ArtiConfig::default());
}

#[test]
fn cfg_arti_options__default_client_config_is_derived_from_defaults() {
    let parsed: ArtiConfig = common::load_defaults()
        .try_into()
        .expect("built-in defaults do not deserialize into ArtiConfig");
    let client = parsed
        .tor_client_config()
        .expect("tor_client_config() failed on the built-in defaults");
    assert_eq!(client, TorClientConfig::default());
}
