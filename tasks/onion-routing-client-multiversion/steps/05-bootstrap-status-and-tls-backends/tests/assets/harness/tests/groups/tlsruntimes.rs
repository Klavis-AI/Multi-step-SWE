//! Group `tlsruntimes` — the concrete, backend-named runtimes.
//!
//! Naming the TLS backend in the runtime type is what makes the backend
//! selectable at all: `TokioNativeTlsRuntime` and `AsyncStdNativeTlsRuntime`
//! are two different types, each with the same constructor vocabulary
//! (`create`, `current`, `run_test`) and each a full `Runtime`.

use tor_rtcompat::async_std::AsyncStdNativeTlsRuntime;
use tor_rtcompat::tokio::TokioNativeTlsRuntime;
use tor_rtcompat::{BlockOn, Runtime};

fn assert_is_runtime<R: Runtime>() {}

#[test]
fn tlsruntimes__tokio_native_tls_runtime_debug_is_opaque() {
    let rt = TokioNativeTlsRuntime::create().expect("tokio/native-tls runtime");
    assert_eq!(format!("{rt:?}"), "TokioNativeTlsRuntime { .. }");
}

#[test]
fn tlsruntimes__async_std_native_tls_runtime_debug_is_opaque() {
    let rt = AsyncStdNativeTlsRuntime::create().expect("async-std/native-tls runtime");
    assert_eq!(format!("{rt:?}"), "AsyncStdNativeTlsRuntime { .. }");
}

#[test]
fn tlsruntimes__tokio_runtime_current_fails_outside_a_runtime() {
    // `current` adopts an *ambient* runtime; there is none here, and the
    // failure has to be reported rather than papered over by building one.
    assert!(
        TokioNativeTlsRuntime::current().is_err(),
        "current() must not silently create a runtime"
    );
}

#[test]
fn tlsruntimes__tokio_runtime_current_succeeds_inside_block_on() {
    let rt = TokioNativeTlsRuntime::create().expect("runtime");
    let found = rt.block_on(async { TokioNativeTlsRuntime::current().is_ok() });
    assert!(found, "block_on must establish the ambient tokio context");
}

#[test]
fn tlsruntimes__run_test_helper_drives_a_future() {
    let out: u32 = TokioNativeTlsRuntime::run_test(|_rt| async { 4 + 4 });
    assert_eq!(out, 8);
    // The future is driven inside the runtime's own context.
    let ambient = TokioNativeTlsRuntime::run_test(|_rt| async {
        TokioNativeTlsRuntime::current().is_ok()
    });
    assert!(ambient);
}

#[test]
fn tlsruntimes__create_runtime_returns_a_usable_runtime() {
    let rt = tor_rtcompat::create_runtime().expect("default runtime");
    assert_eq!(rt.block_on(async { "ok" }), "ok");
    // The crate-level default is the tokio/native-tls one.
    assert_eq!(
        std::any::type_name::<tor_rtcompat::PreferredRuntime>(),
        std::any::type_name::<TokioNativeTlsRuntime>()
    );
}

#[test]
fn tlsruntimes__native_tls_runtimes_are_full_runtimes() {
    assert_is_runtime::<TokioNativeTlsRuntime>();
    assert_is_runtime::<AsyncStdNativeTlsRuntime>();
    let rt = AsyncStdNativeTlsRuntime::create().expect("async-std runtime");
    assert_eq!(rt.block_on(async { 21 * 2 }), 42);
    assert_ne!(
        std::any::type_name::<TokioNativeTlsRuntime>(),
        std::any::type_name::<AsyncStdNativeTlsRuntime>()
    );
}
