//! Group `datacell` - DATA relay messages that refuse to panic.
//!
//! Graded through `tor_cell::relaycell::msg::Data` and `tor_cell::Error`.
//! A DATA message body has to fit in the payload of one RELAY cell, and the
//! step-02 client has to say so with a value rather than an assertion: a
//! stream that hands the encoder a large buffer must get a cell plus a
//! remainder, not an abort.

use tor_cell::relaycell::msg::{Body, Data, RelayMsg};
use tor_cell::relaycell::{RelayCell, RelayCmd, StreamId};
use tor_cell::Error;

/// A deterministic filler of `n` bytes.
fn filler(n: usize) -> Vec<u8> {
    (0..n).map(|i| (i % 251) as u8).collect()
}

#[test]
fn datacell__the_body_limit_is_the_relay_payload_minus_its_header() {
    // A RELAY cell body is 509 bytes and its header is 11 (command, two
    // "recognized" bytes, stream id, four digest bytes, length).
    assert_eq!(Data::MAXLEN, 498);

    let full = filler(Data::MAXLEN);
    let d = Data::new(&full).expect("a body of exactly MAXLEN bytes was refused");
    assert_eq!(d.as_ref(), &full[..]);
    assert_eq!(Vec::<u8>::from(d), full);
}

#[test]
fn datacell__an_oversized_body_is_reported_not_asserted() {
    let too_big = filler(Data::MAXLEN + 1);
    match Data::new(&too_big) {
        Ok(_) => panic!("a body one byte over MAXLEN was accepted"),
        Err(e) => {
            assert!(
                matches!(e, Error::CantEncode),
                "expected Error::CantEncode, got {:?}",
                e
            );
        }
    }

    // Much larger inputs behave the same way; nothing is silently truncated.
    match Data::new(&filler(5000)) {
        Ok(_) => panic!("a 5000-byte body was accepted"),
        Err(e) => assert!(matches!(e, Error::CantEncode)),
    }
}

#[test]
fn datacell__splitting_takes_one_cell_and_hands_back_the_rest() {
    let buf = filler(1200);
    let (first, rest) = Data::split_from(&buf);
    assert_eq!(first.as_ref(), &buf[..Data::MAXLEN]);
    assert_eq!(rest, &buf[Data::MAXLEN..]);
    assert_eq!(rest.len(), 1200 - Data::MAXLEN);

    let (second, rest) = Data::split_from(rest);
    assert_eq!(second.as_ref(), &buf[Data::MAXLEN..2 * Data::MAXLEN]);
    assert_eq!(rest.len(), 1200 - 2 * Data::MAXLEN);

    let (third, rest) = Data::split_from(rest);
    assert_eq!(third.as_ref(), &buf[2 * Data::MAXLEN..]);
    assert!(rest.is_empty(), "three cells should have drained 1200 bytes");
}

#[test]
fn datacell__splitting_a_short_body_leaves_nothing_over() {
    let (cell, rest) = Data::split_from(b"hello");
    assert_eq!(cell.as_ref(), b"hello");
    assert!(rest.is_empty());

    let (cell, rest) = Data::split_from(b"");
    assert_eq!(cell.as_ref(), b"");
    assert!(rest.is_empty());

    // Splitting never rejects an input, so a body of exactly MAXLEN is whole.
    let exact = filler(Data::MAXLEN);
    let (cell, rest) = Data::split_from(&exact);
    assert_eq!(cell.as_ref(), &exact[..]);
    assert!(rest.is_empty());
}

#[test]
fn datacell__a_data_message_fills_the_relay_header_it_was_sized_for() {
    let d = Data::new(b"hello").expect("short body refused");
    let cell = RelayCell::new(StreamId::from(7_u16), d.into_message());
    assert_eq!(cell.cmd(), RelayCmd::DATA);

    let mut rng = crate::common::seeded_rng(0x0da7a);
    let raw = cell.encode(&mut rng).expect("encoding a short DATA failed");
    assert_eq!(raw.len(), 509);
    // command, recognized, stream id, digest, length, body.
    assert_eq!(
        &raw[..16],
        &[2, 0, 0, 0, 7, 0, 0, 0, 0, 0, 5, b'h', b'e', b'l', b'l', b'o'][..]
    );
    // Four bytes of zero are kept after the body before random padding starts.
    assert_eq!(&raw[16..20], &[0, 0, 0, 0][..]);

    let decoded = RelayCell::decode(raw).expect("decoding failed");
    let (sid, msg) = decoded.into_streamid_and_msg();
    assert_eq!(u16::from(sid), 7);
    match msg {
        RelayMsg::Data(d) => assert_eq!(d.as_ref(), b"hello"),
        other => panic!("expected a DATA message, got {:?}", other),
    }

    // A message that exactly fills the payload still encodes.
    let big = Data::new(&filler(Data::MAXLEN)).expect("MAXLEN body refused");
    let cell = RelayCell::new(StreamId::from(1_u16), big.into_message());
    let raw = cell.encode(&mut rng).expect("encoding a full DATA failed");
    assert_eq!(&raw[9..11], &[0x01, 0xF2][..], "length field should be 498");
}
