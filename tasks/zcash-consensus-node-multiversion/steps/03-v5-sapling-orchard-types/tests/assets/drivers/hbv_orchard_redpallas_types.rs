//! The RedPallas signature primitives and the Orchard curve-point wrapper
//! types built on top of them: `primitives::redpallas::{Signature,
//! SigningKey, VerificationKey, VerificationKeyBytes, Error, SpendAuth}`
//! (the `Randomizer` alias is used through its underlying `pallas::Scalar`),
//! and `orchard::{ValueCommitment, NoteCommitment,
//! EncryptedNote, keys::EphemeralPublicKey}`.
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! `new` group. None of `primitives::redpallas` existed before this step --
//! Sapling's spend/binding signatures use `redjubjub` (a Jubjub-curve scheme,
//! covered by `tx-v5-sapling-transfers`), but Orchard signatures use a
//! separate RedPallas instantiation over the Pallas curve. This milestone
//! adds `group = "0.9"` and the pinned `halo2` git dependency to
//! zebra-chain's `[dependencies]`, and these tests require both to be present
//! there, so `group::prime::PrimeCurveAffine::{identity, generator}`
//! give two guaranteed-valid, canonically-encoded Pallas curve points without
//! needing any external test vector -- the same technique zebra-chain's own
//! `orchard::commitment` unit tests use internally. These two points let this
//! driver pin down the one consensus-relevant difference between
//! `VerificationKey` and the plain Orchard commitment/key types: only
//! `VerificationKey::try_from` rejects small-order points (the identity is
//! the smallest-order point there is; the curve generator has large prime
//! order and should be accepted).

use std::convert::TryFrom;

use group::prime::PrimeCurveAffine;
use group::GroupEncoding;
use halo2::arithmetic::FieldExt;
use halo2::pasta::pallas;

use zebra_chain::orchard::keys::EphemeralPublicKey;
use zebra_chain::orchard::{EncryptedNote, NoteCommitment, ValueCommitment};
use zebra_chain::primitives::redpallas::{
    Error, SigningKey, SpendAuth, VerificationKey, VerificationKeyBytes,
};
use zebra_chain::serialization::{ZcashDeserialize, ZcashSerialize};

// ------------------------------------------------------------------- tests

#[test]
fn verification_key_bytes_is_a_plain_byte_wrapper_with_no_validation() {
    // A byte pattern that is not a valid canonical Pallas point encoding
    // (0..32 ascending) must still round trip: VerificationKeyBytes only
    // becomes a validated VerificationKey through a separate TryFrom step.
    let mut bytes = [0u8; 32];
    for (i, b) in bytes.iter_mut().enumerate() {
        *b = i as u8;
    }
    let wrapped = VerificationKeyBytes::<SpendAuth>::from(bytes);
    let restored: [u8; 32] = wrapped.into();
    assert_eq!(restored, bytes, "VerificationKeyBytes should carry its bytes unchanged");
}

#[test]
fn signature_is_a_plain_64_byte_wrapper_with_no_validation() {
    let mut bytes = [0u8; 64];
    for (i, b) in bytes.iter_mut().enumerate() {
        *b = i as u8;
    }
    let sig = zebra_chain::primitives::redpallas::Signature::<SpendAuth>::from(bytes);
    let restored: [u8; 64] = sig.into();
    assert_eq!(
        restored, bytes,
        "Signature<T> -> [u8; 64] -> Signature<T> should be lossless for any 64 bytes"
    );
}

#[test]
fn signing_key_zero_derives_the_identity_public_key_which_fails_the_small_order_check() {
    let sk = SigningKey::<SpendAuth>::try_from([0u8; 32])
        .expect("[0u8; 32] is always a canonical encoding of the scalar zero");
    let vk = VerificationKey::from(&sk);
    let vk_bytes: VerificationKeyBytes<SpendAuth> = vk.into();

    // basepoint * 0 is the curve identity, which is small-order, so
    // re-validating this key's own bytes must fail -- independent of any
    // assumption about which other points are or are not small-order.
    let revalidated = VerificationKey::<SpendAuth>::try_from(vk_bytes);
    assert_eq!(
        revalidated.err(),
        Some(Error::MalformedVerificationKey),
        "the public key derived from signing key 0 is the identity point, the smallest-order point there is"
    );
}

#[test]
fn signing_key_all_ff_bytes_are_rejected_as_a_non_canonical_scalar() {
    let result = SigningKey::<SpendAuth>::try_from([0xffu8; 32]);
    assert_eq!(
        result.err(),
        Some(Error::MalformedSigningKey),
        "[0xff; 32] encodes 2^256 - 1, which exceeds the Pallas scalar field's modulus"
    );
}

#[test]
fn verification_key_rejects_the_curve_identity_point_as_small_order() {
    let identity_bytes = pallas::Affine::identity().to_bytes();
    let bytes = VerificationKeyBytes::<SpendAuth>::from(identity_bytes);
    let result = VerificationKey::<SpendAuth>::try_from(bytes);
    assert_eq!(result.err(), Some(Error::MalformedVerificationKey));
}

#[test]
fn verification_key_accepts_the_curve_generator_point() {
    let generator_bytes = pallas::Affine::generator().to_bytes();
    let bytes = VerificationKeyBytes::<SpendAuth>::from(generator_bytes);
    let result = VerificationKey::<SpendAuth>::try_from(bytes);
    assert!(
        result.is_ok(),
        "the Pallas curve generator has large prime order and should not be rejected as small-order, got {:?}",
        result.err()
    );
}

#[test]
fn orchard_curve_point_types_accept_both_identity_and_generator_with_no_small_order_check() {
    // Unlike VerificationKey, ValueCommitment/NoteCommitment/EphemeralPublicKey
    // perform only a canonical-encoding check, not a small-order check -- so
    // both the identity and the generator should be accepted by all three.
    let identity_bytes = pallas::Affine::identity().to_bytes();
    let generator_bytes = pallas::Affine::generator().to_bytes();

    assert!(ValueCommitment::try_from(identity_bytes).is_ok(), "ValueCommitment should accept the identity point");
    assert!(ValueCommitment::try_from(generator_bytes).is_ok(), "ValueCommitment should accept the generator");

    assert!(NoteCommitment::try_from(identity_bytes).is_ok(), "NoteCommitment should accept the identity point");
    assert!(NoteCommitment::try_from(generator_bytes).is_ok(), "NoteCommitment should accept the generator");

    assert!(
        EphemeralPublicKey::try_from(identity_bytes).is_ok(),
        "EphemeralPublicKey should accept the identity point"
    );
    assert!(
        EphemeralPublicKey::try_from(generator_bytes).is_ok(),
        "EphemeralPublicKey should accept the generator"
    );
}

#[test]
fn orchard_curve_point_types_reject_a_non_canonical_encoding() {
    // All-0xff is not a valid compressed Pallas point encoding.
    let bad = [0xffu8; 32];
    assert_eq!(ValueCommitment::try_from(bad), Err("Invalid pallas::Affine value"));
    assert_eq!(NoteCommitment::try_from(bad), Err("Invalid pallas::Affine value"));
    assert_eq!(EphemeralPublicKey::try_from(bad), Err("Invalid pallas::Affine value"));
}

#[test]
fn orchard_encrypted_note_is_a_580_byte_wrapper_with_no_validation() {
    let mut bytes = [0u8; 580];
    for (i, b) in bytes.iter_mut().enumerate() {
        *b = (i % 256) as u8;
    }
    let note = EncryptedNote(bytes);
    let mut wire = Vec::new();
    note.zcash_serialize(&mut wire).expect("infallible");
    assert_eq!(wire.len(), 580, "EncryptedNote should serialize to exactly 580 bytes");
    assert_eq!(wire, bytes.to_vec());

    let restored = EncryptedNote::zcash_deserialize(&wire[..]).expect("580 bytes should deserialize");
    assert_eq!(restored, note, "round trip should be lossless for arbitrary bytes");
}

#[test]
fn randomizing_a_verification_key_with_a_nonzero_randomizer_changes_its_bytes() {
    let sk = SigningKey::<SpendAuth>::try_from([0u8; 32]).expect("zero is canonical");
    let vk: VerificationKey<SpendAuth> = VerificationKey::from(&sk);
    let original_bytes: [u8; 32] = VerificationKeyBytes::<SpendAuth>::from(vk).into();

    // A nonzero scalar obtained by reducing an arbitrary 64-byte pattern mod
    // the Pallas scalar field order; the chance a specific fixed pattern
    // happens to reduce to exactly zero is negligible.
    let randomizer: pallas::Scalar = pallas::Scalar::from_bytes_wide(&[0x07u8; 64]);
    let randomized = vk.randomize(&randomizer);
    let randomized_bytes: [u8; 32] = VerificationKeyBytes::<SpendAuth>::from(randomized).into();

    assert_ne!(
        randomized_bytes, original_bytes,
        "randomizing a verification key with a nonzero randomizer should change its encoded bytes"
    );
}
