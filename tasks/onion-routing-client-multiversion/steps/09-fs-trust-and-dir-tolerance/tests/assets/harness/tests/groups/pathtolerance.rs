//! Which permission bits `fs_mistrust` tolerates, and where.
//!
//! The rule the crate implements is asymmetric: the object you asked about may
//! not be reachable by anybody else at all, while its ancestors and its
//! contents only have to be un-*writable*.  These cases pin that asymmetry, the
//! sticky-bit exemption, and the `permit_readable` relaxation.  (`Verifier`'s
//! other relaxation, `permit_all_object_types`, is documented but has no
//! behaviour stated for it at this step, so no case here pins it.)
//!
//! Every case pins `trust_user`/`trust_group`, because the defaults resolve
//! against the running account's entry in the user database.
#![allow(non_snake_case)]
use crate::common::{chmod, Tree, BAD_DIR_WORLD_W, GOOD_DIR};
use fs_mistrust::{Error, Mistrust};
use std::path::Path;

fn strict(root: &Path) -> Mistrust {
    let mut b = Mistrust::builder();
    b.ignore_prefix(root)
        .trust_user(fs_mistrust::TrustedUser::Current)
        .trust_group(fs_mistrust::TrustedGroup::None);
    b.build().unwrap()
}

#[test]
fn pathtolerance__private_objects_are_accepted() {
    let t = Tree::new();
    let m = strict(t.root());
    let d = t.dir("good", GOOD_DIR);
    m.verifier().check(&d).unwrap();
    let f = t.file("good/f", b"x", 0o600);
    m.verifier().check(&f).unwrap();
    // The convenience wrapper agrees with the verifier.
    m.check_directory(&d).unwrap();
}

#[test]
fn pathtolerance__final_object_forbids_group_and_world_access() {
    // Not just write: for the object we asked about, *any* group or other
    // access bit is forbidden.
    let t = Tree::new();
    let m = strict(t.root());

    let g = t.dir("g", 0o740);
    match m.verifier().check(&g).unwrap_err() {
        Error::BadPermission(p, bits) => {
            assert_eq!(p, g);
            assert_eq!(bits, 0o040);
        }
        other => panic!("unexpected: {other:?}"),
    }

    let o = t.dir("o", 0o704);
    match m.verifier().check(&o).unwrap_err() {
        Error::BadPermission(p, bits) => {
            assert_eq!(p, o);
            assert_eq!(bits, 0o004);
        }
        other => panic!("unexpected: {other:?}"),
    }
}

#[test]
fn pathtolerance__ancestor_may_be_readable_but_not_writable() {
    // A world-readable, world-searchable parent is fine; adding the write bit
    // is not, and the error names the parent rather than the target.
    let t = Tree::new();
    let parent = t.dir("parent", 0o755);
    let target = t.dir("parent/target", GOOD_DIR);
    strict(t.root()).verifier().check(&target).unwrap();

    chmod(&parent, 0o757);
    match strict(t.root()).verifier().check(&target).unwrap_err() {
        Error::BadPermission(p, bits) => {
            assert_eq!(p, parent);
            assert_eq!(bits, 0o002);
        }
        other => panic!("unexpected: {other:?}"),
    }
}

#[test]
fn pathtolerance__sticky_intermediate_directory_is_tolerated() {
    // A world-writable ancestor is acceptable when the sticky bit is set,
    // because then only the owner of an entry can rename or remove it.
    let t = Tree::new();
    let parent = t.dir("shared", 0o1777);
    let target = t.dir("shared/target", GOOD_DIR);
    strict(t.root()).verifier().check(&target).unwrap();

    // Clearing the sticky bit, and nothing else, makes the same tree bad.
    chmod(&parent, 0o777);
    match strict(t.root()).verifier().check(&target).unwrap_err() {
        Error::BadPermission(p, bits) => {
            assert_eq!(p, parent);
            assert_eq!(bits, 0o022);
        }
        other => panic!("unexpected: {other:?}"),
    }
}

#[test]
fn pathtolerance__sticky_bit_does_not_excuse_the_final_object() {
    // The exemption is for intermediate directories only.
    let t = Tree::new();
    let d = t.dir("shared", 0o1777);
    match strict(t.root()).verifier().check(&d).unwrap_err() {
        Error::BadPermission(p, bits) => {
            assert_eq!(p, d);
            assert_eq!(bits, 0o077);
        }
        other => panic!("unexpected: {other:?}"),
    }
}

#[test]
fn pathtolerance__permit_readable_allows_reading_but_not_writing() {
    let t = Tree::new();
    let f = t.file("f", b"x", 0o604);

    // By default a world-readable target is rejected ...
    match strict(t.root()).verifier().check(&f).unwrap_err() {
        Error::BadPermission(p, bits) => {
            assert_eq!(p, f);
            assert_eq!(bits, 0o004);
        }
        other => panic!("unexpected: {other:?}"),
    }
    // ... and `permit_readable` accepts exactly that case.
    strict(t.root()).verifier().permit_readable().check(&f).unwrap();

    // It does not, however, relax the write bits.
    chmod(&f, 0o606);
    match strict(t.root())
        .verifier()
        .permit_readable()
        .check(&f)
        .unwrap_err()
    {
        Error::BadPermission(p, bits) => {
            assert_eq!(p, f);
            assert_eq!(bits, 0o002);
        }
        other => panic!("unexpected: {other:?}"),
    }
}

#[test]
fn pathtolerance__content_is_allowed_to_be_readable_but_not_writable() {
    // Below a checked directory the rule is the ancestor rule, not the
    // final-object rule: readable content is fine, writable content is not.
    let t = Tree::new();
    let d = t.dir("parent", GOOD_DIR);
    t.file("parent/readable", b"x", 0o644);
    strict(t.root()).verifier().check_content().check(&d).unwrap();

    let w = t.file("parent/writable", b"x", 0o622);
    let e = strict(t.root())
        .verifier()
        .check_content()
        .check(&d)
        .unwrap_err();
    assert_eq!(e.path(), Some(w.as_path()));
    match &e {
        Error::Content(inner) => {
            assert!(matches!(**inner, Error::BadPermission(_, 0o022)), "{inner:?}");
        }
        other => panic!("unexpected: {other:?}"),
    }
}

#[test]
fn pathtolerance__symlinks_are_resolved_before_the_ancestors_are_judged() {
    // A symlink's own mode is irrelevant, but the ancestors of what it points
    // at are checked: the error names the real directory, not the link.
    let t = Tree::new();
    let real = t.dir("real", GOOD_DIR);
    let f = t.file("real/f", b"x", 0o600);
    t.symlink("link", &real);
    strict(t.root())
        .verifier()
        .require_file()
        .check(t.path("link/f"))
        .unwrap();

    let open = t.dir("open", BAD_DIR_WORLD_W);
    t.dir("open/hidden", GOOD_DIR);
    let via = t.symlink("shortcut", t.path("open/hidden"));
    let e = strict(t.root()).verifier().check(&via).unwrap_err();
    assert_eq!(e.path(), Some(open.as_path()));
    assert!(e.is_bad_permission(), "{e}");
    // Sanity: the same target is fine once its parent is tightened.
    chmod(&open, GOOD_DIR);
    strict(t.root()).verifier().check(&via).unwrap();
    let _ = f;
}
