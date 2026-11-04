//! Group `compound` — `CompoundRuntime`, the type that makes a runtime's four
//! halves independently replaceable.
//!
//! A runtime assembled from a real tokio spawner and sleeper plus a mock
//! network for TCP and TLS must behave as a full `Runtime`, and each component
//! must actually be the one that was handed in — that is the whole point of
//! the type and the mechanism by which a TLS backend becomes selectable.

use std::net::SocketAddr;
use std::sync::Arc;

use futures::io::{AsyncReadExt, AsyncWriteExt};
use futures::task::SpawnExt;
use tor_rtcompat::{
    tokio::TokioNativeTlsRuntime, BlockOn, CompoundRuntime, Runtime, SleepProvider, TcpListener,
    TcpProvider,
};
use tor_rtmock::net::{MockNetProvider, MockNetwork};

/// The runtime under test: tokio for spawning and sleeping, the mock network
/// for TCP and TLS.
type Composed = CompoundRuntime<TokioNativeTlsRuntime, TokioNativeTlsRuntime, MockNetProvider, MockNetProvider>;

fn assert_is_runtime<R: Runtime>() {}

fn run_via_runtime<R: Runtime>(rt: &R) -> u32 {
    rt.block_on(async { 5 })
}

fn compose(rt: TokioNativeTlsRuntime, net: &Arc<MockNetwork>, ip: &str) -> Composed {
    let provider = net
        .builder()
        .add_address(ip.parse().expect("mock address"))
        .provider();
    CompoundRuntime::new(rt.clone(), rt, provider.clone(), provider)
}

fn client_addr() -> SocketAddr {
    format!("{}:9001", crate::common::CLIENT_IP)
        .parse()
        .expect("client socket address")
}

#[test]
fn compound__debug_output_is_opaque() {
    let rt = TokioNativeTlsRuntime::create().expect("runtime");
    let net = MockNetwork::new();
    let composed = compose(rt, &net, crate::common::CLIENT_IP);
    assert_eq!(format!("{composed:?}"), "CompoundRuntime { .. }");
}

#[test]
fn compound__clone_is_cheap_and_shares_state() {
    TokioNativeTlsRuntime::run_test(|rt| async move {
        let net = MockNetwork::new();
        let composed = compose(rt, &net, crate::common::CLIENT_IP);
        let other = composed.clone();
        // Listen through one handle, connect through the other: the clone must
        // share the same network component, not get a fresh one.
        let listener = composed.listen(&client_addr()).await.expect("listen");
        let mut conn = other.connect(&client_addr()).await.expect("connect via clone");
        let (mut incoming, source) = listener.accept().await.expect("accept");
        assert_eq!(
            source.ip().to_string(),
            crate::common::CLIENT_IP,
            "the connection arrived from the composed runtime's own address"
        );
        // The stream the two halves share is a working byte pipe, not just a
        // handle: what is written at one end is read, in order, at the other.
        conn.write_all(b"ping").await.expect("write to the accepted stream");
        conn.flush().await.expect("flush");
        let mut got = [0u8; 4];
        incoming.read_exact(&mut got).await.expect("read what was written");
        assert_eq!(&got, b"ping", "the shared stream did not carry the bytes");
    });
}

#[test]
fn compound__block_on_runs_a_future_to_completion() {
    let rt = TokioNativeTlsRuntime::create().expect("runtime");
    let net = MockNetwork::new();
    let composed = compose(rt, &net, crate::common::CLIENT_IP);
    assert_eq!(composed.block_on(async { 6u32 * 7 }), 42);
}

#[test]
fn compound__spawn_delivers_work_to_the_spawn_component() {
    let rt = TokioNativeTlsRuntime::create().expect("runtime");
    let net = MockNetwork::new();
    let composed = compose(rt, &net, crate::common::CLIENT_IP);
    let (tx, rx) = futures::channel::oneshot::channel::<u32>();
    composed
        .spawn(async move {
            let _ = tx.send(99);
        })
        .expect("spawn onto the composed runtime");
    assert_eq!(composed.block_on(rx).expect("spawned task ran"), 99);
}

#[test]
fn compound__tcp_component_is_the_overridden_one() {
    assert_eq!(
        std::any::type_name::<<Composed as TcpProvider>::TcpStream>(),
        std::any::type_name::<<MockNetProvider as TcpProvider>::TcpStream>()
    );
    TokioNativeTlsRuntime::run_test(|rt| async move {
        let net = MockNetwork::new();
        let composed = compose(rt, &net, crate::common::CLIENT_IP);
        // Nothing is listening anywhere on the mock network, and the mock
        // network - not the operating system - is what answers.
        let err = composed
            .connect(&crate::common::DARK_ADDR.parse().expect("address"))
            .await
            .err()
            .expect("connecting to an unserved mock address must fail");
        assert_eq!(err.kind(), std::io::ErrorKind::ConnectionRefused);
        assert_eq!(err.to_string(), "Invalid operation on mock network");
    });
}

#[test]
fn compound__sleep_future_type_comes_from_the_sleep_component() {
    assert_eq!(
        std::any::type_name::<<Composed as SleepProvider>::SleepFuture>(),
        std::any::type_name::<<TokioNativeTlsRuntime as SleepProvider>::SleepFuture>()
    );
}

#[test]
fn compound__is_a_full_runtime() {
    assert_is_runtime::<Composed>();
    let rt = TokioNativeTlsRuntime::create().expect("runtime");
    let net = MockNetwork::new();
    let composed = compose(rt, &net, crate::common::CLIENT_IP);
    // Usable through the `Runtime` bound alone, which is what every arti
    // subsystem is generic over.
    assert_eq!(run_via_runtime(&composed), 5);
}
