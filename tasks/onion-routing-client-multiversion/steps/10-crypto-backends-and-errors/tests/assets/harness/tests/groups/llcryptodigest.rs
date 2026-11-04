//! Inherited: the digest aliases re-exported by `tor_llcrypto::d` must keep
//! producing the standard FIPS-202 / FIPS-180-4 outputs no matter which
//! backend is selected.  Every vector here is a published test vector, so the
//! group is anchored outside arti.

#![allow(non_snake_case)]

use digest::{Digest, ExtendableOutput, XofReader};
use tor_llcrypto::d;

/// One-shot digest of `msg`, rendered as lowercase hex.
fn hex_of<D: Digest>(msg: &[u8]) -> String {
    hex::encode(D::digest(msg))
}

/// Squeeze `out_len` bytes out of an extendable-output function.
fn xof_hex<D: Default + ExtendableOutput + digest::Update>(msg: &[u8], out_len: usize) -> String {
    let mut state = D::default();
    digest::Update::update(&mut state, msg);
    let mut reader = state.finalize_xof();
    let mut out = vec![0_u8; out_len];
    reader.read(&mut out);
    hex::encode(out)
}

#[test]
fn llcryptodigest__sha1_matches_its_published_vectors() {
    assert_eq!(
        hex_of::<d::Sha1>(b"abc"),
        "a9993e364706816aba3e25717850c26c9cd0d89d"
    );
    assert_eq!(
        hex_of::<d::Sha1>(b""),
        "da39a3ee5e6b4b0d3255bfef95601890afd80709"
    );
    assert_eq!(<d::Sha1 as Digest>::output_size(), 20);
}

#[test]
fn llcryptodigest__sha256_matches_its_published_vectors() {
    assert_eq!(
        hex_of::<d::Sha256>(b"abc"),
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
    );
    assert_eq!(
        hex_of::<d::Sha256>(b""),
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
    );
    assert_eq!(<d::Sha256 as Digest>::output_size(), 32);
}

#[test]
fn llcryptodigest__sha512_matches_its_published_vector() {
    assert_eq!(
        hex_of::<d::Sha512>(b"abc"),
        "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a\
         2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f"
    );
    assert_eq!(<d::Sha512 as Digest>::output_size(), 64);
}

#[test]
fn llcryptodigest__sha3_256_matches_its_published_vectors() {
    assert_eq!(
        hex_of::<d::Sha3_256>(b"abc"),
        "3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532"
    );
    assert_eq!(
        hex_of::<d::Sha3_256>(b""),
        "a7ffc6f8bf1ed76651c14756a061d662f580ff4de43b49fa82d80a4b80f8434a"
    );
    assert_eq!(<d::Sha3_256 as Digest>::output_size(), 32);
}

#[test]
fn llcryptodigest__the_shake_xofs_squeeze_the_expected_stream() {
    assert_eq!(
        xof_hex::<d::Shake128>(b"abc", 32),
        "5881092dd818bf5cf8a3ddb793fbcba74097d5c526a6d35f97b83351940f2cc8"
    );
    assert_eq!(
        xof_hex::<d::Shake256>(b"abc", 32),
        "483366601360a8771c6863080cc4114d8db44530f8f1e1ee4f94ea37e78b5739"
    );
    // An XOF is a stream: a longer squeeze extends the shorter one rather
    // than replacing it.
    let long = xof_hex::<d::Shake256>(b"abc", 64);
    assert!(long.starts_with(&xof_hex::<d::Shake256>(b"abc", 32)));
    assert_eq!(long.len(), 128);
}

#[test]
fn llcryptodigest__incremental_feeding_equals_a_single_shot() {
    let mut chunked = d::Sha256::new();
    chunked.update(b"a");
    chunked.update(b"b");
    chunked.update(b"c");
    assert_eq!(
        hex::encode(chunked.finalize()),
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
    );

    let mut sha1 = d::Sha1::new();
    for byte in b"abc" {
        sha1.update([*byte]);
    }
    assert_eq!(
        hex::encode(sha1.finalize()),
        "a9993e364706816aba3e25717850c26c9cd0d89d"
    );
}
