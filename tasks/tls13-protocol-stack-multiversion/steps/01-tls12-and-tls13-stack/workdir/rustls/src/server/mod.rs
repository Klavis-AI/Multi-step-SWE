use crate::error::TLSError;
use crate::key;
use crate::keylog::{KeyLog, NoKeyLog};
#[cfg(feature = "logging")]
use crate::log::trace;
use crate::msgs::enums::ContentType;
use crate::msgs::enums::SignatureScheme;
use crate::msgs::enums::{AlertDescription, HandshakeType, ProtocolVersion};
use crate::msgs::handshake::ServerExtension;
use crate::msgs::message::Message;
use crate::session::{MiddleboxCCS, Session, SessionCommon};
use crate::sign;
use crate::suites::{SupportedCipherSuite, ALL_CIPHERSUITES};
use crate::verify;

use webpki;

use std::fmt;
use std::io::{self, IoSlice};
use std::sync::Arc;

#[macro_use]
mod hs;
mod common;
pub mod handy;
mod tls12;
mod tls13;

/// A trait for the ability to store server session data.
///
/// The keys and values are opaque.
///
/// Both the keys and values should be treated as
/// **highly sensitive data**, containing enough key material
/// to break all security of the corresponding sessions.
///
/// Implementations can be lossy (in other words, forgetting
/// key/value pairs) without any negative security consequences.
///
/// However, note that `take` **must** reliably delete a returned
/// value.  If it does not, there may be security consequences.
///
/// `put` and `take` are mutating operations; this isn't expressed
/// in the type system to allow implementations freedom in
/// how to achieve interior mutability.  `Mutex` is a common
/// choice.
pub trait StoresServerSessions: Send + Sync {
    /// Store session secrets encoded in `value` against `key`,
    /// overwrites any existing value against `key`.  Returns `true`
    /// if the value was stored.
    fn put(&self, key: Vec<u8>, value: Vec<u8>) -> bool;

    /// Find a value with the given `key`.  Return it, or None
    /// if it doesn't exist.
    fn get(&self, key: &[u8]) -> Option<Vec<u8>>;

    /// Find a value with the given `key`.  Return it and delete it;
    /// or None if it doesn't exist.
    fn take(&self, key: &[u8]) -> Option<Vec<u8>>;
}

/// A trait for the ability to encrypt and decrypt tickets.
pub trait ProducesTickets: Send + Sync {
    /// Returns true if this implementation will encrypt/decrypt
    /// tickets.  Should return false if this is a dummy
    /// implementation: the server will not send the SessionTicket
    /// extension and will not call the other functions.
    fn enabled(&self) -> bool;

    /// Returns the lifetime in seconds of tickets produced now.
    /// The lifetime is provided as a hint to clients that the
    /// ticket will not be useful after the given time.
    ///
    /// This lifetime must be implemented by key rolling and
    /// erasure, *not* by storing a lifetime in the ticket.
    ///
    /// The objective is to limit damage to forward secrecy caused
    /// by tickets, not just limiting their lifetime.
    fn get_lifetime(&self) -> u32;

    /// Encrypt and authenticate `plain`, returning the resulting
    /// ticket.  Return None if `plain` cannot be encrypted for
    /// some reason: an empty ticket will be sent and the connection
    /// will continue.
    fn encrypt(&self, plain: &[u8]) -> Option<Vec<u8>>;

    /// Decrypt `cipher`, validating its authenticity protection
    /// and recovering the plaintext.  `cipher` is fully attacker
    /// controlled, so this decryption must be side-channel free,
    /// panic-proof, and otherwise bullet-proof.  If the decryption
    /// fails, return None.
    fn decrypt(&self, cipher: &[u8]) -> Option<Vec<u8>>;
}

/// How to choose a certificate chain and signing key for use
/// in server authentication.
pub trait ResolvesServerCert: Send + Sync {
    /// Choose a certificate chain and matching key given simplified
    /// ClientHello information.
    ///
    /// Return `None` to abort the handshake.
    fn resolve(&self, client_hello: ClientHello) -> Option<sign::CertifiedKey>;
}

/// A struct representing the received Client Hello
pub struct ClientHello<'a> {
    server_name: Option<webpki::DNSNameRef<'a>>,
    sigschemes: &'a [SignatureScheme],
    alpn: Option<&'a [&'a [u8]]>,
}

impl<'a> ClientHello<'a> {
    /// Creates a new ClientHello
    fn new(
        server_name: Option<webpki::DNSNameRef<'a>>,
        sigschemes: &'a [SignatureScheme],
        alpn: Option<&'a [&'a [u8]]>,
    ) -> Self {
        unimplemented!()
    }

    /// Get the server name indicator.
    ///
    /// Returns `None` if the client did not supply a SNI.
    pub fn server_name(&self) -> Option<webpki::DNSNameRef> {
        unimplemented!()
    }

    /// Get the compatible signature schemes.
    ///
    /// Returns standard-specified default if the client omitted this extension.
    pub fn sigschemes(&self) -> &[SignatureScheme] {
        unimplemented!()
    }

    /// Get the alpn.
    ///
    /// Returns `None` if the client did not include an ALPN extension
    pub fn alpn(&self) -> Option<&'a [&'a [u8]]> {
        unimplemented!()
    }
}

/// Common configuration for a set of server sessions.
///
/// Making one of these can be expensive, and should be
/// once per process rather than once per connection.
#[derive(Clone)]
pub struct ServerConfig {
    /// List of ciphersuites, in preference order.
    pub ciphersuites: Vec<&'static SupportedCipherSuite>,

    /// Ignore the client's ciphersuite order. Instead,
    /// choose the top ciphersuite in the server list
    /// which is supported by the client.
    pub ignore_client_order: bool,

    /// Our MTU.  If None, we don't limit TLS message sizes.
    pub mtu: Option<usize>,

    /// How to store client sessions.
    pub session_storage: Arc<dyn StoresServerSessions + Send + Sync>,

    /// How to produce tickets.
    pub ticketer: Arc<dyn ProducesTickets>,

    /// How to choose a server cert and key.
    pub cert_resolver: Arc<dyn ResolvesServerCert>,

    /// Protocol names we support, most preferred first.
    /// If empty we don't do ALPN at all.
    pub alpn_protocols: Vec<Vec<u8>>,

    /// Supported protocol versions, in no particular order.
    /// The default is all supported versions.
    pub versions: Vec<ProtocolVersion>,

    /// How to verify client certificates.
    verifier: Arc<dyn verify::ClientCertVerifier>,

    /// How to output key material for debugging.  The default
    /// does nothing.
    pub key_log: Arc<dyn KeyLog>,

    /// Amount of early data to accept; 0 to disable.
    #[cfg(feature = "quic")] // TLS support unimplemented
    #[doc(hidden)]
    pub max_early_data_size: u32,
}

impl ServerConfig {
    /// Make a `ServerConfig` with a default set of ciphersuites,
    /// no keys/certificates, and no ALPN protocols.  Session resumption
    /// is enabled by storing up to 256 recent sessions in memory. Tickets are
    /// disabled.
    ///
    /// Publicly-available web servers on the internet generally don't do client
    /// authentication; for this use case, `client_cert_verifier` should be a
    /// `NoClientAuth`. Otherwise, use `AllowAnyAuthenticatedClient` or another
    /// implementation to enforce client authentication.
    ///
    /// We don't provide a default for `client_cert_verifier` because the safest
    /// default, requiring client authentication, requires additional
    /// configuration that we cannot provide reasonable defaults for.
    pub fn new(client_cert_verifier: Arc<dyn verify::ClientCertVerifier>) -> ServerConfig {
        unimplemented!()
    }

    /// Make a `ServerConfig` with a custom set of ciphersuites,
    /// no keys/certificates, and no ALPN protocols.  Session resumption
    /// is enabled by storing up to 256 recent sessions in memory. Tickets are
    /// disabled.
    ///
    /// Publicly-available web servers on the internet generally don't do client
    /// authentication; for this use case, `client_cert_verifier` should be a
    /// `NoClientAuth`. Otherwise, use `AllowAnyAuthenticatedClient` or another
    /// implementation to enforce client authentication.
    ///
    /// We don't provide a default for `client_cert_verifier` because the safest
    /// default, requiring client authentication, requires additional
    /// configuration that we cannot provide reasonable defaults for.
    pub fn with_ciphersuites(
        client_cert_verifier: Arc<dyn verify::ClientCertVerifier>,
        ciphersuites: &[&'static SupportedCipherSuite],
    ) -> ServerConfig {
        unimplemented!()
    }

    #[doc(hidden)]
    /// We support a given TLS version if it's quoted in the configured
    /// versions *and* at least one ciphersuite for this version is
    /// also configured.
    pub fn supports_version(&self, v: ProtocolVersion) -> bool {
        unimplemented!()
    }

    #[doc(hidden)]
    pub fn get_verifier(&self) -> &dyn verify::ClientCertVerifier {
        unimplemented!()
    }

    /// Sets the session persistence layer to `persist`.
    pub fn set_persistence(&mut self, persist: Arc<dyn StoresServerSessions + Send + Sync>) {
        unimplemented!()
    }

    /// Sets a single certificate chain and matching private key.  This
    /// certificate and key is used for all subsequent connections,
    /// irrespective of things like SNI hostname.
    ///
    /// Note that the end-entity certificate must have the
    /// [Subject Alternative Name](https://tools.ietf.org/html/rfc6125#section-4.1)
    /// extension to describe, e.g., the valid DNS name. The `commonName` field is
    /// disregarded.
    ///
    /// `cert_chain` is a vector of DER-encoded certificates.
    /// `key_der` is a DER-encoded RSA, ECDSA, or Ed25519 private key.
    ///
    /// This function fails if `key_der` is invalid.
    pub fn set_single_cert(
        &mut self,
        cert_chain: Vec<key::Certificate>,
        key_der: key::PrivateKey,
    ) -> Result<(), TLSError> {
        unimplemented!()
    }

    /// Sets a single certificate chain, matching private key and OCSP
    /// response.  This certificate and key is used for all subsequent
    /// connections, irrespective of things like SNI hostname.
    ///
    /// `cert_chain` is a vector of DER-encoded certificates.
    /// `key_der` is a DER-encoded RSA, ECDSA, or Ed25519 private key.
    /// `ocsp` is a DER-encoded OCSP response.  Ignored if zero length.
    /// `scts` is an `SignedCertificateTimestampList` encoding (see RFC6962)
    /// and is ignored if empty.
    ///
    /// This function fails if `key_der` is invalid.
    pub fn set_single_cert_with_ocsp_and_sct(
        &mut self,
        cert_chain: Vec<key::Certificate>,
        key_der: key::PrivateKey,
        ocsp: Vec<u8>,
        scts: Vec<u8>,
    ) -> Result<(), TLSError> {
        unimplemented!()
    }

    /// Set the ALPN protocol list to the given protocol names.
    /// Overwrites any existing configured protocols.
    ///
    /// The first element in the `protocols` list is the most
    /// preferred, the last is the least preferred.
    pub fn set_protocols(&mut self, protocols: &[Vec<u8>]) {
        unimplemented!()
    }

    /// Overrides the default `ClientCertVerifier` with something else.
    pub fn set_client_certificate_verifier(
        &mut self,
        verifier: Arc<dyn verify::ClientCertVerifier>,
    ) {
        unimplemented!()
    }
}

pub struct ServerSessionImpl {
    pub config: Arc<ServerConfig>,
    pub common: SessionCommon,
    sni: Option<webpki::DNSName>,
    pub alpn_protocol: Option<Vec<u8>>,
    pub quic_params: Option<Vec<u8>>,
    pub received_resumption_data: Option<Vec<u8>>,
    pub resumption_data: Vec<u8>,
    pub error: Option<TLSError>,
    pub state: Option<Box<dyn hs::State + Send + Sync>>,
    pub client_cert_chain: Option<Vec<key::Certificate>>,
    /// Whether to reject early data even if it would otherwise be accepted
    pub reject_early_data: bool,
}

impl fmt::Debug for ServerSessionImpl {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        unimplemented!()
    }
}

impl ServerSessionImpl {
    pub fn new(
        server_config: &Arc<ServerConfig>,
        extra_exts: Vec<ServerExtension>,
    ) -> ServerSessionImpl {
        unimplemented!()
    }

    pub fn wants_read(&self) -> bool {
        unimplemented!()
    }

    pub fn wants_write(&self) -> bool {
        unimplemented!()
    }

    pub fn is_handshaking(&self) -> bool {
        unimplemented!()
    }

    pub fn set_buffer_limit(&mut self, len: usize) {
        unimplemented!()
    }

    pub fn process_msg(&mut self, mut msg: Message) -> Result<(), TLSError> {
        unimplemented!()
    }

    pub fn process_new_handshake_messages(&mut self) -> Result<(), TLSError> {
        unimplemented!()
    }

    fn queue_unexpected_alert(&mut self) {
        unimplemented!()
    }

    fn maybe_send_unexpected_alert(&mut self, rc: hs::NextStateOrError) -> hs::NextStateOrError {
        unimplemented!()
    }

    pub fn process_main_protocol(&mut self, msg: Message) -> Result<(), TLSError> {
        unimplemented!()
    }

    pub fn process_new_packets(&mut self) -> Result<(), TLSError> {
        unimplemented!()
    }

    pub fn get_peer_certificates(&self) -> Option<Vec<key::Certificate>> {
        unimplemented!()
    }

    pub fn get_alpn_protocol(&self) -> Option<&[u8]> {
        unimplemented!()
    }

    pub fn get_protocol_version(&self) -> Option<ProtocolVersion> {
        unimplemented!()
    }

    pub fn get_negotiated_ciphersuite(&self) -> Option<&'static SupportedCipherSuite> {
        unimplemented!()
    }

    pub fn get_sni(&self) -> Option<&webpki::DNSName> {
        unimplemented!()
    }

    pub fn set_sni(&mut self, value: webpki::DNSName) {
        unimplemented!()
    }

    fn export_keying_material(
        &self,
        output: &mut [u8],
        label: &[u8],
        context: Option<&[u8]>,
    ) -> Result<(), TLSError> {
        unimplemented!()
    }

    fn send_some_plaintext(&mut self, buf: &[u8]) -> usize {
        unimplemented!()
    }
}

/// This represents a single TLS server session.
///
/// Send TLS-protected data to the peer using the `io::Write` trait implementation.
/// Read data from the peer using the `io::Read` trait implementation.
#[derive(Debug)]
pub struct ServerSession {
    // We use the pimpl idiom to hide unimportant details.
    pub(crate) imp: ServerSessionImpl,
}

impl ServerSession {
    /// Make a new ServerSession.  `config` controls how
    /// we behave in the TLS protocol.
    pub fn new(config: &Arc<ServerConfig>) -> ServerSession {
        unimplemented!()
    }

    /// Retrieves the SNI hostname, if any, used to select the certificate and
    /// private key.
    ///
    /// This returns `None` until some time after the client's SNI extension
    /// value is processed during the handshake. It will never be `None` when
    /// the connection is ready to send or process application data, unless the
    /// client does not support SNI.
    ///
    /// This is useful for application protocols that need to enforce that the
    /// SNI hostname matches an application layer protocol hostname. For
    /// example, HTTP/1.1 servers commonly expect the `Host:` header field of
    /// every request on a connection to match the hostname in the SNI extension
    /// when the client provides the SNI extension.
    ///
    /// The SNI hostname is also used to match sessions during session
    /// resumption.
    pub fn get_sni_hostname(&self) -> Option<&str> {
        unimplemented!()
    }

    /// Application-controlled portion of the resumption ticket supplied by the client, if any.
    ///
    /// Recovered from the prior session's `set_resumption_data`. Integrity is guaranteed by rustls.
    ///
    /// Returns `Some` iff a valid resumption ticket has been received from the client.
    pub fn received_resumption_data(&self) -> Option<&[u8]> {
        unimplemented!()
    }

    /// Set the resumption data to embed in future resumption tickets supplied to the client.
    ///
    /// Defaults to the empty byte string. Must be less than 2^15 bytes to allow room for other
    /// data. Should be called while `is_handshaking` returns true to ensure all transmitted
    /// resumption tickets are affected.
    ///
    /// Integrity will be assured by rustls, but the data will be visible to the client. If secrecy
    /// from the client is desired, encrypt the data separately.
    pub fn set_resumption_data(&mut self, data: &[u8]) {
        unimplemented!()
    }

    /// Explicitly discard early data, notifying the client
    ///
    /// Useful if invariants encoded in `received_resumption_data()` cannot be respected.
    ///
    /// Must be called while `is_handshaking` is true.
    pub fn reject_early_data(&mut self) {
        unimplemented!()
    }
}

impl Session for ServerSession {
    fn read_tls(&mut self, rd: &mut dyn io::Read) -> io::Result<usize> {
        unimplemented!()
    }

    /// Writes TLS messages to `wr`.
    fn write_tls(&mut self, wr: &mut dyn io::Write) -> io::Result<usize> {
        unimplemented!()
    }

    fn process_new_packets(&mut self) -> Result<(), TLSError> {
        unimplemented!()
    }

    fn wants_read(&self) -> bool {
        unimplemented!()
    }

    fn wants_write(&self) -> bool {
        unimplemented!()
    }

    fn is_handshaking(&self) -> bool {
        unimplemented!()
    }

    fn set_buffer_limit(&mut self, len: usize) {
        unimplemented!()
    }

    fn send_close_notify(&mut self) {
        unimplemented!()
    }

    fn get_peer_certificates(&self) -> Option<Vec<key::Certificate>> {
        unimplemented!()
    }

    fn get_alpn_protocol(&self) -> Option<&[u8]> {
        unimplemented!()
    }

    fn get_protocol_version(&self) -> Option<ProtocolVersion> {
        unimplemented!()
    }

    fn export_keying_material(
        &self,
        output: &mut [u8],
        label: &[u8],
        context: Option<&[u8]>,
    ) -> Result<(), TLSError> {
        unimplemented!()
    }

    fn get_negotiated_ciphersuite(&self) -> Option<&'static SupportedCipherSuite> {
        unimplemented!()
    }
}

impl io::Read for ServerSession {
    /// Obtain plaintext data received from the peer over this TLS connection.
    ///
    /// If the peer closes the TLS session cleanly, this fails with an error of
    /// kind ErrorKind::ConnectionAborted once all the pending data has been read.
    /// No further data can be received on that connection, so the underlying TCP
    /// connection should closed too.
    ///
    /// Note that support close notify varies in peer TLS libraries: many do not
    /// support it and uncleanly close the TCP connection (this might be
    /// vulnerable to truncation attacks depending on the application protocol).
    /// This means applications using rustls must both handle ErrorKind::ConnectionAborted
    /// from this function, *and* unexpected closure of the underlying TCP connection.
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        unimplemented!()
    }
}

impl io::Write for ServerSession {
    /// Send the plaintext `buf` to the peer, encrypting
    /// and authenticating it.  Once this function succeeds
    /// you should call `write_tls` which will output the
    /// corresponding TLS records.
    ///
    /// This function buffers plaintext sent before the
    /// TLS handshake completes, and sends it as soon
    /// as it can.  This buffer is of *unlimited size* so
    /// writing much data before it can be sent will
    /// cause excess memory usage.
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        unimplemented!()
    }

    fn write_vectored(&mut self, bufs: &[IoSlice<'_>]) -> io::Result<usize> {
        unimplemented!()
    }

    fn flush(&mut self) -> io::Result<()> {
        unimplemented!()
    }
}
