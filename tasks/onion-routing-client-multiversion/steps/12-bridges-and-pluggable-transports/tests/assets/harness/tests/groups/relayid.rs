//! Group: relayid — the generalised relay identity, re-checked here.
//!
//! Bridge lines, guard state and channel targets all name relays through the
//! same `RelayId` type that step 11 introduced, so its parsing and display
//! rules have to survive the bridge work unchanged.  The textual forms follow
//! the ones C tor accepts on the command line and in `torrc`.

use std::str::FromStr;

use tor_linkspec::{RelayId, RelayIdError, RelayIdType};

use crate::common::{ed_id, rsa_id, ED_B64, RSA_FINGERPRINT, RSA_HEX};

/// The RSA identity of the test relay, unpadded base64.
const RSA_B64: &str = "L31bjotjtfKpqPnjocTWuODypMY";

#[test]
fn relayid__forty_hex_digits_are_an_rsa_identity_with_or_without_the_dollar() {
    let with = RelayId::from_str(&format!("${}", RSA_FINGERPRINT)).unwrap();
    let without = RelayId::from_str(RSA_FINGERPRINT).unwrap();
    assert_eq!(with, without);
    assert_eq!(with.id_type(), RelayIdType::Rsa);
    assert_eq!(with.as_bytes(), rsa_id().as_bytes());
    // Display always writes the canonical lowercase, dollar-prefixed form.
    assert_eq!(with.to_string(), format!("${}", RSA_HEX));
}

#[test]
fn relayid__a_bare_base64_string_is_an_ed25519_identity() {
    let id = RelayId::from_str(ED_B64).unwrap();
    assert_eq!(id.id_type(), RelayIdType::Ed25519);
    assert_eq!(id.as_bytes(), &ed_id().as_bytes()[..]);
    // Display tags the Ed25519 form, so that it round-trips.
    assert_eq!(id.to_string(), format!("ed25519:{}", ED_B64));
    assert_eq!(RelayId::from_str(&id.to_string()).unwrap(), id);
}

#[test]
fn relayid__an_algorithm_prefix_overrides_the_shape_of_the_key() {
    // A 27-character base64 string is not any recognisable bare form, but with
    // an explicit algorithm name it is an RSA identity.
    let rsa = RelayId::from_str(&format!("rsa:{}", RSA_B64)).unwrap();
    assert_eq!(rsa.id_type(), RelayIdType::Rsa);
    assert_eq!(rsa.as_bytes(), rsa_id().as_bytes());
    assert_eq!(rsa.to_string(), format!("${}", RSA_HEX));

    let ed = RelayId::from_str(&format!("ed25519:{}", ED_B64)).unwrap();
    assert_eq!(ed.id_type(), RelayIdType::Ed25519);

    // The algorithm name itself is matched without regard to case.
    for s in ["rsa", "RSA", "Rsa"] {
        assert_eq!(RelayIdType::from_str(s).unwrap(), RelayIdType::Rsa);
    }
    for s in ["ed25519", "ED25519", "Ed25519"] {
        assert_eq!(RelayIdType::from_str(s).unwrap(), RelayIdType::Ed25519);
    }
    assert!(matches!(
        RelayIdType::from_str("curve25519"),
        Err(RelayIdError::UnrecognizedIdType)
    ));
    assert!(matches!(
        RelayId::from_str("dsa:AAAA"),
        Err(RelayIdError::UnrecognizedIdType)
    ));
}

#[test]
fn relayid__each_identity_type_has_one_fixed_length() {
    assert_eq!(RelayIdType::Ed25519.id_len(), 32);
    assert_eq!(RelayIdType::Rsa.id_len(), 20);
    let all: Vec<_> = RelayIdType::all_types().collect();
    assert_eq!(all, vec![RelayIdType::Ed25519, RelayIdType::Rsa]);
    assert_eq!(RelayIdType::COUNT, 2);
}

#[test]
fn relayid__a_key_of_the_wrong_length_or_alphabet_is_named_as_such() {
    assert!(matches!(
        RelayId::from_type_and_bytes(RelayIdType::Rsa, &[0_u8; 19]),
        Err(RelayIdError::BadLength)
    ));
    assert!(matches!(
        RelayId::from_type_and_bytes(RelayIdType::Ed25519, &[0_u8; 33]),
        Err(RelayIdError::BadLength)
    ));
    // Hex of the right shape but the wrong length.
    assert!(matches!(
        RelayId::from_str("$2f7d5b8e"),
        Err(RelayIdError::BadLength)
    ));
    // Forty characters that are not hex digits at all.
    assert!(matches!(
        RelayId::from_str(&format!("${}", "z".repeat(40))),
        Err(RelayIdError::BadHex)
    ));
    // A tagged key whose base64 payload is the wrong length.
    assert!(matches!(
        RelayId::from_str("ed25519:AAAA"),
        Err(RelayIdError::BadLength)
    ));
}

#[test]
fn relayid__bytes_and_text_describe_the_same_identity() {
    let from_bytes =
        RelayId::from_type_and_bytes(RelayIdType::Rsa, rsa_id().as_bytes()).unwrap();
    assert_eq!(from_bytes, RelayId::from_str(RSA_FINGERPRINT).unwrap());
    assert_eq!(from_bytes, RelayId::from(rsa_id()));

    let ed_bytes = RelayId::from_type_and_bytes(RelayIdType::Ed25519, ed_id().as_bytes()).unwrap();
    assert_eq!(ed_bytes, RelayId::from_str(ED_B64).unwrap());
    assert_eq!(ed_bytes.as_ref().to_owned(), ed_bytes);

    // A `RelayIdRef` compares equal to the bare key it points at.
    assert!(ed_bytes.as_ref() == ed_id());
    assert!(from_bytes.as_ref() == rsa_id());
}
