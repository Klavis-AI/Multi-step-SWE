//! Group `mistrustbits` (new) — the offending-bits field this step adds to
//! `fs_mistrust::Error::BadPermission`.
//!
//! At the previous milestone the variant carried `(PathBuf, u32)`: the path and
//! the mode that was rejected. This step widens it to `(PathBuf, u32, u32)`,
//! where the third field reports *which* bits of that mode were the problem —
//! the mode masked by the bits the configured policy forbids.
//!
//! That widening is this step's capability, so it is scored here rather than
//! asserted by the inherited `mistrustbase` gate group. A gate group that read
//! the third field would veto every candidate that had not yet made the change,
//! while paying nothing for making it.
//!
//! Determinism: each case pins the trusted group explicitly rather than letting
//! the builder fall back to `TrustedGroup::SelfNamed`, which consults
//! `/etc/group` and would make the forbidden mask depend on the host's group
//! database. Modes are set with an explicit `chmod`, so the process umask does
//! not reach them. No clock, no network, no iteration order.

use std::fs;
use std::os::unix::fs::{MetadataExt, PermissionsExt};
use std::path::{Path, PathBuf};

use fs_mistrust::{Error, Mistrust, TrustedGroup};

/// A temporary tree plus the prefix-ignoring builder every case starts from.
struct Tree {
    _dir: tempfile::TempDir,
    root: PathBuf,
}

fn tree() -> Tree {
    let dir = tempfile::TempDir::new().expect("a temporary directory");
    let root = dir
        .path()
        .canonicalize()
        .expect("the temporary directory resolves");
    fs::set_permissions(&root, fs::Permissions::from_mode(0o700)).expect("chmod the root");
    Tree { _dir: dir, root }
}

/// The group that owns `root`, read from the tree itself so that no case
/// depends on `/etc/group`.
fn owning_gid(root: &Path) -> u32 {
    fs::metadata(root).expect("stat the temporary tree").gid()
}

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

/// A `Mistrust` over `t` that trusts no group at all, so the forbidden mask is
/// the full `0o077` and every expected value below is fixed by the mode alone.
fn trusting_no_group(t: &Tree) -> Mistrust {
    Mistrust::builder()
        .ignore_prefix(t.root.clone())
        .trust_group(TrustedGroup::None)
        .build()
        .expect("a Mistrust that trusts no group")
}

#[test]
fn mistrustbits__an_untrusted_group_readable_directory_reports_the_group_bits() {
    let t = tree();
    let m = trusting_no_group(&t);

    let d = mkdir(&t, "group_readable", 0o750);
    let e = m
        .check_directory(&d)
        .expect_err("an untrusted group may not read the directory");
    // 0o750 masked by the forbidden 0o077: group read and execute survive,
    // group-write was never set, and the owner bits are never at fault.
    assert!(
        matches!(e, Error::BadPermission(_, 0o750, 0o050)),
        "got {:?}",
        e
    );
}

#[test]
fn mistrustbits__an_untrusted_group_readable_file_reports_the_group_read_bit() {
    let t = tree();
    let m = trusting_no_group(&t);

    let f = mkfile(&t, "group_readable_file", 0o640);
    let e = m
        .verifier()
        .require_file()
        .check(&f)
        .expect_err("an untrusted group may not read the file");
    assert!(
        matches!(e, Error::BadPermission(_, 0o640, 0o040)),
        "got {:?}",
        e
    );
}

#[test]
fn mistrustbits__a_world_writable_directory_reports_the_world_write_bit_alone() {
    let t = tree();
    let m = trusting_no_group(&t);

    let p = mkdir(&t, "other_writable", 0o702);
    let e = m
        .check_directory(&p)
        .expect_err("world-writable must be rejected");
    assert!(
        matches!(e, Error::BadPermission(_, 0o702, 0o002)),
        "got {:?}",
        e
    );
}

#[test]
fn mistrustbits__trusting_a_group_narrows_the_bits_it_reports() {
    // The reported bits follow the *policy*, not just the mode: the same
    // 0o777 parent is at fault only for its world-write bit once the owning
    // group is trusted. This is what distinguishes the new field from a plain
    // copy of the mode.
    let t = tree();
    let gid = owning_gid(&t.root);
    let m = Mistrust::builder()
        .ignore_prefix(t.root.clone())
        .trust_group(gid)
        .build()
        .expect("a Mistrust that trusts the tree's own group");

    let mid = mkdir(&t, "mid", 0o700);
    let leaf = mkdir(&t, "mid/leaf", 0o700);
    fs::set_permissions(&mid, fs::Permissions::from_mode(0o777)).expect("chmod");

    let e = m
        .check_directory(&leaf)
        .expect_err("nobody trusts a world-writable parent");
    assert!(
        matches!(e, Error::BadPermission(_, 0o777, 0o002)),
        "got {:?}",
        e
    );
}
