//! Shared helpers for the step-11 conformance driver.
//!
//! Everything in this file must compile against BOTH the checkpoint tree and
//! the reference tree, because it is compiled into every per-group attribution
//! target as well as the aggregate. It therefore uses no API that this milestone
//! introduces. `#[allow(dead_code)]` is deliberate: each attribution target
//! pulls in this module but uses only a subset of it.
#![allow(dead_code)]

use tor_cell::chancell::msg::ChanMsg;
use tor_llcrypto::pk::ed25519::Ed25519Identity;
use tor_llcrypto::pk::rsa::RsaIdentity;

/// An `Ed25519Identity` whose 32 bytes are `b` repeated.
pub fn ed_id(b: u8) -> Ed25519Identity {
    Ed25519Identity::from_bytes(&[b; 32]).expect("32 bytes is a valid ed25519 id length")
}

/// An `RsaIdentity` whose 20 bytes are `b` repeated.
pub fn rsa_id(b: u8) -> RsaIdentity {
    RsaIdentity::from_bytes(&[b; 20]).expect("20 bytes is a valid rsa id length")
}

/// An `Ed25519Identity` from an explicit byte array.
pub fn ed_from_bytes(bytes: [u8; 32]) -> Ed25519Identity {
    Ed25519Identity::from(bytes)
}

/// An `RsaIdentity` from an explicit byte array.
pub fn rsa_from_bytes(bytes: [u8; 20]) -> RsaIdentity {
    RsaIdentity::from(bytes)
}

/// Encode the body of `msg` (no command byte, no length) into a fresh `Vec`.
pub fn encode_body(msg: ChanMsg) -> Vec<u8> {
    let mut v = Vec::new();
    msg.write_body_onto(&mut v).expect("encoding a chan msg body");
    v
}
