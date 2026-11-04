//! Group `protokind`: stable kinds for the Tor protocol layer.
//!
//! instruction.md: "a structured error taxonomy" and "errors gain stable
//! kinds". `tor_proto::Error` is where the low-level faults surface. Its
//! `kind()` has to separate three quite different things: a local I/O failure,
//! a relay that broke the protocol, and a mistake made by the caller.

use std::io;
use std::sync::Arc;

use tor_cell::relaycell::msg::EndReason;
use tor_error::{ErrorKind, HasKind};
use tor_proto::Error;

/// Assert a whole row of the kind table at once.
fn check(rows: Vec<(Error, ErrorKind)>) {
    for (err, want) in rows {
        assert_eq!(err.kind(), want, "wrong kind for {:?}", err);
    }
}

/// A shared `io::Error` for the channel-I/O variants.
fn ioerr(kind: io::ErrorKind) -> Arc<io::Error> {
    Arc::new(io::Error::new(kind, "synthetic"))
}

#[test]
fn protokind__io_failures_are_classified_by_where_they_happened() {
    // Trouble on an established channel is a local network problem; trouble
    // while first reaching a relay means we could not reach Tor at all.
    check(vec![
        (
            Error::ChanIoErr(ioerr(io::ErrorKind::BrokenPipe)),
            ErrorKind::LocalNetworkError,
        ),
        (
            Error::HandshakeIoErr(ioerr(io::ErrorKind::ConnectionRefused)),
            ErrorKind::TorAccessFailed,
        ),
        (
            Error::HandshakeProto("no versions in common".into()),
            ErrorKind::TorAccessFailed,
        ),
    ]);
}

#[test]
fn protokind__malformed_traffic_is_a_protocol_violation() {
    check(vec![
        (
            Error::BytesErr(tor_bytes::Error::ExtraneousBytes),
            ErrorKind::TorProtocolViolation,
        ),
        (
            Error::BytesErr(tor_bytes::Error::BadMessage("bad length")),
            ErrorKind::TorProtocolViolation,
        ),
        (Error::BadCellAuth, ErrorKind::TorProtocolViolation),
        (Error::BadCircHandshake, ErrorKind::TorProtocolViolation),
        (
            Error::ChanProto("unexpected cell".into()),
            ErrorKind::TorProtocolViolation,
        ),
        (
            Error::CircProto("unexpected relay cell".into()),
            ErrorKind::TorProtocolViolation,
        ),
        (
            Error::StreamProto("data before connected".into()),
            ErrorKind::TorProtocolViolation,
        ),
    ]);
}

#[test]
fn protokind__a_closed_channel_or_circuit_is_a_collapse() {
    check(vec![
        (Error::ChannelClosed, ErrorKind::CircuitCollapse),
        (Error::CircuitClosed, ErrorKind::CircuitCollapse),
    ]);
}

#[test]
fn protokind__caller_mistakes_are_bad_api_usage() {
    check(vec![
        (Error::NoSuchHop, ErrorKind::BadApiUsage),
        (Error::IdRangeFull, ErrorKind::BadApiUsage),
        (Error::BadStreamAddress, ErrorKind::BadApiUsage),
        (Error::NotConnected, ErrorKind::BadApiUsage),
        // Whereas asking for an impossible amount of key material is our own
        // fault, not the caller's.
        (Error::InvalidOutputLength, ErrorKind::Internal),
    ]);
}

#[test]
fn protokind__remote_refusals_keep_their_own_kind() {
    check(vec![
        (
            Error::CircRefused("extend failed"),
            ErrorKind::CircuitRefused,
        ),
        (
            Error::ChanMismatch("wrong ed25519 identity".into()),
            ErrorKind::RelayIdMismatch,
        ),
        (
            Error::ResolveError("nxdomain".into()),
            ErrorKind::RemoteHostNotFound,
        ),
    ]);
}

#[test]
fn protokind__an_end_cell_is_classified_by_its_reason() {
    // The exit told us why it closed the stream; that reason, not the fact
    // that an END arrived, is what the caller needs.
    check(vec![
        (
            Error::EndReceived(EndReason::EXITPOLICY),
            ErrorKind::ExitPolicyRejected,
        ),
        (
            Error::EndReceived(EndReason::CONNECTREFUSED),
            ErrorKind::RemoteConnectionRefused,
        ),
        (
            Error::EndReceived(EndReason::RESOLVEFAILED),
            ErrorKind::RemoteHostNotFound,
        ),
        (
            Error::EndReceived(EndReason::DESTROY),
            ErrorKind::CircuitCollapse,
        ),
    ]);
}

#[test]
fn protokind__converts_to_an_io_error_without_losing_the_reason() {
    // A protocol error crossing into `std::io` keeps a usable io kind, and an
    // I/O error that came from the OS is handed back unchanged.
    assert_eq!(
        io::Error::from(Error::NotConnected).kind(),
        io::ErrorKind::NotConnected
    );
    assert_eq!(
        io::Error::from(Error::BadStreamAddress).kind(),
        io::ErrorKind::InvalidInput
    );
    assert_eq!(
        io::Error::from(Error::CircuitClosed).kind(),
        io::ErrorKind::ConnectionReset
    );
    assert_eq!(
        io::Error::from(Error::ChanProto("bad cell".into())).kind(),
        io::ErrorKind::InvalidData
    );
    assert_eq!(
        io::Error::from(Error::EndReceived(EndReason::CONNECTREFUSED)).kind(),
        io::ErrorKind::ConnectionRefused
    );
    assert_eq!(
        io::Error::from(Error::ChanIoErr(ioerr(io::ErrorKind::BrokenPipe))).kind(),
        io::ErrorKind::BrokenPipe
    );
}

#[test]
fn protokind__display_states_what_went_wrong() {
    assert_eq!(Error::ChannelClosed.to_string(), "channel closed");
    assert_eq!(Error::CircuitClosed.to_string(), "circuit closed");
    assert_eq!(Error::NotConnected.to_string(), "stream not connected");
    assert_eq!(
        Error::BadStreamAddress.to_string(),
        "invalid stream target address"
    );
    assert_eq!(
        Error::CircProto("surprise".into()).to_string(),
        "circuit protocol violation: surprise"
    );
    assert_eq!(
        Error::EndReceived(EndReason::EXITPOLICY).to_string(),
        "Received an End cell with reason EXITPOLICY"
    );
}
