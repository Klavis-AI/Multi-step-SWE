//! `fs_mistrust::Error`: which variant a given filesystem problem produces,
//! what `path()` and `is_bad_permission()` say about it, and how the offending
//! mode bits are rendered.
#![allow(non_snake_case)]
use crate::common::{Tree, BAD_DIR_WORLD_W, BAD_FILE_WORLD_R, GOOD_DIR};
use fs_mistrust::{Error, Mistrust, TrustedGroup, TrustedUser};
use std::path::Path;

fn strict(root: &Path) -> Mistrust {
    let mut b = Mistrust::builder();
    b.ignore_prefix(root)
        .trust_user(TrustedUser::Current)
        .trust_group(TrustedGroup::None);
    b.build().unwrap()
}

#[test]
fn mistrusterr__absent_path_is_not_found_and_names_it() {
    let t = Tree::new();
    let missing = t.path("nowhere");
    let e = strict(t.root()).verifier().check(&missing).unwrap_err();
    assert!(matches!(e, Error::NotFound(_)), "{e:?}");
    assert_eq!(e.path(), Some(missing.as_path()));
}

#[test]
fn mistrusterr__absent_path_is_not_a_permission_problem() {
    // `is_bad_permission` distinguishes "we don't like the mode/owner" from
    // "we could not look".
    let t = Tree::new();
    let e = strict(t.root())
        .verifier()
        .check(t.path("nowhere"))
        .unwrap_err();
    assert!(!e.is_bad_permission());

    let bad = t.dir("open", BAD_DIR_WORLD_W);
    let e2 = strict(t.root()).verifier().check(&bad).unwrap_err();
    assert!(e2.is_bad_permission());
}

#[test]
fn mistrusterr__final_object_reports_every_forbidden_bit() {
    // For the object we actually asked about, group and world r/w/x are all
    // forbidden, so a 0o707 directory reports 0o007.
    let t = Tree::new();
    let d = t.dir("open", BAD_DIR_WORLD_W);
    let e = strict(t.root()).verifier().check(&d).unwrap_err();
    match e {
        Error::BadPermission(p, bits) => {
            assert_eq!(p, d);
            assert_eq!(bits, 0o007);
        }
        other => panic!("unexpected error: {other:?}"),
    }
}

#[test]
fn mistrusterr__intermediate_directory_reports_only_write_bits() {
    // For an ancestor, only the group/world *write* bits matter: a 0o707
    // parent of a 0o700 target reports 0o002, not 0o007.
    let t = Tree::new();
    let open = t.dir("open", BAD_DIR_WORLD_W);
    let inner = t.dir("open/inner", GOOD_DIR);
    let e = strict(t.root()).verifier().check(&inner).unwrap_err();
    match e {
        Error::BadPermission(p, bits) => {
            assert_eq!(p, open);
            assert_eq!(bits, 0o002);
        }
        other => panic!("unexpected error: {other:?}"),
    }
}

#[test]
fn mistrusterr__display_spells_out_the_access_bits() {
    let t = Tree::new();
    let d = t.dir("open", BAD_DIR_WORLD_W);
    let msg = strict(t.root())
        .verifier()
        .check(&d)
        .unwrap_err()
        .to_string();
    assert!(msg.contains(&d.display().to_string()), "{msg}");
    // 0o007 is "other" with read, write and execute.
    assert!(msg.contains("o=rwx"), "{msg}");
    assert!(!msg.contains("u="), "{msg}");

    let f = t.file("readable", b"x", BAD_FILE_WORLD_R);
    let msg2 = strict(t.root())
        .verifier()
        .check(&f)
        .unwrap_err()
        .to_string();
    assert!(msg2.contains("o=r"), "{msg2}");
}

#[test]
fn mistrusterr__require_directory_rejects_a_file() {
    let t = Tree::new();
    let f = t.file("plain", b"hello", 0o600);
    let e = strict(t.root())
        .verifier()
        .require_directory()
        .check(&f)
        .unwrap_err();
    assert!(matches!(e, Error::BadType(_)), "{e:?}");
    assert_eq!(e.path(), Some(f.as_path()));
    assert!(e.is_bad_permission());
}

#[test]
fn mistrusterr__require_file_rejects_a_directory() {
    let t = Tree::new();
    let d = t.dir("adir", GOOD_DIR);
    let e = strict(t.root())
        .verifier()
        .require_file()
        .check(&d)
        .unwrap_err();
    assert!(matches!(e, Error::BadType(_)), "{e:?}");
    assert_eq!(e.path(), Some(d.as_path()));
}

#[test]
fn mistrusterr__all_errors_collects_each_problem() {
    // Two world-writable ancestors plus a world-writable target: with
    // `all_errors` every one of them is reported.
    let t = Tree::new();
    let a = t.dir("a", BAD_DIR_WORLD_W);
    let b = t.dir("a/b", BAD_DIR_WORLD_W);
    let c = t.dir("a/b/c", BAD_DIR_WORLD_W);

    let e = strict(t.root())
        .verifier()
        .all_errors()
        .check(&c)
        .unwrap_err();
    assert!(matches!(e, Error::Multiple(_)), "{e:?}");
    assert_eq!(e.path(), None);
    assert!(e.is_bad_permission());

    let paths: Vec<_> = e.errors().filter_map(|e| e.path()).collect();
    assert_eq!(paths, vec![a.as_path(), b.as_path(), c.as_path()]);
    assert_eq!(e.errors().count(), 3);
}

#[test]
fn mistrusterr__errors_on_a_single_error_yields_itself() {
    let t = Tree::new();
    let d = t.dir("open", BAD_DIR_WORLD_W);
    let e = strict(t.root()).verifier().check(&d).unwrap_err();
    let collected: Vec<_> = e.errors().collect();
    assert_eq!(collected.len(), 1);
    assert_eq!(collected[0].path(), Some(d.as_path()));
}

#[test]
fn mistrusterr__check_content_wraps_the_inner_problem() {
    // The directory itself is fine; a file inside it is world-writable.
    // (Content is allowed to be world-*readable*; only the write bits are
    // forbidden below the checked directory.)
    let t = Tree::new();
    let d = t.dir("cdir", GOOD_DIR);
    let f = t.file("cdir/writable", b"secret", 0o606);
    t.file("cdir/harmless", b"public", BAD_FILE_WORLD_R);

    strict(t.root()).verifier().check(&d).unwrap();

    let e = strict(t.root())
        .verifier()
        .check_content()
        .check(&d)
        .unwrap_err();
    match &e {
        Error::Content(inner) => {
            assert!(matches!(**inner, Error::BadPermission(_, _)), "{inner:?}");
        }
        other => panic!("unexpected error: {other:?}"),
    }
    // `path` and `is_bad_permission` both delegate through the wrapper.
    assert_eq!(e.path(), Some(f.as_path()));
    assert!(e.is_bad_permission());
}
