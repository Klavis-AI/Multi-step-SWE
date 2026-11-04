//! Cell encoding after the fallible-encoding migration: `Versions::new`,
//! `encode_for_handshake`, `RelayCell::encode`, `Data::new` and the channel codec.

#![allow(non_snake_case)]
use bytes::BytesMut;
use rand::SeedableRng;
use tor_cell::chancell::{codec::ChannelCodec, msg as cmsg, ChanCell, ChanCmd, CircId};
use tor_cell::chancell::{RawCellBody, CELL_DATA_LEN};
use tor_cell::relaycell::{msg as rmsg, RelayCell, RelayCmd, StreamId};

/// A deterministic CSPRNG: `RelayCell::encode` needs one for its padding, and
/// the suite must not depend on entropy.
fn rng() -> rand::rngs::StdRng {
    rand::rngs::StdRng::from_seed([0x5a; 32])
}

#[test]
fn cellencode__versions_new_is_fallible_and_encodes_handshake_layout() {
    let v = tor_cell::chancell::msg::Versions::new(vec![3_u16, 4, 5])
        .expect("three versions is a legal VERSIONS cell");
    let bytes = v
        .encode_for_handshake()
        .expect("encoding a legal VERSIONS cell must succeed");
    crate::common::assert_bytes_eq(
        &bytes,
        &[0x00, 0x00, 0x07, 0x00, 0x06, 0x00, 0x03, 0x00, 0x04, 0x00, 0x05],
        "VERSIONS handshake encoding: 2-byte CircID, command 7, u16 length, u16 versions",
    );
}

#[test]
fn cellencode__versions_new_rejects_an_unrepresentable_list() {
    let too_many = vec![4_u16; (u16::MAX / 2) as usize];
    let err = tor_cell::chancell::msg::Versions::new(too_many)
        .err()
        .expect("a version list whose byte length overflows the u16 length field");
    assert!(
        matches!(err, tor_cell::Error::CantEncode("Too many versions")),
        "expected CantEncode, got {:?}",
        err
    );
    assert_eq!(
        crate::common::shown(&err),
        "Message can't be represented in a Tor cell: Too many versions"
    );
}

#[test]
fn cellencode__relay_cell_encode_produces_a_509_byte_body() {
    let msg = rmsg::Data::new(b"hi").expect("2 bytes fit in a DATA cell");
    let cell = RelayCell::new(StreamId::from(0x0102), msg.into());
    let body: RawCellBody = cell.encode(&mut rng()).expect("encoding must succeed");
    assert_eq!(body.len(), CELL_DATA_LEN, "relay cell body is 509 bytes");
    // command, recognized(2), streamID(2), digest(4), length(2), then the body.
    assert_eq!(body[0], u8::from(RelayCmd::DATA), "relay command byte");
    crate::common::assert_bytes_eq(&body[1..3], &[0, 0], "`recognized` is zero when sent");
    crate::common::assert_bytes_eq(&body[3..5], &[0x01, 0x02], "stream ID is big-endian");
    crate::common::assert_bytes_eq(&body[5..9], &[0, 0, 0, 0], "digest placeholder");
    crate::common::assert_bytes_eq(&body[9..11], &[0x00, 0x02], "payload length");
    crate::common::assert_bytes_eq(&body[11..13], b"hi", "payload");
}

#[test]
fn cellencode__relay_cell_round_trips_through_decode() {
    let msg = rmsg::Data::new(b"conformance").expect("short payload");
    let cell = RelayCell::new(StreamId::from(7), msg.into());
    let body = cell.encode(&mut rng()).expect("encoding must succeed");
    let decoded = RelayCell::decode(body).expect("decoding our own 509-byte body");
    assert_eq!(u16::from(decoded.stream_id()), 7);
    assert_eq!(decoded.cmd(), RelayCmd::DATA);
    match decoded.msg() {
        rmsg::RelayMsg::Data(d) => {
            crate::common::assert_bytes_eq(d.as_ref(), b"conformance", "decoded payload")
        }
        other => panic!("expected a DATA message, got {:?}", other.cmd()),
    }
}

#[test]
fn cellencode__data_maxlen_leaves_room_for_the_relay_header() {
    assert_eq!(
        rmsg::Data::MAXLEN,
        CELL_DATA_LEN - 11,
        "an 11-byte relay header precedes the payload"
    );
    assert_eq!(rmsg::Data::MAXLEN, 498);
}

#[test]
fn cellencode__data_new_rejects_an_oversized_payload() {
    let big = vec![0x41_u8; rmsg::Data::MAXLEN + 1];
    let err = rmsg::Data::new(&big)
        .err()
        .expect("a payload longer than MAXLEN has no relay cell representation");
    assert!(
        matches!(err, tor_cell::Error::CantEncode(_)),
        "expected CantEncode, got {:?}",
        err
    );
    assert!(
        rmsg::Data::new(&big[..rmsg::Data::MAXLEN]).is_ok(),
        "exactly MAXLEN bytes must still be accepted"
    );
}

#[test]
fn cellencode__codec_pads_a_fixed_cell_to_514_bytes() {
    let mut codec = ChannelCodec::new(4);
    let mut dst = BytesMut::new();
    let cell = ChanCell::new(CircId::from(0x11223344), cmsg::Padding::new().into());
    codec.write_cell(cell, &mut dst).expect("PADDING encodes");
    assert_eq!(dst.len(), 514, "4-byte CircID + command + 509-byte payload");
    crate::common::assert_bytes_eq(&dst[..4], &[0x11, 0x22, 0x33, 0x44], "CircID is big-endian");
    assert_eq!(dst[4], u8::from(ChanCmd::PADDING), "command byte");
    assert!(dst[5..].iter().all(|b| *b == 0), "payload is zero-padded");
}

#[test]
fn cellencode__codec_gives_a_variable_cell_a_u16_length() {
    let mut codec = ChannelCodec::new(4);
    let mut dst = BytesMut::new();
    let cell = ChanCell::new(CircId::from(0), cmsg::VPadding::new(6).into());
    codec.write_cell(cell, &mut dst).expect("VPADDING encodes");
    assert_eq!(dst.len(), 4 + 1 + 2 + 6, "CircID, command, u16 length, body");
    assert_eq!(dst[4], u8::from(ChanCmd::VPADDING));
    crate::common::assert_bytes_eq(&dst[5..7], &[0x00, 0x06], "u16 body length");
}

#[test]
fn cellencode__codec_decodes_a_hand_built_fixed_cell() {
    let mut raw = vec![0x00, 0x00, 0x00, 0x00];
    raw.push(u8::from(ChanCmd::PADDING));
    raw.extend(std::iter::repeat(0u8).take(CELL_DATA_LEN));
    let mut src = BytesMut::from(&raw[..]);
    let mut codec = ChannelCodec::new(4);
    let cell = codec
        .decode_cell(&mut src)
        .expect("a well-formed PADDING cell")
        .expect("514 bytes is a complete fixed-length cell");
    assert_eq!(u32::from(cell.circid()), 0);
    assert_eq!(cell.msg().cmd(), ChanCmd::PADDING);
    assert_eq!(src.len(), 0, "the codec consumes exactly one cell");
}

#[test]
fn cellencode__codec_reports_an_incomplete_cell_as_none() {
    let mut src = BytesMut::from(&[0u8; 100][..]);
    let mut codec = ChannelCodec::new(4);
    let got = codec
        .decode_cell(&mut src)
        .expect("a truncated prefix is not an error");
    assert!(got.is_none(), "an incomplete cell yields Ok(None)");
    assert_eq!(src.len(), 100, "nothing is consumed from a partial cell");
}
