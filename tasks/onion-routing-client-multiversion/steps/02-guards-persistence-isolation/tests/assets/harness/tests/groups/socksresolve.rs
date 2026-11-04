//! Group `socksresolve` - SOCKS `RESOLVE` and `RESOLVE_PTR`.
//!
//! Graded through `tor_socksproto::{SocksHandshake, Action, SocksRequest,
//! SocksVersion, SocksCmd, SocksAddr, SocksAuth, SocksStatus, Error}`.
//! The two Tor-only commands carry no port, so a handshake that insists on a
//! nonzero port rejects every name lookup; the command numbers and the reply
//! framing are what a SOCKS client on the other end depends on.

use std::convert::TryFrom;
use std::net::{IpAddr, Ipv4Addr};
use tor_socksproto::{
    Error, SocksAddr, SocksAuth, SocksCmd, SocksHandshake, SocksRequest, SocksStatus, SocksVersion,
};

/// Run a SOCKS5 greeting that offers "no authentication" and check the reply.
fn greet5(hs: &mut SocksHandshake) {
    let act = hs
        .handshake(&[5, 1, 0])
        .expect("the SOCKS5 greeting was rejected");
    assert_eq!(act.drain, 3, "the greeting is three bytes long");
    assert_eq!(act.reply, vec![5, 0], "server must choose 'no auth' (0x00)");
    assert!(!act.finished, "the greeting does not finish a handshake");
}

/// Complete a handshake and return the request it produced.
fn finish(hs: SocksHandshake) -> SocksRequest {
    assert!(hs.finished(), "handshake did not report itself finished");
    hs.into_request().expect("no request after a finished handshake")
}

/// A SOCKS5 request body: version, command, reserved, address, port.
fn s5_hostname(cmd: u8, host: &str, port: u16) -> Vec<u8> {
    let mut v = vec![5, cmd, 0, 3, host.len() as u8];
    v.extend_from_slice(host.as_bytes());
    v.extend_from_slice(&port.to_be_bytes());
    v
}

#[test]
fn socksresolve__a_socks5_resolve_request_needs_no_port() {
    let mut hs = SocksHandshake::new();
    greet5(&mut hs);

    let body = s5_hostname(0xF0, "example.com", 0);
    let act = hs
        .handshake(&body)
        .expect("a RESOLVE request with port 0 was rejected");
    assert_eq!(act.drain, body.len());
    assert!(act.finished, "the request finishes the handshake");
    assert!(act.reply.is_empty(), "a request needs no immediate reply");

    let req = finish(hs);
    assert_eq!(req.version(), SocksVersion::V5);
    assert_eq!(req.command(), SocksCmd::RESOLVE);
    assert_eq!(req.port(), 0, "a RESOLVE request carries no port");
    assert_eq!(req.auth(), &SocksAuth::NoAuth);
    match req.addr() {
        SocksAddr::Hostname(h) => assert_eq!(h.as_ref(), "example.com"),
        other => panic!("expected a hostname, got {:?}", other),
    }
    assert_eq!(req.addr().to_string(), "example.com");
}

#[test]
fn socksresolve__a_socks5_reverse_resolve_takes_an_address() {
    let mut hs = SocksHandshake::new();
    greet5(&mut hs);

    // RESOLVE_PTR asks for the name of an address, so the address is the
    // payload and there is still no port.
    let body = vec![5, 0xF1, 0, 1, 127, 0, 0, 1, 0, 0];
    let act = hs
        .handshake(&body)
        .expect("a RESOLVE_PTR request with port 0 was rejected");
    assert_eq!(act.drain, body.len());
    assert!(act.finished);

    let req = finish(hs);
    assert_eq!(req.command(), SocksCmd::RESOLVE_PTR);
    assert_eq!(req.port(), 0);
    assert_eq!(
        req.addr(),
        &SocksAddr::Ip(IpAddr::V4(Ipv4Addr::new(127, 0, 0, 1)))
    );
}

#[test]
fn socksresolve__a_socks4a_resolve_request_is_understood() {
    // SOCKS4a: version, command, port, an address whose first three octets
    // are zero, a NUL-terminated user string, then a NUL-terminated hostname.
    let mut body = vec![4, 0xF0, 0, 0, 0, 0, 0, 1];
    body.extend_from_slice(b"me\0");
    body.extend_from_slice(b"example.com\0");

    let mut hs = SocksHandshake::new();
    let act = hs
        .handshake(&body)
        .expect("a SOCKS4a RESOLVE request was rejected");
    assert_eq!(act.drain, body.len());
    assert!(act.finished);

    let req = finish(hs);
    assert_eq!(req.version(), SocksVersion::V4);
    assert_eq!(req.command(), SocksCmd::RESOLVE);
    assert_eq!(req.port(), 0);
    assert_eq!(req.auth(), &SocksAuth::Socks4(b"me".to_vec()));
    match req.addr() {
        SocksAddr::Hostname(h) => assert_eq!(h.as_ref(), "example.com"),
        other => panic!("expected a hostname, got {:?}", other),
    }
}

#[test]
fn socksresolve__a_connect_request_still_requires_a_port() {
    // Dropping the port requirement must apply to the lookup commands only:
    // connecting to port 0 remains meaningless.
    let mut hs = SocksHandshake::new();
    greet5(&mut hs);
    let err = match hs.handshake(&[5, 1, 0, 1, 127, 0, 0, 1, 0, 0]) {
        Ok(_) => panic!("a CONNECT request to port 0 was accepted"),
        Err(e) => e,
    };
    assert!(
        matches!(err, Error::Syntax),
        "expected a syntax error, got {:?}",
        err
    );

    // A CONNECT with a real port is still fine.
    let mut hs = SocksHandshake::new();
    greet5(&mut hs);
    hs.handshake(&s5_hostname(1, "example.com", 443))
        .expect("a normal CONNECT request was rejected");
    let req = finish(hs);
    assert_eq!(req.command(), SocksCmd::CONNECT);
    assert_eq!(req.port(), 443);
}

#[test]
fn socksresolve__the_version_byte_maps_onto_the_version_type() {
    assert_eq!(u8::from(SocksVersion::V4), 4);
    assert_eq!(u8::from(SocksVersion::V5), 5);
    assert_eq!(SocksVersion::try_from(4).expect("4 is SOCKS4"), SocksVersion::V4);
    assert_eq!(SocksVersion::try_from(5).expect("5 is SOCKS5"), SocksVersion::V5);

    for bad in [0_u8, 1, 3, 6, 255] {
        match SocksVersion::try_from(bad) {
            Err(Error::BadProtocol(v)) => assert_eq!(v, bad),
            other => panic!("version {} was not refused: {:?}", bad, other),
        }
    }

    // The same refusal reaches the handshake.
    let err = match SocksHandshake::new().handshake(&[6, 1, 0]) {
        Ok(_) => panic!("a handshake with version 6 was accepted"),
        Err(e) => e,
    };
    match err {
        Error::BadProtocol(v) => assert_eq!(v, 6),
        other => panic!("expected BadProtocol(6), got {:?}", other),
    }
}

#[test]
fn socksresolve__the_resolve_commands_use_their_assigned_numbers() {
    assert_eq!(u8::from(SocksCmd::CONNECT), 1);
    assert_eq!(u8::from(SocksCmd::BIND), 2);
    assert_eq!(u8::from(SocksCmd::UDP_ASSOCIATE), 3);
    assert_eq!(u8::from(SocksCmd::RESOLVE), 0xF0);
    assert_eq!(u8::from(SocksCmd::RESOLVE_PTR), 0xF1);
    assert_eq!(SocksCmd::from(0xF0_u8), SocksCmd::RESOLVE);
    assert_eq!(SocksCmd::from(0xF1_u8), SocksCmd::RESOLVE_PTR);
    assert_eq!(SocksCmd::RESOLVE.to_string(), "RESOLVE");
    assert_eq!(SocksCmd::RESOLVE_PTR.to_string(), "RESOLVE_PTR");

    // BIND and UDP_ASSOCIATE are named but not implemented, and the handshake
    // has to say so rather than treat them as a lookup.
    let mut hs = SocksHandshake::new();
    greet5(&mut hs);
    let err = match hs.handshake(&[5, 2, 0, 1, 127, 0, 0, 1, 0, 80]) {
        Ok(_) => panic!("a BIND request was accepted"),
        Err(e) => e,
    };
    assert!(
        matches!(err, Error::NoSupport),
        "expected NoSupport, got {:?}",
        err
    );
}

#[test]
fn socksresolve__a_resolve_reply_carries_the_answer_address() {
    let mut hs = SocksHandshake::new();
    greet5(&mut hs);
    hs.handshake(&s5_hostname(0xF0, "example.com", 0))
        .expect("RESOLVE request rejected");
    let req = finish(hs);

    // SOCKS5 reply: version, status, reserved, address, port.
    let answer = SocksAddr::Ip(IpAddr::V4(Ipv4Addr::new(1, 2, 3, 4)));
    assert_eq!(
        req.reply(SocksStatus::SUCCEEDED, Some(&answer)),
        vec![5, 0, 0, 1, 1, 2, 3, 4, 0, 0]
    );
    // With no address, the reply names the unspecified IPv4 address.
    assert_eq!(
        req.reply(SocksStatus::SUCCEEDED, None),
        vec![5, 0, 0, 1, 0, 0, 0, 0, 0, 0]
    );
    // A failure keeps the framing but changes the status byte.
    assert_eq!(
        req.reply(SocksStatus::HOST_UNREACHABLE, None),
        vec![5, 4, 0, 1, 0, 0, 0, 0, 0, 0]
    );
    assert_eq!(u8::from(SocksStatus::SUCCEEDED), 0x00);
    assert_eq!(u8::from(SocksStatus::ADDRTYPE_NOT_SUPPORTED), 0x08);

    // SOCKS4 has only two outcomes, and its reply starts with a zero byte.
    let mut body = vec![4, 0xF0, 0, 0, 0, 0, 0, 1];
    body.extend_from_slice(b"\0");
    body.extend_from_slice(b"example.com\0");
    let mut hs = SocksHandshake::new();
    hs.handshake(&body).expect("SOCKS4a RESOLVE rejected");
    let req4 = finish(hs);
    assert_eq!(
        req4.reply(SocksStatus::SUCCEEDED, Some(&answer)),
        vec![0, 0x5A, 0, 0, 1, 2, 3, 4]
    );
    assert_eq!(
        req4.reply(SocksStatus::GENERAL_FAILURE, None),
        vec![0, 0x5B, 0, 0, 0, 0, 0, 0]
    );
}
