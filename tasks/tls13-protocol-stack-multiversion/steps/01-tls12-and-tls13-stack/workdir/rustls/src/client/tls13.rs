use crate::check::check_message;
use crate::cipher;
use crate::client::ClientSessionImpl;
use crate::error::TLSError;
use crate::key_schedule::{
    KeyScheduleEarly, KeyScheduleHandshake, KeyScheduleNonSecret, KeyScheduleTraffic,
    KeyScheduleTrafficWithClientFinishedPending,
};
#[cfg(feature = "logging")]
use crate::log::{debug, trace, warn};
use crate::msgs::base::{Payload, PayloadU8};
use crate::msgs::ccs::ChangeCipherSpecPayload;
use crate::msgs::codec::Codec;
use crate::msgs::enums::KeyUpdateRequest;
use crate::msgs::enums::{AlertDescription, NamedGroup, ProtocolVersion};
use crate::msgs::enums::{ContentType, ExtensionType, HandshakeType, SignatureScheme};
use crate::msgs::handshake::DigitallySignedStruct;
use crate::msgs::handshake::EncryptedExtensions;
use crate::msgs::handshake::NewSessionTicketPayloadTLS13;
use crate::msgs::handshake::{CertificateEntry, CertificatePayloadTLS13};
use crate::msgs::handshake::{ClientExtension, HelloRetryRequest, KeyShareEntry};
use crate::msgs::handshake::{HandshakeMessagePayload, HandshakePayload};
use crate::msgs::handshake::{HasServerExtensions, ServerHelloPayload, SessionID};
use crate::msgs::handshake::{PresharedKeyIdentity, PresharedKeyOffer};
use crate::msgs::message::{Message, MessagePayload};
use crate::msgs::persist;
use crate::sign;
use crate::suites;
use crate::ticketer;
use crate::verify;
#[cfg(feature = "quic")]
use crate::{msgs::base::PayloadU16, quic, session::Protocol};

use crate::client::common::{ClientAuthDetails, ClientHelloDetails};
use crate::client::common::{HandshakeDetails, ServerCertDetails};
use crate::client::hs;

use ring::constant_time;
use webpki;

// Extensions we expect in plaintext in the ServerHello.
static ALLOWED_PLAINTEXT_EXTS: &[ExtensionType] = &[
    ExtensionType::KeyShare,
    ExtensionType::PreSharedKey,
    ExtensionType::SupportedVersions,
];

// Only the intersection of things we offer, and those disallowed
// in TLS1.3
static DISALLOWED_TLS13_EXTS: &[ExtensionType] = &[
    ExtensionType::ECPointFormats,
    ExtensionType::SessionTicket,
    ExtensionType::RenegotiationInfo,
    ExtensionType::ExtendedMasterSecret,
];

pub fn validate_server_hello(
    sess: &mut ClientSessionImpl,
    server_hello: &ServerHelloPayload,
) -> Result<(), TLSError> {
    unimplemented!()
}

fn find_kx_hint(sess: &mut ClientSessionImpl, dns_name: webpki::DNSNameRef) -> Option<NamedGroup> {
    unimplemented!()
}

fn save_kx_hint(sess: &mut ClientSessionImpl, dns_name: webpki::DNSNameRef, group: NamedGroup) {
    unimplemented!()
}

pub fn choose_kx_groups(
    sess: &mut ClientSessionImpl,
    exts: &mut Vec<ClientExtension>,
    hello: &mut ClientHelloDetails,
    handshake: &mut HandshakeDetails,
    retryreq: Option<&HelloRetryRequest>,
) {
    unimplemented!()
}

/// This implements the horrifying TLS1.3 hack where PSK binders have a
/// data dependency on the message they are contained within.
pub fn fill_in_psk_binder(
    sess: &mut ClientSessionImpl,
    handshake: &mut HandshakeDetails,
    hmp: &mut HandshakeMessagePayload,
) -> KeyScheduleEarly {
    unimplemented!()
}

pub fn start_handshake_traffic(
    sess: &mut ClientSessionImpl,
    early_key_schedule: Option<KeyScheduleEarly>,
    server_hello: &ServerHelloPayload,
    handshake: &mut HandshakeDetails,
    hello: &mut ClientHelloDetails,
) -> Result<KeyScheduleHandshake, TLSError> {
    unimplemented!()
}

pub fn prepare_resumption(
    sess: &mut ClientSessionImpl,
    ticket: Vec<u8>,
    handshake: &HandshakeDetails,
    exts: &mut Vec<ClientExtension>,
    doing_retry: bool,
) -> bool {
    unimplemented!()
}

pub fn emit_fake_ccs(hs: &mut HandshakeDetails, sess: &mut ClientSessionImpl) {
    unimplemented!()
}

fn validate_encrypted_extensions(
    sess: &mut ClientSessionImpl,
    hello: &ClientHelloDetails,
    exts: &EncryptedExtensions,
) -> Result<(), TLSError> {
    unimplemented!()
}

pub struct ExpectEncryptedExtensions {
    pub handshake: HandshakeDetails,
    pub key_schedule: KeyScheduleHandshake,
    pub server_cert: ServerCertDetails,
    pub hello: ClientHelloDetails,
}

impl ExpectEncryptedExtensions {
    fn into_expect_finished_resume(
        self,
        certv: verify::ServerCertVerified,
        sigv: verify::HandshakeSignatureValid,
    ) -> hs::NextState {
        unimplemented!()
    }

    fn into_expect_certificate_or_certreq(self) -> hs::NextState {
        unimplemented!()
    }
}

impl hs::State for ExpectEncryptedExtensions {
    fn handle(
        mut self: Box<Self>,
        sess: &mut ClientSessionImpl,
        m: Message,
    ) -> hs::NextStateOrError {
        unimplemented!()
    }
}

struct ExpectCertificate {
    handshake: HandshakeDetails,
    key_schedule: KeyScheduleHandshake,
    server_cert: ServerCertDetails,
    client_auth: Option<ClientAuthDetails>,
}

impl ExpectCertificate {
    fn into_expect_certificate_verify(self) -> hs::NextState {
        unimplemented!()
    }
}

impl hs::State for ExpectCertificate {
    fn handle(
        mut self: Box<Self>,
        sess: &mut ClientSessionImpl,
        m: Message,
    ) -> hs::NextStateOrError {
        unimplemented!()
    }
}

struct ExpectCertificateOrCertReq {
    handshake: HandshakeDetails,
    key_schedule: KeyScheduleHandshake,
    server_cert: ServerCertDetails,
}

impl ExpectCertificateOrCertReq {
    fn into_expect_certificate(self) -> hs::NextState {
        unimplemented!()
    }

    fn into_expect_certificate_req(self) -> hs::NextState {
        unimplemented!()
    }
}

impl hs::State for ExpectCertificateOrCertReq {
    fn handle(self: Box<Self>, sess: &mut ClientSessionImpl, m: Message) -> hs::NextStateOrError {
        unimplemented!()
    }
}

// --- TLS1.3 CertificateVerify ---
struct ExpectCertificateVerify {
    handshake: HandshakeDetails,
    key_schedule: KeyScheduleHandshake,
    server_cert: ServerCertDetails,
    client_auth: Option<ClientAuthDetails>,
}

impl ExpectCertificateVerify {
    fn into_expect_finished(
        self,
        certv: verify::ServerCertVerified,
        sigv: verify::HandshakeSignatureValid,
    ) -> hs::NextState {
        unimplemented!()
    }
}

fn send_cert_error_alert(sess: &mut ClientSessionImpl, err: TLSError) -> TLSError {
    unimplemented!()
}

impl hs::State for ExpectCertificateVerify {
    fn handle(
        mut self: Box<Self>,
        sess: &mut ClientSessionImpl,
        m: Message,
    ) -> hs::NextStateOrError {
        unimplemented!()
    }
}

// TLS1.3 version of CertificateRequest handling.  We then move to expecting the server
// Certificate. Unfortunately the CertificateRequest type changed in an annoying way
// in TLS1.3.
struct ExpectCertificateRequest {
    handshake: HandshakeDetails,
    key_schedule: KeyScheduleHandshake,
    server_cert: ServerCertDetails,
}

impl ExpectCertificateRequest {
    fn into_expect_certificate(self, client_auth: ClientAuthDetails) -> hs::NextState {
        unimplemented!()
    }
}

impl hs::State for ExpectCertificateRequest {
    fn handle(
        mut self: Box<Self>,
        sess: &mut ClientSessionImpl,
        m: Message,
    ) -> hs::NextStateOrError {
        unimplemented!()
    }
}

fn emit_certificate_tls13(
    handshake: &mut HandshakeDetails,
    client_auth: &mut ClientAuthDetails,
    sess: &mut ClientSessionImpl,
) {
    unimplemented!()
}

fn emit_certverify_tls13(
    handshake: &mut HandshakeDetails,
    client_auth: &mut ClientAuthDetails,
    sess: &mut ClientSessionImpl,
) -> Result<(), TLSError> {
    unimplemented!()
}

fn emit_finished_tls13(
    handshake: &mut HandshakeDetails,
    key_schedule: &KeyScheduleTrafficWithClientFinishedPending,
    sess: &mut ClientSessionImpl,
) {
    unimplemented!()
}

fn emit_end_of_early_data_tls13(handshake: &mut HandshakeDetails, sess: &mut ClientSessionImpl) {
    unimplemented!()
}

struct ExpectFinished {
    handshake: HandshakeDetails,
    key_schedule: KeyScheduleHandshake,
    client_auth: Option<ClientAuthDetails>,
    cert_verified: verify::ServerCertVerified,
    sig_verified: verify::HandshakeSignatureValid,
}

impl ExpectFinished {
    fn into_expect_traffic(
        handshake: HandshakeDetails,
        key_schedule: KeyScheduleTraffic,
        cert_verified: verify::ServerCertVerified,
        sig_verified: verify::HandshakeSignatureValid,
        fin_verified: verify::FinishedMessageVerified,
    ) -> ExpectTraffic {
        unimplemented!()
    }
}

impl hs::State for ExpectFinished {
    fn handle(self: Box<Self>, sess: &mut ClientSessionImpl, m: Message) -> hs::NextStateOrError {
        unimplemented!()
    }
}

// -- Traffic transit state (TLS1.3) --
// In this state we can be sent tickets, keyupdates,
// and application data.
struct ExpectTraffic {
    handshake: HandshakeDetails,
    key_schedule: KeyScheduleTraffic,
    want_write_key_update: bool,
    _cert_verified: verify::ServerCertVerified,
    _sig_verified: verify::HandshakeSignatureValid,
    _fin_verified: verify::FinishedMessageVerified,
}

impl ExpectTraffic {
    fn handle_new_ticket_tls13(
        &mut self,
        sess: &mut ClientSessionImpl,
        nst: &NewSessionTicketPayloadTLS13,
    ) -> Result<(), TLSError> {
        unimplemented!()
    }

    fn handle_key_update(
        &mut self,
        sess: &mut ClientSessionImpl,
        kur: &KeyUpdateRequest,
    ) -> Result<(), TLSError> {
        unimplemented!()
    }
}

impl hs::State for ExpectTraffic {
    fn handle(
        mut self: Box<Self>,
        sess: &mut ClientSessionImpl,
        mut m: Message,
    ) -> hs::NextStateOrError {
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

    fn perhaps_write_key_update(&mut self, sess: &mut ClientSessionImpl) {
        unimplemented!()
    }
}

#[cfg(feature = "quic")]
pub struct ExpectQUICTraffic(ExpectTraffic);

#[cfg(feature = "quic")]
impl hs::State for ExpectQUICTraffic {
    fn handle(
        mut self: Box<Self>,
        sess: &mut ClientSessionImpl,
        m: Message,
    ) -> hs::NextStateOrError {
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
}
