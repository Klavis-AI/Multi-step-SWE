//! Group `chanmgrapi` (peripheral) — the channel manager's public surface
//! around the new status stream.

use std::io::ErrorKind;
use std::net::SocketAddr;

use tor_chanmgr::{ChanMgr, ConnStatusEvents, Error};
use tor_linkspec::OwnedChanTarget;
use tor_llcrypto::pk::{ed25519::Ed25519Identity, rsa::RsaIdentity};
use tor_proto::channel::Channel;
use tor_rtcompat::{tokio::TokioNativeTlsRuntime, BlockOn};

type Rt = TokioNativeTlsRuntime;

fn future_output_is<F, O>(_f: F)
where
    F: std::future::Future<Output = O>,
{
}

fn target(addrs: Vec<SocketAddr>) -> OwnedChanTarget {
    OwnedChanTarget::new(
        addrs,
        Ed25519Identity::from([0x31; 32]),
        RsaIdentity::from([0x32; 20]),
    )
}

#[test]
fn chanmgrapi__get_or_launch_yields_an_owned_channel() {
    let mgr = ChanMgr::new(Rt::create().expect("runtime"));
    let t = target(vec![]);
    // Built, never polled: this pins the output type without doing any I/O.
    future_output_is::<_, tor_chanmgr::Result<Channel>>(mgr.get_or_launch(&t));
}

#[test]
fn chanmgrapi__unusable_target_is_reported() {
    let rt = Rt::create().expect("runtime");
    let mgr = ChanMgr::new(rt.clone());
    let t = target(vec![]);
    let err = rt
        .block_on(mgr.get_or_launch(&t))
        .err()
        .expect("a relay with no addresses cannot be reached");
    assert_eq!(
        err.to_string(),
        "Target was unusable: No addresses for chosen relay"
    );
    assert!(matches!(err, Error::UnusableTarget(_)));
}

#[test]
fn chanmgrapi__io_errors_wrap_the_underlying_error() {
    let err = Error::from(std::io::Error::new(ErrorKind::ConnectionRefused, "nope"));
    assert_eq!(err.to_string(), "I/O error while opening a channel: nope");
    match &err {
        Error::Io(inner) => assert_eq!(inner.kind(), ErrorKind::ConnectionRefused),
        other => panic!("unexpected variant: {other:?}"),
    }
}

#[test]
fn chanmgrapi__errors_are_cloneable_and_non_exhaustive() {
    let err = Error::ChanTimeout;
    assert_eq!(err.clone().to_string(), "Channel timed out");
    assert_eq!(Error::PendingFailed.to_string(), "Pending channel failed to launch");
    // Non-exhaustive, so a wildcard arm is required even after listing all of
    // the variants this step knows about.
    let described = match Error::PendingFailed {
        Error::ChanTimeout => "timeout",
        Error::PendingFailed => "pending",
        _ => "other",
    };
    assert_eq!(described, "pending");
}

#[test]
fn chanmgrapi__internal_errors_carry_a_static_message() {
    assert_eq!(
        Error::Internal("boom").to_string(),
        "Internal error: boom"
    );
}

#[test]
fn chanmgrapi__channel_manager_exposes_its_status_stream() {
    let f: fn(&ChanMgr<Rt>) -> ConnStatusEvents = ChanMgr::bootstrap_events;
    let mgr = ChanMgr::new(Rt::create().expect("runtime"));
    assert_eq!(format!("{:?}", f(&mgr)), "ConnStatusEvents { .. }");
}
