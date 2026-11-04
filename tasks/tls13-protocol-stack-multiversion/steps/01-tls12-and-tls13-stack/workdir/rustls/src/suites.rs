use crate::cipher;
use crate::msgs::codec::{Codec, Reader};
use crate::msgs::enums::{CipherSuite, HashAlgorithm, SignatureAlgorithm, SignatureScheme};
use crate::msgs::enums::{NamedGroup, ProtocolVersion};
use crate::msgs::handshake::DecomposedSignatureScheme;
use crate::msgs::handshake::KeyExchangeAlgorithm;
use crate::msgs::handshake::{ClientECDHParams, ServerECDHParams};

use ring;
use std::fmt;

/// Bulk symmetric encryption scheme used by a cipher suite.
#[allow(non_camel_case_types)]
#[derive(Debug, PartialEq)]
pub enum BulkAlgorithm {
    /// AES with 128-bit keys in Galois counter mode.
    AES_128_GCM,

    /// AES with 256-bit keys in Galois counter mode.
    AES_256_GCM,

    /// Chacha20 for confidentiality with poly1305 for authenticity.
    CHACHA20_POLY1305,
}

/// The result of a key exchange.  This has our public key,
/// and the agreed shared secret (also known as the "premaster secret"
/// in TLS1.0-era protocols, and "Z" in TLS1.3).
pub struct KeyExchangeResult {
    pub pubkey: ring::agreement::PublicKey,
    pub shared_secret: Vec<u8>,
}

/// An in-progress key exchange.  This has the algorithm,
/// our private key, and our public key.
pub struct KeyExchange {
    pub group: NamedGroup,
    alg: &'static ring::agreement::Algorithm,
    privkey: ring::agreement::EphemeralPrivateKey,
    pub pubkey: ring::agreement::PublicKey,
}

impl KeyExchange {
    pub fn named_group_to_ecdh_alg(
        group: NamedGroup,
    ) -> Option<&'static ring::agreement::Algorithm> {
        unimplemented!()
    }

    pub fn supported_groups() -> &'static [NamedGroup] {
        unimplemented!()
    }

    pub fn client_ecdhe(kx_params: &[u8]) -> Option<KeyExchangeResult> {
        unimplemented!()
    }

    pub fn start_ecdhe(named_group: NamedGroup) -> Option<KeyExchange> {
        unimplemented!()
    }

    pub fn check_client_params(&self, kx_params: &[u8]) -> bool {
        unimplemented!()
    }

    fn decode_client_params(&self, kx_params: &[u8]) -> Option<ClientECDHParams> {
        unimplemented!()
    }

    pub fn server_complete(self, kx_params: &[u8]) -> Option<KeyExchangeResult> {
        unimplemented!()
    }

    pub fn complete(self, peer: &[u8]) -> Option<KeyExchangeResult> {
        unimplemented!()
    }
}

/// A cipher suite supported by rustls.
///
/// All possible instances of this class are provided by the library in
/// the `ALL_CIPHERSUITES` array.
pub struct SupportedCipherSuite {
    /// The TLS enumeration naming this cipher suite.
    pub suite: CipherSuite,

    /// How to exchange/agree keys.
    pub kx: KeyExchangeAlgorithm,

    /// How to do bulk encryption.
    pub bulk: BulkAlgorithm,

    /// How to do hashing.
    pub hash: HashAlgorithm,

    /// How to sign messages for authentication.
    ///
    /// This is not present for TLS1.3, because authentication is orthogonal
    /// to the ciphersuite concept there.
    pub sign: Option<&'static [SignatureScheme]>,

    /// Encryption key length, for the bulk algorithm.
    pub enc_key_len: usize,

    /// How long the fixed part of the 'IV' is.
    ///
    /// This isn't usually an IV, but we continue the
    /// terminology misuse to match the standard.
    pub fixed_iv_len: usize,

    /// This is a non-standard extension which extends the
    /// key block to provide an initial explicit nonce offset,
    /// in a deterministic and safe way.  GCM needs this,
    /// chacha20poly1305 works this way by design.
    pub explicit_nonce_len: usize,

    pub(crate) hkdf_algorithm: ring::hkdf::Algorithm,
    pub(crate) aead_algorithm: &'static ring::aead::Algorithm,
    pub(crate) build_tls12_encrypter: Option<cipher::BuildTLS12Encrypter>,
    pub(crate) build_tls12_decrypter: Option<cipher::BuildTLS12Decrypter>,
}

impl PartialEq for SupportedCipherSuite {
    fn eq(&self, other: &SupportedCipherSuite) -> bool {
        unimplemented!()
    }
}

impl fmt::Debug for SupportedCipherSuite {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        unimplemented!()
    }
}

impl SupportedCipherSuite {
    /// Which hash function to use with this suite.
    pub fn get_hash(&self) -> &'static ring::digest::Algorithm {
        unimplemented!()
    }

    /// We have parameters and a verified public key in `kx_params`.
    /// Generate an ephemeral key, generate the shared secret, and
    /// return it and the public half in a `KeyExchangeResult`.
    pub fn do_client_kx(&self, kx_params: &[u8]) -> Option<KeyExchangeResult> {
        unimplemented!()
    }

    /// Start the KX process with the given group.  This generates
    /// the server's share, but we don't yet have the client's share.
    pub fn start_server_kx(&self, named_group: NamedGroup) -> Option<KeyExchange> {
        unimplemented!()
    }

    /// Resolve the set of supported `SignatureScheme`s from the
    /// offered `SupportedSignatureSchemes`.  If we return an empty
    /// set, the handshake terminates.
    pub fn resolve_sig_schemes(&self, offered: &[SignatureScheme]) -> Vec<SignatureScheme> {
        unimplemented!()
    }

    /// Length of key block that needs to be output by the key
    /// derivation phase for this suite.
    pub fn key_block_len(&self) -> usize {
        unimplemented!()
    }

    /// Return true if this suite is usable for TLS `version`.
    pub fn usable_for_version(&self, version: ProtocolVersion) -> bool {
        unimplemented!()
    }

    /// Return true if this suite is usable for a key only offering `sigalg`
    /// signatures.  This resolves to true for all TLS1.3 suites.
    pub fn usable_for_sigalg(&self, sigalg: SignatureAlgorithm) -> bool {
        unimplemented!()
    }

    /// Can a session using suite self resume using suite new_suite?
    pub fn can_resume_to(&self, new_suite: &SupportedCipherSuite) -> bool {
        unimplemented!()
    }
}

static TLS12_ECDSA_SCHEMES: &[SignatureScheme] = &[
    SignatureScheme::ED25519,
    SignatureScheme::ECDSA_NISTP521_SHA512,
    SignatureScheme::ECDSA_NISTP384_SHA384,
    SignatureScheme::ECDSA_NISTP256_SHA256,
];

static TLS12_RSA_SCHEMES: &[SignatureScheme] = &[
    SignatureScheme::RSA_PSS_SHA512,
    SignatureScheme::RSA_PSS_SHA384,
    SignatureScheme::RSA_PSS_SHA256,
    SignatureScheme::RSA_PKCS1_SHA512,
    SignatureScheme::RSA_PKCS1_SHA384,
    SignatureScheme::RSA_PKCS1_SHA256,
];

/// The TLS1.2 ciphersuite TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256.
pub static TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256: SupportedCipherSuite =
    SupportedCipherSuite {
        suite: CipherSuite::TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256,
        kx: KeyExchangeAlgorithm::ECDHE,
        sign: Some(TLS12_ECDSA_SCHEMES),
        bulk: BulkAlgorithm::CHACHA20_POLY1305,
        hash: HashAlgorithm::SHA256,
        enc_key_len: 32,
        fixed_iv_len: 12,
        explicit_nonce_len: 0,
        hkdf_algorithm: ring::hkdf::HKDF_SHA256,
        aead_algorithm: &ring::aead::CHACHA20_POLY1305,
        build_tls12_encrypter: Some(cipher::build_tls12_chacha_encrypter),
        build_tls12_decrypter: Some(cipher::build_tls12_chacha_decrypter),
    };

/// The TLS1.2 ciphersuite TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256
pub static TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256: SupportedCipherSuite =
    SupportedCipherSuite {
        suite: CipherSuite::TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256,
        kx: KeyExchangeAlgorithm::ECDHE,
        sign: Some(TLS12_RSA_SCHEMES),
        bulk: BulkAlgorithm::CHACHA20_POLY1305,
        hash: HashAlgorithm::SHA256,
        enc_key_len: 32,
        fixed_iv_len: 12,
        explicit_nonce_len: 0,
        hkdf_algorithm: ring::hkdf::HKDF_SHA256,
        aead_algorithm: &ring::aead::CHACHA20_POLY1305,
        build_tls12_encrypter: Some(cipher::build_tls12_chacha_encrypter),
        build_tls12_decrypter: Some(cipher::build_tls12_chacha_decrypter),
    };

/// The TLS1.2 ciphersuite TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256
pub static TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256: SupportedCipherSuite = SupportedCipherSuite {
    suite: CipherSuite::TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
    kx: KeyExchangeAlgorithm::ECDHE,
    sign: Some(TLS12_RSA_SCHEMES),
    bulk: BulkAlgorithm::AES_128_GCM,
    hash: HashAlgorithm::SHA256,
    enc_key_len: 16,
    fixed_iv_len: 4,
    explicit_nonce_len: 8,
    hkdf_algorithm: ring::hkdf::HKDF_SHA256,
    aead_algorithm: &ring::aead::AES_128_GCM,
    build_tls12_encrypter: Some(cipher::build_tls12_gcm_128_encrypter),
    build_tls12_decrypter: Some(cipher::build_tls12_gcm_128_decrypter),
};

/// The TLS1.2 ciphersuite TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384
pub static TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384: SupportedCipherSuite = SupportedCipherSuite {
    suite: CipherSuite::TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
    kx: KeyExchangeAlgorithm::ECDHE,
    sign: Some(TLS12_RSA_SCHEMES),
    bulk: BulkAlgorithm::AES_256_GCM,
    hash: HashAlgorithm::SHA384,
    enc_key_len: 32,
    fixed_iv_len: 4,
    explicit_nonce_len: 8,
    hkdf_algorithm: ring::hkdf::HKDF_SHA384,
    aead_algorithm: &ring::aead::AES_256_GCM,
    build_tls12_encrypter: Some(cipher::build_tls12_gcm_256_encrypter),
    build_tls12_decrypter: Some(cipher::build_tls12_gcm_256_decrypter),
};

/// The TLS1.2 ciphersuite TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256
pub static TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256: SupportedCipherSuite = SupportedCipherSuite {
    suite: CipherSuite::TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
    kx: KeyExchangeAlgorithm::ECDHE,
    sign: Some(TLS12_ECDSA_SCHEMES),
    bulk: BulkAlgorithm::AES_128_GCM,
    hash: HashAlgorithm::SHA256,
    enc_key_len: 16,
    fixed_iv_len: 4,
    explicit_nonce_len: 8,
    hkdf_algorithm: ring::hkdf::HKDF_SHA256,
    aead_algorithm: &ring::aead::AES_128_GCM,
    build_tls12_encrypter: Some(cipher::build_tls12_gcm_128_encrypter),
    build_tls12_decrypter: Some(cipher::build_tls12_gcm_128_decrypter),
};

/// The TLS1.2 ciphersuite TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384
pub static TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384: SupportedCipherSuite = SupportedCipherSuite {
    suite: CipherSuite::TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,
    kx: KeyExchangeAlgorithm::ECDHE,
    sign: Some(TLS12_ECDSA_SCHEMES),
    bulk: BulkAlgorithm::AES_256_GCM,
    hash: HashAlgorithm::SHA384,
    enc_key_len: 32,
    fixed_iv_len: 4,
    explicit_nonce_len: 8,
    hkdf_algorithm: ring::hkdf::HKDF_SHA384,
    aead_algorithm: &ring::aead::AES_256_GCM,
    build_tls12_encrypter: Some(cipher::build_tls12_gcm_256_encrypter),
    build_tls12_decrypter: Some(cipher::build_tls12_gcm_256_decrypter),
};

/// The TLS1.3 ciphersuite TLS_CHACHA20_POLY1305_SHA256
pub static TLS13_CHACHA20_POLY1305_SHA256: SupportedCipherSuite = SupportedCipherSuite {
    suite: CipherSuite::TLS13_CHACHA20_POLY1305_SHA256,
    kx: KeyExchangeAlgorithm::BulkOnly,
    sign: None,
    bulk: BulkAlgorithm::CHACHA20_POLY1305,
    hash: HashAlgorithm::SHA256,
    enc_key_len: 32,
    fixed_iv_len: 12,
    explicit_nonce_len: 0,
    hkdf_algorithm: ring::hkdf::HKDF_SHA256,
    aead_algorithm: &ring::aead::CHACHA20_POLY1305,
    build_tls12_encrypter: None,
    build_tls12_decrypter: None,
};

/// The TLS1.3 ciphersuite TLS_AES_256_GCM_SHA384
pub static TLS13_AES_256_GCM_SHA384: SupportedCipherSuite = SupportedCipherSuite {
    suite: CipherSuite::TLS13_AES_256_GCM_SHA384,
    kx: KeyExchangeAlgorithm::BulkOnly,
    sign: None,
    bulk: BulkAlgorithm::AES_256_GCM,
    hash: HashAlgorithm::SHA384,
    enc_key_len: 32,
    fixed_iv_len: 12,
    explicit_nonce_len: 0,
    hkdf_algorithm: ring::hkdf::HKDF_SHA384,
    aead_algorithm: &ring::aead::AES_256_GCM,
    build_tls12_encrypter: None,
    build_tls12_decrypter: None,
};

/// The TLS1.3 ciphersuite TLS_AES_128_GCM_SHA256
pub static TLS13_AES_128_GCM_SHA256: SupportedCipherSuite = SupportedCipherSuite {
    suite: CipherSuite::TLS13_AES_128_GCM_SHA256,
    kx: KeyExchangeAlgorithm::BulkOnly,
    sign: None,
    bulk: BulkAlgorithm::AES_128_GCM,
    hash: HashAlgorithm::SHA256,
    enc_key_len: 16,
    fixed_iv_len: 12,
    explicit_nonce_len: 0,
    hkdf_algorithm: ring::hkdf::HKDF_SHA256,
    aead_algorithm: &ring::aead::AES_128_GCM,
    build_tls12_encrypter: None,
    build_tls12_decrypter: None,
};

/// A list of all the cipher suites supported by rustls.
pub static ALL_CIPHERSUITES: [&SupportedCipherSuite; 9] = [
    // TLS1.3 suites
    &TLS13_CHACHA20_POLY1305_SHA256,
    &TLS13_AES_256_GCM_SHA384,
    &TLS13_AES_128_GCM_SHA256,
    // TLS1.2 suites
    &TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256,
    &TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256,
    &TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,
    &TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
    &TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
    &TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
];

// These both O(N^2)!
pub fn choose_ciphersuite_preferring_client(
    client_suites: &[CipherSuite],
    server_suites: &[&'static SupportedCipherSuite],
) -> Option<&'static SupportedCipherSuite> {
    unimplemented!()
}

pub fn choose_ciphersuite_preferring_server(
    client_suites: &[CipherSuite],
    server_suites: &[&'static SupportedCipherSuite],
) -> Option<&'static SupportedCipherSuite> {
    unimplemented!()
}

/// Return a list of the ciphersuites in `all` with the suites
/// incompatible with `SignatureAlgorithm` `sigalg` removed.
pub fn reduce_given_sigalg(
    all: &[&'static SupportedCipherSuite],
    sigalg: SignatureAlgorithm,
) -> Vec<&'static SupportedCipherSuite> {
    unimplemented!()
}

/// Return a list of the ciphersuites in `all` with the suites
/// incompatible with the chosen `version` removed.
pub fn reduce_given_version(
    all: &[&'static SupportedCipherSuite],
    version: ProtocolVersion,
) -> Vec<&'static SupportedCipherSuite> {
    unimplemented!()
}

/// Return true if `sigscheme` is usable by any of the given suites.
pub fn compatible_sigscheme_for_suites(
    sigscheme: SignatureScheme,
    common_suites: &[&'static SupportedCipherSuite],
) -> bool {
    unimplemented!()
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::msgs::enums::CipherSuite;

    #[test]
    fn test_client_pref() {
        unimplemented!()
    }

    #[test]
    fn test_server_pref() {
        unimplemented!()
    }

    #[test]
    fn test_pref_fails() {
        unimplemented!()
    }

    #[test]
    fn test_scs_is_debug() {
        unimplemented!()
    }

    #[test]
    fn test_usable_for_version() {
        unimplemented!()
    }

    #[test]
    fn test_can_resume_to() {
        unimplemented!()
    }
}
