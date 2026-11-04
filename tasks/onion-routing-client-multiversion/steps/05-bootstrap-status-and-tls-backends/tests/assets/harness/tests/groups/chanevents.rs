//! Group `chanevents` — the channel manager must *drive* the connection-status
//! stream as it makes and loses progress.
//!
//! Every case here runs on an isolated in-process mock network
//! (`tor_rtmock::net::MockNetwork`), so there is no real socket, no clock
//! dependence and no shared state between cases. The relay end deliberately
//! listens *without* a TLS certificate: the TCP connect therefore succeeds and
//! the TLS negotiation then fails, which is exactly the half-way state the
//! status machine has to be able to report.

use std::io::ErrorKind;
use std::net::SocketAddr;

use futures::{FutureExt, StreamExt};
use tor_chanmgr::{ChanMgr, ConnStatus, Error};
use tor_linkspec::OwnedChanTarget;
use tor_llcrypto::pk::{ed25519::Ed25519Identity, rsa::RsaIdentity};
use tor_rtcompat::{tokio::TokioNativeTlsRuntime, TcpProvider};
use tor_rtmock::net::MockNetwork;

fn relay_addr() -> SocketAddr {
    crate::common::RELAY_ADDR.parse().expect("relay address")
}

fn dark_addr() -> SocketAddr {
    crate::common::DARK_ADDR.parse().expect("unserved address")
}

fn a_target(addr: SocketAddr) -> OwnedChanTarget {
    OwnedChanTarget::new(
        vec![addr],
        Ed25519Identity::from([0x21; 32]),
        RsaIdentity::from([0x43; 20]),
    )
}

/// Make one channel attempt against the mock network and report the error it
/// produced together with the connection status the manager published.
///
/// `serve` decides whether anything is listening on the relay address at all.
fn one_attempt(serve: bool) -> (Error, ConnStatus) {
    TokioNativeTlsRuntime::run_test(|rt| async move {
        let net = MockNetwork::new();
        let client = net
            .builder()
            .add_address(crate::common::CLIENT_IP.parse().expect("client ip"))
            .runtime(rt.clone());
        let relay = net
            .builder()
            .add_address(crate::common::RELAY_IP.parse().expect("relay ip"))
            .runtime(rt);

        // A plain (non-TLS) listener: the connect lands, the handshake cannot.
        let _listener = if serve {
            Some(relay.listen(&relay_addr()).await.expect("mock listen"))
        } else {
            None
        };

        let target = a_target(if serve { relay_addr() } else { dark_addr() });
        let mgr = ChanMgr::new(client);
        let err = mgr
            .get_or_launch(&target)
            .await
            .err()
            .expect("no channel can be completed against a non-Tor listener");
        let status = mgr
            .bootstrap_events()
            .next()
            .now_or_never()
            .flatten()
            .expect("the manager always exposes a current status");
        (err, status)
    })
}

fn io_kind(err: &Error) -> ErrorKind {
    match err {
        Error::Io(e) => e.kind(),
        other => panic!("expected an I/O error from the channel manager, got {other:?}"),
    }
}

#[test]
fn chanevents__initial_status_is_the_default_status() {
    let rt = TokioNativeTlsRuntime::create().expect("runtime");
    let mgr = ChanMgr::new(rt);
    let st = mgr
        .bootstrap_events()
        .next()
        .now_or_never()
        .flatten()
        .expect("current status");
    crate::common::assert_frac(st.frac(), 0.0);
    assert_eq!(st.to_string(), ConnStatus::default().to_string());
}

#[test]
fn chanevents__refused_connection_leaves_status_at_start() {
    let (err, st) = one_attempt(false);
    assert_eq!(io_kind(&err), ErrorKind::ConnectionRefused);
    // No TCP connection was ever made, so no progress may be claimed.
    crate::common::assert_frac(st.frac(), 0.0);
    assert_eq!(st.to_string(), "connecting to the internet");
}

#[test]
fn chanevents__successful_tcp_reports_half_progress() {
    let (_err, st) = one_attempt(true);
    crate::common::assert_frac(st.frac(), 0.5);
}

#[test]
fn chanevents__successful_tcp_updates_the_displayed_phase() {
    let (_err, st) = one_attempt(true);
    assert_eq!(st.to_string(), "handshaking with Tor relays");
}

#[test]
fn chanevents__tcp_only_progress_is_not_usable_and_not_blocked() {
    let (_err, st) = one_attempt(true);
    assert!(!st.usable(), "a half-open connection is not usable");
    // A single failed handshake is not yet evidence of censorship.
    assert!(st.blockage().is_none());
}

#[test]
fn chanevents__tls_failure_is_reported_as_an_io_error() {
    let (err, _st) = one_attempt(true);
    assert_eq!(io_kind(&err), ErrorKind::Other);
    assert_eq!(
        err.to_string(),
        "I/O error while opening a channel: attempted to wrap non-TLS stream!"
    );
}
