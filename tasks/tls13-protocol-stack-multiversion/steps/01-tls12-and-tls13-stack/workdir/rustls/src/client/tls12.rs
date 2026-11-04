use crate::check::check_message;
use crate::client::ClientSessionImpl;
use crate::error::TLSError;
#[cfg(feature = "logging")]
use crate::log::{debug, trace};
use crate::msgs::base::{Payload, PayloadU8};
use crate::msgs::ccs::ChangeCipherSpecPayload;
use crate::msgs::codec::Codec;
use crate::msgs::enums::{AlertDescription, ProtocolVersion};
use crate::msgs::enums::{ContentType, HandshakeType};
use crate::msgs::handshake::DecomposedSignatureScheme;
use crate::msgs::handshake::DigitallySignedStruct;
use crate::msgs::handshake::ServerKeyExchangePayload;
use crate::msgs::handshake::{HandshakeMessagePayload, HandshakePayload};
use crate::msgs::message::{Message, MessagePayload};
use crate::msgs::persist;
use crate::session::SessionSecrets;
use crate::suites;
use crate::ticketer;
use crate::verify;

use crate::client::common::{ClientAuthDetails, ReceivedTicketDetails};
use crate::client::common::{HandshakeDetails, ServerCertDetails, ServerKXDetails};
use crate::client::hs;

use ring::constant_time;
use std::mem;

pub struct ExpectCertificate {
    pub handshake: HandshakeDetails,
    pub server_cert: ServerCertDetails,
    pub may_send_cert_status: bool,
    pub must_issue_new_ticket: bool,
}

impl ExpectCertificate {
    fn into_expect_certificate_status_or_server_kx(self) -> hs::NextState {
        unimplemented!()
    }

    fn into_expect_server_kx(self) -> hs::NextState {
        unimplemented!()
    }
}

impl hs::State for ExpectCertificate {
    fn handle(
        mut self: Box<Self>,
        _sess: &mut ClientSessionImpl,
        m: Message,
    ) -> hs::NextStateOrError {
        unimplemented!()
    }
}

struct ExpectCertificateStatus {
    handshake: HandshakeDetails,
    server_cert: ServerCertDetails,
    must_issue_new_ticket: bool,
}

impl ExpectCertificateStatus {
    fn into_expect_server_kx(self) -> hs::NextState {
        unimplemented!()
    }
}

impl hs::State for ExpectCertificateStatus {
    fn handle(
        mut self: Box<Self>,
        _sess: &mut ClientSessionImpl,
        m: Message,
    ) -> hs::NextStateOrError {
        unimplemented!()
    }
}

struct ExpectCertificateStatusOrServerKX {
    handshake: HandshakeDetails,
    server_cert: ServerCertDetails,
    must_issue_new_ticket: bool,
}

impl ExpectCertificateStatusOrServerKX {
    fn into_expect_server_kx(self) -> hs::NextState {
        unimplemented!()
    }

    fn into_expect_certificate_status(self) -> hs::NextState {
        unimplemented!()
    }
}

impl hs::State for ExpectCertificateStatusOrServerKX {
    fn handle(self: Box<Self>, sess: &mut ClientSessionImpl, m: Message) -> hs::NextStateOrError {
        unimplemented!()
    }
}

struct ExpectServerKX {
    handshake: HandshakeDetails,
    server_cert: ServerCertDetails,
    must_issue_new_ticket: bool,
}

impl ExpectServerKX {
    fn into_expect_server_done_or_certreq(self, skx: ServerKXDetails) -> hs::NextState {
        unimplemented!()
    }
}

impl hs::State for ExpectServerKX {
    fn handle(
        mut self: Box<Self>,
        sess: &mut ClientSessionImpl,
        m: Message,
    ) -> hs::NextStateOrError {
        unimplemented!()
    }
}

fn emit_certificate(
    handshake: &mut HandshakeDetails,
    client_auth: &mut ClientAuthDetails,
    sess: &mut ClientSessionImpl,
) {
    unimplemented!()
}

fn emit_clientkx(
    handshake: &mut HandshakeDetails,
    sess: &mut ClientSessionImpl,
    kxd: &suites::KeyExchangeResult,
) {
    unimplemented!()
}

fn emit_certverify(
    handshake: &mut HandshakeDetails,
    client_auth: &mut ClientAuthDetails,
    sess: &mut ClientSessionImpl,
) -> Result<(), TLSError> {
    unimplemented!()
}

fn emit_ccs(sess: &mut ClientSessionImpl) {
    unimplemented!()
}

fn emit_finished(
    secrets: &SessionSecrets,
    handshake: &mut HandshakeDetails,
    sess: &mut ClientSessionImpl,
) {
    unimplemented!()
}

// --- Either a CertificateRequest, or a ServerHelloDone. ---
// Existence of the CertificateRequest tells us the server is asking for
// client auth.  Otherwise we go straight to ServerHelloDone.
struct ExpectCertificateRequest {
    handshake: HandshakeDetails,
    server_cert: ServerCertDetails,
    server_kx: ServerKXDetails,
    must_issue_new_ticket: bool,
}

impl ExpectCertificateRequest {
    fn into_expect_server_done(self, client_auth: ClientAuthDetails) -> hs::NextState {
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

struct ExpectServerDoneOrCertReq {
    handshake: HandshakeDetails,
    server_cert: ServerCertDetails,
    server_kx: ServerKXDetails,
    must_issue_new_ticket: bool,
}

impl ExpectServerDoneOrCertReq {
    fn into_expect_certificate_req(self) -> hs::NextState {
        unimplemented!()
    }

    fn into_expect_server_done(self) -> hs::NextState {
        unimplemented!()
    }
}

impl hs::State for ExpectServerDoneOrCertReq {
    fn handle(
        mut self: Box<Self>,
        sess: &mut ClientSessionImpl,
        m: Message,
    ) -> hs::NextStateOrError {
        unimplemented!()
    }
}

struct ExpectServerDone {
    handshake: HandshakeDetails,
    server_cert: ServerCertDetails,
    server_kx: ServerKXDetails,
    client_auth: Option<ClientAuthDetails>,
    must_issue_new_ticket: bool,
}

impl ExpectServerDone {
    fn into_expect_new_ticket(
        self,
        secrets: SessionSecrets,
        certv: verify::ServerCertVerified,
        sigv: verify::HandshakeSignatureValid,
    ) -> hs::NextState {
        unimplemented!()
    }

    fn into_expect_ccs(
        self,
        secrets: SessionSecrets,
        certv: verify::ServerCertVerified,
        sigv: verify::HandshakeSignatureValid,
    ) -> hs::NextState {
        unimplemented!()
    }
}

impl hs::State for ExpectServerDone {
    fn handle(self: Box<Self>, sess: &mut ClientSessionImpl, m: Message) -> hs::NextStateOrError {
        unimplemented!()
    }
}

// -- Waiting for their CCS --
pub struct ExpectCCS {
    pub secrets: SessionSecrets,
    pub handshake: HandshakeDetails,
    pub ticket: ReceivedTicketDetails,
    pub resuming: bool,
    pub cert_verified: verify::ServerCertVerified,
    pub sig_verified: verify::HandshakeSignatureValid,
}

impl ExpectCCS {
    fn into_expect_finished(self) -> hs::NextState {
        unimplemented!()
    }
}

impl hs::State for ExpectCCS {
    fn handle(self: Box<Self>, sess: &mut ClientSessionImpl, m: Message) -> hs::NextStateOrError {
        unimplemented!()
    }
}

pub struct ExpectNewTicket {
    pub secrets: SessionSecrets,
    pub handshake: HandshakeDetails,
    pub resuming: bool,
    pub cert_verified: verify::ServerCertVerified,
    pub sig_verified: verify::HandshakeSignatureValid,
}

impl ExpectNewTicket {
    fn into_expect_ccs(self, ticket: ReceivedTicketDetails) -> hs::NextState {
        unimplemented!()
    }
}

impl hs::State for ExpectNewTicket {
    fn handle(
        mut self: Box<Self>,
        _sess: &mut ClientSessionImpl,
        m: Message,
    ) -> hs::NextStateOrError {
        unimplemented!()
    }
}

// -- Waiting for their finished --
fn save_session(
    secrets: &SessionSecrets,
    handshake: &mut HandshakeDetails,
    recvd_ticket: &mut ReceivedTicketDetails,
    sess: &mut ClientSessionImpl,
) {
    unimplemented!()
}

struct ExpectFinished {
    handshake: HandshakeDetails,
    ticket: ReceivedTicketDetails,
    secrets: SessionSecrets,
    resuming: bool,
    cert_verified: verify::ServerCertVerified,
    sig_verified: verify::HandshakeSignatureValid,
}

impl ExpectFinished {
    fn into_expect_traffic(self, fin: verify::FinishedMessageVerified) -> hs::NextState {
        unimplemented!()
    }
}

impl hs::State for ExpectFinished {
    fn handle(self: Box<Self>, sess: &mut ClientSessionImpl, m: Message) -> hs::NextStateOrError {
        unimplemented!()
    }
}

// -- Traffic transit state --
struct ExpectTraffic {
    secrets: SessionSecrets,
    _cert_verified: verify::ServerCertVerified,
    _sig_verified: verify::HandshakeSignatureValid,
    _fin_verified: verify::FinishedMessageVerified,
}

impl hs::State for ExpectTraffic {
    fn handle(
        self: Box<Self>,
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
}
