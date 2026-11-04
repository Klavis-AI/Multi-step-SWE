//! Inherited: filesystem-trust checking from the previous step still has to
//! work against the cumulative workspace.  Every case operates on a private
//! temporary tree whose prefix is ignored, so nothing here depends on the
//! permissions of the machine's shared directories.
//!
//! The group axis is pinned explicitly rather than left to the host: a
//! `Mistrust` built with no `trust_group` inherits `TrustedGroup::SelfNamed`
//! and consults `/etc/group`, so the same mode can be accepted on one machine
//! and rejected on another.  The cases below name the group they trust, which
//! is exactly the lever the trust predicate exposes.

#![allow(non_snake_case)]

use std::fs;
use std::os::unix::fs::{MetadataExt, PermissionsExt};
use std::path::{Path, PathBuf};

use fs_mistrust::{Error, Mistrust, TrustedGroup};

/// A temporary tree plus a `Mistrust` that ignores everything above it.
struct Tree {
    _dir: tempfile::TempDir,
    root: PathBuf,
}

fn tree() -> (Tree, Mistrust) {
    let dir = tempfile::TempDir::new().expect("a temporary directory");
    let root = dir
        .path()
        .canonicalize()
        .expect("the temporary directory resolves");
    fs::set_permissions(&root, fs::Permissions::from_mode(0o700)).expect("chmod the root");
    let m = Mistrust::builder()
        .ignore_prefix(root.clone())
        .build()
        .expect("a Mistrust that ignores the temporary prefix");
    (Tree { _dir: dir, root }, m)
}

/// The group that owns `root`, as a number.  Reading it from the tree itself
/// keeps every case below independent of `/etc/group`.
fn owning_gid(root: &Path) -> u32 {
    fs::metadata(root)
        .expect("stat the temporary tree")
        .gid()
}

/// Create `name` under `t` with mode `mode`, and return its path.
fn mkdir(t: &Tree, name: &str, mode: u32) -> PathBuf {
    let p = t.root.join(name);
    fs::create_dir(&p).expect("create the directory");
    fs::set_permissions(&p, fs::Permissions::from_mode(mode)).expect("chmod the directory");
    p
}

fn mkfile(t: &Tree, name: &str, mode: u32) -> PathBuf {
    let p = t.root.join(name);
    fs::write(&p, b"conformance").expect("write the file");
    fs::set_permissions(&p, fs::Permissions::from_mode(mode)).expect("chmod the file");
    p
}

/// The path an error blames, as an owned value.
fn blamed(e: &Error) -> &Path {
    e.path().expect("this error names the offending path")
}

#[test]
fn mistrustbase__a_private_directory_is_accepted() {
    let (t, m) = tree();
    let p = mkdir(&t, "private", 0o700);
    m.check_directory(&p).expect("0o700 is a trustworthy mode");

    // Tightening the group bits does not make an owned directory untrusted.
    // The instrument is built explicitly: a bare builder inherits
    // `TrustedGroup::SelfNamed`, whose meaning depends on the host's group
    // database, and this case is about the owner axis, not the group one.
    fs::set_permissions(&p, fs::Permissions::from_mode(0o750)).expect("chmod");
    let owner_only = Mistrust::builder()
        .ignore_prefix(t.root.clone())
        .trust_group(owning_gid(&t.root))
        .build()
        .expect("a Mistrust that trusts the tree's own group");
    owner_only
        .check_directory(&p)
        .expect("0o750 is still trustworthy");
}

#[test]
fn mistrustbase__a_trusted_group_may_read_the_final_directory() {
    // The mode here is the same on every host; only the *group* differs, and
    // the builder is told which one to trust.  A bare builder would instead
    // inherit `TrustedGroup::SelfNamed` and consult `/etc/group`, so 0o750 and
    // 0o640 would be accepted only where the running user has a self-named
    // group that happens to own the temporary tree.
    let (t, _default) = tree();
    let gid = owning_gid(&t.root);
    let m = Mistrust::builder()
        .ignore_prefix(t.root.clone())
        .trust_group(gid)
        .build()
        .expect("a Mistrust that trusts the tree's own group");

    let d = mkdir(&t, "group_readable", 0o750);
    assert_eq!(
        fs::metadata(&d).expect("stat").gid(),
        gid,
        "the case pins the group it trusts"
    );
    m.check_directory(&d)
        .expect("a group-trusted directory may be group-readable");

    // A group-readable file inside is fine under the same policy.
    let f = mkfile(&t, "group_readable_file", 0o640);
    m.verifier()
        .require_file()
        .check(&f)
        .expect("a group-trusted file may be group-readable");

    // Trusting the group removes the group-write bit from the forbidden mask,
    // but not the world-write bit: an intermediate directory that is
    // group-writable is still fine, while one anybody can write is not.
    let mid = mkdir(&t, "mid", 0o770);
    let leaf = mkdir(&t, "mid/leaf", 0o700);
    m.check_directory(&leaf)
        .expect("a trusted group may write an intermediate directory");

    fs::set_permissions(&mid, fs::Permissions::from_mode(0o777)).expect("chmod");
    let e = m
        .check_directory(&leaf)
        .expect_err("nobody trusts a world-writable parent");
    assert!(e.is_bad_permission(), "got {:?}", e);
    assert_eq!(blamed(&e), mid.as_path());
    // The offending-bits field is deliberately NOT asserted here: it is the
    // third field this step adds to `Error::BadPermission`, so reading it would
    // put new capability behind a gate group. It is priced by `mistrustbits`.
}

#[test]
fn mistrustbase__an_untrusted_group_may_not_read_the_final_directory() {
    // The same 0o750 and 0o640 the case above accepts are rejected once the
    // group is not trusted.  Together the two cases show the verdict is set by
    // `trust_group` and not by whatever group database the host carries.
    let (t, _default) = tree();
    let m = Mistrust::builder()
        .ignore_prefix(t.root.clone())
        .trust_group(TrustedGroup::None)
        .build()
        .expect("a Mistrust that trusts no group");

    let d = mkdir(&t, "group_readable", 0o750);
    let e = m
        .check_directory(&d)
        .expect_err("an untrusted group may not read the directory");
    assert!(e.is_bad_permission(), "got {:?}", e);
    assert_eq!(blamed(&e), d.as_path());
    // The offending-bits field is deliberately NOT asserted here: it is the
    // third field this step adds to `Error::BadPermission`, so reading it would
    // put new capability behind a gate group. It is priced by `mistrustbits`.

    let f = mkfile(&t, "group_readable_file", 0o640);
    let e = m
        .verifier()
        .require_file()
        .check(&f)
        .expect_err("an untrusted group may not read the file either");
    assert!(e.is_bad_permission(), "got {:?}", e);
    assert_eq!(blamed(&e), f.as_path());

    // The owner-only mode is accepted under both policies.
    let p = mkdir(&t, "private", 0o700);
    fs::set_permissions(&p, fs::Permissions::from_mode(0o600)).expect("chmod");
    m.verifier()
        .require_directory()
        .check(&p)
        .expect("0o700 never depends on the group");
}

#[test]
fn mistrustbase__a_group_or_world_writable_directory_is_rejected() {
    let (t, m) = tree();
    let p = mkdir(&t, "loose", 0o777);
    let e = m.check_directory(&p).expect_err("0o777 must be rejected");
    assert!(e.is_bad_permission(), "got {:?}", e);
    assert_eq!(blamed(&e), p.as_path());

    // Only the "other" write bit is unambiguously untrusted: a self-named
    // group may legitimately be trusted, so this case does not rely on it.
    let q = mkdir(&t, "other_writable", 0o702);
    let e = m
        .check_directory(&q)
        .expect_err("world-writable must be rejected too");
    assert!(e.is_bad_permission(), "got {:?}", e);
    assert_eq!(blamed(&e), q.as_path());
}

#[test]
fn mistrustbase__trusting_everyone_disables_the_check() {
    let (t, _m) = tree();
    let p = mkdir(&t, "loose", 0o777);
    let trusting = Mistrust::new_dangerously_trust_everyone();
    trusting
        .check_directory(&p)
        .expect("a trust-everyone Mistrust accepts any mode");

    let mut built = Mistrust::builder();
    built.ignore_prefix(t.root.clone());
    built.dangerously_trust_everyone();
    built
        .build()
        .expect("build")
        .check_directory(&p)
        .expect("the builder form disables the check as well");
}

#[test]
fn mistrustbase__the_required_object_type_is_enforced() {
    let (t, m) = tree();
    let d = mkdir(&t, "adir", 0o700);
    let f = mkfile(&t, "afile", 0o600);

    let e = m
        .verifier()
        .require_file()
        .check(&d)
        .expect_err("a directory is not a file");
    assert!(matches!(e, Error::BadType(_)), "got {:?}", e);
    assert_eq!(blamed(&e), d.as_path());

    let e = m
        .verifier()
        .require_directory()
        .check(&f)
        .expect_err("a file is not a directory");
    assert!(matches!(e, Error::BadType(_)), "got {:?}", e);
    assert_eq!(blamed(&e), f.as_path());

    m.verifier().require_file().check(&f).expect("file is a file");
    m.verifier()
        .require_directory()
        .check(&d)
        .expect("dir is a dir");
}

#[test]
fn mistrustbase__a_missing_path_is_reported_as_not_found() {
    let (t, m) = tree();
    let missing = t.root.join("no-such-directory");
    let e = m
        .check_directory(&missing)
        .expect_err("a path that is not there cannot be trusted");
    assert!(matches!(e, Error::NotFound(_)), "got {:?}", e);
    assert_eq!(blamed(&e), missing.as_path());
    assert!(!e.is_bad_permission());
}

#[test]
fn mistrustbase__make_directory_creates_a_private_directory_idempotently() {
    let (t, m) = tree();
    let p = t.root.join("state");
    m.make_directory(&p).expect("create it");
    let mode = fs::metadata(&p).expect("stat").permissions().mode() & 0o777;
    assert_eq!(mode, 0o700, "a directory arti makes must be owner-only");
    m.make_directory(&p).expect("creating it again succeeds");
    m.check_directory(&p).expect("and it passes its own check");
}

#[test]
fn mistrustbase__a_checked_dir_refuses_to_escape_itself() {
    let (t, m) = tree();
    let cd = m
        .verifier()
        .make_secure_dir(t.root.join("secure"))
        .expect("a CheckedDir");
    assert_eq!(cd.as_path(), t.root.join("secure").as_path());

    assert_eq!(
        cd.join("inner.json").expect("a plain relative name is fine"),
        t.root.join("secure").join("inner.json")
    );
    for bad in ["..", "../escape", "/etc/passwd"] {
        let e = cd
            .join(bad)
            .err()
            .unwrap_or_else(|| panic!("{:?} should not be joinable", bad));
        assert!(matches!(e, Error::InvalidSubdirectory), "got {:?}", e);
    }

    cd.write_and_replace("inner.json", b"{}")
        .expect("write through the CheckedDir");
    assert_eq!(
        cd.read_to_string("inner.json").expect("read it back"),
        "{}"
    );
}
