//! Group `circkind`: stable kinds for the circuit manager.
//!
//! instruction.md: "a structured error taxonomy". The circuit manager's errors
//! are what a caller sees when a connection cannot be built. The taxonomy has
//! to say whether retrying could help (a transient failure or a timeout),
//! whether the network simply has no path, or whether the process is
//! shutting down.

use std::sync::Arc;

use tor_circmgr::Error;
use tor_error::{internal, ErrorKind, HasKind};

#[test]
fn circkind__no_usable_path_is_its_own_kind() {
    assert_eq!(
        Error::NoPath("no relays with the needed flags".into()).kind(),
        ErrorKind::NoPath
    );
    assert_eq!(
        Error::NoExit("no exit permits port 443".into()).kind(),
        ErrorKind::NoExit
    );
    // These are deliberately distinct: "no route at all" and "no way out" call
    // for different advice.
    assert_ne!(ErrorKind::NoPath, ErrorKind::NoExit);
}

#[test]
fn circkind__discarded_circuits_are_transient() {
    assert_eq!(Error::GuardNotUsable.kind(), ErrorKind::TransientFailure);
    assert_eq!(Error::CircCanceled.kind(), ErrorKind::TransientFailure);
}

#[test]
fn circkind__timeouts_are_network_timeouts() {
    assert_eq!(Error::CircTimeout.kind(), ErrorKind::TorNetworkTimeout);
    assert_eq!(Error::RequestTimeout.kind(), ErrorKind::TorNetworkTimeout);
}

#[test]
fn circkind__shutdown_and_stale_directories_are_distinguished() {
    assert_eq!(
        Error::PendingCanceled.kind(),
        ErrorKind::ReactorShuttingDown
    );
    assert_eq!(Error::ExpiredConsensus.kind(), ErrorKind::DirectoryExpired);
    assert_eq!(
        Error::Spawn {
            spawning: "circuit reactor",
            cause: Arc::new(futures::task::SpawnError::shutdown()),
        }
        .kind(),
        ErrorKind::ReactorShuttingDown
    );
}

#[test]
fn circkind__wrapped_errors_delegate_to_the_layer_that_failed() {
    assert_eq!(
        Error::Protocol(tor_proto::Error::CircuitClosed).kind(),
        ErrorKind::CircuitCollapse
    );
    assert_eq!(
        Error::Protocol(tor_proto::Error::CircRefused("extend refused")).kind(),
        ErrorKind::CircuitRefused
    );
    assert_eq!(
        Error::State(tor_persist::Error::NoLock).kind(),
        ErrorKind::BadApiUsage
    );
    assert_eq!(
        Error::Bug(internal!("impossible state")).kind(),
        ErrorKind::Internal
    );
}

#[test]
fn circkind__display_states_what_went_wrong() {
    assert_eq!(
        Error::CircTimeout.to_string(),
        "Circuit took too long to build"
    );
    assert_eq!(
        Error::RequestTimeout.to_string(),
        "Spent too long waiting for a circuit to build"
    );
    assert_eq!(Error::ExpiredConsensus.to_string(), "Consensus is expired");
    assert_eq!(Error::CircCanceled.to_string(), "Circuit canceled");
    assert_eq!(
        Error::NoExit("port 25 is blocked everywhere".into()).to_string(),
        "Can't find exit for circuit: port 25 is blocked everywhere"
    );
    assert_eq!(
        Error::Protocol(tor_proto::Error::CircuitClosed).to_string(),
        "Problem building a circuit: circuit closed"
    );
}
