use sct;
use std;
use std::sync::Arc;
use webpki;

use crate::anchors::OwnedTrustAnchor;
use crate::anchors::{DistinguishedNames, RootCertStore};
use crate::error::TLSError;
use crate::key::Certificate;
#[cfg(feature = "logging")]
use crate::log::{debug, trace, warn};
use crate::msgs::enums::SignatureScheme;
use crate::msgs::handshake::DigitallySignedStruct;
use crate::msgs::handshake::SCTList;

type SignatureAlgorithms = &'static [&'static webpki::SignatureAlgorithm];

/// Which signature verification mechanisms we support.  No particular
/// order.
static SUPPORTED_SIG_ALGS: SignatureAlgorithms = &[
    &webpki::ECDSA_P256_SHA256,
    &webpki::ECDSA_P256_SHA384,
    &webpki::ECDSA_P384_SHA256,
    &webpki::ECDSA_P384_SHA384,
    &webpki::ED25519,
    &webpki::RSA_PSS_2048_8192_SHA256_LEGACY_KEY,
    &webpki::RSA_PSS_2048_8192_SHA384_LEGACY_KEY,
    &webpki::RSA_PSS_2048_8192_SHA512_LEGACY_KEY,
    &webpki::RSA_PKCS1_2048_8192_SHA256,
    &webpki::RSA_PKCS1_2048_8192_SHA384,
    &webpki::RSA_PKCS1_2048_8192_SHA512,
    &webpki::RSA_PKCS1_3072_8192_SHA384,
];

/// Marker types.  These are used to bind the fact some verification
/// (certificate chain or handshake signature) has taken place into
/// protocol states.  We use this to have the compiler check that there
/// are no 'goto fail'-style elisions of important checks before we
/// reach the traffic stage.
///
/// These types are public, but cannot be directly constructed.  This
/// means their origins can be precisely determined by looking
/// for their `assertion` constructors.
pub struct HandshakeSignatureValid(());
impl HandshakeSignatureValid {
    /// Make a `HandshakeSignatureValid`
    pub fn assertion() -> Self {
        unimplemented!()
    }
}

pub struct FinishedMessageVerified(());
impl FinishedMessageVerified {
    pub fn assertion() -> Self {
        unimplemented!()
    }
}

/// Zero-sized marker type representing verification of a server cert chain.
pub struct ServerCertVerified(());
impl ServerCertVerified {
    /// Make a `ServerCertVerified`
    pub fn assertion() -> Self {
        unimplemented!()
    }
}

/// Zero-sized marker type representing verification of a client cert chain.
pub struct ClientCertVerified(());
impl ClientCertVerified {
    /// Make a `ClientCertVerified`
    pub fn assertion() -> Self {
        unimplemented!()
    }
}

/// Something that can verify a server certificate chain, and verify
/// signatures made by certificates.
pub trait ServerCertVerifier: Send + Sync {
    /// Verify a the certificate chain `presented_certs` against the roots
    /// configured in `roots`.  Make sure that `dns_name` is quoted by
    /// the top certificate in the chain.
    fn verify_server_cert(
        &self,
        roots: &RootCertStore,
        presented_certs: &[Certificate],
        dns_name: webpki::DNSNameRef,
        ocsp_response: &[u8],
    ) -> Result<ServerCertVerified, TLSError>;

    /// Verify a signature allegedly by the given server certificate.
    ///
    /// `message` is not hashed, and needs hashing during the verification.
    /// The signature and algorithm are within `dss`.  `cert` contains the
    /// public key to use.
    ///
    /// `cert` is the same certificate that was previously validated by a
    /// call to `verify_server_cert`.
    ///
    /// If and only if the signature is valid, return HandshakeSignatureValid.
    /// Otherwise, return an error -- rustls will send an alert and abort the
    /// connection.
    ///
    /// This method is only called for TLS1.2 handshakes.  Note that, in TLS1.2,
    /// SignatureSchemes such as `SignatureScheme::ECDSA_NISTP256_SHA256` are not
    /// in fact bound to the specific curve implied in their name.
    ///
    /// This trait method has a default implementation that uses webpki to verify
    /// the signature.
    fn verify_tls12_signature(
        &self,
        message: &[u8],
        cert: &Certificate,
        dss: &DigitallySignedStruct,
    ) -> Result<HandshakeSignatureValid, TLSError> {
        unimplemented!()
    }


    /// Verify a signature allegedly by the given server certificate.
    ///
    /// This method is only called for TLS1.3 handshakes.
    ///
    /// This method is very similar to `verify_tls12_signature`: but note the
    /// tighter ECDSA SignatureScheme semantics -- eg `SignatureScheme::ECDSA_NISTP256_SHA256`
    /// must only validate signatures using public keys on the right curve --
    /// rustls does not enforce this requirement for you.
    ///
    /// This trait method has a default implementation that uses webpki to verify
    /// the signature.
    fn verify_tls13_signature(
        &self,
        message: &[u8],
        cert: &Certificate,
        dss: &DigitallySignedStruct,
    ) -> Result<HandshakeSignatureValid, TLSError> {
        unimplemented!()
    }

    /// Return the list of SignatureSchemes that this verifier will handle,
    /// in `verify_tls12_signature` and `verify_tls13_signature` calls.
    ///
    /// This should be in priority order, with the most preferred first.
    ///
    /// This trait mehod has a default implementation that reflects the schemes
    /// supported by webpki.
    fn supported_verify_schemes(&self) -> Vec<SignatureScheme> {
        unimplemented!()
    }
}

/// Something that can verify a client certificate chain
pub trait ClientCertVerifier: Send + Sync {
    /// Returns `true` to enable the server to request a client certificate and
    /// `false` to skip requesting a client certificate. Defaults to `true`.
    fn offer_client_auth(&self) -> bool {
        unimplemented!()
    }

    /// Return `Some(true)` to require a client certificate and `Some(false)` to make
    /// client authentication optional. Return `None` to abort the connection.
    /// Defaults to `Some(self.offer_client_auth())`.
    ///
    /// `sni` is the server name quoted by the client in its ClientHello; it has
    /// been validated as a proper DNS name but is otherwise untrusted.
    fn client_auth_mandatory(&self, _sni: Option<&webpki::DNSName>) -> Option<bool> {
        unimplemented!()
    }

    /// Returns the subject names of the client authentication trust anchors to
    /// share with the client when requesting client authentication.
    ///
    /// Return `None` to abort the connection.
    ///
    /// `sni` is the server name quoted by the client in its ClientHello; it has
    /// been validated as a proper DNS name but is otherwise untrusted.
    fn client_auth_root_subjects(
        &self,
        sni: Option<&webpki::DNSName>,
    ) -> Option<DistinguishedNames>;

    /// Verify a certificate chain. `presented_certs` is the certificate chain from the client.
    ///
    /// `sni` is the server name quoted by the client in its ClientHello; it has
    /// been validated as a proper DNS name but is otherwise untrusted.
    fn verify_client_cert(
        &self,
        presented_certs: &[Certificate],
        sni: Option<&webpki::DNSName>,
    ) -> Result<ClientCertVerified, TLSError>;

    /// Verify a signature allegedly by the given server certificate.
    ///
    /// `message` is not hashed, and needs hashing during the verification.
    /// The signature and algorithm are within `dss`.  `cert` contains the
    /// public key to use.
    ///
    /// `cert` is the same certificate that was previously validated by a
    /// call to `verify_server_cert`.
    ///
    /// If and only if the signature is valid, return HandshakeSignatureValid.
    /// Otherwise, return an error -- rustls will send an alert and abort the
    /// connection.
    ///
    /// This method is only called for TLS1.2 handshakes.  Note that, in TLS1.2,
    /// SignatureSchemes such as `SignatureScheme::ECDSA_NISTP256_SHA256` are not
    /// in fact bound to the specific curve implied in their name.
    ///
    /// This trait method has a default implementation that uses webpki to verify
    /// the signature.
    fn verify_tls12_signature(
        &self,
        message: &[u8],
        cert: &Certificate,
        dss: &DigitallySignedStruct,
    ) -> Result<HandshakeSignatureValid, TLSError> {
        unimplemented!()
    }


    /// Verify a signature allegedly by the given server certificate.
    ///
    /// This method is only called for TLS1.3 handshakes.
    ///
    /// This method is very similar to `verify_tls12_signature`: but note the
    /// tighter ECDSA SignatureScheme semantics -- eg `SignatureScheme::ECDSA_NISTP256_SHA256`
    /// must only validate signatures using public keys on the right curve --
    /// rustls does not enforce this requirement for you.
    ///
    /// This trait method has a default implementation that uses webpki to verify
    /// the signature.
    fn verify_tls13_signature(
        &self,
        message: &[u8],
        cert: &Certificate,
        dss: &DigitallySignedStruct,
    ) -> Result<HandshakeSignatureValid, TLSError> {
        unimplemented!()
    }

    /// Return the list of SignatureSchemes that this verifier will handle,
    /// in `verify_tls12_signature` and `verify_tls13_signature` calls.
    ///
    /// This should be in priority order, with the most preferred first.
    ///
    /// This trait mehod has a default implementation that reflects the schemes
    /// supported by webpki.
    fn supported_verify_schemes(&self) -> Vec<SignatureScheme> {
        unimplemented!()
    }
}

impl ServerCertVerifier for WebPKIVerifier {
    /// Will verify the certificate is valid in the following ways:
    /// - Signed by a  trusted `RootCertStore` CA
    /// - Not Expired
    /// - Valid for DNS entry
    /// - OCSP data is present
    fn verify_server_cert(
        &self,
        roots: &RootCertStore,
        presented_certs: &[Certificate],
        dns_name: webpki::DNSNameRef,
        ocsp_response: &[u8],
    ) -> Result<ServerCertVerified, TLSError> {
        unimplemented!()
    }
}

/// Default `ServerCertVerifier`, see the trait impl for more information.
pub struct WebPKIVerifier {
    /// time provider
    pub time: fn() -> Result<webpki::Time, TLSError>,
}

impl WebPKIVerifier {
    /// Create a new `WebPKIVerifier`
    pub fn new() -> WebPKIVerifier {
        unimplemented!()
    }

    /// Returns the signature verification methods supported by
    /// webpki.
    pub fn verification_schemes() -> Vec<SignatureScheme> {
        unimplemented!()
    }
}

type CertChainAndRoots<'a, 'b> = (
    webpki::EndEntityCert<'a>,
    Vec<&'a [u8]>,
    Vec<webpki::TrustAnchor<'b>>,
);

fn prepare<'a, 'b>(
    roots: &'b RootCertStore,
    presented_certs: &'a [Certificate],
) -> Result<CertChainAndRoots<'a, 'b>, TLSError> {
    unimplemented!()
}

fn try_now() -> Result<webpki::Time, TLSError> {
    unimplemented!()
}

/// A `ClientCertVerifier` that will ensure that every client provides a trusted
/// certificate, without any name checking.
pub struct AllowAnyAuthenticatedClient {
    roots: RootCertStore,
}

impl AllowAnyAuthenticatedClient {
    /// Construct a new `AllowAnyAuthenticatedClient`.
    ///
    /// `roots` is the list of trust anchors to use for certificate validation.
    pub fn new(roots: RootCertStore) -> Arc<dyn ClientCertVerifier> {
        unimplemented!()
    }
}

impl ClientCertVerifier for AllowAnyAuthenticatedClient {
    fn offer_client_auth(&self) -> bool {
        unimplemented!()
    }

    fn client_auth_mandatory(&self, _sni: Option<&webpki::DNSName>) -> Option<bool> {
        unimplemented!()
    }

    fn client_auth_root_subjects(
        &self,
        _sni: Option<&webpki::DNSName>,
    ) -> Option<DistinguishedNames> {
        unimplemented!()
    }

    fn verify_client_cert(
        &self,
        presented_certs: &[Certificate],
        _sni: Option<&webpki::DNSName>,
    ) -> Result<ClientCertVerified, TLSError> {
        unimplemented!()
    }
}

/// A `ClientCertVerifier` that will allow both anonymous and authenticated
/// clients, without any name checking.
///
/// Client authentication will be requested during the TLS handshake. If the
/// client offers a certificate then this acts like
/// `AllowAnyAuthenticatedClient`, otherwise this acts like `NoClientAuth`.
pub struct AllowAnyAnonymousOrAuthenticatedClient {
    inner: AllowAnyAuthenticatedClient,
}

impl AllowAnyAnonymousOrAuthenticatedClient {
    /// Construct a new `AllowAnyAnonymousOrAuthenticatedClient`.
    ///
    /// `roots` is the list of trust anchors to use for certificate validation.
    pub fn new(roots: RootCertStore) -> Arc<dyn ClientCertVerifier> {
        unimplemented!()
    }
}

impl ClientCertVerifier for AllowAnyAnonymousOrAuthenticatedClient {
    fn offer_client_auth(&self) -> bool {
        unimplemented!()
    }

    fn client_auth_mandatory(&self, _sni: Option<&webpki::DNSName>) -> Option<bool> {
        unimplemented!()
    }

    fn client_auth_root_subjects(
        &self,
        sni: Option<&webpki::DNSName>,
    ) -> Option<DistinguishedNames> {
        unimplemented!()
    }

    fn verify_client_cert(
        &self,
        presented_certs: &[Certificate],
        sni: Option<&webpki::DNSName>,
    ) -> Result<ClientCertVerified, TLSError> {
        unimplemented!()
    }
}

/// Turns off client authentication.
pub struct NoClientAuth;

impl NoClientAuth {
    /// Constructs a `NoClientAuth` and wraps it in an `Arc`.
    pub fn new() -> Arc<dyn ClientCertVerifier> {
        unimplemented!()
    }
}

impl ClientCertVerifier for NoClientAuth {
    fn offer_client_auth(&self) -> bool {
        unimplemented!()
    }

    fn client_auth_root_subjects(
        &self,
        _sni: Option<&webpki::DNSName>,
    ) -> Option<DistinguishedNames> {
        unimplemented!()
    }

    fn verify_client_cert(
        &self,
        _presented_certs: &[Certificate],
        _sni: Option<&webpki::DNSName>,
    ) -> Result<ClientCertVerified, TLSError> {
        unimplemented!()
    }
}

static ECDSA_SHA256: SignatureAlgorithms =
    &[&webpki::ECDSA_P256_SHA256, &webpki::ECDSA_P384_SHA256];

static ECDSA_SHA384: SignatureAlgorithms =
    &[&webpki::ECDSA_P256_SHA384, &webpki::ECDSA_P384_SHA384];

static ED25519: SignatureAlgorithms = &[&webpki::ED25519];

static RSA_SHA256: SignatureAlgorithms = &[&webpki::RSA_PKCS1_2048_8192_SHA256];
static RSA_SHA384: SignatureAlgorithms = &[&webpki::RSA_PKCS1_2048_8192_SHA384];
static RSA_SHA512: SignatureAlgorithms = &[&webpki::RSA_PKCS1_2048_8192_SHA512];
static RSA_PSS_SHA256: SignatureAlgorithms = &[&webpki::RSA_PSS_2048_8192_SHA256_LEGACY_KEY];
static RSA_PSS_SHA384: SignatureAlgorithms = &[&webpki::RSA_PSS_2048_8192_SHA384_LEGACY_KEY];
static RSA_PSS_SHA512: SignatureAlgorithms = &[&webpki::RSA_PSS_2048_8192_SHA512_LEGACY_KEY];

fn convert_scheme(scheme: SignatureScheme) -> Result<SignatureAlgorithms, TLSError> {
    unimplemented!()
}

fn verify_sig_using_any_alg(
    cert: &webpki::EndEntityCert,
    algs: SignatureAlgorithms,
    message: &[u8],
    sig: &[u8],
) -> Result<(), webpki::Error> {
    unimplemented!()
}

fn verify_signed_struct(
    message: &[u8],
    cert: &Certificate,
    dss: &DigitallySignedStruct,
) -> Result<HandshakeSignatureValid, TLSError> {
    unimplemented!()
}

fn convert_alg_tls13(
    scheme: SignatureScheme,
) -> Result<&'static webpki::SignatureAlgorithm, TLSError> {
    unimplemented!()
}

/// Constructs the signature message specified in section 4.4.3 of RFC8446.
pub fn construct_tls13_client_verify_message(handshake_hash: &[u8]) -> Vec<u8> {
    unimplemented!()
}

/// Constructs the signature message specified in section 4.4.3 of RFC8446.
pub fn construct_tls13_server_verify_message(handshake_hash: &[u8]) -> Vec<u8> {
    unimplemented!()
}

fn construct_tls13_verify_message(handshake_hash: &[u8], context_string_with_0: &[u8]) -> Vec<u8> {
    unimplemented!()
}

fn verify_tls13(
    msg: &[u8],
    cert: &Certificate,
    dss: &DigitallySignedStruct,
) -> Result<HandshakeSignatureValid, TLSError> {
    unimplemented!()
}

fn unix_time_millis() -> Result<u64, TLSError> {
    unimplemented!()
}

pub fn verify_scts(cert: &Certificate, scts: &SCTList, logs: &[&sct::Log]) -> Result<(), TLSError> {
    unimplemented!()
}
