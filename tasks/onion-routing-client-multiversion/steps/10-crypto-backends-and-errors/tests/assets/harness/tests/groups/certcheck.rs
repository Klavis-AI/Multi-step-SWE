//! Ed25519 certificate checking through the identity-keyed API: `decode`,
//! `check_key`, signature verification and timeliness.

#![allow(non_snake_case)]
use tor_cert::{CertError, CertType, Ed25519Cert, KeyType};
use tor_checkable::{SelfSigned, Timebound};
use tor_llcrypto::pk::ed25519::Ed25519Identity;

/// A first-party ID_V_SIGNING certificate carrying a SIGNED_WITH_ED25519_KEY
/// extension; see `SPEC_NOTES.md`.
const CERT_WITH_EXT: &[u8] = include_bytes!("../../fixtures/cert_id_v_signing.bin");
/// The same certificate body, but with no extensions at all.
const CERT_NO_EXT: &[u8] = include_bytes!("../../fixtures/cert_no_ext.bin");

/// Ed25519 identity that signed both fixtures.
const SIGNER: [u8; 32] = [
    0x1d, 0x43, 0x78, 0x2b, 0xde, 0x87, 0xc1, 0x45, 0xdf, 0xf1, 0x32, 0xf7, 0x6c, 0x7e, 0x3c, 0x1b,
    0xe4, 0x26, 0x36, 0xf0, 0x66, 0x92, 0x60, 0x93, 0x86, 0x6a, 0x8d, 0x09, 0x19, 0x30, 0x74, 0xcc,
];
/// Ed25519 key certified by both fixtures.
const SUBJECT: [u8; 32] = [
    0xa5, 0x5d, 0xc8, 0xaa, 0x26, 0x09, 0x29, 0x98, 0xea, 0x05, 0xba, 0xa0, 0x70, 0xaa, 0xd2, 0x70,
    0xd3, 0x52, 0x41, 0xb0, 0x47, 0xaf, 0x0f, 0x24, 0xa0, 0xf0, 0x73, 0x01, 0x93, 0xf8, 0x2a, 0x03,
];

fn signer() -> Ed25519Identity {
    Ed25519Identity::from(SIGNER)
}

#[test]
fn certcheck__decode_reads_the_header_and_subject_key() {
    let c = Ed25519Cert::decode(CERT_WITH_EXT).expect("well-formed certificate");
    assert_eq!(c.peek_cert_type(), CertType::IDENTITY_V_SIGNING);
    assert_eq!(u8::from(c.peek_cert_type()), 4);
    let subject = c.peek_subject_key();
    assert_eq!(subject.key_type(), KeyType::ED25519_KEY);
    crate::common::assert_bytes_eq(subject.as_bytes(), &SUBJECT, "certified key");
    let id = subject
        .as_ed25519()
        .expect("an ED25519_KEY subject is an Ed25519 identity");
    assert_eq!(id, &Ed25519Identity::from(SUBJECT));
}

#[test]
fn certcheck__signing_key_is_taken_from_the_extension() {
    let c = Ed25519Cert::decode(CERT_WITH_EXT).expect("well-formed certificate");
    let unchecked = c
        .check_key(None)
        .expect("the SIGNED_WITH_ED25519_KEY extension supplies the signer");
    assert_eq!(unchecked.peek_signing_key(), &signer());
}

#[test]
fn certcheck__check_key_accepts_a_matching_external_key() {
    let c = Ed25519Cert::decode(CERT_WITH_EXT).expect("well-formed certificate");
    let unchecked = c
        .check_key(Some(&signer()))
        .expect("the supplied key matches the extension");
    assert_eq!(unchecked.peek_signing_key(), &signer());
}

#[test]
fn certcheck__check_key_rejects_a_mismatched_external_key() {
    let c = Ed25519Cert::decode(CERT_WITH_EXT).expect("well-formed certificate");
    let other = Ed25519Identity::from([0x11_u8; 32]);
    let err = c
        .check_key(Some(&other))
        .err()
        .expect("a different key than the extension names");
    assert_eq!(err, CertError::KeyMismatch);
    assert_eq!(
        crate::common::shown(&err),
        "Key on certificate was not as expected"
    );
}

#[test]
fn certcheck__no_extension_and_no_external_key_is_missing_pubkey() {
    let c = Ed25519Cert::decode(CERT_NO_EXT).expect("well-formed certificate");
    assert_eq!(c.peek_cert_type(), CertType::IDENTITY_V_SIGNING);
    let err = c
        .check_key(None)
        .err()
        .expect("nothing names the signing key");
    assert_eq!(err, CertError::MissingPubKey);
    assert_eq!(
        crate::common::shown(&err),
        "Missing signing key on certificate"
    );
}

#[test]
fn certcheck__external_key_supplies_an_absent_signing_key() {
    let c = Ed25519Cert::decode(CERT_NO_EXT).expect("well-formed certificate");
    let unchecked = c
        .check_key(Some(&signer()))
        .expect("an externally supplied key is accepted when the cert names none");
    assert_eq!(unchecked.peek_signing_key(), &signer());
    let checked = unchecked
        .check_signature()
        .expect("the fixture is signed by this key");
    let cert = checked
        .check_valid_at(&(std::time::UNIX_EPOCH + std::time::Duration::from_secs(600)))
        .expect("the fixture is timely at that instant");
    assert_eq!(cert.signing_key(), Some(&signer()));
}

#[test]
fn certcheck__a_tampered_signature_is_rejected() {
    let mut bytes = CERT_WITH_EXT.to_vec();
    let last = bytes.len() - 1;
    bytes[last] ^= 0x01;
    let unchecked = Ed25519Cert::decode(&bytes)
        .expect("still well-formed")
        .check_key(None)
        .expect("the extension still names the signer");
    let err = unchecked
        .check_signature()
        .err()
        .expect("a flipped signature bit must not verify");
    assert_eq!(err, CertError::BadSignature);
    assert_eq!(
        crate::common::shown(&err),
        "Signature on certificate was invalid"
    );
}

#[test]
fn certcheck__expiry_is_an_hour_count_since_the_unix_epoch() {
    let cert = Ed25519Cert::decode(CERT_WITH_EXT)
        .expect("well-formed certificate")
        .check_key(None)
        .expect("signer from the extension")
        .check_signature()
        .expect("correctly signed")
        .dangerously_assume_timely();
    let want = std::time::UNIX_EPOCH + std::time::Duration::from_secs(500_000 * 3600);
    assert_eq!(cert.expiry(), want, "EXPIRATION_DATE counts hours");
    assert!(!cert.is_expired_at(want - std::time::Duration::from_secs(1)));
    assert!(cert.is_expired_at(want + std::time::Duration::from_secs(1)));
    assert_eq!(cert.cert_type(), CertType::IDENTITY_V_SIGNING);
}

#[test]
fn certcheck__an_expired_certificate_fails_the_timeliness_check() {
    let checked = Ed25519Cert::decode(CERT_WITH_EXT)
        .expect("well-formed certificate")
        .check_key(None)
        .expect("signer from the extension")
        .check_signature()
        .expect("correctly signed");
    let too_late =
        std::time::UNIX_EPOCH + std::time::Duration::from_secs(500_001 * 3600);
    let err = checked
        .check_valid_at(&too_late)
        .err()
        .expect("past the expiry hour");
    assert!(
        matches!(err, tor_checkable::TimeValidityError::Expired(_)),
        "got {:?}",
        err
    );
}

#[test]
fn certcheck__truncated_input_does_not_decode() {
    let err = Ed25519Cert::decode(&CERT_WITH_EXT[..20])
        .err()
        .expect("a 20-byte prefix is not a certificate");
    assert_eq!(err, tor_bytes::Error::Truncated, "got {:?}", err);
}
