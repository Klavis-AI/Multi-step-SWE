//! Long- and short-header packet parsing, and header protection.

use crate::crypto::{self, PacketKeys, SAMPLE_LEN, TAG_LEN};
use crate::varint::{varint_len, Reader};
use crate::{Error, Result};

/// Maximum length of a connection ID in the versions this crate handles
/// (RFC 9000 Section 17.2). Drafts 23-29 agree with v1 on this.
pub const MAX_CID_LEN: usize = 20;

/// High bit of the first byte: set on long-header packets.
const FORM_BIT: u8 = 0x80;
/// Second bit of the first byte: set on every packet that is not a Version
/// Negotiation packet.
const FIXED_BIT: u8 = 0x40;
/// Low two bits of the first byte: packet number length minus one. Protected.
const PN_LEN_MASK: u8 = 0x03;

/// The four long-header packet types.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum LongPacketType {
    /// Initial packet: carries a token and the first CRYPTO frames.
    Initial,
    /// 0-RTT packet.
    ZeroRtt,
    /// Handshake packet.
    Handshake,
    /// Retry packet: no packet number, no payload, no packet protection.
    Retry,
}

impl LongPacketType {
    /// Decode from the two type bits of a long header's first byte.
    ///
    /// This mapping is stable across v1 and drafts 23-29. (QUIC v2 permutes
    /// it, which is one more reason this crate refuses unknown versions
    /// outright rather than guessing.)
    fn from_first_byte(first: u8) -> Self {
        match (first & 0x30) >> 4 {
            0x00 => LongPacketType::Initial,
            0x01 => LongPacketType::ZeroRtt,
            0x02 => LongPacketType::Handshake,
            _ => LongPacketType::Retry,
        }
    }
}

/// A parsed long header for a packet-number-bearing packet type: Initial,
/// 0-RTT or Handshake.
///
/// The connection ID and token slices borrow from the datagram. All offsets
/// are relative to the start of the *packet*, which is the start of the
/// datagram unless packets are coalesced.
#[derive(Clone, Debug)]
pub struct LongHeader<'a> {
    /// The first byte, still header-protected unless it has been removed.
    pub first_byte: u8,
    /// Initial, 0-RTT or Handshake. Never Retry: see [`RetryPacket`].
    pub ty: LongPacketType,
    /// The 32-bit version.
    pub version: u32,
    /// Destination Connection ID.
    pub dcid: &'a [u8],
    /// Source Connection ID.
    pub scid: &'a [u8],
    /// Address validation token. Empty for anything but an Initial.
    pub token: &'a [u8],
    /// The Length field: packet number bytes plus protected payload bytes,
    /// the latter including the 16-byte AEAD tag.
    pub length: u64,
    /// Offset of the first byte of the packet number field.
    pub pn_offset: usize,
    /// Offset one past the last byte of this packet. Anything after this in
    /// the datagram is a coalesced packet.
    pub packet_end: usize,
}

impl<'a> LongHeader<'a> {
    /// Parse a long header from the start of `buf`.
    ///
    /// Rejects short headers, Version Negotiation packets and Retry packets;
    /// those have their own parsers because they have no packet number and no
    /// Length field. Use [`parse_packet`] when the type is not known up front.
    pub fn parse(buf: &'a [u8]) -> Result<Self> {
        let mut r = Reader::new(buf);

        let first_byte = r.u8("first byte")?;
        if first_byte & FORM_BIT == 0 {
            return Err(Error::Invalid("packet form (expected a long header)"));
        }

        let version = r.u32("version")?;
        if version == 0 {
            return Err(Error::Invalid("version (this is a Version Negotiation packet)"));
        }

        let ty = LongPacketType::from_first_byte(first_byte);
        if ty == LongPacketType::Retry {
            return Err(Error::Invalid("packet type (this is a Retry packet)"));
        }

        let dcid = read_cid(&mut r, "destination connection ID")?;
        let scid = read_cid(&mut r, "source connection ID")?;

        let token: &[u8] = if ty == LongPacketType::Initial {
            r.varint_prefixed("token")?
        } else {
            &[]
        };

        let length = r.varint("length")?;
        let pn_offset = r.offset();

        // A Length that overruns the datagram is a framing error, not a
        // decryption failure; say so precisely so a driver can tell them apart.
        let packet_end = usize::try_from(length)
            .ok()
            .and_then(|l| pn_offset.checked_add(l))
            .ok_or(Error::Invalid("length field"))?;
        if packet_end > buf.len() {
            return Err(Error::Truncated("packet payload"));
        }

        // The sample for header protection starts 4 bytes into the packet
        // number field, so a packet with fewer than pn(4) + 16 bytes left
        // cannot be unprotected at all (RFC 9001 Section 5.4.2).
        if packet_end < pn_offset + 4 + SAMPLE_LEN {
            return Err(Error::Invalid("payload (too short to sample for header protection)"));
        }

        Ok(LongHeader { first_byte, ty, version, dcid, scid, token, length, pn_offset, packet_end })
    }

    /// Length of the packet number field, valid only after header protection
    /// has been removed (the two bits encoding it are themselves protected).
    pub fn packet_number_len(&self) -> usize {
        packet_number_len(self.first_byte)
    }

    /// The header bytes that are the AEAD's associated data: everything from
    /// the first byte through the packet number, inclusive.
    ///
    /// `pkt` must be the packet with header protection already removed.
    pub fn associated_data<'b>(&self, pkt: &'b [u8]) -> Result<&'b [u8]> {
        let end = self.pn_offset + packet_number_len(pkt.first().copied().unwrap_or(0));
        pkt.get(..end).ok_or(Error::Truncated("packet number"))
    }
}

/// A parsed Retry packet.
///
/// Retry is the one long-header type with no packet number and no packet
/// protection; everything after the connection IDs is the token, except for
/// the trailing Retry Integrity Tag in the versions that have one.
#[derive(Clone, Debug)]
pub struct RetryPacket<'a> {
    /// The first byte.
    pub first_byte: u8,
    /// The 32-bit version.
    pub version: u32,
    /// Destination Connection ID (the client's Source Connection ID).
    pub dcid: &'a [u8],
    /// Source Connection ID, which the client must use as its new DCID — and
    /// which becomes the input to the Initial key schedule for everything the
    /// client sends afterwards.
    pub scid: &'a [u8],
    /// Original Destination Connection ID, carried inline by drafts 23 and 24
    /// only. Later versions authenticate it through the integrity tag instead
    /// of transmitting it.
    pub odcid: Option<&'a [u8]>,
    /// The address validation token.
    pub token: &'a [u8],
    /// The Retry Integrity Tag, absent in drafts 23 and 24.
    pub integrity_tag: Option<[u8; TAG_LEN]>,
}

impl<'a> RetryPacket<'a> {
    /// Parse a Retry packet. The layout depends on the version, so it must be
    /// supplied rather than inferred.
    ///
    /// Drafts 23 and 24 put an ODCID Length byte and the Original Destination
    /// Connection ID after the Source Connection ID and have no integrity tag
    /// (see `git show 0.1.0:src/packet.rs` in the upstream quiche clone).
    /// draft-25 replaced both with the 16-byte tag (`git show
    /// 0.3.0:src/packet.rs`, which strips `aead::AES_128_GCM.tag_len()` bytes
    /// off the end of the token).
    pub fn parse(buf: &'a [u8], version: u32) -> Result<Self> {
        let mut r = Reader::new(buf);

        let first_byte = r.u8("first byte")?;
        if first_byte & FORM_BIT == 0 {
            return Err(Error::Invalid("packet form (expected a long header)"));
        }
        if LongPacketType::from_first_byte(first_byte) != LongPacketType::Retry {
            return Err(Error::Invalid("packet type (expected a Retry packet)"));
        }

        let found = r.u32("version")?;
        if found != version {
            return Err(Error::VersionMismatch { expected: version, actual: found });
        }

        let dcid = read_cid(&mut r, "destination connection ID")?;
        let scid = read_cid(&mut r, "source connection ID")?;

        let has_odcid = matches!(version, crate::VERSION_DRAFT23 | crate::VERSION_DRAFT24);
        let odcid = if has_odcid {
            Some(read_cid(&mut r, "original destination connection ID")?)
        } else {
            None
        };

        let rest = r.rest();

        let (token, integrity_tag) = if has_odcid {
            (rest, None)
        } else {
            if rest.len() < TAG_LEN {
                return Err(Error::Truncated("retry integrity tag"));
            }
            let (token, tag) = rest.split_at(rest.len() - TAG_LEN);
            let mut buf = [0u8; TAG_LEN];
            buf.copy_from_slice(tag);
            (token, Some(buf))
        };

        Ok(RetryPacket { first_byte, version, dcid, scid, odcid, token, integrity_tag })
    }
}

/// Verify a Retry packet's integrity tag against the Original Destination
/// Connection ID (RFC 9001 Section 5.8).
///
/// `packet` is the whole Retry packet including the trailing tag. Returns
/// [`Error::DecryptionFailed`] if the tag does not match.
pub fn verify_retry_integrity(packet: &[u8], version: u32, odcid: &[u8]) -> Result<()> {
    if packet.len() < TAG_LEN {
        return Err(Error::Truncated("retry integrity tag"));
    }

    let (pseudo, tag) = packet.split_at(packet.len() - TAG_LEN);
    let expected = crypto::retry_integrity_tag(version, odcid, pseudo)?;

    // Not constant time; the inputs are public and the verifier is not a
    // decryption oracle for anyone.
    if expected != tag {
        return Err(Error::DecryptionFailed);
    }

    Ok(())
}

/// A parsed Version Negotiation packet.
///
/// The giveaway is a zero Version field, not the type bits: a VN packet's
/// first byte is otherwise unconstrained and its type bits mean nothing.
#[derive(Clone, Debug)]
pub struct VersionNegotiationPacket<'a> {
    /// Destination Connection ID, echoing the client's Source Connection ID.
    pub dcid: &'a [u8],
    /// Source Connection ID, echoing the client's Destination Connection ID.
    pub scid: &'a [u8],
    /// The versions the server claims to support, in the order sent. May
    /// include a deliberate "greasing" value that is not a real version.
    pub versions: Vec<u32>,
}

impl<'a> VersionNegotiationPacket<'a> {
    /// Parse a Version Negotiation packet.
    pub fn parse(buf: &'a [u8]) -> Result<Self> {
        let mut r = Reader::new(buf);

        let first_byte = r.u8("first byte")?;
        if first_byte & FORM_BIT == 0 {
            return Err(Error::Invalid("packet form (expected a long header)"));
        }

        if r.u32("version")? != 0 {
            return Err(Error::Invalid("version (not a Version Negotiation packet)"));
        }

        let dcid = read_cid(&mut r, "destination connection ID")?;
        let scid = read_cid(&mut r, "source connection ID")?;

        if r.remaining() == 0 || r.remaining() % 4 != 0 {
            return Err(Error::Invalid("supported version list length"));
        }

        let mut versions = Vec::with_capacity(r.remaining() / 4);
        while !r.is_empty() {
            versions.push(r.u32("supported version")?);
        }

        Ok(VersionNegotiationPacket { dcid, scid, versions })
    }
}

/// A parsed short header (a 1-RTT packet).
#[derive(Clone, Debug)]
pub struct ShortHeader<'a> {
    /// The first byte, still header-protected unless it has been removed.
    pub first_byte: u8,
    /// Destination Connection ID.
    pub dcid: &'a [u8],
    /// Offset of the first byte of the packet number field.
    pub pn_offset: usize,
}

impl<'a> ShortHeader<'a> {
    /// Parse a short header.
    ///
    /// A short header does not encode its connection ID length; the receiver
    /// is expected to know it because it chose the connection ID itself. A
    /// verifier that sent the connection ID knows it too, so it is a parameter
    /// here rather than a guess.
    pub fn parse(buf: &'a [u8], dcid_len: usize) -> Result<Self> {
        if dcid_len > MAX_CID_LEN {
            return Err(Error::Invalid("connection ID (longer than 20 bytes)"));
        }

        let mut r = Reader::new(buf);
        let first_byte = r.u8("first byte")?;
        if first_byte & FORM_BIT != 0 {
            return Err(Error::Invalid("packet form (expected a short header)"));
        }

        let dcid = r.bytes(dcid_len, "destination connection ID")?;
        let pn_offset = r.offset();

        if buf.len() < pn_offset + 4 + SAMPLE_LEN {
            return Err(Error::Invalid("payload (too short to sample for header protection)"));
        }

        Ok(ShortHeader { first_byte, dcid, pn_offset })
    }

    /// The key phase bit. Only meaningful once header protection is removed.
    pub fn key_phase(&self) -> bool {
        self.first_byte & 0x04 != 0
    }

    /// The latency spin bit. Not protected, so readable either way.
    pub fn spin(&self) -> bool {
        self.first_byte & 0x20 != 0
    }
}

/// Any packet this crate can identify from the wire.
#[derive(Clone, Debug)]
pub enum Packet<'a> {
    /// Initial, 0-RTT or Handshake.
    Long(LongHeader<'a>),
    /// Retry.
    Retry(RetryPacket<'a>),
    /// Version Negotiation.
    VersionNegotiation(VersionNegotiationPacket<'a>),
    /// 1-RTT.
    Short(ShortHeader<'a>),
}

/// Identify and parse whatever is at the start of `buf`.
///
/// `short_dcid_len` is used only if the packet turns out to have a short
/// header, where the length is not on the wire.
pub fn parse_packet(buf: &[u8], short_dcid_len: usize) -> Result<Packet<'_>> {
    let first = *buf.first().ok_or(Error::Truncated("first byte"))?;

    if first & FORM_BIT == 0 {
        return Ok(Packet::Short(ShortHeader::parse(buf, short_dcid_len)?));
    }

    let version = peek_version(buf)?;
    if version == 0 {
        return Ok(Packet::VersionNegotiation(VersionNegotiationPacket::parse(buf)?));
    }

    if LongPacketType::from_first_byte(first) == LongPacketType::Retry {
        return Ok(Packet::Retry(RetryPacket::parse(buf, version)?));
    }

    Ok(Packet::Long(LongHeader::parse(buf)?))
}

/// Read the version field of a long-header packet without parsing further.
pub fn peek_version(buf: &[u8]) -> Result<u32> {
    let mut r = Reader::new(buf);
    let first = r.u8("first byte")?;
    if first & FORM_BIT == 0 {
        return Err(Error::Invalid("packet form (a short header has no version)"));
    }
    r.u32("version")
}

/// Whether the first byte says this is a long-header packet.
pub fn is_long_header(first_byte: u8) -> bool {
    first_byte & FORM_BIT != 0
}

/// Whether the Fixed Bit is set. Clear means either a Version Negotiation
/// packet or a protocol violation.
pub fn has_fixed_bit(first_byte: u8) -> bool {
    first_byte & FIXED_BIT != 0
}

/// Length of the packet number field encoded in the low two bits of an
/// unprotected first byte: 1 to 4 bytes.
pub fn packet_number_len(first_byte: u8) -> usize {
    (first_byte & PN_LEN_MASK) as usize + 1
}

/// The packet number field of a packet whose header protection was removed.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PacketNumberField {
    /// Offset of the field within the packet.
    pub offset: usize,
    /// Its length in bytes, 1 to 4.
    pub len: usize,
    /// The truncated value as it appears on the wire. Expand it with
    /// [`decode_packet_number`] before using it as an AEAD nonce input.
    pub truncated: u64,
}

impl PacketNumberField {
    /// Offset one past the packet number, i.e. where the ciphertext starts and
    /// where the AEAD's associated data ends.
    pub fn end(&self) -> usize {
        self.offset + self.len
    }
}

/// Borrow the 16 bytes of ciphertext sampled for header protection.
///
/// The sample always starts 4 bytes after the start of the packet number
/// field, because the receiver does not yet know how long that field is: the
/// two bits saying so are themselves protected (RFC 9001 Section 5.4.2).
pub fn header_protection_sample(pkt: &[u8], pn_offset: usize) -> Result<&[u8]> {
    let start = pn_offset + 4;
    pkt.get(start..start + SAMPLE_LEN)
        .ok_or(Error::Truncated("header protection sample"))
}

/// Mask applied to the low bits of the first byte.
///
/// Long headers protect 4 bits (reserved bits and packet number length); short
/// headers protect 5 (reserved bits, key phase and packet number length). The
/// form bit itself is never protected, which is what makes it safe to decide
/// between the two by looking at the packet before unmasking it.
fn first_byte_mask(first_byte: u8) -> u8 {
    if is_long_header(first_byte) {
        0x0f
    } else {
        0x1f
    }
}

/// Remove header protection in place.
///
/// Takes the sample from the ciphertext, unmasks the first byte, learns the
/// packet number length from the bits that just became readable, and unmasks
/// exactly that many packet number bytes.
pub fn remove_header_protection(
    keys: &PacketKeys, pkt: &mut [u8], pn_offset: usize,
) -> Result<PacketNumberField> {
    let mask = keys.header_mask(header_protection_sample(pkt, pn_offset)?)?;

    let first = *pkt.first().ok_or(Error::Truncated("first byte"))?;
    pkt[0] = first ^ (mask[0] & first_byte_mask(first));

    let pn_len = packet_number_len(pkt[0]);

    let mut truncated: u64 = 0;
    for i in 0..pn_len {
        let off = pn_offset + i;
        let b = *pkt.get(off).ok_or(Error::Truncated("packet number"))?;
        pkt[off] = b ^ mask[1 + i];
        truncated = (truncated << 8) | u64::from(pkt[off]);
    }

    Ok(PacketNumberField { offset: pn_offset, len: pn_len, truncated })
}

/// Apply header protection in place, the inverse of
/// [`remove_header_protection`].
///
/// `pn_len` must be the value already encoded in the first byte's low bits;
/// it is passed explicitly because after this call those bits are masked and
/// the information is no longer recoverable from the packet.
pub fn apply_header_protection(
    keys: &PacketKeys, pkt: &mut [u8], pn_offset: usize, pn_len: usize,
) -> Result<()> {
    if !(1..=4).contains(&pn_len) {
        return Err(Error::Invalid("packet number length"));
    }
    if packet_number_len(*pkt.first().ok_or(Error::Truncated("first byte"))?) != pn_len {
        return Err(Error::Invalid("packet number length (disagrees with the first byte)"));
    }

    // The sample is taken from the ciphertext, which this function does not
    // touch, so sampling before or after masking the header makes no
    // difference. Sampling first keeps this symmetric with removal.
    let mask = keys.header_mask(header_protection_sample(pkt, pn_offset)?)?;

    pkt[0] ^= mask[0] & first_byte_mask(pkt[0]);

    for i in 0..pn_len {
        let off = pn_offset + i;
        let b = pkt.get_mut(off).ok_or(Error::Truncated("packet number"))?;
        *b ^= mask[1 + i];
    }

    Ok(())
}

/// Reconstruct a full packet number from its truncated on-wire form
/// (RFC 9000 Appendix A.3).
///
/// `largest_acked` is the largest packet number already processed in this
/// packet number space, or `None` before any. The result is the value nearest
/// to `largest_acked + 1` that has the given low bytes.
pub fn decode_packet_number(largest_acked: Option<u64>, truncated: u64, pn_len: usize) -> u64 {
    let expected = largest_acked.map_or(0, |l| l + 1);
    let win = 1u64 << (pn_len * 8);
    let hwin = win / 2;
    let mask = win - 1;

    let candidate = (expected & !mask) | truncated;

    if candidate + hwin <= expected && candidate + win < (1 << 62) {
        candidate + win
    } else if candidate > expected + hwin && candidate >= win {
        candidate - win
    } else {
        candidate
    }
}

/// Encode a packet number into `pn_len` big-endian bytes, dropping the high
/// bytes the peer is expected to infer.
pub(crate) fn encode_packet_number(pn: u64, pn_len: usize, out: &mut Vec<u8>) -> Result<()> {
    if !(1..=4).contains(&pn_len) {
        return Err(Error::Invalid("packet number length"));
    }
    out.extend_from_slice(&pn.to_be_bytes()[8 - pn_len..]);
    Ok(())
}

/// Size in bytes of a long header for the given fields, packet number included.
pub(crate) fn long_header_len(
    dcid_len: usize, scid_len: usize, token_len: usize, length_varint_len: usize, pn_len: usize,
) -> usize {
    1 + 4
        + 1
        + dcid_len
        + 1
        + scid_len
        + varint_len(token_len as u64)
        + token_len
        + length_varint_len
        + pn_len
}

fn read_cid<'a>(r: &mut Reader<'a>, what: &'static str) -> Result<&'a [u8]> {
    let len = r.u8(what)? as usize;
    if len > MAX_CID_LEN {
        return Err(Error::Invalid("connection ID (longer than 20 bytes)"));
    }
    r.bytes(len, what)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn packet_number_decoding_matches_rfc9000_appendix_a3() {
        // The worked example: largest acked 0xa82f30ea, truncated 0x9b32 on
        // two bytes, expected result 0xa82f9b32.
        assert_eq!(decode_packet_number(Some(0xa82f_30ea), 0x9b32, 2), 0xa82f_9b32);
        // First packet of a space, nothing acked yet.
        assert_eq!(decode_packet_number(None, 0, 4), 0);
        assert_eq!(decode_packet_number(None, 2, 4), 2);
        // The A.5 vector's packet number, 3 bytes of 0x00bff4.
        assert_eq!(decode_packet_number(Some(654_360_563), 0x00bf_f4, 3), 654_360_564);
    }

    #[test]
    fn version_negotiation_round_trip() {
        let mut pkt = vec![0x80 | 0x7f];
        pkt.extend_from_slice(&0u32.to_be_bytes());
        pkt.push(4);
        pkt.extend_from_slice(&[1, 2, 3, 4]);
        pkt.push(2);
        pkt.extend_from_slice(&[9, 9]);
        pkt.extend_from_slice(&0x0000_0001u32.to_be_bytes());
        pkt.extend_from_slice(&0xff00_001du32.to_be_bytes());

        let vn = match parse_packet(&pkt, 0).unwrap() {
            Packet::VersionNegotiation(vn) => vn,
            other => panic!("expected a Version Negotiation packet, got {other:?}"),
        };
        assert_eq!(vn.dcid, &[1, 2, 3, 4]);
        assert_eq!(vn.scid, &[9, 9]);
        assert_eq!(vn.versions, vec![0x0000_0001, 0xff00_001d]);

        // A trailing partial version is a framing error.
        pkt.push(0);
        assert_eq!(
            VersionNegotiationPacket::parse(&pkt).unwrap_err(),
            Error::Invalid("supported version list length")
        );
    }

    #[test]
    fn oversized_connection_id_is_rejected() {
        let mut pkt = vec![0xc3];
        pkt.extend_from_slice(&1u32.to_be_bytes());
        pkt.push(21);
        pkt.extend_from_slice(&[0; 21]);
        assert_eq!(
            LongHeader::parse(&pkt).unwrap_err(),
            Error::Invalid("connection ID (longer than 20 bytes)")
        );
    }

    #[test]
    fn truncated_datagram_is_truncated_not_a_panic() {
        for cut in 0..24 {
            let mut pkt = vec![0xc3];
            pkt.extend_from_slice(&1u32.to_be_bytes());
            pkt.push(0);
            pkt.push(0);
            pkt.push(0);
            pkt.extend_from_slice(&[0x44, 0x9e]);
            pkt.truncate(cut);
            // Whatever it decides, it must not panic and must not succeed.
            assert!(LongHeader::parse(&pkt).is_err(), "cut at {cut}");
        }
    }
}
