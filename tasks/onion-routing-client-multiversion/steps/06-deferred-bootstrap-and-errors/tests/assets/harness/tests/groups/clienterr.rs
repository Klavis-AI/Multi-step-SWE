//! Group `clienterr`: `arti_client::Error`, the single public error type of the
//! supported client API.
//!
//! instruction.md: "The client API is declared supported, and errors gain
//! stable kinds." The concrete variant is deliberately hidden; what callers get
//! is `kind()` plus a rendering that leads with `tor: <kind>`.

use arti_client::{Error, ErrorKind, HasKind};
use tor_rtcompat::BlockOn;

use crate::common::{manual_client, runtime, ClientDirs};

/// Attempt a connection that never reaches the network, and return the error.
fn connect_err(target: &str) -> Error {
    let dirs = ClientDirs::new();
    let rt = runtime();
    let client = manual_client(rt.clone(), &dirs);
    rt.block_on(client.connect(target.to_string()))
        .map(|_| ())
        .expect_err("this target must be rejected")
}

#[test]
fn clienterr__display_is_prefixed_with_tor_and_the_kind() {
    let e = connect_err("127.0.0.1:80");
    assert_eq!(
        e.to_string(),
        "tor: target address disabled locally: \
         Cannot connect to a local-only address without enabling allow_local_addrs"
    );
}

#[test]
fn clienterr__local_address_is_a_forbidden_target() {
    assert_eq!(
        connect_err("127.0.0.1:80").kind(),
        ErrorKind::ForbiddenStreamTarget
    );
    assert_eq!(
        connect_err("[::1]:80").kind(),
        ErrorKind::ForbiddenStreamTarget
    );
}

#[test]
fn clienterr__localhost_is_a_forbidden_target() {
    let e = connect_err("localhost:80");
    assert_eq!(e.kind(), ErrorKind::ForbiddenStreamTarget);
}

#[test]
fn clienterr__address_without_a_port_is_an_invalid_target() {
    let e = connect_err("example.com");
    assert_eq!(e.kind(), ErrorKind::InvalidStreamTarget);
    assert!(
        e.to_string().starts_with("tor: target address was invalid: "),
        "unexpected rendering: {:?}",
        e.to_string()
    );
    assert!(e.to_string().contains("No port found in string"));
}

#[test]
fn clienterr__unparsable_port_is_an_invalid_target() {
    let e = connect_err("example.com:definitely-not-a-port");
    assert_eq!(e.kind(), ErrorKind::InvalidStreamTarget);
    assert!(e.to_string().contains("Could not parse port"));
    // Port zero is not a usable target either.
    assert_eq!(
        connect_err("example.com:0").kind(),
        ErrorKind::InvalidStreamTarget
    );
}

#[test]
fn clienterr__error_kind_is_the_tor_error_kind() {
    // arti-client must re-export tor-error's taxonomy, not define its own.
    let k: tor_error::ErrorKind = ErrorKind::BootstrapRequired;
    assert_eq!(k, tor_error::ErrorKind::BootstrapRequired);
    // ... and the same HasKind trait, so a generic classifier works on both.
    fn classify<E: tor_error::HasKind>(e: &E) -> tor_error::ErrorKind {
        e.kind()
    }
    let e = connect_err("127.0.0.1:80");
    assert_eq!(classify(&e), ErrorKind::ForbiddenStreamTarget);
    let via_trait: &dyn HasKind = &e;
    assert_eq!(via_trait.kind(), ErrorKind::ForbiddenStreamTarget);
}

#[test]
fn clienterr__error_is_send_sync_clone_and_static() {
    fn assert_traits<T: Send + Sync + Clone + std::error::Error + 'static>(_: &T) {}
    let e = connect_err("127.0.0.1:80");
    assert_traits(&e);
    let cloned = e.clone();
    assert_eq!(cloned.to_string(), e.to_string());
    assert_eq!(cloned.kind(), e.kind());
    assert!(format!("{:?}", e).contains("Error"));
}
