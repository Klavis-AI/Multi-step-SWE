//! Group `chankind`: stable kinds for the channel manager.
//!
//! instruction.md: "a structured error taxonomy". The channel manager sits
//! between the socket and the protocol layer, so it re-reads some of
//! `tor_proto`'s verdicts: an I/O failure that `tor_proto` calls a local
//! network problem is, from here, a failure to reach the Tor network at all.

use std::net::SocketAddr;
use std::sync::Arc;

use tor_chanmgr::Error;
use tor_error::{internal, ErrorKind, HasKind};

/// A fixed, documentation-range peer address for the `Io` variant.
fn peer() -> SocketAddr {
    "198.51.100.7:443".parse().expect("literal socket address")
}

#[test]
fn chankind__unreachable_relays_are_a_tor_access_failure() {
    assert_eq!(Error::ChanTimeout.kind(), ErrorKind::TorAccessFailed);
    assert_eq!(Error::PendingFailed.kind(), ErrorKind::TorAccessFailed);
    assert_eq!(
        Error::Io {
            peer: peer(),
            action: "connecting",
            source: Arc::new(std::io::Error::new(
                std::io::ErrorKind::ConnectionRefused,
                "refused"
            )),
        }
        .kind(),
        ErrorKind::TorAccessFailed
    );
}

#[test]
fn chankind__channel_io_is_reclassified_as_a_tor_access_failure() {
    // `tor_proto` calls this a local network error, because it does not know
    // who it was talking to. The channel manager does, so it says so.
    let inner = tor_proto::Error::ChanIoErr(Arc::new(std::io::Error::new(
        std::io::ErrorKind::BrokenPipe,
        "gone",
    )));
    assert_eq!(inner.kind(), ErrorKind::LocalNetworkError);
    assert_eq!(Error::Proto(inner).kind(), ErrorKind::TorAccessFailed);
}

#[test]
fn chankind__other_protocol_errors_keep_the_protocol_verdict() {
    assert_eq!(
        Error::Proto(tor_proto::Error::ChanProto("bad cell".into())).kind(),
        ErrorKind::TorProtocolViolation
    );
    assert_eq!(
        Error::Proto(tor_proto::Error::ChannelClosed).kind(),
        ErrorKind::CircuitCollapse
    );
    assert_eq!(
        Error::Proto(tor_proto::Error::HandshakeIoErr(Arc::new(
            std::io::Error::new(std::io::ErrorKind::TimedOut, "slow")
        )))
        .kind(),
        ErrorKind::TorAccessFailed
    );
}

#[test]
fn chankind__our_own_faults_are_internal() {
    assert_eq!(
        Error::UnusableTarget(internal!("no addresses")).kind(),
        ErrorKind::Internal
    );
    assert_eq!(
        Error::Internal(internal!("lock poisoned")).kind(),
        ErrorKind::Internal
    );
}

#[test]
fn chankind__a_failed_spawn_reports_the_executors_verdict() {
    let e = Error::Spawn {
        spawning: "channel reactor",
        cause: Arc::new(futures::task::SpawnError::shutdown()),
    };
    assert_eq!(e.kind(), ErrorKind::ReactorShuttingDown);
    assert_eq!(e.to_string(), "unable to spawn channel reactor");
}

#[test]
fn chankind__display_states_what_went_wrong() {
    assert_eq!(Error::ChanTimeout.to_string(), "Channel timed out");
    assert_eq!(
        Error::PendingFailed.to_string(),
        "Pending channel failed to launch"
    );
    assert_eq!(
        Error::Io {
            peer: peer(),
            action: "handshaking",
            source: Arc::new(std::io::Error::new(std::io::ErrorKind::Other, "x")),
        }
        .to_string(),
        "Network IO error, or TLS error, in handshaking, talking to 198.51.100.7:443"
    );
    assert_eq!(
        Error::Proto(tor_proto::Error::ChannelClosed).to_string(),
        "Protocol error while opening a channel: channel closed"
    );
}
