//! The two things a driver actually calls: decrypt a captured Initial packet,
//! and build a protected one to send.

use crate::crypto::{derive_initial_keys, SAMPLE_LEN, TAG_LEN};
use crate::frame::{frames, CryptoAssembler, Frames};
use crate::packet::{
    apply_header_protection, decode_packet_number, encode_packet_number, long_header_len,
    remove_header_protection, LongHeader, LongPacketType, MAX_CID_LEN,
};
use crate::varint::{encode_varint, varint_len};
use crate::{Error, Result};

/// Which endpoint *sent* the packet being examined.
///
/// Named after the sender rather than the observer on purpose. "Server side"
/// is ambiguous in a verifier that is sometimes the client and sometimes just
/// a bystander with a packet capture; "who wrote these bytes" is not.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Perspective {
    /// The packet was sent by the client, so it is protected with the client
    /// Initial keys.
    Client,
    /// The packet was sent by the server, so it is protected with the server
    /// Initial keys.
    Server,
}

/// A successfully decrypted Initial packet.
#[derive(Clone, Debug)]
pub struct DecryptedInitial {
    /// Version from the long header.
    pub version: u32,
    /// Destination Connection ID as it appears in this packet.
    pub dcid: Vec<u8>,
    /// Source Connection ID as it appears in this packet.
    pub scid: Vec<u8>,
    /// The address validation token; empty unless the client is replying to a
    /// Retry or using a NEW_TOKEN.
    pub token: Vec<u8>,
    /// The full reconstructed packet number.
    pub packet_number: u64,
    /// Length of the packet number field on the wire, 1 to 4 bytes.
    pub packet_number_len: usize,
    /// The unprotected header, which is also the AEAD's associated data.
    pub header: Vec<u8>,
    /// The decrypted payload: a sequence of frames, PADDING included.
    pub payload: Vec<u8>,
    /// Length of this packet within the datagram. Bytes after it are a
    /// coalesced packet (typically a Handshake packet) and can be handed to
    /// another parse.
    pub packet_len: usize,
}

impl DecryptedInitial {
    /// Iterate the frames of the decrypted payload.
    pub fn frames(&self) -> Frames<'_> {
        frames(&self.payload)
    }

    /// Reassemble the CRYPTO frames of this packet into a contiguous byte
    /// stream starting at offset 0.
    ///
    /// A ClientHello larger than one packet will come back short; use a
    /// [`CryptoAssembler`] across packets for that case.
    pub fn crypto_data(&self) -> Result<Vec<u8>> {
        let mut assembler = CryptoAssembler::new();
        assembler.absorb(&self.payload)?;
        Ok(assembler.contiguous())
    }
}

/// Decrypt an Initial packet, deriving keys from the packet's own Destination
/// Connection ID.
///
/// Correct for a client's first Initial, which is the interesting case for a
/// verifier: that DCID *is* the original one. It is wrong for a server Initial
/// and for a client Initial sent after a Retry — in both cases the DCID on the
/// wire is no longer the value the Initial keys came from, so use
/// [`decrypt_initial_with_odcid`] and pass the original explicitly.
pub fn decrypt_initial(
    datagram: &[u8], version: u32, sender: Perspective,
) -> Result<DecryptedInitial> {
    let hdr = LongHeader::parse(datagram)?;
    let odcid = hdr.dcid.to_vec();
    decrypt_initial_with_odcid(datagram, version, sender, &odcid)
}

/// Decrypt an Initial packet using an explicitly supplied original DCID.
///
/// `odcid` is the Destination Connection ID from the client's first Initial
/// packet — or, after a Retry, the Source Connection ID the server put in the
/// Retry. Both endpoints protect Initial packets with keys derived from that
/// one value for the whole handshake, in both directions, no matter what
/// connection IDs they subsequently choose (RFC 9001 Section 5.2). Getting
/// this wrong on the server's first flight is the classic way to conclude that
/// a working implementation is broken.
pub fn decrypt_initial_with_odcid(
    datagram: &[u8], version: u32, sender: Perspective, odcid: &[u8],
) -> Result<DecryptedInitial> {
    let hdr = LongHeader::parse(datagram)?;

    if hdr.version != version {
        return Err(Error::VersionMismatch { expected: version, actual: hdr.version });
    }
    if hdr.ty != LongPacketType::Initial {
        return Err(Error::Invalid("packet type (expected an Initial packet)"));
    }

    let keys = derive_initial_keys(version, odcid)?;
    let keys = match sender {
        Perspective::Client => keys.client,
        Perspective::Server => keys.server,
    };

    // Work on a copy: header protection is removed in place, and a driver's
    // captured datagram should stay as it arrived for logging.
    let mut pkt = datagram[..hdr.packet_end].to_vec();

    let pn = remove_header_protection(&keys, &mut pkt, hdr.pn_offset)?;

    // Initial packet numbers start at 0 and a first flight never gets near
    // 2^(8*pn_len - 1), so there is nothing to disambiguate against here.
    // `decode_packet_number` is public for the cases that do.
    let packet_number = decode_packet_number(None, pn.truncated, pn.len);

    let header = pkt.get(..pn.end()).ok_or(Error::Truncated("packet number"))?.to_vec();
    let ciphertext = pkt.get(pn.end()..hdr.packet_end).ok_or(Error::Truncated("payload"))?;

    let payload = keys.open(packet_number, &header, ciphertext)?;

    Ok(DecryptedInitial {
        version: hdr.version,
        dcid: hdr.dcid.to_vec(),
        scid: hdr.scid.to_vec(),
        token: hdr.token.to_vec(),
        packet_number,
        packet_number_len: pn.len,
        header,
        payload,
        packet_len: hdr.packet_end,
    })
}

/// Decrypt an Initial packet whose version is whatever the packet says.
///
/// Convenience for a driver that captured a flight without knowing which
/// version the candidate chose. Still fails loudly, via
/// [`Error::UnknownVersion`], if the version has no salt in the table.
pub fn decrypt_initial_any_version(
    datagram: &[u8], sender: Perspective,
) -> Result<DecryptedInitial> {
    let version = LongHeader::parse(datagram)?.version;
    decrypt_initial(datagram, version, sender)
}

/// A client Initial packet to build and send.
///
/// The outbound direction: a driver constructs one of these around a
/// caller-supplied CRYPTO payload (a ClientHello, or deliberate nonsense) and
/// requires a specific answer from the candidate.
#[derive(Clone, Debug)]
pub struct ClientInitial<'a> {
    /// QUIC version for the long header. Must have an entry in the salt table.
    pub version: u32,
    /// Destination Connection ID. The Initial keys are derived from this, so
    /// the peer will derive the same ones from the packet it receives.
    pub dcid: &'a [u8],
    /// Source Connection ID.
    pub scid: &'a [u8],
    /// Address validation token; empty unless replying to a Retry.
    pub token: &'a [u8],
    /// Packet number.
    pub packet_number: u64,
    /// How many bytes to encode the packet number in, 1 to 4.
    pub packet_number_len: usize,
    /// Offset of `crypto_data` within the crypto stream.
    pub crypto_offset: u64,
    /// The CRYPTO frame payload. Empty means no CRYPTO frame at all.
    pub crypto_data: &'a [u8],
    /// Pad the datagram to exactly this many bytes with PADDING frames inside
    /// the packet payload. RFC 9000 Section 14.1 requires a client Initial to
    /// be at least 1200 bytes or a compliant server will drop it. Zero means
    /// no padding.
    pub datagram_len: usize,
}

impl<'a> ClientInitial<'a> {
    /// A client Initial with the defaults a real client would use: a 4-byte
    /// packet number 0, no token, and padding to 1200 bytes.
    pub fn new(version: u32, dcid: &'a [u8], scid: &'a [u8], crypto_data: &'a [u8]) -> Self {
        ClientInitial {
            version,
            dcid,
            scid,
            token: &[],
            packet_number: 0,
            packet_number_len: 4,
            crypto_offset: 0,
            crypto_data,
            datagram_len: 1200,
        }
    }

    /// Serialise, encrypt and header-protect the packet.
    pub fn build(&self) -> Result<Vec<u8>> {
        if self.dcid.len() > MAX_CID_LEN || self.scid.len() > MAX_CID_LEN {
            return Err(Error::Invalid("connection ID (longer than 20 bytes)"));
        }
        if !(1..=4).contains(&self.packet_number_len) {
            return Err(Error::Invalid("packet number length"));
        }

        let keys = derive_initial_keys(self.version, self.dcid)?.client;

        let mut payload = Vec::new();
        if !self.crypto_data.is_empty() {
            payload.push(0x06);
            encode_varint(self.crypto_offset, &mut payload)?;
            encode_varint(self.crypto_data.len() as u64, &mut payload)?;
            payload.extend_from_slice(self.crypto_data);
        }

        let (length_varint_len, padding) = self.plan_padding(payload.len())?;
        payload.resize(payload.len() + padding, 0x00);

        // The Length field covers the packet number and the protected payload,
        // tag included.
        let length = (self.packet_number_len + payload.len() + TAG_LEN) as u64;
        if varint_len(length) != length_varint_len {
            return Err(Error::Invalid("length field (padding plan is inconsistent)"));
        }

        let mut pkt = Vec::with_capacity(
            long_header_len(
                self.dcid.len(),
                self.scid.len(),
                self.token.len(),
                length_varint_len,
                self.packet_number_len,
            ) + payload.len()
                + TAG_LEN,
        );

        // Long header, Initial, fixed bit set, packet number length in the low
        // two bits. They are masked by header protection a few lines below.
        pkt.push(0xc0 | (self.packet_number_len as u8 - 1));
        pkt.extend_from_slice(&self.version.to_be_bytes());
        pkt.push(self.dcid.len() as u8);
        pkt.extend_from_slice(self.dcid);
        pkt.push(self.scid.len() as u8);
        pkt.extend_from_slice(self.scid);
        encode_varint(self.token.len() as u64, &mut pkt)?;
        pkt.extend_from_slice(self.token);
        encode_varint(length, &mut pkt)?;

        let pn_offset = pkt.len();
        encode_packet_number(self.packet_number, self.packet_number_len, &mut pkt)?;

        let ciphertext = keys.seal(self.packet_number, &pkt, &payload)?;
        pkt.extend_from_slice(&ciphertext);

        apply_header_protection(&keys, &mut pkt, pn_offset, self.packet_number_len)?;

        Ok(pkt)
    }

    /// Decide how many bytes of the Length field and how much PADDING to use.
    ///
    /// These are mutually dependent — padding changes the Length value, which
    /// can change the size of its own varint — so try each varint size and
    /// keep the one that is self-consistent.
    fn plan_padding(&self, payload_len: usize) -> Result<(usize, usize)> {
        for length_varint_len in [1usize, 2, 4, 8] {
            let overhead = long_header_len(
                self.dcid.len(),
                self.scid.len(),
                self.token.len(),
                length_varint_len,
                self.packet_number_len,
            ) + TAG_LEN;

            let unpadded = overhead + payload_len;
            let padding = if self.datagram_len == 0 {
                0
            } else if self.datagram_len >= unpadded {
                self.datagram_len - unpadded
            } else {
                return Err(Error::Invalid("datagram_len (smaller than the packet it must hold)"));
            };

            let length = (self.packet_number_len + payload_len + padding + TAG_LEN) as u64;
            if varint_len(length) != length_varint_len {
                continue;
            }

            // RFC 9001 Section 5.4.2: the packet number plus the protected
            // payload must leave 4 bytes plus a full 16-byte sample.
            if self.packet_number_len + payload_len + padding + TAG_LEN < 4 + SAMPLE_LEN {
                return Err(Error::Invalid("payload (too short to sample for header protection)"));
            }

            return Ok((length_varint_len, padding));
        }

        Err(Error::Invalid("length field (no self-consistent encoding)"))
    }
}

/// Build a client Initial and decrypt it again: proves the two directions of
/// this crate agree with each other, independently of the RFC vectors.
#[cfg(test)]
mod tests {
    use super::*;
    use crate::frame::Frame;
    use crate::VERSION_V1;

    #[test]
    fn build_then_decrypt_round_trip() {
        let dcid = [0x83, 0x94, 0xc8, 0xf0, 0x3e, 0x51, 0x57, 0x08];
        let scid = [0xde, 0xad, 0xbe, 0xef];
        let crypto = b"not really a ClientHello".to_vec();

        let datagram = ClientInitial::new(VERSION_V1, &dcid, &scid, &crypto).build().unwrap();
        assert_eq!(datagram.len(), 1200);

        let got = decrypt_initial(&datagram, VERSION_V1, Perspective::Client).unwrap();
        assert_eq!(got.packet_number, 0);
        assert_eq!(got.dcid, dcid);
        assert_eq!(got.scid, scid);
        assert_eq!(got.crypto_data().unwrap(), crypto);
        assert_eq!(got.packet_len, 1200);

        let frames: Vec<_> = got.frames().collect::<Result<Vec<_>>>().unwrap();
        assert!(matches!(frames[0], Frame::Crypto { offset: 0, .. }));
        assert!(matches!(frames[1], Frame::Padding { .. }));
    }

    #[test]
    fn short_packet_numbers_and_no_padding_work() {
        let dcid = [1, 2, 3, 4];
        for pn_len in 1..=4 {
            let mut ci = ClientInitial::new(VERSION_V1, &dcid, &[], b"hello");
            ci.packet_number = 7;
            ci.packet_number_len = pn_len;
            ci.datagram_len = 0;

            let datagram = ci.build().unwrap();
            let got = decrypt_initial(&datagram, VERSION_V1, Perspective::Client).unwrap();
            assert_eq!(got.packet_number, 7);
            assert_eq!(got.packet_number_len, pn_len);
            assert_eq!(got.crypto_data().unwrap(), b"hello");
        }
    }

    #[test]
    fn the_wrong_keys_fail_to_open() {
        let dcid = [1, 2, 3, 4];
        let datagram = ClientInitial::new(VERSION_V1, &dcid, &[], b"hello").build().unwrap();

        // Right keys, wrong direction.
        assert_eq!(
            decrypt_initial(&datagram, VERSION_V1, Perspective::Server).unwrap_err(),
            Error::DecryptionFailed
        );

        // Right direction, wrong original DCID.
        assert_eq!(
            decrypt_initial_with_odcid(&datagram, VERSION_V1, Perspective::Client, &[9, 9, 9, 9])
                .unwrap_err(),
            Error::DecryptionFailed
        );

        // Right everything, one flipped ciphertext bit.
        let mut tampered = datagram.clone();
        let last = tampered.len() - 1;
        tampered[last] ^= 0x01;
        assert_eq!(
            decrypt_initial(&tampered, VERSION_V1, Perspective::Client).unwrap_err(),
            Error::DecryptionFailed
        );
    }

    #[test]
    fn version_disagreements_are_reported_as_such() {
        let dcid = [1, 2, 3, 4];
        let datagram = ClientInitial::new(VERSION_V1, &dcid, &[], b"hello").build().unwrap();

        assert_eq!(
            decrypt_initial(&datagram, crate::VERSION_DRAFT29, Perspective::Client).unwrap_err(),
            Error::VersionMismatch { expected: crate::VERSION_DRAFT29, actual: VERSION_V1 }
        );

        let mut ci = ClientInitial::new(0x0000_0002, &dcid, &[], b"hello");
        ci.datagram_len = 0;
        assert_eq!(ci.build().unwrap_err(), Error::UnknownVersion(0x0000_0002));
    }

    #[test]
    fn a_datagram_budget_that_cannot_be_met_is_an_error() {
        let dcid = [1, 2, 3, 4];
        let mut ci = ClientInitial::new(VERSION_V1, &dcid, &[], &[0u8; 2000]);
        ci.datagram_len = 1200;
        assert_eq!(
            ci.build().unwrap_err(),
            Error::Invalid("datagram_len (smaller than the packet it must hold)")
        );
    }
}
