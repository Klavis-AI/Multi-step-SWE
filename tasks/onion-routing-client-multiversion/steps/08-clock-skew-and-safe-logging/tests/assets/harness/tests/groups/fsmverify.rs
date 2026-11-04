//! Group `fsmverify`: the per-check options that relax or tighten the
//! filesystem-permission check -- readability, required object type, error
//! collection and directory contents.
//!
//! Traces to instruction.md section 1: "filesystem-permission checking".
//! Interface: `fs_mistrust::Mistrust::verifier` and the `fs_mistrust::Verifier`
//! builder methods.

use crate::common::{assert_bad_permission, mistrust_under, Tree};
use fs_mistrust::Error;

#[test]
fn fsmverify__permit_readable_accepts_a_world_readable_target() {
    let tree = Tree::new();
    tree.dir("r");
    tree.chmod("r", 0o755);
    let m = mistrust_under(&tree);

    m.verifier()
        .permit_readable()
        .require_directory()
        .check(tree.canonical_path("r"))
        .expect("permit_readable should accept a 0755 target");

    let err = m
        .verifier()
        .require_directory()
        .check(tree.canonical_path("r"))
        .expect_err("without permit_readable the same target is rejected");
    assert_bad_permission(&err, &tree.canonical_path("r"), 0o055);
}

#[test]
fn fsmverify__permit_readable_still_rejects_a_writable_target() {
    let tree = Tree::new();
    tree.dir("w");
    tree.chmod("w", 0o777);

    let err = mistrust_under(&tree)
        .verifier()
        .permit_readable()
        .require_directory()
        .check(tree.canonical_path("w"))
        .expect_err("permit_readable must not permit writing");
    // Only the write bits remain forbidden once reading is permitted.
    assert_bad_permission(&err, &tree.canonical_path("w"), 0o022);
}

#[test]
fn fsmverify__require_file_rejects_a_directory() {
    let tree = Tree::new();
    tree.dir("d");
    tree.chmod("d", 0o700);
    tree.file("f");
    tree.chmod("f", 0o600);
    let m = mistrust_under(&tree);

    m.verifier()
        .require_file()
        .check(tree.canonical_path("f"))
        .expect("a 0600 file satisfies require_file");

    let err = m
        .verifier()
        .require_file()
        .check(tree.canonical_path("d"))
        .expect_err("a directory does not satisfy require_file");
    match &err {
        Error::BadType(p) => assert_eq!(p.as_path(), tree.canonical_path("d")),
        other => panic!("expected BadType, got {:?}", other),
    }
}

#[test]
fn fsmverify__require_directory_rejects_a_file() {
    let tree = Tree::new();
    tree.dir("d");
    tree.chmod("d", 0o700);
    tree.file("f");
    tree.chmod("f", 0o600);
    let m = mistrust_under(&tree);

    m.verifier()
        .require_directory()
        .check(tree.canonical_path("d"))
        .expect("a 0700 directory satisfies require_directory");

    let err = m
        .verifier()
        .require_directory()
        .check(tree.canonical_path("f"))
        .expect_err("a file does not satisfy require_directory");
    match &err {
        Error::BadType(p) => assert_eq!(p.as_path(), tree.canonical_path("f")),
        other => panic!("expected BadType, got {:?}", other),
    }
}

#[test]
fn fsmverify__permit_all_object_types_overrides_an_earlier_type_requirement() {
    let tree = Tree::new();
    tree.dir("d");
    tree.chmod("d", 0o700);
    tree.file("f");
    tree.chmod("f", 0o600);
    let m = mistrust_under(&tree);

    // The type requirement is whichever one was set last.
    m.verifier()
        .require_file()
        .permit_all_object_types()
        .check(tree.canonical_path("d"))
        .expect("permit_all_object_types should override require_file");
    m.verifier()
        .permit_all_object_types()
        .check(tree.canonical_path("f"))
        .expect("permit_all_object_types accepts a file too");

    let err = m
        .verifier()
        .permit_all_object_types()
        .require_file()
        .check(tree.canonical_path("d"))
        .expect_err("require_file should override permit_all_object_types");
    assert!(matches!(err, Error::BadType(_)), "got {:?}", err);
}

#[test]
fn fsmverify__all_errors_collects_every_problem_on_the_path() {
    let tree = Tree::new();
    tree.dir("x/y/z");
    tree.chmod("x", 0o777);
    tree.chmod("x/y", 0o777);
    tree.chmod("x/y/z", 0o700);
    let m = mistrust_under(&tree);

    let err = m
        .verifier()
        .require_directory()
        .all_errors()
        .check(tree.canonical_path("x/y/z"))
        .expect_err("two ancestors are world-writable");
    assert!(matches!(err, Error::Multiple(_)), "got {:?}", err);
    assert_eq!(err.to_string(), "Multiple errors found");
    assert_eq!(err.path(), None, "a Multiple error names no single path");

    let collected: Vec<&Error> = err.errors().collect();
    assert_eq!(collected.len(), 2);
    assert_bad_permission(collected[0], &tree.canonical_path("x"), 0o022);
    assert_bad_permission(collected[1], &tree.canonical_path("x/y"), 0o022);

    // Without `all_errors`, only the outermost problem is reported.
    let err = m
        .verifier()
        .require_directory()
        .check(tree.canonical_path("x/y/z"))
        .expect_err("the same tree is still bad");
    assert_bad_permission(&err, &tree.canonical_path("x"), 0o022);
    assert_eq!(err.errors().count(), 1);
}

#[test]
fn fsmverify__check_content_inspects_the_members_of_the_directory() {
    let tree = Tree::new();
    tree.dir("c");
    tree.chmod("c", 0o700);
    tree.file("c/f");
    tree.chmod("c/f", 0o666);
    let m = mistrust_under(&tree);

    // The directory itself is fine...
    m.verifier()
        .require_directory()
        .check(tree.canonical_path("c"))
        .expect("the directory itself is 0700");

    // ...but one of its members is not.
    let err = m
        .verifier()
        .check_content()
        .check(tree.canonical_path("c"))
        .expect_err("a 0666 member must be reported");
    assert_eq!(err.to_string(), "Invalid directory content");
    match &err {
        Error::Content(inner) => {
            assert_bad_permission(inner, &tree.canonical_path("c/f"), 0o066);
        }
        other => panic!("expected Content, got {:?}", other),
    }
    // A content error reports the path of the member that caused it.
    assert_eq!(err.path(), Some(tree.canonical_path("c/f").as_path()));
}
