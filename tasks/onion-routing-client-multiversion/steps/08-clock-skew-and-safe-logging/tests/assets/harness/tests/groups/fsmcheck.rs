//! Group `fsmcheck`: the basic filesystem-permission check -- every directory
//! on the way to the target must be unwritable by anyone untrusted, and the
//! target itself must not be readable by anyone untrusted.
//!
//! Traces to instruction.md section 1: "filesystem-permission checking".
//! Interface: `fs_mistrust::Mistrust::check_directory` and
//! `fs_mistrust::Error`.

use crate::common::{assert_bad_permission, mistrust_under, Tree};
use fs_mistrust::Error;

#[test]
fn fsmcheck__a_private_chain_of_directories_is_accepted() {
    let tree = Tree::new();
    tree.dir("a/b/c");
    tree.chmod("a", 0o700);
    tree.chmod("a/b", 0o700);
    tree.chmod("a/b/c", 0o700);

    mistrust_under(&tree)
        .check_directory(tree.canonical_path("a/b/c"))
        .expect("a chain of 0700 directories should be accepted");

    // A merely-searchable ancestor is fine too: only write access matters
    // above the target.
    tree.chmod("a", 0o755);
    tree.chmod("a/b", 0o755);
    mistrust_under(&tree)
        .check_directory(tree.canonical_path("a/b/c"))
        .expect("0755 ancestors should be accepted");
}

#[test]
fn fsmcheck__a_world_writable_ancestor_is_rejected() {
    let tree = Tree::new();
    tree.dir("e/f/g");
    tree.chmod("e", 0o755);
    tree.chmod("e/f", 0o777);
    tree.chmod("e/f/g", 0o700);

    let err = mistrust_under(&tree)
        .check_directory(tree.canonical_path("e/f/g"))
        .expect_err("a world-writable ancestor must be rejected");
    // Above the target only the write bits are forbidden, so only 0o022 of
    // the offending 0o777 is reported.
    assert_bad_permission(&err, &tree.canonical_path("e/f"), 0o022);
}

#[test]
fn fsmcheck__a_world_writable_target_reports_every_forbidden_bit() {
    let tree = Tree::new();
    tree.dir("x");
    tree.chmod("x", 0o777);

    let err = mistrust_under(&tree)
        .check_directory(tree.canonical_path("x"))
        .expect_err("a world-writable target must be rejected");
    // The target may not be readable either, so all of group and other is
    // forbidden.
    assert_bad_permission(&err, &tree.canonical_path("x"), 0o077);
}

#[test]
fn fsmcheck__a_world_readable_target_is_rejected_by_default() {
    let tree = Tree::new();
    tree.dir("y");
    tree.chmod("y", 0o755);

    let err = mistrust_under(&tree)
        .check_directory(tree.canonical_path("y"))
        .expect_err("a world-readable target must be rejected by default");
    assert_bad_permission(&err, &tree.canonical_path("y"), 0o055);
}

#[test]
fn fsmcheck__a_missing_component_is_named_in_the_error() {
    let tree = Tree::new();
    tree.dir("p");
    tree.chmod("p", 0o700);

    let err = mistrust_under(&tree)
        .check_directory(tree.canonical_path("p/q/r"))
        .expect_err("a missing directory must be reported");
    match &err {
        Error::NotFound(p) => assert_eq!(p.as_path(), tree.canonical_path("p/q")),
        other => panic!("expected NotFound, got {:?}", other),
    }
    assert_eq!(err.path(), Some(tree.canonical_path("p/q").as_path()));
}

#[test]
fn fsmcheck__a_regular_file_is_rejected_where_a_directory_is_required() {
    let tree = Tree::new();
    tree.file("f");
    tree.chmod("f", 0o600);

    let err = mistrust_under(&tree)
        .check_directory(tree.canonical_path("f"))
        .expect_err("a file is not a directory");
    match &err {
        Error::BadType(p) => assert_eq!(p.as_path(), tree.canonical_path("f")),
        other => panic!("expected BadType, got {:?}", other),
    }
    assert_eq!(err.to_string(), format!("Wrong type of file at {}", tree.canonical_path("f").display()));
}

#[test]
fn fsmcheck__checking_never_creates_anything() {
    let tree = Tree::new();
    tree.dir("z");
    tree.chmod("z", 0o700);

    let err = mistrust_under(&tree)
        .check_directory(tree.canonical_path("z/sub"))
        .expect_err("the subdirectory does not exist");
    assert!(matches!(err, Error::NotFound(_)), "got {:?}", err);
    assert!(
        !tree.exists("z/sub"),
        "check_directory must not create the directory it checks"
    );
}
