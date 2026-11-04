//! Group `cfg_cmdline` - the command-line configuration source.
//!
//! instruction.md section 1: "a rewritten configuration system built around
//! builders" - the rewritten system accepts TOML fragments on the command line
//! as one of its sources.
//!
//! Graded items: `arti_config::CmdLine::{new, push_toml_line}` and its
//! `config::Source` implementation.  (`CmdLine::default()` is documented in
//! TEST_INTERFACE.md section 2.2 but no case here calls it.)

// The step-03 helpers live apart from `common` so that no gate group is
// forced to compile them; see the note at the top of common/current.rs.
use crate::current as common;

use arti_config::CmdLine;
use config::Source;

/// A `CmdLine` holding one line per element of `lines`.
fn cmdline(lines: &[&str]) -> CmdLine {
    let mut cl = CmdLine::new();
    for l in lines {
        cl.push_toml_line((*l).to_string());
    }
    cl
}

#[test]
fn cfg_cmdline__numeric_bareword_is_kept_as_a_string() {
    // A bareword right-hand side is quoted for convenience, so `a=3` arrives
    // as the string "3"; `config` coerces it where an integer is wanted.
    let v = common::collect_source(&cmdline(&["a=3"]));
    assert_eq!(v["a"], "3".into());
    assert_eq!(
        v["a"].clone().into_int().expect("\"3\" should coerce to an integer"),
        3
    );
}

#[test]
fn cfg_cmdline__alphabetic_bareword_is_quoted() {
    let v = common::collect_source(&cmdline(&["bcd=hello", "a=b_c"]));
    assert_eq!(v["bcd"], "hello".into());
    assert_eq!(v["a"], "b_c".into());
}

#[test]
fn cfg_cmdline__quoted_value_keeps_its_spaces() {
    let v = common::collect_source(&cmdline(&["ef=\"gh i\""]));
    assert_eq!(v["ef"], "gh i".into());
}

#[test]
fn cfg_cmdline__array_value_is_parsed_as_toml() {
    let v = common::collect_source(&cmdline(&["w=[1,2,3]"]));
    assert_eq!(v["w"], vec![1, 2, 3].into());
}

#[test]
fn cfg_cmdline__unparsable_line_is_reported_with_the_line() {
    // A hyphen on the right-hand side is not a bareword, so the line is handed
    // to the TOML parser unchanged and fails there.
    let err = cmdline(&["a=b-c"])
        .collect()
        .expect_err("`a=b-c` is not valid TOML and must not be accepted");
    let msg = err.to_string();
    assert!(
        msg.contains("a=b-c"),
        "the error should quote the offending command-line argument, got {:?}",
        msg
    );
}

#[test]
fn cfg_cmdline__dotted_key_nests_into_the_configuration() {
    let cfg = common::load_with_opts(&["hello.there.now=a_greeting"]);
    assert_eq!(
        cfg.get_str("hello.there.now").expect("dotted key did not nest"),
        "a_greeting"
    );
    // A default from the built-in file is still present alongside it.
    assert_eq!(cfg.get_int("proxy.socks_port").expect("defaults lost"), 9150);
}
