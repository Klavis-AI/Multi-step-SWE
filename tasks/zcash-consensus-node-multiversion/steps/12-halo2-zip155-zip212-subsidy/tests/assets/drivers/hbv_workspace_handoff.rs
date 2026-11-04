//! The handed-down documents in `/app` survive this milestone -- gate group.
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//! It touches no crate API at all: it reads the filesystem.
//!
//! `instruction.md`'s "Handoff" requirement and `TEST_INTERFACE.md §9` both say
//! that `/app/specs/`, `/app/TEST_INTERFACE.md`, `/app/WORKSPACE_HANDOFF.md`
//! and `/app/timer.sh` must not be deleted. The whole `/app` tree is snapshotted
//! as this step's artifact (minus `.git/` and `target/`) and restored as step
//! 13's starting workspace, so a candidate who cleans them up hands the next
//! milestone a workspace with no specification to read -- and, until this group
//! existed, still scored 100.
//!
//! Nothing here is new at step 12: the same four paths are handed down at every
//! band, so a tree that stopped at step 11 passes unchanged. That is why this is
//! a weight-0 gate group and not a scored one -- it earns nothing and vetoes.
//!
//! The workspace root is derived from `CARGO_MANIFEST_DIR` rather than
//! hard-coded to `/app`: the driver is compiled from `<workspace>/zebra-chain`,
//! whose parent is the workspace root. Under the verifier that is `/app`; under
//! the authoring loop's local runner it is the reference tree, which is seeded
//! with the same four paths.

use std::path::{Path, PathBuf};

/// The workspace root: the parent of the crate this test is compiled into.
fn app_root() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .expect("the crate directory always has a parent")
        .to_path_buf()
}

/// Assert that `relative` names a readable, non-empty regular file under the
/// workspace root.
fn assert_nonempty_file(relative: &str) {
    let path = app_root().join(relative);
    assert!(
        path.is_file(),
        "{} must still exist at {:?}: the milestone's handoff requirement says \
         it must not be deleted, and step 13 starts from this workspace",
        relative,
        path
    );
    let len = std::fs::metadata(&path)
        .unwrap_or_else(|e| panic!("{:?} is not readable: {}", path, e))
        .len();
    assert!(
        len > 0,
        "{} must still have contents, but {:?} is empty",
        relative,
        path
    );
}

/// `/app/specs/` survives, with the protocol text and the ZIPs this milestone's
/// "Read these local references" section names.
///
/// Emptying the directory is as damaging to step 13 as deleting it, so the
/// individual documents are named rather than just the directory.
#[test]
fn specs_directory_survives_with_the_texts_this_milestone_cites() {
    let specs: &Path = &app_root().join("specs");
    assert!(
        specs.is_dir(),
        "specs/ must still be a directory at {:?}",
        specs
    );

    for relative in [
        "specs/nu5.txt",
        "specs/PROVENANCE.md",
        "specs/zips/zip-0155.rst",
        "specs/zips/zip-0207.rst",
        "specs/zips/zip-0212.rst",
    ] {
        assert_nonempty_file(relative);
    }
}

/// `/app/TEST_INTERFACE.md` survives. It is the contract the graded tests are
/// written against, and step 13 replaces it rather than recreating it.
#[test]
fn test_interface_document_survives() {
    assert_nonempty_file("TEST_INTERFACE.md");
}

/// `/app/WORKSPACE_HANDOFF.md` survives. It carries the offline-resolution and
/// dependency rules that apply to every band.
#[test]
fn workspace_handoff_document_survives() {
    assert_nonempty_file("WORKSPACE_HANDOFF.md");
}

/// `/app/timer.sh` survives and is still a script, not a hollowed-out stub.
#[test]
fn timer_script_survives() {
    assert_nonempty_file("timer.sh");

    let path = app_root().join("timer.sh");
    let text = std::fs::read_to_string(&path)
        .unwrap_or_else(|e| panic!("{:?} is not readable as text: {}", path, e));
    assert!(
        text.starts_with("#!"),
        "timer.sh must still be an executable script beginning with a shebang, \
         but {:?} starts with {:?}",
        path,
        text.chars().take(16).collect::<String>()
    );
}
