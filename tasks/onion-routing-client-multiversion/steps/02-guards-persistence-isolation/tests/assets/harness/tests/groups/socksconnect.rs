//! Group `socksconnect` - the ordinary SOCKS request a client serves.
//!
//! Graded through `tor_socksproto::{SocksHandshake, Action, SocksRequest,
//! SocksCmd, SocksAddr, SocksAuth, Error}`. A local program that wants a
//! connection over Tor talks this protocol to the client, so the greeting, the
//! authentication exchange, and the request body are all graded byte by byte,
//! including the piecewise arrival of a request.
//!
//! The length-checked hostname inside `SocksAddr::Hostname` is not nameable
//! from outside `tor_socksproto` at this checkpoint, so it is reached through
//! the variant that carries it and through `TryFrom<String>`.

use std::convert::TryInto;
use std::net::{IpAddr, Ipv4Addr, Ipv6Addr};
use tor_socksproto::{Error, SocksAddr, SocksAuth, SocksCmd, SocksHandshake};

/// The `SocksAddr` that names `host`, or the error its length check gave.
///
/// The hostname type is private, so it is built through the conversion the
/// variant forces and never named.
fn hostname(host: &str) -> Result<SocksAddr, Error> {
    Ok(SocksAddr::Hostname(host.to_string().try_into()?))
}

/// A SOCKS5 request body naming a hostname.
fn s5_hostname(cmd: u8, host: &str, port: u16) -> Vec<u8> {
    let mut v = vec![5, cmd, 0, 3, host.len() as u8];
    v.extend_from_slice(host.as_bytes());
    v.extend_from_slice(&port.to_be_bytes());
    v
}

#[test]
fn socksconnect__a_hostname_request_is_carried_through_unnamed() {
    let mut hs = SocksHandshake::new();
    hs.handshake(&[5, 1, 0]).expect("a plain SOCKS5 greeting was rejected");

    let body = s5_hostname(1, "www.example.com", 8080);
    let act = hs.handshake(&body).expect("a CONNECT request to a hostname failed");
    assert_eq!(act.drain, body.len(), "the whole request should be consumed");
    assert!(act.finished);
    assert!(act.reply.is_empty(), "a request provokes no immediate reply");

    let req = hs.into_request().expect("a finished handshake produced no request");
    assert_eq!(req.command(), SocksCmd::CONNECT);
    assert_eq!(req.port(), 8080);
    assert_eq!(req.auth(), &SocksAuth::NoAuth);
    // The name is kept as a name: resolving it locally would leak it.
    assert_eq!(req.addr(), &hostname("www.example.com").expect("bad name"));
    assert_eq!(req.addr().to_string(), "www.example.com");
}

#[test]
fn socksconnect__an_address_of_either_family_is_understood() {
    // IPv4 is tag 1 and four bytes long.
    let mut hs = SocksHandshake::new();
    hs.handshake(&[5, 1, 0]).expect("greeting rejected");
    let body = vec![5, 1, 0, 1, 203, 0, 113, 9, 0x01, 0xBB];
    let act = hs.handshake(&body).expect("an IPv4 CONNECT failed");
    assert_eq!(act.drain, body.len());
    let req = hs.into_request().expect("no request");
    assert_eq!(req.port(), 443);
    assert_eq!(req.addr(), &SocksAddr::Ip(IpAddr::V4(Ipv4Addr::new(203, 0, 113, 9))));

    // IPv6 is tag 4 and sixteen bytes long.
    let mut hs = SocksHandshake::new();
    hs.handshake(&[5, 1, 0]).expect("greeting rejected");
    let mut body = vec![5, 1, 0, 4];
    body.extend_from_slice(&"2001:db8::7".parse::<Ipv6Addr>().expect("bad fixture").octets());
    body.extend_from_slice(&80_u16.to_be_bytes());
    hs.handshake(&body).expect("an IPv6 CONNECT failed");
    let req = hs.into_request().expect("no request");
    assert_eq!(req.port(), 80);
    assert_eq!(
        req.addr(),
        &SocksAddr::Ip(IpAddr::V6("2001:db8::7".parse().expect("bad fixture")))
    );
    // The third byte of a request is reserved and is ignored, not checked.
    let mut hs = SocksHandshake::new();
    hs.handshake(&[5, 1, 0]).expect("greeting rejected");
    hs.handshake(&vec![5, 1, 0xAB, 1, 1, 2, 3, 4, 0, 53])
        .expect("a request with a nonzero reserved byte was rejected");
    assert_eq!(hs.into_request().expect("no request").port(), 53);
}

#[test]
fn socksconnect__a_username_and_password_may_be_offered() {
    let mut hs = SocksHandshake::new();
    // Offering method 2 (username/password) alongside 0 (none) makes the
    // server pick 2, because credentials are the more capable option.
    let act = hs.handshake(&[5, 2, 0, 2]).expect("greeting rejected");
    assert_eq!(act.reply, vec![5, 2], "the server should have chosen method 2");
    assert!(!act.finished);

    // The credentials sub-negotiation: version 1, then two length-prefixed
    // strings. It is answered with version 1 and a success code.
    let body = vec![
        1, 4, b'u', b's', b'e', b'r', 6, b's', b'e', b'c', b'r', b'e', b't',
    ];
    let act = hs.handshake(&body).expect("a username/password offer failed");
    assert_eq!(act.drain, body.len());
    assert_eq!(act.reply, vec![1, 0]);
    assert!(!act.finished, "the request itself is still to come");

    hs.handshake(&s5_hostname(1, "example.com", 25))
        .expect("the request after authenticating failed");
    let req = hs.into_request().expect("no request");
    assert_eq!(
        req.auth(),
        &SocksAuth::Username(b"user".to_vec(), b"secret".to_vec())
    );

    // Once method 2 is chosen, the next message must be a credentials
    // message: its first byte is the sub-negotiation version, and only
    // version 1 exists, so anything else is not a credentials message at all.
    let mut hs = SocksHandshake::new();
    hs.handshake(&[5, 1, 2]).expect("greeting rejected");
    match hs.handshake(&[2, 1, b'u', 1, b'p']) {
        Ok(a) => panic!("an auth version other than 1 was accepted: {:?}", a),
        Err(e) => assert!(matches!(e, Error::Syntax), "got {:?}", e),
    }
    // That refusal is terminal: the handshake does not get a second chance.
    assert!(!hs.finished());
    match hs.handshake(&[1, 4, b'u', b's', b'e', b'r', 0]) {
        Ok(a) => panic!("a failed handshake accepted more input: {:?}", a),
        Err(e) => assert!(matches!(e, Error::AlreadyFinished), "got {:?}", e),
    }
    // Sending the request before the credentials is equally out of order.
    let mut hs = SocksHandshake::new();
    hs.handshake(&[5, 1, 2]).expect("greeting rejected");
    match hs.handshake(&s5_hostname(1, "example.com", 443)) {
        Ok(a) => panic!("a request arrived before its credentials: {:?}", a),
        Err(e) => assert!(matches!(e, Error::Syntax), "got {:?}", e),
    }

    // A greeting that offers no method we implement is refused outright.
    let mut hs = SocksHandshake::new();
    match hs.handshake(&[5, 1, 0x80]) {
        Ok(a) => panic!("an unimplementable greeting was accepted: {:?}", a),
        Err(e) => assert!(matches!(e, Error::NoSupport), "got {:?}", e),
    }
}

#[test]
fn socksconnect__a_request_that_arrives_in_pieces_is_resumed_not_lost() {
    let mut hs = SocksHandshake::new();
    hs.handshake(&[5, 1, 0]).expect("greeting rejected");

    let body = s5_hostname(1, "example.com", 443);
    // Hand over the request one byte at a time. Each short read has to be
    // reported as "need more", and the handshake has to remain usable.
    for split in 1..body.len() {
        let mut piecemeal = SocksHandshake::new();
        piecemeal.handshake(&[5, 1, 0]).expect("greeting rejected");
        match piecemeal.handshake(&body[..split]) {
            Err(Error::Truncated) => {}
            Err(Error::Syntax) | Err(Error::NoSupport) => {
                // The request header declares its own length, so a partial
                // body is reported as short and never as malformed.
                panic!("a partial request was reported as a protocol failure at {}", split)
            }
            other => panic!("a partial request produced {:?} at {}", other, split),
        }
        // The whole request still works on the same handshake.
        piecemeal
            .handshake(&body)
            .expect("a partial request spoiled the handshake");
        assert!(piecemeal.finished(), "the handshake never finished at {}", split);
        assert_eq!(piecemeal.into_request().expect("no request").port(), 443);
    }

    // An empty read is short, not a syntax error, and being short leaves the
    // handshake exactly as it was.
    let mut done = SocksHandshake::new();
    done.handshake(&[5, 1, 0]).expect("greeting rejected");
    done.handshake(&body).expect("request rejected");
    assert!(done.finished(), "a completed request did not finish the handshake");
    assert!(matches!(done.handshake(b""), Err(Error::Truncated)));
    assert!(done.finished(), "a short read undid a finished handshake");

    // Real input after the request is refused, and the refusal is a failure:
    // a failed handshake is no longer a finished one, and stays refused.
    assert!(matches!(done.handshake(&body), Err(Error::AlreadyFinished)));
    assert!(!done.finished(), "a failed handshake still reported itself finished");
    assert!(matches!(done.handshake(&body), Err(Error::AlreadyFinished)));
    assert!(
        done.into_request().is_some(),
        "the request negotiated before the failure was thrown away"
    );
}

#[test]
fn socksconnect__the_socks4_request_form_is_understood() {
    // SOCKS4: version, command, port, an integer address, then a
    // NUL-terminated user string.
    let mut body = vec![4, 1, 0x01, 0xBB, 10, 0, 0, 1];
    body.extend_from_slice(b"person\0");
    let mut hs = SocksHandshake::new();
    let act = hs.handshake(&body).expect("a SOCKS4 CONNECT failed");
    assert_eq!(act.drain, body.len());
    assert!(act.finished);

    let req = hs.into_request().expect("no request");
    assert_eq!(req.command(), SocksCmd::CONNECT);
    assert_eq!(req.port(), 443);
    assert_eq!(req.addr(), &SocksAddr::Ip(IpAddr::V4(Ipv4Addr::new(10, 0, 0, 1))));
    assert_eq!(req.auth(), &SocksAuth::Socks4(b"person".to_vec()));

    // The 4a extension: an address whose top three bytes are zero means a
    // hostname follows the user string, so no local lookup happens either.
    let mut body = vec![4, 1, 0, 80, 0, 0, 0, 5];
    body.extend_from_slice(b"\0");
    body.extend_from_slice(b"hidden.example\0");
    let mut hs = SocksHandshake::new();
    hs.handshake(&body).expect("a SOCKS4a CONNECT failed");
    let req = hs.into_request().expect("no request");
    assert_eq!(req.port(), 80);
    assert_eq!(req.addr(), &hostname("hidden.example").expect("bad name"));
    assert_eq!(req.auth(), &SocksAuth::Socks4(Vec::new()));

    // A command other than CONNECT is not something this client serves.
    let mut body = vec![4, 3, 0, 80, 10, 0, 0, 1];
    body.extend_from_slice(b"\0");
    let mut hs = SocksHandshake::new();
    match hs.handshake(&body) {
        Ok(a) => panic!("a SOCKS4 UDP_ASSOCIATE was accepted: {:?}", a),
        Err(e) => assert!(matches!(e, Error::NoSupport), "got {:?}", e),
    }
}

#[test]
fn socksconnect__a_name_is_carried_as_bytes_of_a_bounded_length() {
    // Names are kept verbatim and never resolved, so a name that is not a
    // well-formed hostname is still carried through.
    let raw = hostname("xn--0.example").expect("an ASCII name was refused");
    assert_eq!(raw.to_string(), "xn--0.example");
    match &raw {
        SocksAddr::Hostname(h) => assert_eq!(h.as_ref(), "xn--0.example"),
        other => panic!("expected a hostname, got {:?}", other),
    }

    // 255 bytes is the most the one-byte length field can describe.
    let longest = "a".repeat(255);
    let ok = hostname(&longest).expect("255 bytes was refused");
    assert_eq!(ok.to_string(), longest);

    let too_long = "a".repeat(256);
    match hostname(&too_long) {
        Ok(h) => panic!("a 256-byte name was accepted: {}", h),
        Err(e) => assert!(matches!(e, Error::Syntax), "got {:?}", e),
    }

    // The longest name a request can describe is still accepted in a
    // request, and moving 256 bytes through the greeting path does not
    // change the answer.
    let mut hs = SocksHandshake::new();
    hs.handshake(&[5, 1, 0]).expect("greeting rejected");
    let body = s5_hostname(1, &"a".repeat(255), 80);
    hs.handshake(&body).expect("a 255-byte name in a request was refused");
    let req = hs.into_request().expect("no request");
    assert_eq!(req.port(), 80);
    match req.addr() {
        SocksAddr::Hostname(h) => assert_eq!(h.as_ref().len(), 255),
        other => panic!("expected a hostname, got {:?}", other),
    }

    // A declared length of zero names nothing at all, which is still a
    // name as far as this layer is concerned.
    let mut hs = SocksHandshake::new();
    hs.handshake(&[5, 1, 0]).expect("greeting rejected");
    let mut body = vec![5, 1, 0, 3, 0];
    body.extend_from_slice(&80_u16.to_be_bytes());
    let act = hs.handshake(&body).expect("an empty name was refused");
    assert!(act.finished);
    match hs.into_request().expect("no request").addr() {
        SocksAddr::Hostname(h) => assert_eq!(h.as_ref(), ""),
        other => panic!("expected an empty hostname, got {:?}", other),
    }

    // A name that is not valid UTF-8 is a syntax violation, and one that is
    // simply unusual is not: the layer never tries to interpret the name.
    let mut hs = SocksHandshake::new();
    hs.handshake(&[5, 1, 0]).expect("greeting rejected");
    let mut body = vec![5, 1, 0, 3, 2, 0xFF, 0xFE];
    body.extend_from_slice(&80_u16.to_be_bytes());
    match hs.handshake(&body) {
        Ok(a) => panic!("a non-UTF-8 name was accepted: {:?}", a),
        Err(e) => assert!(matches!(e, Error::Syntax), "got {:?}", e),
    }
}
