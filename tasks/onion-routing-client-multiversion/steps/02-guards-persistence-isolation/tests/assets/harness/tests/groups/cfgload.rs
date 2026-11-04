//! Group `cfgload` - loading a configuration file from disk.
//!
//! Graded through `tor_config::{load, CmdLine}` and `config::Config`. The
//! client reads its configuration from TOML files and from the command line,
//! and has to be able to say whether a missing file is an error or merely an
//! absent override, with the command line winning over the files.

use std::fs;
use std::path::PathBuf;
use config::Config;
use tor_config::{load, CmdLine};

/// Write `text` to `name` inside `dir` and return the path.
fn write(dir: &std::path::Path, name: &str, text: &str) -> PathBuf {
    let p = dir.join(name);
    fs::write(&p, text).expect("could not write the fixture config file");
    p
}

#[test]
fn cfgload__a_required_file_that_is_absent_is_an_error() {
    let dir = crate::common::tempdir();
    let missing: PathBuf = dir.path().join("no-such-file.toml");

    let mut cfg = Config::new();
    let err = load(&mut cfg, &[(missing.as_path(), true)], Vec::new())
        .expect_err("a required file that does not exist was accepted");
    assert!(
        err.to_string().contains("no-such-file.toml"),
        "the error does not name the file: {}",
        err
    );

    // Nothing was merged, so the configuration is still empty.
    assert!(cfg.get_str("hello").is_err());
}

#[test]
fn cfgload__an_optional_file_that_is_absent_simply_contributes_nothing() {
    let dir = crate::common::tempdir();
    let missing: PathBuf = dir.path().join("no-such-file.toml");

    let mut cfg = Config::new();
    load(&mut cfg, &[(missing.as_path(), false)], Vec::new())
        .expect("an optional file that does not exist was refused");
    assert!(
        cfg.get_str("hello").is_err(),
        "a missing optional file still contributed a value"
    );
}

#[test]
fn cfgload__values_from_a_file_reach_the_configuration() {
    let dir = crate::common::tempdir();
    let path = write(
        dir.path(),
        "arti.toml",
        "[hello]\nworld = \"stuff\"\nfriends = 4242\n",
    );

    let mut cfg = Config::new();
    load(&mut cfg, &[(path.as_path(), true)], Vec::new()).expect("loading a good file failed");
    assert_eq!(cfg.get_str("hello.world").expect("missing key"), "stuff");
    assert_eq!(cfg.get_int("hello.friends").expect("missing key"), 4242);

    // A later file overrides an earlier one, and the command line overrides
    // both, so the last source named wins.
    let second = write(dir.path(), "second.toml", "[hello]\nworld = \"other\"\n");
    let mut cfg = Config::new();
    load(
        &mut cfg,
        &[(path.as_path(), true), (second.as_path(), true)],
        Vec::new(),
    )
    .expect("loading two files failed");
    assert_eq!(cfg.get_str("hello.world").expect("missing key"), "other");
    assert_eq!(
        cfg.get_int("hello.friends").expect("missing key"),
        4242,
        "a key the second file is silent about should survive"
    );
}

#[test]
fn cfgload__command_line_options_are_toml_and_beat_the_files() {
    let dir = crate::common::tempdir();
    let path = write(dir.path(), "arti.toml", "[hello]\nworld = \"stuff\"\n");

    let mut cfg = Config::new();
    load(
        &mut cfg,
        &[(path.as_path(), true)],
        vec!["hello.world=overridden".to_string()],
    )
    .expect("a command-line option was rejected");
    assert_eq!(cfg.get_str("hello.world").expect("missing key"), "overridden");

    // A bareword is quoted for convenience, so a number-typed setting can
    // still be given as `key=123`, and dotted keys build a nested table.
    let mut cfg = Config::new();
    load(
        &mut cfg,
        &[] as &[(PathBuf, bool)],
        vec!["hello.friends=12345".to_string(), "hello.world=x".to_string()],
    )
    .expect("bareword command-line options were rejected");
    assert_eq!(cfg.get_int("hello.friends").expect("missing key"), 12345);
    assert_eq!(cfg.get_str("hello.world").expect("missing key"), "x");

    // An option that is not valid TOML is an error, and the message points
    // at the offending option rather than at a line number in a file that
    // does not exist.
    let mut cfg = Config::new();
    let err = load(
        &mut cfg,
        &[] as &[(PathBuf, bool)],
        vec!["this is not toml at all".to_string()],
    )
    .expect_err("a malformed command-line option was accepted");
    let msg = err.to_string();
    assert!(
        msg.contains("this is not toml at all"),
        "the error does not quote the option: {}",
        msg
    );
}

#[test]
fn cfgload__a_cmdline_can_be_fed_options_one_at_a_time() {
    // `CmdLine` is the building block `load` uses; it collects TOML lines
    // and is itself a configuration source.
    let mut cl = CmdLine::new();
    cl.push_toml_line("a=1".to_string());
    cl.push_toml_line("b.c=hello".to_string());

    let mut cfg = Config::new();
    cfg.merge(cl).expect("merging a CmdLine failed");
    assert_eq!(cfg.get_int("a").expect("missing key"), 1);
    assert_eq!(cfg.get_str("b.c").expect("missing key"), "hello");

    // An empty command line contributes nothing at all.
    let mut cfg = Config::new();
    cfg.merge(CmdLine::new()).expect("merging an empty CmdLine failed");
    assert!(cfg.get_str("a").is_err());
    assert!(cfg.get_int("a").is_err());
}
