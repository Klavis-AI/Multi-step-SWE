//! Group `blockon` — the runtime trait that lets a synchronous caller drive an
//! asynchronous one.
//!
//! The capability is reached only through the trait, never through an inherent
//! method, so every case here calls it behind a generic bound.

use tor_rtcompat::tokio::TokioNativeTlsRuntime;
use tor_rtcompat::{BlockOn, Runtime};
use tor_rtmock::net::MockNetwork;
use tor_rtmock::MockNetRuntime;

/// Drive a future through the trait alone.
fn drive<B: BlockOn>(b: &B) -> u32 {
    b.block_on(async { 7 })
}

/// Anything that satisfies the `Runtime` bound also satisfies `BlockOn`.
fn drive_runtime<R: Runtime>(rt: &R) -> String {
    rt.block_on(async { String::from("driven") })
}

#[test]
fn blockon__trait_is_exported_under_its_new_name() {
    let rt = TokioNativeTlsRuntime::create().expect("runtime");
    assert_eq!(drive(&rt), 7);
}

#[test]
fn blockon__block_on_returns_the_future_output() {
    let rt = TokioNativeTlsRuntime::create().expect("runtime");
    // The output is whatever the future resolves to, including values that
    // are only produced after an await point.
    let out = rt.block_on(async {
        let (tx, rx) = futures::channel::oneshot::channel::<&'static str>();
        tx.send("resolved").expect("send");
        rx.await.expect("receive")
    });
    assert_eq!(out, "resolved");
}

#[test]
fn blockon__runtime_bound_provides_block_on() {
    let rt = TokioNativeTlsRuntime::create().expect("runtime");
    assert_eq!(drive_runtime(&rt), "driven");
}

#[test]
fn blockon__current_user_runtime_tracks_the_ambient_runtime() {
    // No ambient runtime here.
    assert!(tor_rtcompat::current_user_runtime().is_err());
    // Inside one, the same query succeeds and yields a working runtime.
    let rt = TokioNativeTlsRuntime::create().expect("runtime");
    let inner_ok = rt.block_on(async {
        let found = tor_rtcompat::current_user_runtime();
        found.is_ok()
    });
    assert!(inner_ok);
}

#[test]
fn blockon__mock_network_runtime_satisfies_the_runtime_bound() {
    let rt = TokioNativeTlsRuntime::create().expect("runtime");
    let net = MockNetwork::new();
    let mocked: MockNetRuntime<TokioNativeTlsRuntime> = net
        .builder()
        .add_address(crate::common::CLIENT_IP.parse().expect("ip"))
        .runtime(rt);
    assert_eq!(drive_runtime(&mocked), "driven");
    assert_eq!(drive(&mocked), 7);
}
