//! ZIP-215: one set of Ed25519 validity rules, shared by batched and single verification.
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! Before ZIP-215 every Zcash implementation had its own opinion about which
//! Ed25519 signatures were valid, because RFC 8032 leaves the hard cases open:
//! whether a non-canonically encoded point may appear in a key or a nonce, and
//! whether the verification equation is checked with or without the cofactor.
//! Two nodes that answered differently would split the chain on a JoinSplit
//! signature nobody had ever looked at. ZIP-215 closes the question by fixing
//! one answer:
//!
//!   * `A` and `R` MUST be encodings of points on the twisted Edwards form of
//!     Curve25519, and **non-canonical encodings MUST be accepted**;
//!   * `s` MUST be an integer strictly less than `l`, the order of the
//!     prime-order subgroup;
//!   * the equation `[8][s]B = [8]R + [8][k]A` MUST be satisfied;
//!   * the cofactorless equation `[s]B = R + [k]A` MUST NOT be used.
//!
//! The consequence that matters is the one this file spends most of its
//! assertions on: because the equation is multiplied through by the cofactor,
//! a batch verifier -- which can only ever check a cofactored equation -- and a
//! single verifier accept *exactly the same* signatures. That equivalence is
//! what makes it safe for a node to verify a block's JoinSplit signatures in
//! one batch and fall back to individual checks when the batch fails.

use std::convert::TryFrom;

use zebra_chain::primitives::ed25519::{
    batch, Error, Signature, SigningKey, VerificationKey, VerificationKeyBytes,
};

use rand_core::{CryptoRng, RngCore};

// ----------------------------------------------------------------- fixtures
//
// Three kinds of fixture, all of them stated in the interface document and
// none of them read out of the crate under test:
//
//   * the first two RFC 8032 Ed25519 test vectors, which pin the signature
//     scheme itself (key derivation, the deterministic nonce, the digest);
//   * the eight canonical encodings of the 8-torsion subgroup of Curve25519
//     and four non-canonical encodings that still decode to points in it,
//     derived from the curve equation `-x^2 + y^2 = 1 + d x^2 y^2` over
//     GF(2^255 - 19);
//   * `l` itself, little-endian, the first scalar a signature may not use.

/// RFC 8032 section 7.1, TEST 1: the empty message.
const RFC8032_1_SK: &str = "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60";
const RFC8032_1_PK: &str = "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a";
const RFC8032_1_SIG: &str = "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b";
const RFC8032_1_MSG: &str = "";

/// RFC 8032 section 7.1, TEST 2: the one-byte message `0x72`.
const RFC8032_2_SK: &str = "4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb";
const RFC8032_2_PK: &str = "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c";
const RFC8032_2_SIG: &str = "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00";
const RFC8032_2_MSG: &str = "72";

/// The eight canonical encodings of the points `P` with `[8]P = O`.
const CANONICAL_TORSION: [&str; 8] = [
    "0000000000000000000000000000000000000000000000000000000000000000",
    "0000000000000000000000000000000000000000000000000000000000000080",
    "0100000000000000000000000000000000000000000000000000000000000000",
    "26e8958fc2b227b045c3f489f2ef98f0d5dfac05d3c63339b13802886d53fc05",
    "26e8958fc2b227b045c3f489f2ef98f0d5dfac05d3c63339b13802886d53fc85",
    "c7176a703d4dd84fba3c0b760d10670f2a2053fa2c39ccc64ec7fd7792ac037a",
    "c7176a703d4dd84fba3c0b760d10670f2a2053fa2c39ccc64ec7fd7792ac03fa",
    "ecffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f",
];

/// Four encodings RFC 8032 would reject as non-canonical -- two set the sign
/// bit on `x = 0`, two encode `y` as `p`, `p + 1` rather than `0`, `1` -- and
/// which ZIP-215 requires an implementation to accept.
const NON_CANONICAL_TORSION: [&str; 4] = [
    "0100000000000000000000000000000000000000000000000000000000000080",
    "ecffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
    "edffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f",
    "eeffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f",
];

/// `y = 2` is not the y-coordinate of any point on Curve25519.
const NOT_ON_CURVE: &str = "0200000000000000000000000000000000000000000000000000000000000000";

/// `l = 2^252 + 27742317777372353535851937790883648493`, little-endian.
const GROUP_ORDER_LE: &str = "edd3f55c1a631258d69cf7a2def9de1400000000000000000000000000000010";

// ------------------------------------------------------------------ helpers

fn bytes32(hex_str: &str) -> [u8; 32] {
    let raw = hex::decode(hex_str).expect("fixture is hex");
    assert_eq!(raw.len(), 32, "fixture {} is not 32 bytes", hex_str);
    let mut out = [0u8; 32];
    out.copy_from_slice(&raw);
    out
}

fn bytes64(hex_str: &str) -> [u8; 64] {
    let raw = hex::decode(hex_str).expect("fixture is hex");
    assert_eq!(raw.len(), 64, "fixture is not 64 bytes");
    let mut out = [0u8; 64];
    out.copy_from_slice(&raw);
    out
}

/// A signature built directly out of its two halves, so that `R` and `s` can be
/// chosen rather than produced by a signer.
fn signature_from_parts(r_bytes: [u8; 32], s_bytes: [u8; 32]) -> Signature {
    let mut raw = [0u8; 64];
    raw[..32].copy_from_slice(&r_bytes);
    raw[32..].copy_from_slice(&s_bytes);
    Signature::from(raw)
}

/// A deterministic, reproducible source of the blinding factors the batch
/// equation needs. Batch verification is randomised so that an adversary
/// cannot choose signatures that cancel; the randomness has to be
/// unpredictable to *it*, not to this test, and a fixed stream keeps a
/// failure reproducible.
struct FixedRng(u64);

impl FixedRng {
    fn new() -> Self {
        FixedRng(0x243f_6a88_85a3_08d3)
    }

    fn step(&mut self) -> u64 {
        // SplitMix64.
        self.0 = self.0.wrapping_add(0x9e37_79b9_7f4a_7c15);
        let mut z = self.0;
        z = (z ^ (z >> 30)).wrapping_mul(0xbf58_476d_1ce4_e5b9);
        z = (z ^ (z >> 27)).wrapping_mul(0x94d0_49bb_1331_11eb);
        z ^ (z >> 31)
    }
}

impl RngCore for FixedRng {
    fn next_u32(&mut self) -> u32 {
        self.step() as u32
    }

    fn next_u64(&mut self) -> u64 {
        self.step()
    }

    fn fill_bytes(&mut self, dest: &mut [u8]) {
        for chunk in dest.chunks_mut(8) {
            let word = self.step().to_le_bytes();
            let n = chunk.len();
            chunk.copy_from_slice(&word[..n]);
        }
    }

    fn try_fill_bytes(&mut self, dest: &mut [u8]) -> Result<(), rand_core::Error> {
        self.fill_bytes(dest);
        Ok(())
    }
}

impl CryptoRng for FixedRng {}

/// Thirty-two distinct signing keys and the messages they sign, so that a
/// batch has something to be a batch of.
fn real_signatures(count: u8) -> Vec<(VerificationKeyBytes, Signature, Vec<u8>)> {
    let mut out = Vec::new();
    for i in 0..count {
        let mut seed = [0u8; 32];
        seed[0] = i;
        seed[31] = 0xa5;
        let sk = SigningKey::from(seed);
        let msg = vec![i; (i as usize % 7) + 1];
        let sig = sk.sign(&msg);
        out.push((VerificationKeyBytes::from(&sk), sig, msg));
    }
    out
}

/// Every `(A, R)` pair drawn from the twelve low-order encodings above, signed
/// with `s = 0`. The cofactored equation reads `[8][0]B = [8]R + [8][k]A`, and
/// `[8]P = O` for every one of these points, so both sides are the identity and
/// every one of the pairs is a valid ZIP-215 signature -- whatever the message.
fn low_order_pairs() -> Vec<(VerificationKeyBytes, Signature)> {
    let mut out = Vec::new();
    for a in CANONICAL_TORSION.iter() {
        for r in CANONICAL_TORSION.iter() {
            out.push((
                VerificationKeyBytes::from(bytes32(a)),
                signature_from_parts(bytes32(r), [0u8; 32]),
            ));
        }
    }
    out
}

// -------------------------------------------------------------------- tests

#[test]
fn the_rfc_8032_test_vectors_sign_and_verify() {
    for (sk_hex, pk_hex, sig_hex, msg_hex) in [
        (RFC8032_1_SK, RFC8032_1_PK, RFC8032_1_SIG, RFC8032_1_MSG),
        (RFC8032_2_SK, RFC8032_2_PK, RFC8032_2_SIG, RFC8032_2_MSG),
    ]
    .iter()
    {
        let msg = hex::decode(msg_hex).expect("message is hex");
        let sk = SigningKey::from(bytes32(sk_hex));
        let sig = Signature::from(bytes64(sig_hex));

        assert_eq!(
            <[u8; 32]>::from(VerificationKeyBytes::from(&sk)),
            bytes32(pk_hex),
            "the verification key derived from RFC 8032 secret key {} is not the one the RFC \
             publishes",
            sk_hex
        );
        assert_eq!(
            <[u8; 64]>::from(sk.sign(&msg)),
            bytes64(sig_hex),
            "signing the RFC 8032 message {:?} did not reproduce the published signature",
            msg_hex
        );

        let vk = VerificationKey::try_from(bytes32(pk_hex))
            .expect("an RFC 8032 verification key must decode");
        vk.verify(&sig, &msg)
            .expect("an RFC 8032 signature must verify under its own key and message");
    }
}

#[test]
fn a_signature_verifies_only_for_the_message_it_signed() {
    let sk = SigningKey::from(bytes32(RFC8032_2_SK));
    let vk = VerificationKey::from(&sk);
    let msg = b"the quick brown fox";
    let sig = sk.sign(msg);

    vk.verify(&sig, msg)
        .expect("a freshly produced signature must verify");

    let mut altered = msg.to_vec();
    altered[0] ^= 0x01;
    assert_eq!(
        vk.verify(&sig, &altered),
        Err(Error::InvalidSignature),
        "a signature verified under a message it was not made for"
    );

    assert_eq!(
        vk.verify(&sig, b""),
        Err(Error::InvalidSignature),
        "a signature over a non-empty message verified under the empty message"
    );
}

#[test]
fn flipping_any_byte_of_a_signature_breaks_verification() {
    let sk = SigningKey::from(bytes32(RFC8032_1_SK));
    let vk = VerificationKey::from(&sk);
    let msg = b"zip 215";
    let good = <[u8; 64]>::from(sk.sign(msg));

    for index in 0..64 {
        let mut raw = good;
        raw[index] ^= 0x40;
        if raw == good {
            continue;
        }
        assert_eq!(
            vk.verify(&Signature::from(raw), msg),
            Err(Error::InvalidSignature),
            "a signature with byte {} altered still verified",
            index
        );
    }
}

#[test]
fn a_signature_does_not_verify_under_a_different_key() {
    let signer = SigningKey::from(bytes32(RFC8032_1_SK));
    let other = VerificationKey::from(&SigningKey::from(bytes32(RFC8032_2_SK)));
    let msg = b"one key, one signature";
    let sig = signer.sign(msg);

    assert_eq!(
        other.verify(&sig, msg),
        Err(Error::InvalidSignature),
        "a signature verified under a key that did not produce it"
    );
}

#[test]
fn all_eight_canonical_low_order_encodings_are_valid_verification_keys() {
    for encoding in CANONICAL_TORSION.iter() {
        let raw = bytes32(encoding);
        VerificationKey::try_from(raw).unwrap_or_else(|e| {
            panic!(
                "the canonical encoding {} of an 8-torsion point was rejected as a verification \
                 key with {:?}; ZIP-215 requires every point on the curve to be accepted",
                encoding, e
            )
        });
        assert_eq!(
            <[u8; 32]>::from(VerificationKeyBytes::from(raw)),
            raw,
            "VerificationKeyBytes altered the {} encoding",
            encoding
        );
    }
}

#[test]
fn non_canonically_encoded_points_are_accepted_as_verification_keys() {
    for encoding in NON_CANONICAL_TORSION.iter() {
        let raw = bytes32(encoding);
        VerificationKey::try_from(raw).unwrap_or_else(|e| {
            panic!(
                "the non-canonical encoding {} was rejected with {:?}; ZIP-215 says non-canonical \
                 encodings of curve points MUST be accepted",
                encoding, e
            )
        });
    }
}

#[test]
fn an_encoding_that_is_not_a_curve_point_is_a_malformed_key() {
    let raw = bytes32(NOT_ON_CURVE);
    assert_eq!(
        VerificationKey::try_from(raw).err(),
        Some(Error::MalformedPublicKey),
        "y = 2 is not a point on Curve25519, so {} must not decode to a verification key",
        NOT_ON_CURVE
    );
}

#[test]
fn every_low_order_key_and_nonce_pair_verifies_under_the_cofactored_equation() {
    // With s = 0 the equation is [8]O = [8]R + [8][k]A, and every A and R here
    // has order dividing 8, so both sides are the identity regardless of k.
    // Under the cofactorless equation almost none of these would verify: that
    // is the whole difference ZIP-215 legislates.
    let pairs = low_order_pairs();
    assert_eq!(pairs.len(), 64, "the fixture builds 8 x 8 pairs");

    let msg = b"ZIP 215 low order test";
    for (index, (vk_bytes, sig)) in pairs.iter().enumerate() {
        let vk = VerificationKey::try_from(*vk_bytes)
            .unwrap_or_else(|e| panic!("pair {}: key did not decode: {:?}", index, e));
        vk.verify(sig, msg).unwrap_or_else(|e| {
            panic!(
                "pair {} ({}) failed to verify with {:?}; the cofactored equation accepts every \
                 low-order (A, R) pair with s = 0",
                index,
                hex::encode(AsRef::<[u8]>::as_ref(vk_bytes)),
                e
            )
        });
    }
}

#[test]
fn the_batch_verifier_accepts_the_same_sixty_four_pairs() {
    let msg = b"ZIP 215 low order test";
    let mut verifier = batch::Verifier::new();
    for (vk_bytes, sig) in low_order_pairs() {
        verifier.queue((vk_bytes, sig, &msg[..]));
    }
    verifier.verify(FixedRng::new()).expect(
        "a batch of low-order ZIP-215 signatures must verify; a batch verifier that rejects them \
         does not agree with single verification",
    );
}

#[test]
fn batch_and_single_verification_agree_on_every_low_order_pair() {
    // The property that lets a node batch a block's signatures and fall back to
    // individual checks: the two paths must classify every input identically.
    let msg = b"ZIP 215 low order test";
    for (index, (vk_bytes, sig)) in low_order_pairs().into_iter().enumerate() {
        let item = batch::Item::from((vk_bytes, sig, &msg[..]));
        let batched = item.clone().verify_single().is_ok();

        let single = VerificationKey::try_from(vk_bytes)
            .map(|vk| vk.verify(&sig, msg).is_ok())
            .unwrap_or(false);

        assert_eq!(
            batched, single,
            "pair {}: the batch item says {} and single verification says {}",
            index, batched, single
        );
        assert!(batched, "pair {} was rejected by both paths", index);
    }
}

#[test]
fn a_scalar_at_or_above_the_group_order_is_rejected() {
    let msg = b"ZIP 215 low order test";
    let r_bytes = bytes32(CANONICAL_TORSION[0]);
    let vk_bytes = VerificationKeyBytes::from(bytes32(CANONICAL_TORSION[0]));
    let vk = VerificationKey::try_from(vk_bytes).expect("the identity is a valid key");

    for s_hex in [GROUP_ORDER_LE, &"ff".repeat(32)[..]].iter() {
        let sig = signature_from_parts(r_bytes, bytes32(s_hex));
        assert_eq!(
            vk.verify(&sig, msg),
            Err(Error::InvalidSignature),
            "s = {} is not less than l, so the signature must be rejected",
            s_hex
        );
        assert!(
            batch::Item::from((vk_bytes, sig, &msg[..]))
                .verify_single()
                .is_err(),
            "s = {} was rejected by single verification but accepted as a batch item",
            s_hex
        );
    }
}

#[test]
fn a_scalar_one_below_the_group_order_is_inside_the_range() {
    // The bound is exclusive, so l - 1 must get as far as the curve equation
    // rather than being turned away for being out of range. It then fails the
    // equation, because the signature was not produced by any signer -- but it
    // fails for that reason and not for the length of the scalar. The two
    // paths must still agree.
    let msg = b"ZIP 215 low order test";
    let mut s = bytes32(GROUP_ORDER_LE);
    s[0] -= 1;
    let sig = signature_from_parts(bytes32(CANONICAL_TORSION[3]), s);
    let vk_bytes = VerificationKeyBytes::from(bytes32(CANONICAL_TORSION[5]));
    let vk = VerificationKey::try_from(vk_bytes).expect("a torsion point is a valid key");

    let single = vk.verify(&sig, msg).is_ok();
    let batched = batch::Item::from((vk_bytes, sig, &msg[..]))
        .verify_single()
        .is_ok();
    assert_eq!(
        single, batched,
        "single verification says {} for s = l - 1 and the batch item says {}",
        single, batched
    );
}

#[test]
fn a_batch_of_real_signatures_verifies_and_one_forgery_spoils_it() {
    let signatures = real_signatures(32);

    let mut verifier = batch::Verifier::new();
    for (vk_bytes, sig, msg) in signatures.iter() {
        verifier.queue((*vk_bytes, *sig, &msg[..]));
    }
    verifier
        .verify(FixedRng::new())
        .expect("a batch of 32 honestly produced signatures must verify");

    // Swap one message for another signer's message. Nothing else changes.
    let mut verifier = batch::Verifier::new();
    for (index, (vk_bytes, sig, msg)) in signatures.iter().enumerate() {
        let payload: &[u8] = if index == 17 {
            &signatures[3].2[..]
        } else {
            &msg[..]
        };
        verifier.queue((*vk_bytes, *sig, payload));
    }
    assert_eq!(
        verifier.verify(FixedRng::new()),
        Err(Error::InvalidSignature),
        "a batch containing one signature over the wrong message must not verify"
    );
}

#[test]
fn an_empty_batch_verifies() {
    batch::Verifier::new()
        .verify(FixedRng::new())
        .expect("a batch with nothing in it has nothing to reject");
}

#[test]
fn key_and_signature_byte_conversions_round_trip() {
    let sk = SigningKey::from(bytes32(RFC8032_1_SK));
    let vk = VerificationKey::from(&sk);
    let vk_bytes = VerificationKeyBytes::from(vk);
    let raw: [u8; 32] = vk_bytes.into();

    assert_eq!(raw, bytes32(RFC8032_1_PK));
    assert_eq!(AsRef::<[u8]>::as_ref(&vk), &raw[..]);
    assert_eq!(
        VerificationKeyBytes::from(VerificationKey::try_from(raw).expect("valid key")),
        vk_bytes
    );
    assert_eq!(
        VerificationKeyBytes::from(
            VerificationKey::try_from(&raw[..]).expect("valid key from a slice")
        ),
        vk_bytes
    );
    assert_eq!(
        VerificationKey::try_from(&raw[..31]).err(),
        Some(Error::InvalidSliceLength),
        "a 31-byte slice is not a verification key"
    );

    let sig = Signature::from(bytes64(RFC8032_1_SIG));
    assert_eq!(<[u8; 64]>::from(sig), bytes64(RFC8032_1_SIG));
    assert_eq!(
        Signature::try_from(&bytes64(RFC8032_1_SIG)[..]).expect("valid signature from a slice"),
        sig
    );
    assert_eq!(
        Signature::try_from(&bytes64(RFC8032_1_SIG)[..63]).err(),
        Some(Error::InvalidSliceLength),
        "a 63-byte slice is not a signature"
    );
}
