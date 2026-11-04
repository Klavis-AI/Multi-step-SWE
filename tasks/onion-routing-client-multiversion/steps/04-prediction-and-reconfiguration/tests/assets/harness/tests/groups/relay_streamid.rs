//! Group `relay_streamid` — a decoded relay cell keeps the stream ID it
//! arrived with, that ID survives the round trip through the wire form, and it
//! is read from the field the specification puts it in.
//!
//! `RelayCell::stream_id` is new in this step: at the previous checkpoint the
//! stream ID was only reachable by consuming the cell.

use crate::common;
use rand::SeedableRng;
use tor_bytes::Reader;
use tor_cell::relaycell::msg::{Begin, RelayMsg, Sendme};
use tor_cell::relaycell::{RelayCell, RelayCmd, StreamId};

/// A reproducible RNG for encoding cells.
fn rng() -> rand_chacha::ChaCha20Rng {
    rand_chacha::ChaCha20Rng::seed_from_u64(0x0bad_c0de)
}

/// The wire number of a relay command.
fn relay_num(cmd: RelayCmd) -> u8 {
    cmd.into()
}

#[test]
fn relay_streamid__a_fresh_cell_reports_the_id_it_was_given() {
    let begin = Begin::new("example.com", 443, 0).expect("begin cell should build");
    let cell = RelayCell::new(StreamId::from(7), RelayMsg::Begin(begin));
    assert_eq!(cell.stream_id(), StreamId::from(7));
    assert_eq!(u16::from(cell.stream_id()), 7);
    assert!(!cell.stream_id().is_zero());
    // Reporting the ID does not consume the cell.
    assert_eq!(cell.cmd(), RelayCmd::BEGIN);
    assert_eq!(u16::from(cell.stream_id()), 7);
}

#[test]
fn relay_streamid__the_id_survives_a_round_trip_through_the_wire_form() {
    for id in [0_u16, 1, 2, 42, 255, 32768, u16::MAX] {
        let begin = Begin::new("example.com", 80, 0).expect("begin cell should build");
        let cell = RelayCell::new(StreamId::from(id), RelayMsg::Begin(begin));
        let body = cell.encode(&mut rng()).expect("cell should encode");
        let decoded = RelayCell::decode(body).expect("cell should decode");
        assert_eq!(decoded.stream_id(), StreamId::from(id));
        assert_eq!(u16::from(decoded.stream_id()), id);
    }
}

#[test]
fn relay_streamid__a_zero_id_marks_a_circuit_wide_message() {
    let cell = RelayCell::new(
        StreamId::from(0),
        RelayMsg::Sendme(Sendme::new_empty()),
    );
    assert!(cell.stream_id().is_zero());
    let body = cell.encode(&mut rng()).expect("cell should encode");
    let decoded = RelayCell::decode(body).expect("cell should decode");
    assert!(decoded.stream_id().is_zero());
    assert!(matches!(decoded.msg(), RelayMsg::Sendme(_)));
}

#[test]
fn relay_streamid__the_id_is_read_from_the_documented_offset() {
    // A body laid out by hand: command, two "recognized" bytes, the stream ID,
    // a four-byte digest and a two-byte length.
    let body = common::relay_body(relay_num(RelayCmd::DATA), 0, 0x1234, [0_u8; 4], b"payload");
    let decoded = RelayCell::decode(body).expect("hand-built cell should decode");
    assert_eq!(decoded.stream_id(), StreamId::from(0x1234));
    assert_eq!(decoded.cmd(), RelayCmd::DATA);
    match decoded.msg() {
        RelayMsg::Data(d) => assert_eq!(d.as_ref(), b"payload"),
        other => panic!("decoded message is not DATA: {:?}", other.cmd()),
    }
}

#[test]
fn relay_streamid__the_id_is_not_confused_with_the_recognized_field() {
    // Every byte of the two "recognized" bytes is set: none of it may leak
    // into the stream ID.
    let body = common::relay_body(relay_num(RelayCmd::SENDME), 0xffff, 0x0001, [0xff_u8; 4], &[]);
    let decoded = RelayCell::decode(body).expect("cell should decode");
    assert_eq!(decoded.stream_id(), StreamId::from(1));
    assert_eq!(decoded.cmd(), RelayCmd::SENDME);

    // The digest bytes do not leak in either.
    let body = common::relay_body(relay_num(RelayCmd::SENDME), 0, 0x0002, [0xab; 4], &[]);
    let decoded = RelayCell::decode(body).expect("cell should decode");
    assert_eq!(decoded.stream_id(), StreamId::from(2));

    // And a body cut short of the stream-id field cannot decode at all.
    let short = &common::body_from_fixture(&body)[..4];
    let mut reader = Reader::from_slice(short);
    assert!(RelayCell::decode_from_reader(&mut reader).is_err());
}
