//! Group `bytesrw`: the `tor-bytes` reader and writer.
//!
//! INHERITED. `Reader`, `Writer` and their length-counted nesting helpers are
//! unchanged at this milestone (`tor-bytes/src/reader.rs` and `writer.rs` are
//! byte-identical to the previous checkpoint); what this milestone adds is
//! `SecretBuf` beside them. Every cell this step's padding work encodes goes
//! through these two traits, so they are graded here as a regression guard:
//! all multi-byte integers are big-endian, a short read fails without
//! consuming, and a nested item is bounded by its own length field.
#![allow(non_snake_case)]

use tor_bytes::{Error, Reader, Writer};

/// Integers are read big-endian, and each read advances by its own width.
#[test]
fn bytesrw__take_integers_are_big_endian() {
    let bytes = [
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    ];
    let mut r = Reader::from_slice(&bytes);
    assert_eq!(r.total_len(), 15);
    assert_eq!(r.take_u8().expect("1 byte"), 0x01);
    assert_eq!(r.consumed(), 1);
    assert_eq!(r.take_u16().expect("2 bytes"), 0x0203);
    assert_eq!(r.consumed(), 3);
    assert_eq!(r.take_u32().expect("4 bytes"), 0x0405_0607);
    assert_eq!(r.consumed(), 7);
    assert_eq!(r.take_u64().expect("8 bytes"), 0x0809_0a0b_0c0d_0e0f);
    assert_eq!(r.remaining(), 0);
    r.should_be_exhausted().expect("all 15 bytes were consumed");
}

/// A read that would run off the end fails as `Truncated` and leaves the
/// position alone, so a caller can retry once more bytes arrive.
#[test]
fn bytesrw__short_read_is_truncated_and_consumes_nothing() {
    let mut r = Reader::from_slice(&[0xaa, 0xbb, 0xcc]);
    assert_eq!(r.take_u8().expect("1 byte"), 0xaa);
    assert_eq!(r.take_u32().unwrap_err(), Error::Truncated);
    assert_eq!(r.remaining(), 2, "a failed read must not consume");
    assert_eq!(r.take_u16().expect("the two remaining bytes"), 0xbbcc);

    assert_eq!(
        Reader::from_slice(&[]).take(1).unwrap_err(),
        Error::Truncated
    );
}

/// Leftover input is an error in its own right: a cell body that decodes but
/// has trailing bytes is malformed.
#[test]
fn bytesrw__should_be_exhausted_detects_extra_bytes() {
    let mut r = Reader::from_slice(&[0x00, 0x01, 0xff]);
    let _ = r.take_u16().expect("2 bytes");
    assert_eq!(r.remaining(), 1);
    assert_eq!(
        r.should_be_exhausted().unwrap_err(),
        Error::ExtraneousBytes
    );
    assert_eq!(r.take_rest(), &[0xff], "the bytes are still there to inspect");
}

/// `take_until` returns the run before the terminator and consumes the
/// terminator itself; with no terminator present it is a truncated object.
#[test]
fn bytesrw__take_until_splits_on_the_terminator() {
    let mut r = Reader::from_slice(b"one\0two\0");
    assert_eq!(r.take_until(0).expect("first field"), b"one");
    assert_eq!(r.consumed(), 4, "the terminator is consumed too");
    assert_eq!(r.take_until(0).expect("second field"), b"two");
    r.should_be_exhausted().expect("both fields consumed");

    let mut r = Reader::from_slice(b"no terminator");
    assert_eq!(r.take_until(0).unwrap_err(), Error::Truncated);
}

/// `peek` and `advance` move (or do not move) the position exactly as
/// documented, and `truncate` hides the tail from the reader entirely.
#[test]
fn bytesrw__peek_advance_and_truncate() {
    let mut r = Reader::from_slice(&[1, 2, 3, 4, 5, 6]);
    assert_eq!(r.peek(2).expect("2 bytes"), &[1, 2]);
    assert_eq!(r.consumed(), 0, "peek does not consume");
    assert_eq!(r.peek(7).unwrap_err(), Error::Truncated);

    r.advance(2).expect("skip two");
    assert_eq!(r.consumed(), 2);
    assert_eq!(r.remaining(), 4);

    r.truncate(2);
    assert_eq!(r.remaining(), 2, "truncate is relative to the position");
    assert_eq!(r.take_u16().expect("2 bytes"), 0x0304);
    assert_eq!(r.advance(1).unwrap_err(), Error::Truncated);
    assert_eq!(r.into_rest(), &[] as &[u8], "the hidden tail is gone");
}

/// A nested item writes its own length and reads back inside a reader bounded
/// by that length: the inner closure cannot see the outer bytes.
#[test]
fn bytesrw__nested_length_fields_round_trip() {
    let mut buf: Vec<u8> = Vec::new();
    buf.write_u8(0xff);
    {
        let mut nested = buf.write_nested_u16len();
        nested.write_all(b"abc");
        nested.finish().expect("3 bytes fit in a u16 length");
    }
    buf.write_u8(0xee);
    assert_eq!(buf, [0xff, 0x00, 0x03, b'a', b'b', b'c', 0xee]);

    let mut r = Reader::from_slice(&buf);
    assert_eq!(r.take_u8().expect("lead byte"), 0xff);
    let inner = r
        .read_nested_u16len(|inner| {
            assert_eq!(inner.total_len(), 3, "the inner reader sees only its own bytes");
            Ok(inner.take_rest().to_vec())
        })
        .expect("a well-formed nested item");
    assert_eq!(inner, b"abc");
    assert_eq!(r.take_u8().expect("trailing byte"), 0xee);
    r.should_be_exhausted().expect("everything consumed");
}

/// The nesting helpers refuse to lie about a length: a body that does not fit
/// the length field fails to encode, and a closure that leaves bytes behind
/// fails to decode.
#[test]
fn bytesrw__nested_length_mismatches_are_refused() {
    let mut buf: Vec<u8> = Vec::new();
    let mut nested = buf.write_nested_u8len();
    nested.write_zeros(256);
    nested
        .finish()
        .expect_err("256 does not fit in a u8 length field");
    assert!(buf.is_empty(), "a failed nested write emits nothing");

    // Decoding: the closure must consume the whole counted region.
    let encoded = [0x03_u8, b'a', b'b', b'c'];
    let mut r = Reader::from_slice(&encoded);
    let err = r
        .read_nested_u8len(|inner| inner.take_u8())
        .expect_err("the closure left two of the three counted bytes");
    assert_eq!(err, Error::ExtraneousBytes);

    // And the length field itself must be present.
    let mut r = Reader::from_slice(&[0x05_u8, b'a']);
    assert_eq!(
        r.read_nested_u8len(|inner| Ok(inner.take_rest().to_vec()))
            .unwrap_err(),
        Error::Truncated
    );
}
