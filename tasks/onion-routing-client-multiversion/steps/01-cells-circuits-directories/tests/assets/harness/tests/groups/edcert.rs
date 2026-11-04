//! Group `edcert`: Tor's Ed25519 certificate format.
//!
//! Every `fixtures/edcert_*.bin` file was laid out by hand from the field
//! order in cert-spec (see SPEC_NOTES.md); the keys are openssl-generated and
//! the signatures are filler, so only parsing and key plumbing are graded.

use std::time::{Duration, SystemTime};

use tor_cert::{CertType, Ed25519Cert, ExtType, KeyType};
use tor_checkable::{SelfSigned, Timebound};
use tor_llcrypto::pk::ed25519;

use crate::common::fixture_bytes;

/// Hours-since-the-epoch value baked into every certificate fixture.
const EXPIRY_HOURS: u64 = 456_000;

fn pubkey(b: &[u8]) -> ed25519::PublicKey {
    ed25519::PublicKey::from_bytes(b).expect("fixture key must be a valid ed25519 point")
}

#[test]
fn edcert__signing_certificate_fixture_parses_into_its_fields() {
    let f = fixture_bytes("edcert_signing.bin");
    assert_eq!(f.len(), 140);
    let c = Ed25519Cert::decode(&f).expect("the fixture certificate must decode");
    assert_eq!(c.peek_cert_type(), CertType::IDENTITY_V_SIGNING);
    assert_eq!(c.peek_subject_key().key_type(), KeyType::ED25519_KEY);
    assert_eq!(c.peek_subject_key().as_bytes(), &f[7..39]);
    assert_eq!(
        c.peek_subject_key().as_ed25519().map(|k| k.as_bytes()),
        Some(&f[7..39].try_into().unwrap())
    );

    // The signing key comes from the SIGNED_WITH_ED25519_KEY extension.
    let unchecked = c.check_key(&None).expect("the cert names its own signer");
    assert_eq!(unchecked.peek_signing_key().as_bytes(), &f[44..76]);
}

#[test]
fn edcert__expiry_is_measured_in_whole_hours_since_the_epoch() {
    let f = fixture_bytes("edcert_signing.bin");
    assert_eq!(u32::from_be_bytes([f[2], f[3], f[4], f[5]]) as u64, EXPIRY_HOURS);

    let cert = Ed25519Cert::decode(&f)
        .unwrap()
        .check_key(&None)
        .unwrap()
        .dangerously_assume_wellsigned()
        .dangerously_assume_timely();

    let expiry = SystemTime::UNIX_EPOCH + Duration::from_secs(EXPIRY_HOURS * 3600);
    assert_eq!(cert.expiry(), expiry);
    assert!(!cert.is_expired_at(expiry - Duration::from_secs(1)));
    assert!(cert.is_expired_at(expiry + Duration::from_secs(1)));
    assert_eq!(cert.cert_type(), CertType::IDENTITY_V_SIGNING);
    assert_eq!(cert.subject_key().as_bytes(), &f[7..39]);
    assert_eq!(cert.signing_key().map(|k| k.as_bytes()), Some(&f[44..76].try_into().unwrap()));
}

#[test]
fn edcert__only_version_one_certificates_are_accepted() {
    let f = fixture_bytes("edcert_badversion.bin");
    assert_eq!(f[0], 2);
    // Only the rejection is graded, not the error variant or its message.
    Ed25519Cert::decode(&f)
        .map(|_| ())
        .expect_err("version 2 is not a known format");
}

#[test]
fn edcert__a_certificate_without_the_key_extension_needs_an_external_key() {
    let f = fixture_bytes("edcert_noext.bin");
    assert_eq!(f.len(), 104);
    assert_eq!(f[39], 0, "no extensions");

    Ed25519Cert::decode(&f)
        .unwrap()
        .check_key(&None)
        .map(|_| ())
        .expect_err("there is no key to check against");

    let signer = pubkey(&fixture_bytes("edcert_signing.bin")[44..76]);
    let unchecked = Ed25519Cert::decode(&f)
        .unwrap()
        .check_key(&Some(signer))
        .expect("an externally supplied key is accepted");
    assert_eq!(unchecked.peek_signing_key().as_bytes(), signer.as_bytes());
}

#[test]
fn edcert__an_external_key_must_match_the_embedded_one() {
    let f = fixture_bytes("edcert_signing.bin");
    let wrong = pubkey(&f[7..39]); // the subject key, not the signer
    Ed25519Cert::decode(&f)
        .unwrap()
        .check_key(&Some(wrong))
        .map(|_| ())
        .expect_err("the embedded signer disagrees");

    let right = pubkey(&f[44..76]);
    assert!(Ed25519Cert::decode(&f).unwrap().check_key(&Some(right)).is_ok());
}

#[test]
fn edcert__unknown_extensions_are_skipped_unless_they_affect_validation() {
    let ok = fixture_bytes("edcert_unknownext.bin");
    assert_eq!(ok[39], 2, "two extensions");
    assert_eq!(ok[78], 0x2a, "an extension type this version does not know");
    assert_eq!(ok[79], 0, "flags = 0: safe to ignore");
    let c = Ed25519Cert::decode(&ok).expect("an ignorable extension must be skipped");
    assert_eq!(c.peek_cert_type(), CertType::IDENTITY_V_SIGNING);

    let bad = fixture_bytes("edcert_criticalext.bin");
    assert_eq!(bad[78], 0x2a);
    assert_eq!(bad[79], 1, "flags = 1: affects validation");
    Ed25519Cert::decode(&bad)
        .map(|_| ())
        .expect_err("a critical unknown extension is fatal");
}

#[test]
fn edcert__certificate_and_key_type_codes() {
    assert_eq!(u8::from(CertType::TLS_LINK_X509), 0x01);
    assert_eq!(u8::from(CertType::RSA_ID_X509), 0x02);
    assert_eq!(u8::from(CertType::LINK_AUTH_X509), 0x03);
    assert_eq!(u8::from(CertType::IDENTITY_V_SIGNING), 0x04);
    assert_eq!(u8::from(CertType::SIGNING_V_TLS_CERT), 0x05);
    assert_eq!(u8::from(CertType::SIGNING_V_LINK_AUTH), 0x06);
    assert_eq!(u8::from(CertType::RSA_ID_V_IDENTITY), 0x07);
    assert_eq!(u8::from(CertType::NTOR_CC_IDENTITY), 0x0a);
    assert_eq!(u8::from(ExtType::SIGNED_WITH_ED25519_KEY), 0x04);
    assert_eq!(u8::from(KeyType::ED25519_KEY), 0x01);
    assert_eq!(u8::from(KeyType::SHA256_OF_RSA), 0x02);
    assert_eq!(u8::from(KeyType::SHA256_OF_X509), 0x03);

    // A certificate over a non-Ed25519 subject keeps the digest verbatim.
    let f = fixture_bytes("edcert_rsakeytype.bin");
    assert_eq!(f[1], 0x07);
    assert_eq!(f[6], 0x03);
    let c = Ed25519Cert::decode(&f).expect("a digest-subject certificate must decode");
    assert_eq!(c.peek_cert_type(), CertType::RSA_ID_V_IDENTITY);
    assert_eq!(c.peek_subject_key().key_type(), KeyType::SHA256_OF_X509);
    assert_eq!(c.peek_subject_key().as_bytes(), &[0xab_u8; 32]);
    assert!(c.peek_subject_key().as_ed25519().is_none());
}
