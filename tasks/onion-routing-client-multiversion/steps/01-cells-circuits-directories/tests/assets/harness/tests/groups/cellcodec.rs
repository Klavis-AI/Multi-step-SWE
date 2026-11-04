//! Framing of channel cells on a link-protocol-4 channel.
//!
//! tor-spec.txt s3 ("Cell Packet format"): on a v4+ link the CircID is four
//! bytes, fixed-length cells are 514 bytes in total and variable-length cells
//! carry a two-byte length before their payload.

use crate::common::{body_of_fixed_cell, fixture_bytes, raw_body};
use bytes::BytesMut;
use tor_cell::chancell::codec::ChannelCodec;
use tor_cell::chancell::{msg, ChanCell, ChanCmd, CircId, CELL_DATA_LEN};

fn codec() -> ChannelCodec {
    ChannelCodec::new(4)
}

fn encode(cell: ChanCell) -> Vec<u8> {
    let mut dst = BytesMut::new();
    codec().write_cell(cell, &mut dst).expect("encode failed");
    dst.to_vec()
}

#[test]
fn cellcodec__fixed_cell_is_514_bytes() {
    let body = raw_body(&[0x41_u8; CELL_DATA_LEN]);
    let cell = ChanCell::new(
        CircId::from(0x8000_0001),
        msg::ChanMsg::Relay(msg::Relay::from_raw(body)),
    );
    let out = encode(cell);
    assert_eq!(out.len(), 5 + CELL_DATA_LEN);
    assert_eq!(out.len(), 514);
    assert_eq!(&out[0..4], &[0x80, 0x00, 0x00, 0x01]);
    assert_eq!(out[4], 3);
    assert_eq!(&out[5..], &[0x41_u8; CELL_DATA_LEN][..]);
}

#[test]
fn cellcodec__short_body_is_zero_padded_to_the_cell_length() {
    let cell = ChanCell::new(
        CircId::from(7),
        msg::ChanMsg::Destroy(msg::Destroy::new(msg::DestroyReason::PROTOCOL)),
    );
    let out = encode(cell);
    assert_eq!(out.len(), 514);
    assert_eq!(out[4], 4);
    assert_eq!(out[5], u8::from(msg::DestroyReason::PROTOCOL));
    assert!(out[6..].iter().all(|b| *b == 0), "tail must be zero padding");
}

#[test]
fn cellcodec__var_cell_length_is_two_bytes_before_the_body() {
    let mut certs = msg::Certs::new_empty();
    certs.push_cert_body(tor_cert::CertType::RSA_ID_X509, &b"\x01\x02\x03"[..]);
    certs.push_cert_body(tor_cert::CertType::IDENTITY_V_SIGNING, &b"ABCDEFGH"[..]);
    let out = encode(ChanCell::new(CircId::from(0), msg::ChanMsg::Certs(certs)));
    assert_eq!(out, fixture_bytes("cell_certs.bin"));
    // header: four-byte circid, one-byte command, two-byte length
    assert_eq!(&out[0..5], &[0, 0, 0, 0, 129]);
    let len = u16::from_be_bytes([out[5], out[6]]) as usize;
    assert_eq!(len, out.len() - 7);
    assert_eq!(len, 18);
}

// NOTE: VERSIONS is deliberately not framed through `ChannelCodec` here.
// tor-spec s3 gives VERSIONS a two-byte circuit ID whatever the link version,
// while this codec frames every cell at the negotiated width, so pushing a
// VERSIONS cell through `write_cell` would grade an encoder on a cell the
// codec is never asked to carry. The VERSIONS wire format is graded where it
// is actually produced, in the `chanmsg` group, against the fixture
// `versions_handshake.bin`.

#[test]
fn cellcodec__decodes_fixture_relay_cell() {
    let bytes = fixture_bytes("cell_relay_data.bin");
    let mut src = BytesMut::from(&bytes[..]);
    let cell = codec()
        .decode_cell(&mut src)
        .expect("decode failed")
        .expect("a whole cell was supplied");
    assert_eq!(src.len(), 0, "the codec must consume exactly one cell");
    assert_eq!(cell.circid(), CircId::from(0x8000_0001));
    assert_eq!(cell.msg().cmd(), ChanCmd::RELAY);
    let (_, m) = cell.into_circid_and_msg();
    match m {
        msg::ChanMsg::Relay(r) => {
            assert_eq!(r.into_relay_body(), body_of_fixed_cell(&bytes));
        }
        other => panic!("expected a RELAY cell, got {:?}", other.cmd()),
    }
}

#[test]
fn cellcodec__decodes_fixture_certs_cell() {
    let bytes = fixture_bytes("cell_certs.bin");
    let mut src = BytesMut::from(&bytes[..]);
    let cell = codec().decode_cell(&mut src).unwrap().unwrap();
    assert_eq!(src.len(), 0);
    assert_eq!(cell.circid(), CircId::from(0));
    match cell.into_circid_and_msg().1 {
        msg::ChanMsg::Certs(c) => {
            assert_eq!(
                c.cert_body(tor_cert::CertType::RSA_ID_X509),
                Some(&b"\x01\x02\x03"[..])
            );
            assert_eq!(
                c.cert_body(tor_cert::CertType::IDENTITY_V_SIGNING),
                Some(&b"ABCDEFGH"[..])
            );
            assert_eq!(c.cert_body(tor_cert::CertType::SIGNING_V_TLS_CERT), None);
        }
        other => panic!("expected CERTS, got {:?}", other.cmd()),
    }
}

#[test]
fn cellcodec__decodes_fixture_create2_cell() {
    let bytes = fixture_bytes("cell_create2.bin");
    let mut src = BytesMut::from(&bytes[..]);
    let cell = codec().decode_cell(&mut src).unwrap().unwrap();
    assert_eq!(cell.circid(), CircId::from(5));
    match cell.into_circid_and_msg().1 {
        msg::ChanMsg::Create2(c) => {
            assert_eq!(c.handshake_type(), 2);
            assert_eq!(c.body().len(), 84);
            assert_eq!(c.body(), &bytes[9..93]);
        }
        other => panic!("expected CREATE2, got {:?}", other.cmd()),
    }
}

#[test]
fn cellcodec__incomplete_input_decodes_to_none() {
    let bytes = fixture_bytes("cell_relay_data.bin");
    // fewer bytes than a cell header
    let mut src = BytesMut::from(&bytes[..6]);
    assert!(codec().decode_cell(&mut src).unwrap().is_none());
    assert_eq!(src.len(), 6, "nothing may be consumed");
    // one byte short of a whole fixed-length cell
    let mut src = BytesMut::from(&bytes[..513]);
    let mut c = codec();
    assert!(c.decode_cell(&mut src).unwrap().is_none());
    src.extend_from_slice(&bytes[513..]);
    assert!(c.decode_cell(&mut src).unwrap().is_some());
    // one byte short of a whole variable-length cell
    let certs = fixture_bytes("cell_certs.bin");
    let mut src = BytesMut::from(&certs[..certs.len() - 1]);
    assert!(codec().decode_cell(&mut src).unwrap().is_none());
}

#[test]
fn cellcodec__rejects_cells_whose_circid_the_command_forbids() {
    // NETINFO is a channel-wide cell and must carry circuit id zero.
    let mut netinfo = crate::common::netinfo_cell();
    netinfo[0..4].copy_from_slice(&7_u32.to_be_bytes());
    let mut src = BytesMut::from(&netinfo[..]);
    assert!(codec().decode_cell(&mut src).is_err());

    // A RELAY cell must carry a nonzero circuit id.
    let mut relay = fixture_bytes("cell_relay_data.bin");
    relay[0..4].copy_from_slice(&0_u32.to_be_bytes());
    let mut src = BytesMut::from(&relay[..]);
    assert!(codec().decode_cell(&mut src).is_err());

    // ... and the unmodified fixtures are accepted.
    let mut src = BytesMut::from(&crate::common::netinfo_cell()[..]);
    assert!(codec().decode_cell(&mut src).unwrap().is_some());
}
