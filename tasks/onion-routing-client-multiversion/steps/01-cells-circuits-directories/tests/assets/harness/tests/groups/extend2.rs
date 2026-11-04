//! Group `extend2`: link specifiers, EXTEND2/EXTENDED2 and CREATE2/CREATED2.
//!
//! `fixtures/relay_extend2.bin` and `fixtures/cell_create2.bin` were laid out
//! by hand from the field orders given in tor-spec (see SPEC_NOTES.md).

use std::net::{IpAddr, Ipv4Addr, Ipv6Addr};

use tor_bytes::{Reader, Writer};
use tor_cell::chancell::msg::{ChanMsg, Create2, Created2};
use tor_cell::chancell::ChanCmd;
use tor_cell::relaycell::msg::{Extend2, Extended2, RelayMsg};
use tor_cell::relaycell::RelayCmd;
use tor_linkspec::LinkSpec;
use tor_llcrypto::pk::ed25519::Ed25519Identity;
use tor_llcrypto::pk::rsa::RsaIdentity;

use crate::common::fixture_bytes;

/// The three link specifiers the EXTEND2 fixture carries, in spec order.
fn fixture_linkspecs() -> Vec<LinkSpec> {
    let f = fixture_bytes("relay_extend2.bin");
    // 11 header bytes, then NSPEC=1 byte; the ed25519 key sits at the end of
    // the third link specifier.
    let ed = &f[11 + 1 + 8 + 22 + 2..11 + 1 + 8 + 22 + 2 + 32];
    vec![
        LinkSpec::OrPort(IpAddr::V4(Ipv4Addr::new(10, 0, 0, 7)), 9001),
        LinkSpec::RsaId(RsaIdentity::from_bytes(&[0x33; 20]).unwrap()),
        LinkSpec::Ed25519Id(Ed25519Identity::from_bytes(ed).unwrap()),
    ]
}

fn enc<W: tor_bytes::Writeable>(v: &W) -> Vec<u8> {
    let mut w = Vec::new();
    w.write(v);
    w
}

#[test]
fn extend2__fixture_decodes_to_the_expected_handshake() {
    let f = fixture_bytes("relay_extend2.bin");
    let mut r = Reader::from_slice(&f[11..11 + 0x99]);
    let msg = RelayMsg::decode_from_reader(RelayCmd::EXTEND2, &mut r)
        .expect("the fixture EXTEND2 body must decode");
    match msg {
        RelayMsg::Extend2(e) => {
            assert_eq!(e.handshake_type(), 2, "ntor is handshake type 2");
            assert_eq!(e.handshake().len(), 84);
            assert_eq!(e.handshake(), &f[11 + 0x99 - 84..11 + 0x99]);
        }
        other => panic!("expected Extend2, got {:?}", other),
    }
}

#[test]
fn extend2__constructor_sorts_link_specifiers_and_reproduces_the_fixture() {
    let f = fixture_bytes("relay_extend2.bin");
    let handshake = f[11 + 0x99 - 84..11 + 0x99].to_vec();
    // Deliberately out of order: the constructor must reorder them.
    let mut ls = fixture_linkspecs();
    ls.reverse();
    let e = Extend2::new(ls, 2, handshake);
    let mut body = Vec::new();
    RelayMsg::Extend2(e).encode_onto(&mut body);
    assert_eq!(body, f[11..11 + 0x99].to_vec());
}

#[test]
fn extend2__link_specifier_wire_forms() {
    assert_eq!(
        enc(&LinkSpec::OrPort(IpAddr::V4(Ipv4Addr::new(10, 0, 0, 7)), 9001)),
        vec![0, 6, 10, 0, 0, 7, 0x23, 0x29]
    );
    let mut want_v6 = vec![1, 18];
    want_v6.extend_from_slice(&Ipv6Addr::LOCALHOST.octets());
    want_v6.extend_from_slice(&443u16.to_be_bytes());
    assert_eq!(
        enc(&LinkSpec::OrPort(IpAddr::V6(Ipv6Addr::LOCALHOST), 443)),
        want_v6
    );

    let rsa = RsaIdentity::from_bytes(&[0x33; 20]).unwrap();
    let mut want_rsa = vec![2, 20];
    want_rsa.extend_from_slice(&[0x33; 20]);
    assert_eq!(enc(&LinkSpec::RsaId(rsa)), want_rsa);

    let ed = Ed25519Identity::from_bytes(&[0x44; 32]).unwrap();
    let mut want_ed = vec![3, 32];
    want_ed.extend_from_slice(&[0x44; 32]);
    assert_eq!(enc(&LinkSpec::Ed25519Id(ed)), want_ed);

    assert_eq!(
        enc(&LinkSpec::Unrecognized(99, vec![1, 2, 3])),
        vec![99, 3, 1, 2, 3]
    );
}

#[test]
fn extend2__link_specifiers_sort_into_ipv4_rsa_ed25519_ipv6_order() {
    let ed = Ed25519Identity::from_bytes(&[0x44; 32]).unwrap();
    let rsa = RsaIdentity::from_bytes(&[0x33; 20]).unwrap();
    let mut ls = vec![
        LinkSpec::OrPort(IpAddr::V6(Ipv6Addr::LOCALHOST), 443),
        LinkSpec::Ed25519Id(ed),
        LinkSpec::RsaId(rsa),
        LinkSpec::OrPort(IpAddr::V4(Ipv4Addr::new(10, 0, 0, 7)), 9001),
    ];
    LinkSpec::sort_by_type(&mut ls);
    let types: Vec<u8> = ls.iter().map(|l| enc(l)[0]).collect();
    assert_eq!(types, vec![0, 2, 3, 1]);
}

#[test]
fn extend2__link_specifier_length_must_agree_with_its_type() {
    let mut r = Reader::from_slice(&[0, 7, 10, 0, 0, 7, 0x23, 0x29, 0]);
    r.extract::<LinkSpec>()
        .expect_err("an IPv4 ORPort specifier is exactly six bytes");

    // An unrecognized type has no fixed length, so it is accepted as-is.
    let mut r = Reader::from_slice(&[77, 2, 0xaa, 0xbb]);
    let ls: LinkSpec = r.extract().expect("unknown link specifier types parse");
    assert_eq!(ls, LinkSpec::Unrecognized(77, vec![0xaa, 0xbb]));
}

#[test]
fn extend2__extended2_body_is_a_length_prefixed_handshake() {
    let mut body = Vec::new();
    RelayMsg::Extended2(Extended2::new(vec![0xde, 0xad, 0xbe, 0xef])).encode_onto(&mut body);
    assert_eq!(body, vec![0, 4, 0xde, 0xad, 0xbe, 0xef]);

    let mut r = Reader::from_slice(&body);
    match RelayMsg::decode_from_reader(RelayCmd::EXTENDED2, &mut r).expect("decodes") {
        RelayMsg::Extended2(e) => assert_eq!(e.into_body(), vec![0xde, 0xad, 0xbe, 0xef]),
        other => panic!("expected Extended2, got {:?}", other),
    }
}

#[test]
fn extend2__create2_fixture_carries_type_and_length_prefixed_handshake() {
    let f = fixture_bytes("cell_create2.bin");
    assert_eq!(f[4], u8::from(ChanCmd::CREATE2));
    let mut r = Reader::from_slice(&f[5..]);
    let msg = ChanMsg::take(&mut r, ChanCmd::CREATE2).expect("the fixture CREATE2 must decode");
    match msg {
        ChanMsg::Create2(c) => {
            assert_eq!(c.handshake_type(), 2);
            assert_eq!(c.body(), &f[9..9 + 84]);
        }
        other => panic!("expected Create2, got {:?}", other),
    }

    let mut body = Vec::new();
    ChanMsg::Create2(Create2::new(2, f[9..9 + 84].to_vec())).write_body_onto(&mut body);
    assert_eq!(&body[..], &f[5..5 + 88]);

    let mut body = Vec::new();
    ChanMsg::Created2(Created2::new(vec![7, 7])).write_body_onto(&mut body);
    assert_eq!(body, vec![0, 2, 7, 7]);
}
