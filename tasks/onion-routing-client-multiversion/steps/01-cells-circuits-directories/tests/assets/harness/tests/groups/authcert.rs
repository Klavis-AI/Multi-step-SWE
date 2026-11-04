//! Group `authcert`: directory-authority key certificates.
//!
//! The fixture certificates carry filler signatures, so the signature gate is
//! discharged rather than checked; everything else is observed.

use std::time::{Duration, SystemTime};

use tor_checkable::{SelfSigned, TimeValidityError, Timebound};
use tor_netdoc::doc::authcert::AuthCert;
use tor_netdoc::Error;

use crate::common::fixture_text;

/// `2021-06-01 00:00:00 UTC`, the `dir-key-published` time of the fixture.
const PUBLISHED: u64 = 1_622_505_600;
/// `2022-06-01 00:00:00 UTC`, the `dir-key-expires` time of the fixture.
const EXPIRES: u64 = 1_654_041_600;

fn t(secs: u64) -> SystemTime {
    SystemTime::UNIX_EPOCH + Duration::from_secs(secs)
}

fn hexid(s: &str) -> Vec<u8> {
    hex::decode(s).unwrap()
}

fn cert(name: &str) -> AuthCert {
    let text = fixture_text(name);
    AuthCert::parse(&text)
        .expect("fixture must parse")
        .dangerously_assume_wellsigned()
        .dangerously_assume_timely()
}

fn parse_err(name: &str) -> Error {
    let text = fixture_text(name);
    AuthCert::parse(&text)
        .map(|_| ())
        .expect_err("this fixture must not parse")
}

#[test]
fn authcert__fixture_parses_into_its_key_fingerprints_and_times() {
    let c = cert("authcert.txt");
    assert_eq!(c.published(), t(PUBLISHED));
    assert_eq!(c.expires(), t(EXPIRES));

    // The `fingerprint` line is the SHA-1 digest of the identity key.
    let id = hexid("268af4e5131bff56de3308d6e5071cdc2ed60964");
    let sk = hexid("c952cf0653ea72cab922ddd2df4d3e1f9ea06d3f");
    assert_eq!(c.id_fingerprint().as_bytes(), &id[..]);
    assert_eq!(c.sk_fingerprint().as_bytes(), &sk[..]);
    assert_eq!(c.key_ids().id_fingerprint.as_bytes(), &id[..]);
    assert_eq!(c.key_ids().sk_fingerprint.as_bytes(), &sk[..]);
    // `sk_fingerprint` identifies the key returned by `signing_key`.
    assert_eq!(
        c.signing_key().to_rsa_identity().as_bytes(),
        c.sk_fingerprint().as_bytes()
    );
    assert_ne!(c.id_fingerprint().as_bytes(), c.sk_fingerprint().as_bytes());
}

#[test]
fn authcert__validity_runs_from_published_until_expires() {
    let bound = |name: &str| {
        AuthCert::parse(&fixture_text(name))
            .expect("fixture must parse")
            .dangerously_assume_wellsigned()
    };
    // Inside the interval the certificate unwraps.
    let inside = bound("authcert.txt")
        .check_valid_at(&t(PUBLISHED + 86_400))
        .expect("should be valid one day after publication");
    assert_eq!(inside.expires(), t(EXPIRES));

    match bound("authcert.txt").check_valid_at(&t(PUBLISHED - 3600)).map(|_| ()) {
        Err(TimeValidityError::NotYetValid(d)) => assert_eq!(d, Duration::from_secs(3600)),
        other => panic!("expected NotYetValid, got {:?}", other),
    }
    match bound("authcert.txt").check_valid_at(&t(EXPIRES + 7200)).map(|_| ()) {
        Err(TimeValidityError::Expired(d)) => assert_eq!(d, Duration::from_secs(7200)),
        other => panic!("expected Expired, got {:?}", other),
    }
}

#[test]
fn authcert__malformed_certificates_are_rejected() {
    // Each of these documents must be refused.  Which error variant carries
    // the refusal, and what it says, are not part of the contract.
    //
    // The `fingerprint` line must agree with the identity key.
    parse_err("authcert-badfp.txt");
    // Only version 3 of this document is defined.
    parse_err("authcert-badversion.txt");
}

#[test]
fn authcert__concatenated_certificates_are_read_one_after_another() {
    let one = fixture_text("authcert.txt");
    let both = format!("{}{}", one, one);
    let certs: Vec<_> = AuthCert::parse_multiple(&both)
        .map(|r| r.expect("each certificate must parse"))
        .map(|c| c.dangerously_assume_wellsigned().dangerously_assume_timely())
        .collect();
    assert_eq!(certs.len(), 2);
    assert_eq!(certs[0].key_ids(), certs[1].key_ids());
    assert_eq!(certs[0].published(), t(PUBLISHED));

    // A single certificate yields exactly one item.
    assert_eq!(AuthCert::parse_multiple(&one).count(), 1);
}
