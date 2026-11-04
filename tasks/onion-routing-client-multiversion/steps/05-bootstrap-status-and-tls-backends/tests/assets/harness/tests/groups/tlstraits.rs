//! Group `tlstraits` (peripheral) — the trait vocabulary a selectable TLS
//! backend has to speak.
//!
//! Per scope decision S4 the TLS implementation itself is out of scope, so
//! these cases use `tor_rtmock`'s provider as the stand-in backend and check
//! the shape of the boundary: where the traits live, that a provider hands out
//! a connector, that a negotiated connection reports the peer certificate as
//! raw DER, and that a composed runtime forwards the component's connector
//! type unchanged.

use std::net::SocketAddr;

use tor_rtcompat::tls::{CertifiedConn, TlsConnector};
use tor_rtcompat::{tokio::TokioNativeTlsRuntime, CompoundRuntime, TcpProvider, TlsProvider};
use tor_rtmock::io::LocalStream;
use tor_rtmock::net::{MockNetProvider, MockNetwork};

type Composed =
    CompoundRuntime<TokioNativeTlsRuntime, TokioNativeTlsRuntime, MockNetProvider, MockNetProvider>;

fn relay_addr() -> SocketAddr {
    crate::common::RELAY_ADDR.parse().expect("relay address")
}

#[test]
fn tlstraits__tls_module_reexports_the_connector_trait() {
    // Reachable through the `tls` module, which is the documented path.
    fn takes_connector<C: TlsConnector<LocalStream>>() {}
    takes_connector::<<MockNetProvider as TlsProvider<LocalStream>>::Connector>();
    assert_eq!(
        std::any::type_name::<<MockNetProvider as TlsProvider<LocalStream>>::Connector>()
            .rsplit("::")
            .next(),
        Some("MockTlsConnector")
    );
}

#[test]
fn tlstraits__tls_module_reexports_the_certified_conn_trait() {
    fn takes_certified<C: CertifiedConn>() {}
    takes_certified::<<MockNetProvider as TlsProvider<LocalStream>>::TlsStream>();
    // Same trait, whether reached through `tls::` or through the crate root.
    fn takes_certified_root<C: tor_rtcompat::CertifiedConn>() {}
    takes_certified_root::<<MockNetProvider as TlsProvider<LocalStream>>::TlsStream>();
}

#[test]
fn tlstraits__provider_hands_out_a_connector() {
    let net = MockNetwork::new();
    let provider = net
        .builder()
        .add_address(crate::common::CLIENT_IP.parse().expect("ip"))
        .provider();
    let connector = <MockNetProvider as TlsProvider<LocalStream>>::tls_connector(&provider);
    // Each call returns a value, not a borrow of the provider, so a connector
    // outlives the call that produced it.
    let second = <MockNetProvider as TlsProvider<LocalStream>>::tls_connector(&provider);
    drop(provider);
    let _ = (connector, second);
}

#[test]
fn tlstraits__certified_conn_returns_der_bytes() {
    TokioNativeTlsRuntime::run_test(|rt| async move {
        let net = MockNetwork::new();
        let client = net
            .builder()
            .add_address(crate::common::CLIENT_IP.parse().expect("ip"))
            .provider();
        let server = net
            .builder()
            .add_address(crate::common::RELAY_IP.parse().expect("ip"))
            .provider();
        let _listener = server
            .listen_tls(&relay_addr(), crate::common::PEER_CERT_B.to_vec())
            .expect("tls listener");
        let stream = client.connect(&relay_addr()).await.expect("connect");
        let conn = <MockNetProvider as TlsProvider<LocalStream>>::tls_connector(&client)
            .negotiate_unvalidated(stream, "ignored")
            .await
            .expect("negotiate");
        let cert = conn
            .peer_certificate()
            .expect("peer_certificate query")
            .expect("a certificate was presented");
        assert_eq!(cert.as_slice(), crate::common::PEER_CERT_B);
        let tlv = crate::common::parse_der_tlv(&cert).expect("DER header");
        assert_eq!(tlv.tag, 0x30);
        let _ = rt;
    });
}

#[test]
fn tlstraits__compound_runtime_exposes_the_component_connector_type() {
    // Composition must not wrap or rename the connector: the runtime hands
    // back exactly what the TLS component declared.
    assert_eq!(
        std::any::type_name::<<Composed as TlsProvider<LocalStream>>::Connector>(),
        std::any::type_name::<<MockNetProvider as TlsProvider<LocalStream>>::Connector>()
    );
    assert_eq!(
        std::any::type_name::<<Composed as TcpProvider>::TcpStream>(),
        std::any::type_name::<LocalStream>()
    );
}
