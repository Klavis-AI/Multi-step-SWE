//! Group `relaymsg`: per-command relay message bodies.
//!
//! `fixtures/relay_begin.bin` was laid out by hand from the RELAY_BEGIN body
//! grammar in tor-spec (see SPEC_NOTES.md).

use std::net::{IpAddr, Ipv4Addr, Ipv6Addr};

use tor_bytes::Reader;
use tor_cell::relaycell::msg::{
    Begin, BeginFlags, Connected, Data, End, EndReason, RelayMsg, Resolved, ResolvedVal,
    Unrecognized,
};
use tor_cell::relaycell::{RelayCell, RelayCmd, StreamId};

use crate::common::{det_rng, fixture_bytes};

/// Encode one relay message body on its own (no header, no padding).
fn body(msg: RelayMsg) -> Vec<u8> {
    let mut v = Vec::new();
    msg.encode_onto(&mut v);
    v
}

/// Decode one relay message body on its own.
fn parse(cmd: RelayCmd, b: &[u8]) -> tor_bytes::Result<RelayMsg> {
    let mut r = Reader::from_slice(b);
    RelayMsg::decode_from_reader(cmd, &mut r)
}

#[test]
fn relaymsg__begin_body_matches_the_spec_fixture() {
    let fixture = fixture_bytes("relay_begin.bin");
    let b = Begin::new("www.example.com", 443, BeginFlags::from_bits_truncate(3))
        .expect("ascii hostname is legal");
    let got = RelayCell::new(StreamId::from(1), RelayMsg::Begin(b))
        .encode(&mut det_rng())
        .expect("encode must succeed");
    // 11 header bytes plus a 24-byte body: "www.example.com:443\0" + 4 flag bytes.
    assert_eq!(u16::from_be_bytes([got[9], got[10]]), 24);
    assert_eq!(&got[0..35], &fixture[0..35]);
}

#[test]
fn relaymsg__begin_lowercases_the_target_hostname() {
    let b = Begin::new("WWW.Example.COM", 443, BeginFlags::from_bits_truncate(3))
        .expect("ascii hostname is legal");
    assert_eq!(body(RelayMsg::Begin(b)), &fixture_bytes("relay_begin.bin")[11..35]);
}

#[test]
fn relaymsg__begin_brackets_an_ipv6_literal_and_omits_zero_flags() {
    let b = Begin::new("::1", 80, BeginFlags::empty()).expect("ipv6 literal is ascii");
    assert_eq!(body(RelayMsg::Begin(b)), b"[::1]:80\0".to_vec());
}

#[test]
fn relaymsg__begin_rejects_non_ascii_targets_and_unparsable_ports() {
    // Only the rejections are graded, not which error variant carries them.
    Begin::new("ex\u{e4}mple.com", 80, BeginFlags::empty())
        .expect_err("non-ascii address must be refused");

    parse(RelayCmd::BEGIN, b"www.example.com:notaport\0")
        .expect_err("non-numeric port must be refused");
}

#[test]
fn relaymsg__data_body_is_the_payload_verbatim() {
    assert_eq!(body(RelayMsg::Data(Data::new(b"abc"))), b"abc".to_vec());
    match parse(RelayCmd::DATA, b"\x00\xffpayload").expect("data always parses") {
        RelayMsg::Data(d) => assert_eq!(d.as_ref(), b"\x00\xffpayload"),
        other => panic!("expected Data, got {:?}", other),
    }
}

#[test]
fn relaymsg__end_body_is_a_reason_byte_and_an_optional_address() {
    assert_eq!(u8::from(EndReason::MISC), 1);
    assert_eq!(u8::from(EndReason::EXITPOLICY), 4);
    assert_eq!(u8::from(EndReason::NOTDIRECTORY), 14);

    assert_eq!(body(RelayMsg::End(End::new_misc())), vec![1]);
    assert_eq!(
        body(RelayMsg::End(End::new_with_reason(EndReason::TORPROTOCOL))),
        vec![13]
    );

    let e = End::new_exitpolicy(IpAddr::V4(Ipv4Addr::new(10, 0, 0, 1)), 3600);
    assert_eq!(body(RelayMsg::End(e)), vec![4, 10, 0, 0, 1, 0, 0, 0x0e, 0x10]);

    // An empty END body means "misc", per tor's own behaviour.
    match parse(RelayCmd::END, b"").expect("empty end body is legal") {
        RelayMsg::End(e) => assert_eq!(e.reason(), EndReason::MISC),
        other => panic!("expected End, got {:?}", other),
    }
}

#[test]
fn relaymsg__connected_body_distinguishes_ipv4_from_ipv6() {
    assert_eq!(body(RelayMsg::Connected(Connected::new_empty())), Vec::<u8>::new());

    let v4 = Connected::new_with_addr(IpAddr::V4(Ipv4Addr::new(192, 0, 2, 9)), 256);
    assert_eq!(
        body(RelayMsg::Connected(v4)),
        vec![192, 0, 2, 9, 0, 0, 1, 0]
    );

    let v6 = Connected::new_with_addr(IpAddr::V6(Ipv6Addr::LOCALHOST), 1);
    let enc = body(RelayMsg::Connected(v6));
    assert_eq!(&enc[0..4], &[0, 0, 0, 0], "ipv4 slot is zero for ipv6");
    assert_eq!(enc[4], 6, "address type 6 marks an IPv6 answer");
    assert_eq!(&enc[5..21], &Ipv6Addr::LOCALHOST.octets()[..]);
    assert_eq!(&enc[21..25], &1u32.to_be_bytes());
    assert_eq!(enc.len(), 25);
}

#[test]
fn relaymsg__resolved_answers_are_type_length_value_plus_ttl() {
    let mut r = Resolved::new_empty();
    r.add_answer(ResolvedVal::Ip(IpAddr::V4(Ipv4Addr::new(1, 2, 3, 4))), 60);
    r.add_answer(ResolvedVal::Hostname(b"host.example".to_vec()), 120);
    let enc = body(RelayMsg::Resolved(r));
    let mut want = vec![4, 4, 1, 2, 3, 4, 0, 0, 0, 60];
    want.extend_from_slice(&[0, 12]);
    want.extend_from_slice(b"host.example");
    want.extend_from_slice(&120u32.to_be_bytes());
    assert_eq!(enc, want);

    // A declared length that disagrees with the address type is rejected.
    parse(RelayCmd::RESOLVED, &[4, 5, 1, 2, 3, 4, 5, 0, 0, 0, 0])
        .expect_err("an IPv4 answer must be four bytes long");
}

#[test]
fn relaymsg__body_less_commands_and_unrecognized_commands() {
    for m in [RelayMsg::Drop, RelayMsg::Truncate, RelayMsg::BeginDir] {
        assert_eq!(body(m), Vec::<u8>::new());
    }
    assert_eq!(
        parse(RelayCmd::DROP, b"").expect("drop parses").cmd(),
        RelayCmd::DROP
    );

    let u = Unrecognized::new(RelayCmd::from(250), b"opaque".to_vec());
    assert_eq!(u.cmd(), RelayCmd::from(250));
    assert_eq!(body(RelayMsg::Unrecognized(u)), b"opaque".to_vec());

    match parse(RelayCmd::from(250), b"opaque").expect("unknown commands parse") {
        RelayMsg::Unrecognized(u) => {
            assert_eq!(u.cmd(), RelayCmd::from(250));
            assert_eq!(body(RelayMsg::Unrecognized(u)), b"opaque".to_vec());
        }
        other => panic!("expected Unrecognized, got {:?}", other),
    }
}
