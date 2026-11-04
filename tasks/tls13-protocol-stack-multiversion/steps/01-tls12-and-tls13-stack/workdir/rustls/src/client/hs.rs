#[cfg(feature = "logging")]
use crate::bs_debug;
use crate::check::check_message;
use crate::cipher;
use crate::client::ClientSessionImpl;
use crate::error::TLSError;
use crate::key_schedule::{KeyScheduleEarly, KeyScheduleHandshake};
#[cfg(feature = "logging")]
use crate::log::{debug, trace};
use crate::msgs::base::Payload;
#[cfg(feature = "quic")]
use crate::msgs::base::PayloadU16;
use crate::msgs::codec::{Codec, Reader};
use crate::msgs::enums::{AlertDescription, Compression, ProtocolVersion};
use crate::msgs::enums::{ContentType, ExtensionType, HandshakeType};
use crate::msgs::enums::{ECPointFormat, PSKKeyExchangeMode};
use crate::msgs::handshake::HelloRetryRequest;
use crate::msgs::handshake::{CertificateStatusRequest, SCTList};
use crate::msgs::handshake::{ClientExtension, HasServerExtensions};
use crate::msgs::handshake::{ClientHelloPayload, HandshakeMessagePayload, HandshakePayload};
use crate::msgs::handshake::{ConvertProtocolNameList, ProtocolNameList};
use crate::msgs::handshake::{ECPointFormatList, SupportedPointFormats};
use crate::msgs::handshake::{Random, SessionID};
use crate::msgs::message::{Message, MessagePayload};
use crate::msgs::persist;
use crate::rand;
use crate::session::SessionSecrets;
use crate::suites;
use crate::ticketer;
use crate::verify;

use crate::client::common::{ClientHelloDetails, ReceivedTicketDetails};
use crate::client::common::{HandshakeDetails, ServerCertDetails};
use crate::client::{tls12, tls13};

use webpki;

pub type NextState = Box<dyn State + Send + Sync>;
pub type NextStateOrError = Result<NextState, TLSError>;

pub trait State {
    /// Each handle() implementation consumes a whole TLS message, and returns
    /// either an error or the next state.
    fn handle(self: Box<Self>, sess: &mut ClientSessionImpl, m: Message) -> NextStateOrError;

    fn export_keying_material(
        &self,
        _output: &mut [u8],
        _label: &[u8],
        _context: Option<&[u8]>,
    ) -> Result<(), TLSError> {
        unimplemented!()
    }

    fn perhaps_write_key_update(&mut self, _sess: &mut ClientSessionImpl) {
        unimplemented!()
    }
}

pub fn illegal_param(sess: &mut ClientSessionImpl, why: &str) -> TLSError {
    unimplemented!()
}

pub fn check_aligned_handshake(sess: &mut ClientSessionImpl) -> Result<(), TLSError> {
    unimplemented!()
}

fn find_session(
    sess: &mut ClientSessionImpl,
    dns_name: webpki::DNSNameRef,
) -> Option<persist::ClientSessionValue> {
    unimplemented!()
}

fn random_sessionid() -> SessionID {
    unimplemented!()
}

/// If we have a ticket, we use the sessionid as a signal that we're
/// doing an abbreviated handshake.  See section 3.4 in RFC5077.
fn random_sessionid_for_ticket(csv: &mut persist::ClientSessionValue) {
    unimplemented!()
}

struct InitialState {
    handshake: HandshakeDetails,
}

impl InitialState {
    fn new(host_name: webpki::DNSName, extra_exts: Vec<ClientExtension>) -> InitialState {
        unimplemented!()
    }

    fn emit_initial_client_hello(mut self, sess: &mut ClientSessionImpl) -> NextState {
        unimplemented!()
    }
}

pub fn start_handshake(
    sess: &mut ClientSessionImpl,
    host_name: webpki::DNSName,
    extra_exts: Vec<ClientExtension>,
) -> NextState {
    unimplemented!()
}

struct ExpectServerHello {
    handshake: HandshakeDetails,
    early_key_schedule: Option<KeyScheduleEarly>,
    hello: ClientHelloDetails,
    server_cert: ServerCertDetails,
    may_send_cert_status: bool,
    must_issue_new_ticket: bool,
}

struct ExpectServerHelloOrHelloRetryRequest(ExpectServerHello);

pub fn compatible_suite(
    sess: &ClientSessionImpl,
    resuming_suite: Option<&suites::SupportedCipherSuite>,
) -> bool {
    unimplemented!()
}

fn emit_client_hello_for_retry(
    sess: &mut ClientSessionImpl,
    mut handshake: HandshakeDetails,
    mut hello: ClientHelloDetails,
    retryreq: Option<&HelloRetryRequest>,
) -> NextState {
    unimplemented!()
}

pub fn process_alpn_protocol(
    sess: &mut ClientSessionImpl,
    proto: Option<&[u8]>,
) -> Result<(), TLSError> {
    unimplemented!()
}

pub fn sct_list_is_invalid(scts: &SCTList) -> bool {
    unimplemented!()
}

impl ExpectServerHello {
    fn into_expect_tls13_encrypted_extensions(
        self,
        key_schedule: KeyScheduleHandshake,
    ) -> NextState {
        unimplemented!()
    }

    fn into_expect_tls12_new_ticket_resume(
        self,
        secrets: SessionSecrets,
        certv: verify::ServerCertVerified,
        sigv: verify::HandshakeSignatureValid,
    ) -> NextState {
        unimplemented!()
    }

    fn into_expect_tls12_ccs_resume(
        self,
        secrets: SessionSecrets,
        certv: verify::ServerCertVerified,
        sigv: verify::HandshakeSignatureValid,
    ) -> NextState {
        unimplemented!()
    }

    fn into_expect_tls12_certificate(self) -> NextState {
        unimplemented!()
    }
}

impl State for ExpectServerHello {
    fn handle(mut self: Box<Self>, sess: &mut ClientSessionImpl, m: Message) -> NextStateOrError {
        unimplemented!()
    }
}

impl ExpectServerHelloOrHelloRetryRequest {
    fn into_expect_server_hello(self) -> NextState {
        unimplemented!()
    }

    fn handle_hello_retry_request(
        mut self,
        sess: &mut ClientSessionImpl,
        m: Message,
    ) -> NextStateOrError {
        unimplemented!()
    }
}

impl State for ExpectServerHelloOrHelloRetryRequest {
    fn handle(self: Box<Self>, sess: &mut ClientSessionImpl, m: Message) -> NextStateOrError {
        unimplemented!()
    }
}

pub fn send_cert_error_alert(sess: &mut ClientSessionImpl, err: TLSError) -> TLSError {
    unimplemented!()
}
