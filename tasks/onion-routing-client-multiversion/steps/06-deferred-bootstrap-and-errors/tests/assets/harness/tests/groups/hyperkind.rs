//! Group `hyperkind`: how the HTTP-over-Arti adapter reports failure.
//!
//! instruction.md: "an HTTP client over Arti" plus "errors gain stable kinds".
//! `ConnectionError` is the error type hyper sees; it must carry a stable
//! `ErrorKind`, and it must classify its own faults (a URI it cannot use) apart
//! from the faults it merely relays (whatever the Tor client said).

use std::sync::Arc;

use arti_hyper::ConnectionError;
use hyper::http::Uri;
use tor_error::{ErrorKind, HasKind};
use tor_rtcompat::BlockOn;

use crate::common::{manual_client, runtime, ClientDirs};

/// Produce a genuine `arti_client::Error` without touching the network.
fn arti_error(target: &str) -> arti_client::Error {
    let dirs = ClientDirs::new();
    let rt = runtime();
    let client = manual_client(rt.clone(), &dirs);
    rt.block_on(client.connect(target.to_string()))
        .map(|_| ())
        .expect_err("this connection must fail offline")
}

/// Parse a URI that is known to be well-formed.
fn uri(s: &str) -> Uri {
    s.parse().expect("test URI should parse")
}

#[test]
fn hyperkind__unsupported_scheme_is_not_implemented() {
    let e = ConnectionError::UnsupportedUriScheme {
        uri: uri("ftp://example.com/"),
    };
    assert_eq!(e.kind(), ErrorKind::NotImplemented);
}

#[test]
fn hyperkind__missing_hostname_is_bad_api_usage() {
    // `Uri::default()` is the origin-form URI "/": a URI with no authority at
    // all, which is exactly the case this variant reports.
    let e = ConnectionError::MissingHostname {
        uri: Uri::default(),
    };
    assert_eq!(e.kind(), ErrorKind::BadApiUsage);
}

#[test]
fn hyperkind__tls_failure_is_a_remote_protocol_failure() {
    let e = ConnectionError::TLS(Arc::new(anyhow::anyhow!("handshake refused")));
    assert_eq!(e.kind(), ErrorKind::RemoteProtocolFailed);
}

#[test]
fn hyperkind__arti_errors_keep_their_own_kind() {
    // The adapter must not flatten the client's taxonomy into one kind: an
    // unbootstrapped client and a forbidden target stay distinguishable.
    let deferred = ConnectionError::from(arti_error("example.com:80"));
    assert_eq!(deferred.kind(), ErrorKind::BootstrapRequired);
    let forbidden = ConnectionError::from(arti_error("127.0.0.1:80"));
    assert_eq!(forbidden.kind(), ErrorKind::ForbiddenStreamTarget);
    assert_ne!(deferred.kind(), forbidden.kind());
}

#[test]
fn hyperkind__unsupported_scheme_display_names_the_uri() {
    let e = ConnectionError::UnsupportedUriScheme {
        uri: uri("ftp://example.com/"),
    };
    let text = e.to_string();
    assert!(
        text.starts_with("unsupported URI scheme in "),
        "unexpected rendering: {:?}",
        text
    );
    assert!(
        text.contains("ftp://example.com/"),
        "the offending URI must be shown: {:?}",
        text
    );
}

#[test]
fn hyperkind__missing_hostname_display_names_the_uri() {
    let e = ConnectionError::MissingHostname {
        uri: Uri::default(),
    };
    let text = e.to_string();
    assert!(
        text.starts_with("Missing hostname in "),
        "unexpected rendering: {:?}",
        text
    );
}

#[test]
fn hyperkind__relayed_failures_render_as_fixed_summaries() {
    // These two variants carry their detail in their source, not their
    // message, so the message itself is constant.
    assert_eq!(
        ConnectionError::from(arti_error("example.com:80")).to_string(),
        "Tor connection failed"
    );
    assert_eq!(
        ConnectionError::TLS(Arc::new(anyhow::anyhow!("handshake refused"))).to_string(),
        "TLS connection failed"
    );
}

#[test]
fn hyperkind__is_a_cloneable_std_error_that_keeps_its_cause() {
    fn assert_traits<T: Clone + std::fmt::Debug + std::error::Error>(_: &T) {}
    let e = ConnectionError::from(arti_error("example.com:80"));
    assert_traits(&e);
    let cloned = e.clone();
    assert_eq!(cloned.kind(), e.kind());
    assert_eq!(cloned.to_string(), e.to_string());
    // The underlying client error is reachable as the source.
    let src = std::error::Error::source(&e).expect("Arti variant must expose its source");
    assert!(
        src.to_string().contains("unbootstrapped"),
        "unexpected source: {:?}",
        src.to_string()
    );
    assert!(format!("{:?}", e).contains("Arti"));
}
