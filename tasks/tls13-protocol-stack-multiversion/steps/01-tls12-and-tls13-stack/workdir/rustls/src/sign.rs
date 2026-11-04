use crate::error::TLSError;
use crate::key;
use crate::msgs::enums::{SignatureAlgorithm, SignatureScheme};

use ring::{
    self,
    signature::{self, EcdsaKeyPair, Ed25519KeyPair, RsaKeyPair},
};
use webpki;

use std::mem;
use std::sync::Arc;

/// An abstract signing key.
pub trait SigningKey: Send + Sync {
    /// Choose a `SignatureScheme` from those offered.
    ///
    /// Expresses the choice by returning something that implements `Signer`,
    /// using the chosen scheme.
    fn choose_scheme(&self, offered: &[SignatureScheme]) -> Option<Box<dyn Signer>>;

    /// What kind of key we have.
    fn algorithm(&self) -> SignatureAlgorithm;
}

/// A thing that can sign a message.
pub trait Signer: Send + Sync {
    /// Signs `message` using the selected scheme.
    fn sign(&self, message: &[u8]) -> Result<Vec<u8>, TLSError>;

    /// Reveals which scheme will be used when you call `sign()`.
    fn get_scheme(&self) -> SignatureScheme;
}

/// A packaged-together certificate chain, matching `SigningKey` and
/// optional stapled OCSP response and/or SCT list.
#[derive(Clone)]
pub struct CertifiedKey {
    /// The certificate chain.
    pub cert: Vec<key::Certificate>,

    /// The certified key.
    pub key: Arc<Box<dyn SigningKey>>,

    /// An optional OCSP response from the certificate issuer,
    /// attesting to its continued validity.
    pub ocsp: Option<Vec<u8>>,

    /// An optional collection of SCTs from CT logs, proving the
    /// certificate is included on those logs.  This must be
    /// a `SignedCertificateTimestampList` encoding; see RFC6962.
    pub sct_list: Option<Vec<u8>>,
}

impl CertifiedKey {
    /// Make a new CertifiedKey, with the given chain and key.
    ///
    /// The cert chain must not be empty. The first certificate in the chain
    /// must be the end-entity certificate.
    pub fn new(cert: Vec<key::Certificate>, key: Arc<Box<dyn SigningKey>>) -> CertifiedKey {
        unimplemented!()
    }

    /// The end-entity certificate.
    pub fn end_entity_cert(&self) -> Result<&key::Certificate, ()> {
        unimplemented!()
    }

    /// Steal ownership of the certificate chain.
    pub fn take_cert(&mut self) -> Vec<key::Certificate> {
        unimplemented!()
    }

    /// Return true if there's an OCSP response.
    pub fn has_ocsp(&self) -> bool {
        unimplemented!()
    }

    /// Steal ownership of the OCSP response.
    pub fn take_ocsp(&mut self) -> Option<Vec<u8>> {
        unimplemented!()
    }

    /// Return true if there's an SCT list.
    pub fn has_sct_list(&self) -> bool {
        unimplemented!()
    }

    /// Steal ownership of the SCT list.
    pub fn take_sct_list(&mut self) -> Option<Vec<u8>> {
        unimplemented!()
    }

    /// Check the certificate chain for validity:
    /// - it should be non-empty list
    /// - the first certificate should be parsable as a x509v3,
    /// - the first certificate should quote the given server name
    ///   (if provided)
    ///
    /// These checks are not security-sensitive.  They are the
    /// *server* attempting to detect accidental misconfiguration.
    pub fn cross_check_end_entity_cert(
        &self,
        name: Option<webpki::DNSNameRef>,
    ) -> Result<(), TLSError> {
        unimplemented!()
    }
}

/// Parse `der` as any supported key encoding/type, returning
/// the first which works.
pub fn any_supported_type(der: &key::PrivateKey) -> Result<Box<dyn SigningKey>, ()> {
    unimplemented!()
}

/// Parse `der` as any ECDSA key type, returning the first which works.
pub fn any_ecdsa_type(der: &key::PrivateKey) -> Result<Box<dyn SigningKey>, ()> {
    unimplemented!()
}

/// Parse `der` as any EdDSA key type, returning the first which works.
pub fn any_eddsa_type(der: &key::PrivateKey) -> Result<Box<dyn SigningKey>, ()> {
    unimplemented!()
}

/// A `SigningKey` for RSA-PKCS1 or RSA-PSS
pub struct RSASigningKey {
    key: Arc<RsaKeyPair>,
}

static ALL_RSA_SCHEMES: &[SignatureScheme] = &[
    SignatureScheme::RSA_PSS_SHA512,
    SignatureScheme::RSA_PSS_SHA384,
    SignatureScheme::RSA_PSS_SHA256,
    SignatureScheme::RSA_PKCS1_SHA512,
    SignatureScheme::RSA_PKCS1_SHA384,
    SignatureScheme::RSA_PKCS1_SHA256,
];

impl RSASigningKey {
    /// Make a new `RSASigningKey` from a DER encoding, in either
    /// PKCS#1 or PKCS#8 format.
    pub fn new(der: &key::PrivateKey) -> Result<RSASigningKey, ()> {
        unimplemented!()
    }
}

impl SigningKey for RSASigningKey {
    fn choose_scheme(&self, offered: &[SignatureScheme]) -> Option<Box<dyn Signer>> {
        unimplemented!()
    }

    fn algorithm(&self) -> SignatureAlgorithm {
        unimplemented!()
    }
}

struct RSASigner {
    key: Arc<RsaKeyPair>,
    scheme: SignatureScheme,
    encoding: &'static dyn signature::RsaEncoding,
}

impl RSASigner {
    fn new(key: Arc<RsaKeyPair>, scheme: SignatureScheme) -> Box<dyn Signer> {
        unimplemented!()
    }
}

impl Signer for RSASigner {
    fn sign(&self, message: &[u8]) -> Result<Vec<u8>, TLSError> {
        unimplemented!()
    }

    fn get_scheme(&self) -> SignatureScheme {
        unimplemented!()
    }
}

/// A SigningKey that uses exactly one TLS-level SignatureScheme
/// and one ring-level signature::SigningAlgorithm.
///
/// Compare this to RSASigningKey, which for a particular key is
/// willing to sign with several algorithms.  This is quite poor
/// cryptography practice, but is necessary because a given RSA key
/// is expected to work in TLS1.2 (PKCS#1 signatures) and TLS1.3
/// (PSS signatures) -- nobody is willing to obtain certificates for
/// different protocol versions.
///
/// Currently this is only implemented for ECDSA keys.
struct ECDSASigningKey {
    key: Arc<EcdsaKeyPair>,
    scheme: SignatureScheme,
}

impl ECDSASigningKey {
    /// Make a new `ECDSASigningKey` from a DER encoding in PKCS#8 format,
    /// expecting a key usable with precisely the given signature scheme.
    pub fn new(
        der: &key::PrivateKey,
        scheme: SignatureScheme,
        sigalg: &'static signature::EcdsaSigningAlgorithm,
    ) -> Result<ECDSASigningKey, ()> {
        unimplemented!()
    }
}

impl SigningKey for ECDSASigningKey {
    fn choose_scheme(&self, offered: &[SignatureScheme]) -> Option<Box<dyn Signer>> {
        unimplemented!()
    }

    fn algorithm(&self) -> SignatureAlgorithm {
        unimplemented!()
    }
}

struct ECDSASigner {
    key: Arc<EcdsaKeyPair>,
    scheme: SignatureScheme,
}

impl Signer for ECDSASigner {
    fn sign(&self, message: &[u8]) -> Result<Vec<u8>, TLSError> {
        unimplemented!()
    }

    fn get_scheme(&self) -> SignatureScheme {
        unimplemented!()
    }
}

/// A SigningKey that uses exactly one TLS-level SignatureScheme
/// and one ring-level signature::SigningAlgorithm.
///
/// Compare this to RSASigningKey, which for a particular key is
/// willing to sign with several algorithms.  This is quite poor
/// cryptography practice, but is necessary because a given RSA key
/// is expected to work in TLS1.2 (PKCS#1 signatures) and TLS1.3
/// (PSS signatures) -- nobody is willing to obtain certificates for
/// different protocol versions.
///
/// Currently this is only implemented for Ed25519 keys.
struct Ed25519SigningKey {
    key: Arc<Ed25519KeyPair>,
    scheme: SignatureScheme,
}

impl Ed25519SigningKey {
    /// Make a new `Ed25519SigningKey` from a DER encoding in PKCS#8 format,
    /// expecting a key usable with precisely the given signature scheme.
    pub fn new(der: &key::PrivateKey, scheme: SignatureScheme) -> Result<Ed25519SigningKey, ()> {
        unimplemented!()
    }
}

impl SigningKey for Ed25519SigningKey {
    fn choose_scheme(&self, offered: &[SignatureScheme]) -> Option<Box<dyn Signer>> {
        unimplemented!()
    }

    fn algorithm(&self) -> SignatureAlgorithm {
        unimplemented!()
    }
}

struct Ed25519Signer {
    key: Arc<Ed25519KeyPair>,
    scheme: SignatureScheme,
}

impl Signer for Ed25519Signer {
    fn sign(&self, message: &[u8]) -> Result<Vec<u8>, TLSError> {
        unimplemented!()
    }

    fn get_scheme(&self) -> SignatureScheme {
        unimplemented!()
    }
}

/// The set of schemes we support for signatures and
/// that are allowed for TLS1.3.
pub fn supported_sign_tls13() -> &'static [SignatureScheme] {
    unimplemented!()
}
