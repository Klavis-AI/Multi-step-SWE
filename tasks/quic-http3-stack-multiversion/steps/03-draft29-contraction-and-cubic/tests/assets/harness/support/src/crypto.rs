//! Packet protection keys: the version→salt table, the Initial key schedule,
//! AEAD open/seal with the QUIC nonce, and header protection masks.

use aes::cipher::BlockEncrypt as _;
use aes::cipher::KeyInit as _;
use aes_gcm::aead::AeadInPlace as _;
use aes_gcm::{Aes128Gcm, Aes256Gcm};
use chacha20::cipher::{KeyIvInit as _, StreamCipher as _, StreamCipherSeek as _};
use chacha20poly1305::ChaCha20Poly1305;

use crate::kdf;
use crate::{Error, Result};

/// QUIC version 1, RFC 9000.
pub const VERSION_V1: u32 = 0x0000_0001;
/// draft-ietf-quic-transport-23.
pub const VERSION_DRAFT23: u32 = 0xff00_0017;
/// draft-ietf-quic-transport-24.
pub const VERSION_DRAFT24: u32 = 0xff00_0018;
/// draft-ietf-quic-transport-25.
pub const VERSION_DRAFT25: u32 = 0xff00_0019;
/// draft-ietf-quic-transport-26.
pub const VERSION_DRAFT26: u32 = 0xff00_001a;
/// draft-ietf-quic-transport-27.
pub const VERSION_DRAFT27: u32 = 0xff00_001b;
/// draft-ietf-quic-transport-28.
pub const VERSION_DRAFT28: u32 = 0xff00_001c;
/// draft-ietf-quic-transport-29.
pub const VERSION_DRAFT29: u32 = 0xff00_001d;

/// Initial salt for QUIC v1.
///
/// Source: RFC 9001 Section 5.2, `initial_salt = 0x38762cf7...`, and confirmed
/// against `git show 0.9.0:src/crypto.rs` in the upstream quiche clone, where
/// it is `INITIAL_SALT`.
pub const INITIAL_SALT_V1: [u8; 20] = [
    0x38, 0x76, 0x2c, 0xf7, 0xf5, 0x59, 0x34, 0xb3, 0x4d, 0x17, 0x9a, 0xe6, 0xa4, 0xc8, 0x0c, 0xad,
    0xcc, 0xbb, 0x7f, 0x0a,
];

/// Initial salt introduced by draft-29 (draft-ietf-quic-tls-29 Section 5.2).
///
/// Source: upstream quiche clone, `git show 0.5.0:src/crypto.rs` — the
/// constant named `INITIAL_SALT` there, selected for every version that is not
/// draft-27 or draft-28, in a release whose `PROTOCOL_VERSION` is draft-29.
/// `git show 0.9.0:src/crypto.rs` names the same bytes `INITIAL_SALT_DRAFT29`,
/// removing any doubt about which version it belongs to.
pub const INITIAL_SALT_DRAFT29: [u8; 20] = [
    0xaf, 0xbf, 0xec, 0x28, 0x99, 0x93, 0xd2, 0x4c, 0x9e, 0x97, 0x86, 0xf1, 0x9c, 0x61, 0x11, 0xe0,
    0x43, 0x90, 0xa8, 0x99,
];

/// Initial salt introduced by draft-23 and kept until draft-29 replaced it.
///
/// Source: upstream quiche clone, `git show 0.1.0:src/crypto.rs` — the sole
/// `INITIAL_SALT` of a release whose `PROTOCOL_VERSION` is `0xff00_0017`
/// (draft-23). The same bytes appear as `INITIAL_SALT_OLD` in
/// `git show 0.5.0:src/crypto.rs` and as `INITIAL_SALT_DRAFT27` in
/// `git show 0.9.0:src/crypto.rs`.
pub const INITIAL_SALT_DRAFT23: [u8; 20] = [
    0xc3, 0xee, 0xf7, 0x12, 0xc7, 0x2e, 0xbb, 0x5a, 0x11, 0xa7, 0xd2, 0x43, 0x2b, 0xb4, 0x63, 0x65,
    0xbe, 0xf9, 0xf5, 0x02,
];

/// The version→salt table.
///
/// Read the provenance note before editing an entry. The grouping is *not*
/// "one salt per draft number you have heard of":
///
/// * drafts 23, 24, 25, 26, 27 and 28 all share [`INITIAL_SALT_DRAFT23`];
/// * drafts 29 and later drafts share [`INITIAL_SALT_DRAFT29`];
/// * v1 has its own, [`INITIAL_SALT_V1`].
///
/// In particular drafts 27 and 28 belong to the *draft-23* salt, not the
/// draft-29 one. quiche says so itself, twice and unambiguously: 0.5.0's
/// `derive_initial_secret` selects `INITIAL_SALT_OLD` (the draft-23 bytes)
/// exactly for `PROTOCOL_VERSION_DRAFT27 | PROTOCOL_VERSION_DRAFT28`, and
/// 0.9.0 renames that constant to `INITIAL_SALT_DRAFT27` while keeping the
/// same match arm. `tests/vectors.rs` pins both salts end to end against
/// quiche's own expected key bytes, so a transcription slip here fails a test
/// rather than turning into a phantom candidate bug.
///
/// draft-26 is the one entry no quiche release attests directly (quiche
/// shipped 23, 24, 25, 27, 28, 29 but never 26). It is bracketed on both
/// sides by attested draft-23-salt versions, and the IETF drafts changed the
/// salt only at 23 and at 29, so it is listed here for completeness.
static INITIAL_SALTS: &[(u32, &[u8; 20])] = &[
    (VERSION_V1, &INITIAL_SALT_V1),
    (VERSION_DRAFT29, &INITIAL_SALT_DRAFT29),
    (VERSION_DRAFT28, &INITIAL_SALT_DRAFT23),
    (VERSION_DRAFT27, &INITIAL_SALT_DRAFT23),
    (VERSION_DRAFT26, &INITIAL_SALT_DRAFT23),
    (VERSION_DRAFT25, &INITIAL_SALT_DRAFT23),
    (VERSION_DRAFT24, &INITIAL_SALT_DRAFT23),
    (VERSION_DRAFT23, &INITIAL_SALT_DRAFT23),
];

/// Look up the Initial salt for a long-header version number.
///
/// Unknown versions are an error, never a default. Silently falling back to
/// the v1 salt would turn "the verifier does not know this version" into "the
/// candidate's crypto is broken", which is the most expensive kind of wrong
/// answer this crate can give.
pub fn initial_salt(version: u32) -> Result<&'static [u8; 20]> {
    INITIAL_SALTS
        .iter()
        .find(|(v, _)| *v == version)
        .map(|(_, salt)| *salt)
        .ok_or(Error::UnknownVersion(version))
}

/// Whether [`initial_salt`] has an entry for this version.
pub fn is_known_version(version: u32) -> bool {
    initial_salt(version).is_ok()
}

/// AEAD algorithms QUIC defines packet protection for.
///
/// `TLS_AES_128_CCM_SHA256` is omitted: RFC 9001 defines no header protection
/// for `TLS_AES_128_CCM_8_SHA256` and quiche never negotiates CCM.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Suite {
    /// `AEAD_AES_128_GCM`, the Initial-packet suite for every version here.
    Aes128Gcm,
    /// `AEAD_AES_256_GCM`.
    Aes256Gcm,
    /// `AEAD_CHACHA20_POLY1305`.
    ChaCha20Poly1305,
}

impl Suite {
    /// Packet protection key length in bytes.
    pub fn key_len(self) -> usize {
        match self {
            Suite::Aes128Gcm => 16,
            Suite::Aes256Gcm | Suite::ChaCha20Poly1305 => 32,
        }
    }

    /// Header protection key length in bytes. Always equal to [`Self::key_len`]
    /// for the suites QUIC allows, but named separately because they are
    /// different keys derived with different labels.
    pub fn hp_key_len(self) -> usize {
        self.key_len()
    }

    /// Nonce/IV length in bytes. 12 for every QUIC suite.
    pub fn iv_len(self) -> usize {
        NONCE_LEN
    }

    /// Authentication tag length in bytes. 16 for every QUIC suite.
    pub fn tag_len(self) -> usize {
        TAG_LEN
    }
}

/// Length of a QUIC AEAD nonce.
pub const NONCE_LEN: usize = 12;

/// Length of a QUIC AEAD authentication tag.
pub const TAG_LEN: usize = 16;

/// One direction's packet protection keys: AEAD key, IV, header protection key.
#[derive(Clone)]
pub struct PacketKeys {
    suite: Suite,
    key: Vec<u8>,
    iv: [u8; NONCE_LEN],
    hp: Vec<u8>,
}

// Hand-written so that a `{:?}` in a failing test never prints key material.
impl std::fmt::Debug for PacketKeys {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("PacketKeys").field("suite", &self.suite).finish_non_exhaustive()
    }
}

impl PacketKeys {
    /// Build from raw key material, e.g. secrets recovered from a `SSLKEYLOGFILE`.
    pub fn new(suite: Suite, key: &[u8], iv: &[u8], hp: &[u8]) -> Result<Self> {
        if key.len() != suite.key_len() || hp.len() != suite.hp_key_len() {
            return Err(Error::Invalid("packet protection key length"));
        }
        if iv.len() != NONCE_LEN {
            return Err(Error::Invalid("packet protection IV length"));
        }

        let mut iv_buf = [0u8; NONCE_LEN];
        iv_buf.copy_from_slice(iv);

        Ok(PacketKeys { suite, key: key.to_vec(), iv: iv_buf, hp: hp.to_vec() })
    }

    /// Derive key, IV and header protection key from a traffic secret
    /// (RFC 9001 Section 5.1).
    pub fn from_secret(suite: Suite, secret: &[u8]) -> Result<Self> {
        let key = kdf::expand_label_to_vec(secret, kdf::LABEL_KEY, b"", suite.key_len())?;
        let iv = kdf::expand_label_to_vec(secret, kdf::LABEL_IV, b"", NONCE_LEN)?;
        let hp = kdf::expand_label_to_vec(secret, kdf::LABEL_HP, b"", suite.hp_key_len())?;

        Self::new(suite, &key, &iv, &hp)
    }

    /// The suite these keys are for.
    pub fn suite(&self) -> Suite {
        self.suite
    }

    /// The packet protection key.
    pub fn key(&self) -> &[u8] {
        &self.key
    }

    /// The packet protection IV, before the packet number is mixed in.
    pub fn iv(&self) -> &[u8; NONCE_LEN] {
        &self.iv
    }

    /// The header protection key.
    pub fn hp_key(&self) -> &[u8] {
        &self.hp
    }

    /// The AEAD nonce for a packet number (RFC 9001 Section 5.3).
    ///
    /// This is where QUIC parts company with a TLS 1.3 record layer. TLS mixes
    /// in an *implicit* 64-bit sequence number that both ends count locally
    /// and reset whenever keys change. QUIC mixes in the packet number carried
    /// in the header of this very packet: reconstructed to its full 62 bits,
    /// written big-endian, left-padded with zeros to 12 bytes, and XORed with
    /// the IV. Two consequences the code has to respect:
    ///
    /// * the number is the full reconstructed value, not the 1-4 truncated
    ///   bytes on the wire, so header protection must be removed and the
    ///   packet number decoded *before* the nonce exists at all;
    /// * packets may arrive reordered or be lost, and the nonce still follows
    ///   the packet, so there is no counter state to keep in sync.
    pub fn nonce(&self, packet_number: u64) -> [u8; NONCE_LEN] {
        let mut nonce = self.iv;
        let pn = packet_number.to_be_bytes();

        // The packet number is 8 bytes; the IV is 12. Left-pad with zeros,
        // i.e. XOR into the rightmost 8 bytes and leave the first 4 alone.
        for (n, p) in nonce[NONCE_LEN - 8..].iter_mut().zip(pn.iter()) {
            *n ^= *p;
        }

        nonce
    }

    /// Encrypt `plaintext`, returning ciphertext with the 16-byte tag appended.
    ///
    /// `aad` is the unprotected packet header, from the first byte through the
    /// packet number inclusive.
    pub fn seal(&self, packet_number: u64, aad: &[u8], plaintext: &[u8]) -> Result<Vec<u8>> {
        let nonce = self.nonce(packet_number);
        let mut buf = plaintext.to_vec();

        let tag = match self.suite {
            Suite::Aes128Gcm => Aes128Gcm::new_from_slice(&self.key)
                .map_err(|_| Error::Crypto("AES-128-GCM key length"))?
                .encrypt_in_place_detached(nonce.as_slice().into(), aad, &mut buf)
                .map_err(|_| Error::Crypto("AES-128-GCM seal"))?,
            Suite::Aes256Gcm => Aes256Gcm::new_from_slice(&self.key)
                .map_err(|_| Error::Crypto("AES-256-GCM key length"))?
                .encrypt_in_place_detached(nonce.as_slice().into(), aad, &mut buf)
                .map_err(|_| Error::Crypto("AES-256-GCM seal"))?,
            Suite::ChaCha20Poly1305 => ChaCha20Poly1305::new_from_slice(&self.key)
                .map_err(|_| Error::Crypto("ChaCha20-Poly1305 key length"))?
                .encrypt_in_place_detached(nonce.as_slice().into(), aad, &mut buf)
                .map_err(|_| Error::Crypto("ChaCha20-Poly1305 seal"))?,
        };

        buf.extend_from_slice(&tag);

        Ok(buf)
    }

    /// Decrypt `ciphertext` (which must include the trailing 16-byte tag).
    ///
    /// Returns [`Error::DecryptionFailed`] for any authentication failure, with
    /// no further detail: distinguishing "wrong key" from "wrong nonce" from
    /// "tampered" is not something an AEAD offers.
    pub fn open(&self, packet_number: u64, aad: &[u8], ciphertext: &[u8]) -> Result<Vec<u8>> {
        if ciphertext.len() < TAG_LEN {
            return Err(Error::Truncated("AEAD ciphertext (shorter than the tag)"));
        }

        let nonce = self.nonce(packet_number);
        let (body, tag) = ciphertext.split_at(ciphertext.len() - TAG_LEN);
        let mut buf = body.to_vec();

        let res = match self.suite {
            Suite::Aes128Gcm => Aes128Gcm::new_from_slice(&self.key)
                .map_err(|_| Error::Crypto("AES-128-GCM key length"))?
                .decrypt_in_place_detached(nonce.as_slice().into(), aad, &mut buf, tag.into()),
            Suite::Aes256Gcm => Aes256Gcm::new_from_slice(&self.key)
                .map_err(|_| Error::Crypto("AES-256-GCM key length"))?
                .decrypt_in_place_detached(nonce.as_slice().into(), aad, &mut buf, tag.into()),
            Suite::ChaCha20Poly1305 => ChaCha20Poly1305::new_from_slice(&self.key)
                .map_err(|_| Error::Crypto("ChaCha20-Poly1305 key length"))?
                .decrypt_in_place_detached(nonce.as_slice().into(), aad, &mut buf, tag.into()),
        };

        res.map_err(|_| Error::DecryptionFailed)?;

        Ok(buf)
    }

    /// Compute the 5-byte header protection mask from a 16-byte ciphertext
    /// sample (RFC 9001 Sections 5.4.3 and 5.4.4).
    ///
    /// The AES suites run one ECB block through the header protection key; the
    /// ChaCha suite runs the raw ChaCha20 block function with the first 4 bytes
    /// of the sample as a little-endian block counter and the remaining 12 as
    /// the nonce, encrypting five zero bytes. Only the first 5 bytes of either
    /// output are ever used: one for the low bits of the first byte and four
    /// for the largest possible packet number field.
    pub fn header_mask(&self, sample: &[u8]) -> Result<[u8; 5]> {
        if sample.len() != SAMPLE_LEN {
            return Err(Error::Invalid("header protection sample length"));
        }

        let mut mask = [0u8; 5];

        match self.suite {
            Suite::Aes128Gcm | Suite::Aes256Gcm => {
                let mut block = *aes::cipher::generic_array::GenericArray::from_slice(sample);

                match self.suite {
                    Suite::Aes128Gcm => aes::Aes128::new_from_slice(&self.hp)
                        .map_err(|_| Error::Crypto("AES-128 header protection key length"))?
                        .encrypt_block(&mut block),
                    _ => aes::Aes256::new_from_slice(&self.hp)
                        .map_err(|_| Error::Crypto("AES-256 header protection key length"))?
                        .encrypt_block(&mut block),
                }

                mask.copy_from_slice(&block[..5]);
            },

            Suite::ChaCha20Poly1305 => {
                let counter = u32::from_le_bytes([sample[0], sample[1], sample[2], sample[3]]);

                let mut cipher = chacha20::ChaCha20::new_from_slices(&self.hp, &sample[4..16])
                    .map_err(|_| Error::Crypto("ChaCha20 header protection key length"))?;

                // `seek` is in bytes; one ChaCha20 block is 64 of them. This is
                // how the sample's counter becomes the block counter without
                // reaching into the cipher's internals.
                cipher
                    .try_seek(u64::from(counter) * 64)
                    .map_err(|_| Error::Crypto("ChaCha20 counter out of range"))?;
                cipher.apply_keystream(&mut mask);
            },
        }

        Ok(mask)
    }
}

/// Number of ciphertext bytes sampled for header protection.
pub const SAMPLE_LEN: usize = 16;

/// The three secrets RFC 9001 Section 5.2 derives from a connection ID.
#[derive(Clone, Debug)]
pub struct InitialSecrets {
    /// `HKDF-Extract(initial_salt, client_dcid)`.
    pub initial_secret: [u8; kdf::HASH_LEN],
    /// `HKDF-Expand-Label(initial_secret, "client in", "", 32)`.
    pub client: [u8; kdf::HASH_LEN],
    /// `HKDF-Expand-Label(initial_secret, "server in", "", 32)`.
    pub server: [u8; kdf::HASH_LEN],
}

/// Both directions' Initial packet protection keys.
#[derive(Clone, Debug)]
pub struct InitialKeys {
    /// Keys protecting packets the client sends.
    pub client: PacketKeys,
    /// Keys protecting packets the server sends.
    pub server: PacketKeys,
}

/// Derive the Initial secrets for a version and a Destination Connection ID.
///
/// `odcid` is the DCID from the client's *first* Initial packet. It is not
/// whatever DCID happens to be in the packet being decrypted: once the server
/// chooses its own connection ID the client switches to it, but both ends keep
/// using Initial keys derived from the original value (RFC 9001 Section 5.2,
/// "The secrets do not change when a client changes the Destination Connection
/// ID it uses in response to an Initial packet from the server"). The one
/// exception is a Retry, which *does* rekey the Initial packets onto the
/// server-selected connection ID from the Retry's Source Connection ID field.
pub fn derive_initial_secrets(version: u32, odcid: &[u8]) -> Result<InitialSecrets> {
    if odcid.len() > crate::packet::MAX_CID_LEN {
        return Err(Error::Invalid("connection ID (longer than 20 bytes)"));
    }

    let salt = initial_salt(version)?;
    let initial_secret = kdf::extract(salt, odcid);

    let mut client = [0u8; kdf::HASH_LEN];
    let mut server = [0u8; kdf::HASH_LEN];
    kdf::expand_label(&initial_secret, kdf::LABEL_CLIENT_IN, b"", &mut client)?;
    kdf::expand_label(&initial_secret, kdf::LABEL_SERVER_IN, b"", &mut server)?;

    Ok(InitialSecrets { initial_secret, client, server })
}

/// Derive both directions' Initial keys for a version and a DCID.
///
/// Initial packets always use `AEAD_AES_128_GCM` with SHA-256: the suite is
/// not negotiated yet when they are sent.
pub fn derive_initial_keys(version: u32, odcid: &[u8]) -> Result<InitialKeys> {
    let secrets = derive_initial_secrets(version, odcid)?;

    Ok(InitialKeys {
        client: PacketKeys::from_secret(Suite::Aes128Gcm, &secrets.client)?,
        server: PacketKeys::from_secret(Suite::Aes128Gcm, &secrets.server)?,
    })
}

/// Retry integrity key and nonce for QUIC v1.
///
/// Source: RFC 9001 Section 5.8 — "The secret key, K, is 128 bits equal to
/// 0xbe0c690b9f66575a1d766b54e368c84e" and "The nonce, N, is 96 bits equal to
/// 0x461599d35d632bf2239825bb". Also present in the extracted Appendix A
/// vectors as `retry_integrity_key` / `retry_integrity_nonce`.
const RETRY_KEY_V1: [u8; 16] = [
    0xbe, 0x0c, 0x69, 0x0b, 0x9f, 0x66, 0x57, 0x5a, 0x1d, 0x76, 0x6b, 0x54, 0xe3, 0x68, 0xc8, 0x4e,
];
const RETRY_NONCE_V1: [u8; NONCE_LEN] = [
    0x46, 0x15, 0x99, 0xd3, 0x5d, 0x63, 0x2b, 0xf2, 0x23, 0x98, 0x25, 0xbb,
];

/// Retry integrity key and nonce for draft-29.
///
/// Source: upstream quiche clone, `git show 0.5.0:src/packet.rs`, constants
/// `RETRY_INTEGRITY_KEY` / `RETRY_INTEGRITY_NONCE` (the arm taken for every
/// version other than draft-27 and draft-28, in a draft-29 release).
const RETRY_KEY_DRAFT29: [u8; 16] = [
    0xcc, 0xce, 0x18, 0x7e, 0xd0, 0x9a, 0x09, 0xd0, 0x57, 0x28, 0x15, 0x5a, 0x6c, 0xb9, 0x6b, 0xe1,
];
const RETRY_NONCE_DRAFT29: [u8; NONCE_LEN] = [
    0xe5, 0x49, 0x30, 0xf9, 0x7f, 0x21, 0x36, 0xf0, 0x53, 0x0a, 0x8c, 0x1c,
];

/// Retry integrity key and nonce for drafts 25 through 28.
///
/// Source: upstream quiche clone, `git show 0.5.0:src/packet.rs`
/// `RETRY_INTEGRITY_KEY_OLD` / `_NONCE_OLD` (selected for draft-27 and
/// draft-28), which are the same bytes as the sole `RETRY_INTEGRITY_KEY` in
/// `git show 0.3.0:src/packet.rs`, a release covering drafts 23 through 27.
const RETRY_KEY_DRAFT25: [u8; 16] = [
    0x4d, 0x32, 0xec, 0xdb, 0x2a, 0x21, 0x33, 0xc8, 0x41, 0xe4, 0x04, 0x3d, 0xf2, 0x7d, 0x44, 0x30,
];
const RETRY_NONCE_DRAFT25: [u8; NONCE_LEN] = [
    0x4d, 0x16, 0x11, 0xd0, 0x55, 0x13, 0xa5, 0x52, 0xc5, 0x87, 0xd5, 0x75,
];

/// The Retry integrity key and nonce this crate uses for `version`.
///
/// Exposed for the same reason [`initial_salt`] is: these are transcribed
/// constants, and a verifier should be able to check them against a published
/// value directly instead of only through a tag that happens to come out
/// right. Drafts 23 and 24 have no Retry integrity tag and return
/// [`Error::UnsupportedForVersion`].
pub fn retry_integrity_keys(
    version: u32,
) -> Result<(&'static [u8; 16], &'static [u8; NONCE_LEN])> {
    retry_key(version)
}

fn retry_key(version: u32) -> Result<(&'static [u8; 16], &'static [u8; NONCE_LEN])> {
    match version {
        VERSION_V1 => Ok((&RETRY_KEY_V1, &RETRY_NONCE_V1)),
        VERSION_DRAFT29 => Ok((&RETRY_KEY_DRAFT29, &RETRY_NONCE_DRAFT29)),
        VERSION_DRAFT25 | VERSION_DRAFT26 | VERSION_DRAFT27 | VERSION_DRAFT28 =>
            Ok((&RETRY_KEY_DRAFT25, &RETRY_NONCE_DRAFT25)),

        // Drafts 23 and 24 predate the Retry Integrity Tag entirely: their
        // Retry packets carry the Original Destination Connection ID inline
        // and nothing to authenticate it with. See `RetryPacket::parse`.
        VERSION_DRAFT23 | VERSION_DRAFT24 => Err(Error::UnsupportedForVersion {
            version,
            what: "Retry Integrity Tag",
        }),

        _ => Err(Error::UnknownVersion(version)),
    }
}

/// Compute the Retry Integrity Tag over a Retry packet (RFC 9001 Section 5.8).
///
/// `retry_without_tag` is the Retry packet as transmitted with its trailing
/// 16-byte tag removed; `odcid` is the Destination Connection ID of the Initial
/// packet the Retry answers — the value that is authenticated but never sent,
/// which is exactly what stops an off-path attacker from forging a Retry.
pub fn retry_integrity_tag(
    version: u32, odcid: &[u8], retry_without_tag: &[u8],
) -> Result<[u8; TAG_LEN]> {
    if odcid.len() > crate::packet::MAX_CID_LEN {
        return Err(Error::Invalid("connection ID (longer than 20 bytes)"));
    }

    let (key, nonce) = retry_key(version)?;

    // Retry Pseudo-Packet: ODCID Length, ODCID, then the Retry packet itself.
    let mut pseudo = Vec::with_capacity(1 + odcid.len() + retry_without_tag.len());
    pseudo.push(odcid.len() as u8);
    pseudo.extend_from_slice(odcid);
    pseudo.extend_from_slice(retry_without_tag);

    // The tag is an AEAD over an empty plaintext: the pseudo-packet is all
    // associated data, and the "ciphertext" is just the tag.
    let tag = Aes128Gcm::new_from_slice(key)
        .map_err(|_| Error::Crypto("Retry integrity key length"))?
        .encrypt_in_place_detached(nonce.as_slice().into(), &pseudo, &mut [])
        .map_err(|_| Error::Crypto("Retry integrity tag"))?;

    let mut out = [0u8; TAG_LEN];
    out.copy_from_slice(&tag);

    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn unknown_version_is_an_error_not_a_default() {
        assert_eq!(initial_salt(0xdead_beef).unwrap_err(), Error::UnknownVersion(0xdead_beef));
        assert_eq!(initial_salt(0x0000_0002).unwrap_err(), Error::UnknownVersion(2));
        assert!(!is_known_version(0x6b33_43cf));
        assert_eq!(
            derive_initial_keys(0xff00_0016, &[0; 8]).unwrap_err(),
            Error::UnknownVersion(0xff00_0016)
        );
    }

    #[test]
    fn salt_table_groups_drafts_the_way_quiche_does() {
        assert_eq!(initial_salt(VERSION_V1).unwrap(), &INITIAL_SALT_V1);
        assert_eq!(initial_salt(VERSION_DRAFT29).unwrap(), &INITIAL_SALT_DRAFT29);
        for v in [
            VERSION_DRAFT23,
            VERSION_DRAFT24,
            VERSION_DRAFT25,
            VERSION_DRAFT26,
            VERSION_DRAFT27,
            VERSION_DRAFT28,
        ] {
            assert_eq!(initial_salt(v).unwrap(), &INITIAL_SALT_DRAFT23, "version {v:#x}");
        }
        assert_ne!(INITIAL_SALT_V1, INITIAL_SALT_DRAFT29);
        assert_ne!(INITIAL_SALT_DRAFT29, INITIAL_SALT_DRAFT23);
    }

    #[test]
    fn drafts_without_a_retry_tag_say_so() {
        for v in [VERSION_DRAFT23, VERSION_DRAFT24] {
            assert_eq!(
                retry_integrity_tag(v, &[0; 8], &[0; 20]).unwrap_err(),
                Error::UnsupportedForVersion { version: v, what: "Retry Integrity Tag" }
            );
        }
    }

    #[test]
    fn nonce_xors_the_packet_number_into_the_right_hand_bytes() {
        let keys = PacketKeys::new(Suite::Aes128Gcm, &[0; 16], &[0xff; 12], &[0; 16]).unwrap();
        assert_eq!(keys.nonce(0), [0xff; 12]);
        // 0x0102030405060708 lands in the last 8 bytes, high byte first.
        assert_eq!(
            keys.nonce(0x0102_0304_0506_0708),
            [0xff, 0xff, 0xff, 0xff, 0xfe, 0xfd, 0xfc, 0xfb, 0xfa, 0xf9, 0xf8, 0xf7]
        );
    }

    #[test]
    fn key_lengths_are_checked() {
        assert_eq!(
            PacketKeys::new(Suite::Aes128Gcm, &[0; 15], &[0; 12], &[0; 16]).unwrap_err(),
            Error::Invalid("packet protection key length")
        );
        assert_eq!(
            PacketKeys::new(Suite::Aes128Gcm, &[0; 16], &[0; 11], &[0; 16]).unwrap_err(),
            Error::Invalid("packet protection IV length")
        );
    }
}
