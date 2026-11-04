//! Group `cfgpath_vars` - the `${ARTI_*}` path variables.
//!
//! instruction.md Migration: "The configuration file format is revised, and
//! `${APP_*}` variables become `${ARTI_*}`".
//!
//! Graded items: `tor_config::CfgPath::{new, path, from_path}` and its
//! `Display` implementation.
//!
//! Recognition, not expansion, is asserted: where `${ARTI_CACHE}` expands to
//! depends on the invoking user's home and XDG directories, but whether the
//! name is recognised at all does not. An unrecognised name produces the
//! message "unrecognized variable NAME"; see `common::cfg_var_is_recognized`.

// The step-03 helpers live apart from `common` so that no gate group is
// forced to compile them; see the note at the top of common/current.rs.
use crate::current as common;

use tor_config::CfgPath;

#[test]
fn cfgpath_vars__arti_cache_is_recognized() {
    assert!(common::cfg_var_is_recognized("ARTI_CACHE"));
}

#[test]
fn cfgpath_vars__arti_config_is_recognized() {
    assert!(common::cfg_var_is_recognized("ARTI_CONFIG"));
}

#[test]
fn cfgpath_vars__arti_data_directories_are_recognized() {
    assert!(common::cfg_var_is_recognized("ARTI_SHARED_DATA"));
    assert!(common::cfg_var_is_recognized("ARTI_LOCAL_DATA"));
}

#[test]
fn cfgpath_vars__user_home_is_recognized() {
    assert!(common::cfg_var_is_recognized("USER_HOME"));
}

#[test]
fn cfgpath_vars__app_prefixed_variables_are_gone() {
    // The pre-migration spellings must no longer be understood, and the error
    // must name the variable that was not understood.
    for name in ["APP_CACHE", "APP_CONFIG", "APP_SHARED_DATA", "APP_LOCAL_DATA"] {
        let p = CfgPath::new(format!("${{{}}}/example", name));
        let err = p
            .path()
            .err()
            .unwrap_or_else(|| panic!("${{{}}} is still being expanded", name));
        assert_eq!(err.to_string(), format!("unrecognized variable {}", name));
    }
}

#[test]
fn cfgpath_vars__unknown_variable_names_itself() {
    let p = CfgPath::new("${ARTI_WOMBAT}/example".to_owned());
    assert_eq!(
        p.path().expect_err("unknown variable expanded").to_string(),
        "unrecognized variable ARTI_WOMBAT"
    );

    // A path with no variables in it is passed through unchanged.
    let plain = CfgPath::new("Hello/world".to_owned());
    assert_eq!(plain.to_string(), "Hello/world");
    assert_eq!(
        plain.path().expect("a plain path must expand"),
        std::path::PathBuf::from("Hello/world")
    );
}

#[test]
fn cfgpath_vars__literal_paths_are_never_expanded() {
    // A path built from a `Path` is literal: it keeps its dollar signs.
    let p = CfgPath::from_path(std::path::PathBuf::from("${ARTI_CACHE}/literally"));
    assert_eq!(
        p.path().expect("a literal path must not fail"),
        std::path::PathBuf::from("${ARTI_CACHE}/literally")
    );
    assert_eq!(p.to_string(), "\"${ARTI_CACHE}/literally\" [exactly]");
}
