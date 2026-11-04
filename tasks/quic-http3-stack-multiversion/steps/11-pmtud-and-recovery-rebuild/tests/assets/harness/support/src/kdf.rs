//! HKDF-Expand-Label as TLS 1.3 and QUIC define it.
//!
//! QUIC reuses the TLS 1.3 key schedule wholesale (RFC 8446 Section 7.1),
//! including the `"tls13 "` label prefix, and adds its own set of labels in
//! RFC 9001 Section 5.1. The hash is SHA-256 everywhere the *Initial* keys are
//! concerned (RFC 9001 Section 5.2 pins it), which is why this module is not
//! generic over a digest: an Initial packet derived with anything else is
//! wrong by definition, and a generic parameter would only make that mistake
//! expressible.
//!
//! Handshake and 1-RTT keys for a TLS_AES_256_GCM_SHA384 connection would need
//! SHA-384; that is out of scope here because the verifier derives only
//! Initial keys itself and is handed later secrets directly.

use hkdf::Hkdf;
use sha2::Sha256;

use crate::{Error, Result};

/// Output length of the hash used by the QUIC Initial key schedule.
pub const HASH_LEN: usize = 32;

/// Prefix every TLS 1.3 HkdfLabel carries (RFC 8446 Section 7.1).
const LABEL_PREFIX: &[u8] = b"tls13 ";

/// Label deriving the client's Initial secret (RFC 9001 Section 5.2).
pub const LABEL_CLIENT_IN: &str = "client in";
/// Label deriving the server's Initial secret (RFC 9001 Section 5.2).
pub const LABEL_SERVER_IN: &str = "server in";
/// Label deriving a packet protection key (RFC 9001 Section 5.1).
pub const LABEL_KEY: &str = "quic key";
/// Label deriving a packet protection IV (RFC 9001 Section 5.1).
pub const LABEL_IV: &str = "quic iv";
/// Label deriving a header protection key (RFC 9001 Section 5.1).
pub const LABEL_HP: &str = "quic hp";
/// Label deriving the next generation of a secret on key update
/// (RFC 9001 Section 6.1).
pub const LABEL_KU: &str = "quic ku";

/// HKDF-Extract with SHA-256.
///
/// For QUIC Initial keys the salt is the version's Initial salt and the input
/// keying material is the client's Destination Connection ID.
pub fn extract(salt: &[u8], ikm: &[u8]) -> [u8; HASH_LEN] {
    let (prk, _) = Hkdf::<Sha256>::extract(Some(salt), ikm);
    let mut out = [0u8; HASH_LEN];
    out.copy_from_slice(&prk);
    out
}

/// Encode the `HkdfLabel` structure that HKDF-Expand consumes as `info`.
///
/// ```text
/// struct {
///     uint16 length = Length;
///     opaque label<7..255> = "tls13 " + Label;
///     opaque context<0..255> = Context;
/// } HkdfLabel;
/// ```
///
/// Exposed because RFC 9001 Appendix A.1 publishes these encodings directly,
/// which makes them a cheap and very precise self-test: a crate that gets the
/// prefix or the two length bytes wrong fails here rather than 300 lines later
/// with an opaque AEAD failure.
pub fn hkdf_label(label: &str, context: &[u8], out_len: usize) -> Result<Vec<u8>> {
    let label_len = LABEL_PREFIX.len() + label.len();
    if label_len > 255 {
        return Err(Error::Invalid("HkdfLabel label (longer than 255 bytes)"));
    }
    if context.len() > 255 {
        return Err(Error::Invalid("HkdfLabel context (longer than 255 bytes)"));
    }
    let len = u16::try_from(out_len).map_err(|_| Error::Invalid("HkdfLabel length"))?;

    let mut info = Vec::with_capacity(2 + 1 + label_len + 1 + context.len());
    info.extend_from_slice(&len.to_be_bytes());
    info.push(label_len as u8);
    info.extend_from_slice(LABEL_PREFIX);
    info.extend_from_slice(label.as_bytes());
    info.push(context.len() as u8);
    info.extend_from_slice(context);

    Ok(info)
}

/// HKDF-Expand-Label into a caller-provided buffer.
///
/// `prk` must be at least [`HASH_LEN`] bytes: it is a secret produced by
/// HKDF-Extract or by a previous HKDF-Expand-Label, never a raw password.
pub fn expand_label(prk: &[u8], label: &str, context: &[u8], out: &mut [u8]) -> Result<()> {
    let info = hkdf_label(label, context, out.len())?;

    Hkdf::<Sha256>::from_prk(prk)
        .map_err(|_| Error::Crypto("HKDF pseudorandom key is shorter than the hash"))?
        .expand(&info, out)
        .map_err(|_| Error::Crypto("HKDF output length out of range"))
}

/// HKDF-Expand-Label returning a freshly allocated `len`-byte secret.
pub fn expand_label_to_vec(
    prk: &[u8], label: &str, context: &[u8], len: usize,
) -> Result<Vec<u8>> {
    let mut out = vec![0u8; len];
    expand_label(prk, label, context, &mut out)?;
    Ok(out)
}

/// Derive the next generation of a traffic secret for a key update
/// (RFC 9001 Section 6.1): `HKDF-Expand-Label(secret, "quic ku", "", Hash.length)`.
pub fn next_secret(secret: &[u8]) -> Result<Vec<u8>> {
    expand_label_to_vec(secret, LABEL_KU, b"", HASH_LEN)
}
