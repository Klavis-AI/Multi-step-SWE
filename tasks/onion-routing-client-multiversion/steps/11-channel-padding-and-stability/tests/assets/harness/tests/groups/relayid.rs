//! Group `relayid`: the relay-identity type enumeration and a single relay
//! identity value (`tor_linkspec::{RelayIdType, RelayId, RelayIdRef,
//! RelayIdError}`).
//!
//! Before this milestone a relay was addressed by two unrelated concrete key
//! types. This milestone introduces one abstraction over "a relay identity of
//! some kind", and its whole value is in the details: the textual form it
//! accepts is the one C tor uses on the control port and in bridge lines, so a
//! parser that is merely "close" silently accepts identities that the rest of
//! the Tor network would reject, or normalises an identity to a string that no
//! longer round-trips. These cases therefore grade the accept/reject boundary
//! and the canonical output form, not just that a parse function exists.
#![allow(non_snake_case)]

use std::str::FromStr;

use tor_linkspec::{RelayId, RelayIdError, RelayIdRef, RelayIdType};

use crate::common::{ed_id, rsa_id};

/// Base-64 (unpadded) of 32 bytes of 0x01 -- a well-formed Ed25519 identity.
const ED_01_B64: &str = "AQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQE";
/// Base-64 (unpadded) of 20 bytes of 0xab -- a well-formed RSA identity.
const RSA_AB_B64: &str = "q6urq6urq6urq6urq6urq6urq6s";
/// Hex of 20 bytes of 0xab.
const RSA_AB_HEX: &str = "abababababababababababababababababababab";

/// An RSA identity may be written with or without the leading `$`; both forms
/// must yield the same identity, and the canonical rendering re-adds the `$`.
/// Dropping the `$` on output would break every consumer that feeds the string
/// back to a bridge line.
#[test]
fn relayid__rsa_hex_accepts_both_forms() {
    let bare = RelayId::from_str(RSA_AB_HEX).expect("bare 40-hex is an RSA identity");
    let dollar =
        RelayId::from_str(&format!("${}", RSA_AB_HEX)).expect("$-prefixed hex is an RSA identity");

    assert_eq!(bare, dollar);
    assert_eq!(bare.id_type(), RelayIdType::Rsa);
    assert_eq!(bare.as_bytes(), &[0xab_u8; 20][..]);
    assert_eq!(bare, rsa_id(0xab));
    // Canonical form always carries the `$`.
    assert_eq!(bare.to_string(), format!("${}", RSA_AB_HEX));
    assert_eq!(dollar.to_string(), format!("${}", RSA_AB_HEX));
}

/// Hex is case-insensitive on input but the canonical form is lowercase, so
/// two spellings of one relay compare equal and render identically.
#[test]
fn relayid__rsa_hex_normalizes_case() {
    let upper = RelayId::from_str(&RSA_AB_HEX.to_uppercase()).expect("uppercase hex parses");
    let lower = RelayId::from_str(RSA_AB_HEX).expect("lowercase hex parses");

    assert_eq!(upper, lower);
    assert_eq!(upper.to_string(), format!("${}", RSA_AB_HEX));
    // ...and the canonical string parses back to the same value.
    let again = RelayId::from_str(&upper.to_string()).expect("canonical form re-parses");
    assert_eq!(again, upper);
}

/// A bare 43-character unpadded base-64 string is an Ed25519 identity, and its
/// canonical form gains the `ed25519:` prefix so it cannot later be mistaken
/// for an identity of another algorithm.
#[test]
fn relayid__ed25519_bare_base64_round_trips() {
    let id = RelayId::from_str(ED_01_B64).expect("43-char base64 is an ed25519 identity");

    assert_eq!(id.id_type(), RelayIdType::Ed25519);
    assert_eq!(id, ed_id(0x01));
    assert_eq!(id.as_bytes(), &[0x01_u8; 32][..]);
    assert_eq!(id.to_string(), format!("ed25519:{}", ED_01_B64));

    let again = RelayId::from_str(&id.to_string()).expect("prefixed form parses");
    assert_eq!(again, id);
    assert_eq!(again.to_string(), id.to_string());
}

/// An explicit algorithm prefix decides the type, overriding what the shape of
/// the payload would otherwise imply, and the algorithm name is matched without
/// regard to case. `rsa:` + base-64 is how a 20-byte identity is written when
/// hex is not wanted.
#[test]
fn relayid__algorithm_prefix_selects_type() {
    let rsa = RelayId::from_str(&format!("rsa:{}", RSA_AB_B64)).expect("rsa: prefix parses");
    assert_eq!(rsa.id_type(), RelayIdType::Rsa);
    assert_eq!(rsa, rsa_id(0xab));
    // The canonical form of an RSA id is hex, whatever form it arrived in.
    assert_eq!(rsa.to_string(), format!("${}", RSA_AB_HEX));

    let rsa_upper =
        RelayId::from_str(&format!("RSA:{}", RSA_AB_B64)).expect("algorithm name is caseless");
    assert_eq!(rsa_upper, rsa);

    let ed = RelayId::from_str(&format!("ED25519:{}", ED_01_B64)).expect("caseless ed25519");
    assert_eq!(ed.id_type(), RelayIdType::Ed25519);
    assert_eq!(ed, ed_id(0x01));
}

/// An unknown algorithm name is rejected rather than guessed at. This is the
/// forward-compatibility boundary: a future identity type must not be silently
/// truncated into one we happen to understand.
#[test]
fn relayid__unknown_algorithm_rejected() {
    let e = RelayId::from_str("merkle-hellman:bestavoided").unwrap_err();
    assert!(
        matches!(e, RelayIdError::UnrecognizedIdType),
        "expected UnrecognizedIdType, got {:?}",
        e
    );

    // The type parser itself agrees, and accepts the two known names caselessly.
    assert_eq!(RelayIdType::from_str("rsa").unwrap(), RelayIdType::Rsa);
    assert_eq!(RelayIdType::from_str("Ed25519").unwrap(), RelayIdType::Ed25519);
    assert!(matches!(
        RelayIdType::from_str("ed448").unwrap_err(),
        RelayIdError::UnrecognizedIdType
    ));
}

/// The error enum distinguishes *why* a string was rejected. A caller that
/// reports "bad identity" for all four cases cannot tell a typo'd digit from a
/// truncated key, so each failure mode is graded separately.
#[test]
fn relayid__error_variants_distinguish_failures() {
    // Too short for base-64 ed25519, no prefix, not 40 hex chars.
    assert!(matches!(
        RelayId::from_str("tooshort").unwrap_err(),
        RelayIdError::BadLength
    ));
    // Right length for hex, but not hex.
    assert!(matches!(
        RelayId::from_str("this_string_is_40_bytes_but_it_isnt_hex!").unwrap_err(),
        RelayIdError::BadHex
    ));
    // Right algorithm, wrong number of bytes.
    assert!(matches!(
        RelayId::from_str(&format!("ed25519:{}", RSA_AB_B64)).unwrap_err(),
        RelayIdError::BadLength
    ));
    // Right length, illegal base-64 character.
    let mut bad = String::from(&ED_01_B64[..ED_01_B64.len() - 1]);
    bad.push('*');
    assert_eq!(bad.len(), 43);
    assert!(matches!(
        RelayId::from_str(&format!("ed25519:{}", bad)).unwrap_err(),
        RelayIdError::BadBase64
    ));
}

/// `RelayIdType` is an enumeration that callers iterate over to handle "every
/// kind of identity a relay can have". The count, the iteration order and the
/// per-type byte length are all part of that contract.
#[test]
fn relayid__type_metadata() {
    assert_eq!(RelayIdType::COUNT, 2);
    let all: Vec<_> = RelayIdType::all_types().collect();
    assert_eq!(all, vec![RelayIdType::Ed25519, RelayIdType::Rsa]);
    assert_eq!(all.len(), RelayIdType::COUNT);

    assert_eq!(RelayIdType::Ed25519.id_len(), 32);
    assert_eq!(RelayIdType::Rsa.id_len(), 20);

    // The human-readable names flag the legacy status of the RSA identity.
    assert_eq!(RelayIdType::Ed25519.to_string(), "Ed25519");
    assert_eq!(RelayIdType::Rsa.to_string(), "RSA (legacy)");
}

/// Building an identity from raw bytes must agree with parsing the text form,
/// and must reject a byte slice of the wrong length for the chosen type rather
/// than padding or truncating it.
#[test]
fn relayid__from_type_and_bytes() {
    let from_bytes = RelayId::from_type_and_bytes(RelayIdType::Rsa, &[0xab; 20]).unwrap();
    assert_eq!(from_bytes, RelayId::from_str(RSA_AB_HEX).unwrap());
    assert_eq!(from_bytes, RelayId::from(rsa_id(0xab)));

    let ed = RelayId::from_type_and_bytes(RelayIdType::Ed25519, &[0x01; 32]).unwrap();
    assert_eq!(ed, RelayId::from(ed_id(0x01)));
    assert_eq!(ed.id_type(), RelayIdType::Ed25519);

    // An ed25519-sized slice is not an RSA identity, and vice versa.
    assert!(matches!(
        RelayId::from_type_and_bytes(RelayIdType::Rsa, &[0x01; 32]).unwrap_err(),
        RelayIdError::BadLength
    ));
    assert!(matches!(
        RelayId::from_type_and_bytes(RelayIdType::Ed25519, &[0xab; 20]).unwrap_err(),
        RelayIdError::BadLength
    ));
}

/// The borrowed form must carry exactly the same information as the owned one
/// and convert back without loss, and comparing it against a bare key type must
/// respect the identity's type -- an RSA reference is never equal to an Ed25519
/// key even if the bytes were to match.
#[test]
fn relayid__ref_matches_owned() {
    let owned = RelayId::from(ed_id(0x02));
    let r = owned.as_ref();

    assert_eq!(r.id_type(), RelayIdType::Ed25519);
    assert_eq!(r.as_bytes(), owned.as_bytes());
    assert_eq!(r.to_owned(), owned);
    assert_eq!(r.to_string(), owned.to_string());
    assert_eq!(r, ed_id(0x02));

    // A reference built directly from a key behaves the same way.
    let rsa = rsa_id(0xab);
    let rsa_ref: RelayIdRef<'_> = (&rsa).into();
    assert_eq!(rsa_ref.id_type(), RelayIdType::Rsa);
    assert_eq!(rsa_ref.to_owned(), RelayId::from(rsa));
    // Different types are never equal, even between references.
    assert_ne!(rsa_ref, r);
    assert_ne!(rsa_ref.to_owned(), owned);
}

/// Serde encodes an identity as its canonical string, so a serialized config or
/// state file is readable and is the same text a human would type. An invalid
/// string must fail deserialization instead of producing a nonsense identity.
#[test]
fn relayid__serde_uses_canonical_string() {
    let id = RelayId::from(ed_id(0x01));
    let json = serde_json::to_string(&id).unwrap();
    assert_eq!(json, format!("\"ed25519:{}\"", ED_01_B64));

    let back: RelayId = serde_json::from_str(&json).unwrap();
    assert_eq!(back, id);

    let rsa = RelayId::from(rsa_id(0xab));
    assert_eq!(
        serde_json::to_string(&rsa).unwrap(),
        format!("\"${}\"", RSA_AB_HEX)
    );
    assert_eq!(
        serde_json::from_str::<RelayId>(&format!("\"${}\"", RSA_AB_HEX)).unwrap(),
        rsa
    );

    assert!(serde_json::from_str::<RelayId>("\"not-an-identity\"").is_err());
}
