//! Step-12 fixtures that hold at the PREVIOUS checkpoint.
//!
//! Split from the mock-proxy helpers on purpose. Every generated attribution
//! target compiles the fixture modules its group names, so anything in here is
//! compiled into the gate groups `relayid` and `ridtraits`. `tor-ptmgr` does
//! not exist at v1.0.0, so a single `use tor_ptmgr::...` at the top of this
//! file vetoed both of them over code neither one calls. Those helpers now
//! live in `current.rs`, which only scored groups may name.
//!
//! Nothing here reimplements anything that is scored: it loads text fixtures,
//! builds the well-known identity constants the bridge-line cases use, and
//! formats bridge lines as strings. No network, no randomness, no clock.
#![allow(dead_code)]

use std::path::PathBuf;

use tor_llcrypto::pk::ed25519::Ed25519Identity;
use tor_llcrypto::pk::rsa::RsaIdentity;

// ------------------------------------------------------------------ fixtures

/// Absolute path of a fixture shipped next to this driver.
pub fn fixture_path(name: &str) -> PathBuf {
    let mut p = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    p.push("fixtures");
    p.push(name);
    p
}

/// Load a text fixture.
pub fn fixture_text(name: &str) -> String {
    let p = fixture_path(name);
    std::fs::read_to_string(&p).unwrap_or_else(|e| panic!("cannot read fixture {:?}: {}", p, e))
}

// ------------------------------------------------------- identity constants

/// Hex of the RSA identity used throughout the bridge-line cases.
pub const RSA_HEX: &str = "2f7d5b8e8b63b5f2a9a8f9e3a1c4d6b8e0f2a4c6";
/// The same identity, as the `$`-less hex a bridge line carries.
pub const RSA_FINGERPRINT: &str = "2F7D5B8E8B63B5F2A9A8F9E3A1C4D6B8E0F2A4C6";
/// Base64 (unpadded) of the Ed25519 identity used by the bridge-line cases.
///
/// The 32 bytes it encodes are ASCII, so [`ED_BYTES`] can spell them out and
/// the driver never needs a base64 decoder of its own.
pub const ED_B64: &str = "dGhpcyBpcyBpbmNyZWRpYmx5IHNpbGx5IGV4YW1wbGU";
/// The 32 bytes [`ED_B64`] encodes.
pub const ED_BYTES: &[u8; 32] = b"this is incredibly silly example";

/// The RSA identity named by [`RSA_FINGERPRINT`].
pub fn rsa_id() -> RsaIdentity {
    RsaIdentity::from_hex(RSA_HEX).expect("bad RSA identity constant")
}

/// The Ed25519 identity named by [`ED_B64`].
pub fn ed_id() -> Ed25519Identity {
    Ed25519Identity::from_bytes(ED_BYTES).expect("bad ed25519 identity constant")
}

// ----------------------------------------------------- mock managed proxy

/// Path of the mock managed proxy built alongside this driver.
pub fn mock_pt_path() -> PathBuf {
    PathBuf::from(env!("CARGO_BIN_EXE_mockpt"))
}

/// A private, empty scratch directory named after the calling case.
///
/// Deterministic: the name is derived from `case`, not from a clock or an RNG,
/// and the directory is emptied on entry so a rerun starts from the same state.
pub fn scratch_dir(case: &str) -> PathBuf {
    let dir = std::env::temp_dir().join(format!("step12-driver-{}", case));
    let _ = std::fs::remove_dir_all(&dir);
    std::fs::create_dir_all(&dir).expect("cannot create scratch directory");
    dir
}
// --------------------------------------------------- bridge-line fixtures

/// The RSA fingerprint every bridge-line case uses, with the `$` prefix.
pub fn rsa_hex_prefixed() -> String {
    format!("${}", RSA_FINGERPRINT)
}

/// A direct bridge line naming `rsa_id()`.
pub fn direct_bridge_line(addr: &str) -> String {
    format!("{} {} {}", "bridge", addr, RSA_FINGERPRINT)
}

/// A pluggable bridge line naming the transport `pt` and `rsa_id()`.
pub fn pt_bridge_line(pt: &str, addr: &str, settings: &[(&str, &str)]) -> String {
    let mut line = format!("bridge {} {} {}", pt, addr, RSA_FINGERPRINT);
    for (k, v) in settings {
        line.push(' ');
        line.push_str(k);
        line.push('=');
        line.push_str(v);
    }
    line
}
