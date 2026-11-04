//! Shared, version-neutral helpers for the step-05 conformance driver.
//!
//! This module is compiled into *every* test target, including the
//! per-group attribution targets for `inherited` groups, which must still
//! build against a workspace that has not yet grown this step's API.  It
//! therefore deliberately imports nothing from the candidate's crates.
#![allow(dead_code)]

/// Tolerance used when comparing the bootstrap-progress fractions.
pub const TOL: f32 = 1e-6;

/// Assert that two `f32` progress fractions agree to within [`TOL`].
#[track_caller]
pub fn assert_frac(got: f32, want: f32) {
    assert!(
        (got - want).abs() <= TOL,
        "fraction {got} is not within {TOL} of {want}"
    );
}

/// The DER-encoded stand-in certificates handed to the mock TLS listeners.
pub const PEER_CERT_A: &[u8] = include_bytes!("../../fixtures/peer_cert_a.der");
/// A second, distinct DER-encoded stand-in certificate.
pub const PEER_CERT_B: &[u8] = include_bytes!("../../fixtures/peer_cert_b.der");

/// A single decoded DER tag-length-value header.
pub struct Tlv<'a> {
    /// The identifier octet.
    pub tag: u8,
    /// The number of octets of content the header declares.
    pub len: usize,
    /// The content octets themselves.
    pub value: &'a [u8],
}

/// Decode one DER tag-length-value header from the front of `bytes`.
///
/// Supports the short definite form and the long definite form; the
/// indefinite form is not valid DER and is rejected.
pub fn parse_der_tlv(bytes: &[u8]) -> Option<Tlv<'_>> {
    let tag = *bytes.first()?;
    let first_len = *bytes.get(1)?;
    let (len, hdr) = if first_len < 0x80 {
        (usize::from(first_len), 2usize)
    } else {
        let n = usize::from(first_len & 0x7f);
        if n == 0 || n > 4 {
            return None;
        }
        let mut acc = 0usize;
        for i in 0..n {
            acc = (acc << 8) | usize::from(*bytes.get(2 + i)?);
        }
        (acc, 2 + n)
    };
    let value = bytes.get(hdr..hdr + len)?;
    Some(Tlv { tag, len, value })
}

/// Assert that `T` implements `Clone`.
pub fn assert_clone<T: Clone>() {}

/// Assert that `T` implements `Debug`.
pub fn assert_debug<T: std::fmt::Debug>() {}

/// Assert that `T` implements `Display`.
pub fn assert_display<T: std::fmt::Display>() {}

/// Assert that `T` is `Send + Sync + 'static`.
pub fn assert_send_sync<T: Send + Sync + 'static>() {}

/// The mock client address used by the driver.
///
/// Every case builds its own mock network, so the same RFC 5737
/// documentation addresses can be reused without any cross-case coupling.
pub const CLIENT_IP: &str = "192.0.2.3";
/// The mock relay address used by the driver.
pub const RELAY_IP: &str = "192.0.2.55";
/// The mock relay port used by the driver.
pub const RELAY_PORT: u16 = 443;
/// An address on the mock network that nothing ever listens on.
pub const DARK_IP: &str = "192.0.2.99";
/// The mock relay's primary socket address.
pub const RELAY_ADDR: &str = "192.0.2.55:443";
/// A second socket address on the same mock relay.
pub const RELAY_ALT_ADDR: &str = "192.0.2.55:9001";
/// A socket address on the mock network that nothing ever listens on.
pub const DARK_ADDR: &str = "192.0.2.99:443";
