/// This module contains optional APIs for implementing QUIC TLS.
use crate::client::{ClientConfig, ClientSession, ClientSessionImpl};
use crate::error::TLSError;
use crate::key_schedule::hkdf_expand;
use crate::msgs::enums::{AlertDescription, ContentType, ProtocolVersion};
use crate::msgs::handshake::{ClientExtension, ServerExtension};
use crate::msgs::message::{Message, MessagePayload};
use crate::server::{ServerConfig, ServerSession, ServerSessionImpl};
use crate::session::{Protocol, SessionCommon};
use crate::suites::{BulkAlgorithm, SupportedCipherSuite, TLS13_AES_128_GCM_SHA256};

use std::sync::Arc;

use ring::{aead, hkdf};
use webpki;

/// Secrets used to encrypt/decrypt traffic
#[derive(Clone, Debug)]
pub(crate) struct Secrets {
    /// Secret used to encrypt packets transmitted by the client
    pub client: hkdf::Prk,
    /// Secret used to encrypt packets transmitted by the server
    pub server: hkdf::Prk,
}

impl Secrets {
    fn local_remote(&self, is_client: bool) -> (&hkdf::Prk, &hkdf::Prk) {
        unimplemented!()
    }
}

/// Generic methods for QUIC sessions
pub trait QuicExt {
    /// Return the TLS-encoded transport parameters for the session's peer.
    fn get_quic_transport_parameters(&self) -> Option<&[u8]>;

    /// Compute the keys for encrypting/decrypting 0-RTT packets, if available
    fn get_0rtt_keys(&self) -> Option<DirectionalKeys>;

    /// Consume unencrypted TLS handshake data.
    ///
    /// Handshake data obtained from separate encryption levels should be supplied in separate calls.
    fn read_hs(&mut self, plaintext: &[u8]) -> Result<(), TLSError>;

    /// Emit unencrypted TLS handshake data.
    ///
    /// When this returns `Some(_)`, the new keys must be used for future handshake data.
    fn write_hs(&mut self, buf: &mut Vec<u8>) -> Option<Keys>;

    /// Emit the TLS description code of a fatal alert, if one has arisen.
    ///
    /// Check after `read_hs` returns `Err(_)`.
    fn get_alert(&self) -> Option<AlertDescription>;

    /// Compute the keys to use following a 1-RTT key update
    ///
    /// Must not be called until the handshake is complete
    fn next_1rtt_keys(&mut self) -> PacketKeySet;
}

impl QuicExt for ClientSession {
    fn get_quic_transport_parameters(&self) -> Option<&[u8]> {
        unimplemented!()
    }

    fn get_0rtt_keys(&self) -> Option<DirectionalKeys> {
        unimplemented!()
    }

    fn read_hs(&mut self, plaintext: &[u8]) -> Result<(), TLSError> {
        unimplemented!()
    }

    fn write_hs(&mut self, buf: &mut Vec<u8>) -> Option<Keys> {
        unimplemented!()
    }

    fn get_alert(&self) -> Option<AlertDescription> {
        unimplemented!()
    }

    fn next_1rtt_keys(&mut self) -> PacketKeySet {
        unimplemented!()
    }
}

impl QuicExt for ServerSession {
    fn get_quic_transport_parameters(&self) -> Option<&[u8]> {
        unimplemented!()
    }

    fn get_0rtt_keys(&self) -> Option<DirectionalKeys> {
        unimplemented!()
    }

    fn read_hs(&mut self, plaintext: &[u8]) -> Result<(), TLSError> {
        unimplemented!()
    }
    fn write_hs(&mut self, buf: &mut Vec<u8>) -> Option<Keys> {
        unimplemented!()
    }

    fn get_alert(&self) -> Option<AlertDescription> {
        unimplemented!()
    }

    fn next_1rtt_keys(&mut self) -> PacketKeySet {
        unimplemented!()
    }
}

/// Keys used to communicate in a single direction
pub struct DirectionalKeys {
    /// Encrypts or decrypts a packet's headers
    pub header: aead::quic::HeaderProtectionKey,
    /// Encrypts or decrypts the payload of a packet
    pub packet: PacketKey,
}

impl DirectionalKeys {
    fn new(suite: &'static SupportedCipherSuite, secret: &hkdf::Prk) -> Self {
        unimplemented!()
    }
}

/// Keys to encrypt or decrypt the payload of a packet
pub struct PacketKey {
    /// Encrypts or decrypts a packet's payload
    pub key: aead::LessSafeKey,
    /// Computes unique nonces for each packet
    pub iv: Iv,
}

impl PacketKey {
    fn new(suite: &'static SupportedCipherSuite, secret: &hkdf::Prk) -> Self {
        unimplemented!()
    }
}

/// Packet protection keys for bidirectional 1-RTT communication
pub struct PacketKeySet {
    /// Encrypts outgoing packets
    pub local: PacketKey,
    /// Decrypts incoming packets
    pub remote: PacketKey,
}

/// Computes unique nonces for each packet
pub struct Iv([u8; aead::NONCE_LEN]);

impl Iv {
    /// Compute the nonce to use for encrypting or decrypting `packet_number`
    pub fn nonce_for(&self, packet_number: u64) -> ring::aead::Nonce {
        unimplemented!()
    }
}

impl From<hkdf::Okm<'_, IvLen>> for Iv {
    fn from(okm: hkdf::Okm<IvLen>) -> Self {
        unimplemented!()
    }
}

struct IvLen;

impl hkdf::KeyType for IvLen {
    fn len(&self) -> usize {
        unimplemented!()
    }
}

/// Complete set of keys used to communicate with the peer
pub struct Keys {
    /// Encrypts outgoing packets
    pub local: DirectionalKeys,
    /// Decrypts incoming packets
    pub remote: DirectionalKeys,
}

impl Keys {
    /// Construct keys for use with initial packets
    pub fn initial(
        initial_salt: &hkdf::Salt,
        client_dst_connection_id: &[u8],
        is_client: bool,
    ) -> Self {
        unimplemented!()
    }

    fn new(suite: &'static SupportedCipherSuite, is_client: bool, secrets: &Secrets) -> Self {
        unimplemented!()
    }
}

fn read_hs(this: &mut SessionCommon, plaintext: &[u8]) -> Result<(), TLSError> {
    unimplemented!()
}

fn write_hs(this: &mut SessionCommon, buf: &mut Vec<u8>) -> Option<Keys> {
    unimplemented!()
}

fn next_1rtt_keys(this: &mut SessionCommon) -> PacketKeySet {
    unimplemented!()
}

fn next_1rtt_secrets(hkdf_alg: hkdf::Algorithm, prev: &Secrets) -> Secrets {
    unimplemented!()
}

/// Methods specific to QUIC client sessions
pub trait ClientQuicExt {
    /// Make a new QUIC ClientSession. This differs from `ClientSession::new()`
    /// in that it takes an extra argument, `params`, which contains the
    /// TLS-encoded transport parameters to send.
    fn new_quic(
        config: &Arc<ClientConfig>,
        hostname: webpki::DNSNameRef,
        params: Vec<u8>,
    ) -> ClientSession {
        unimplemented!()
    }
}

impl ClientQuicExt for ClientSession {}

/// Methods specific to QUIC server sessions
pub trait ServerQuicExt {
    /// Make a new QUIC ServerSession. This differs from `ServerSession::new()`
    /// in that it takes an extra argument, `params`, which contains the
    /// TLS-encoded transport parameters to send.
    fn new_quic(config: &Arc<ServerConfig>, params: Vec<u8>) -> ServerSession {
        unimplemented!()
    }
}

impl ServerQuicExt for ServerSession {}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn initial_keys_test_vectors() {
        unimplemented!()
    }

    #[test]
    fn key_update_test_vector() {
        unimplemented!()
    }
}
