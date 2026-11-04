//! Group `mock_tls_net` — the in-process mock network models a TLS listener:
//! a connection to one arrives carrying a certificate, must be wrapped before
//! it can be used, and then behaves like the plain stream underneath it.

use futures::io::{AsyncReadExt, AsyncWriteExt};
use std::io::ErrorKind;
use std::io::Result as IoResult;
use std::net::SocketAddr;
use tor_rtcompat::tls::{CertifiedConn, TlsConnector};
use tor_rtcompat::{TcpListener, TcpProvider, TlsProvider};
use tor_rtmock::io::{stream_pair, LocalStream};
use tor_rtmock::net::{MockNetProvider, MockNetwork};

/// The certificate the test listener presents.
const CERT: &[u8] = b"mock-listener-certificate";

/// A client provider and a server provider on the same mock network.
fn pair() -> (MockNetProvider, MockNetProvider) {
    let net = MockNetwork::new();
    let client = net
        .builder()
        .add_address("192.0.2.11".parse().expect("address"))
        .provider();
    let server = net
        .builder()
        .add_address("192.0.2.12".parse().expect("address"))
        .provider();
    (client, server)
}

/// The address the server listens on.
fn server_addr() -> SocketAddr {
    "192.0.2.12:443".parse().expect("address")
}

#[test]
fn mock_tls_net__a_plain_connection_carries_bytes_in_both_directions() {
    tor_rtcompat::tokio::test_with_runtime(|_rt| async move {
        let (client, server) = pair();
        let listener = server.listen(&server_addr()).await.expect("listener");
        let addr = listener.local_addr().expect("listener address");
        let (out, back): (IoResult<()>, IoResult<()>) = futures::join!(
            async {
                let mut conn = client.connect(&addr).await?;
                conn.write_all(b"ping").await?;
                conn.close().await?;
                Ok(())
            },
            async {
                let (mut conn, from) = listener.accept().await?;
                assert_eq!(
                    from.ip(),
                    "192.0.2.11".parse::<std::net::IpAddr>().expect("address")
                );
                let mut got = Vec::new();
                conn.read_to_end(&mut got).await?;
                assert_eq!(&got[..], b"ping");
                Ok(())
            }
        );
        out.expect("client side");
        back.expect("server side");
    });
}

#[test]
fn mock_tls_net__a_stream_from_a_tls_listener_refuses_plain_use() {
    tor_rtcompat::tokio::test_with_runtime(|_rt| async move {
        let (client, server) = pair();
        let listener = server
            .listen_tls(&server_addr(), CERT.to_vec())
            .expect("tls listener");
        let addr = listener.local_addr().expect("listener address");
        let (accepted, raw) = futures::join!(listener.accept(), client.connect(&addr));
        accepted.expect("the listener should see the connection");
        let mut raw = raw.expect("connecting to a TLS listener should succeed");
        let write_err = raw
            .write_all(b"cleartext")
            .await
            .expect_err("a certificate-bearing stream must not accept plain writes");
        assert_eq!(write_err.kind(), ErrorKind::Other);
        let mut buf = [0_u8; 4];
        let read_err = raw
            .read(&mut buf)
            .await
            .expect_err("a certificate-bearing stream must not accept plain reads");
        assert_eq!(read_err.kind(), ErrorKind::Other);
    });
}

#[test]
fn mock_tls_net__wrapping_the_stream_yields_the_listeners_certificate() {
    tor_rtcompat::tokio::test_with_runtime(|_rt| async move {
        let (client, server) = pair();
        let listener = server
            .listen_tls(&server_addr(), CERT.to_vec())
            .expect("tls listener");
        let addr = listener.local_addr().expect("listener address");
        let connector = TlsProvider::<LocalStream>::tls_connector(&client);
        let (accepted, session) = futures::join!(listener.accept(), async {
            let stream = client.connect(&addr).await?;
            connector.negotiate_unvalidated(stream, "relay.example").await
        });
        accepted.expect("the listener should see the connection");
        let session = session.expect("negotiation should succeed");
        assert_eq!(
            session.peer_certificate().expect("certificate"),
            Some(CERT.to_vec())
        );
    });
}

#[test]
fn mock_tls_net__bytes_pass_through_the_wrapped_stream() {
    tor_rtcompat::tokio::test_with_runtime(|_rt| async move {
        let (client, server) = pair();
        let listener = server
            .listen_tls(&server_addr(), CERT.to_vec())
            .expect("tls listener");
        let addr = listener.local_addr().expect("listener address");
        let connector = TlsProvider::<LocalStream>::tls_connector(&client);
        let (served, sent): (IoResult<()>, IoResult<()>) = futures::join!(
            async {
                let (mut conn, _from) = listener.accept().await?;
                let mut got = Vec::new();
                conn.read_to_end(&mut got).await?;
                assert_eq!(&got[..], b"over the session");
                Ok(())
            },
            async {
                let stream = client.connect(&addr).await?;
                let mut session = connector.negotiate_unvalidated(stream, "relay.example").await?;
                session.write_all(b"over the session").await?;
                session.close().await?;
                Ok(())
            }
        );
        served.expect("server side");
        sent.expect("client side");
    });
}

#[test]
fn mock_tls_net__a_stream_with_no_certificate_cannot_be_wrapped() {
    tor_rtcompat::tokio::test_with_runtime(|_rt| async move {
        let (client, server) = pair();
        let connector = TlsProvider::<LocalStream>::tls_connector(&client);

        // A stream that never came from a listener at all.
        let (loose, _other_end) = stream_pair();
        let err = connector
            .negotiate_unvalidated(loose, "relay.example")
            .await
            .err()
            .expect("wrapping a stream with no certificate must fail");
        assert_eq!(err.kind(), ErrorKind::Other);

        // And a stream from a listener that is not a TLS listener.
        let listener = server.listen(&server_addr()).await.expect("listener");
        let addr = listener.local_addr().expect("listener address");
        let (accepted, wrapped) = futures::join!(listener.accept(), async {
            let stream = client.connect(&addr).await?;
            connector.negotiate_unvalidated(stream, "relay.example").await
        });
        accepted.expect("the listener should see the connection");
        let err = wrapped.err().expect("a plain listener offers no certificate");
        assert_eq!(err.kind(), ErrorKind::Other);
    });
}
