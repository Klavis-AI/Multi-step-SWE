//! Variable-length integers (RFC 9000 Section 16) and a bounds-checked reader.
//!
//! QUIC encodes the length in the top two bits of the first byte, so a varint
//! is 1, 2, 4 or 8 bytes long and carries at most 62 bits. The encoding is not
//! required to be minimal on the wire, which matters when reproducing a
//! captured packet byte for byte: this module decodes whatever length the
//! sender used and only ever *emits* the minimal form.

use crate::{Error, Result};

/// Largest value a QUIC varint can represent (2^62 - 1).
pub const MAX_VARINT: u64 = (1 << 62) - 1;

/// Length in bytes of the minimal encoding of `v`.
///
/// Returns 8 for values above [`MAX_VARINT`]; callers that care should reject
/// such values before encoding.
pub fn varint_len(v: u64) -> usize {
    match v {
        0..=63 => 1,
        64..=16383 => 2,
        16384..=1_073_741_823 => 4,
        _ => 8,
    }
}

/// Decode a varint from the front of `buf`.
///
/// Returns the value and the number of bytes consumed.
pub fn decode_varint(buf: &[u8]) -> Result<(u64, usize)> {
    let first = *buf.first().ok_or(Error::Truncated("varint"))?;

    // Top two bits are log2 of the encoded length.
    let len = 1usize << (first >> 6);
    if buf.len() < len {
        return Err(Error::Truncated("varint"));
    }

    let mut v = u64::from(first & 0x3f);
    for b in &buf[1..len] {
        v = (v << 8) | u64::from(*b);
    }

    Ok((v, len))
}

/// Append the minimal encoding of `v` to `out`.
pub fn encode_varint(v: u64, out: &mut Vec<u8>) -> Result<()> {
    if v > MAX_VARINT {
        return Err(Error::Invalid("varint value (exceeds 2^62-1)"));
    }

    match varint_len(v) {
        1 => out.push(v as u8),
        2 => out.extend_from_slice(&((v as u16) | 0x4000).to_be_bytes()),
        4 => out.extend_from_slice(&((v as u32) | 0x8000_0000).to_be_bytes()),
        _ => out.extend_from_slice(&(v | 0xc000_0000_0000_0000).to_be_bytes()),
    }

    Ok(())
}

/// A forward-only, bounds-checked cursor over a borrowed byte slice.
///
/// Every accessor takes a `what` label that ends up in [`Error::Truncated`],
/// so a driver reporting a parse failure can say *which* field ran out rather
/// than just "short buffer".
#[derive(Clone, Debug)]
pub struct Reader<'a> {
    buf: &'a [u8],
    off: usize,
}

impl<'a> Reader<'a> {
    /// Wrap a slice.
    pub fn new(buf: &'a [u8]) -> Self {
        Reader { buf, off: 0 }
    }

    /// Bytes consumed so far. Used to record field offsets such as the packet
    /// number offset, which header protection needs.
    pub fn offset(&self) -> usize {
        self.off
    }

    /// Bytes left unread.
    pub fn remaining(&self) -> usize {
        self.buf.len() - self.off
    }

    /// Whether the cursor is at the end of the slice.
    pub fn is_empty(&self) -> bool {
        self.remaining() == 0
    }

    /// Read one byte.
    pub fn u8(&mut self, what: &'static str) -> Result<u8> {
        let b = *self.buf.get(self.off).ok_or(Error::Truncated(what))?;
        self.off += 1;
        Ok(b)
    }

    /// Read a big-endian `u32`.
    pub fn u32(&mut self, what: &'static str) -> Result<u32> {
        let b = self.bytes(4, what)?;
        Ok(u32::from_be_bytes([b[0], b[1], b[2], b[3]]))
    }

    /// Borrow the next `n` bytes.
    pub fn bytes(&mut self, n: usize, what: &'static str) -> Result<&'a [u8]> {
        let end = self.off.checked_add(n).ok_or(Error::Truncated(what))?;
        let out = self.buf.get(self.off..end).ok_or(Error::Truncated(what))?;
        self.off = end;
        Ok(out)
    }

    /// Read a varint.
    pub fn varint(&mut self, what: &'static str) -> Result<u64> {
        let (v, len) = decode_varint(&self.buf[self.off..]).map_err(|_| Error::Truncated(what))?;
        self.off += len;
        Ok(v)
    }

    /// Read a varint length followed by that many bytes (the `Token` field of
    /// an Initial packet, a CRYPTO frame body, a CONNECTION_CLOSE reason).
    pub fn varint_prefixed(&mut self, what: &'static str) -> Result<&'a [u8]> {
        let len = self.varint(what)?;
        let len = usize::try_from(len).map_err(|_| Error::Truncated(what))?;
        self.bytes(len, what)
    }

    /// Borrow everything from the cursor to the end, consuming it.
    pub fn rest(&mut self) -> &'a [u8] {
        let out = &self.buf[self.off..];
        self.off = self.buf.len();
        out
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn varint_round_trip() {
        for v in [0u64, 1, 37, 63, 64, 1182, 16383, 16384, 494_878_333, MAX_VARINT] {
            let mut out = Vec::new();
            encode_varint(v, &mut out).unwrap();
            assert_eq!(out.len(), varint_len(v));
            assert_eq!(decode_varint(&out).unwrap(), (v, out.len()));
        }
    }

    #[test]
    fn varint_examples_from_rfc9000_appendix_a1() {
        // The four worked examples in RFC 9000 Appendix A.1.
        assert_eq!(decode_varint(&hexish("c2197c5eff14e88c")).unwrap().0, 151_288_809_941_952_652);
        assert_eq!(decode_varint(&hexish("9d7f3e7d")).unwrap().0, 494_878_333);
        assert_eq!(decode_varint(&hexish("7bbd")).unwrap().0, 15293);
        assert_eq!(decode_varint(&hexish("25")).unwrap().0, 37);
        // ...and the non-minimal two-byte encoding of 37 the RFC also shows.
        assert_eq!(decode_varint(&hexish("4025")).unwrap().0, 37);
    }

    #[test]
    fn varint_rejects_truncation() {
        assert_eq!(decode_varint(&[]).unwrap_err(), Error::Truncated("varint"));
        assert_eq!(decode_varint(&[0x40]).unwrap_err(), Error::Truncated("varint"));
        assert_eq!(encode_varint(MAX_VARINT + 1, &mut Vec::new()).unwrap_err(),
                   Error::Invalid("varint value (exceeds 2^62-1)"));
    }

    #[test]
    fn reader_reports_the_field_that_ran_out() {
        let mut r = Reader::new(&[0x01, 0x02]);
        assert_eq!(r.u8("first").unwrap(), 1);
        assert_eq!(r.bytes(4, "dcid").unwrap_err(), Error::Truncated("dcid"));
    }

    fn hexish(s: &str) -> Vec<u8> {
        (0..s.len() / 2)
            .map(|i| u8::from_str_radix(&s[i * 2..i * 2 + 2], 16).unwrap())
            .collect()
    }
}
