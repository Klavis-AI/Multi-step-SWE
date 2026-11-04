//! `tor_config::ConfigurationSources`: the ordered list of configuration files
//! and command-line overrides, together with the `Mistrust` used to decide
//! whether each file is safe to read.
#![allow(non_snake_case)]
use crate::common::Tree;
use fs_mistrust::{Mistrust, TrustedGroup, TrustedUser};
use std::path::{Path, PathBuf};
use tor_config::ConfigurationSources;

fn strict(root: &Path) -> Mistrust {
    let mut b = Mistrust::builder();
    b.ignore_prefix(root)
        .trust_user(TrustedUser::Current)
        .trust_group(TrustedGroup::None);
    b.build().unwrap()
}

/// `ConfigurationSources` whose permission checks are pinned to this tree.
fn sources(t: &Tree) -> ConfigurationSources {
    let mut s = ConfigurationSources::new_empty();
    s.set_mistrust(strict(t.root()));
    s
}

/// No config files, no overrides: the types the generic parameters need.
const NO_FILES: [PathBuf; 0] = [];
const NO_OPTS: [String; 0] = [];

#[test]
fn cfgsources__new_empty_reads_nothing() {
    let s = ConfigurationSources::new_empty();
    assert_eq!(s.files().count(), 0);
    // The default is the ordinary strict `Mistrust`, not a permissive one.
    assert_eq!(s.mistrust(), &Mistrust::new());

    let cfg = s.load().unwrap();
    assert!(cfg.get_int("anything").is_err());
}

#[test]
fn cfgsources__files_are_reported_in_the_order_they_were_added() {
    let t = Tree::new();
    let mut s = sources(&t);
    let a = t.file("a.toml", b"", 0o600);
    let b = t.file("b.toml", b"", 0o600);
    let c = t.file("c.toml", b"", 0o600);
    s.push_file(&a);
    s.push_optional_file(&b);
    s.push_file(&c);
    let got: Vec<&Path> = s.files().collect();
    assert_eq!(got, vec![a.as_path(), b.as_path(), c.as_path()]);
}

#[test]
fn cfgsources__from_cmdline_falls_back_to_the_default_file() {
    // With no `-c` on the command line we read the default file, and its
    // absence is not fatal.
    let t = Tree::new();
    let default = t.path("arti.toml");
    let mut s = ConfigurationSources::from_cmdline(&default, NO_FILES, NO_OPTS);
    s.set_mistrust(strict(t.root()));

    let got: Vec<&Path> = s.files().collect();
    assert_eq!(got, vec![default.as_path()]);
    assert!(!default.exists());
    s.load().unwrap();
}

#[test]
fn cfgsources__from_cmdline_replaces_the_default_with_the_named_files() {
    let t = Tree::new();
    let default = t.file("arti.toml", b"answer = 1\n", 0o600);
    let named = t.file("named.toml", b"answer = 2\n", 0o600);

    let mut s = ConfigurationSources::from_cmdline(&default, [&named], NO_OPTS);
    s.set_mistrust(strict(t.root()));
    let got: Vec<&Path> = s.files().collect();
    assert_eq!(got, vec![named.as_path()]);
    assert_eq!(s.load().unwrap().get_int("answer").unwrap(), 2);
}

#[test]
fn cfgsources__from_cmdline_appends_the_override_options() {
    let t = Tree::new();
    let f = t.file("arti.toml", b"answer = 1\nkept = 9\n", 0o600);
    let mut s = ConfigurationSources::from_cmdline(&f, [&f], ["answer=2".to_string()]);
    s.set_mistrust(strict(t.root()));

    let cfg = s.load().unwrap();
    assert_eq!(cfg.get_int("answer").unwrap(), 2);
    assert_eq!(cfg.get_int("kept").unwrap(), 9);
}

#[test]
fn cfgsources__later_files_override_earlier_ones() {
    let t = Tree::new();
    let a = t.file("a.toml", b"shared = 1\nonly_a = 10\n", 0o600);
    let b = t.file("b.toml", b"shared = 2\nonly_b = 20\n", 0o600);

    let mut s = sources(&t);
    s.push_file(&a);
    s.push_file(&b);
    let cfg = s.load().unwrap();
    assert_eq!(cfg.get_int("shared").unwrap(), 2);
    assert_eq!(cfg.get_int("only_a").unwrap(), 10);
    assert_eq!(cfg.get_int("only_b").unwrap(), 20);

    // Reversing the order reverses which value wins.
    let mut s = sources(&t);
    s.push_file(&b);
    s.push_file(&a);
    assert_eq!(s.load().unwrap().get_int("shared").unwrap(), 1);
}

#[test]
fn cfgsources__options_are_applied_after_every_file() {
    let t = Tree::new();
    let a = t.file("a.toml", b"[section]\nvalue = 1\n", 0o600);
    let mut s = sources(&t);
    s.push_option("section.value=2");
    s.push_file(&a);

    // The option was added first, but options still win over files.
    let cfg = s.load().unwrap();
    assert_eq!(cfg.get_int("section.value").unwrap(), 2);
}

#[test]
fn cfgsources__a_missing_required_file_is_an_error_but_an_optional_one_is_not() {
    let t = Tree::new();
    let present = t.file("present.toml", b"answer = 1\n", 0o600);

    let mut s = sources(&t);
    s.push_file(&present);
    s.push_optional_file(t.path("absent.toml"));
    assert_eq!(s.load().unwrap().get_int("answer").unwrap(), 1);

    let mut s = sources(&t);
    s.push_file(&present);
    s.push_file(t.path("absent.toml"));
    assert!(s.load().is_err());
}

#[test]
fn cfgsources__a_writable_config_file_is_refused_but_a_readable_one_is_not() {
    let t = Tree::new();
    let f = t.file("arti.toml", b"answer = 1\n", 0o644);

    // World-readable is fine: config files are not secrets.
    let mut s = sources(&t);
    s.push_file(&f);
    assert_eq!(s.load().unwrap().get_int("answer").unwrap(), 1);

    // World-writable is not: anyone could rewrite our configuration.
    crate::common::chmod(&f, 0o666);
    let mut s = sources(&t);
    s.push_file(&f);
    assert!(s.load().is_err());

    // Nor is an optional file excused from the check; only absence is.
    let mut s = sources(&t);
    s.push_optional_file(&f);
    assert!(s.load().is_err());
}

#[test]
fn cfgsources__set_mistrust_decides_which_files_are_acceptable() {
    let t = Tree::new();
    let f = t.file("arti.toml", b"answer = 1\n", 0o666);

    let mut s = sources(&t);
    s.push_file(&f);
    assert!(s.load().is_err());

    // The same sources with the permissive checker read the same file.
    let lax = Mistrust::new_dangerously_trust_everyone();
    s.set_mistrust(lax.clone());
    assert_eq!(s.mistrust(), &lax);
    assert_eq!(s.load().unwrap().get_int("answer").unwrap(), 1);
}
