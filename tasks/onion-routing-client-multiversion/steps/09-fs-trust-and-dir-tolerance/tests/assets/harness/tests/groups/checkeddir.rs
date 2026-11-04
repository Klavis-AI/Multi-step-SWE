//! `fs_mistrust::CheckedDir`: a directory that has been verified once, plus
//! accessors that keep every member inside it and at the same trust level.
#![allow(non_snake_case)]
use crate::common::{mode_of, Tree, BAD_DIR_WORLD_W, GOOD_DIR};
use fs_mistrust::{CheckedDir, Error, Mistrust};
use std::fs::OpenOptions;
use std::os::unix::fs::OpenOptionsExt;
use std::path::Path;

fn strict(root: &Path) -> Mistrust {
    let mut b = Mistrust::builder();
    b.ignore_prefix(root)
        .trust_user(fs_mistrust::TrustedUser::Current)
        .trust_group(fs_mistrust::TrustedGroup::None);
    b.build().unwrap()
}

/// A `CheckedDir` for `<tree>/state`, created fresh.
fn checked(t: &Tree) -> CheckedDir {
    strict(t.root())
        .verifier()
        .make_secure_dir(t.path("state"))
        .unwrap()
}

#[test]
fn checkeddir__secure_dir_requires_an_existing_directory() {
    let t = Tree::new();
    let e = strict(t.root())
        .verifier()
        .secure_dir(t.path("absent"))
        .err()
        .expect("absent directory accepted");
    assert!(matches!(e, Error::NotFound(_)), "{e:?}");

    let f = t.file("plain", b"x", 0o600);
    let e = strict(t.root())
        .verifier()
        .secure_dir(&f)
        .err()
        .expect("regular file accepted as a directory");
    assert!(matches!(e, Error::BadType(_)), "{e:?}");
}

#[test]
fn checkeddir__secure_dir_rejects_an_unsafe_directory() {
    let t = Tree::new();
    let bad = t.dir("open", BAD_DIR_WORLD_W);
    let e = strict(t.root())
        .verifier()
        .secure_dir(&bad)
        .err()
        .expect("world-writable directory accepted");
    assert_eq!(e.path(), Some(bad.as_path()));
    assert!(e.is_bad_permission(), "{e}");

    // An unsafe *ancestor* is equally disqualifying.
    let nested = t.dir("open/inner", GOOD_DIR);
    let e = strict(t.root())
        .verifier()
        .secure_dir(&nested)
        .err()
        .expect("directory under a world-writable parent accepted");
    assert_eq!(e.path(), Some(bad.as_path()));
}

#[test]
fn checkeddir__make_secure_dir_creates_a_private_directory() {
    let t = Tree::new();
    let want = t.path("state");
    assert!(!want.exists());

    let cd = checked(&t);
    assert_eq!(cd.as_path(), want.as_path());
    assert!(want.is_dir());
    assert_eq!(mode_of(&want) & 0o777, 0o700);

    // Calling it again on the existing directory succeeds without changing it.
    let again = strict(t.root()).verifier().make_secure_dir(&want).unwrap();
    assert_eq!(again.as_path(), want.as_path());
    // And plain `secure_dir` now accepts it too.
    strict(t.root()).verifier().secure_dir(&want).unwrap();
}

#[test]
fn checkeddir__join_refuses_to_escape() {
    let t = Tree::new();
    let cd = checked(&t);

    assert_eq!(cd.join("a/b").unwrap(), cd.as_path().join("a").join("b"));
    assert_eq!(cd.join("x").unwrap(), cd.as_path().join("x"));

    for escape in ["..", "../sibling", "a/../../sibling"] {
        let e = cd.join(escape).err().unwrap_or_else(|| panic!("{escape} allowed"));
        assert!(matches!(e, Error::InvalidSubdirectory), "{escape}: {e:?}");
    }
    let e = cd.join("/etc").err().expect("absolute path allowed");
    assert!(matches!(e, Error::InvalidSubdirectory), "{e:?}");
}

#[test]
fn checkeddir__make_directory_stays_inside_and_is_private() {
    let t = Tree::new();
    let cd = checked(&t);

    cd.make_directory("sub").unwrap();
    let sub = cd.as_path().join("sub");
    assert!(sub.is_dir());
    assert_eq!(mode_of(&sub) & 0o777, 0o700);

    let e = cd.make_directory("../escape").unwrap_err();
    assert!(matches!(e, Error::InvalidSubdirectory), "{e:?}");
    assert!(!t.path("escape").exists());
}

#[test]
fn checkeddir__write_and_replace_round_trips() {
    let t = Tree::new();
    let cd = checked(&t);

    cd.write_and_replace("state.json", b"{\"v\":1}").unwrap();
    assert_eq!(cd.read_to_string("state.json").unwrap(), "{\"v\":1}");
    assert_eq!(cd.read("state.json").unwrap(), b"{\"v\":1}".to_vec());

    // A second write replaces the contents rather than appending.
    cd.write_and_replace("state.json", b"{\"v\":2}").unwrap();
    assert_eq!(cd.read_to_string("state.json").unwrap(), "{\"v\":2}");

    // The temporary file it uses is renamed over the target, not left behind:
    // whatever the temporary is called, the directory holds exactly one entry
    // afterwards and that entry is the target.
    let mut entries: Vec<String> = std::fs::read_dir(cd.as_path())
        .unwrap()
        .map(|e| e.unwrap().file_name().to_string_lossy().into_owned())
        .collect();
    entries.sort();
    assert_eq!(entries, vec!["state.json".to_string()], "{entries:?}");
}

#[test]
fn checkeddir__written_files_are_private() {
    let t = Tree::new();
    let cd = checked(&t);
    cd.write_and_replace("secret", b"hello").unwrap();
    assert_eq!(mode_of(cd.as_path().join("secret")) & 0o777, 0o600);
}

#[test]
fn checkeddir__open_ignores_the_requested_mode() {
    // Documented behaviour: files created through a CheckedDir are mode 600
    // whatever the caller asked for.
    let t = Tree::new();
    let cd = checked(&t);

    let mut opts = OpenOptions::new();
    opts.create(true).write(true).mode(0o666);
    let f = cd.open("loose", &opts).unwrap();
    drop(f);
    assert_eq!(mode_of(cd.as_path().join("loose")) & 0o777, 0o600);

    // Escaping is refused here too.
    let e = cd.open("../loose", &opts).unwrap_err();
    assert!(matches!(e, Error::InvalidSubdirectory), "{e:?}");
}

#[test]
fn checkeddir__open_refuses_to_follow_a_symlink() {
    // Documented behaviour: `open` adds O_NOFOLLOW, so a symlink in the final
    // position is an error rather than a redirection -- even when it points at
    // a perfectly good file inside this same CheckedDir. (This is the opposite
    // of the rule for the ancestors of a checked path, which are resolved.)
    let t = Tree::new();
    let cd = checked(&t);

    cd.write_and_replace("real", b"contents").unwrap();
    let real = cd.as_path().join("real");
    let link = cd.as_path().join("link");
    std::os::unix::fs::symlink(&real, &link).unwrap();

    let mut opts = OpenOptions::new();
    opts.read(true);
    let e = cd
        .open("link", &opts)
        .err()
        .expect("a symlink member was followed");
    // The refusal names the link itself, and it is not a verdict on the link's
    // permissions -- nor is it "nothing is there", since both files exist.
    assert_eq!(e.path(), Some(link.as_path()));
    assert!(!e.is_bad_permission(), "{e:?}");
    assert!(!matches!(e, Error::NotFound(_)), "{e:?}");

    // The target is readable through its own name, so the refusal is about the
    // symlink and not about the file or the directory.
    assert_eq!(cd.read_to_string("real").unwrap(), "contents");
    assert!(real.exists() && link.exists());
}

#[test]
fn checkeddir__members_may_be_readable_but_not_writable() {
    // Members are judged by the content rule: readable is fine, writable by
    // anyone else is not.
    let t = Tree::new();
    let cd = checked(&t);

    let readable = cd.as_path().join("readable");
    std::fs::write(&readable, b"public").unwrap();
    crate::common::chmod(&readable, 0o644);
    assert_eq!(cd.read_to_string("readable").unwrap(), "public");

    let writable = cd.as_path().join("writable");
    std::fs::write(&writable, b"tamperable").unwrap();
    crate::common::chmod(&writable, 0o666);
    let e = cd.read("writable").unwrap_err();
    match e {
        Error::BadPermission(p, bits) => {
            assert_eq!(p, writable);
            assert_eq!(bits, 0o022);
        }
        other => panic!("unexpected: {other:?}"),
    }
}
