//! compactSize, the Bitcoin-inherited variable-length integer.
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time and run with `cargo test -p zebra-chain --test hbv_compactsize_canonical`.
//! It uses only `zebra-chain`'s public API and only dependencies the crate
//! already declares, so injecting it changes neither Cargo.toml nor Cargo.lock.
//!
//! What is being checked is protocol specification section 7.1: the encoding
//! is *canonical*. Each of the four forms covers a disjoint range of values,
//! a reader must reject a value written in a longer form than it needs, and a
//! writer must always pick the shortest form. Non-canonical acceptance is a
//! consensus split, not a cosmetic defect: two nodes would disagree about the
//! byte length of the same object and therefore about where the next field
//! starts.

use std::io::Cursor;

use zebra_chain::serialization::{ReadZcashExt, SerializationError, WriteZcashExt};

/// Read a compactSize from `bytes`, expecting success.
fn read_ok(bytes: &[u8]) -> u64 {
    Cursor::new(bytes)
        .read_compactsize()
        .unwrap_or_else(|e| panic!("expected {:?} to decode, got {:?}", bytes, e))
}

/// Read a compactSize from `bytes`, expecting a `Parse` error -- the encoding
/// was well-formed but not canonical.
fn read_non_canonical(bytes: &[u8]) {
    match Cursor::new(bytes).read_compactsize() {
        Err(SerializationError::Parse(_)) => {}
        Err(other) => panic!(
            "expected {:?} to be rejected as non-canonical, got {:?}",
            bytes, other
        ),
        Ok(value) => panic!(
            "expected {:?} to be rejected as non-canonical, got {}",
            bytes, value
        ),
    }
}

/// Read a compactSize from `bytes`, expecting an i/o error -- the input ran
/// out before the encoding was complete.
fn read_truncated(bytes: &[u8]) {
    match Cursor::new(bytes).read_compactsize() {
        Err(SerializationError::Io(_)) => {}
        Err(other) => panic!(
            "expected truncated {:?} to be an io error, got {:?}",
            bytes, other
        ),
        Ok(value) => panic!(
            "expected truncated {:?} to be an io error, got {}",
            bytes, value
        ),
    }
}

fn write(value: u64) -> Vec<u8> {
    let mut buf = Vec::new();
    buf.write_compactsize(value).expect("Vec writes are infallible");
    buf
}

#[test]
fn single_byte_form_covers_zero_to_0xfc() {
    assert_eq!(0x00, read_ok(b"\x00"));
    assert_eq!(0x12, read_ok(b"\x12"));
    assert_eq!(0xfc, read_ok(b"\xfc"));
    // A single-byte encoding is complete after one byte: trailing bytes belong
    // to the next field and must not be consumed.
    let mut cursor = Cursor::new(&b"\x12\xff\xff"[..]);
    assert_eq!(0x12, cursor.read_compactsize().expect("decodes"));
    assert_eq!(1, cursor.position());
}

#[test]
fn two_byte_form_covers_0xfd_to_0xffff() {
    assert_eq!(0x0000_00fd, read_ok(b"\xfd\xfd\x00"));
    assert_eq!(0x0000_aafd, read_ok(b"\xfd\xfd\xaa"));
    assert_eq!(0x0000_ffff, read_ok(b"\xfd\xff\xff"));
}

#[test]
fn four_byte_form_covers_0x1_0000_to_0xffff_ffff() {
    assert_eq!(0x0001_0000, read_ok(b"\xfe\x00\x00\x01\x00"));
    assert_eq!(0x00bb_aafd, read_ok(b"\xfe\xfd\xaa\xbb\x00"));
    assert_eq!(0xffff_ffff, read_ok(b"\xfe\xff\xff\xff\xff"));
}

#[test]
fn eight_byte_form_covers_the_rest_of_u64() {
    assert_eq!(
        0x1_0000_0000,
        read_ok(b"\xff\x00\x00\x00\x00\x01\x00\x00\x00")
    );
    assert_eq!(
        0x22cc_bbaa_fd,
        read_ok(b"\xff\xfd\xaa\xbb\xcc\x22\x00\x00\x00")
    );
    // There is no upper bound at this layer. Size limits belong to the types
    // that use a length prefix, not to the prefix encoding itself.
    assert_eq!(
        u64::max_value(),
        read_ok(b"\xff\xff\xff\xff\xff\xff\xff\xff\xff")
    );
}

#[test]
fn two_byte_form_rejects_values_that_fit_in_one() {
    read_non_canonical(b"\xfd\x00\x00"); // 0, which is a bare 0x00
    read_non_canonical(b"\xfd\x12\x00"); // 0x12
    read_non_canonical(b"\xfd\xfc\x00"); // 0xfc, the last single-byte value
}

#[test]
fn four_byte_form_rejects_values_that_fit_in_two() {
    read_non_canonical(b"\xfe\x00\x00\x00\x00"); // 0
    read_non_canonical(b"\xfe\xfd\x00\x00\x00"); // 0xfd
    read_non_canonical(b"\xfe\xff\xff\x00\x00"); // 0xffff, the last two-byte value
}

#[test]
fn eight_byte_form_rejects_values_that_fit_in_four() {
    read_non_canonical(b"\xff\x00\x00\x00\x00\x00\x00\x00\x00"); // 0
    read_non_canonical(b"\xff\x00\x00\x01\x00\x00\x00\x00\x00"); // 0x1_0000
    read_non_canonical(b"\xff\xff\xff\xff\xff\x00\x00\x00\x00"); // 0xffff_ffff
}

#[test]
fn truncated_encodings_are_io_errors_not_parse_errors() {
    // The distinction matters to the caller: a parse error is a permanently
    // invalid object, an io error may just mean "read more bytes".
    read_truncated(b"");
    read_truncated(b"\xfd");
    read_truncated(b"\xfd\x01");
    read_truncated(b"\xfe\x00\x00\x01");
    read_truncated(b"\xff\x00\x00\x00\x00\x01\x00\x00");
}

#[test]
fn writes_use_the_shortest_form() {
    assert_eq!(write(0x00), b"\x00".to_vec());
    assert_eq!(write(0xfc), b"\xfc".to_vec());
    assert_eq!(write(0xfd), b"\xfd\xfd\x00".to_vec());
    assert_eq!(write(0xaafd), b"\xfd\xfd\xaa".to_vec());
    assert_eq!(write(0xffff), b"\xfd\xff\xff".to_vec());
    assert_eq!(write(0x1_0000), b"\xfe\x00\x00\x01\x00".to_vec());
    assert_eq!(write(0xbbaafd), b"\xfe\xfd\xaa\xbb\x00".to_vec());
    assert_eq!(write(0xffff_ffff), b"\xfe\xff\xff\xff\xff".to_vec());
    assert_eq!(
        write(0x1_0000_0000),
        b"\xff\x00\x00\x00\x00\x01\x00\x00\x00".to_vec()
    );
    assert_eq!(
        write(u64::max_value()),
        b"\xff\xff\xff\xff\xff\xff\xff\xff\xff".to_vec()
    );
}

#[test]
fn written_lengths_are_exactly_one_three_five_or_nine() {
    for &(value, len) in &[
        (0u64, 1usize),
        (0xfc, 1),
        (0xfd, 3),
        (0xffff, 3),
        (0x1_0000, 5),
        (0xffff_ffff, 5),
        (0x1_0000_0000, 9),
        (u64::max_value(), 9),
    ] {
        assert_eq!(
            write(value).len(),
            len,
            "compactSize({:#x}) should occupy {} bytes",
            value,
            len
        );
    }
}

#[test]
fn every_boundary_value_round_trips() {
    // Both sides of each of the three boundaries, plus a value in the middle
    // of each range. A writer that is canonical and a reader that enforces
    // canonicality agree on all of them.
    let values: [u64; 12] = [
        0,
        1,
        0xfc,
        0xfd,
        0x1234,
        0xffff,
        0x1_0000,
        0x1234_5678,
        0xffff_ffff,
        0x1_0000_0000,
        0x1234_5678_9abc,
        u64::max_value(),
    ];
    for &value in values.iter() {
        let encoded = write(value);
        let mut cursor = Cursor::new(&encoded[..]);
        let decoded = cursor
            .read_compactsize()
            .unwrap_or_else(|e| panic!("{:#x} encoded as {:?} but did not decode: {:?}", value, encoded, e));
        assert_eq!(value, decoded, "round trip through {:?}", encoded);
        assert_eq!(
            cursor.position() as usize,
            encoded.len(),
            "the reader consumed the wrong number of bytes for {:#x}",
            value
        );
    }
}
