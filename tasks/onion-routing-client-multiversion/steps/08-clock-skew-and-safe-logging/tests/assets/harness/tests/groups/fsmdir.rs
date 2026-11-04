//! Group `fsmdir`: creating state directories with private permissions, and
//! working inside one through a checked handle.
//!
//! Traces to instruction.md section 1: "filesystem-permission checking" --
//! the directories arti creates for its own state have to be created private
//! and stay private.  Interface: `fs_mistrust::Mistrust::make_directory`,
//! `fs_mistrust::Verifier::{make_directory, secure_dir}` and
//! `fs_mistrust::CheckedDir`.

use crate::common::{assert_bad_permission, mistrust_under, mode_of, Tree};
use fs_mistrust::Error;
use std::fs::OpenOptions;
use std::os::unix::fs::OpenOptionsExt;

#[test]
fn fsmdir__make_directory_creates_a_private_directory() {
    let tree = Tree::new();
    let m = mistrust_under(&tree);

    assert!(!tree.exists("new"));
    m.make_directory(tree.canonical_path("new"))
        .expect("could not create the directory");
    assert!(tree.exists("new"));
    assert_eq!(tree.mode("new"), 0o700, "a created directory must be 0700");
}

#[test]
fn fsmdir__make_directory_creates_every_missing_parent() {
    let tree = Tree::new();
    let m = mistrust_under(&tree);

    m.make_directory(tree.canonical_path("a/b/c"))
        .expect("could not create a nested directory");
    for rel in ["a", "a/b", "a/b/c"] {
        assert!(tree.exists(rel), "{} was not created", rel);
        assert_eq!(tree.mode(rel), 0o700, "{} has the wrong mode", rel);
    }
}

#[test]
fn fsmdir__make_directory_rechecks_a_directory_that_already_exists() {
    let tree = Tree::new();
    tree.dir("d");
    tree.chmod("d", 0o700);
    let m = mistrust_under(&tree);

    // Creating an acceptable directory that already exists is a no-op.
    m.make_directory(tree.canonical_path("d"))
        .expect("an existing 0700 directory is acceptable");

    // But an existing directory with bad permissions is still an error.
    tree.chmod("d", 0o777);
    let err = m
        .make_directory(tree.canonical_path("d"))
        .expect_err("an existing 0777 directory must be rejected");
    assert_bad_permission(&err, &tree.canonical_path("d"), 0o077);
}

#[test]
fn fsmdir__make_directory_refuses_to_build_under_an_insecure_parent() {
    let tree = Tree::new();
    tree.dir("p");
    tree.chmod("p", 0o777);

    let err = mistrust_under(&tree)
        .make_directory(tree.canonical_path("p/sub"))
        .expect_err("the parent is world-writable");
    assert_bad_permission(&err, &tree.canonical_path("p"), 0o022);
    assert!(
        !tree.exists("p/sub"),
        "nothing may be created under a rejected parent"
    );
}

#[test]
fn fsmdir__a_checked_directory_remembers_where_it_is() {
    let tree = Tree::new();
    tree.dir("s");
    tree.chmod("s", 0o700);
    let m = mistrust_under(&tree);

    let checked = m
        .verifier()
        .secure_dir(tree.canonical_path("s"))
        .expect("a 0700 directory can be secured");
    assert_eq!(checked.as_path(), tree.canonical_path("s"));

    checked
        .make_directory("sub")
        .expect("could not create a subdirectory");
    assert!(tree.exists("s/sub"));
    assert_eq!(tree.mode("s/sub"), 0o700);

    // A directory that is not private cannot be secured at all.
    tree.dir("t");
    tree.chmod("t", 0o777);
    match m.verifier().secure_dir(tree.canonical_path("t")) {
        Ok(_) => panic!("a 0777 directory cannot be secured"),
        Err(err) => assert_bad_permission(&err, &tree.canonical_path("t"), 0o077),
    }
}

#[test]
fn fsmdir__files_opened_in_a_checked_directory_are_private() {
    let tree = Tree::new();
    tree.dir("s");
    tree.chmod("s", 0o700);
    tree.file("s/exposed");
    tree.chmod("s/exposed", 0o666);
    let m = mistrust_under(&tree);
    let checked = m
        .verifier()
        .secure_dir(tree.canonical_path("s"))
        .expect("a 0700 directory can be secured");

    // A newly created file is private no matter what the caller asked for.
    let f = checked
        .open(
            "fresh",
            OpenOptions::new().write(true).create(true).mode(0o666),
        )
        .expect("could not create a file");
    drop(f);
    assert_eq!(
        mode_of(&tree.canonical_path("s/fresh")),
        0o600,
        "a created file must be 0600"
    );

    // An existing file with loose permissions cannot be opened.
    let err = checked
        .open("exposed", OpenOptions::new().read(true))
        .expect_err("a 0666 file must be refused");
    assert_bad_permission(&err, &tree.canonical_path("s/exposed"), 0o066);
}

#[test]
fn fsmdir__a_checked_directory_refuses_paths_that_could_escape_it() {
    let tree = Tree::new();
    tree.dir("s");
    tree.chmod("s", 0o700);
    let m = mistrust_under(&tree);
    let checked = m
        .verifier()
        .secure_dir(tree.canonical_path("s"))
        .expect("a 0700 directory can be secured");

    for bad in ["hello/../world", "/hello", ".."] {
        let err = match checked.make_directory(bad) {
            Ok(()) => panic!("{:?} should have been refused", bad),
            Err(e) => e,
        };
        assert!(
            matches!(err, Error::InvalidSubdirectory),
            "{:?} gave {:?}",
            bad,
            err
        );
        assert_eq!(err.to_string(), "Path was not valid for use with CheckedDir.");
    }

    let err = checked
        .open("../outside", OpenOptions::new().read(true))
        .expect_err("an escaping path should be refused");
    assert!(matches!(err, Error::InvalidSubdirectory), "got {:?}", err);

    // Ordinary relative paths still work.
    checked
        .make_directory("hello/world")
        .expect("a plain relative path is fine");
    assert!(tree.exists("s/hello/world"));
}
