//! Group: socksclient — speaking SOCKS to a pluggable transport.
//!
//! A managed proxy is reached over SOCKS, so the client now needs the
//! initiator side of the handshake as well as the responder side it already
//! had.  The byte strings asserted on here come from the SOCKS protocol
//! definitions (RFC 1928 and RFC 1929 for version 5, the SOCKS4/4a memos for
//! version 4), not from arti's own encoder, so the group grades the wire
//! format rather than a round trip.

use tor_socksproto::{
    Action, SocksAddr, SocksAuth, SocksClientHandshake, SocksCmd, SocksRequest, SocksStatus,
    SocksVersion,
};

/// Build a request, panicking if the combination is refused.
fn request(v: SocksVersion, addr: SocksAddr, port: u16, auth: SocksAuth) -> SocksRequest {
    SocksRequest::new(v, SocksCmd::CONNECT, addr, port, auth).expect("request should be accepted")
}

/// A hostname address.
fn host(s: &str) -> SocksAddr {
    SocksAddr::Hostname(s.to_string().try_into().expect("hostname is short enough"))
}

/// An IP address.
fn ip(s: &str) -> SocksAddr {
    SocksAddr::Ip(s.parse().expect("address should parse"))
}

/// Advance a handshake and unwrap both the truncation and the error layer.
fn step(hs: &mut SocksClientHandshake, input: &[u8]) -> Action {
    hs.handshake(input)
        .expect("input should be complete")
        .expect("handshake should not fail")
}

#[test]
fn socksclient__version_five_opens_by_offering_its_methods() {
    let mut hs = SocksClientHandshake::new(request(
        SocksVersion::V5,
        host("example.com"),
        443,
        SocksAuth::NoAuth,
    ));
    let a = step(&mut hs, &[]);
    // Version, method count, then that many method bytes; 0x00 is "no
    // authentication required".
    assert_eq!(a.reply, vec![5, 1, 0]);
    assert_eq!(a.drain, 0, "nothing has arrived yet to consume");
    assert!(!a.finished);
}

#[test]
fn socksclient__a_username_makes_the_client_offer_two_methods() {
    let mut hs = SocksClientHandshake::new(request(
        SocksVersion::V5,
        host("example.com"),
        443,
        SocksAuth::Username(b"user".to_vec(), b"pass".to_vec()),
    ));
    let a = step(&mut hs, &[]);
    // 0x02 is username/password; the client still accepts 0x00 as a fallback.
    assert_eq!(a.reply, vec![5, 2, 2, 0]);
    assert!(!a.finished);
}

#[test]
fn socksclient__the_username_block_is_two_length_prefixed_strings() {
    let mut hs = SocksClientHandshake::new(request(
        SocksVersion::V5,
        host("example.com"),
        443,
        SocksAuth::Username(b"user".to_vec(), b"secret".to_vec()),
    ));
    let _ = step(&mut hs, &[]);
    // The proxy selects username/password.
    let a = step(&mut hs, &[5, 2]);
    assert_eq!(a.drain, 2);
    let mut expected = vec![1_u8, 4];
    expected.extend_from_slice(b"user");
    expected.push(6);
    expected.extend_from_slice(b"secret");
    assert_eq!(a.reply, expected);
    assert!(!a.finished);
}

#[test]
fn socksclient__the_version_five_command_carries_the_address_and_port() {
    let mut hs = SocksClientHandshake::new(request(
        SocksVersion::V5,
        host("bridge.example"),
        9001,
        SocksAuth::NoAuth,
    ));
    let _ = step(&mut hs, &[]);
    // The proxy selects "no authentication".
    let a = step(&mut hs, &[5, 0]);
    assert_eq!(a.drain, 2);

    let mut expected = vec![5_u8, 1, 0, 3, 14];
    expected.extend_from_slice(b"bridge.example");
    expected.extend_from_slice(&9001_u16.to_be_bytes());
    assert_eq!(a.reply, expected, "address type 3 is a length-prefixed name");
    assert!(!a.finished);

    // A numeric address uses address type 1 and four raw bytes instead.
    let mut numeric = SocksClientHandshake::new(request(
        SocksVersion::V5,
        ip("198.51.100.5"),
        9001,
        SocksAuth::NoAuth,
    ));
    let _ = step(&mut numeric, &[]);
    let a = step(&mut numeric, &[5, 0]);
    assert_eq!(a.reply, vec![5, 1, 0, 1, 198, 51, 100, 5, 0x23, 0x29]);
}

#[test]
fn socksclient__the_final_version_five_reply_is_reported_to_the_caller() {
    let mut hs = SocksClientHandshake::new(request(
        SocksVersion::V5,
        host("bridge.example"),
        9001,
        SocksAuth::NoAuth,
    ));
    let _ = step(&mut hs, &[]);
    let _ = step(&mut hs, &[5, 0]);

    // Status 0x00, reserved byte, bound address 203.0.113.9, bound port 1080.
    let reply = [5_u8, 0, 0, 1, 203, 0, 113, 9, 0x04, 0x38];
    let a = step(&mut hs, &reply);
    assert_eq!(a.drain, reply.len());
    assert!(a.reply.is_empty(), "nothing left to send");
    assert!(a.finished);

    let r = hs.into_reply().expect("a finished handshake has a reply");
    assert_eq!(r.status(), SocksStatus::SUCCEEDED);
    assert_eq!(r.addr(), &ip("203.0.113.9"));
    assert_eq!(r.port(), 1080);
}

#[test]
fn socksclient__version_four_sends_one_message_and_terminates_the_user_id() {
    let mut hs = SocksClientHandshake::new(request(
        SocksVersion::V4,
        ip("198.51.100.5"),
        9001,
        SocksAuth::Socks4(b"tor".to_vec()),
    ));
    let a = step(&mut hs, &[]);
    let mut expected = vec![4_u8, 1, 0x23, 0x29, 198, 51, 100, 5];
    expected.extend_from_slice(b"tor");
    expected.push(0);
    assert_eq!(a.reply, expected);
    assert_eq!(a.drain, 0);
    assert!(!a.finished);
}

#[test]
fn socksclient__version_four_a_signals_a_name_with_an_impossible_address() {
    let mut hs = SocksClientHandshake::new(request(
        SocksVersion::V4,
        host("bridge.example"),
        9001,
        SocksAuth::NoAuth,
    ));
    let a = step(&mut hs, &[]);
    let mut expected = vec![4_u8, 1, 0x23, 0x29, 0, 0, 0, 1, 0];
    expected.extend_from_slice(b"bridge.example");
    expected.push(0);
    assert_eq!(
        a.reply, expected,
        "0.0.0.1 marks a 4a request and the name follows the user id"
    );
}

#[test]
fn socksclient__version_four_status_bytes_map_onto_the_version_five_codes() {
    // A granted SOCKS4 reply: null version byte, 0x5A, bound port, bound address.
    let mut ok = SocksClientHandshake::new(request(
        SocksVersion::V4,
        ip("198.51.100.5"),
        9001,
        SocksAuth::NoAuth,
    ));
    let _ = step(&mut ok, &[]);
    let a = step(&mut ok, &[0, 0x5A, 0x04, 0x38, 203, 0, 113, 9]);
    assert_eq!(a.drain, 8);
    assert!(a.finished);
    let r = ok.into_reply().expect("finished");
    assert_eq!(r.status(), SocksStatus::SUCCEEDED);
    assert_eq!(r.port(), 1080);
    assert_eq!(r.addr(), &ip("203.0.113.9"));

    // A rejected reply is reported as a general failure.
    let mut bad = SocksClientHandshake::new(request(
        SocksVersion::V4,
        ip("198.51.100.5"),
        9001,
        SocksAuth::NoAuth,
    ));
    let _ = step(&mut bad, &[]);
    let _ = step(&mut bad, &[0, 0x5B, 0, 0, 0, 0, 0, 0]);
    assert_eq!(
        bad.into_reply().expect("finished").status(),
        SocksStatus::GENERAL_FAILURE
    );
}

#[test]
fn socksclient__a_short_reply_is_reported_as_truncated_and_consumes_nothing() {
    let mut hs = SocksClientHandshake::new(request(
        SocksVersion::V5,
        host("bridge.example"),
        9001,
        SocksAuth::NoAuth,
    ));
    let _ = step(&mut hs, &[]);
    let _ = step(&mut hs, &[5, 0]);

    // Only part of the final reply has arrived.
    assert!(
        hs.handshake(&[5, 0, 0, 1, 203]).is_err(),
        "an incomplete reply must be reported as truncated, not as a failure"
    );

    // The handshake is still usable once the rest turns up.
    let a = step(&mut hs, &[5, 0, 0, 1, 203, 0, 113, 9, 0x04, 0x38]);
    assert!(a.finished);
    assert_eq!(
        hs.into_reply().expect("finished").status(),
        SocksStatus::SUCCEEDED
    );
}
