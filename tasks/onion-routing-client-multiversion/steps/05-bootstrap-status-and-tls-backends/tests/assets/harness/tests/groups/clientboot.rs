//! Group `clientboot` — the runtime-override bootstrap conveniences.
//!
//! Three entry points must exist: one that picks up the ambient tokio runtime,
//! one that picks up the ambient async-std runtime, and the general one that
//! takes whatever runtime the caller supplies. Constructing the returned
//! future performs no work, so the output type can be pinned without ever
//! touching the network.

use arti_client::{TorClient, TorClientConfig};
use tor_rtcompat::tokio::TokioNativeTlsRuntime;

type Tokio = tor_rtcompat::tokio::PreferredRuntime;
type AsyncStd = tor_rtcompat::async_std::PreferredRuntime;

/// Pin the `Output` of a future without polling it.
fn future_output_is<F, O>(_f: F)
where
    F: std::future::Future<Output = O>,
{
}

#[test]
fn clientboot__bootstrap_with_tokio_returns_a_tokio_backed_client() {
    let fut = TorClient::bootstrap_with_tokio(TorClientConfig::default());
    future_output_is::<_, arti_client::Result<TorClient<Tokio>>>(fut);
}

#[test]
fn clientboot__bootstrap_with_async_std_returns_an_async_std_backed_client() {
    let fut = TorClient::bootstrap_with_async_std(TorClientConfig::default());
    future_output_is::<_, arti_client::Result<TorClient<AsyncStd>>>(fut);
}

#[test]
fn clientboot__bootstrap_accepts_a_caller_supplied_runtime() {
    let rt = TokioNativeTlsRuntime::create().expect("tokio/native-tls runtime");
    let fut = TorClient::bootstrap(rt, TorClientConfig::default());
    future_output_is::<_, arti_client::Result<TorClient<TokioNativeTlsRuntime>>>(fut);
}

#[test]
fn clientboot__the_two_backend_modules_name_different_preferred_runtimes() {
    let tokio_name = std::any::type_name::<Tokio>();
    let async_std_name = std::any::type_name::<AsyncStd>();
    assert!(
        tokio_name.ends_with("TokioNativeTlsRuntime"),
        "unexpected tokio preferred runtime: {tokio_name}"
    );
    assert!(
        async_std_name.ends_with("AsyncStdNativeTlsRuntime"),
        "unexpected async-std preferred runtime: {async_std_name}"
    );
    assert_ne!(tokio_name, async_std_name);
}

#[test]
fn clientboot__the_crate_level_preferred_runtime_is_the_tokio_one() {
    assert_eq!(
        std::any::type_name::<tor_rtcompat::PreferredRuntime>(),
        std::any::type_name::<Tokio>()
    );
}
