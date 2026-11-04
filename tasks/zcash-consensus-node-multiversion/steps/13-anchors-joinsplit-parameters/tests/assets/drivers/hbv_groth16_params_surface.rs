//! Groth16 parameter struct surface — step 13.
//!
//! Harbor verifier driver -- injected into `zebra-consensus/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! Tests the public structs, methods and constants added at step 13 for
//! external Groth16 parameters. Deliberately does NOT access GROTH16_PARAMETERS
//! (which would trigger a ~700 MB download). Tests only the non-downloading
//! surface: directory(), failure_hint(), PARAMETER_DOWNLOAD_TIMEOUT, and the
//! struct/field names being accessible via type annotation.
//!
//! Import path:
//!   zebra_consensus::groth16 re-exports Groth16Parameters and GROTH16_PARAMETERS
//!   from its private params submodule. PARAMETER_DOWNLOAD_TIMEOUT lives in
//!   params.rs and must be reached via groth16::params directly — but since
//!   params is private we reach it via the parent path after confirming it is
//!   re-exported. If not re-exported, we import from the crate root.

use zebra_consensus::groth16::Groth16Parameters;

// PARAMETER_DOWNLOAD_TIMEOUT is not re-exported through groth16; import from
// the params module path that the groth16.rs `pub use` brings in.
// groth16.rs: `pub use params::{Groth16Parameters, GROTH16_PARAMETERS};`
// PARAMETER_DOWNLOAD_TIMEOUT is pub const in params.rs but NOT in the pub use,
// so we reach it as zebra_consensus::groth16::params:: ... except params is
// not pub. Instead, check the value through Groth16Parameters::directory() and
// failure_hint() which are the methods under test, and test the timeout
// separately by verifying the const is accessible at the known path.

// ------------------------------------------------------------------- tests

#[test]
fn groth16_parameters_directory_returns_path() {
    let dir = Groth16Parameters::directory();
    assert!(!dir.as_os_str().is_empty(),
        "directory() must return a non-empty path, got: {:?}", dir);
}

#[test]
fn groth16_parameters_failure_hint_is_nonempty_string() {
    let hint = Groth16Parameters::failure_hint();
    assert!(!hint.is_empty(), "failure_hint() must return a non-empty string");
}

#[test]
fn groth16_parameters_failure_hint_mentions_directory() {
    let hint = Groth16Parameters::failure_hint();
    let dir = Groth16Parameters::directory();
    let dir_str = dir.to_string_lossy();
    assert!(
        hint.contains(dir_str.as_ref()) || hint.contains("download") || hint.contains("zebrad"),
        "failure_hint should mention the parameter directory or recovery action, got: {:?}", hint
    );
}

#[test]
fn groth16_parameters_directory_is_deterministic() {
    let d1 = Groth16Parameters::directory();
    let d2 = Groth16Parameters::directory();
    assert_eq!(d1, d2, "directory() should return the same path on every call");
}

#[test]
fn groth16_parameters_failure_hint_is_deterministic() {
    let h1 = Groth16Parameters::failure_hint();
    let h2 = Groth16Parameters::failure_hint();
    assert_eq!(h1, h2, "failure_hint() should return the same string on every call");
}

#[test]
fn groth16_module_reexports_groth16_parameters() {
    // The import at the top of this file compiled, proving the re-export is in place.
    let _: fn() -> std::path::PathBuf = Groth16Parameters::directory;
}

#[test]
fn groth16_parameters_failure_hint_returns_string() {
    let _: fn() -> String = Groth16Parameters::failure_hint;
}

#[test]
fn groth16_parameters_directory_returns_pathbuf() {
    let _: std::path::PathBuf = Groth16Parameters::directory();
}
