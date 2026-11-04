//! Group `tlsbackend`: Selectable TLS backends (inherited from step 05).
//!
//! instruction.md: "**Preserve** bootstrap status reporting and the selectable
//! TLS backends from step 5. It is re-checked here against the cumulative
//! workspace, so it has to keep working, not merely have worked."
//!
//! Scope decision S4: TLS/crypto *primitives* are out of scope. What is graded
//! here is that both backends still satisfy `tor_rtcompat::Runtime` well enough
//! to compose: run futures, spawn tasks, and time out. No handshake is driven
//! and no certificate is validated.
//!
//! TEST_INTERFACE.md: `tor_rtcompat::Runtime`, `tor_rtcompat::SleepProviderExt`,
//! `tor_rtcompat::tokio::{TokioNativeTlsRuntime, TokioRustlsRuntime}`

use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::Arc;

use futures::task::SpawnExt;
use tor_rtcompat::tokio::{TokioNativeTlsRuntime, TokioRustlsRuntime};
use tor_rtcompat::{BlockOn, Runtime, SleepProviderExt};

/// Exercise the "runs futures" half of the `Runtime` contract.
///
/// Generic over `R: Runtime` so the compiler itself checks that both concrete
/// backends still implement the trait; `rt.block_on` below is the runtime half.
fn run_one_future<R: Runtime>(rt: &R) -> u32 {
    rt.block_on(async { 40 + 2 })
}

/// Exercise the "spawns tasks" half of the `Runtime` contract.
fn run_spawned<R: Runtime + SpawnExt>(rt: &R) -> u32 {
    let counter = Arc::new(AtomicUsize::new(0));
    let (tx, rx) = futures::channel::oneshot::channel();
    let counter2 = Arc::clone(&counter);
    rt.spawn(async move {
        counter2.store(7, Ordering::SeqCst);
        let _ = tx.send(());
    })
    .expect("spawn a task on the runtime");

    rt.block_on(async move {
        let _ = rx.await;
        counter.load(Ordering::SeqCst) as u32
    })
}

#[test]
fn tlsbackend__native_tls_runs_futures() {
    let rt = TokioNativeTlsRuntime::create().expect("create native-tls runtime");
    assert_eq!(run_one_future(&rt), 42);
}

#[test]
fn tlsbackend__rustls_runs_futures() {
    let rt = TokioRustlsRuntime::create().expect("create rustls runtime");
    assert_eq!(run_one_future(&rt), 42);
}

#[test]
fn tlsbackend__native_tls_spawns_tasks() {
    let rt = TokioNativeTlsRuntime::create().expect("create native-tls runtime");
    assert_eq!(run_spawned(&rt), 7);
}

#[test]
fn tlsbackend__rustls_spawns_tasks() {
    let rt = TokioRustlsRuntime::create().expect("create rustls runtime");
    assert_eq!(run_spawned(&rt), 7);
}

#[test]
fn tlsbackend__native_tls_composes_with_timeout() {
    let rt = TokioNativeTlsRuntime::create().expect("create native-tls runtime");
    rt.block_on(async {
        let ready = futures::future::ready(99u32);
        let out = rt.timeout(std::time::Duration::from_secs(3600), ready).await;
        assert_eq!(out.expect("timeout must not fire"), 99);
    });
}

#[test]
fn tlsbackend__rustls_composes_with_timeout() {
    let rt = TokioRustlsRuntime::create().expect("create rustls runtime");
    rt.block_on(async {
        let ready = futures::future::ready(99u32);
        let out = rt.timeout(std::time::Duration::from_secs(3600), ready).await;
        assert_eq!(out.expect("timeout must not fire"), 99);
    });
}

#[test]
fn tlsbackend__runtime_handles_are_cloneable() {
    let rt = TokioNativeTlsRuntime::create().expect("create native-tls runtime");
    let clone = rt.clone();
    // Both handles refer to the same underlying executor: work submitted
    // through the clone is visible when awaited through the original.
    let (tx, rx) = futures::channel::oneshot::channel();
    clone.spawn(async move {
        let _ = tx.send(1234u32);
    })
    .expect("spawn through clone");
    let got = rt.block_on(async move { rx.await.expect("task ran") });
    assert_eq!(got, 1234);
}
