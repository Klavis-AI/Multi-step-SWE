use crate::msgs::base::{PayloadU8, PayloadU16};
use crate::msgs::codec::{Codec, Reader};
use crate::msgs::enums::{CipherSuite, ProtocolVersion};
use crate::msgs::handshake::CertificatePayload;
use crate::msgs::handshake::SessionID;

use webpki;

use std::cmp;
use std::mem;

// These are the keys and values we store in session storage.

// --- Client types ---
/// Keys for session resumption and tickets.
/// Matching value is a `ClientSessionValue`.
#[derive(Debug)]
pub struct ClientSessionKey {
    kind: &'static [u8],
    dns_name: PayloadU8,
}

impl Codec for ClientSessionKey {
    fn encode(&self, bytes: &mut Vec<u8>) {
        unimplemented!()
    }

    // Don't need to read these.
    fn read(_r: &mut Reader) -> Option<ClientSessionKey> {
        unimplemented!()
    }
}

impl ClientSessionKey {
    pub fn session_for_dns_name(dns_name: webpki::DNSNameRef) -> ClientSessionKey {
        unimplemented!()
    }

    pub fn hint_for_dns_name(dns_name: webpki::DNSNameRef) -> ClientSessionKey {
        unimplemented!()
    }
}

#[derive(Debug)]
pub struct ClientSessionValue {
    pub version: ProtocolVersion,
    pub cipher_suite: CipherSuite,
    pub session_id: SessionID,
    pub ticket: PayloadU16,
    pub master_secret: PayloadU8,
    pub epoch: u64,
    pub lifetime: u32,
    pub age_add: u32,
    pub extended_ms: bool,
    pub max_early_data_size: u32,
    pub server_cert_chain: CertificatePayload,
}

impl Codec for ClientSessionValue {
    fn encode(&self, bytes: &mut Vec<u8>) {
        unimplemented!()
    }

    fn read(r: &mut Reader) -> Option<ClientSessionValue> {
        unimplemented!()
    }
}

static MAX_TICKET_LIFETIME: u32 = 7 * 24 * 60 * 60;

impl ClientSessionValue {
    pub fn new(
        v: ProtocolVersion,
        cs: CipherSuite,
        sessid: &SessionID,
        ticket: Vec<u8>,
        ms: Vec<u8>,
        server_cert_chain: &CertificatePayload,
    ) -> ClientSessionValue {
        unimplemented!()
    }

    pub fn set_extended_ms_used(&mut self) {
        unimplemented!()
    }

    pub fn set_times(&mut self, receipt_time_secs: u64, lifetime_secs: u32, age_add: u32) {
        unimplemented!()
    }

    pub fn has_expired(&self, time_now: u64) -> bool {
        unimplemented!()
    }

    pub fn get_obfuscated_ticket_age(&self, time_now: u64) -> u32 {
        unimplemented!()
    }

    pub fn take_ticket(&mut self) -> Vec<u8> {
        unimplemented!()
    }

    pub fn set_max_early_data_size(&mut self, sz: u32) {
        unimplemented!()
    }
}

// --- Server types ---
pub type ServerSessionKey = SessionID;

#[derive(Debug)]
pub struct ServerSessionValue {
    pub sni: Option<webpki::DNSName>,
    pub version: ProtocolVersion,
    pub cipher_suite: CipherSuite,
    pub master_secret: PayloadU8,
    pub extended_ms: bool,
    pub client_cert_chain: Option<CertificatePayload>,
    pub alpn: Option<PayloadU8>,
    pub application_data: PayloadU16,
}

impl Codec for ServerSessionValue {
    fn encode(&self, bytes: &mut Vec<u8>) {
        unimplemented!()
    }

    fn read(r: &mut Reader) -> Option<ServerSessionValue> {
        unimplemented!()
    }
}

impl ServerSessionValue {
    pub fn new(
        sni: Option<&webpki::DNSName>,
        v: ProtocolVersion,
        cs: CipherSuite,
        ms: Vec<u8>,
        cert_chain: &Option<CertificatePayload>,
        alpn: Option<Vec<u8>>,
        application_data: Vec<u8>,
    ) -> ServerSessionValue {
        unimplemented!()
    }

    pub fn set_extended_ms_used(&mut self) {
        unimplemented!()
    }
}
