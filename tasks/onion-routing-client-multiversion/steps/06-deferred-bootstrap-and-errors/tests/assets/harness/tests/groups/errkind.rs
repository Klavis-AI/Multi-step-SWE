//! Group `errkind`: the stable `ErrorKind` taxonomy published by `tor-error`.
//!
//! instruction.md: "a structured error taxonomy" / "errors gain stable kinds".
//! The kind is the stable, machine-readable classification that every arti
//! error is required to report, so its variants and their human-readable
//! renderings are part of the contract.

use tor_error::{ErrorKind, HasKind};

/// Assert that every `(kind, rendering)` pair renders as stated.
fn check(pairs: &[(ErrorKind, &str)]) {
    for (kind, text) in pairs {
        assert_eq!(
            &kind.to_string(),
            text,
            "ErrorKind::{:?} must render as {:?}",
            kind,
            text
        );
    }
}

#[test]
fn errkind__bootstrap_required_display() {
    // The kind that a deferred-bootstrap client reports when it is used early.
    assert_eq!(
        ErrorKind::BootstrapRequired.to_string(),
        "attempted to use unbootstrapped client"
    );
}

#[test]
fn errkind__connectivity_kinds_display() {
    check(&[
        (ErrorKind::TorAccessFailed, "error connecting to Tor"),
        (
            ErrorKind::LocalNetworkError,
            "problem with network or connection",
        ),
        (ErrorKind::TorNetworkTimeout, "tor operation timed out"),
        (
            ErrorKind::RemoteNetworkTimeout,
            "operation timed out at exit",
        ),
        (ErrorKind::TorDirectoryError, "directory fetch attempt failed"),
        (ErrorKind::DirectoryExpired, "network directory is expired."),
    ]);
}

#[test]
fn errkind__storage_kinds_display() {
    check(&[
        (
            ErrorKind::PersistentStateAccessFailed,
            "could not read/write persistent state",
        ),
        (
            ErrorKind::PersistentStateCorrupted,
            "corrupted data in persistent state",
        ),
        (ErrorKind::CacheCorrupted, "corrupted data in cache"),
        (ErrorKind::CacheAccessFailed, "cache access problem"),
    ]);
}

#[test]
fn errkind__protocol_kinds_display() {
    check(&[
        (
            ErrorKind::LocalProtocolViolation,
            "local protocol violation (local bug or incompatibility)",
        ),
        (
            ErrorKind::TorProtocolViolation,
            "Tor network protocol violation (bug, incompatibility, or attack)",
        ),
        (ErrorKind::RelayIdMismatch, "identity mismatch"),
        (ErrorKind::RemoteProtocolFailed, "remote protocol failed"),
    ]);
}

#[test]
fn errkind__stream_kinds_display() {
    check(&[
        (ErrorKind::RemoteStreamClosed, "remote stream closed"),
        (ErrorKind::RemoteStreamReset, "remote stream reset"),
        (ErrorKind::RemoteStreamError, "remote stream error"),
        (
            ErrorKind::RemoteConnectionRefused,
            "remote host refused connection",
        ),
        (ErrorKind::ExitPolicyRejected, "rejected by exit policy"),
        (ErrorKind::ExitTimeout, "timeout at exit relay"),
        (ErrorKind::RemoteNetworkFailed, "network failure at exit"),
        (ErrorKind::RemoteHostNotFound, "remote hostname lookup failure"),
        (ErrorKind::RelayTooBusy, "relay too busy"),
        (ErrorKind::InvalidStreamTarget, "target address was invalid"),
        (
            ErrorKind::ForbiddenStreamTarget,
            "target address disabled locally",
        ),
    ]);
}

#[test]
fn errkind__bug_kinds_display() {
    // The two kinds that mean "this is a bug", and which `Bug` can carry.
    check(&[
        (ErrorKind::Internal, "internal error (bug)"),
        (ErrorKind::BadApiUsage, "bad API usage (bug)"),
    ]);
}

#[test]
fn errkind__lifecycle_and_config_kinds_display() {
    check(&[
        (ErrorKind::ReactorShuttingDown, "reactor is shutting down"),
        (ErrorKind::ArtiShuttingDown, "Tor client is shutting down."),
        (ErrorKind::TransientFailure, "un-retried transient failure"),
        (ErrorKind::CircuitCollapse, "circuit collapsed"),
        (ErrorKind::CircuitRefused, "remote host refused our request"),
        (ErrorKind::NoPath, "could not construct a path"),
        (ErrorKind::NoExit, "no exit available for path"),
        (ErrorKind::InvalidConfig, "invalid configuration"),
        (
            ErrorKind::InvalidConfigTransition,
            "invalid configuration transition",
        ),
        (ErrorKind::NoHomeDirectory, "could not find a home directory"),
        (ErrorKind::NotImplemented, "operation not implemented"),
        (
            ErrorKind::FeatureDisabled,
            "operation not supported because Arti feature disabled",
        ),
    ]);
}

#[test]
fn errkind__kinds_are_copy_and_distinct() {
    // `ErrorKind` is a plain value that callers match on: it must be Copy and
    // compare by variant.
    let a = ErrorKind::BootstrapRequired;
    let b = a;
    assert_eq!(a, b);
    assert_eq!(a, ErrorKind::BootstrapRequired);
    assert_ne!(ErrorKind::BootstrapRequired, ErrorKind::Internal);
    assert_ne!(ErrorKind::Internal, ErrorKind::BadApiUsage);
    assert_ne!(ErrorKind::RemoteStreamClosed, ErrorKind::RemoteStreamReset);
    // Debug names the variant, not the display text.
    assert_eq!(format!("{:?}", ErrorKind::NoExit), "NoExit");
}

#[test]
fn errkind__spawn_error_has_kind() {
    // `futures::task::SpawnError` is classified by tor-error itself, because
    // every manager in the tree converts spawn failures into its own error.
    let shutdown = futures::task::SpawnError::shutdown();
    assert!(shutdown.is_shutdown());
    assert_eq!(shutdown.kind(), ErrorKind::ReactorShuttingDown);
}
