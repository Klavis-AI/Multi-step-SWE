//! Group `socksdiag`: telling a user that they pointed a browser at the SOCKS
//! port.
//!
//! instruction.md: "a diagnostic for HTTP sent to the SOCKS port". A SOCKS
//! handshake starts with a version byte; SOCKS itself defines only 4 and 5
//! (see SPEC_NOTES.md, socks-extensions.txt S1). Every other first byte is
//! reported as an unrecognised version, naming the byte, so that the proxy can
//! tell an HTTP request apart from a truncated SOCKS one.

use tor_error::{ErrorKind, HasKind};
use tor_socksproto::{Error, SocksHandshake};

use crate::common::HTTP_FIRST_BYTES;

/// Run one step of a fresh handshake and demand a hard (non-truncation) error.
fn reject(input: &[u8]) -> Error {
    let mut hs = SocksHandshake::new();
    hs.handshake(input)
        .expect("a complete first byte is not a truncation")
        .map(|_| ())
        .expect_err("this input is not a SOCKS handshake")
}

#[test]
fn socksdiag__http_request_is_reported_as_a_bad_version() {
    let err = reject(b"GET / HTTP/1.0\r\nHost: example.com\r\n\r\n");
    assert!(
        matches!(err, Error::BadProtocol(b'G')),
        "unexpected error: {:?}",
        err
    );
}

#[test]
fn socksdiag__every_http_method_initial_is_rejected() {
    // CONNECT, DELETE, GET, HEAD, OPTIONS, POST/PUT/PATCH, TRACE: the first
    // byte of every HTTP method a browser or curl might send.
    for &b in HTTP_FIRST_BYTES {
        let err = reject(&[b, b'E', b'T', b' ']);
        match err {
            Error::BadProtocol(got) => assert_eq!(got, b, "wrong byte reported for {:?}", b as char),
            other => panic!("byte {:?} should be a bad version, got {:?}", b as char, other),
        }
    }
}

#[test]
fn socksdiag__bad_version_message_names_the_offending_byte() {
    // 'G' is 0x47 == 71.
    assert_eq!(
        reject(b"GET /").to_string(),
        "Unrecognized SOCKS protocol version 71"
    );
    // 'P' is 0x50 == 80.
    assert_eq!(
        reject(b"POST /").to_string(),
        "Unrecognized SOCKS protocol version 80"
    );
}

#[test]
fn socksdiag__bad_version_is_a_local_protocol_violation() {
    // The fault is on this side of the network: something local spoke the
    // wrong protocol to us.
    assert_eq!(reject(b"GET /").kind(), ErrorKind::LocalProtocolViolation);
    assert_eq!(reject(&[0u8, 1, 2]).kind(), ErrorKind::LocalProtocolViolation);
}

#[test]
fn socksdiag__socks4_initial_byte_is_accepted() {
    let mut hs = SocksHandshake::new();
    // version 4, CONNECT, port 80, 127.0.0.1, empty username.
    let action = hs
        .handshake(&[4, 1, 0, 80, 127, 0, 0, 1, 0])
        .expect("not truncated")
        .expect("a SOCKS4 CONNECT is a valid handshake");
    assert!(action.finished);
    assert_eq!(action.drain, 9);
}

#[test]
fn socksdiag__socks5_initial_byte_is_accepted() {
    let mut hs = SocksHandshake::new();
    let action = hs
        .handshake(&[5, 1, 0])
        .expect("not truncated")
        .expect("a SOCKS5 greeting is a valid handshake start");
    assert!(!action.finished);
    assert_eq!(action.reply, vec![5, 0]);
}

#[test]
fn socksdiag__a_rejected_handshake_cannot_be_resumed() {
    // Once we have decided this is not SOCKS, feeding it real SOCKS bytes must
    // not rescue it; the connection is over.
    let mut hs = SocksHandshake::new();
    let first = hs
        .handshake(b"GET / HTTP/1.1\r\n")
        .expect("not truncated")
        .expect_err("not SOCKS");
    assert!(matches!(first, Error::BadProtocol(b'G')));
    assert!(!hs.finished());
    let second = hs
        .handshake(&[5, 1, 0])
        .expect("not truncated")
        .expect_err("a failed handshake stays failed");
    assert!(
        matches!(second, Error::AlreadyFinished(_)),
        "unexpected error: {:?}",
        second
    );
    assert!(hs.into_request().is_none());
}

#[test]
fn socksdiag__reusing_a_finished_handshake_is_an_internal_bug() {
    let mut hs = SocksHandshake::new();
    hs.handshake(&[4, 1, 0, 80, 127, 0, 0, 1, 0])
        .expect("not truncated")
        .expect("valid SOCKS4");
    assert!(hs.finished());
    let err = hs
        .handshake(&[4, 1, 0, 80, 127, 0, 0, 1, 0])
        .expect("not truncated")
        .expect_err("a finished handshake must refuse more input");
    assert!(
        matches!(err, Error::AlreadyFinished(_)),
        "unexpected error: {:?}",
        err
    );
    // Calling it again is our bug, not the peer's.
    assert_eq!(err.kind(), ErrorKind::Internal);
    assert_eq!(err.to_string(), "SOCKS handshake was finished; no need to call this again");
}
