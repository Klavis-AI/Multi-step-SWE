//! Group `dnsportcfg`: the `dns_port` proxy setting that turns the DNS
//! resolver on.  The SOCKS port keeps its old default; the DNS port is opt-in,
//! so its default has to be "off".
//!
//! Graded items: `arti::cfg::{ProxyConfig, ProxyConfigBuilder, ArtiConfig,
//! ArtiConfigBuilder}`.  `ProxyConfig` implements `PartialEq`, so every case
//! compares whole configuration values rather than formatted text.

use arti::cfg::{ArtiConfig, ArtiConfigBuilder, ProxyConfig, ProxyConfigBuilder};

/// Build a `ProxyConfig` with both ports set explicitly.
fn proxy(socks: Option<u16>, dns: Option<u16>) -> ProxyConfig {
    let mut b = ProxyConfig::builder();
    b.socks_port(socks);
    b.dns_port(dns);
    b.build().expect("proxy configuration builds")
}

/// Deserialize a TOML document into an `ArtiConfigBuilder` and build just its
/// proxy section.
fn proxy_from_toml(toml: &str) -> ProxyConfig {
    let cfg = config::Config::builder()
        .add_source(config::File::from_str(toml, config::FileFormat::Toml))
        .build()
        .expect("the test document is valid TOML");
    let mut builder: ArtiConfigBuilder = cfg
        .try_deserialize()
        .expect("the document deserializes into an ArtiConfigBuilder");
    builder
        .proxy()
        .build()
        .expect("the proxy section builds")
}

#[test]
fn dnsportcfg__the_dns_port_is_off_by_default() {
    // An unconfigured proxy section leaves the DNS listener disabled while the
    // SOCKS listener keeps its usual port.
    assert_eq!(ProxyConfig::default(), proxy(Some(9150), None));
    assert_eq!(
        ProxyConfig::builder()
            .build()
            .expect("an empty proxy builder builds"),
        proxy(Some(9150), None)
    );
    // A configuration that enables DNS is a different configuration.
    assert_ne!(ProxyConfig::default(), proxy(Some(9150), Some(53)));
}

#[test]
fn dnsportcfg__a_dns_port_can_be_set_without_disturbing_the_socks_port() {
    let mut b = ProxyConfig::builder();
    b.dns_port(Some(5353));
    let cfg = b.build().expect("proxy configuration builds");
    assert_eq!(cfg, proxy(Some(9150), Some(5353)));
    assert_ne!(cfg, proxy(Some(9150), Some(53)));
    assert_ne!(cfg, ProxyConfig::default());
}

#[test]
fn dnsportcfg__either_listener_can_be_turned_off_independently() {
    // DNS on, SOCKS off.
    assert_ne!(proxy(None, Some(53)), proxy(Some(9150), Some(53)));
    // SOCKS on, DNS off: this is the default shape.
    assert_eq!(proxy(Some(9150), None), ProxyConfig::default());
    // Both off is distinct from both on.
    assert_ne!(proxy(None, None), proxy(Some(9150), Some(53)));
    // The two ports are independent settings.
    assert_ne!(proxy(None, Some(53)), proxy(Some(53), None));
}

#[test]
fn dnsportcfg__a_toml_proxy_section_configures_the_dns_port() {
    let cfg = proxy_from_toml("[proxy]\nsocks_port = 9150\ndns_port = 5353\n");
    assert_eq!(cfg, proxy(Some(9150), Some(5353)));

    // Omitting dns_port falls back to "off" rather than to some port number.
    let cfg = proxy_from_toml("[proxy]\nsocks_port = 9150\n");
    assert_eq!(cfg, proxy(Some(9150), None));

    // Omitting the whole section gives the defaults.
    let cfg = proxy_from_toml("[application]\nwatch_configuration = false\n");
    assert_eq!(cfg, ProxyConfig::default());
}

#[test]
fn dnsportcfg__the_builder_default_matches_the_deserialized_default() {
    let deserialized = proxy_from_toml("[proxy]\n");
    let built = ProxyConfigBuilder::default()
        .build()
        .expect("a default proxy builder builds");
    assert_eq!(deserialized, built);
    assert_eq!(built, ProxyConfig::default());

    // Setting a port through either route gives the same value.
    let deserialized = proxy_from_toml("[proxy]\ndns_port = 1053\n");
    let mut b = ProxyConfigBuilder::default();
    b.dns_port(Some(1053));
    assert_eq!(deserialized, b.build().expect("proxy builds"));
}

#[test]
fn dnsportcfg__the_application_config_exposes_its_proxy_section() {
    let cfg = ArtiConfig::default();
    assert_eq!(cfg.proxy(), &ProxyConfig::default());
    assert_eq!(cfg.proxy().clone(), proxy(Some(9150), None));

    // The builder threads a DNS port all the way through to the built config.
    let mut builder = ArtiConfig::builder();
    builder.proxy().dns_port(Some(9153));
    let built = builder.build().expect("the application config builds");
    assert_eq!(built.proxy(), &proxy(Some(9150), Some(9153)));
    assert_ne!(built, ArtiConfig::default());
    // Everything else is untouched.
    assert_eq!(built.application(), ArtiConfig::default().application());
}
