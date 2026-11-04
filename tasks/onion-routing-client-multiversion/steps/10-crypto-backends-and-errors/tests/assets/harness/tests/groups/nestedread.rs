//! Length-prefixed decoding: `Reader::read_nested_*` and `Reader::take_rest`.

#![allow(non_snake_case)]
use tor_bytes::{Error, Reader};

#[test]
fn nestedread__u8len_gives_inner_reader_of_exactly_that_length() {
    let buf = [0x03, 0xaa, 0xbb, 0xcc, 0xff];
    let mut r = Reader::from_slice(&buf);
    let inner: Vec<u8> = r
        .read_nested_u8len(|b| Ok(b.take_rest().to_vec()))
        .expect("well-formed u8-length nesting");
    crate::common::assert_bytes_eq(&inner, &[0xaa, 0xbb, 0xcc], "nested body");
    assert_eq!(r.remaining(), 1, "only the counted bytes are consumed");
    assert_eq!(r.take_u8().expect("trailing byte"), 0xff);
}

#[test]
fn nestedread__u16len_length_field_is_big_endian() {
    let mut buf = vec![0x01, 0x00];
    buf.extend(std::iter::repeat(0x5a).take(256));
    let mut r = Reader::from_slice(&buf);
    let n = r
        .read_nested_u16len(|b| Ok(b.take_rest().len()))
        .expect("well-formed u16-length nesting");
    assert_eq!(n, 256, "0x0100 must be read as 256, not 1");
    assert_eq!(r.remaining(), 0);
}

#[test]
fn nestedread__u32len_length_field_is_four_bytes() {
    let buf = [0x00, 0x00, 0x00, 0x02, 0x10, 0x20];
    let mut r = Reader::from_slice(&buf);
    let v: Vec<u8> = r
        .read_nested_u32len(|b| Ok(b.take_rest().to_vec()))
        .expect("well-formed u32-length nesting");
    crate::common::assert_bytes_eq(&v, &[0x10, 0x20], "nested body after u32 length");
}

#[test]
fn nestedread__short_body_is_truncated() {
    let buf = [0x08, 0x01, 0x02];
    let mut r = Reader::from_slice(&buf);
    let err = r
        .read_nested_u8len(|b| Ok(b.take_rest().to_vec()))
        .expect_err("length field promises more bytes than are present");
    assert_eq!(err, Error::Truncated, "got {:?}", err);
}

#[test]
fn nestedread__unconsumed_inner_bytes_are_extraneous() {
    let buf = [0x04, 0x01, 0x02, 0x03, 0x04];
    let mut r = Reader::from_slice(&buf);
    let err = r
        .read_nested_u8len(|b| b.take_u8())
        .expect_err("the closure must consume the whole counted region");
    assert_eq!(err, Error::ExtraneousBytes, "got {:?}", err);
}

#[test]
fn nestedread__inner_error_is_propagated() {
    let buf = [0x02, 0x01, 0x02];
    let mut r = Reader::from_slice(&buf);
    let err = r
        .read_nested_u8len(|b| b.take_u32().map(|_| ()))
        .expect_err("a failing closure must not be masked");
    assert_eq!(err, Error::Truncated, "got {:?}", err);
}

#[test]
fn nestedread__empty_nested_region_is_legal() {
    let buf = [0x00, 0x77];
    let mut r = Reader::from_slice(&buf);
    let v: Vec<u8> = r
        .read_nested_u8len(|b| Ok(b.take_rest().to_vec()))
        .expect("a zero-length nested region is well-formed");
    assert!(v.is_empty(), "zero-length body must be empty");
    assert_eq!(r.take_u8().expect("byte after empty region"), 0x77);
}

#[test]
fn nestedread__take_rest_consumes_everything_remaining() {
    let buf = [0x01, 0x02, 0x03];
    let mut r = Reader::from_slice(&buf);
    assert_eq!(r.take_u8().expect("first"), 0x01);
    crate::common::assert_bytes_eq(r.take_rest(), &[0x02, 0x03], "take_rest");
    assert_eq!(r.remaining(), 0);
    assert!(r.should_be_exhausted().is_ok());
    crate::common::assert_bytes_eq(r.take_rest(), &[], "take_rest on an empty reader");
}
