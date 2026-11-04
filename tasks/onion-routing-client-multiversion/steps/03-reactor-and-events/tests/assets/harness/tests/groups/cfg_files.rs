//! Group `cfg_files` - layering configuration files over the built-in defaults.
//!
//! instruction.md section 1: "a rewritten configuration system built around
//! builders"; Migration: "The configuration file format is revised".
//!
//! Graded item: `arti_config::load(files, opts)` - the required/optional flag,
//! the precedence of later sources over earlier ones, and the precedence of the
//! command line over files.
//!
//! Every file used here is created inside a `tempfile::TempDir` that the case
//! owns, so no case observes another's files.

// The step-03 helpers live apart from `common` so that no gate group is
// forced to compile them; see the note at the top of common/current.rs.
use crate::current as common;

use std::path::PathBuf;

#[test]
fn cfg_files__missing_required_file_is_an_error() {
    let dir = tempfile::tempdir().expect("tempdir");
    let missing: Vec<(PathBuf, bool)> = vec![(dir.path().join("absent.toml"), true)];
    assert!(
        arti_config::load(&missing, Vec::new()).is_err(),
        "a required file that does not exist must fail the load"
    );
}

#[test]
fn cfg_files__missing_optional_file_is_ignored() {
    let dir = tempfile::tempdir().expect("tempdir");
    let missing: Vec<(PathBuf, bool)> = vec![(dir.path().join("absent.toml"), false)];
    let cfg = arti_config::load(&missing, Vec::new())
        .expect("an optional file that does not exist must be skipped");
    assert_eq!(cfg.get_int("proxy.socks_port").expect("defaults lost"), 9150);
}

#[test]
fn cfg_files__file_overrides_a_builtin_default() {
    let dir = tempfile::tempdir().expect("tempdir");
    let p = common::write_file(dir.path(), "arti.toml", "[proxy]\nsocks_port = 1080\n");
    let files: Vec<(PathBuf, bool)> = vec![(p, true)];
    let cfg = arti_config::load(&files, Vec::new()).expect("load failed");
    assert_eq!(cfg.get_int("proxy.socks_port").expect("no socks_port"), 1080);
    // Values the file says nothing about keep their built-in values.
    assert_eq!(cfg.get_str("logging.trace_filter").expect("no trace_filter"), "debug");
    assert_eq!(cfg.get_str("storage.cache_dir").expect("no cache_dir"), "${ARTI_CACHE}");
}

#[test]
fn cfg_files__later_file_wins() {
    let dir = tempfile::tempdir().expect("tempdir");
    let first = common::write_file(dir.path(), "first.toml", "[proxy]\nsocks_port = 1080\n");
    let second = common::write_file(dir.path(), "second.toml", "[proxy]\nsocks_port = 2080\n");
    let files: Vec<(PathBuf, bool)> = vec![(first, true), (second, true)];
    let cfg = arti_config::load(&files, Vec::new()).expect("load failed");
    assert_eq!(cfg.get_int("proxy.socks_port").expect("no socks_port"), 2080);
}

#[test]
fn cfg_files__command_line_beats_files() {
    let dir = tempfile::tempdir().expect("tempdir");
    let p = common::write_file(
        dir.path(),
        "arti.toml",
        "[proxy]\nsocks_port = 1080\n\n[logging]\njournald = true\n",
    );
    let files: Vec<(PathBuf, bool)> = vec![(p, true)];
    let cfg = arti_config::load(&files, vec!["proxy.socks_port=3080".to_string()])
        .expect("load failed");
    assert_eq!(cfg.get_int("proxy.socks_port").expect("no socks_port"), 3080);
    // The rest of the file survives the command-line override.
    assert_eq!(cfg.get_bool("logging.journald").expect("no journald"), true);
}
