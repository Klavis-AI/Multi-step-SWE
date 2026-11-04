//! Group `cfgsource`: configuration sources and the configuration directory.
//!
//! This milestone adds a *directory* as a configuration source: a source may
//! now be a file or a directory, a directory contributes its `*.toml` entries
//! in sorted order, and the directory itself is retained in the scan result so
//! a caller can watch it for new files. `ConfigurationSource`,
//! `ConfigurationSources::scan` and `FoundConfigFiles` are all new here; at
//! the previous milestone the sources were a plain list of files.
//!
//! Every case that reads files installs a permissive `Mistrust`: filesystem
//! ownership and mode are a property of whatever machine this runs on, and are
//! not what this group grades.
#![allow(non_snake_case)]

use std::path::{Path, PathBuf};

use fs_mistrust::Mistrust;
use tor_config::config_crate::Config;
use tor_config::sources::MustRead;
use tor_config::{ConfigurationSource, ConfigurationSources};

/// The absolute path of a fixture, which is what a configuration source names.
fn fixture(name: &str) -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("fixtures").join(name)
}

/// Sources that read the fixtures without judging their permissions.
fn sources() -> ConfigurationSources {
    let mut s = ConfigurationSources::new_empty();
    s.set_mistrust(Mistrust::new_dangerously_trust_everyone());
    s
}

/// Load, or report why not.
fn load(s: &ConfigurationSources) -> Config {
    s.load().expect("these sources should load")
}

/// Whether a source is a file or a directory is decided syntactically, without
/// touching the filesystem, so a config path can be classified before it
/// exists.
#[test]
fn cfgsource__from_path_distinguishes_files_and_directories() {
    assert_eq!(
        ConfigurationSource::from_path("/etc/arti/arti.toml"),
        ConfigurationSource::File(PathBuf::from("/etc/arti/arti.toml"))
    );
    assert_eq!(
        ConfigurationSource::from_path("/etc/arti.d/"),
        ConfigurationSource::Dir(PathBuf::from("/etc/arti.d/")),
        "a trailing separator means a directory"
    );
    for dirish in [".", "..", "/"] {
        assert!(
            matches!(
                ConfigurationSource::from_path(dirish),
                ConfigurationSource::Dir(_)
            ),
            "{dirish} names a directory"
        );
    }
    assert_eq!(
        ConfigurationSource::from_path("/etc/arti.d/").as_path(),
        Path::new("/etc/arti.d/")
    );
}

/// Scanning a directory yields its `*.toml` entries in sorted order, and keeps
/// the directory itself in the list so it can be watched for new files.
#[test]
fn cfgsource__scan_expands_a_directory_to_its_toml_files() {
    let dir = fixture("cfgdir");
    let mut s = sources();
    s.push_source(ConfigurationSource::Dir(dir.clone()), MustRead::MustRead);

    let found = s.scan().expect("the fixture directory exists");
    let got: Vec<&ConfigurationSource> = found.iter().collect();
    assert_eq!(
        got,
        vec![
            &ConfigurationSource::Dir(dir.clone()),
            &ConfigurationSource::File(dir.join("a-first.toml")),
            &ConfigurationSource::File(dir.join("b-second.toml")),
        ],
        "the directory, then its .toml entries sorted by path"
    );
    assert!(
        !got.contains(&&ConfigurationSource::File(dir.join("zzz.txt"))),
        "a non-.toml entry is not a configuration file"
    );
}

/// A directory that is not there is skipped when it is optional and fails the
/// scan when it is required.
#[test]
fn cfgsource__missing_directory_is_tolerated_only_when_optional() {
    let missing = fixture("no-such-dir");

    let mut ok = sources();
    ok.push_source(
        ConfigurationSource::Dir(missing.clone()),
        MustRead::TolerateAbsence,
    );
    let found = ok.scan().expect("an absent optional directory is not an error");
    assert_eq!(found.iter().count(), 0, "nothing was found, and that is fine");

    let mut bad = sources();
    bad.push_source(ConfigurationSource::Dir(missing), MustRead::MustRead);
    let err = bad.scan().expect_err("a required directory must exist");
    assert!(
        err.to_string().contains("unable to access config path"),
        "unexpected error: {err}"
    );
}

/// A required file that is absent is an error when the configuration is
/// actually loaded.
#[test]
fn cfgsource__missing_required_file_is_an_error() {
    let mut s = sources();
    s.push_source(
        ConfigurationSource::File(fixture("no-such-file.toml")),
        MustRead::MustRead,
    );
    let err = s.load().expect_err("a required file must exist");
    let text = err.to_string();
    assert!(
        text.contains("no-such-file"),
        "the error should name the file it could not read: {text}"
    );
}

/// An optional file that is absent leaves the configuration untouched.
#[test]
fn cfgsource__missing_optional_file_loads_an_empty_configuration() {
    let mut s = sources();
    s.push_source(
        ConfigurationSource::File(fixture("no-such-file.toml")),
        MustRead::TolerateAbsence,
    );
    let cfg = load(&s);
    assert!(
        cfg.get_string("padding").is_err(),
        "nothing was read, so no key is set"
    );
}

/// Within a directory, a later file overrides an earlier one key by key, and
/// keys only the earlier file set survive.
#[test]
fn cfgsource__directory_entries_merge_in_sorted_order() {
    let mut s = sources();
    s.push_source(
        ConfigurationSource::Dir(fixture("cfgmerge")),
        MustRead::MustRead,
    );
    let cfg = load(&s);
    assert_eq!(
        cfg.get_string("padding").expect("padding is set"),
        "reduced",
        "02-override.toml is read after 01-base.toml"
    );
    assert_eq!(
        cfg.get_int("extra.n").expect("extra.n is set"),
        1,
        "a key the later file does not mention survives"
    );
}

/// Command-line options are applied after every file, so they win.
#[test]
fn cfgsource__options_override_the_files() {
    let mut s = sources();
    s.push_source(
        ConfigurationSource::Dir(fixture("cfgmerge")),
        MustRead::MustRead,
    );
    s.push_option("padding=\"none\"");
    let cfg = load(&s);
    assert_eq!(cfg.get_string("padding").expect("padding is set"), "none");
    assert_eq!(cfg.get_int("extra.n").expect("extra.n is set"), 1);
}

/// The `*.toml` rule filters a directory scan only. A file named directly is
/// read as TOML whatever it is called -- so naming a non-TOML file is a
/// configuration error, not a silent skip.
#[test]
fn cfgsource__a_named_file_is_parsed_as_toml_regardless_of_extension() {
    let mut s = sources();
    s.push_source(
        ConfigurationSource::File(fixture("cfgdir").join("zzz.txt")),
        MustRead::MustRead,
    );
    let err = s.load().expect_err("zzz.txt is not valid TOML");
    assert!(!err.to_string().is_empty(), "the parse failure is reported");

    // The same file inside a scanned directory is ignored instead.
    let mut ok = sources();
    ok.push_source(
        ConfigurationSource::Dir(fixture("cfgdir")),
        MustRead::MustRead,
    );
    let cfg = load(&ok);
    assert_eq!(
        cfg.get_string("padding").expect("from a-first.toml"),
        "normal"
    );
}
