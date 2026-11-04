//! Group `resolvedcell` - the RESOLVE / RESOLVED relay messages.
//!
//! Graded through `tor_cell::relaycell::msg::{Resolve, Resolved, ResolvedVal}`
//! and the `RelayCell` framing around them. A DNS lookup made through the Tor
//! network travels as a RESOLVE cell and comes back as a RESOLVED cell holding
//! a list of answers with their time-to-live values, so both the wire form and
//! the answer list are graded here.

use std::net::{IpAddr, Ipv4Addr, Ipv6Addr};
use tor_bytes::{Readable, Reader, Writeable};
use tor_cell::relaycell::msg::{Body, RelayMsg, Resolve, Resolved, ResolvedVal};
use tor_cell::relaycell::{RelayCell, RelayCmd, StreamId};

/// Round-trip a relay message through the wire form of a RELAY cell.
fn round_trip(msg: RelayMsg, stream: u16) -> RelayMsg {
    let cell = RelayCell::new(StreamId::from(stream), msg);
    let mut rng = crate::common::seeded_rng(0x5e501);
    let raw = cell.encode(&mut rng).expect("encoding failed");
    let decoded = RelayCell::decode(raw).expect("decoding failed");
    let (sid, msg) = decoded.into_streamid_and_msg();
    assert_eq!(u16::from(sid), stream, "the stream id did not survive");
    msg
}

/// The NUL-terminated query a `Resolve` message puts on the wire.
fn query_of(msg: &RelayMsg) -> Vec<u8> {
    let mut w = Vec::new();
    match msg {
        RelayMsg::Resolve(r) => r.clone().encode_onto(&mut w),
        other => panic!("expected a RESOLVE message, got {:?}", other),
    }
    let mut r = Reader::from_slice(&w);
    let q = r.take_until(0).expect("query is not NUL-terminated");
    assert_eq!(r.remaining(), 0, "nothing may follow the NUL");
    q.to_vec()
}

#[test]
fn resolvedcell__a_resolve_message_carries_a_nul_terminated_name() {
    let msg = RelayMsg::Resolve(Resolve::new("www.example.com"));
    assert_eq!(msg.cmd(), RelayCmd::RESOLVE);

    let got = round_trip(msg, 3);
    assert_eq!(got.cmd(), RelayCmd::RESOLVE);
    assert_eq!(query_of(&got), b"www.example.com".to_vec());
}

#[test]
fn resolvedcell__a_reverse_lookup_query_is_formed_from_the_address() {
    // A reverse lookup for 1.2.3.4 asks for the PTR record of
    // 4.3.2.1.in-addr.arpa: the octets are reversed.
    let v4 = RelayMsg::Resolve(Resolve::new_reverse(&IpAddr::V4(Ipv4Addr::new(1, 2, 3, 4))));
    let got = round_trip(v4, 9);
    assert_eq!(query_of(&got), b"4.3.2.1.in-addr.arpa".to_vec());

    // For IPv6 the address is written as nybbles, low nybble of each octet
    // first, under ip6.arpa: 32 labels of one hex digit each.
    let v6_addr: Ipv6Addr = "2001:db8::42".parse().expect("bad fixture address");
    let v6 = RelayMsg::Resolve(Resolve::new_reverse(&IpAddr::V6(v6_addr)));
    let got = round_trip(v6, 9);
    let query = query_of(&got);
    let text = String::from_utf8(query).expect("the IPv6 query is not ASCII");
    assert!(text.ends_with(".ip6.arpa"), "got {:?}", text);
    // Thirty-two "<digit>." labels, then the bare suffix.
    assert_eq!(text.len(), 32 * 2 + "ip6.arpa".len());
    // 2001:db8::42 ends in the octet 0x42, whose low nybble leads the query.
    assert!(text.starts_with("2.4.0.0."), "got {:?}", text);
}

#[test]
fn resolvedcell__the_answer_tags_match_the_wire_encoding() {
    let v4 = ResolvedVal::Ip(IpAddr::V4(Ipv4Addr::new(1, 2, 3, 4)));
    let v6 = ResolvedVal::Ip(IpAddr::V6("2001:db8::1".parse().expect("bad fixture")));
    let name = ResolvedVal::Hostname(b"example.com".to_vec());

    // Compare the rendered bytes rather than depending on a write method
    // that takes ownership, so the values survive for later assertions.
    let enc = |v: &ResolvedVal| -> Vec<u8> {
        let mut w = Vec::new();
        v.clone().write_onto(&mut w);
        w
    };

    assert_eq!(enc(&v4), vec![4, 4, 1, 2, 3, 4]);
    assert_eq!(enc(&name), {
        let mut v = vec![0, 11];
        v.extend_from_slice(b"example.com");
        v
    });
    assert_eq!(enc(&v6).len(), 2 + 16);
    assert_eq!(&enc(&v6)[..2], &[6, 16][..]);

    // The two Tor-specific error answers carry no payload.
    assert_eq!(enc(&ResolvedVal::TransientError), vec![0xF0, 0]);
    assert_eq!(enc(&ResolvedVal::NontransientError), vec![0xF1, 0]);

    // Anything else is preserved verbatim under its own tag.
    assert_eq!(
        enc(&ResolvedVal::Unrecognized(0x77, vec![9, 9])),
        vec![0x77, 2, 9, 9]
    );

    // The tagged forms are equal to themselves and unequal to each other.
    assert_eq!(v4, ResolvedVal::Ip(IpAddr::V4(Ipv4Addr::new(1, 2, 3, 4))));
    assert_ne!(v4, name);
    assert_ne!(ResolvedVal::TransientError, ResolvedVal::NontransientError);
}

#[test]
fn resolvedcell__a_fixed_length_answer_of_the_wrong_length_is_refused() {
    // An IPv4 answer is tagged 4 and declared as 4 bytes long; a different
    // declared length means the cell is malformed.
    let bad = [4_u8, 3, 1, 2, 3, 0, 0, 0, 0];
    let mut r = Reader::from_slice(&bad);
    let e = ResolvedVal::take_from(&mut r).expect_err("a short IPv4 answer was accepted");
    // The wording is a diagnostic and is not graded. What is graded is that a
    // declared length that disagrees with the tag is a *malformed object*
    // (`BadMessage`) and not a "come back with more bytes" (`Truncated`).
    assert!(
        matches!(e, tor_bytes::Error::BadMessage(_)),
        "expected BadMessage, got {:?}",
        e
    );

    // The same for IPv6, whose answers are 16 bytes.
    let mut short6 = vec![6_u8, 4];
    short6.extend_from_slice(&[0_u8; 4]);
    short6.extend_from_slice(&[0_u8; 4]);
    let mut r = Reader::from_slice(&short6);
    match ResolvedVal::take_from(&mut r) {
        Ok(v) => panic!("a truncated IPv6 answer was accepted: {:?}", v),
        Err(e) => assert!(
            matches!(e, tor_bytes::Error::BadMessage(_)),
            "expected BadMessage, got {:?}",
            e
        ),
    }
}

#[test]
fn resolvedcell__an_empty_resolved_message_round_trips_as_no_answers() {
    let msg = RelayMsg::Resolved(Resolved::new_empty());
    assert_eq!(msg.cmd(), RelayCmd::RESOLVED);

    let got = round_trip(msg, 0);
    match got {
        RelayMsg::Resolved(r) => assert!(r.into_answers().is_empty()),
        other => panic!("expected a RESOLVED message, got {:?}", other),
    }
}

#[test]
fn resolvedcell__the_answers_and_their_ttls_survive_the_cell() {
    let mut res = Resolved::new_empty();
    res.add_answer(
        ResolvedVal::Ip(IpAddr::V4(Ipv4Addr::new(10, 0, 0, 1))),
        3600,
    );
    res.add_answer(
        ResolvedVal::Ip(IpAddr::V6("2001:db8::99".parse().expect("bad fixture"))),
        60,
    );
    res.add_answer(ResolvedVal::Hostname(b"a.example".to_vec()), 7);

    let got = round_trip(RelayMsg::Resolved(res), 41);
    match got {
        RelayMsg::Resolved(r) => {
            let answers = r.into_answers();
            assert_eq!(answers.len(), 3, "an answer was lost in the cell");
            assert_eq!(
                answers[0],
                (
                    ResolvedVal::Ip(IpAddr::V4(Ipv4Addr::new(10, 0, 0, 1))),
                    3600
                )
            );
            assert_eq!(
                answers[1],
                (
                    ResolvedVal::Ip(IpAddr::V6("2001:db8::99".parse().expect("bad fixture"))),
                    60
                )
            );
            assert_eq!(
                answers[2],
                (ResolvedVal::Hostname(b"a.example".to_vec()), 7)
            );
        }
        other => panic!("expected a RESOLVED message, got {:?}", other),
    }

    // An error answer is a one-element answer list, not an empty one.
    let got = round_trip(RelayMsg::Resolved(Resolved::new_err(true, 30)), 41);
    match got {
        RelayMsg::Resolved(r) => {
            assert_eq!(r.into_answers(), vec![(ResolvedVal::TransientError, 30)])
        }
        other => panic!("expected a RESOLVED message, got {:?}", other),
    }
    let got = round_trip(RelayMsg::Resolved(Resolved::new_err(false, 30)), 41);
    match got {
        RelayMsg::Resolved(r) => {
            assert_eq!(r.into_answers(), vec![(ResolvedVal::NontransientError, 30)])
        }
        other => panic!("expected a RESOLVED message, got {:?}", other),
    }
}
