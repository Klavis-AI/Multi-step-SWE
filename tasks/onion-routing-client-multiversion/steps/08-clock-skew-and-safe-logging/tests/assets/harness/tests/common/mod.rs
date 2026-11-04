//! Shared fixtures and helpers for the step-08 conformance driver.
//!
//! Nothing in here re-implements graded behaviour: these helpers only build
//! filesystem fixtures and wrap boilerplate.

#![allow(dead_code)]

use std::fs;
use std::path::{Path, PathBuf};
use std::time::{Duration, SystemTime};

/// A throw-away directory tree used as a fixture.
///
/// The root is created by `tempfile` (mode 0700) and removed when the `Tree` is
/// dropped.  Every helper takes a path relative to the root.
pub struct Tree {
    /// The temporary directory itself; dropping it deletes the tree.
    dir: tempfile::TempDir,
    /// The canonical form of the root, resolved once at construction.
    canonical_root: PathBuf,
}

impl Tree {
    /// Build a new empty tree.
    pub fn new() -> Self {
        let dir = tempfile::TempDir::new().expect("could not create a temporary directory");
        let canonical_root = dir
            .path()
            .canonicalize()
            .expect("could not canonicalize the temporary directory");
        Tree {
            dir,
            canonical_root,
        }
    }

    /// The (uncanonicalized) root of the tree.
    pub fn root(&self) -> &Path {
        self.dir.path()
    }

    /// The canonical root of the tree, suitable for `Mistrust::ignore_prefix`.
    pub fn canonical_root(&self) -> &Path {
        self.canonical_root.as_path()
    }

    /// Resolve `rel` against the root.
    pub fn path(&self, rel: &str) -> PathBuf {
        self.dir.path().join(rel)
    }

    /// Resolve `rel` against the canonical root.
    pub fn canonical_path(&self, rel: &str) -> PathBuf {
        self.canonical_root.join(rel)
    }

    /// Create the directory `rel` and every missing parent.
    pub fn dir(&self, rel: &str) -> PathBuf {
        let p = self.path(rel);
        fs::create_dir_all(&p).expect("could not create a fixture directory");
        p
    }

    /// Create an empty file at `rel`; parents must already exist.
    pub fn file(&self, rel: &str) -> PathBuf {
        let p = self.path(rel);
        fs::write(&p, b"").expect("could not create a fixture file");
        p
    }

    /// Set the unix permission bits of `rel`.
    pub fn chmod(&self, rel: &str, mode: u32) {
        use std::os::unix::fs::PermissionsExt;
        fs::set_permissions(self.path(rel), fs::Permissions::from_mode(mode))
            .expect("could not set fixture permissions");
    }

    /// Set the modification time of `rel` to `age` before the current time.
    ///
    /// The reference point is the same `SystemTime::now()` the implementation
    /// uses, so the resulting age is deterministic relative to any cutoff.
    pub fn age(&self, rel: &str, age: Duration) {
        let when = SystemTime::now()
            .checked_sub(age)
            .expect("fixture age is before the epoch");
        set_mtime(&self.path(rel), when);
    }

    /// Set the modification time of `rel` to `ahead` after the current time.
    pub fn date_in_future(&self, rel: &str, ahead: Duration) {
        let when = SystemTime::now()
            .checked_add(ahead)
            .expect("fixture date overflows");
        set_mtime(&self.path(rel), when);
    }

    /// Return true if `rel` exists.
    pub fn exists(&self, rel: &str) -> bool {
        self.path(rel).exists()
    }

    /// Return the unix permission bits (low 12) of `rel`.
    pub fn mode(&self, rel: &str) -> u32 {
        mode_of(&self.path(rel))
    }
}

impl Default for Tree {
    fn default() -> Self {
        Tree::new()
    }
}

/// Overwrite the modification time of `path`.
pub fn set_mtime(path: &Path, when: SystemTime) {
    let f = fs::OpenOptions::new()
        .write(true)
        .open(path)
        .expect("could not open a fixture file to re-date it");
    let times = fs::FileTimes::new().set_modified(when).set_accessed(when);
    f.set_times(times).expect("could not re-date a fixture file");
}

/// Return the low 12 permission bits of `path`.
pub fn mode_of(path: &Path) -> u32 {
    use std::os::unix::fs::MetadataExt;
    fs::metadata(path).expect("could not stat a fixture path").mode() & 0o7777
}

/// Return the owning uid of `path`.
pub fn uid_of(path: &Path) -> u32 {
    use std::os::unix::fs::MetadataExt;
    fs::metadata(path).expect("could not stat a fixture path").uid()
}

/// Return the owning gid of `path`.
pub fn gid_of(path: &Path) -> u32 {
    use std::os::unix::fs::MetadataExt;
    fs::metadata(path).expect("could not stat a fixture path").gid()
}

/// Build a `Mistrust` that is deterministic in any environment.
///
/// `Mistrust::default()` trusts the group whose name matches the current user,
/// which differs between machines; we always clear it.  We also ignore
/// everything at or above the fixture root, so that the permissions of the
/// system temporary directory never affect a result.
pub fn mistrust_under(tree: &Tree) -> fs_mistrust::Mistrust {
    let mut m = fs_mistrust::Mistrust::new();
    m.trust_no_group_id();
    m.ignore_prefix(tree.canonical_root())
        .expect("could not ignore the fixture root");
    m
}

/// As `mistrust_under`, but ignoring only the paths at or above `rel`.
pub fn mistrust_ignoring(tree: &Tree, rel: &str) -> fs_mistrust::Mistrust {
    let mut m = fs_mistrust::Mistrust::new();
    m.trust_no_group_id();
    m.ignore_prefix(
        tree.path(rel)
            .canonicalize()
            .expect("could not canonicalize an ignored prefix"),
    )
    .expect("could not ignore a fixture prefix");
    m
}

/// Assert that `err` is `BadPermission` naming `path` with exactly `bits` set.
pub fn assert_bad_permission(err: &fs_mistrust::Error, path: &Path, bits: u32) {
    match err {
        fs_mistrust::Error::BadPermission(p, b) => {
            assert_eq!(p.as_path(), path, "wrong path in {:?}", err);
            assert_eq!(*b, bits, "wrong permission bits in {:?}", err);
        }
        other => panic!("expected BadPermission({:?}, {:#o}), got {:?}", path, bits, other),
    }
}

/// One second, as a `Duration`.
pub const SEC: Duration = Duration::from_secs(1);

/// One day, as a `Duration`.
pub const DAY: Duration = Duration::from_secs(24 * 60 * 60);
