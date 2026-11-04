use crate::cipher;
use crate::error::TLSError;
use crate::key;
#[cfg(feature = "logging")]
use crate::log::{debug, error, warn};
use crate::msgs::base::Payload;
use crate::msgs::codec::Codec;
use crate::msgs::deframer::MessageDeframer;
use crate::msgs::enums::{AlertDescription, AlertLevel, ContentType, ProtocolVersion};
use crate::msgs::fragmenter::{MessageFragmenter, MAX_FRAGMENT_LEN};
use crate::msgs::hsjoiner::HandshakeJoiner;
use crate::msgs::message::{BorrowMessage, Message, MessagePayload};
use crate::prf;
use crate::quic;
use crate::rand;
use crate::record_layer;
use crate::suites::SupportedCipherSuite;
use crate::vecbuf::ChunkVecBuffer;
use ring;
use std::io::{Read, Write};

use std::collections::VecDeque;
use std::io;

/// Generalises `ClientSession` and `ServerSession`
pub trait Session: quic::QuicExt + Read + Write + Send + Sync {
    /// Read TLS content from `rd`.  This method does internal
    /// buffering, so `rd` can supply TLS messages in arbitrary-
    /// sized chunks (like a socket or pipe might).
    ///
    /// You should call `process_new_packets` each time a call to
    /// this function succeeds.
    ///
    /// The returned error only relates to IO on `rd`.  TLS-level
    /// errors are emitted from `process_new_packets`.
    ///
    /// This function returns `Ok(0)` when the underlying `rd` does
    /// so.  This typically happens when a socket is cleanly closed,
    /// or a file is at EOF.
    fn read_tls(&mut self, rd: &mut dyn Read) -> Result<usize, io::Error>;

    /// Writes TLS messages to `wr`.
    ///
    /// On success the function returns `Ok(n)` where `n` is a number
    /// of bytes written to `wr`, number of bytes after encoding and
    /// encryption.
    ///
    /// Note that after function return the session buffer maybe not
    /// yet fully flushed. [`wants_write`] function can be used
    /// to check if output buffer is not empty.
    ///
    /// [`wants_write`]: #tymethod.wants_write
    fn write_tls(&mut self, wr: &mut dyn Write) -> Result<usize, io::Error>;

    /// Processes any new packets read by a previous call to `read_tls`.
    /// Errors from this function relate to TLS protocol errors, and
    /// are fatal to the session.  Future calls after an error will do
    /// no new work and will return the same error.
    ///
    /// Success from this function can mean new plaintext is available:
    /// obtain it using `read`.
    fn process_new_packets(&mut self) -> Result<(), TLSError>;

    /// Returns true if the caller should call `read_tls` as soon
    /// as possible.
    fn wants_read(&self) -> bool;

    /// Returns true if the caller should call `write_tls` as soon
    /// as possible.
    fn wants_write(&self) -> bool;

    /// Returns true if the session is currently perform the TLS
    /// handshake.  During this time plaintext written to the
    /// session is buffered in memory.
    fn is_handshaking(&self) -> bool;

    /// Sets a limit on the internal buffers used to buffer
    /// unsent plaintext (prior to completing the TLS handshake)
    /// and unsent TLS records.
    ///
    /// By default, there is no limit.  The limit can be set
    /// at any time, even if the current buffer use is higher.
    fn set_buffer_limit(&mut self, limit: usize);

    /// Queues a close_notify fatal alert to be sent in the next
    /// `write_tls` call.  This informs the peer that the
    /// connection is being closed.
    fn send_close_notify(&mut self);

    /// Retrieves the certificate chain used by the peer to authenticate.
    ///
    /// The order of the certificate chain is as it appears in the TLS
    /// protocol: the first certificate relates to the peer, the
    /// second certifies the first, the third certifies the second, and
    /// so on.
    ///
    /// For clients, this is the certificate chain of the server.
    ///
    /// For servers, this is the certificate chain of the client,
    /// if client authentication was completed.
    ///
    /// The return value is None until this value is available.
    fn get_peer_certificates(&self) -> Option<Vec<key::Certificate>>;

    /// Retrieves the protocol agreed with the peer via ALPN.
    ///
    /// A return value of None after handshake completion
    /// means no protocol was agreed (because no protocols
    /// were offered or accepted by the peer).
    fn get_alpn_protocol(&self) -> Option<&[u8]>;

    /// Retrieves the protocol version agreed with the peer.
    ///
    /// This returns None until the version is agreed.
    fn get_protocol_version(&self) -> Option<ProtocolVersion>;

    /// Derives key material from the agreed session secrets.
    ///
    /// This function fills in `output` with `output.len()` bytes of key
    /// material derived from the master session secret using `label`
    /// and `context` for diversification.
    ///
    /// See RFC5705 for more details on what this does and is for.
    ///
    /// For TLS1.3 connections, this function does not use the
    /// "early" exporter at any point.
    ///
    /// This function fails if called prior to the handshake completing;
    /// check with `is_handshaking()` first.
    fn export_keying_material(
        &self,
        output: &mut [u8],
        label: &[u8],
        context: Option<&[u8]>,
    ) -> Result<(), TLSError>;

    /// Retrieves the ciphersuite agreed with the peer.
    ///
    /// This returns None until the ciphersuite is agreed.
    fn get_negotiated_ciphersuite(&self) -> Option<&'static SupportedCipherSuite>;

    /// This function uses `io` to complete any outstanding IO for
    /// this session.
    ///
    /// This is a convenience function which solely uses other parts
    /// of the public API.
    ///
    /// What this means depends on the session state:
    ///
    /// - If the session `is_handshaking()`, then IO is performed until
    ///   the handshake is complete.
    /// - Otherwise, if `wants_write` is true, `write_tls` is invoked
    ///   until it is all written.
    /// - Otherwise, if `wants_read` is true, `read_tls` is invoked
    ///   once.
    ///
    /// The return value is the number of bytes read from and written
    /// to `io`, respectively.
    ///
    /// This function will block if `io` blocks.
    ///
    /// Errors from TLS record handling (ie, from `process_new_packets()`)
    /// are wrapped in an `io::ErrorKind::InvalidData`-kind error.
    fn complete_io<T>(&mut self, io: &mut T) -> Result<(usize, usize), io::Error>
    where
        Self: Sized,
        T: Read + Write,
    {
        unimplemented!()
    }
}

#[derive(Copy, Clone, Eq, PartialEq)]
pub enum Protocol {
    Tls13,
    #[cfg(feature = "quic")]
    Quic,
}

#[derive(Clone, Debug)]
pub struct SessionRandoms {
    pub we_are_client: bool,
    pub client: [u8; 32],
    pub server: [u8; 32],
}

static TLS12_DOWNGRADE_SENTINEL: &[u8] = &[0x44, 0x4f, 0x57, 0x4e, 0x47, 0x52, 0x44, 0x01];

impl SessionRandoms {
    pub fn for_server() -> SessionRandoms {
        unimplemented!()
    }

    pub fn for_client() -> SessionRandoms {
        unimplemented!()
    }

    pub fn set_tls12_downgrade_marker(&mut self) {
        unimplemented!()
    }

    pub fn has_tls12_downgrade_marker(&mut self) -> bool {
        unimplemented!()
    }
}

fn join_randoms(first: &[u8], second: &[u8]) -> [u8; 64] {
    unimplemented!()
}

/// TLS1.2 per-session keying material
pub struct SessionSecrets {
    pub randoms: SessionRandoms,
    hash: &'static ring::digest::Algorithm,
    pub master_secret: [u8; 48],
}

impl SessionSecrets {
    pub fn new(
        randoms: &SessionRandoms,
        hashalg: &'static ring::digest::Algorithm,
        pms: &[u8],
    ) -> SessionSecrets {
        unimplemented!()
    }

    pub fn new_ems(
        randoms: &SessionRandoms,
        hs_hash: &[u8],
        hashalg: &'static ring::digest::Algorithm,
        pms: &[u8],
    ) -> SessionSecrets {
        unimplemented!()
    }

    pub fn new_resume(
        randoms: &SessionRandoms,
        hashalg: &'static ring::digest::Algorithm,
        master_secret: &[u8],
    ) -> SessionSecrets {
        unimplemented!()
    }

    pub fn make_key_block(&self, len: usize) -> Vec<u8> {
        unimplemented!()
    }

    pub fn get_master_secret(&self) -> Vec<u8> {
        unimplemented!()
    }

    pub fn make_verify_data(&self, handshake_hash: &[u8], label: &[u8]) -> Vec<u8> {
        unimplemented!()
    }

    pub fn client_verify_data(&self, handshake_hash: &[u8]) -> Vec<u8> {
        unimplemented!()
    }

    pub fn server_verify_data(&self, handshake_hash: &[u8]) -> Vec<u8> {
        unimplemented!()
    }

    pub fn export_keying_material(&self, output: &mut [u8], label: &[u8], context: Option<&[u8]>) {
        unimplemented!()
    }
}

// --- Common (to client and server) session functions ---

enum Limit {
    Yes,
    No,
}

/// For TLS1.3 middlebox compatibility mode, how to handle
/// a received ChangeCipherSpec message.
pub enum MiddleboxCCS {
    /// process the message as normal
    Process,

    /// just ignore it
    Drop,
}

pub struct SessionCommon {
    pub negotiated_version: Option<ProtocolVersion>,
    pub is_client: bool,
    pub record_layer: record_layer::RecordLayer,
    suite: Option<&'static SupportedCipherSuite>,
    peer_eof: bool,
    pub traffic: bool,
    pub early_traffic: bool,
    sent_fatal_alert: bool,
    received_middlebox_ccs: bool,
    pub message_deframer: MessageDeframer,
    pub handshake_joiner: HandshakeJoiner,
    pub message_fragmenter: MessageFragmenter,
    received_plaintext: ChunkVecBuffer,
    sendable_plaintext: ChunkVecBuffer,
    pub sendable_tls: ChunkVecBuffer,
    /// Protocol whose key schedule should be used. Unused for TLS < 1.3.
    pub protocol: Protocol,
    #[cfg(feature = "quic")]
    pub(crate) quic: Quic,
}

impl SessionCommon {
    pub fn new(mtu: Option<usize>, client: bool) -> SessionCommon {
        unimplemented!()
    }

    pub fn is_tls13(&self) -> bool {
        unimplemented!()
    }

    pub fn get_suite(&self) -> Option<&'static SupportedCipherSuite> {
        unimplemented!()
    }

    pub fn get_suite_assert(&self) -> &'static SupportedCipherSuite {
        unimplemented!()
    }

    pub fn set_suite(&mut self, suite: &'static SupportedCipherSuite) -> bool {
        unimplemented!()
    }

    pub fn filter_tls13_ccs(&mut self, msg: &Message) -> Result<MiddleboxCCS, TLSError> {
        unimplemented!()
    }

    pub fn decrypt_incoming(&mut self, encr: Message) -> Result<Message, TLSError> {
        unimplemented!()
    }

    pub fn has_readable_plaintext(&self) -> bool {
        unimplemented!()
    }

    pub fn set_buffer_limit(&mut self, limit: usize) {
        unimplemented!()
    }

    pub fn process_alert(&mut self, msg: Message) -> Result<(), TLSError> {
        unimplemented!()
    }

    /// Fragment `m`, encrypt the fragments, and then queue
    /// the encrypted fragments for sending.
    pub fn send_msg_encrypt(&mut self, m: Message) {
        unimplemented!()
    }

    /// Like send_msg_encrypt, but operate on an appdata directly.
    fn send_appdata_encrypt(&mut self, payload: &[u8], limit: Limit) -> usize {
        unimplemented!()
    }

    fn send_single_fragment(&mut self, m: BorrowMessage) {
        unimplemented!()
    }

    /// Are we done? ie, have we processed all received messages,
    /// and received a close_notify to indicate that no new messages
    /// will arrive?
    pub fn connection_at_eof(&self) -> bool {
        unimplemented!()
    }

    /// Read TLS content from `rd`.  This method does internal
    /// buffering, so `rd` can supply TLS messages in arbitrary-
    /// sized chunks (like a socket or pipe might).
    pub fn read_tls(&mut self, rd: &mut dyn Read) -> io::Result<usize> {
        unimplemented!()
    }

    pub fn write_tls(&mut self, wr: &mut dyn Write) -> io::Result<usize> {
        unimplemented!()
    }

    /// Send plaintext application data, fragmenting and
    /// encrypting it as it goes out.
    ///
    /// If internal buffers are too small, this function will not accept
    /// all the data.
    pub fn send_some_plaintext(&mut self, data: &[u8]) -> usize {
        unimplemented!()
    }

    pub fn send_early_plaintext(&mut self, data: &[u8]) -> usize {
        unimplemented!()
    }

    /// Encrypt and send some plaintext `data`.  `limit` controls
    /// whether the per-session buffer limits apply.
    ///
    /// Returns the number of bytes written from `data`: this might
    /// be less than `data.len()` if buffer limits were exceeded.
    fn send_plain(&mut self, data: &[u8], limit: Limit) -> usize {
        unimplemented!()
    }

    pub fn start_traffic(&mut self) {
        unimplemented!()
    }

    /// Send any buffered plaintext.  Plaintext is buffered if
    /// written during handshake.
    pub fn flush_plaintext(&mut self) {
        unimplemented!()
    }

    // Put m into sendable_tls for writing.
    fn queue_tls_message(&mut self, m: Message) {
        unimplemented!()
    }

    /// Send a raw TLS message, fragmenting it if needed.
    pub fn send_msg(&mut self, m: Message, must_encrypt: bool) {
        unimplemented!()
    }

    pub fn take_received_plaintext(&mut self, bytes: Payload) {
        unimplemented!()
    }

    pub fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        unimplemented!()
    }

    pub fn start_encryption_tls12(&mut self, secrets: &SessionSecrets) {
        unimplemented!()
    }

    pub fn send_warning_alert(&mut self, desc: AlertDescription) {
        unimplemented!()
    }

    pub fn send_fatal_alert(&mut self, desc: AlertDescription) {
        unimplemented!()
    }

    pub fn send_close_notify(&mut self) {
        unimplemented!()
    }

    fn send_warning_alert_no_log(&mut self, desc: AlertDescription) {
        unimplemented!()
    }

    pub fn is_quic(&self) -> bool {
        unimplemented!()
    }
}

#[cfg(feature = "quic")]
pub(crate) struct Quic {
    /// QUIC transport parameters received from the peer during the handshake
    pub params: Option<Vec<u8>>,
    pub alert: Option<AlertDescription>,
    pub hs_queue: VecDeque<(bool, Vec<u8>)>,
    pub early_secret: Option<ring::hkdf::Prk>,
    pub hs_secrets: Option<quic::Secrets>,
    pub traffic_secrets: Option<quic::Secrets>,
    /// Whether keys derived from traffic_secrets have been passed to the QUIC implementation
    pub returned_traffic_keys: bool,
}

#[cfg(feature = "quic")]
impl Quic {
    pub fn new() -> Self {
        unimplemented!()
    }
}
