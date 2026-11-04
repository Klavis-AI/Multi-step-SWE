//! Group `relayhdr`: the 11-byte relay-cell header and its 509-byte body.
//!
//! Anchored to `fixtures/cell_relay_data.bin`, whose bytes were laid out by
//! hand from the relay-cell field order in tor-spec (see SPEC_NOTES.md), not
//! captured from any implementation.

use tor_cell::chancell::CELL_DATA_LEN;
use tor_cell::relaycell::msg::{Data, RelayMsg};
use tor_cell::relaycell::{RelayCell, RelayCmd, StreamId};

use crate::common::{body_of_fixed_cell, det_rng, fixture_bytes, raw_body};

#[test]
fn relayhdr__fixture_relay_cell_decodes_to_its_header_fields() {
    let cell = fixture_bytes("cell_relay_data.bin");
    let body = body_of_fixed_cell(&cell);
    let rc = RelayCell::decode(body).expect("fixture relay cell must decode");
    assert_eq!(rc.cmd(), RelayCmd::DATA);
    let (sid, msg) = rc.into_streamid_and_msg();
    assert_eq!(u16::from(sid), 0x0171);
    match msg {
        RelayMsg::Data(d) => assert_eq!(d.as_ref(), b"hello world"),
        other => panic!("expected Data, got {:?}", other),
    }
}

#[test]
fn relayhdr__encoded_header_matches_the_fixture_byte_layout() {
    let fixture = fixture_bytes("cell_relay_data.bin");
    let expected = &fixture[5..]; // strip circid+command; the rest is the body

    let rc = RelayCell::new(
        StreamId::from(0x0171),
        RelayMsg::Data(Data::new(b"hello world")),
    );
    let got = rc.encode(&mut det_rng()).expect("encode must succeed");

    // 11 header bytes + 11 payload bytes are fully determined by the spec.
    assert_eq!(&got[0..22], &expected[0..22]);
    // The next four bytes are zero padding before any random filler.
    assert_eq!(&got[22..26], &[0, 0, 0, 0]);
}

#[test]
fn relayhdr__recognized_and_digest_fields_are_zeroed_before_encryption() {
    let rc = RelayCell::new(StreamId::from(0x2345), RelayMsg::Drop);
    let got = rc.encode(&mut det_rng()).expect("encode must succeed");
    assert_eq!(got[0], u8::from(RelayCmd::DROP));
    assert_eq!(&got[1..3], &[0, 0], "recognized field must be zero");
    assert_eq!(&got[3..5], &0x2345u16.to_be_bytes());
    assert_eq!(&got[5..9], &[0, 0, 0, 0], "digest field must be zero");
    assert_eq!(&got[9..11], &[0, 0], "DROP carries an empty body");
}

#[test]
fn relayhdr__length_field_counts_only_the_payload() {
    for n in [0_usize, 1, 100, 498] {
        let payload = vec![0x5a_u8; n];
        let rc = if n == 0 {
            RelayCell::new(StreamId::from(7), RelayMsg::Drop)
        } else {
            RelayCell::new(
                StreamId::from(7),
                RelayMsg::Data(Data::new(&payload[..])),
            )
        };
        let got = rc.encode(&mut det_rng()).expect("encode must succeed");
        let len = u16::from_be_bytes([got[9], got[10]]) as usize;
        assert_eq!(len, n, "length field for a {}-byte payload", n);
        assert_eq!(&got[11..11 + n], &payload[..]);
    }
}

#[test]
fn relayhdr__body_is_always_the_full_cell_payload_length() {
    assert_eq!(CELL_DATA_LEN, 509);
    let msgs = vec![
        RelayMsg::Drop,
        RelayMsg::Truncate,
        RelayMsg::BeginDir,
        RelayMsg::Data(Data::new(b"x")),
    ];
    for m in msgs {
        let got = RelayCell::new(StreamId::from(1), m)
            .encode(&mut det_rng())
            .expect("encode must succeed");
        assert_eq!(got.len(), CELL_DATA_LEN);
    }
}

#[test]
fn relayhdr__decode_rejects_a_length_field_past_the_end_of_the_cell() {
    let mut body = vec![0_u8; CELL_DATA_LEN];
    body[0] = u8::from(RelayCmd::DATA);
    body[3..5].copy_from_slice(&1u16.to_be_bytes());
    body[9..11].copy_from_slice(&600u16.to_be_bytes());
    // Only the rejection is graded: neither the error variant nor its
    // message is part of the contract.
    RelayCell::decode(raw_body(&body)).expect_err("600 > 498 remaining");
}

#[test]
fn relayhdr__maximum_data_payload_is_four_hundred_ninety_eight_bytes() {
    assert_eq!(Data::MAXLEN, CELL_DATA_LEN - 11);
    assert_eq!(Data::MAXLEN, 498);
    let payload = vec![0xa5_u8; Data::MAXLEN];
    let got = RelayCell::new(
        StreamId::from(9),
        RelayMsg::Data(Data::new(&payload[..])),
    )
    .encode(&mut det_rng())
    .expect("a maximum-length data cell must encode");
    // A full-length body leaves no room for padding, so every byte is fixed.
    assert_eq!(u16::from_be_bytes([got[9], got[10]]), 498);
    assert_eq!(&got[11..], &payload[..]);
}
