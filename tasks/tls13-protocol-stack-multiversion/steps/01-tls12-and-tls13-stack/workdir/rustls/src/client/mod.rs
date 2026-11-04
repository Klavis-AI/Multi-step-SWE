use crate::anchors;
use crate::error::TLSError;
use crate::key;
use crate::keylog::{KeyLog, NoKeyLog};
#[cfg(feature = "logging")]
use crate::log::trace;
use crate::msgs::enums::CipherSuite;
use crate::msgs::enums::SignatureScheme;
use crate::msgs::enums::{AlertDescription, HandshakeType};
use crate::msgs::enums::{ContentType, ProtocolVersion};
use crate::msgs::handshake::CertificatePayload;
use crate::msgs::handshake::ClientExtension;
use crate::msgs::message::Message;
use crate::session::{MiddleboxCCS, Session, SessionCommon};
use crate::sign;
use crate::suites::{SupportedCipherSuite, ALL_CIPHERSUITES};
use crate::verify;

use std::fmt;
use std::io::{self, IoSlice};
use std::mem;
use std::sync::Arc;

use sct;
use webpki;

#[macro_use]
mod hs;
mod common;
pub mod handy;
mod tls12;
mod tls13;

/// A trait for the ability to store client session data.
/// The keys and values are opaque.
///
/// Both the keys and values should be treated as
/// **highly sensitive data**, containing enough key material
/// to break all security of the corresponding session.
///
/// `put` is a mutating operation; this isn't expressed
/// in the type system to allow implementations freedom in
/// how to achieve interior mutability.  `Mutex` is a common
/// choice.
pub trait StoresClientSessions: Send + Sync {
    /// Stores a new `value` for `key`.  Returns `true`
    /// if the value was stored.
    fn put(&self, key: Vec<u8>, value: Vec<u8>) -> bool;

    /// Returns the latest value for `key`.  Returns `None`
    /// if there's no such value.
    fn get(&self, key: &[u8]) -> Option<Vec<u8>>;
}

/// A trait for the ability to choose a certificate chain and
/// private key for the purposes of client authentication.
pub trait ResolvesClientCert: Send + Sync {
    /// With the server-supplied acceptable issuers in `acceptable_issuers`,
    /// the server's supported signature schemes in `sigschemes`,
    /// return a certificate chain and signing key to authenticate.
    ///
    /// `acceptable_issuers` is undecoded and unverified by the rustls
    /// library, but it should be expected to contain a DER encodings
    /// of X501 NAMEs.
    ///
    /// Return None to continue the handshake without any client
    /// authentication.  The server may reject the handshake later
    /// if it requires authentication.
    fn resolve(
        &self,
        acceptable_issuers: &[&[u8]],
        sigschemes: &[SignatureScheme],
    ) -> Option<sign::CertifiedKey>;

    /// Return true if any certificates at all are available.
    fn has_certs(&self) -> bool;
}

/// Common configuration for (typically) all connections made by
/// a program.
///
/// Making one of these can be expensive, and should be
/// once per process rather than once per connection.
#[derive(Clone)]
pub struct ClientConfig {
    /// List of ciphersuites, in preference order.
    pub ciphersuites: Vec<&'static SupportedCipherSuite>,

    /// Collection of root certificates.
    pub root_store: anchors::RootCertStore,

    /// Which ALPN protocols we include in our client hello.
    /// If empty, no ALPN extension is sent.
    pub alpn_protocols: Vec<Vec<u8>>,

    /// How we store session data or tickets.
    pub session_persistence: Arc<dyn StoresClientSessions>,

    /// Our MTU.  If None, we don't limit TLS message sizes.
    pub mtu: Option<usize>,

    /// How to decide what client auth certificate/keys to use.
    pub client_auth_cert_resolver: Arc<dyn ResolvesClientCert>,

    /// Whether to support RFC5077 tickets.  You must provide a working
    /// `session_persistence` member for this to have any meaningful
    /// effect.
    ///
    /// The default is true.
    pub enable_tickets: bool,

    /// Supported versions, in no particular order.  The default
    /// is all supported versions.
    pub versions: Vec<ProtocolVersion>,

    /// Collection of certificate transparency logs.
    /// If this collection is empty, then certificate transparency
    /// checking is disabled.
    pub ct_logs: Option<&'static [&'static sct::Log<'static>]>,

    /// Whether to send the Server Name Indication (SNI) extension
    /// during the client handshake.
    ///
    /// The default is true.
    pub enable_sni: bool,

    /// How to verify the server certificate chain.
    verifier: Arc<dyn verify::ServerCertVerifier>,

    /// How to output key material for debugging.  The default
    /// does nothing.
    pub key_log: Arc<dyn KeyLog>,

    /// Whether to send data on the first flight ("early data") in
    /// TLS 1.3 handshakes.
    ///
    /// The default is false.
    pub enable_early_data: bool,
}

impl Default for ClientConfig {
    fn default() -> Self {
        unimplemented!()
    }
}

impl ClientConfig {
    /// Make a `ClientConfig` with a default set of ciphersuites,
    /// no root certificates, no ALPN protocols, and no client auth.
    ///
    /// The default session persistence provider stores up to 32
    /// items in memory.
    pub fn new() -> ClientConfig {
        unimplemented!()
    }

    /// Make a `ClientConfig` with a custom set of ciphersuites,
    /// no root certificates, no ALPN protocols, and no client auth.
    ///
    /// The default session persistence provider stores up to 32
    /// items in memory.
    pub fn with_ciphersuites(ciphersuites: &[&'static SupportedCipherSuite]) -> ClientConfig {
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
    pub fn get_verifier(&self) -> &dyn verify::ServerCertVerifier {
        unimplemented!()
    }

    /// Set the ALPN protocol list to the given protocol names.
    /// Overwrites any existing configured protocols.
    /// The first element in the `protocols` list is the most
    /// preferred, the last is the least preferred.
    pub fn set_protocols(&mut self, protocols: &[Vec<u8>]) {
        unimplemented!()
    }

    /// Sets persistence layer to `persist`.
    pub fn set_persistence(&mut self, persist: Arc<dyn StoresClientSessions>) {
        unimplemented!()
    }

    /// Sets MTU to `mtu`.  If None, the default is used.
    /// If Some(x) then x must be greater than 5 bytes.
    pub fn set_mtu(&mut self, mtu: &Option<usize>) {
        unimplemented!()
    }

    /// Sets a single client authentication certificate and private key.
    /// This is blindly used for all servers that ask for client auth.
    ///
    /// `cert_chain` is a vector of DER-encoded certificates,
    /// `key_der` is a DER-encoded RSA or ECDSA private key.
    pub fn set_single_client_cert(
        &mut self,
        cert_chain: Vec<key::Certificate>,
        key_der: key::PrivateKey,
    ) -> Result<(), TLSError> {
        unimplemented!()
    }

    /// Access configuration options whose use is dangerous and requires
    /// extra care.
    #[cfg(feature = "dangerous_configuration")]
    pub fn dangerous(&mut self) -> danger::DangerousClientConfig {
        unimplemented!()
    }
}

/// Container for unsafe APIs
#[cfg(feature = "dangerous_configuration")]
pub mod danger {
    use std::sync::Arc;

    use super::verify::ServerCertVerifier;
    use super::ClientConfig;

    /// Accessor for dangerous configuration options.
    pub struct DangerousClientConfig<'a> {
        /// The underlying ClientConfig
        pub cfg: &'a mut ClientConfig,
    }

    impl<'a> DangerousClientConfig<'a> {
        /// Overrides the default `ServerCertVerifier` with something else.
        pub fn set_certificate_verifier(&mut self, verifier: Arc<dyn ServerCertVerifier>) {
            unimplemented!()
        }
    }
}

#[derive(Debug, PartialEq)]
enum EarlyDataState {
    Disabled,
    Ready,
    Accepted,
    AcceptedFinished,
    Rejected,
}

pub struct EarlyData {
    state: EarlyDataState,
    left: usize,
}

impl EarlyData {
    fn new() -> EarlyData {
        unimplemented!()
    }

    fn is_enabled(&self) -> bool {
        unimplemented!()
    }

    fn is_accepted(&self) -> bool {
        unimplemented!()
    }

    fn enable(&mut self, max_data: usize) {
        unimplemented!()
    }

    fn rejected(&mut self) {
        unimplemented!()
    }

    fn accepted(&mut self) {
        unimplemented!()
    }

    fn finished(&mut self) {
        unimplemented!()
    }

    fn check_write(&mut self, sz: usize) -> io::Result<usize> {
        unimplemented!()
    }

    fn bytes_left(&self) -> usize {
        unimplemented!()
    }
}

/// Stub that implements io::Write and dispatches to `write_early_data`.
pub struct WriteEarlyData<'a> {
    sess: &'a mut ClientSessionImpl,
}

impl<'a> WriteEarlyData<'a> {
    fn new(sess: &'a mut ClientSessionImpl) -> WriteEarlyData<'a> {
        unimplemented!()
    }

    /// How many bytes you may send.  Writes will become short
    /// once this reaches zero.
    pub fn bytes_left(&self) -> usize {
        unimplemented!()
    }
}

impl<'a> io::Write for WriteEarlyData<'a> {
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        unimplemented!()
    }

    fn flush(&mut self) -> io::Result<()> {
        unimplemented!()
    }
}

pub struct ClientSessionImpl {
    pub config: Arc<ClientConfig>,
    pub alpn_protocol: Option<Vec<u8>>,
    pub common: SessionCommon,
    pub error: Option<TLSError>,
    pub state: Option<hs::NextState>,
    pub server_cert_chain: CertificatePayload,
    pub early_data: EarlyData,
    pub resumption_ciphersuite: Option<&'static SupportedCipherSuite>,
}

impl fmt::Debug for ClientSessionImpl {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        unimplemented!()
    }
}

impl ClientSessionImpl {
    pub fn new(config: &Arc<ClientConfig>) -> ClientSessionImpl {
        unimplemented!()
    }

    pub fn start_handshake(&mut self, hostname: webpki::DNSName, extra_exts: Vec<ClientExtension>) {
        unimplemented!()
    }

    pub fn get_cipher_suites(&self) -> Vec<CipherSuite> {
        unimplemented!()
    }

    pub fn find_cipher_suite(&self, suite: CipherSuite) -> Option<&'static SupportedCipherSuite> {
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

    fn reject_renegotiation_attempt(&mut self) -> Result<(), TLSError> {
        unimplemented!()
    }

    fn queue_unexpected_alert(&mut self) {
        unimplemented!()
    }

    fn maybe_send_unexpected_alert(&mut self, rc: hs::NextStateOrError) -> hs::NextStateOrError {
        unimplemented!()
    }

    /// Process `msg`.  First, we get the current state.  Then we ask what messages
    /// that state expects, enforced via `check_message`.  Finally, we ask the handler
    /// to handle the message.
    fn process_main_protocol(&mut self, msg: Message) -> Result<(), TLSError> {
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

    pub fn write_early_data(&mut self, data: &[u8]) -> io::Result<usize> {
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

/// This represents a single TLS client session.
#[derive(Debug)]
pub struct ClientSession {
    // We use the pimpl idiom to hide unimportant details.
    pub(crate) imp: ClientSessionImpl,
}

impl ClientSession {
    /// Make a new ClientSession.  `config` controls how
    /// we behave in the TLS protocol, `hostname` is the
    /// hostname of who we want to talk to.
    pub fn new(config: &Arc<ClientConfig>, hostname: webpki::DNSNameRef) -> ClientSession {
        unimplemented!()
    }

    /// Returns an `io::Write` implementor you can write bytes to
    /// to send TLS1.3 early data (a.k.a. "0-RTT data") to the server.
    ///
    /// This returns None in many circumstances when the capability to
    /// send early data is not available, including but not limited to:
    ///
    /// - The server hasn't been talked to previously.
    /// - The server does not support resumption.
    /// - The server does not support early data.
    /// - The resumption data for the server has expired.
    ///
    /// The server specifies a maximum amount of early data.  You can
    /// learn this limit through the returned object, and writes through
    /// it will process only this many bytes.
    ///
    /// The server can choose not to accept any sent early data --
    /// in this case the data is lost but the connection continues.  You
    /// can tell this happened using `is_early_data_accepted`.
    pub fn early_data(&mut self) -> Option<WriteEarlyData> {
        unimplemented!()
    }

    /// Returns True if the server signalled it will process early data.
    ///
    /// If you sent early data and this returns false at the end of the
    /// handshake then the server will not process the data.  This
    /// is not an error, but you may wish to resend the data.
    pub fn is_early_data_accepted(&self) -> bool {
        unimplemented!()
    }
}

impl Session for ClientSession {
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

impl io::Read for ClientSession {
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

impl io::Write for ClientSession {
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
