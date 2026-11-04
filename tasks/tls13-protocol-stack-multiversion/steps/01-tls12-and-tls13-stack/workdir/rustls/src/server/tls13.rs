use crate::check::check_message;
use crate::cipher;
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
use crate::msgs::enums::{AlertDescription, NamedGroup, SignatureScheme};
use crate::msgs::enums::{Compression, PSKKeyExchangeMode};
use crate::msgs::enums::{ContentType, HandshakeType, ProtocolVersion};
use crate::msgs::handshake::CertReqExtension;
use crate::msgs::handshake::CertificateEntry;
use crate::msgs::handshake::CertificateExtension;
use crate::msgs::handshake::CertificatePayloadTLS13;
use crate::msgs::handshake::CertificateRequestPayloadTLS13;
use crate::msgs::handshake::CertificateStatus;
use crate::msgs::handshake::ClientHelloPayload;
use crate::msgs::handshake::DigitallySignedStruct;
use crate::msgs::handshake::HandshakeMessagePayload;
use crate::msgs::handshake::HandshakePayload;
use crate::msgs::handshake::HelloRetryExtension;
use crate::msgs::handshake::HelloRetryRequest;
use crate::msgs::handshake::KeyShareEntry;
use crate::msgs::handshake::NewSessionTicketPayloadTLS13;
use crate::msgs::handshake::Random;
use crate::msgs::handshake::ServerExtension;
use crate::msgs::handshake::ServerHelloPayload;
use crate::msgs::handshake::SessionID;
use crate::msgs::message::{Message, MessagePayload};
use crate::msgs::persist;
use crate::rand;
use crate::server::ServerSessionImpl;
use crate::sign;
use crate::suites;
use crate::verify;
#[cfg(feature = "quic")]
use crate::{msgs::handshake::NewSessionTicketExtension, quic, session::Protocol};

use crate::server::common::{ClientCertDetails, HandshakeDetails};
use crate::server::hs;

use ring::constant_time;

pub struct CompleteClientHelloHandling {
    pub handshake: HandshakeDetails,
    pub done_retry: bool,
    pub send_cert_status: bool,
    pub send_sct: bool,
    pub send_ticket: bool,
}

impl CompleteClientHelloHandling {
    fn check_binder(
        &self,
        sess: &mut ServerSessionImpl,
        client_hello: &Message,
        psk: &[u8],
        binder: &[u8],
    ) -> bool {
        unimplemented!()
    }

    fn into_expect_retried_client_hello(self) -> hs::NextState {
        unimplemented!()
    }

    fn into_expect_certificate(
        self,
        key_schedule: KeyScheduleTrafficWithClientFinishedPending,
    ) -> hs::NextState {
        unimplemented!()
    }

    fn into_expect_finished(
        self,
        key_schedule: KeyScheduleTrafficWithClientFinishedPending,
    ) -> hs::NextState {
        unimplemented!()
    }

    fn emit_server_hello(
        &mut self,
        sess: &mut ServerSessionImpl,
        session_id: &SessionID,
        share: &KeyShareEntry,
        chosen_psk_idx: Option<usize>,
        resuming_psk: Option<&[u8]>,
    ) -> Result<KeyScheduleHandshake, TLSError> {
        unimplemented!()
    }

    fn emit_fake_ccs(&mut self, sess: &mut ServerSessionImpl) {
        unimplemented!()
    }

    fn emit_hello_retry_request(&mut self, sess: &mut ServerSessionImpl, group: NamedGroup) {
        unimplemented!()
    }

    fn emit_encrypted_extensions(
        &mut self,
        sess: &mut ServerSessionImpl,
        server_key: &mut sign::CertifiedKey,
        hello: &ClientHelloPayload,
        resumedata: Option<&persist::ServerSessionValue>,
    ) -> Result<(), TLSError> {
        unimplemented!()
    }

    fn emit_certificate_req_tls13(
        &mut self,
        sess: &mut ServerSessionImpl,
    ) -> Result<bool, TLSError> {
        unimplemented!()
    }

    fn emit_certificate_tls13(
        &mut self,
        sess: &mut ServerSessionImpl,
        server_key: &mut sign::CertifiedKey,
    ) {
        unimplemented!()
    }

    fn emit_certificate_verify_tls13(
        &mut self,
        sess: &mut ServerSessionImpl,
        server_key: &mut sign::CertifiedKey,
        schemes: &[SignatureScheme],
    ) -> Result<(), TLSError> {
        unimplemented!()
    }

    fn emit_finished_tls13(
        &mut self,
        sess: &mut ServerSessionImpl,
        key_schedule: KeyScheduleHandshake,
    ) -> KeyScheduleTrafficWithClientFinishedPending {
        unimplemented!()
    }

    fn attempt_tls13_ticket_decryption(
        &mut self,
        sess: &mut ServerSessionImpl,
        ticket: &[u8],
    ) -> Option<persist::ServerSessionValue> {
        unimplemented!()
    }

    pub fn handle_client_hello(
        mut self,
        sess: &mut ServerSessionImpl,
        mut server_key: sign::CertifiedKey,
        chm: &Message,
    ) -> hs::NextStateOrError {
        unimplemented!()
    }
}

pub struct ExpectCertificate {
    pub handshake: HandshakeDetails,
    pub key_schedule: KeyScheduleTrafficWithClientFinishedPending,
    pub send_ticket: bool,
}

impl ExpectCertificate {
    fn into_expect_finished(self) -> hs::NextState {
        unimplemented!()
    }

    fn into_expect_certificate_verify(self, cert: ClientCertDetails) -> hs::NextState {
        unimplemented!()
    }
}

impl hs::State for ExpectCertificate {
    fn handle(
        mut self: Box<Self>,
        sess: &mut ServerSessionImpl,
        m: Message,
    ) -> hs::NextStateOrError {
        unimplemented!()
    }
}

pub struct ExpectCertificateVerify {
    handshake: HandshakeDetails,
    key_schedule: KeyScheduleTrafficWithClientFinishedPending,
    client_cert: ClientCertDetails,
    send_ticket: bool,
}

impl ExpectCertificateVerify {
    fn into_expect_finished(self) -> hs::NextState {
        unimplemented!()
    }
}

impl hs::State for ExpectCertificateVerify {
    fn handle(
        mut self: Box<Self>,
        sess: &mut ServerSessionImpl,
        m: Message,
    ) -> hs::NextStateOrError {
        unimplemented!()
    }
}

// --- Process client's Finished ---
fn get_server_session_value(
    handshake: &mut HandshakeDetails,
    key_schedule: &KeyScheduleTraffic,
    sess: &ServerSessionImpl,
    nonce: &[u8],
) -> persist::ServerSessionValue {
    unimplemented!()
}

pub struct ExpectFinished {
    pub handshake: HandshakeDetails,
    pub key_schedule: KeyScheduleTrafficWithClientFinishedPending,
    pub send_ticket: bool,
}

impl ExpectFinished {
    fn into_expect_traffic(
        fin: verify::FinishedMessageVerified,
        ks: KeyScheduleTraffic,
    ) -> hs::NextState {
        unimplemented!()
    }

    fn emit_stateless_ticket(
        handshake: &mut HandshakeDetails,
        sess: &mut ServerSessionImpl,
        key_schedule: &KeyScheduleTraffic,
    ) {
        unimplemented!()
    }

    fn emit_stateful_ticket(
        handshake: &mut HandshakeDetails,
        sess: &mut ServerSessionImpl,
        key_schedule: &KeyScheduleTraffic,
    ) {
        unimplemented!()
    }
}

impl hs::State for ExpectFinished {
    fn handle(
        mut self: Box<Self>,
        sess: &mut ServerSessionImpl,
        m: Message,
    ) -> hs::NextStateOrError {
        unimplemented!()
    }
}

// --- Process traffic ---
pub struct ExpectTraffic {
    key_schedule: KeyScheduleTraffic,
    want_write_key_update: bool,
    _fin_verified: verify::FinishedMessageVerified,
}

impl ExpectTraffic {
    fn handle_traffic(&self, sess: &mut ServerSessionImpl, mut m: Message) -> Result<(), TLSError> {
        unimplemented!()
    }

    fn handle_key_update(
        &mut self,
        sess: &mut ServerSessionImpl,
        kur: &KeyUpdateRequest,
    ) -> Result<(), TLSError> {
        unimplemented!()
    }
}

impl hs::State for ExpectTraffic {
    fn handle(
        mut self: Box<Self>,
        sess: &mut ServerSessionImpl,
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

    fn perhaps_write_key_update(&mut self, sess: &mut ServerSessionImpl) {
        unimplemented!()
    }
}

#[cfg(feature = "quic")]
pub struct ExpectQUICTraffic {
    key_schedule: KeyScheduleTraffic,
    _fin_verified: verify::FinishedMessageVerified,
}

#[cfg(feature = "quic")]
impl hs::State for ExpectQUICTraffic {
    fn handle(self: Box<Self>, _: &mut ServerSessionImpl, m: Message) -> hs::NextStateOrError {
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
