//! Group `sendmefc`: SENDME flow-control cells.
//!
//! `fixtures/relay_sendme_tag.bin` was laid out by hand from the authenticated
//! SENDME body format in tor-spec (see SPEC_NOTES.md).

use tor_bytes::Reader;
use tor_cell::relaycell::msg::{RelayMsg, Sendme};
use tor_cell::relaycell::{RelayCell, RelayCmd, StreamId};

use crate::common::{det_rng, fixture_bytes, raw_body};

/// The 20-byte tag carried by the fixture.
const TAG: [u8; 20] = [
    0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e,
    0x4f, 0x50, 0x51, 0x52, 0x53,
];

fn parse_sendme(b: &[u8]) -> tor_bytes::Result<Sendme> {
    let mut r = Reader::from_slice(b);
    match RelayMsg::decode_from_reader(RelayCmd::SENDME, &mut r)? {
        RelayMsg::Sendme(s) => Ok(s),
        other => panic!("expected Sendme, got {:?}", other),
    }
}

#[test]
fn sendmefc__tagged_sendme_matches_the_spec_fixture() {
    let f = fixture_bytes("relay_sendme_tag.bin");
    assert_eq!(f[0], u8::from(RelayCmd::SENDME));
    assert_eq!(u16::from_be_bytes([f[9], f[10]]), 23);

    let s = parse_sendme(&f[11..11 + 23]).expect("the fixture SENDME must decode");
    assert_eq!(s.into_tag(), Some(TAG.to_vec()));

    let got = RelayCell::new(StreamId::from(0), RelayMsg::Sendme(Sendme::new_tag(TAG)))
        .encode(&mut det_rng())
        .expect("encode must succeed");
    assert_eq!(&got[0..34], &f[0..34]);
}

#[test]
fn sendmefc__empty_sendme_body_is_zero_length() {
    let mut body = Vec::new();
    RelayMsg::Sendme(Sendme::new_empty()).encode_onto(&mut body);
    assert_eq!(body, Vec::<u8>::new());
    assert_eq!(
        parse_sendme(b"").expect("an empty SENDME body is legal").into_tag(),
        None
    );
}

#[test]
fn sendmefc__version_zero_and_unknown_versions_carry_no_tag() {
    assert_eq!(parse_sendme(&[0]).expect("version 0 parses").into_tag(), None);
    assert_eq!(
        parse_sendme(&[2, 0, 4, 1, 2, 3, 4])
            .expect("an unknown SENDME version is tolerated")
            .into_tag(),
        None
    );
}

#[test]
fn sendmefc__truncated_tag_is_rejected() {
    parse_sendme(&[1, 0, 20, 0xaa, 0xbb]).expect_err("declared 20 bytes, gave 2");
}

#[test]
fn sendmefc__sendme_is_carried_at_both_circuit_and_stream_level() {
    // Circuit-level: stream ID zero, with an authentication tag.
    let body = RelayCell::new(StreamId::from(0), RelayMsg::Sendme(Sendme::new_tag(TAG)))
        .encode(&mut det_rng())
        .expect("encode must succeed");
    let rc = RelayCell::decode(raw_body(&body)).expect("decode must succeed");
    assert_eq!(rc.cmd(), RelayCmd::SENDME);
    let (sid, msg) = rc.into_streamid_and_msg();
    assert!(sid.is_zero());
    match msg {
        RelayMsg::Sendme(s) => assert_eq!(s.into_tag(), Some(TAG.to_vec())),
        other => panic!("expected Sendme, got {:?}", other),
    }

    // Stream-level: non-zero stream ID, no tag.
    let body = RelayCell::new(StreamId::from(0x2a), RelayMsg::Sendme(Sendme::new_empty()))
        .encode(&mut det_rng())
        .expect("encode must succeed");
    assert_eq!(u16::from_be_bytes([body[9], body[10]]), 0);
    let rc = RelayCell::decode(raw_body(&body)).expect("decode must succeed");
    let (sid, msg) = rc.into_streamid_and_msg();
    assert_eq!(u16::from(sid), 0x2a);
    match msg {
        RelayMsg::Sendme(s) => assert_eq!(s.into_tag(), None),
        other => panic!("expected Sendme, got {:?}", other),
    }
}
