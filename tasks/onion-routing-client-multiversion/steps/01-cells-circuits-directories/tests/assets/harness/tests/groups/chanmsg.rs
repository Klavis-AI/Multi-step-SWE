//! The bodies of the channel-negotiation messages.
//!
//! tor-spec.txt s4 ("Negotiating and initializing connections"): VERSIONS
//! (s4.1), CERTS (s4.2), AUTH_CHALLENGE (s4.3) and NETINFO (s4.5).

use crate::common::fixture_bytes;
use tor_bytes::Reader;
use tor_cell::chancell::{msg, ChanCmd};

#[test]
fn chanmsg__versions_handshake_encoding_uses_a_two_byte_circid() {
    let v = msg::Versions::new([3_u16, 4, 5]);
    let encoded = v.encode_for_handshake();
    assert_eq!(encoded, fixture_bytes("versions_handshake.bin"));
    assert_eq!(encoded.len(), 11);
    assert_eq!(&encoded[0..3], &[0, 0, 7]);
    assert_eq!(&encoded[3..5], &[0, 6]);
}

#[test]
fn chanmsg__best_shared_link_protocol_picks_the_highest_common_version() {
    let theirs = msg::Versions::new([1_u16, 2, 3, 4, 5]);
    assert_eq!(theirs.best_shared_link_protocol(&[4]), Some(4));
    assert_eq!(theirs.best_shared_link_protocol(&[3, 4, 5]), Some(5));
    assert_eq!(theirs.best_shared_link_protocol(&[2, 7]), Some(2));
    assert_eq!(theirs.best_shared_link_protocol(&[6, 7]), None);
    assert_eq!(theirs.best_shared_link_protocol(&[]), None);
    let empty = msg::Versions::new(Vec::<u16>::new());
    assert_eq!(empty.best_shared_link_protocol(&[4]), None);
}

#[test]
fn chanmsg__certs_cell_round_trips_through_the_fixture_bytes() {
    let cell = fixture_bytes("cell_certs.bin");
    let mut r = Reader::from_slice(&cell[7..]);
    let certs = msg::ChanMsg::take(&mut r, ChanCmd::CERTS).expect("CERTS body must parse");
    assert_eq!(certs.cmd(), ChanCmd::CERTS);
    match &certs {
        msg::ChanMsg::Certs(c) => {
            assert_eq!(
                c.cert_body(tor_cert::CertType::RSA_ID_X509),
                Some(&b"\x01\x02\x03"[..])
            );
            assert_eq!(
                c.cert_body(tor_cert::CertType::IDENTITY_V_SIGNING),
                Some(&b"ABCDEFGH"[..])
            );
        }
        other => panic!("expected CERTS, got {:?}", other.cmd()),
    }
    let mut out = Vec::new();
    certs.write_body_onto(&mut out);
    assert_eq!(out, &cell[7..]);
}

#[test]
fn chanmsg__netinfo_for_client_omits_our_own_addresses() {
    let m = msg::ChanMsg::Netinfo(msg::Netinfo::for_client(Some(
        "10.0.0.200".parse().unwrap(),
    )));
    assert_eq!(m.cmd(), ChanCmd::NETINFO);
    let mut out = Vec::new();
    m.write_body_onto(&mut out);
    // A client sends a zero timestamp, the responder's address, and an
    // empty list of its own addresses.
    assert_eq!(&out[0..4], &[0, 0, 0, 0]);
    assert_eq!(&out[4..10], &[0x04, 4, 10, 0, 0, 200]);
    assert_eq!(out[10], 0);
    assert_eq!(out.len(), 11);
}

#[test]
fn chanmsg__netinfo_for_relay_lists_its_own_addresses() {
    let mine: Vec<std::net::IpAddr> = vec!["10.0.0.1".parse().unwrap(), "::1".parse().unwrap()];
    let m = msg::Netinfo::for_relay(0x6000_0000, Some("10.0.0.200".parse().unwrap()), mine);
    let mut out = Vec::new();
    msg::ChanMsg::Netinfo(m).write_body_onto(&mut out);
    assert_eq!(&out[0..4], &[0x60, 0, 0, 0]);
    assert_eq!(&out[4..10], &[0x04, 4, 10, 0, 0, 200]);
    assert_eq!(out[10], 2);
    assert_eq!(&out[11..17], &[0x04, 4, 10, 0, 0, 1]);
    assert_eq!(&out[17..19], &[0x06, 16]);
    assert_eq!(out.len(), 35);
}

#[test]
fn chanmsg__vpadding_length_is_the_body_length() {
    for n in [0_u16, 1, 250] {
        let mut out = Vec::new();
        msg::ChanMsg::VPadding(msg::VPadding::new(n)).write_body_onto(&mut out);
        assert_eq!(out.len(), n as usize);
        assert!(out.iter().all(|b| *b == 0));
    }
    let mut out = Vec::new();
    msg::ChanMsg::Padding(msg::Padding::new()).write_body_onto(&mut out);
    assert_eq!(out.len(), 0);
}

#[test]
fn chanmsg__auth_challenge_and_authenticate_bodies() {
    let ac = msg::AuthChallenge::new([0x5A_u8; 32], vec![1_u16, 3]);
    let mut out = Vec::new();
    msg::ChanMsg::AuthChallenge(ac).write_body_onto(&mut out);
    assert_eq!(&out[0..32], &[0x5A_u8; 32][..]);
    assert_eq!(&out[32..34], &[0, 2]);
    assert_eq!(&out[34..38], &[0, 1, 0, 3]);
    assert_eq!(out.len(), 38);

    let au = msg::Authenticate::new(3_u16, &b"body"[..]);
    let mut out = Vec::new();
    msg::ChanMsg::Authenticate(au).write_body_onto(&mut out);
    assert_eq!(&out[0..2], &[0, 3]);
    assert_eq!(&out[2..4], &[0, 4]);
    assert_eq!(&out[4..], b"body");
}
