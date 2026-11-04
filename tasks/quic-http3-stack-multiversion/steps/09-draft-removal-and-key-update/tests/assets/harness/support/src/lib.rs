//! Independent QUIC packet protection for the quiche multi-version verifier.
//!
//! A driver owns a UDP socket, captures a candidate's first flight, and has to
//! answer one question without ever calling into the candidate: *is this a
//! correctly protected QUIC packet?* This crate is the second implementation
//! that makes that question answerable. It derives Initial keys from the salt
//! and the client's Destination Connection ID, removes header protection,
//! decrypts, parses frames, and can build a protected client Initial to send.
//!
//! Nothing in here is generic over a TLS stack and nothing links C. The only
//! inputs are bytes off the wire.
//!
//! # Shape of a typical use
//!
//! ```no_run
//! use quiche_verifier_support::{decrypt_initial, Perspective, VERSION_V1};
//!
//! # fn main() -> Result<(), quiche_verifier_support::Error> {
//! # let datagram: Vec<u8> = Vec::new();
//! // `datagram` is exactly what recv_from() gave us.
//! let initial = decrypt_initial(&datagram, VERSION_V1, Perspective::Client)?;
//! let client_hello = initial.crypto_data()?;
//! # let _ = client_hello;
//! # Ok(())
//! # }
//! ```
//!
//! # Errors, not panics
//!
//! Every entry point returns [`Result`]. A malformed or undecryptable packet
//! from a candidate must surface as a failed assertion in a test, never as a
//! panicking verifier: a crashed verifier scores nothing and diagnoses
//! nothing. Slicing is bounds-checked explicitly rather than by indexing.
//!
//! # Where QUIC departs from plain TLS 1.3
//!
//! Three places, all of them easy to get subtly wrong, and all of them
//! commented at the implementation site:
//!
//! * the AEAD nonce is the packet's own packet number XORed into the IV
//!   ([`PacketKeys::nonce`]), not an implicit record counter;
//! * the header itself is encrypted with a separate key and a mask derived
//!   from the *ciphertext* ([`remove_header_protection`]), which is why the
//!   packet number cannot be read before the sample is taken;
//! * Initial keys come from the client's original Destination Connection ID
//!   ([`derive_initial_keys`]) and do not follow the connection IDs the peers
//!   pick later.

#![forbid(unsafe_code)]
#![deny(missing_docs)]
#![deny(missing_debug_implementations)]

use std::fmt;

pub mod frame;
pub mod kdf;
pub mod varint;

mod crypto;
mod initial;
mod packet;

pub use crate::crypto::{
    derive_initial_keys, derive_initial_secrets, initial_salt, is_known_version,
    retry_integrity_keys, retry_integrity_tag, InitialKeys, InitialSecrets, PacketKeys, Suite,
    INITIAL_SALT_DRAFT23, INITIAL_SALT_DRAFT29, INITIAL_SALT_V1, NONCE_LEN, SAMPLE_LEN, TAG_LEN,
    VERSION_DRAFT23, VERSION_DRAFT24, VERSION_DRAFT25, VERSION_DRAFT26, VERSION_DRAFT27,
    VERSION_DRAFT28, VERSION_DRAFT29, VERSION_V1,
};
pub use crate::frame::{frames, parse_frames, CryptoAssembler, EcnCounts, Frame, Frames};
pub use crate::initial::{
    decrypt_initial, decrypt_initial_any_version, decrypt_initial_with_odcid, ClientInitial,
    DecryptedInitial, Perspective,
};
pub use crate::packet::{
    apply_header_protection, decode_packet_number, has_fixed_bit, header_protection_sample,
    is_long_header, packet_number_len, parse_packet, peek_version, remove_header_protection,
    verify_retry_integrity, LongHeader, LongPacketType, Packet, PacketNumberField, RetryPacket,
    ShortHeader, VersionNegotiationPacket, MAX_CID_LEN,
};
pub use crate::varint::{decode_varint, encode_varint, varint_len, Reader};

/// Result alias used throughout the crate.
pub type Result<T> = std::result::Result<T, Error>;

/// Everything that can go wrong while inspecting a datagram.
///
/// The variants distinguish "the bytes ran out", "the bytes were nonsense" and
/// "the bytes did not authenticate", because those three mean very different
/// things when a candidate produced them: the first two usually indicate a
/// framing bug, the third a key-schedule bug.
#[derive(Clone, Debug, PartialEq, Eq)]
#[non_exhaustive]
pub enum Error {
    /// A field ran off the end of the buffer. The payload names the field.
    Truncated(&'static str),
    /// A field was present but structurally invalid. The payload names it.
    Invalid(&'static str),
    /// No Initial salt is registered for this long-header version number.
    ///
    /// Deliberately not a silent fallback to QUIC v1: deriving Initial keys
    /// with the wrong salt produces garbage that looks exactly like a
    /// candidate crypto bug.
    UnknownVersion(u32),
    /// The packet's version field disagrees with the version the caller asked
    /// to interpret it as.
    VersionMismatch {
        /// Version the caller passed in.
        expected: u32,
        /// Version found in the packet's long header.
        actual: u32,
    },
    /// The operation is defined, but not for this QUIC version. Used for
    /// e.g. the Retry Integrity Tag, which drafts 23 and 24 predate.
    UnsupportedForVersion {
        /// The version in question.
        version: u32,
        /// What was being attempted.
        what: &'static str,
    },
    /// AEAD authentication failed: wrong keys, wrong nonce, wrong associated
    /// data, or corrupted ciphertext. Indistinguishable by design.
    DecryptionFailed,
    /// A frame type this crate does not decode was found in the payload.
    ///
    /// Frame iteration cannot skip an unknown frame, because a frame's length
    /// is implied by its type; iteration stops here rather than resynchronising
    /// on garbage.
    UnknownFrame(u64),
    /// An underlying primitive rejected its inputs (bad key length and the
    /// like). Should not happen with well-formed callers.
    Crypto(&'static str),
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Error::Truncated(what) => write!(f, "buffer too short while reading {what}"),
            Error::Invalid(what) => write!(f, "invalid {what}"),
            Error::UnknownVersion(v) => {
                write!(f, "no Initial salt registered for QUIC version {v:#010x}")
            },
            Error::VersionMismatch { expected, actual } => write!(
                f,
                "packet version {actual:#010x} does not match requested version {expected:#010x}"
            ),
            Error::UnsupportedForVersion { version, what } => {
                write!(f, "{what} is not defined for QUIC version {version:#010x}")
            },
            Error::DecryptionFailed => write!(f, "AEAD authentication failed"),
            Error::UnknownFrame(ty) => write!(f, "unhandled QUIC frame type {ty:#x}"),
            Error::Crypto(what) => write!(f, "crypto primitive rejected input: {what}"),
        }
    }
}

impl std::error::Error for Error {}
