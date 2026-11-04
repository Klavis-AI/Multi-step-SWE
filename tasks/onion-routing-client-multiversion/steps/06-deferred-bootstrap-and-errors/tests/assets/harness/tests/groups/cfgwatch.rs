//! Group `cfgwatch`: configuration-file watching.
//!
//! instruction.md: "Configuration-file watching". Whether arti re-reads its
//! configuration when the files change is itself a configuration setting,
//! `application.watch_configuration`, reachable from a file, from a
//! command-line override, and from the builder. It is off unless asked for.

use std::convert::TryFrom;

use arti_config::{ApplicationConfig, ArtiConfig, ConfigurationSources};

use crate::common::write_file;

/// Build an `ArtiConfig` from a set of TOML fragments and command-line options.
fn config_from(files: &[(&str, &str)], opts: &[&str]) -> ArtiConfig {
    let dir = tempfile::tempdir().expect("could not create a config tempdir");
    let mut sources = ConfigurationSources::new();
    for (name, text) in files {
        sources.push_file(write_file(&dir, name, text));
    }
    for opt in opts {
        sources.push_option(*opt);
    }
    let cfg = sources.load().expect("configuration should load");
    ArtiConfig::try_from(cfg).expect("configuration should deserialize")
}

#[test]
fn cfgwatch__off_by_default() {
    let cfg = config_from(&[], &[]);
    assert!(!cfg.application().watch_configuration());
}

#[test]
fn cfgwatch__enabled_by_a_configuration_file() {
    let cfg = config_from(
        &[("watch.toml", "[application]\nwatch_configuration = true\n")],
        &[],
    );
    assert!(cfg.application().watch_configuration());
}

#[test]
fn cfgwatch__file_can_also_turn_it_back_off() {
    let cfg = config_from(
        &[
            ("on.toml", "[application]\nwatch_configuration = true\n"),
            ("off.toml", "[application]\nwatch_configuration = false\n"),
        ],
        &[],
    );
    assert!(!cfg.application().watch_configuration());
}

#[test]
fn cfgwatch__enabled_by_a_command_line_option() {
    let cfg = config_from(&[], &["application.watch_configuration=true"]);
    assert!(cfg.application().watch_configuration());
    // ... and a command-line option beats a file that says otherwise.
    let cfg = config_from(
        &[("off.toml", "[application]\nwatch_configuration = false\n")],
        &["application.watch_configuration=true"],
    );
    assert!(cfg.application().watch_configuration());
}

#[test]
fn cfgwatch__builder_defaults_to_not_watching() {
    let cfg = ArtiConfig::builder()
        .build()
        .expect("an untouched builder should build");
    assert!(!cfg.application().watch_configuration());
    assert!(!ApplicationConfig::default().watch_configuration());
}

#[test]
fn cfgwatch__builder_sets_the_flag() {
    let mut bld = ArtiConfig::builder();
    bld.application().watch_configuration(true);
    let cfg = bld.build().expect("builder should build");
    assert!(cfg.application().watch_configuration());

    let mut bld = ArtiConfig::builder();
    bld.application().watch_configuration(false);
    let cfg2 = bld.build().expect("builder should build");
    assert!(!cfg2.application().watch_configuration());
    assert_ne!(cfg.application(), cfg2.application());
}

#[test]
fn cfgwatch__application_section_rejects_unknown_keys() {
    // The setting is spelled exactly `watch_configuration`; a near miss must
    // be reported rather than silently ignored, or a user who asked for
    // watching would never find out that they did not get it.
    let dir = tempfile::tempdir().expect("could not create a config tempdir");
    let mut sources = ConfigurationSources::new();
    sources.push_file(write_file(
        &dir,
        "typo.toml",
        "[application]\nwatch_config = true\n",
    ));
    let cfg = sources.load().expect("the file itself is valid TOML");
    assert!(
        ArtiConfig::try_from(cfg).is_err(),
        "an unknown key in [application] must be rejected"
    );
}
