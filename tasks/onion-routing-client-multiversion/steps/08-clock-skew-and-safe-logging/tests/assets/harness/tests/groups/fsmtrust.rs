//! Group `fsmtrust`: who counts as trusted when the filesystem-permission
//! check runs -- the trusted group, the trusted user, the ignored prefix, and
//! the escape hatch that trusts everyone.
//!
//! Traces to instruction.md section 1: "filesystem-permission checking".
//! Interface: `fs_mistrust::Mistrust::{new, trust_no_group_id, trust_group_id,
//! trust_admin_only, ignore_prefix, dangerously_trust_everyone}`.

use crate::common::{assert_bad_permission, gid_of, mistrust_ignoring, mistrust_under, uid_of, Tree};
use fs_mistrust::{Error, Mistrust};

#[test]
fn fsmtrust__trust_no_group_id_rejects_a_group_writable_target() {
    let tree = Tree::new();
    tree.dir("g");
    tree.chmod("g", 0o770);

    let err = mistrust_under(&tree)
        .check_directory(tree.canonical_path("g"))
        .expect_err("no group is trusted, so group access is forbidden");
    assert_bad_permission(&err, &tree.canonical_path("g"), 0o070);
}

#[test]
fn fsmtrust__trust_group_id_permits_that_group() {
    let tree = Tree::new();
    tree.dir("g");
    tree.chmod("g", 0o770);
    let gid = gid_of(&tree.canonical_path("g"));

    let mut m = Mistrust::new();
    m.trust_group_id(gid);
    m.ignore_prefix(tree.canonical_root())
        .expect("could not ignore the fixture root");
    m.check_directory(tree.canonical_path("g"))
        .expect("the owning group is trusted, so 0770 is acceptable");
}

#[test]
fn fsmtrust__trust_group_id_does_not_extend_to_everyone() {
    let tree = Tree::new();
    tree.dir("g");
    tree.chmod("g", 0o777);
    let gid = gid_of(&tree.canonical_path("g"));

    let mut m = Mistrust::new();
    m.trust_group_id(gid);
    m.ignore_prefix(tree.canonical_root())
        .expect("could not ignore the fixture root");
    let err = m
        .check_directory(tree.canonical_path("g"))
        .expect_err("trusting a group does not trust the rest of the world");
    // Only the "other" bits remain forbidden.
    assert_bad_permission(&err, &tree.canonical_path("g"), 0o007);
}

#[test]
fn fsmtrust__trust_admin_only_distrusts_the_calling_user() {
    let tree = Tree::new();
    tree.dir("o");
    tree.chmod("o", 0o700);
    let uid = uid_of(&tree.canonical_path("o"));

    let mut m = Mistrust::new();
    m.trust_admin_only();
    m.ignore_prefix(tree.canonical_root())
        .expect("could not ignore the fixture root");
    let err = m
        .check_directory(tree.canonical_path("o"))
        .expect_err("only root is trusted, and this fixture is not owned by root");
    match &err {
        Error::BadOwner(p, u) => {
            assert_eq!(p.as_path(), tree.canonical_path("o"));
            assert_eq!(*u, uid);
        }
        other => panic!("expected BadOwner, got {:?}", other),
    }
    assert_eq!(
        err.to_string(),
        format!(
            "Bad owner (UID {}) on file or directory {}",
            uid,
            tree.canonical_path("o").display()
        )
    );
}

#[test]
fn fsmtrust__ignore_prefix_skips_the_named_directory_and_its_ancestors() {
    let tree = Tree::new();
    tree.dir("x/y");
    tree.chmod("x", 0o777);
    tree.chmod("x/y", 0o700);

    // With `x` itself ignored, its permissions do not matter.
    mistrust_ignoring(&tree, "x")
        .check_directory(tree.canonical_path("x/y"))
        .expect("an ignored prefix is not inspected");

    // With only the root ignored, `x` is inspected and rejected.
    let err = mistrust_under(&tree)
        .check_directory(tree.canonical_path("x/y"))
        .expect_err("x is world-writable and is not ignored here");
    assert_bad_permission(&err, &tree.canonical_path("x"), 0o022);
}

#[test]
fn fsmtrust__ignore_prefix_rejects_a_path_that_does_not_exist() {
    let tree = Tree::new();
    let missing = tree.path("nowhere");

    let mut m = Mistrust::new();
    let err = m
        .ignore_prefix(&missing)
        .expect_err("an ignored prefix must exist");
    match &err {
        Error::NotFound(p) => assert_eq!(p.as_path(), missing.as_path()),
        other => panic!("expected NotFound, got {:?}", other),
    }
}

#[test]
fn fsmtrust__dangerously_trust_everyone_keeps_only_the_type_check() {
    let tree = Tree::new();
    tree.dir("w/v");
    tree.chmod("w", 0o777);
    tree.chmod("w/v", 0o777);
    tree.file("w/v/f");
    tree.chmod("w/v/f", 0o666);

    let mut m = Mistrust::new();
    m.dangerously_trust_everyone();
    m.check_directory(tree.canonical_path("w/v"))
        .expect("permission checking is disabled");

    // Object types are still enforced.
    let err = m
        .check_directory(tree.canonical_path("w/v/f"))
        .expect_err("a file is still not a directory");
    match &err {
        Error::BadType(p) => assert_eq!(p.as_path(), tree.canonical_path("w/v/f")),
        other => panic!("expected BadType, got {:?}", other),
    }
}
