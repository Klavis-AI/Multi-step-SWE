//! Group `bootdefer`: an unbootstrapped `TorClient` refuses network work and
//! says exactly why.
//!
//! instruction.md: "an unbootstrapped `TorClient` with deferred and lazy
//! bootstrap". A client built with `create_unbootstrapped()` must come into
//! existence without touching the network, and every operation that needs a
//! directory must fail with `ErrorKind::BootstrapRequired` rather than hang.

use arti_client::{ErrorKind, HasKind};
use tor_rtcompat::BlockOn;

use crate::common::{manual_client, runtime, ClientDirs};

/// The one rendering an unbootstrapped client produces for a circuit request.
const BOOTSTRAP_REQUIRED: &str =
    "tor: attempted to use unbootstrapped client: cannot launch a circuit with unbootstrapped client";

#[test]
fn bootdefer__create_unbootstrapped_succeeds_offline() {
    let dirs = ClientDirs::new();
    let rt = runtime();
    let client = manual_client(rt, &dirs);
    // Creating the client must not have bootstrapped it.
    assert!(!client.bootstrap_status().ready_for_traffic());
    assert_eq!(client.bootstrap_status().as_frac(), 0.0);
}

#[test]
fn bootdefer__connect_before_bootstrap_reports_bootstrap_required() {
    let dirs = ClientDirs::new();
    let rt = runtime();
    let client = manual_client(rt.clone(), &dirs);
    let err = rt
        .block_on(client.connect("example.com:80"))
        .expect_err("an unbootstrapped client must refuse to connect");
    assert_eq!(err.kind(), ErrorKind::BootstrapRequired);
}

#[test]
fn bootdefer__connect_error_names_the_refused_action() {
    let dirs = ClientDirs::new();
    let rt = runtime();
    let client = manual_client(rt.clone(), &dirs);
    let err = rt
        .block_on(client.connect("example.com:80"))
        .expect_err("an unbootstrapped client must refuse to connect");
    assert_eq!(err.to_string(), BOOTSTRAP_REQUIRED);
}

#[test]
fn bootdefer__resolve_ptr_before_bootstrap_reports_bootstrap_required() {
    use std::net::{IpAddr, Ipv4Addr};
    let dirs = ClientDirs::new();
    let rt = runtime();
    let client = manual_client(rt.clone(), &dirs);
    // 192.0.2.1 is TEST-NET-1; nothing is ever dialled, because the client
    // gives up before it asks for a circuit.
    let err = rt
        .block_on(client.resolve_ptr(IpAddr::V4(Ipv4Addr::new(192, 0, 2, 1))))
        .expect_err("an unbootstrapped client must refuse to do a PTR lookup");
    assert_eq!(err.kind(), ErrorKind::BootstrapRequired);
    assert_eq!(err.to_string(), BOOTSTRAP_REQUIRED);
}

#[test]
fn bootdefer__isolated_client_also_defers_bootstrap() {
    let dirs = ClientDirs::new();
    let rt = runtime();
    let client = manual_client(rt.clone(), &dirs);
    let isolated = client.isolated_client();
    let err = rt
        .block_on(isolated.connect("example.com:80"))
        .expect_err("an isolated handle inherits the deferred bootstrap");
    assert_eq!(err.kind(), ErrorKind::BootstrapRequired);
}

#[test]
fn bootdefer__refusal_is_repeatable() {
    // Failing once must not poison the client or start a bootstrap behind our
    // back: the second attempt reports the very same thing.
    let dirs = ClientDirs::new();
    let rt = runtime();
    let client = manual_client(rt.clone(), &dirs);
    for _ in 0..3 {
        let err = rt
            .block_on(client.connect("example.com:80"))
            .expect_err("an unbootstrapped client must refuse to connect");
        assert_eq!(err.kind(), ErrorKind::BootstrapRequired);
        assert_eq!(err.to_string(), BOOTSTRAP_REQUIRED);
    }
    assert!(!client.bootstrap_status().ready_for_traffic());
}
