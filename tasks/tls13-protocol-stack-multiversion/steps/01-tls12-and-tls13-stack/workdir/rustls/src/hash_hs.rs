#[cfg(feature = "logging")]
use crate::log::warn;
use crate::msgs::codec::Codec;
use crate::msgs::handshake::HandshakeMessagePayload;
use crate::msgs::message::{Message, MessagePayload};
use ring::digest;
use std::mem;

/// This deals with keeping a running hash of the handshake
/// payloads.  This is computed by buffering initially.  Once
/// we know what hash function we need to use we switch to
/// incremental hashing.
///
/// For client auth, we also need to buffer all the messages.
/// This is disabled in cases where client auth is not possible.
pub struct HandshakeHash {
    /// None before we know what hash function we're using
    alg: Option<&'static digest::Algorithm>,

    /// None before we know what hash function we're using
    ctx: Option<digest::Context>,

    /// true if we need to keep all messages
    client_auth_enabled: bool,

    /// buffer for pre-hashing stage and client-auth.
    buffer: Vec<u8>,
}

impl HandshakeHash {
    pub fn new() -> HandshakeHash {
        unimplemented!()
    }

    /// We might be doing client auth, so need to keep a full
    /// log of the handshake.
    pub fn set_client_auth_enabled(&mut self) {
        unimplemented!()
    }

    /// We decided not to do client auth after all, so discard
    /// the transcript.
    pub fn abandon_client_auth(&mut self) {
        unimplemented!()
    }

    /// We now know what hash function the verify_data will use.
    pub fn start_hash(&mut self, alg: &'static digest::Algorithm) -> bool {
        unimplemented!()
    }

    /// Hash/buffer a handshake message.
    pub fn add_message(&mut self, m: &Message) -> &mut HandshakeHash {
        unimplemented!()
    }

    /// Hash or buffer a byte slice.
    fn update_raw(&mut self, buf: &[u8]) -> &mut Self {
        unimplemented!()
    }

    /// Get the hash value if we were to hash `extra` too,
    /// using hash function `hash`.
    pub fn get_hash_given(&self, hash: &'static digest::Algorithm, extra: &[u8]) -> Vec<u8> {
        unimplemented!()
    }

    /// Take the current hash value, and encapsulate it in a
    /// 'handshake_hash' handshake message.  Start this hash
    /// again, with that message at the front.
    pub fn rollup_for_hrr(&mut self) {
        unimplemented!()
    }

    /// Get the current hash value.
    pub fn get_current_hash(&self) -> Vec<u8> {
        unimplemented!()
    }

    /// Takes this object's buffer containing all handshake messages
    /// so far.  This method only works once; it resets the buffer
    /// to empty.
    pub fn take_handshake_buf(&mut self) -> Vec<u8> {
        unimplemented!()
    }
}

#[cfg(test)]
mod test {
    use super::HandshakeHash;
    use ring::digest;

    #[test]
    fn hashes_correctly() {
        unimplemented!()
    }

    #[test]
    fn buffers_correctly() {
        unimplemented!()
    }

    #[test]
    fn abandon() {
        unimplemented!()
    }
}
