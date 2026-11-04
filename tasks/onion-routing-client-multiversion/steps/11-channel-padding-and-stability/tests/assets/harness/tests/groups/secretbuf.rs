//! Group `secretbuf`: `tor_bytes::SecretBuf`, the write-only accumulator used
//! for key-derivation inputs.
//!
//! The point of this type is that a plain `Vec<u8>` leaks. When a `Vec` grows
//! it copies its bytes to a fresh allocation and frees the old one *without*
//! erasing it, so half-built KDF input stays readable in freed heap. `SecretBuf`
//! therefore owns its growth: it reallocates one step early -- as soon as the
//! new length would *reach* the capacity, not exceed it -- copies, and wipes the
//! old buffer itself. That "one step early, always at least double" rule is
//! directly observable in the resulting capacity, and it is what these cases
//! grade, along with the `Writer` implementation that makes the type usable as
//! an encoding target in the first place.
#![allow(non_snake_case)]

use tor_bytes::{SecretBuf, Writer};

/// A fresh buffer is empty but already has room, so building a typical secret
/// never has to reallocate (and so never has to wipe an intermediate copy).
#[test]
fn secretbuf__new_preallocates_and_is_empty() {
    let buf = SecretBuf::new();
    assert!(buf.is_empty());
    assert_eq!(buf.len(), 0);
    assert_eq!(&buf[..], &[] as &[u8]);
    assert!(
        buf.capacity() >= 384,
        "a fresh buffer should hold a typical secret without growing, got {}",
        buf.capacity()
    );

    // `Default` is `new`.
    assert_eq!(SecretBuf::default().capacity(), buf.capacity());
    assert_eq!(SecretBuf::default(), buf);

    // An explicit capacity is honoured.
    let small = SecretBuf::with_capacity(16);
    assert!(small.is_empty());
    assert_eq!(small.capacity(), 16);
}

/// Appending concatenates, in order, and updates the length. Appending nothing
/// is a no-op.
#[test]
fn secretbuf__extend_from_slice_appends_in_order() {
    let mut buf = SecretBuf::new();
    buf.extend_from_slice(b"hello");
    buf.extend_from_slice(b", ");
    buf.extend_from_slice(b"world");

    assert_eq!(&buf[..], b"hello, world");
    assert_eq!(buf.len(), 12);

    buf.extend_from_slice(&[]);
    assert_eq!(&buf[..], b"hello, world");
    assert_eq!(buf.len(), 12);
}

/// The growth rule: reallocation happens when the new length would *reach* the
/// capacity, and the new capacity is at least double the old. A plain `Vec`
/// would have filled its last byte in place and kept the same capacity, so this
/// distinguishes the wiping implementation from a naive passthrough -- and the
/// contents must survive the copy.
#[test]
fn secretbuf__growth_reallocates_early_and_doubles() {
    let mut buf = SecretBuf::with_capacity(8);
    buf.extend_from_slice(&[1, 2, 3, 4]);
    assert_eq!(buf.capacity(), 8, "still fits");
    assert_eq!(buf.len(), 4);

    // Exactly filling the buffer triggers the copy-and-wipe path anyway.
    buf.extend_from_slice(&[5, 6, 7, 8]);
    assert_eq!(buf.len(), 8);
    assert_eq!(&buf[..], &[1, 2, 3, 4, 5, 6, 7, 8]);
    assert_eq!(
        buf.capacity(),
        16,
        "capacity must at least double when the buffer is refilled"
    );

    // A single write larger than double the capacity gets exactly what it needs.
    let mut buf = SecretBuf::with_capacity(8);
    buf.extend_from_slice(&[0xaa; 100]);
    assert_eq!(buf.len(), 100);
    assert_eq!(buf.capacity(), 100);
    assert!(buf[..].iter().all(|b| *b == 0xaa));
}

/// Truncation drops the tail. It does not hand the capacity back, because the
/// buffer is about to be reused and a shrink would be another uncontrolled
/// copy. Truncating to a length that is not shorter does nothing.
#[test]
fn secretbuf__truncate_drops_the_tail() {
    let mut buf = SecretBuf::new();
    buf.extend_from_slice(b"0123456789");
    let cap = buf.capacity();

    buf.truncate(4);
    assert_eq!(&buf[..], b"0123");
    assert_eq!(buf.len(), 4);
    assert_eq!(buf.capacity(), cap);

    buf.truncate(100);
    assert_eq!(&buf[..], b"0123", "truncating past the end is a no-op");

    buf.truncate(0);
    assert!(buf.is_empty());
    assert_eq!(buf.capacity(), cap);

    // ...and it can be refilled afterwards.
    buf.extend_from_slice(b"xy");
    assert_eq!(&buf[..], b"xy");
}

/// `SecretBuf` is a `Writer`, so any encoder can target it. It must produce
/// byte-for-byte what the same encoding into a `Vec<u8>` produces -- the whole
/// arrangement fails if the secret-protecting target encodes differently from
/// the ordinary one.
#[test]
fn secretbuf__is_a_writer_that_matches_a_vec() {
    let mut secret = SecretBuf::new();
    let mut plain: Vec<u8> = Vec::new();

    // `Writer` has generic methods, so it is not dyn-compatible; drive both
    // targets through one generic function instead.
    fn write_sample<W: Writer + ?Sized>(target: &mut W) {
        target.write_u8(0x01);
        target.write_u16(0x0203);
        target.write_u32(0x0405_0607);
        target.write_u64(0x0809_0a0b_0c0d_0e0f);
        target.write_zeros(3);
        target.write_all(b"tail");
    }
    write_sample(&mut secret);
    write_sample(&mut plain);

    assert_eq!(&secret[..], &plain[..]);
    assert_eq!(
        &secret[..],
        &[
            0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e,
            0x0f, 0x00, 0x00, 0x00, b't', b'a', b'i', b'l'
        ]
    );

    // Writing enough to force several reallocations still matches.
    let mut secret = SecretBuf::with_capacity(4);
    let mut plain: Vec<u8> = Vec::new();
    for i in 0..200_u32 {
        secret.write_u32(i);
        plain.write_u32(i);
    }
    assert_eq!(&secret[..], &plain[..]);
}

/// A `Vec` that already holds a secret can be moved in without copying, and
/// comparison is by contents -- two buffers holding the same bytes are equal
/// even when they were allocated quite differently.
#[test]
fn secretbuf__from_vec_adopts_the_contents() {
    let v = vec![1_u8, 2, 3];
    let buf = SecretBuf::from(v);
    assert_eq!(&buf[..], &[1, 2, 3]);
    assert_eq!(buf.len(), 3);

    let mut built = SecretBuf::new();
    built.extend_from_slice(&[1, 2, 3]);
    assert_eq!(
        built, buf,
        "equality compares contents, not capacity or provenance"
    );
    assert_eq!(built.clone(), built);
    assert_eq!(SecretBuf::from(Vec::new()), SecretBuf::new());

    let other = SecretBuf::from(vec![1_u8, 2, 4]);
    assert_ne!(other, buf);
}

/// The contents are readable through `Deref` and mutable in place through
/// `AsMut<[u8]>` -- in place, so that a caller can overwrite or unwrap a secret
/// without producing a second copy of it somewhere else.
#[test]
fn secretbuf__as_mut_edits_in_place() {
    let mut buf = SecretBuf::new();
    buf.extend_from_slice(b"secret");

    {
        let s: &mut [u8] = buf.as_mut();
        assert_eq!(s.len(), 6);
        s[0] = b'S';
        s.reverse();
    }
    assert_eq!(&buf[..], b"terceS", "'secret' with byte 0 set to 'S', reversed");
    assert_eq!(buf.len(), 6, "an in-place edit does not change the length");

    // The mutable view spans exactly the initialized bytes, not the capacity.
    let mut empty = SecretBuf::new();
    let s: &mut [u8] = empty.as_mut();
    assert!(s.is_empty(), "capacity is not visible as content");
}
