//! Group `hyperconn`: the connector itself.
//!
//! instruction.md: "an HTTP client over Arti". `ArtiHttpConnector` is a
//! `hyper::service::Service<Uri>`: hyper hands it a URI and gets back a future
//! that yields a connection. It must be always-ready, it must reject a URI it
//! cannot serve before opening anything, and it must route an http(s) URI
//! through the Tor client it was built with.

use std::task::{Context, Poll};

use arti_hyper::{ArtiHttpConnector, ConnectionError};
use hyper::http::Uri;
use hyper::service::Service;
use tls_api::{TlsConnector as _, TlsConnectorBuilder as _};
use tor_error::{ErrorKind, HasKind};
use tor_rtcompat::{BlockOn, PreferredRuntime};

use crate::common::{manual_client, runtime, ClientDirs};

/// The concrete connector type the driver exercises.
type Connector = ArtiHttpConnector<PreferredRuntime, tls_api_native_tls::TlsConnector>;

/// Build a connector over a client that will never bootstrap on its own.
fn connector(rt: PreferredRuntime, dirs: &ClientDirs) -> Connector {
    let tls = tls_api_native_tls::TlsConnector::builder()
        .expect("TLS connector builder")
        .build()
        .expect("TLS connector");
    ArtiHttpConnector::new(manual_client(rt, dirs), tls)
}

/// Parse a URI that is known to be well-formed.
fn uri(s: &str) -> Uri {
    s.parse().expect("test URI should parse")
}

#[test]
fn hyperconn__poll_ready_is_immediately_ready() {
    let dirs = ClientDirs::new();
    let mut conn = connector(runtime(), &dirs);
    let waker = futures::task::noop_waker();
    let mut cx = Context::from_waker(&waker);
    // A connector that is never ready would wedge hyper; this one is ready
    // without ever being woken.
    match conn.poll_ready(&mut cx) {
        Poll::Ready(Ok(())) => {}
        other => panic!("poll_ready must be Ready(Ok(())), got {:?}", other),
    }
    assert!(conn.poll_ready(&mut cx).is_ready());
}

#[test]
fn hyperconn__unsupported_scheme_is_rejected_before_connecting() {
    let dirs = ClientDirs::new();
    let rt = runtime();
    let mut conn = connector(rt.clone(), &dirs);
    let err = rt
        .block_on(conn.call(uri("ftp://example.com/")))
        .map(|_| ())
        .expect_err("ftp is not an http(s) scheme");
    assert!(
        matches!(err, ConnectionError::UnsupportedUriScheme { .. }),
        "unexpected error: {:?}",
        err
    );
    assert_eq!(err.kind(), ErrorKind::NotImplemented);
}

#[test]
fn hyperconn__uri_without_a_scheme_is_rejected() {
    let dirs = ClientDirs::new();
    let rt = runtime();
    let mut conn = connector(rt.clone(), &dirs);
    let err = rt
        .block_on(conn.call(uri("/index.html")))
        .map(|_| ())
        .expect_err("a schemeless URI cannot be served");
    assert!(
        matches!(err, ConnectionError::UnsupportedUriScheme { .. }),
        "unexpected error: {:?}",
        err
    );
}

#[test]
fn hyperconn__http_uri_is_handed_to_the_tor_client() {
    // An http URI is accepted by the connector and turned into a Tor stream
    // request; because this client has not bootstrapped, the request comes
    // back as the client's own error rather than as a URI complaint.
    let dirs = ClientDirs::new();
    let rt = runtime();
    let mut conn = connector(rt.clone(), &dirs);
    for target in ["http://example.com/", "https://example.com/status"] {
        let err = rt
            .block_on(conn.call(uri(target)))
            .map(|_| ())
            .expect_err("an unbootstrapped client cannot serve a request");
        assert!(
            matches!(err, ConnectionError::Arti(_)),
            "{} should have reached the Tor client, got {:?}",
            target,
            err
        );
        assert_eq!(err.kind(), ErrorKind::BootstrapRequired);
    }
}

#[test]
fn hyperconn__a_clone_uses_the_same_client() {
    let dirs = ClientDirs::new();
    let rt = runtime();
    let conn = connector(rt.clone(), &dirs);
    let mut cloned = conn.clone();
    let mut original = conn;
    let a = rt
        .block_on(original.call(uri("http://example.com/")))
        .map(|_| ())
        .expect_err("unbootstrapped");
    let b = rt
        .block_on(cloned.call(uri("http://example.com/")))
        .map(|_| ())
        .expect_err("unbootstrapped");
    assert_eq!(a.kind(), ErrorKind::BootstrapRequired);
    assert_eq!(b.kind(), a.kind());
    assert_eq!(b.to_string(), a.to_string());
}
