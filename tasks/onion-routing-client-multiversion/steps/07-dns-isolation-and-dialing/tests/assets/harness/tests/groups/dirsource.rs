//! Group `dirsource`: directory response source tracking. At step 07,
//! `SourceInfo` carries both the circuit ID and the cache peer identity.
//!
//! Graded items: `tor_dirclient::{DirResponse, SourceInfo, Error}`.

use tor_circmgr::Error as CircError;
use tor_dirclient::{DirResponse, Error as DirError};

use crate::common::chan_target;

/// A `tor_circmgr::Error` that blames exactly one relay.
///
/// `Error::Channel` is the only `peers()`-bearing variant an out-of-crate
/// caller can build at this version: its fields are public, and a
/// `tor_chanmgr::Error::ChannelBuild` with an empty address list is a valid
/// cause that needs no `io::Error`.
fn channel_err(addr: &str, seed: u8) -> CircError {
    CircError::Channel {
        peer: chan_target(&[addr], seed),
        cause: tor_chanmgr::Error::ChannelBuild {
            addresses: Vec::new(),
        },
    }
}

#[test]
fn dirsource__response_source_accessor_returns_none_for_from_body() {
    let resp = DirResponse::from_body(b"example");
    assert!(resp.source().is_none());
}

#[test]
fn dirsource__request_failed_without_source_blames_nobody() {
    // `SourceInfo` has no public constructor at this version, so the
    // `source: Some(..)` arm is not reachable from outside the crate. The
    // reachable half is still meaningful: an error with no known source must
    // blame no cache rather than guessing one.
    let error = DirError::RequestFailed {
        source: None,
        error: tor_dirclient::RequestError::DirTimeout,
    };
    assert!(error.cache_ids().is_empty());
    // The rendering names the (absent) source rather than dropping the field.
    assert!(error.to_string().contains("None"));
}

#[test]
fn dirsource__cache_ids_delegates_to_circmgr_peers() {
    // `cache_ids()` on the `CircMgr` arm is exactly the inner error's
    // `peers()`: a circuit failure that blames a relay names that relay here
    // too, and one that blames nobody stays empty.
    let blaming = DirError::CircMgr(channel_err("192.0.2.1:9001", 7));
    let ids = blaming.cache_ids();
    assert_eq!(ids.len(), 1);
    assert!(ids[0].to_string().contains("192.0.2.1:9001"));

    let blameless = DirError::CircMgr(CircError::NoPath("test".into()));
    assert!(blameless.cache_ids().is_empty());
}

#[test]
fn dirsource__error_should_retire_circ_for_request_errors() {
    let err = DirError::RequestFailed {
        source: None,
        error: tor_dirclient::RequestError::DirTimeout,
    };
    assert!(err.should_retire_circ());
}

#[test]
fn dirsource__error_should_retire_circ_for_circmgr_errors() {
    let err = DirError::CircMgr(CircError::NoPath("test".into()));
    assert!(err.should_retire_circ());
}

#[test]
fn dirsource__circmgr_error_peers_distinguishes_blame() {
    // A failure that never reached a relay blames nobody; a channel failure
    // blames exactly the relay it was talking to.
    let unreached = CircError::NoPath("example".into());
    assert!(unreached.peers().is_empty());

    let reached = channel_err("192.0.2.9:443", 21);
    let peers = reached.peers();
    assert_eq!(peers.len(), 1);
    assert!(peers[0].to_string().contains("192.0.2.9:443"));
}

#[test]
fn dirsource__circmgr_request_failed_flattens_peer_list() {
    // RetryError wraps multiple attempts; peers() concatenates the attempts'
    // own blame lists, in order, rather than returning only the last.
    use retry_error::RetryError;
    let mut retry: RetryError<Box<CircError>> = RetryError::in_attempt_to("build circuit");
    retry.push(Box::new(channel_err("192.0.2.1:443", 1)));
    retry.push(Box::new(CircError::NoPath("no path this time".into())));
    retry.push(Box::new(channel_err("192.0.2.2:443", 2)));
    let err = CircError::RequestFailed(retry);
    let peers = err.peers();
    // Two blaming attempts contribute one peer each; the blameless one
    // contributes none, and does not stop the others being reported.
    assert_eq!(peers.len(), 2);
    assert!(peers[0].to_string().contains("192.0.2.1:443"));
    assert!(peers[1].to_string().contains("192.0.2.2:443"));

    // Nested errors that all blame nobody still flatten to an empty list.
    let mut blameless: RetryError<Box<CircError>> = RetryError::in_attempt_to("build circuit");
    blameless.push(Box::new(CircError::NoPath("first".into())));
    blameless.push(Box::new(CircError::NoExit("second".into())));
    assert!(CircError::RequestFailed(blameless).peers().is_empty());
}
