//! Step-07 helpers that hold at the PREVIOUS checkpoint.
//!
//! Split from the fallback-directory helpers on purpose. Every generated
//! attribution target compiles the fixture modules its group names, so
//! anything in here is compiled into the gate group `deferboot`.
//! `tor_guardmgr::fallback` does not exist at v0.1.0, so a single `use` of it
//! at the top of this file vetoed that group over code it never calls. Those
//! helpers now live in `current.rs`, which only scored groups may name.
//!
//! Nothing in here re-implements anything that is graded: it only builds
//! inputs (identities, channel targets, SOCKS requests), seeds deterministic
//! RNGs, and stands up an offline client.

#![allow(dead_code)]

use std::net::SocketAddr;

use tor_linkspec::OwnedChanTarget;
use tor_llcrypto::pk::ed25519::Ed25519Identity;
use tor_llcrypto::pk::rsa::RsaIdentity;
use tor_socksproto::{SocksHandshake, SocksRequest};

/// A deterministic Ed25519 identity: 32 copies of `seed`.
pub fn ed_id(seed: u8) -> Ed25519Identity {
    Ed25519Identity::from_bytes(&[seed; 32]).expect("32 bytes is a well-formed ed25519 identity")
}

/// A deterministic RSA identity (SHA-1 digest): 20 copies of `seed`.
pub fn rsa_id(seed: u8) -> RsaIdentity {
    RsaIdentity::from_bytes(&[seed; 20]).expect("20 bytes is a well-formed RSA identity")
}

/// Parse a list of `ip:port` strings.
pub fn addrs(list: &[&str]) -> Vec<SocketAddr> {
    list.iter()
        .map(|a| a.parse::<SocketAddr>().expect("test address parses"))
        .collect()
}

/// An `OwnedChanTarget` with the given addresses and seed-derived identities.
pub fn chan_target(list: &[&str], seed: u8) -> OwnedChanTarget {
    OwnedChanTarget::new(addrs(list), ed_id(seed), rsa_id(seed))
}
/// A reproducible RNG.  Every case that draws random numbers uses this, so
/// that a failure is always reproducible.
pub fn seeded_rng(seed: u64) -> rand::rngs::StdRng {
    use rand::SeedableRng;
    rand::rngs::StdRng::seed_from_u64(seed)
}

/// Drive a SOCKS5 handshake (no authentication) to completion with a
/// hostname request, and return the negotiated request.
pub fn socks5_request(host: &str, port: u16) -> SocksRequest {
    let mut hs = SocksHandshake::new();
    hs.handshake(&[5, 1, 0])
        .expect("greeting is complete")
        .expect("greeting is well-formed");
    let mut msg = vec![5u8, 1, 0, 3, host.len() as u8];
    msg.extend_from_slice(host.as_bytes());
    msg.extend_from_slice(&port.to_be_bytes());
    hs.handshake(&msg)
        .expect("request is complete")
        .expect("request is well-formed");
    hs.into_request().expect("handshake yields a request")
}

/// Drive a SOCKS4a handshake to completion and return the request.
pub fn socks4a_request(host: &str, port: u16) -> SocksRequest {
    let mut hs = SocksHandshake::new();
    let mut msg = vec![4u8, 1];
    msg.extend_from_slice(&port.to_be_bytes());
    // A nonzero address whose top three octets are zero means "socks4a".
    msg.extend_from_slice(&[0, 0, 0, 1]);
    msg.extend_from_slice(b"username");
    msg.push(0);
    msg.extend_from_slice(host.as_bytes());
    msg.push(0);
    hs.handshake(&msg)
        .expect("request is complete")
        .expect("request is well-formed");
    hs.into_request().expect("handshake yields a request")
}

/// An unbootstrapped `TorClient` whose state and cache directories are
/// throw-away temporaries, together with those temporaries.
///
/// Nothing in here reaches the network: the client is built with
/// `BootstrapBehavior::Manual`, so it never starts a bootstrap of its own, and
/// no case in this suite calls `bootstrap()`.  The temporary directories are
/// held in the struct so that they outlive the client and are removed when it
/// is dropped; the field order is the drop order.
pub struct OfflineClient {
    /// The client itself.  Dropped first.
    pub client: arti_client::TorClient<tor_rtcompat::PreferredRuntime>,
    /// Throw-away `state_dir`.
    _state_dir: tempfile::TempDir,
    /// Throw-away `cache_dir`.
    _cache_dir: tempfile::TempDir,
}

impl OfflineClient {
    /// Build an unbootstrapped client on a fresh runtime.
    ///
    /// # Panics
    ///
    /// Panics if the runtime or the client cannot be created; both are
    /// failures of the item under test rather than of the environment.
    pub fn new() -> Self {
        use arti_client::config::TorClientConfigBuilder;
        use arti_client::{BootstrapBehavior, TorClient};
        use tor_rtcompat::PreferredRuntime;

        let state_dir = tempfile::tempdir().expect("a temporary state directory");
        let cache_dir = tempfile::tempdir().expect("a temporary cache directory");
        let cfg = TorClientConfigBuilder::from_directories(state_dir.path(), cache_dir.path())
            .build()
            .expect("a config naming both directories builds");
        let runtime = PreferredRuntime::create().expect("a preferred runtime");
        let client = TorClient::with_runtime(runtime)
            .config(cfg)
            .bootstrap_behavior(BootstrapBehavior::Manual)
            .create_unbootstrapped()
            .expect("an unbootstrapped client is created without network access");
        OfflineClient {
            client,
            _state_dir: state_dir,
            _cache_dir: cache_dir,
        }
    }

    /// The path of the client's cache directory.
    pub fn cache_dir(&self) -> &std::path::Path {
        self._cache_dir.path()
    }
}
