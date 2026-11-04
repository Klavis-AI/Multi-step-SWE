use crate::error::TLSError;
#[cfg(feature = "logging")]
use crate::log::{debug, trace};
use crate::msgs::codec::Codec;
use crate::msgs::enums::{AlertDescription, ExtensionType};
use crate::msgs::enums::{CipherSuite, Compression, ECPointFormat, NamedGroup};
use crate::msgs::enums::{ClientCertificateType, SignatureScheme};
use crate::msgs::enums::{ContentType, HandshakeType, ProtocolVersion};
use crate::msgs::handshake::CertificateRequestPayload;
use crate::msgs::handshake::CertificateStatus;
use crate::msgs::handshake::ClientExtension;
use crate::msgs::handshake::{ClientHelloPayload, ServerExtension, SessionID};
use crate::msgs::handshake::{ConvertProtocolNameList, ConvertServerNameList};
use crate::msgs::handshake::{DigitallySignedStruct, ServerECDHParams};
use crate::msgs::handshake::{ECDHEServerKeyExchange, ServerKeyExchangePayload};
use crate::msgs::handshake::{ECPointFormatList, SupportedPointFormats};
use crate::msgs::handshake::{HandshakeMessagePayload, Random, ServerHelloPayload};
use crate::msgs::handshake::{HandshakePayload, SupportedSignatureSchemes};
use crate::msgs::message::{Message, MessagePayload};
use crate::msgs::persist;
use crate::rand;
use crate::server::{ClientHello, ServerConfig, ServerSessionImpl};
#[cfg(feature = "quic")]
use crate::session::Protocol;
use crate::session::SessionSecrets;
use crate::sign;
use crate::suites;
use webpki;

use crate::server::common::{HandshakeDetails, ServerKXDetails};
use crate::server::{tls12, tls13};

pub type NextState = Box<dyn State + Send + Sync>;
pub type NextStateOrError = Result<NextState, TLSError>;

pub trait State {
    fn handle(self: Box<Self>, sess: &mut ServerSessionImpl, m: Message) -> NextStateOrError;

    fn export_keying_material(
        &self,
        _output: &mut [u8],
        _label: &[u8],
        _context: Option<&[u8]>,
    ) -> Result<(), TLSError> {
        unimplemented!()
    }

    fn perhaps_write_key_update(&mut self, _sess: &mut ServerSessionImpl) {
        unimplemented!()
    }
}

pub fn incompatible(sess: &mut ServerSessionImpl, why: &str) -> TLSError {
    unimplemented!()
}

fn bad_version(sess: &mut ServerSessionImpl, why: &str) -> TLSError {
    unimplemented!()
}

pub fn illegal_param(sess: &mut ServerSessionImpl, why: &str) -> TLSError {
    unimplemented!()
}

pub fn decode_error(sess: &mut ServerSessionImpl, why: &str) -> TLSError {
    unimplemented!()
}

pub fn can_resume(
    sess: &ServerSessionImpl,
    handshake: &HandshakeDetails,
    resumedata: &Option<persist::ServerSessionValue>,
) -> bool {
    unimplemented!()
}

// Require an exact match for the purpose of comparing SNI DNS Names from two
// client hellos, even though a case-insensitive comparison might also be OK.
fn same_dns_name_or_both_none(a: Option<&webpki::DNSName>, b: Option<&webpki::DNSName>) -> bool {
    unimplemented!()
}

// Changing the keys must not span any fragmented handshake
// messages.  Otherwise the defragmented messages will have
// been protected with two different record layer protections,
// which is illegal.  Not mentioned in RFC.
pub fn check_aligned_handshake(sess: &mut ServerSessionImpl) -> Result<(), TLSError> {
    unimplemented!()
}

pub fn save_sni(sess: &mut ServerSessionImpl, sni: Option<webpki::DNSName>) {
    unimplemented!()
}

#[derive(Default)]
pub struct ExtensionProcessing {
    // extensions to reply with
    pub exts: Vec<ServerExtension>,

    // effects on later handshake steps
    pub send_cert_status: bool,
    pub send_sct: bool,
    pub send_ticket: bool,
}

impl ExtensionProcessing {
    pub fn new() -> Self {
        unimplemented!()
    }

    pub fn process_common(
        &mut self,
        sess: &mut ServerSessionImpl,
        server_key: Option<&mut sign::CertifiedKey>,
        hello: &ClientHelloPayload,
        resumedata: Option<&persist::ServerSessionValue>,
        handshake: &HandshakeDetails,
    ) -> Result<(), TLSError> {
        unimplemented!()
    }

    fn process_tls12(
        &mut self,
        sess: &ServerSessionImpl,
        hello: &ClientHelloPayload,
        handshake: &HandshakeDetails,
    ) {
        unimplemented!()
    }
}

pub struct ExpectClientHello {
    pub handshake: HandshakeDetails,
    pub done_retry: bool,
    pub send_cert_status: bool,
    pub send_sct: bool,
    pub send_ticket: bool,
}

impl ExpectClientHello {
    pub fn new(
        server_config: &ServerConfig,
        extra_exts: Vec<ServerExtension>,
    ) -> ExpectClientHello {
        unimplemented!()
    }

    fn into_expect_tls12_ccs(self, secrets: SessionSecrets) -> NextState {
        unimplemented!()
    }

    fn into_complete_tls13_client_hello_handling(self) -> tls13::CompleteClientHelloHandling {
        unimplemented!()
    }

    fn into_expect_tls12_certificate(self, kx: suites::KeyExchange) -> NextState {
        unimplemented!()
    }

    fn into_expect_tls12_client_kx(self, kx: suites::KeyExchange) -> NextState {
        unimplemented!()
    }

    fn emit_server_hello(
        &mut self,
        sess: &mut ServerSessionImpl,
        server_key: Option<&mut sign::CertifiedKey>,
        hello: &ClientHelloPayload,
        resumedata: Option<&persist::ServerSessionValue>,
    ) -> Result<(), TLSError> {
        unimplemented!()
    }

    fn emit_certificate(
        &mut self,
        sess: &mut ServerSessionImpl,
        server_certkey: &mut sign::CertifiedKey,
    ) {
        unimplemented!()
    }

    fn emit_cert_status(
        &mut self,
        sess: &mut ServerSessionImpl,
        server_certkey: &mut sign::CertifiedKey,
    ) {
        unimplemented!()
    }

    fn emit_server_kx(
        &mut self,
        sess: &mut ServerSessionImpl,
        sigschemes: Vec<SignatureScheme>,
        group: NamedGroup,
        server_certkey: &mut sign::CertifiedKey,
    ) -> Result<suites::KeyExchange, TLSError> {
        unimplemented!()
    }

    fn emit_certificate_req(&mut self, sess: &mut ServerSessionImpl) -> Result<bool, TLSError> {
        unimplemented!()
    }

    fn emit_server_hello_done(&mut self, sess: &mut ServerSessionImpl) {
        unimplemented!()
    }

    fn start_resumption(
        mut self,
        sess: &mut ServerSessionImpl,
        client_hello: &ClientHelloPayload,
        sni: Option<&webpki::DNSName>,
        id: &SessionID,
        resumedata: persist::ServerSessionValue,
    ) -> NextStateOrError {
        unimplemented!()
    }
}

impl State for ExpectClientHello {
    fn handle(mut self: Box<Self>, sess: &mut ServerSessionImpl, m: Message) -> NextStateOrError {
        unimplemented!()
    }
}
