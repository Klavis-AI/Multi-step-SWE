use crate::check::check_message;
use crate::error::TLSError;
#[cfg(feature = "logging")]
use crate::log::{debug, trace};
use crate::msgs::base::Payload;
use crate::msgs::ccs::ChangeCipherSpecPayload;
use crate::msgs::codec::Codec;
use crate::msgs::enums::AlertDescription;
use crate::msgs::enums::{ContentType, HandshakeType, ProtocolVersion};
use crate::msgs::handshake::HandshakeMessagePayload;
use crate::msgs::handshake::HandshakePayload;
use crate::msgs::handshake::NewSessionTicketPayload;
use crate::msgs::message::{Message, MessagePayload};
use crate::msgs::persist;
use crate::server::ServerSessionImpl;
use crate::session::SessionSecrets;
use crate::verify;

use crate::server::common::{ClientCertDetails, HandshakeDetails, ServerKXDetails};
use crate::server::hs;

use ring::constant_time;

// --- Process client's Certificate for client auth ---
pub struct ExpectCertificate {
    pub handshake: HandshakeDetails,
    pub server_kx: ServerKXDetails,
    pub send_ticket: bool,
}

impl ExpectCertificate {
    fn into_expect_tls12_client_kx(self, cert: Option<ClientCertDetails>) -> hs::NextState {
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

// --- Process client's KeyExchange ---
pub struct ExpectClientKX {
    pub handshake: HandshakeDetails,
    pub server_kx: ServerKXDetails,
    pub client_cert: Option<ClientCertDetails>,
    pub send_ticket: bool,
}

impl ExpectClientKX {
    fn into_expect_tls12_certificate_verify(self, secrets: SessionSecrets) -> hs::NextState {
        unimplemented!()
    }

    fn into_expect_tls12_ccs(self, secrets: SessionSecrets) -> hs::NextState {
        unimplemented!()
    }
}

impl hs::State for ExpectClientKX {
    fn handle(
        mut self: Box<Self>,
        sess: &mut ServerSessionImpl,
        m: Message,
    ) -> hs::NextStateOrError {
        unimplemented!()
    }
}

// --- Process client's certificate proof ---
pub struct ExpectCertificateVerify {
    secrets: SessionSecrets,
    handshake: HandshakeDetails,
    client_cert: ClientCertDetails,
    send_ticket: bool,
}

impl ExpectCertificateVerify {
    fn into_expect_tls12_ccs(self) -> hs::NextState {
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

// --- Process client's ChangeCipherSpec ---
pub struct ExpectCCS {
    pub secrets: SessionSecrets,
    pub handshake: HandshakeDetails,
    pub resuming: bool,
    pub send_ticket: bool,
}

impl ExpectCCS {
    fn into_expect_tls12_finished(self) -> hs::NextState {
        unimplemented!()
    }
}

impl hs::State for ExpectCCS {
    fn handle(self: Box<Self>, sess: &mut ServerSessionImpl, m: Message) -> hs::NextStateOrError {
        unimplemented!()
    }
}

// --- Process client's Finished ---
fn get_server_session_value_tls12(
    secrets: &SessionSecrets,
    handshake: &HandshakeDetails,
    sess: &ServerSessionImpl,
) -> persist::ServerSessionValue {
    unimplemented!()
}

pub fn emit_ticket(
    secrets: &SessionSecrets,
    handshake: &mut HandshakeDetails,
    sess: &mut ServerSessionImpl,
) {
    unimplemented!()
}

pub fn emit_ccs(sess: &mut ServerSessionImpl) {
    unimplemented!()
}

pub fn emit_finished(
    secrets: &SessionSecrets,
    handshake: &mut HandshakeDetails,
    sess: &mut ServerSessionImpl,
) {
    unimplemented!()
}

pub struct ExpectFinished {
    secrets: SessionSecrets,
    handshake: HandshakeDetails,
    resuming: bool,
    send_ticket: bool,
}

impl ExpectFinished {
    fn into_expect_tls12_traffic(self, fin: verify::FinishedMessageVerified) -> hs::NextState {
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
    secrets: SessionSecrets,
    _fin_verified: verify::FinishedMessageVerified,
}

impl ExpectTraffic {}

impl hs::State for ExpectTraffic {
    fn handle(
        self: Box<Self>,
        sess: &mut ServerSessionImpl,
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
