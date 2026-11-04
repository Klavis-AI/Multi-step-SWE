//! RedPallas: the async batched verifier over two signature roles.
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! `zebra_chain::primitives::redpallas` itself is not new here: RedDSA (a
//! Schnorr variant) instantiated over the Pallas curve, with its two
//! signature roles, already exists on the predecessor milestone --
//! `SigningKey`, `VerificationKey`, `Signature` and single-signature
//! `sign`/`verify` are unchanged, byte for byte, from the predecessor's
//! source. What this milestone adds is `redpallas::batch`, a new submodule
//! containing `Item` and `Verifier`: the *async batched* verifier the ladder
//! row's name refers to. This driver targets exactly that addition -- it
//! imports `batch`, which does not exist on the predecessor tree, so it does
//! not compile there regardless of which test runs.
//!
//! The two signature roles that share the scheme:
//!
//!   * `SpendAuth` signatures authorize spending a note, and their signing
//!     key can be *rerandomized* per spend so that repeated spends by the
//!     same key are unlinkable on-chain;
//!   * `Binding` signatures prove that a transaction's Orchard value
//!     commitments balance, without revealing the individual amounts.
//!
//! `redpallas::batch::Verifier` accepts `Item`s built from either role in the
//! same batch (each `Item` remembers which generator it needs internally),
//! and verifies all of them with one multiscalar multiplication. There is no
//! RFC or ZIP-published known-answer vector for RedPallas, so every signature
//! this file exercises is produced by the crate's own signing key against a
//! fixed, reproducible source of randomness -- legitimate setup, not a claim
//! borrowed from the code under test. The claims under test are the ones
//! that do not depend on any particular signature being "the right one":
//! that a batch of honestly produced signatures verifies, that one corrupted
//! signature spoils the whole batch, that batched and direct single
//! verification agree, and that an empty batch is trivially valid.

use std::convert::TryFrom;

use rand_core::{CryptoRng, RngCore};

use zebra_chain::primitives::redpallas::{
    batch, Binding, Error, SigType, Signature, SigningKey, SpendAuth, VerificationKey,
    VerificationKeyBytes,
};

// ----------------------------------------------------------------- helpers
//
// A deterministic, reproducible source of the nonces `SigningKey::sign` and
// the blinding factors `Verifier::verify` both require. RedPallas signing is
// randomised, so a fixed stream is what keeps a failure reproducible rather
// than flaky.

struct FixedRng(u64);

impl FixedRng {
    fn new() -> Self {
        FixedRng(0x9e37_79b9_7f4a_7c15)
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

/// A signing key from a small, deterministic, always-canonical seed. `1..=64`
/// is many orders of magnitude below the Pallas scalar field's modulus, so
/// every one of these decodes; the seed is never `0`, so the resulting
/// verification key is never the identity (a key of small order, which the
/// crate's own `TryFrom<VerificationKeyBytes>` rejects elsewhere).
fn signing_key<T: SigType>(seed_byte: u8) -> SigningKey<T> {
    let mut seed = [0u8; 32];
    seed[0] = seed_byte;
    SigningKey::try_from(seed).unwrap_or_else(|e| {
        panic!(
            "seed byte {} did not decode to a signing key scalar: {:?}",
            seed_byte, e
        )
    })
}

/// `count` distinct, genuinely signed `(verification key, signature, message)`
/// triples of sig-type `T`, advancing `rng` as it goes so that every call
/// site's batch draws fresh, reproducible randomness.
fn real_signatures<T: SigType>(
    rng: &mut FixedRng,
    count: u8,
    seed_base: u8,
) -> Vec<(VerificationKeyBytes<T>, Signature<T>, Vec<u8>)> {
    let mut out = Vec::new();
    for i in 0..count {
        let sk: SigningKey<T> = signing_key(seed_base.wrapping_add(i).wrapping_add(1));
        let vk = VerificationKey::from(&sk);
        let msg = vec![seed_base.wrapping_add(i); (i as usize % 5) + 1];
        let sig = sk.sign(&mut *rng, &msg);
        out.push((VerificationKeyBytes::from(vk), sig, msg));
    }
    out
}

// ------------------------------------------------------------------- tests

#[test]
fn the_batch_verifier_accepts_a_mix_of_spendauth_and_binding_signatures() {
    let mut rng = FixedRng::new();
    let spend_auth = real_signatures::<SpendAuth>(&mut rng, 6, 50);
    let binding = real_signatures::<Binding>(&mut rng, 6, 80);

    let mut verifier = batch::Verifier::new();
    for (vk_bytes, sig, msg) in spend_auth.iter() {
        verifier.queue((*vk_bytes, *sig, msg));
    }
    for (vk_bytes, sig, msg) in binding.iter() {
        verifier.queue((*vk_bytes, *sig, msg));
    }

    verifier.verify(FixedRng::new()).expect(
        "a batch of twelve honestly produced signatures, six SpendAuth and six Binding, must \
         verify",
    );
}

#[test]
fn one_corrupted_signature_in_a_mixed_batch_spoils_the_whole_batch() {
    let mut rng = FixedRng::new();
    let spend_auth = real_signatures::<SpendAuth>(&mut rng, 4, 100);
    let binding = real_signatures::<Binding>(&mut rng, 4, 130);

    // A batch of the same eight signatures verifies on its own...
    let mut verifier = batch::Verifier::new();
    for (vk_bytes, sig, msg) in spend_auth.iter() {
        verifier.queue((*vk_bytes, *sig, msg));
    }
    for (vk_bytes, sig, msg) in binding.iter() {
        verifier.queue((*vk_bytes, *sig, msg));
    }
    verifier
        .verify(FixedRng::new())
        .expect("the uncorrupted eight-signature batch must verify");

    // ...but corrupting one signature's bytes, leaving everything else
    // untouched, must fail the entire batch: a batch verifier cannot report
    // "valid" for a set that contains even one forgery.
    let mut verifier = batch::Verifier::new();
    for (index, (vk_bytes, sig, msg)) in spend_auth.iter().enumerate() {
        let sig = if index == 2 {
            let mut bytes: [u8; 64] = (*sig).into();
            bytes[0] ^= 0x01;
            Signature::from(bytes)
        } else {
            *sig
        };
        verifier.queue((*vk_bytes, sig, msg));
    }
    for (vk_bytes, sig, msg) in binding.iter() {
        verifier.queue((*vk_bytes, *sig, msg));
    }
    assert_eq!(
        verifier.verify(FixedRng::new()),
        Err(Error::InvalidSignature),
        "a batch with one corrupted SpendAuth signature among seven good ones must not verify"
    );
}

#[test]
fn batch_item_verify_single_agrees_with_direct_verification_for_both_signature_types() {
    let mut rng = FixedRng::new();
    let spend_auth = real_signatures::<SpendAuth>(&mut rng, 3, 160);
    let binding = real_signatures::<Binding>(&mut rng, 3, 180);

    for (index, (vk_bytes, sig, msg)) in spend_auth.iter().enumerate() {
        let direct = VerificationKey::try_from(*vk_bytes)
            .map(|vk| vk.verify(msg, sig).is_ok())
            .unwrap_or(false);
        let batched = batch::Item::from((*vk_bytes, *sig, msg)).verify_single().is_ok();
        assert_eq!(
            direct, batched,
            "SpendAuth pair {}: direct verification says {}, verify_single says {}",
            index, direct, batched
        );
        assert!(direct, "SpendAuth pair {} was rejected by both paths", index);
    }

    for (index, (vk_bytes, sig, msg)) in binding.iter().enumerate() {
        let direct = VerificationKey::try_from(*vk_bytes)
            .map(|vk| vk.verify(msg, sig).is_ok())
            .unwrap_or(false);
        let batched = batch::Item::from((*vk_bytes, *sig, msg)).verify_single().is_ok();
        assert_eq!(
            direct, batched,
            "Binding pair {}: direct verification says {}, verify_single says {}",
            index, direct, batched
        );
        assert!(direct, "Binding pair {} was rejected by both paths", index);
    }
}

#[test]
fn an_empty_batch_verifies_trivially() {
    batch::Verifier::new()
        .verify(FixedRng::new())
        .expect("a batch with nothing queued has nothing to reject");
}
