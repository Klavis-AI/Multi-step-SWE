//! Group `tls_generic` — the TLS boundary is generic over the transport it
//! wraps, so a caller can bring its own TCP stream type and its own TLS
//! implementation, and a runtime is a TLS provider for the streams it makes.

use async_trait::async_trait;
use futures::io::{AsyncRead, AsyncReadExt, AsyncWrite, AsyncWriteExt};
use std::io::{Error as IoError, ErrorKind, Result as IoResult};
use std::net::SocketAddr;
use std::pin::Pin;
use std::task::{Context, Poll};
use tor_rtcompat::tls::{CertifiedConn, TlsConnector};
use tor_rtcompat::{Runtime, TcpListener, TcpProvider, TlsProvider};
use tor_rtmock::io::{stream_pair, LocalStream};
use tor_rtmock::net::MockNetwork;
use tor_rtmock::MockNetRuntime;

/// A transport that is deliberately *not* the type any shipped provider
/// works with: it exists to show that the TLS traits take the stream type
/// as a parameter rather than fixing it.
struct Pipe(LocalStream);

impl AsyncRead for Pipe {
    fn poll_read(mut self: Pin<&mut Self>, cx: &mut Context<'_>, buf: &mut [u8]) -> Poll<IoResult<usize>> {
        Pin::new(&mut self.0).poll_read(cx, buf)
    }
}
impl AsyncWrite for Pipe {
    fn poll_write(mut self: Pin<&mut Self>, cx: &mut Context<'_>, buf: &[u8]) -> Poll<IoResult<usize>> {
        Pin::new(&mut self.0).poll_write(cx, buf)
    }
    fn poll_flush(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<IoResult<()>> {
        Pin::new(&mut self.0).poll_flush(cx)
    }
    fn poll_close(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<IoResult<()>> {
        Pin::new(&mut self.0).poll_close(cx)
    }
}

/// The "TLS" session our first-party provider produces: the bytes are passed
/// straight through, and the certificate records which transport was wrapped
/// and under which name.
struct Session<S> {
    /// The stream we were handed.
    inner: S,
    /// The certificate this session claims its peer presented.
    cert: Vec<u8>,
}

impl<S: AsyncRead + Unpin> AsyncRead for Session<S> {
    fn poll_read(mut self: Pin<&mut Self>, cx: &mut Context<'_>, buf: &mut [u8]) -> Poll<IoResult<usize>> {
        Pin::new(&mut self.inner).poll_read(cx, buf)
    }
}
impl<S: AsyncWrite + Unpin> AsyncWrite for Session<S> {
    fn poll_write(mut self: Pin<&mut Self>, cx: &mut Context<'_>, buf: &[u8]) -> Poll<IoResult<usize>> {
        Pin::new(&mut self.inner).poll_write(cx, buf)
    }
    fn poll_flush(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<IoResult<()>> {
        Pin::new(&mut self.inner).poll_flush(cx)
    }
    fn poll_close(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<IoResult<()>> {
        Pin::new(&mut self.inner).poll_close(cx)
    }
}
impl<S> CertifiedConn for Session<S> {
    fn peer_certificate(&self) -> IoResult<Option<Vec<u8>>> {
        Ok(Some(self.cert.clone()))
    }
}

/// A first-party connector that tags each session with the transport it was
/// asked to wrap.
#[derive(Clone)]
struct Tagging {
    /// Prefix written into every certificate this connector produces.
    tag: &'static str,
}

/// Build the certificate for a session over `tag` with name `sni`.
fn cert_for(tag: &str, sni: &str) -> Vec<u8> {
    format!("{}{}", tag, sni).into_bytes()
}

#[async_trait]
impl TlsConnector<LocalStream> for Tagging {
    type Conn = Session<LocalStream>;
    async fn negotiate_unvalidated(
        &self,
        stream: LocalStream,
        sni_hostname: &str,
    ) -> IoResult<Self::Conn> {
        if sni_hostname.is_empty() {
            return Err(IoError::new(ErrorKind::InvalidInput, "no name to negotiate for"));
        }
        Ok(Session {
            inner: stream,
            cert: cert_for(self.tag, sni_hostname),
        })
    }
}

#[async_trait]
impl TlsConnector<Pipe> for Tagging {
    type Conn = Session<Pipe>;
    async fn negotiate_unvalidated(&self, stream: Pipe, sni_hostname: &str) -> IoResult<Self::Conn> {
        if sni_hostname.is_empty() {
            return Err(IoError::new(ErrorKind::InvalidInput, "no name to negotiate for"));
        }
        Ok(Session {
            inner: stream,
            cert: cert_for("pipe/", sni_hostname),
        })
    }
}

/// A single first-party provider that serves two different transports.
#[derive(Clone)]
struct BothWays;

impl TlsProvider<LocalStream> for BothWays {
    type Connector = Tagging;
    type TlsStream = Session<LocalStream>;
    fn tls_connector(&self) -> Tagging {
        Tagging { tag: "local/" }
    }
}

impl TlsProvider<Pipe> for BothWays {
    type Connector = Tagging;
    type TlsStream = Session<Pipe>;
    fn tls_connector(&self) -> Tagging {
        Tagging { tag: "pipe/" }
    }
}

/// Ask any TLS provider for the connector it uses with streams of type `S`.
fn connector_of<S, P: TlsProvider<S>>(provider: &P) -> P::Connector {
    provider.tls_connector()
}

/// Ask any runtime for the connector it uses with its own TCP streams.
fn runtime_connector<R: Runtime>(
    rt: &R,
) -> <R as TlsProvider<<R as TcpProvider>::TcpStream>>::Connector {
    rt.tls_connector()
}

#[test]
fn tls_generic__a_first_party_provider_wraps_the_transport_it_chose() {
    tor_rtcompat::tokio::test_with_runtime(|_rt| async move {
        let (client, mut server) = stream_pair();
        let connector = connector_of::<LocalStream, _>(&BothWays);
        let mut session = connector
            .negotiate_unvalidated(client, "relay.example")
            .await
            .expect("negotiation over a local stream should succeed");
        let (w, r): (IoResult<()>, IoResult<()>) = futures::join!(
            async {
                session.write_all(b"through the session").await?;
                session.close().await
            },
            async {
                let mut got = Vec::new();
                server.read_to_end(&mut got).await?;
                assert_eq!(&got[..], &b"through the session"[..]);
                Ok(())
            }
        );
        w.expect("write side");
        r.expect("read side");
    });
}

#[test]
fn tls_generic__the_session_reports_the_certificate_it_negotiated() {
    tor_rtcompat::tokio::test_with_runtime(|_rt| async move {
        let (client, _server) = stream_pair();
        let session = connector_of::<LocalStream, _>(&BothWays)
            .negotiate_unvalidated(client, "relay.example")
            .await
            .expect("negotiation should succeed");
        assert_eq!(
            session.peer_certificate().expect("certificate should be readable"),
            Some(b"local/relay.example".to_vec())
        );
    });
}

#[test]
fn tls_generic__one_provider_serves_two_different_transports() {
    tor_rtcompat::tokio::test_with_runtime(|_rt| async move {
        let (a, _a2) = stream_pair();
        let (b, _b2) = stream_pair();
        let over_local = connector_of::<LocalStream, _>(&BothWays)
            .negotiate_unvalidated(a, "same.name")
            .await
            .expect("local stream");
        let over_pipe = connector_of::<Pipe, _>(&BothWays)
            .negotiate_unvalidated(Pipe(b), "same.name")
            .await
            .expect("pipe");
        assert_eq!(
            over_local.peer_certificate().expect("cert"),
            Some(b"local/same.name".to_vec())
        );
        assert_eq!(
            over_pipe.peer_certificate().expect("cert"),
            Some(b"pipe/same.name".to_vec())
        );
    });
}

#[test]
fn tls_generic__negotiation_may_reject_the_stream_it_was_handed() {
    tor_rtcompat::tokio::test_with_runtime(|_rt| async move {
        let (client, _server) = stream_pair();
        let outcome = connector_of::<LocalStream, _>(&BothWays)
            .negotiate_unvalidated(client, "")
            .await;
        match outcome {
            Err(e) => assert_eq!(e.kind(), ErrorKind::InvalidInput),
            Ok(_) => panic!("an unnamed negotiation should have been refused"),
        }
    });
}

#[test]
fn tls_generic__the_shipped_mock_provider_satisfies_the_same_bound() {
    tor_rtcompat::tokio::test_with_runtime(|_rt| async move {
        let net = MockNetwork::new();
        let server = net
            .builder()
            .add_address("198.51.100.9".parse().expect("address"))
            .provider();
        let client = net
            .builder()
            .add_address("198.51.100.10".parse().expect("address"))
            .provider();
        let listener = server
            .listen_tls(
                &"198.51.100.9:443".parse::<SocketAddr>().expect("address"),
                b"server-certificate".to_vec(),
            )
            .expect("listener");
        let addr = listener.local_addr().expect("listener address");
        let connector = connector_of::<LocalStream, _>(&client);
        let (accepted, negotiated) = futures::join!(listener.accept(), async {
            let stream = client.connect(&addr).await?;
            connector.negotiate_unvalidated(stream, "relay.example").await
        });
        accepted.expect("the listener should see the connection");
        let session = negotiated.expect("negotiation should succeed");
        assert_eq!(
            session.peer_certificate().expect("cert"),
            Some(b"server-certificate".to_vec())
        );
    });
}

#[test]
fn tls_generic__a_runtime_is_a_tls_provider_for_its_own_tcp_streams() {
    tor_rtcompat::tokio::test_with_runtime(|rt| async move {
        let net = MockNetwork::new();
        let server = net
            .builder()
            .add_address("198.51.100.11".parse().expect("address"))
            .provider();
        let listener = server
            .listen_tls(
                &"198.51.100.11:443".parse::<SocketAddr>().expect("address"),
                b"runtime-certificate".to_vec(),
            )
            .expect("listener");
        let addr = listener.local_addr().expect("listener address");
        let client: MockNetRuntime<_> = net
            .builder()
            .add_address("198.51.100.12".parse().expect("address"))
            .runtime(rt);
        // Only the `Runtime` bound is used from here on: the connector, the
        // stream it wraps and the session it returns are all associated types.
        let connector = runtime_connector(&client);
        let (accepted, negotiated) = futures::join!(listener.accept(), async {
            let stream = client.connect(&addr).await?;
            connector.negotiate_unvalidated(stream, "relay.example").await
        });
        accepted.expect("the listener should see the connection");
        let session = negotiated.expect("negotiation should succeed");
        assert_eq!(
            session.peer_certificate().expect("cert"),
            Some(b"runtime-certificate".to_vec())
        );
    });
}
