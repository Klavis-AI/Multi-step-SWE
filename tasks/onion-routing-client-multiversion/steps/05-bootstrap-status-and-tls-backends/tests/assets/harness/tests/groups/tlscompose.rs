//! Group `tlscompose` — a pluggable TLS backend, composed into a runtime.
//!
//! Scope decision S4 puts the TLS implementation and the cryptography itself
//! out of scope; what is graded is the *composition*. So the TLS half of a
//! `CompoundRuntime` is filled with `tor_rtmock`'s TLS provider and these
//! cases check that the composed runtime really negotiates through it, really
//! surfaces the certificate that provider presented, and really fails when the
//! far end offers none.

use std::net::SocketAddr;

use tor_chanmgr::{ChanMgr, Error};
use tor_linkspec::OwnedChanTarget;
use tor_llcrypto::pk::{ed25519::Ed25519Identity, rsa::RsaIdentity};
use futures::task::SpawnExt;
use tor_rtcompat::tls::{CertifiedConn, TlsConnector};
use tor_rtcompat::{
    tokio::TokioNativeTlsRuntime, CompoundRuntime, TcpListener, TcpProvider, TlsProvider,
};
use tor_rtmock::io::LocalStream;
use tor_rtmock::net::{MockNetProvider, MockNetwork};

type Composed =
    CompoundRuntime<TokioNativeTlsRuntime, TokioNativeTlsRuntime, MockNetProvider, MockNetProvider>;

fn compose(rt: TokioNativeTlsRuntime, provider: MockNetProvider) -> Composed {
    CompoundRuntime::new(rt.clone(), rt, provider.clone(), provider)
}

fn relay_addr() -> SocketAddr {
    crate::common::RELAY_ADDR.parse().expect("relay address")
}

fn alt_relay_addr() -> SocketAddr {
    crate::common::RELAY_ALT_ADDR.parse().expect("alt address")
}

/// Negotiate once through the composed runtime's TLS half and return the
/// certificate the far end presented.
async fn negotiated_cert(client: &Composed, addr: &SocketAddr) -> Vec<u8> {
    let stream = client.connect(addr).await.expect("mock tcp connect");
    let conn = <Composed as TlsProvider<LocalStream>>::tls_connector(client)
        .negotiate_unvalidated(stream, "ignored")
        .await
        .expect("tls negotiation through the composed backend");
    conn.peer_certificate()
        .expect("peer_certificate query")
        .expect("the mock backend always presents a certificate")
}

#[test]
fn tlscompose__composed_runtime_yields_the_pluggable_tls_connector() {
    TokioNativeTlsRuntime::run_test(|rt| async move {
        let net = MockNetwork::new();
        let client = compose(
            rt,
            net.builder()
                .add_address(crate::common::CLIENT_IP.parse().expect("ip"))
                .provider(),
        );
        let server = net
            .builder()
            .add_address(crate::common::RELAY_IP.parse().expect("ip"))
            .provider();
        let _listener = server
            .listen_tls(&relay_addr(), crate::common::PEER_CERT_A.to_vec())
            .expect("tls listener");

        let cert = negotiated_cert(&client, &relay_addr()).await;
        assert_eq!(cert.as_slice(), crate::common::PEER_CERT_A);
        // The connector type is the one the TLS component declared, not the
        // one the spawn/sleep component would have supplied.
        assert_eq!(
            std::any::type_name::<<Composed as TlsProvider<LocalStream>>::Connector>(),
            std::any::type_name::<<MockNetProvider as TlsProvider<LocalStream>>::Connector>()
        );
    });
}

#[test]
fn tlscompose__negotiation_fails_when_the_listener_offers_no_certificate() {
    TokioNativeTlsRuntime::run_test(|rt| async move {
        let net = MockNetwork::new();
        let client = compose(
            rt,
            net.builder()
                .add_address(crate::common::CLIENT_IP.parse().expect("ip"))
                .provider(),
        );
        let server = net
            .builder()
            .add_address(crate::common::RELAY_IP.parse().expect("ip"))
            .provider();
        // A plain listener: TCP works, TLS must not.
        let _listener = server.listen(&relay_addr()).await.expect("plain listener");

        let stream = client.connect(&relay_addr()).await.expect("tcp connect");
        let err = <Composed as TlsProvider<LocalStream>>::tls_connector(&client)
            .negotiate_unvalidated(stream, "ignored")
            .await
            .err()
            .expect("a plain stream cannot be wrapped as TLS");
        assert_eq!(err.kind(), std::io::ErrorKind::Other);
        assert_eq!(err.to_string(), "attempted to wrap non-TLS stream!");
    });
}

#[test]
fn tlscompose__reported_certificate_parses_as_a_der_sequence() {
    TokioNativeTlsRuntime::run_test(|rt| async move {
        let net = MockNetwork::new();
        let client = compose(
            rt,
            net.builder()
                .add_address(crate::common::CLIENT_IP.parse().expect("ip"))
                .provider(),
        );
        let server = net
            .builder()
            .add_address(crate::common::RELAY_IP.parse().expect("ip"))
            .provider();
        let _listener = server
            .listen_tls(&relay_addr(), crate::common::PEER_CERT_A.to_vec())
            .expect("tls listener");

        // Parse what the candidate handed back, not the fixture: the bytes
        // must survive the round trip intact, tag, length and all.
        let cert = negotiated_cert(&client, &relay_addr()).await;
        let tlv = crate::common::parse_der_tlv(&cert).expect("a DER header");
        assert_eq!(tlv.tag, 0x30, "peer certificates are DER SEQUENCEs");
        assert_eq!(tlv.len, cert.len() - 3, "long-form length header");
        assert_eq!(tlv.value.len(), tlv.len);
        let inner = crate::common::parse_der_tlv(tlv.value).expect("an inner DER header");
        assert_eq!(inner.tag, 0x04);
    });
}

#[test]
fn tlscompose__channel_manager_uses_the_composed_tls_backend() {
    let err = TokioNativeTlsRuntime::run_test(|rt| async move {
        let net = MockNetwork::new();
        let client = compose(
            rt,
            net.builder()
                .add_address(crate::common::CLIENT_IP.parse().expect("ip"))
                .provider(),
        );
        let server = net
            .builder()
            .add_address(crate::common::RELAY_IP.parse().expect("ip"))
            .provider();
        let listener = server
            .listen_tls(&relay_addr(), crate::common::PEER_CERT_A.to_vec())
            .expect("tls listener");
        // Accept and immediately hang up, so the Tor link handshake that runs
        // *after* TLS fails at once instead of waiting for a timer.
        client
            .spawn(async move {
                while let Ok((stream, _)) = listener.accept().await {
                    drop(stream);
                }
            })
            .expect("spawn the accept loop");

        let target = OwnedChanTarget::new(
            vec![relay_addr()],
            Ed25519Identity::from([0x11; 32]),
            RsaIdentity::from([0x22; 20]),
        );
        ChanMgr::new(client)
            .get_or_launch(&target)
            .await
            .err()
            .expect("the mock peer speaks no Tor, so no channel completes")
    });
    // TLS succeeded - the certificate was accepted - and the failure happened
    // afterwards, in the Tor link handshake, rather than in the wrapper.
    assert!(
        !err.to_string().contains("non-TLS stream"),
        "the composed TLS backend was not used: {err}"
    );
    assert!(
        matches!(err, Error::Proto(_) | Error::ChanTimeout | Error::Io(_)),
        "unexpected channel error: {err:?}"
    );
}

#[test]
fn tlscompose__composed_runtime_tcp_half_is_the_mock_network() {
    assert_eq!(
        std::any::type_name::<<Composed as TcpProvider>::TcpStream>(),
        std::any::type_name::<LocalStream>()
    );
    assert_eq!(
        std::any::type_name::<<Composed as TlsProvider<LocalStream>>::TlsStream>(),
        std::any::type_name::<<MockNetProvider as TlsProvider<LocalStream>>::TlsStream>()
    );
}

#[test]
fn tlscompose__two_listeners_present_distinct_certificates() {
    TokioNativeTlsRuntime::run_test(|rt| async move {
        let net = MockNetwork::new();
        let client = compose(
            rt,
            net.builder()
                .add_address(crate::common::CLIENT_IP.parse().expect("ip"))
                .provider(),
        );
        let server = net
            .builder()
            .add_address(crate::common::RELAY_IP.parse().expect("ip"))
            .provider();
        let _a = server
            .listen_tls(&relay_addr(), crate::common::PEER_CERT_A.to_vec())
            .expect("first tls listener");
        let _b = server
            .listen_tls(&alt_relay_addr(), crate::common::PEER_CERT_B.to_vec())
            .expect("second tls listener");

        let first = negotiated_cert(&client, &relay_addr()).await;
        let second = negotiated_cert(&client, &alt_relay_addr()).await;
        assert_eq!(first.as_slice(), crate::common::PEER_CERT_A);
        assert_eq!(second.as_slice(), crate::common::PEER_CERT_B);
        assert_ne!(first, second);
    });
}
