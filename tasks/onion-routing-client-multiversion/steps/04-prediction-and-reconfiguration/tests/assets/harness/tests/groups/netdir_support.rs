//! Directory fixtures for the groups that need the synthetic test network.
//!
//! This file is deliberately **not** part of `common/mod.rs`. `common` is
//! compiled into the aggregate conformance target *and* into every one of the
//! twenty-three per-group attribution targets, so anything it touches becomes a
//! compile-time dependency of all twenty-three groups. The netdir fixtures need
//! `tor_netdir::testnet`, `tor_netdoc`'s document builders and `tor-netdir`'s
//! `testing` feature; putting them in `common` would mean a candidate who had
//! not built `testnet` yet lost the cell, config and TLS groups too, which is
//! exactly the failure the per-group attribution pass exists to prevent.
//!
//! So the eleven groups that build a directory include this file themselves
//! with `#[path = "netdir_support.rs"] mod netdir_support;`. Each gets its own
//! copy; the other twelve groups never see it.
//!
//! Everything the fixtures assume about the shipped network — forty nodes, the
//! identity derivation, the flag decades, the exit-policy parity, the family
//! pairing, the DirCache parity — is stated in `TEST_INTERFACE.md` section 7.5.

#![allow(dead_code)]
#![allow(unused_imports)]

use tor_llcrypto::pk::ed25519::Ed25519Identity;
use tor_llcrypto::pk::rsa::RsaIdentity;
use tor_netdir::testnet::{self, NodeBuilders};
use tor_netdir::{MdReceiver, NetDir, PartialNetDir};
use tor_netdoc::doc::netstatus::NetParams;

/// The Ed25519 identity the test network gives to relay number `idx`.
pub fn ed_id(idx: u8) -> Ed25519Identity {
    [idx; 32].into()
}

/// The RSA identity the test network gives to relay number `idx`.
pub fn rsa_id(idx: u8) -> RsaIdentity {
    [idx; 20].into()
}

/// Recover a test-network relay index from its Ed25519 identity.
pub fn ed_index(id: &Ed25519Identity) -> u8 {
    id.as_bytes()[0]
}

/// Render a `family` line body naming the given test-network relays.
pub fn family_of(members: &[u8]) -> String {
    members
        .iter()
        .map(|idx| hex::encode([*idx; 20]))
        .collect::<Vec<_>>()
        .join(" ")
}

/// Parse a space-separated `key=value` list into consensus parameters.
pub fn net_params(s: &str) -> NetParams<i32> {
    s.parse().expect("bad parameter list in test support code")
}

/// Build a network directory from the shipped test network, customised by
/// `func`, with no parameter overrides.
pub fn netdir_custom<F>(func: F) -> NetDir
where
    F: FnMut(usize, &mut NodeBuilders),
{
    netdir_custom_with_params(func, None)
}

/// Build a network directory from the shipped test network, customised by
/// `func`, overriding consensus parameters with `params`.
pub fn netdir_custom_with_params<F>(func: F, params: Option<&NetParams<i32>>) -> NetDir
where
    F: FnMut(usize, &mut NodeBuilders),
{
    partial_netdir_custom_with_params(func, params)
        .unwrap_if_sufficient()
        .expect("test network did not have enough paths")
}

/// As [`netdir_custom_with_params`], but stop before the sufficiency check.
pub fn partial_netdir_custom_with_params<F>(func: F, params: Option<&NetParams<i32>>) -> PartialNetDir
where
    F: FnMut(usize, &mut NodeBuilders),
{
    let (consensus, mds) = testnet::construct_custom_network(func).expect("cannot build test net");
    let mut dir = PartialNetDir::new(consensus, params);
    for md in mds {
        dir.add_microdesc(md);
    }
    dir
}

/// A customiser that leaves only the listed relays advertising `DirCache=2`.
///
/// Guard sampling only ever considers relays that are directory caches, so
/// this is how a test pins down exactly which relays can end up in the guard
/// sample without disturbing the consensus flags that decide whether the
/// directory has enough paths.
pub fn only_these_are_dir_caches(keep: &[usize], idx: usize, nb: &mut NodeBuilders) {
    if !keep.contains(&idx) {
        nb.rs.protos("".parse().expect("empty protocol list"));
    }
}
