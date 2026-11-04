//! Group `resolvequery` - the question half of a resolve exchange.
//!
//! Graded through `tor_cell::relaycell::msg::{Body, RelayMsg, Resolve}` and
//! `tor_cell::relaycell::{RelayCell, StreamId}`. An application that asks the
//! client to resolve a name has that request turned into a relay message, and
//! a forward lookup and a reverse lookup are spelled differently on the wire:
//! this group pins both spellings, and pins the message inside the cell that
//! carries it.

use std::net::{IpAddr, Ipv4Addr, Ipv6Addr};

use tor_cell::relaycell::msg::{Body, RelayMsg, Resolve};

use rand::{rngs::StdRng, SeedableRng};

/// The one fixture helper this group needs, kept local on purpose.
///
/// Reaching into the shared fixture module would compile all of it into this
/// gate group's attribution target, and that module is shared with the scored
/// groups, so it legitimately uses APIs this step introduces. The group would
/// then veto a candidate over code it never calls. `StdRng` is a `CryptoRng`,
/// which the cell encoder requires, and an explicit seed keeps the padding
/// bytes identical from run to run.
fn seeded_rng(seed: u64) -> StdRng {
    StdRng::seed_from_u64(seed)
}
use tor_cell::relaycell::{RelayCell, StreamId};

/// The query bytes a `Resolve` puts on the wire, terminator included.
fn wire(msg: &Resolve) -> Vec<u8> {
    let mut w = Vec::new();
    msg.clone().encode_onto(&mut w);
    w
}

/// The query bytes inside a `Resolve`, read back by decoding its own output.
///
/// Reading the bytes back through the decoder, rather than reaching into the
/// struct, is what makes this an assertion about the wire format.
fn query_of(msg: &Resolve) -> Vec<u8> {
    let w = wire(msg);
    let mut r = tor_bytes::Reader::from_slice(&w[..]);
    let decoded = Resolve::decode_from_reader(&mut r).expect("the query was not decodable");
    let mut again = Vec::new();
    decoded.encode_onto(&mut again);
    assert_eq!(again, w, "a query did not survive a round trip");
    r.should_be_exhausted()
        .expect("the decoder left bytes behind after the query");
    w[..w.len() - 1].to_vec()
}

#[test]
fn resolvequery__a_name_is_carried_as_a_nul_terminated_string() {
    let msg = Resolve::new("www.example.com");
    assert_eq!(
        wire(&msg),
        b"www.example.com\0".to_vec(),
        "a forward lookup is not the name plus a NUL"
    );
    assert_eq!(query_of(&msg), b"www.example.com".to_vec());

    // A name with nothing in it is still a name, and costs one byte.
    assert_eq!(wire(&Resolve::new("")), vec![0]);
    assert_eq!(query_of(&Resolve::new("")), Vec::<u8>::new());

    // Names are carried as bytes without any interpretation, so a name that
    // is not a valid hostname still goes out unchanged.
    let odd = Resolve::new("_service._tcp.example");
    assert_eq!(wire(&odd), b"_service._tcp.example\0".to_vec());
    assert_eq!(query_of(&odd), b"_service._tcp.example".to_vec());

    // A name that is longer than a DNS label is not truncated here: the
    // message layer is not the place that decides what a name may be.
    let long = "a".repeat(300);
    assert_eq!(wire(&Resolve::new(&long)).len(), 301);
    assert_eq!(query_of(&Resolve::new(&long)), long.as_bytes().to_vec());
}

#[test]
fn resolvequery__a_reverse_lookup_spells_the_address_out() {
    // IPv4 is reversed by octet and put under in-addr.arpa.
    let v4 = Resolve::new_reverse(&IpAddr::V4(Ipv4Addr::new(1, 2, 3, 4)));
    assert_eq!(query_of(&v4), b"4.3.2.1.in-addr.arpa".to_vec());
    assert_eq!(
        wire(&v4),
        b"4.3.2.1.in-addr.arpa\0".to_vec(),
        "a reverse query is not the name plus a NUL"
    );

    // The last octet comes first, so two addresses that differ only in
    // their last octet differ in the leading position of the query.
    let other = Resolve::new_reverse(&IpAddr::V4(Ipv4Addr::new(1, 2, 3, 5)));
    assert_eq!(query_of(&other), b"5.3.2.1.in-addr.arpa".to_vec());

    // IPv6 is spelled one hexadecimal nybble at a time, low nybble first,
    // with every octet of the address included.
    let v6 = Resolve::new_reverse(&IpAddr::V6("2001:db8::42".parse().expect("bad fixture")));
    let q = String::from_utf8(query_of(&v6)).expect("an IPv6 query was not text");
    assert!(q.ends_with(".ip6.arpa"), "wrong suffix: {}", q);
    assert_eq!(
        q.len(),
        32 * 2 + "ip6.arpa".len(),
        "an IPv6 query is not one nybble per label: {}",
        q
    );
    assert!(q.starts_with("2.4.0.0."), "wrong lead: {}", q);

    // The address is not echoed back; only its reversed digits are.
    assert!(!q.contains("2001:db8"));
    assert_eq!(
        query_of(&Resolve::new_reverse(&IpAddr::V6(
            "::1".parse().expect("bad fixture")
        ))),
        b"1.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.ip6.arpa".to_vec()
    );
}

#[test]
fn resolvequery__the_question_survives_the_cell_that_carries_it() {
    let cell = RelayCell::new(
        StreamId::from(7_u16),
        RelayMsg::Resolve(Resolve::new("www.example.com")),
    );
    // The command a resolve message answers to identifies it to the relay.
    assert_eq!(cell.cmd().to_string(), "RESOLVE");
    match cell.msg() {
        RelayMsg::Resolve(r) => assert_eq!(query_of(r), b"www.example.com".to_vec()),
        other => panic!("the message inside the cell changed: {:?}", other),
    }

    // A resolve message is not a fixed width, so the cell that carries it is
    // exactly the header plus the query, padded out to a relay cell body.
    let raw = cell
        .encode(&mut seeded_rng(0x5e50))
        .expect("the resolve cell did not encode");
    assert_eq!(raw.len(), 509);

    // The header names the stream and declares the length that follows: the
    // command, the reserved "recognized" field, the stream, the digest, and
    // the length, then the query.
    let header = &raw[..11];
    assert_eq!(&header[..1], &[11], "RESOLVE is not command 11");
    assert_eq!(&header[1..3], &[0, 0], "the recognized field is not zero");
    assert_eq!(&header[3..5], &[0, 7], "the stream id in the header is wrong");
    assert_eq!(&header[5..9], &[0, 0, 0, 0], "the digest field is not zero");
    assert_eq!(
        &header[9..11],
        &[0, 16],
        "the declared length is not the query plus its terminator"
    );
    assert_eq!(&raw[11..27], b"www.example.com\0");
    // A short message is followed by four zero bytes and then random padding,
    // so that a reader can tell the end of the body from the filler.
    assert_eq!(
        &raw[27..31],
        &[0, 0, 0, 0],
        "the four bytes after the body must be zero"
    );
    assert!(
        raw[31..].iter().any(|b| *b != 0),
        "the tail of a short relay cell was left unpadded"
    );

    // The whole cell decodes back to the same question and the same stream.
    let mut back = tor_bytes::Reader::from_slice(raw.as_ref());
    let decoded = RelayCell::decode_from_reader(&mut back).expect("the cell did not decode");
    assert_eq!(decoded.cmd().to_string(), "RESOLVE");
    match decoded.msg() {
        RelayMsg::Resolve(r) => assert_eq!(query_of(r), b"www.example.com".to_vec()),
        other => panic!("the round trip changed the message: {:?}", other),
    }
    let (streamid, _) = decoded.into_streamid_and_msg();
    assert_eq!(
        u16::from(streamid),
        7,
        "the stream a resolve belongs to was forgotten"
    );
}

#[test]
fn resolvequery__a_query_is_only_read_up_to_its_terminator() {
    // The decoder stops at the first NUL, so trailing bytes after the
    // terminator are not part of the query.
    let mut bytes = b"example.com\0".to_vec();
    bytes.extend_from_slice(&[1, 2, 3]);
    let mut r = tor_bytes::Reader::from_slice(&bytes[..]);
    let decoded = Resolve::decode_from_reader(&mut r).expect("the query was not decodable");
    assert_eq!(query_of(&decoded), b"example.com".to_vec());
    assert_eq!(
        r.remaining(),
        3,
        "the decoder consumed bytes past the terminator"
    );

    // A query with no terminator at all is rejected rather than read as a
    // complete name. Only accept-versus-reject is graded: `tor_bytes::Error`
    // is graded as a failure and nothing else (step 01, shared `tor-bytes`
    // items), so neither the variant nor its wording is asserted here.
    let unterminated = b"example.com".to_vec();
    let mut r = tor_bytes::Reader::from_slice(&unterminated[..]);
    assert!(
        Resolve::decode_from_reader(&mut r).is_err(),
        "an unterminated query was accepted"
    );
}
