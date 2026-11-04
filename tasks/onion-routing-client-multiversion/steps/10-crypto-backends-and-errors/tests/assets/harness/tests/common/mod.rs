//! Shared support code for the step-10 conformance driver.
//!
//! Everything in here is deliberately `std`-only and version-agnostic: this
//! module is linked into *every* group target, so a compile error here zeroes
//! the whole step, including the inherited tier. Version-specific code belongs
//! in the individual group files.

#![allow(dead_code)]

use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

// ---------------------------------------------------------------------------
// Locating the candidate workspace
// ---------------------------------------------------------------------------

/// Root of the candidate workspace (the directory that holds `crates/`).
///
/// The driver manifest names the candidate tree through path dependencies of
/// the form `<root>/crates/<crate-name>`; the grading harness rewrites them to
/// an absolute path before building. We recover `<root>` by reading our own
/// manifest back, which is the only reference to the candidate tree that is
/// guaranteed to be present at run time.
pub fn app_root() -> PathBuf {
    let manifest = Path::new(env!("CARGO_MANIFEST_DIR")).join("Cargo.toml");
    let text = std::fs::read_to_string(&manifest)
        .unwrap_or_else(|e| panic!("cannot read driver manifest {}: {e}", manifest.display()));

    for line in text.lines() {
        let line = strip_comment(line);
        // ... path = "/somewhere/app/crates/tor-bytes" ...
        let Some(idx) = line.find("path") else { continue };
        let rest = &line[idx + 4..];
        let rest = rest.trim_start();
        if !rest.starts_with('=') {
            continue;
        }
        let rest = rest[1..].trim_start();
        if !rest.starts_with('"') {
            continue;
        }
        let Some(end) = rest[1..].find('"') else { continue };
        let value = &rest[1..1 + end];
        if let Some(cut) = value.rfind("/crates/") {
            return PathBuf::from(&value[..cut]);
        }
    }
    panic!(
        "driver manifest {} has no `path = \".../crates/<name>\"` dependency",
        manifest.display()
    );
}

/// Absolute path of one candidate crate's directory.
pub fn crate_dir(crate_name: &str) -> PathBuf {
    app_root().join("crates").join(crate_name)
}

/// Text of one candidate crate's `Cargo.toml`.
pub fn crate_manifest(crate_name: &str) -> String {
    let p = crate_dir(crate_name).join("Cargo.toml");
    std::fs::read_to_string(&p)
        .unwrap_or_else(|e| panic!("cannot read manifest for {crate_name} at {}: {e}", p.display()))
}

// ---------------------------------------------------------------------------
// A very small manifest reader
// ---------------------------------------------------------------------------
//
// Only what these tests need: the `[features]` table and the raw right-hand
// side of one key in `[dependencies]`. Cargo manifests in this workspace put
// each dependency on a single line, and feature values are either inline
// arrays or arrays broken across a few lines, both of which are handled.

/// Drop a trailing `#` comment. Feature names and dependency specs in these
/// manifests never contain `#`, so no quote tracking is needed.
fn strip_comment(line: &str) -> &str {
    match line.find('#') {
        Some(i) => line[..i].trim(),
        None => line.trim(),
    }
}

/// Split an inline TOML array of strings into its members.
fn parse_string_array(s: &str) -> Vec<String> {
    let start = match s.find('[') {
        Some(i) => i + 1,
        None => return Vec::new(),
    };
    let end = match s.rfind(']') {
        Some(i) => i,
        None => s.len(),
    };
    s[start..end]
        .split(',')
        .map(|m| m.trim().trim_matches('"').trim().to_string())
        .filter(|m| !m.is_empty())
        .collect()
}

/// The `[features]` table of one candidate crate, as `name -> members`.
///
/// A crate with no `[features]` section yields an empty map.
pub fn features_of(crate_name: &str) -> BTreeMap<String, Vec<String>> {
    let text = crate_manifest(crate_name);
    let mut out: BTreeMap<String, Vec<String>> = BTreeMap::new();
    let mut in_features = false;
    let mut pending: Option<(String, String)> = None;

    for raw in text.lines() {
        let trimmed = raw.trim();
        if trimmed.starts_with('[') && pending.is_none() {
            in_features = trimmed.starts_with("[features]");
            continue;
        }
        if !in_features {
            continue;
        }
        let line = strip_comment(raw);
        if let Some((name, acc)) = pending.as_mut() {
            acc.push(' ');
            acc.push_str(line);
            if acc.contains(']') {
                let (n, a) = (name.clone(), acc.clone());
                out.insert(n, parse_string_array(&a));
                pending = None;
            }
            continue;
        }
        if line.is_empty() {
            continue;
        }
        let Some((key, value)) = line.split_once('=') else { continue };
        let key = key.trim().trim_matches('"').to_string();
        let value = value.trim().to_string();
        if value.contains(']') {
            out.insert(key, parse_string_array(&value));
        } else {
            pending = Some((key, value));
        }
    }
    out
}

/// The members a named feature pulls in, or `None` if the feature is absent.
pub fn feature_members(crate_name: &str, feature: &str) -> Option<Vec<String>> {
    features_of(crate_name).get(feature).cloned()
}

/// Raw right-hand side of `<dep> = ...` in the `[dependencies]` table.
pub fn dependency_spec(crate_name: &str, dep: &str) -> Option<String> {
    let text = crate_manifest(crate_name);
    let mut in_deps = false;
    for raw in text.lines() {
        let trimmed = raw.trim();
        if trimmed.starts_with('[') {
            in_deps = trimmed.starts_with("[dependencies]");
            continue;
        }
        if !in_deps {
            continue;
        }
        let line = strip_comment(raw);
        let Some((key, value)) = line.split_once('=') else { continue };
        if key.trim().trim_matches('"') == dep {
            return Some(value.trim().to_string());
        }
    }
    None
}

/// Assert that `feature` exists on `crate_name` and activates exactly
/// `expected` (order-insensitive).
pub fn assert_feature_is(crate_name: &str, feature: &str, expected: &[&str]) {
    let got = feature_members(crate_name, feature).unwrap_or_else(|| {
        panic!("crate `{crate_name}` declares no `{feature}` feature");
    });
    let mut got_sorted = got.clone();
    got_sorted.sort();
    let mut want_sorted: Vec<String> = expected.iter().map(|s| s.to_string()).collect();
    want_sorted.sort();
    assert_eq!(
        got_sorted, want_sorted,
        "crate `{crate_name}` feature `{feature}` activates {got:?}"
    );
}

/// Assert that `feature` exists on `crate_name` and that its member list
/// contains `member`.
pub fn assert_feature_contains(crate_name: &str, feature: &str, member: &str) {
    let got = feature_members(crate_name, feature).unwrap_or_else(|| {
        panic!("crate `{crate_name}` declares no `{feature}` feature");
    });
    assert!(
        got.iter().any(|m| m == member),
        "crate `{crate_name}` feature `{feature}` = {got:?}, expected it to activate `{member}`"
    );
}

// ---------------------------------------------------------------------------
// Misc assertion helpers
// ---------------------------------------------------------------------------

/// `Display` rendering of any value, for error-message assertions.
pub fn shown<T: std::fmt::Display>(v: &T) -> String {
    format!("{v}")
}

/// Assert that two byte strings are equal, reporting both in hex.
pub fn assert_bytes_eq(got: &[u8], want: &[u8], what: &str) {
    assert_eq!(
        hex::encode(got),
        hex::encode(want),
        "{what}: encoded bytes differ"
    );
}
