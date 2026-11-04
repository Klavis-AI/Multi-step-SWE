use crate::error::TLSError;
use crate::key_schedule::{derive_traffic_iv, derive_traffic_key};
use crate::msgs::codec;
use crate::msgs::codec::Codec;
use crate::msgs::enums::{ContentType, ProtocolVersion};
use crate::msgs::fragmenter::MAX_FRAGMENT_LEN;
use crate::msgs::message::{BorrowMessage, Message, MessagePayload};
use crate::session::SessionSecrets;
use crate::suites::SupportedCipherSuite;
use ring::{aead, hkdf};
use std::io::Write;

/// Objects with this trait can decrypt TLS messages.
pub trait MessageDecrypter: Send + Sync {
    fn decrypt(&self, m: Message, seq: u64) -> Result<Message, TLSError>;
}

/// Objects with this trait can encrypt TLS messages.
pub trait MessageEncrypter: Send + Sync {
    fn encrypt(&self, m: BorrowMessage, seq: u64) -> Result<Message, TLSError>;
}

impl dyn MessageEncrypter {
    pub fn invalid() -> Box<dyn MessageEncrypter> {
        unimplemented!()
    }
}

impl dyn MessageDecrypter {
    pub fn invalid() -> Box<dyn MessageDecrypter> {
        unimplemented!()
    }
}

pub type MessageCipherPair = (Box<dyn MessageDecrypter>, Box<dyn MessageEncrypter>);

const TLS12_AAD_SIZE: usize = 8 + 1 + 2 + 2;
fn make_tls12_aad(
    seq: u64,
    typ: ContentType,
    vers: ProtocolVersion,
    len: usize,
) -> ring::aead::Aad<[u8; TLS12_AAD_SIZE]> {
    unimplemented!()
}

fn make_tls12_gcm_nonce(write_iv: &[u8], explicit: &[u8]) -> Iv {
    unimplemented!()
}

pub type BuildTLS12Decrypter = fn(&[u8], &[u8]) -> Box<dyn MessageDecrypter>;
pub type BuildTLS12Encrypter = fn(&[u8], &[u8], &[u8]) -> Box<dyn MessageEncrypter>;

pub fn build_tls12_gcm_128_decrypter(key: &[u8], iv: &[u8]) -> Box<dyn MessageDecrypter> {
    unimplemented!()
}

pub fn build_tls12_gcm_128_encrypter(
    key: &[u8],
    iv: &[u8],
    extra: &[u8],
) -> Box<dyn MessageEncrypter> {
    unimplemented!()
}

pub fn build_tls12_gcm_256_decrypter(key: &[u8], iv: &[u8]) -> Box<dyn MessageDecrypter> {
    unimplemented!()
}

pub fn build_tls12_gcm_256_encrypter(
    key: &[u8],
    iv: &[u8],
    extra: &[u8],
) -> Box<dyn MessageEncrypter> {
    unimplemented!()
}

pub fn build_tls12_chacha_decrypter(key: &[u8], iv: &[u8]) -> Box<dyn MessageDecrypter> {
    unimplemented!()
}

pub fn build_tls12_chacha_encrypter(key: &[u8], iv: &[u8], _: &[u8]) -> Box<dyn MessageEncrypter> {
    unimplemented!()
}

/// Make a `MessageCipherPair` based on the given supported ciphersuite `scs`,
/// and the session's `secrets`.
pub fn new_tls12(
    scs: &'static SupportedCipherSuite,
    secrets: &SessionSecrets,
) -> MessageCipherPair {
    unimplemented!()
}

pub fn new_tls13_read(
    scs: &'static SupportedCipherSuite,
    secret: &hkdf::Prk,
) -> Box<dyn MessageDecrypter> {
    unimplemented!()
}

pub fn new_tls13_write(
    scs: &'static SupportedCipherSuite,
    secret: &hkdf::Prk,
) -> Box<dyn MessageEncrypter> {
    unimplemented!()
}

/// A `MessageEncrypter` for AES-GCM AEAD ciphersuites. TLS 1.2 only.
pub struct GCMMessageEncrypter {
    enc_key: aead::LessSafeKey,
    iv: Iv,
}

/// A `MessageDecrypter` for AES-GCM AEAD ciphersuites.  TLS1.2 only.
pub struct GCMMessageDecrypter {
    dec_key: aead::LessSafeKey,
    dec_salt: [u8; 4],
}

const GCM_EXPLICIT_NONCE_LEN: usize = 8;
const GCM_OVERHEAD: usize = GCM_EXPLICIT_NONCE_LEN + 16;

impl MessageDecrypter for GCMMessageDecrypter {
    fn decrypt(&self, mut msg: Message, seq: u64) -> Result<Message, TLSError> {
        unimplemented!()
    }
}

impl MessageEncrypter for GCMMessageEncrypter {
    fn encrypt(&self, msg: BorrowMessage, seq: u64) -> Result<Message, TLSError> {
        unimplemented!()
    }
}

impl GCMMessageEncrypter {
    fn new(alg: &'static aead::Algorithm, enc_key: &[u8], iv: Iv) -> GCMMessageEncrypter {
        unimplemented!()
    }
}

impl GCMMessageDecrypter {
    fn new(alg: &'static aead::Algorithm, dec_key: &[u8], dec_iv: &[u8]) -> GCMMessageDecrypter {
        unimplemented!()
    }
}

/// A TLS 1.3 write or read IV.
pub(crate) struct Iv([u8; ring::aead::NONCE_LEN]);

impl Iv {
    pub(crate) fn new(value: [u8; ring::aead::NONCE_LEN]) -> Self {
        unimplemented!()
    }

    fn copy(value: &[u8]) -> Self {
        unimplemented!()
    }

    #[cfg(test)]
    pub(crate) fn value(&self) -> &[u8; 12] {
        unimplemented!()
    }
}

pub(crate) struct IvLen;

impl hkdf::KeyType for IvLen {
    fn len(&self) -> usize {
        unimplemented!()
    }
}

impl From<hkdf::Okm<'_, IvLen>> for Iv {
    fn from(okm: hkdf::Okm<IvLen>) -> Self {
        unimplemented!()
    }
}

struct TLS13MessageEncrypter {
    enc_key: aead::LessSafeKey,
    iv: Iv,
}

struct TLS13MessageDecrypter {
    dec_key: aead::LessSafeKey,
    iv: Iv,
}

fn unpad_tls13(v: &mut Vec<u8>) -> ContentType {
    unimplemented!()
}

fn make_tls13_nonce(iv: &Iv, seq: u64) -> ring::aead::Nonce {
    unimplemented!()
}

fn make_tls13_aad(len: usize) -> ring::aead::Aad<[u8; 1 + 2 + 2]> {
    unimplemented!()
}

impl MessageEncrypter for TLS13MessageEncrypter {
    fn encrypt(&self, msg: BorrowMessage, seq: u64) -> Result<Message, TLSError> {
        unimplemented!()
    }
}

impl MessageDecrypter for TLS13MessageDecrypter {
    fn decrypt(&self, mut msg: Message, seq: u64) -> Result<Message, TLSError> {
        unimplemented!()
    }
}

impl TLS13MessageEncrypter {
    fn new(key: aead::UnboundKey, enc_iv: Iv) -> TLS13MessageEncrypter {
        unimplemented!()
    }
}

impl TLS13MessageDecrypter {
    fn new(key: aead::UnboundKey, dec_iv: Iv) -> TLS13MessageDecrypter {
        unimplemented!()
    }
}

/// The RFC7905/RFC7539 ChaCha20Poly1305 construction.
/// This implementation does the AAD construction required in TLS1.2.
/// TLS1.3 uses `TLS13MessageEncrypter`.
pub struct ChaCha20Poly1305MessageEncrypter {
    enc_key: aead::LessSafeKey,
    enc_offset: Iv,
}

/// The RFC7905/RFC7539 ChaCha20Poly1305 construction.
/// This implementation does the AAD construction required in TLS1.2.
/// TLS1.3 uses `TLS13MessageDecrypter`.
pub struct ChaCha20Poly1305MessageDecrypter {
    dec_key: aead::LessSafeKey,
    dec_offset: Iv,
}

impl ChaCha20Poly1305MessageEncrypter {
    fn new(
        alg: &'static aead::Algorithm,
        enc_key: &[u8],
        enc_iv: Iv,
    ) -> ChaCha20Poly1305MessageEncrypter {
        unimplemented!()
    }
}

impl ChaCha20Poly1305MessageDecrypter {
    fn new(
        alg: &'static aead::Algorithm,
        dec_key: &[u8],
        dec_iv: Iv,
    ) -> ChaCha20Poly1305MessageDecrypter {
        unimplemented!()
    }
}

const CHACHAPOLY1305_OVERHEAD: usize = 16;

impl MessageDecrypter for ChaCha20Poly1305MessageDecrypter {
    fn decrypt(&self, mut msg: Message, seq: u64) -> Result<Message, TLSError> {
        unimplemented!()
    }
}

impl MessageEncrypter for ChaCha20Poly1305MessageEncrypter {
    fn encrypt(&self, msg: BorrowMessage, seq: u64) -> Result<Message, TLSError> {
        unimplemented!()
    }
}

/// A `MessageEncrypter` which doesn't work.
pub struct InvalidMessageEncrypter {}

impl MessageEncrypter for InvalidMessageEncrypter {
    fn encrypt(&self, _m: BorrowMessage, _seq: u64) -> Result<Message, TLSError> {
        unimplemented!()
    }
}

/// A `MessageDecrypter` which doesn't work.
pub struct InvalidMessageDecrypter {}

impl MessageDecrypter for InvalidMessageDecrypter {
    fn decrypt(&self, _m: Message, _seq: u64) -> Result<Message, TLSError> {
        unimplemented!()
    }
}
