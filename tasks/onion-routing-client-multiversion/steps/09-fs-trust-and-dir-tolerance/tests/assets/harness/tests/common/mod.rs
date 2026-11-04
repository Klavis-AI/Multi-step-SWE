//! Shared helpers for the step-09 conformance driver.
//!
//! Only `std` and `tempfile` are used here so that this module also compiles
//! against the step-08 tree (the `inherited` tier).
#![allow(dead_code)]

use std::fs;
use std::os::unix::fs::{MetadataExt, PermissionsExt};
use std::path::{Path, PathBuf};

/// Directory modes used by the fs-mistrust cases.
pub const GOOD_DIR: u32 = 0o700;
/// A directory any user on the machine may write into.
pub const BAD_DIR_WORLD_W: u32 = 0o707;
/// A directory members of our own group may write into.
pub const BAD_DIR_GROUP_W: u32 = 0o720;
/// A file any user on the machine may read.
pub const BAD_FILE_WORLD_R: u32 = 0o604;
/// A file members of our own group may write into.
pub const BAD_FILE_GROUP_W: u32 = 0o620;

/// A private temporary tree, with explicit mode control.
///
/// The root is canonicalized, so that a `Mistrust` configured with the root as
/// its ignore-prefix never has to walk above it.  That keeps the cases
/// independent of how `/tmp` happens to be configured on the host.
pub struct Tree {
    _tmp: tempfile::TempDir,
    root: PathBuf,
}

impl Tree {
    /// Make a new tree whose root is mode 0o700.
    pub fn new() -> Self {
        let tmp = tempfile::tempdir().unwrap();
        let root = fs::canonicalize(tmp.path()).unwrap();
        fs::set_permissions(&root, fs::Permissions::from_mode(GOOD_DIR)).unwrap();
        Tree { _tmp: tmp, root }
    }

    /// The canonical root of the tree.
    pub fn root(&self) -> &Path {
        &self.root
    }

    /// A path inside the tree; the file need not exist.
    pub fn path<P: AsRef<Path>>(&self, rel: P) -> PathBuf {
        self.root.join(rel)
    }

    /// Create (and chmod) a directory inside the tree.
    pub fn dir<P: AsRef<Path>>(&self, rel: P, mode: u32) -> PathBuf {
        let p = self.path(rel);
        fs::create_dir_all(&p).unwrap();
        fs::set_permissions(&p, fs::Permissions::from_mode(mode)).unwrap();
        p
    }

    /// Create (and chmod) a regular file inside the tree.
    pub fn file<P: AsRef<Path>>(&self, rel: P, content: &[u8], mode: u32) -> PathBuf {
        let p = self.path(rel);
        if let Some(parent) = p.parent() {
            fs::create_dir_all(parent).unwrap();
        }
        fs::write(&p, content).unwrap();
        fs::set_permissions(&p, fs::Permissions::from_mode(mode)).unwrap();
        p
    }

    /// Create a symlink inside the tree.
    pub fn symlink<P: AsRef<Path>, Q: AsRef<Path>>(&self, rel: P, target: Q) -> PathBuf {
        let p = self.path(rel);
        if let Some(parent) = p.parent() {
            fs::create_dir_all(parent).unwrap();
        }
        std::os::unix::fs::symlink(target, &p).unwrap();
        p
    }

    /// The uid owning the tree.
    pub fn uid(&self) -> u32 {
        fs::metadata(&self.root).unwrap().uid()
    }

    /// The gid owning the tree.
    pub fn gid(&self) -> u32 {
        fs::metadata(&self.root).unwrap().gid()
    }
}

/// Change the mode of an existing path.
pub fn chmod<P: AsRef<Path>>(path: P, mode: u32) {
    fs::set_permissions(path, fs::Permissions::from_mode(mode)).unwrap();
}

/// The full mode (including file-type bits) of an existing path.
pub fn mode_of<P: AsRef<Path>>(path: P) -> u32 {
    fs::metadata(path).unwrap().permissions().mode()
}

/// A fixed reference instant, so that no case depends on the wall clock.
///
/// 2020-09-22T00:00:00Z, chosen only because it is round and in the past.
pub const T0_SECS: u64 = 1_600_732_800;

/// The fixed reference instant as a `SystemTime`.
pub fn t0() -> std::time::SystemTime {
    std::time::SystemTime::UNIX_EPOCH + std::time::Duration::from_secs(T0_SECS)
}

/// One day, in seconds.
pub const DAY: std::time::Duration = std::time::Duration::from_secs(86_400);

/// Serialises every case that reads or writes a process-wide environment
/// variable, so that those cases stay correct even if the runner is ever
/// asked for more than one test thread.
static ENV_LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());

/// Run `f` with the environment variable `name` forced to `value`
/// (`None` meaning "unset"), restoring the previous setting afterwards.
pub fn with_env_var<T>(name: &str, value: Option<&str>, f: impl FnOnce() -> T) -> T {
    let _guard = ENV_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let previous = std::env::var_os(name);
    match value {
        Some(v) => std::env::set_var(name, v),
        None => std::env::remove_var(name),
    }
    let out = f();
    match previous {
        Some(v) => std::env::set_var(name, v),
        None => std::env::remove_var(name),
    }
    out
}
