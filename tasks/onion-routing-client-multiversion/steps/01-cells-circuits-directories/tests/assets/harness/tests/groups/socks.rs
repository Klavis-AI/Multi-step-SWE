//! Group `socks`: the SOCKS4/4a/5 proxy handshake the client offers to
//! applications, and the Tor-specific commands layered on top of it.

use std::net::IpAddr;

use tor_socksproto::{Error, SocksAddr, SocksAuth, SocksCmd, SocksHandshake, SocksRequest, SocksStatus};

/// Feed one message to a handshake and return the action.
fn step(hs: &mut SocksHandshake, msg: &[u8]) -> tor_socksproto::Action {
    let a = hs
        .handshake(msg)
        .unwrap_or_else(|e| panic!("handshake step failed: {}", e));
    assert_eq!(a.drain, msg.len(), "whole message should be consumed");
    a
}

/// Run a full SOCKS5 no-authentication negotiation and return the request.
fn socks5_noauth(request: &[u8]) -> SocksRequest {
    let mut hs = SocksHandshake::new();
    let a = step(&mut hs, &[5, 1, 0]);
    assert_eq!(a.reply, vec![5, 0]);
    assert!(!a.finished);
    let a = step(&mut hs, request);
    assert!(a.reply.is_empty());
    assert!(a.finished);
    assert!(hs.finished());
    hs.into_request().expect("a completed request")
}

/// A SOCKS5 request message for a hostname target.
fn s5_hostname(cmd: u8, host: &str, port: u16) -> Vec<u8> {
    let mut v = vec![5, cmd, 0, 3, host.len() as u8];
    v.extend_from_slice(host.as_bytes());
    v.extend_from_slice(&port.to_be_bytes());
    v
}

#[test]
fn socks__socks5_negotiates_no_authentication_then_takes_a_connect_request() {
    let req = socks5_noauth(&s5_hostname(1, "example.com", 80));
    assert_eq!(req.version(), 5);
    assert_eq!(req.command(), SocksCmd::CONNECT);
    assert_eq!(u8::from(req.command()), 1);
    assert_eq!(req.port(), 80);
    assert_eq!(req.addr().to_string(), "example.com");
    assert!(matches!(req.addr(), SocksAddr::Hostname(h) if h.as_ref() == "example.com"));
    assert_eq!(req.auth(), &SocksAuth::NoAuth);
}

#[test]
fn socks__socks5_prefers_username_password_when_the_client_offers_it() {
    let mut hs = SocksHandshake::new();
    // Methods 0 (none) and 2 (username/password): 2 wins.
    let a = step(&mut hs, &[5, 2, 0, 2]);
    assert_eq!(a.reply, vec![5, 2]);
    assert!(!a.finished);

    // RFC 1929 sub-negotiation: version 1, then length-prefixed strings.
    let a = step(&mut hs, &[1, 3, b'b', b'o', b'b', 2, b'h', b'i']);
    assert_eq!(a.reply, vec![1, 0]);
    assert!(!a.finished);

    let a = step(&mut hs, &[5, 1, 0, 1, 10, 0, 0, 7, 0x1f, 0x90]);
    assert!(a.finished);
    let req = hs.into_request().expect("a completed request");
    assert_eq!(req.port(), 8080);
    assert_eq!(
        req.auth(),
        &SocksAuth::Username(b"bob".to_vec(), b"hi".to_vec())
    );
    assert_eq!(req.addr(), &SocksAddr::Ip("10.0.0.7".parse::<IpAddr>().unwrap()));
}

#[test]
fn socks__socks5_understands_all_three_address_types() {
    // ATYP 1: IPv4.
    let req = socks5_noauth(&[5, 1, 0, 1, 192, 0, 2, 33, 0x01, 0xbb]);
    assert_eq!(req.addr(), &SocksAddr::Ip("192.0.2.33".parse::<IpAddr>().unwrap()));
    assert_eq!(req.port(), 443);
    assert_eq!(req.addr().to_string(), "192.0.2.33");

    // ATYP 4: IPv6.
    let mut msg = vec![5, 1, 0, 4];
    msg.extend_from_slice(&[0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1]);
    msg.extend_from_slice(&443u16.to_be_bytes());
    let req = socks5_noauth(&msg);
    assert_eq!(req.addr(), &SocksAddr::Ip("2001:db8::1".parse::<IpAddr>().unwrap()));
    assert_eq!(req.addr().to_string(), "2001:db8::1");

    // ATYP 3: a length-prefixed hostname.
    let req = socks5_noauth(&s5_hostname(1, "torproject.org", 9150));
    assert_eq!(req.addr().to_string(), "torproject.org");
    assert_eq!(req.port(), 9150);

    // ATYP 2 is not assigned.
    let mut hs = SocksHandshake::new();
    let _ = step(&mut hs, &[5, 1, 0]);
    let e = hs.handshake(&[5, 1, 0, 2, 1, 2, 3, 4, 0, 80]).unwrap_err();
    assert!(matches!(e, Error::Syntax), "{:?}", e);
}

#[test]
fn socks__socks4_and_socks4a_carry_the_userid_and_an_optional_hostname() {
    // SOCKS4: VN=4, CD=1, port, IPv4, userid, NUL.
    let mut hs = SocksHandshake::new();
    let mut msg = vec![4, 1, 0x00, 0x50, 127, 0, 0, 1];
    msg.extend_from_slice(b"user\0");
    let a = step(&mut hs, &msg);
    assert!(a.reply.is_empty(), "socks4 sends no interim reply");
    assert!(a.finished);
    let req = hs.into_request().unwrap();
    assert_eq!(req.version(), 4);
    assert_eq!(req.command(), SocksCmd::CONNECT);
    assert_eq!(req.port(), 80);
    assert_eq!(req.addr(), &SocksAddr::Ip("127.0.0.1".parse::<IpAddr>().unwrap()));
    assert_eq!(req.auth(), &SocksAuth::Socks4(b"user".to_vec()));

    // SOCKS4a: an address of the form 0.0.0.x with x nonzero means the real
    // target is the NUL-terminated hostname that follows the userid.
    let mut hs = SocksHandshake::new();
    let mut msg = vec![4, 1, 0x1f, 0x90, 0, 0, 0, 1];
    msg.extend_from_slice(b"\0");
    msg.extend_from_slice(b"example.com\0");
    let a = step(&mut hs, &msg);
    assert!(a.finished);
    let req = hs.into_request().unwrap();
    assert_eq!(req.port(), 8080);
    assert_eq!(req.addr().to_string(), "example.com");
    assert_eq!(req.auth(), &SocksAuth::Socks4(Vec::new()));
}

#[test]
fn socks__replies_use_the_version_of_the_request() {
    let req = socks5_noauth(&s5_hostname(1, "example.com", 80));
    // With no address, a SOCKS5 reply reports 0.0.0.0:0.
    assert_eq!(
        req.reply(SocksStatus::SUCCEEDED, None),
        vec![5, 0, 0, 1, 0, 0, 0, 0, 0, 0]
    );
    assert_eq!(
        req.reply(SocksStatus::HOST_UNREACHABLE, None),
        vec![5, 4, 0, 1, 0, 0, 0, 0, 0, 0]
    );
    assert_eq!(u8::from(SocksStatus::GENERAL_FAILURE), 1);
    // With an address, the reply echoes the request's port.
    let addr = SocksAddr::Ip("192.0.2.33".parse::<IpAddr>().unwrap());
    assert_eq!(
        req.reply(SocksStatus::SUCCEEDED, Some(&addr)),
        vec![5, 0, 0, 1, 192, 0, 2, 33, 0, 80]
    );

    // SOCKS4 has only two statuses: 0x5A granted, 0x5B rejected.
    let mut hs = SocksHandshake::new();
    let mut msg = vec![4, 1, 0x00, 0x50, 127, 0, 0, 1];
    msg.extend_from_slice(b"\0");
    let _ = step(&mut hs, &msg);
    let req4 = hs.into_request().unwrap();
    assert_eq!(
        req4.reply(SocksStatus::SUCCEEDED, None),
        vec![0, 0x5a, 0, 0, 0, 0, 0, 0]
    );
    assert_eq!(
        req4.reply(SocksStatus::CONNECTION_REFUSED, None),
        vec![0, 0x5b, 0, 0, 0, 0, 0, 0]
    );
}

#[test]
fn socks__a_partial_message_is_truncated_and_can_be_retried() {
    let mut hs = SocksHandshake::new();
    // The method list is one byte short.
    let e = hs.handshake(&[5, 2, 0]).unwrap_err();
    assert!(matches!(e, Error::Truncated), "{:?}", e);
    // An empty read is truncated too, and neither attempt poisoned the state.
    let e = hs.handshake(&[]).unwrap_err();
    assert!(matches!(e, Error::Truncated), "{:?}", e);
    let a = step(&mut hs, &[5, 2, 0, 2]);
    assert_eq!(a.reply, vec![5, 2]);

    let mut hs = SocksHandshake::new();
    let _ = step(&mut hs, &[5, 1, 0]);
    // A request missing its last port byte is truncated, not a syntax error.
    let full = s5_hostname(1, "example.com", 80);
    let e = hs.handshake(&full[..full.len() - 1]).unwrap_err();
    assert!(matches!(e, Error::Truncated), "{:?}", e);
    assert!(!hs.finished());
    let a = step(&mut hs, &full);
    assert!(a.finished);
    assert_eq!(hs.into_request().unwrap().port(), 80);
}

#[test]
fn socks__tor_extends_the_command_set_and_rejects_the_rest() {
    // Tor's RESOLVE and RESOLVE_PTR replace the unsupported BIND and UDP.
    let req = socks5_noauth(&s5_hostname(0xf0, "example.com", 80));
    assert_eq!(req.command(), SocksCmd::RESOLVE);
    assert_eq!(u8::from(SocksCmd::RESOLVE), 0xf0);
    let req = socks5_noauth(&[5, 0xf1, 0, 1, 192, 0, 2, 33, 0, 80]);
    assert_eq!(req.command(), SocksCmd::RESOLVE_PTR);
    assert_eq!(u8::from(SocksCmd::RESOLVE_PTR), 0xf1);

    for cmd in [2u8, 3u8] {
        let mut hs = SocksHandshake::new();
        let _ = step(&mut hs, &[5, 1, 0]);
        let e = hs.handshake(&s5_hostname(cmd, "example.com", 80)).unwrap_err();
        assert!(matches!(e, Error::NoSupport), "cmd {}: {:?}", cmd, e);
    }
    assert_eq!(u8::from(SocksCmd::BIND), 2);
    assert_eq!(u8::from(SocksCmd::UDP_ASSOCIATE), 3);

    // Port zero is not a destination.
    let mut hs = SocksHandshake::new();
    let _ = step(&mut hs, &[5, 1, 0]);
    let e = hs.handshake(&s5_hostname(1, "example.com", 0)).unwrap_err();
    assert!(matches!(e, Error::Syntax), "{:?}", e);

    // A version byte that is neither 4 nor 5 is refused outright.
    let mut hs = SocksHandshake::new();
    let e = hs.handshake(&[6, 1, 0]).unwrap_err();
    assert!(matches!(e, Error::BadProtocol(6)), "{:?}", e);

    // A client that keeps talking after the request is a programming error.
    let mut hs = SocksHandshake::new();
    let _ = step(&mut hs, &[5, 1, 0]);
    let _ = step(&mut hs, &s5_hostname(1, "example.com", 80));
    let e = hs.handshake(&[5, 1, 0]).unwrap_err();
    assert!(matches!(e, Error::AlreadyFinished), "{:?}", e);

    // A client that offers no method we accept gets NoSupport.
    let mut hs = SocksHandshake::new();
    let e = hs.handshake(&[5, 1, 0x80]).unwrap_err();
    assert!(matches!(e, Error::NoSupport), "{:?}", e);
}
