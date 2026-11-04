//! Fallible encoding: `Writeable`/`WriteableOnce` return `EncodeResult<()>`,
//! and length-prefixed nesting is done through `NestedWriter`.

#![allow(non_snake_case)]
use tor_bytes::{EncodeError, EncodeResult, Writeable, WriteableOnce, Writer};

/// A first-party `Writeable` that always succeeds; it exists to prove the
/// trait method is fallible (the `Ok(())` would not compile otherwise).
struct Pair(u8, u16);

impl Writeable for Pair {
    fn write_onto<B: Writer + ?Sized>(&self, b: &mut B) -> EncodeResult<()> {
        b.write_u8(self.0);
        b.write_u16(self.1);
        Ok(())
    }
}

/// A first-party `Writeable` that always reports a failure, so that error
/// propagation through `Writer::write` is observable.
struct AlwaysBad;

impl Writeable for AlwaysBad {
    fn write_onto<B: Writer + ?Sized>(&self, _b: &mut B) -> EncodeResult<()> {
        Err(EncodeError::BadLengthValue)
    }
}

/// A first-party `WriteableOnce`, consumed by `write_and_consume`.
struct Consumed(Vec<u8>);

impl WriteableOnce for Consumed {
    fn write_into<B: Writer + ?Sized>(self, b: &mut B) -> EncodeResult<()> {
        b.write_u8(self.0.len() as u8);
        b.write_all(&self.0);
        Ok(())
    }
}

#[test]
fn writeable__write_returns_ok_and_appends_big_endian() {
    let mut v: Vec<u8> = Vec::new();
    let r: EncodeResult<()> = v.write(&Pair(0x07, 0x0102));
    assert!(r.is_ok(), "encoding a well-formed object must succeed");
    crate::common::assert_bytes_eq(&v, &[0x07, 0x01, 0x02], "Pair encoding");
}

#[test]
fn writeable__write_propagates_encode_error() {
    let mut v: Vec<u8> = Vec::new();
    let err = v
        .write(&AlwaysBad)
        .expect_err("Writer::write must surface the Writeable's EncodeError");
    assert!(
        matches!(err, EncodeError::BadLengthValue),
        "expected BadLengthValue, got {:?}",
        err
    );
}

#[test]
fn writeable__encode_error_bad_length_value_display() {
    let msg = crate::common::shown(&EncodeError::BadLengthValue);
    assert_eq!(msg, "Object length too large to encode");
}

#[test]
fn writeable__write_and_consume_takes_ownership() {
    let mut v: Vec<u8> = Vec::new();
    v.write_and_consume(Consumed(vec![0xaa, 0xbb]))
        .expect("WriteableOnce must encode");
    crate::common::assert_bytes_eq(&v, &[0x02, 0xaa, 0xbb], "WriteableOnce encoding");
}

#[test]
fn writeable__nested_u8len_writes_one_byte_length() {
    let mut v: Vec<u8> = Vec::new();
    v.write_u8(0xff);
    let mut nested = v.write_nested_u8len();
    nested.write_u16(0x1234);
    nested.finish().expect("2 bytes fit in a u8 length");
    crate::common::assert_bytes_eq(&v, &[0xff, 0x02, 0x12, 0x34], "u8-length nesting");
}

#[test]
fn writeable__nested_u16len_writes_two_byte_length() {
    let mut v: Vec<u8> = Vec::new();
    let mut nested = v.write_nested_u16len();
    nested.write_all(&[0x41; 300]);
    nested.finish().expect("300 bytes fit in a u16 length");
    assert_eq!(v.len(), 302, "u16 length field plus body");
    crate::common::assert_bytes_eq(&v[..2], &[0x01, 0x2c], "u16 length field is big-endian");
}

#[test]
fn writeable__nested_u32len_writes_four_byte_length() {
    let mut v: Vec<u8> = Vec::new();
    let mut nested = v.write_nested_u32len();
    nested.write_u8(0x5a);
    nested.finish().expect("1 byte fits in a u32 length");
    crate::common::assert_bytes_eq(&v, &[0x00, 0x00, 0x00, 0x01, 0x5a], "u32-length nesting");
}

#[test]
fn writeable__nested_overflow_is_bad_length_value() {
    let mut v: Vec<u8> = Vec::new();
    let mut nested = v.write_nested_u8len();
    nested.write_all(&[0u8; 256]);
    let err = nested
        .finish()
        .expect_err("256 bytes cannot be described by a u8 length");
    assert!(
        matches!(err, EncodeError::BadLengthValue),
        "expected BadLengthValue, got {:?}",
        err
    );
}

#[test]
fn writeable__nested_overflow_leaves_outer_writer_untouched() {
    let mut v: Vec<u8> = Vec::new();
    v.write_u8(0xde);
    {
        let mut nested = v.write_nested_u8len();
        nested.write_all(&[0u8; 1000]);
        assert!(nested.finish().is_err());
    }
    crate::common::assert_bytes_eq(
        &v,
        &[0xde],
        "a failed nested write must not append a partial body",
    );
}

#[test]
fn writeable__nested_writers_compose() {
    let mut v: Vec<u8> = Vec::new();
    let mut outer = v.write_nested_u16len();
    {
        let mut inner = outer.write_nested_u8len();
        inner.write_all(&[0x01, 0x02, 0x03]);
        inner.finish().expect("inner fits");
    }
    outer.write_u8(0xff);
    outer.finish().expect("outer fits");
    crate::common::assert_bytes_eq(
        &v,
        &[0x00, 0x05, 0x03, 0x01, 0x02, 0x03, 0xff],
        "nested-within-nested encoding",
    );
}
