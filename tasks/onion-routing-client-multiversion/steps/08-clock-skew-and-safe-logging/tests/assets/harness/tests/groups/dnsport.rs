//! Group `dnsport` (inherited from step 7): the DNS listener is configurable
//! and off unless a port is named, and it still works on the cumulative tree.
//!
//! Traces to instruction.md section 1: "Preserve user-supplied isolation and
//! the DNS port from step 7."  Interface: `arti::cfg::{ProxyConfig,
//! ProxyConfigBuilder}` and the configuration keys `dns_port`/`socks_port`.

use arti::ProxyConfig;

/// Parse a `ProxyConfig` from a JSON fragment.
fn parse(text: &str) -> ProxyConfig {
    serde_json::from_str(text).expect("the fragment should deserialize")
}

#[test]
fn dnsport__is_disabled_unless_a_port_is_named() {
    let built = ProxyConfig::builder().build().expect("builder should build");
    assert_eq!(ProxyConfig::default(), built);

    let with_dns = ProxyConfig::builder()
        .dns_port(Some(53))
        .build()
        .expect("builder should build");
    assert_ne!(ProxyConfig::default(), with_dns);
}

#[test]
fn dnsport__the_config_key_is_dns_port() {
    let with_dns = ProxyConfig::builder()
        .dns_port(Some(53))
        .build()
        .expect("builder should build");
    assert_eq!(parse(r#"{"dns_port": 53}"#), with_dns);

    let other_port = ProxyConfig::builder()
        .dns_port(Some(5353))
        .build()
        .expect("builder should build");
    assert_ne!(parse(r#"{"dns_port": 53}"#), other_port);
}

#[test]
fn dnsport__an_unknown_key_is_rejected() {
    assert!(serde_json::from_str::<ProxyConfig>(r#"{"dnsport": 53}"#).is_err());
    assert!(serde_json::from_str::<ProxyConfig>(r#"{"dns": 53}"#).is_err());
    // ... but the two documented keys may be given together.
    let both = parse(r#"{"dns_port": 53, "socks_port": 1080}"#);
    let expected = ProxyConfig::builder()
        .dns_port(Some(53))
        .socks_port(Some(1080))
        .build()
        .expect("builder should build");
    assert_eq!(both, expected);
}

#[test]
fn dnsport__the_socks_port_keeps_its_own_default() {
    let default_socks = ProxyConfig::builder()
        .socks_port(Some(9150))
        .build()
        .expect("builder should build");
    assert_eq!(ProxyConfig::default(), default_socks);
    assert_eq!(parse("{}"), ProxyConfig::default());
    assert_ne!(parse(r#"{"socks_port": 1080}"#), ProxyConfig::default());
}

#[test]
fn dnsport__a_null_port_means_disabled() {
    assert_eq!(parse(r#"{"dns_port": null}"#), ProxyConfig::default());

    let nothing_listening = ProxyConfig::builder()
        .dns_port(None)
        .socks_port(None)
        .build()
        .expect("builder should build");
    assert_eq!(
        parse(r#"{"dns_port": null, "socks_port": null}"#),
        nothing_listening
    );
    assert_ne!(nothing_listening, ProxyConfig::default());
}
