//! Group `miscutil`: the two small utilities this milestone adds beside the
//! main work -- `tor_basic_utils::IoErrorExt` and `fs_mistrust::anon_home`.
//!
//! PERIPHERAL. Neither is protocol-visible. They exist because the new
//! configuration-directory support has to tell "you named a file where a
//! directory belongs" apart from every other I/O failure, and because the
//! paths it reports end up in logs that should not name the user.
#![allow(non_snake_case)]

use std::io::ErrorKind;
use std::path::{Path, PathBuf};

use fs_mistrust::anon_home::PathExt as _;
use tor_basic_utils::IoErrorExt as _;

/// A directory inside this test's own fixtures, used as a real filesystem
/// object to provoke real OS errors.
fn fixture(name: &str) -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("fixtures").join(name)
}

/// Pin the home directory this process reports, so the substitution is
/// observable without depending on who is running the tests. `anon_home`
/// samples the environment once, on first use, and every case in this group
/// sets the same value first, so the order the cases run in does not matter.
fn pinned_home() -> PathBuf {
    let home = PathBuf::from("/conformance-home");
    std::env::set_var("HOME", &home);
    home
}

/// Walking "through" a plain file is ENOTDIR, and that is the case the
/// configuration-directory code has to recognise.
#[test]
fn miscutil__not_a_directory_is_detected() {
    let through_a_file = fixture("cfgdir").join("zzz.txt").join("inner");
    let err = std::fs::metadata(&through_a_file)
        .expect_err("zzz.txt is a file, so it has no children");
    assert!(
        err.is_not_a_directory(),
        "expected ENOTDIR for {}, got {err:?}",
        through_a_file.display()
    );
}

/// Every other I/O failure answers no, including a missing file inside a real
/// directory -- which is the failure it is most often confused with.
#[test]
fn miscutil__other_io_errors_are_not_not_a_directory() {
    let missing = fixture("cfgdir").join("no-such-file.toml");
    let err = std::fs::metadata(&missing).expect_err("this file does not exist");
    assert_eq!(err.kind(), ErrorKind::NotFound);
    assert!(!err.is_not_a_directory());

    let synthetic = std::io::Error::new(ErrorKind::PermissionDenied, "denied");
    assert!(!synthetic.is_not_a_directory());
}

/// A path under the home directory is reported symbolically.
#[test]
fn miscutil__anonymize_home_substitutes_the_home_prefix() {
    let home = pinned_home();
    let conf = home.join(".config").join("arti.toml");
    assert_eq!(conf.anonymize_home().to_string(), "${HOME}/.config/arti.toml");
    assert_eq!(home.join("x").anonymize_home().to_string(), "${HOME}/x");
    assert_eq!(
        conf,
        Path::new("/conformance-home/.config/arti.toml"),
        "anonymize_home is a display wrapper; it does not alter the path"
    );
}

/// A path outside the home directory is reported verbatim.
#[test]
fn miscutil__anonymize_home_leaves_other_paths_alone() {
    let _home = pinned_home();
    for path in ["/completely/untouchable/arti.toml", "/etc/arti/arti.toml"] {
        assert_eq!(
            Path::new(path).anonymize_home().to_string(),
            path,
            "{path} is not under the home directory"
        );
    }
    let relative = Path::new("arti.toml");
    assert_eq!(relative.anonymize_home().to_string(), "arti.toml");
}

/// The prefix match is by whole path component: a sibling directory whose name
/// merely starts with the home directory's name is not rewritten.
#[test]
fn miscutil__anonymize_home_matches_whole_components_only() {
    let _home = pinned_home();
    let sibling = "/conformance-home-of-someone-else/arti.toml";
    assert_eq!(
        Path::new(sibling).anonymize_home().to_string(),
        sibling,
        "a longer directory name is a different directory"
    );
    assert_eq!(
        Path::new("/conformance-home").anonymize_home().to_string(),
        "${HOME}/",
        "the home directory itself is the empty suffix"
    );
}
