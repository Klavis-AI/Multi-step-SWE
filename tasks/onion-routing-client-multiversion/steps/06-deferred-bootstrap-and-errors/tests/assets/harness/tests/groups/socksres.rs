//! Group `socksres`: "need more bytes" is a separate result from "this is
//! broken".
//!
//! instruction.md: "a diagnostic for HTTP sent to the SOCKS port" and "errors
//! gain stable kinds". Telling HTTP apart from SOCKS is only possible if
//! running out of input is *not* an error: the handshake returns a
//! `Truncated` marker in the outer result and a real `Error` in the inner one,
//! so a caller can retry the first and must give up on the second.

use tor_error::{ErrorKind, HasKind};
use tor_socksproto::{Error, SocksAddr, SocksAuth, SocksCmd, SocksHandshake, SocksVersion};

use crate::common::SOCKS5_GREETING_NOAUTH;

#[test]
fn socksres__no_input_at_all_is_truncation_not_failure() {
    let mut hs = SocksHandshake::new();
    let outcome = hs.handshake(&[]);
    assert!(outcome.is_err(), "empty input must be reported as truncated");
    assert_eq!(
        outcome.unwrap_err().to_string(),
        "Incomplete data; more input needed"
    );
}

#[test]
fn socksres__a_partial_greeting_is_truncation() {
    // Announces two authentication methods but supplies only one byte of them.
    let mut hs = SocksHandshake::new();
    assert!(
        hs.handshake(&[5, 2, 0]).is_err(),
        "a short greeting must be reported as truncated"
    );
}

#[test]
fn socksres__truncation_leaves_the_handshake_usable() {
    // This is the whole point of separating the two results: after a short
    // read the caller waits for more bytes and tries again with the same
    // handshake object.
    let mut hs = SocksHandshake::new();
    assert!(hs.handshake(&[5, 2, 0]).is_err());
    let action = hs
        .handshake(&[5, 2, 0, 2])
        .expect("now the greeting is complete")
        .expect("and it is valid");
    assert_eq!(action.drain, 4);
    assert!(!hs.finished());
}

#[test]
fn socksres__no_auth_is_accepted_and_acknowledged() {
    let mut hs = SocksHandshake::new();
    let action = hs
        .handshake(SOCKS5_GREETING_NOAUTH)
        .expect("not truncated")
        .expect("a no-auth greeting is valid");
    assert_eq!(action.drain, 3);
    assert_eq!(action.reply, vec![5, 0]);
    assert!(!action.finished);
    assert!(!hs.finished());
}

#[test]
fn socksres__username_password_is_preferred_when_offered() {
    // Arti uses SOCKS authentication for stream isolation, so when the client
    // offers both, it picks username/password (method 2) over none (method 0).
    let mut hs = SocksHandshake::new();
    let action = hs
        .handshake(&[5, 2, 0, 2])
        .expect("not truncated")
        .expect("a two-method greeting is valid");
    assert_eq!(action.drain, 4);
    assert_eq!(action.reply, vec![5, 2]);
    assert!(!action.finished);
}

#[test]
fn socksres__an_unusable_auth_method_is_not_implemented() {
    // 0xFF is "no acceptable methods"; we do not support it as an offer.
    let mut hs = SocksHandshake::new();
    let err = hs
        .handshake(&[5, 1, 0xFF])
        .expect("not truncated")
        .expect_err("we support neither GSSAPI nor anything exotic");
    assert!(
        matches!(err, Error::NotImplemented),
        "unexpected error: {:?}",
        err
    );
    assert_eq!(err.kind(), ErrorKind::NotImplemented);
    assert_eq!(err.to_string(), "SOCKS feature not implemented");
}

#[test]
fn socksres__a_socks4_connect_yields_the_request() {
    let mut hs = SocksHandshake::new();
    // VN=4, CD=CONNECT, DSTPORT=443, DSTIP=93.184.216.34, USERID="bob", NUL.
    let input = [4u8, 1, 1, 187, 93, 184, 216, 34, b'b', b'o', b'b', 0];
    let action = hs
        .handshake(&input)
        .expect("not truncated")
        .expect("a SOCKS4 CONNECT is valid");
    assert_eq!(action.drain, input.len());
    assert!(action.reply.is_empty());
    assert!(action.finished);
    assert!(hs.finished());

    let req = hs.into_request().expect("a finished handshake has a request");
    assert_eq!(req.version(), SocksVersion::V4);
    assert_eq!(req.command(), SocksCmd::CONNECT);
    assert_eq!(req.port(), 443);
    assert_eq!(
        req.addr(),
        &SocksAddr::Ip("93.184.216.34".parse().expect("literal IPv4"))
    );
    assert_eq!(req.auth(), &SocksAuth::Socks4(b"bob".to_vec()));
}
