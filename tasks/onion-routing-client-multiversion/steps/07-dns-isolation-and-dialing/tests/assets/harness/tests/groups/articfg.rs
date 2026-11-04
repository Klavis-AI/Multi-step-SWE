//! Group `articfg`: peripheral coverage of the `arti` binary crate's own
//! configuration surface - the sections that belong to the *process* rather
//! than to the Tor client it drives.  The `proxy` section's `dns_port` is the
//! headline change at this checkpoint and is graded by `dnsportcfg`; this
//! group covers the rest of the crate's exported config API, so the surface is
//! graded as a whole instead of at one field.
//!
//! Graded items: `arti::{ApplicationConfig, ApplicationConfigBuilder,
//! ArtiConfig, ArtiConfigBuilder, SystemConfig, SystemConfigBuilder}`, the
//! `ArtiConfig` section accessors, `ArtiConfig::tor_client_config`, and the
//! `TryFrom<config::Config>` impl that is how a real `arti.toml` becomes an
//! `ArtiConfig`.
//!
//! Every field on these structs is `pub(crate)`, so an out-of-crate test can
//! only reach them through the builders and the accessors.  That is the real
//! consumer's view, and it is deliberately the only view used here.

use arti::{ApplicationConfig, ArtiConfig, SystemConfig};

/// Parse a TOML fragment the way `arti` parses a configuration file.
fn from_toml(s: &str) -> Result<ArtiConfig, config::ConfigError> {
    let cfg = config::Config::builder()
        .add_source(config::File::from_str(s, config::FileFormat::Toml))
        .build()?;
    ArtiConfig::try_from(cfg)
}

#[test]
fn articfg__the_derived_default_and_the_empty_builder_agree() {
    // `ArtiConfig` gets its `Default` from `derive`, which defaults each
    // section independently; `build()` instead runs every section's builder.
    // Two construction paths that disagree would mean a config file that omits
    // a section behaves differently from one that has no sections at all, so
    // the agreement is worth pinning.
    let derived = ArtiConfig::default();
    let built = ArtiConfig::builder().build().expect("empty builder must build");
    assert_eq!(derived, built);

    // An empty config file is the same thing again, through serde this time.
    let parsed = from_toml("").expect("an empty config file is valid");
    assert_eq!(parsed, built);

    // The accessors hand back exactly the standalone defaults of each section.
    assert_eq!(built.application(), &ApplicationConfig::default());
    assert_eq!(built.proxy(), &arti::ProxyConfig::default());
}

#[test]
fn articfg__the_application_section_reaches_the_same_value_from_toml_and_builder() {
    // `watch_configuration` is off unless asked for.
    assert_eq!(
        ArtiConfig::default().application(),
        &ApplicationConfig::default()
    );

    let parsed = from_toml("[application]\nwatch_configuration = true\n")
        .expect("a known key in a known section must parse");

    let mut b = ArtiConfig::builder();
    b.application().watch_configuration(true);
    let built = b.build().expect("builder must build");

    // The file and the builder are two doors into one value.
    assert_eq!(parsed.application(), built.application());
    // ...and the value really did move off its default.
    assert_ne!(parsed.application(), &ApplicationConfig::default());
    // Setting one section leaves the others where they were.
    assert_eq!(parsed.proxy(), ArtiConfig::default().proxy());
}

#[test]
fn articfg__each_section_is_settable_without_disturbing_the_others() {
    // The builder exposes one inner builder per section.  A setter that wrote
    // through to the wrong section would still produce a config that "works",
    // so the property worth pinning is isolation: moving one section moves
    // exactly that section.
    let baseline = ArtiConfig::default();

    let mut ab = ArtiConfig::builder();
    ab.application().watch_configuration(true);
    let app_only = ab.build().expect("application-only build");

    let mut sb = ArtiConfig::builder();
    sb.system().max_files(4096_u64);
    let sys_only = sb.build().expect("system-only build");

    let mut pb = ArtiConfig::builder();
    pb.proxy().socks_port(Some(1080));
    let proxy_only = pb.build().expect("proxy-only build");

    // Each differs from the baseline...
    assert_ne!(app_only, baseline);
    assert_ne!(sys_only, baseline);
    assert_ne!(proxy_only, baseline);
    // ...and from each other: no two setters are aliases.
    assert_ne!(app_only, sys_only);
    assert_ne!(app_only, proxy_only);
    assert_ne!(sys_only, proxy_only);

    // Setting the application section leaves proxy where it was, and setting
    // the proxy section leaves application where it was.
    assert_eq!(app_only.proxy(), baseline.proxy());
    assert_eq!(proxy_only.application(), baseline.application());
    assert_eq!(sys_only.application(), baseline.application());
    assert_eq!(sys_only.proxy(), baseline.proxy());

    // A section set twice takes the last value rather than accumulating.
    let mut tb = ArtiConfig::builder();
    tb.application().watch_configuration(true);
    tb.application().watch_configuration(false);
    assert_eq!(tb.build().expect("rebuild").application(), baseline.application());
}

#[test]
fn articfg__the_system_section_is_arti_only_and_does_not_reach_the_tor_client_config() {
    // `max_files` is a process resource limit, not a Tor parameter.  It has a
    // default, it is settable, and it must not leak into the configuration
    // handed to `arti-client`.
    let mut b = ArtiConfig::builder();
    b.system().max_files(4096_u64);
    let tuned = b.build().expect("builder must build");

    let untouched = ArtiConfig::default();
    // The section moved...
    assert_ne!(tuned, untouched);
    // ...but the arti-only section is not part of what the Tor client is told.
    let tuned_tor = tuned.tor_client_config().expect("tor_client_config");
    let untouched_tor = untouched.tor_client_config().expect("tor_client_config");
    assert_eq!(format!("{:?}", tuned_tor), format!("{:?}", untouched_tor));

    // `SystemConfig` has a default of its own and the builder honours it.
    assert_eq!(
        SystemConfig::builder().build().expect("system builder"),
        SystemConfig::default()
    );
}
