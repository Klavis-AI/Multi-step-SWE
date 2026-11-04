//! Group `cfgsources`: where arti's configuration comes from.
//!
//! instruction.md: "Configuration-file watching" - watching presupposes a
//! named, ordered list of configuration sources to watch. `ConfigurationSources`
//! is that list: built-in defaults first, then each file in the order it was
//! added, then the command-line overrides.

use std::path::Path;

use std::convert::TryFrom;

use arti_config::{ArtiConfig, ConfigurationSources};

use crate::common::write_file;

#[test]
fn cfgsources__a_new_source_list_is_empty() {
    let sources = ConfigurationSources::new();
    assert_eq!(sources.files().count(), 0);
    assert_eq!(ConfigurationSources::default().files().count(), 0);
}

#[test]
fn cfgsources__files_are_listed_in_the_order_they_were_added() {
    let dir = tempfile::tempdir().expect("could not create a config tempdir");
    let a = write_file(&dir, "a.toml", "");
    let b = write_file(&dir, "b.toml", "");
    let mut sources = ConfigurationSources::new();
    sources.push_file(&a);
    sources.push_file(&b);
    let listed: Vec<&Path> = sources.files().collect();
    assert_eq!(listed, vec![a.as_path(), b.as_path()]);
}

#[test]
fn cfgsources__optional_files_are_listed_as_well() {
    // Watching has to cover a file that is not there yet, so an optional file
    // still appears in the list of files we care about.
    let dir = tempfile::tempdir().expect("could not create a config tempdir");
    let present = write_file(&dir, "present.toml", "");
    let absent = dir.path().join("absent.toml");
    let mut sources = ConfigurationSources::new();
    sources.push_optional_file(&absent);
    sources.push_file(&present);
    let listed: Vec<&Path> = sources.files().collect();
    assert_eq!(listed, vec![absent.as_path(), present.as_path()]);
}

#[test]
fn cfgsources__an_absent_optional_file_is_skipped() {
    let dir = tempfile::tempdir().expect("could not create a config tempdir");
    let mut sources = ConfigurationSources::new();
    sources.push_optional_file(dir.path().join("not-there.toml"));
    let cfg = sources.load().expect("an absent optional file must be ok");
    let cfg = ArtiConfig::try_from(cfg).expect("defaults should deserialize");
    assert_eq!(cfg.proxy().socks_port(), Some(9150));
}

#[test]
fn cfgsources__an_absent_required_file_is_an_error() {
    let dir = tempfile::tempdir().expect("could not create a config tempdir");
    let mut sources = ConfigurationSources::new();
    sources.push_file(dir.path().join("not-there.toml"));
    assert!(
        sources.load().is_err(),
        "a file added with push_file must be required"
    );
}

#[test]
fn cfgsources__built_in_defaults_are_always_applied() {
    // Even with no sources at all, loading yields a usable configuration.
    let cfg = ConfigurationSources::new()
        .load()
        .expect("the built-in defaults must always load");
    let cfg = ArtiConfig::try_from(cfg).expect("defaults should deserialize");
    assert_eq!(cfg.proxy().socks_port(), Some(9150));
    assert_eq!(cfg.logging().console_filter(), Some("debug"));
    assert!(!cfg.application().watch_configuration());
}

#[test]
fn cfgsources__a_later_file_overrides_an_earlier_one() {
    let dir = tempfile::tempdir().expect("could not create a config tempdir");
    let mut sources = ConfigurationSources::new();
    sources.push_file(write_file(&dir, "first.toml", "[proxy]\nsocks_port = 1111\n"));
    sources.push_file(write_file(
        &dir,
        "second.toml",
        "[proxy]\nsocks_port = 2222\n",
    ));
    let cfg = sources.load().expect("both files should load");
    let cfg = ArtiConfig::try_from(cfg).expect("configuration should deserialize");
    assert_eq!(cfg.proxy().socks_port(), Some(2222));
}

#[test]
fn cfgsources__command_line_options_override_every_file() {
    let dir = tempfile::tempdir().expect("could not create a config tempdir");
    let mut sources = ConfigurationSources::new();
    sources.push_file(write_file(&dir, "file.toml", "[proxy]\nsocks_port = 1111\n"));
    sources.push_option("proxy.socks_port=3333");
    let cfg = sources.load().expect("file and option should load");
    let cfg = ArtiConfig::try_from(cfg).expect("configuration should deserialize");
    assert_eq!(cfg.proxy().socks_port(), Some(3333));
    // The defaults are still there for everything the option did not mention.
    assert_eq!(cfg.logging().console_filter(), Some("debug"));
}
