//! Group `dirkind`: stable kinds for directory fetching and caching.
//!
//! instruction.md: "a structured error taxonomy". The directory layer has two
//! distinct failure surfaces: talking to a directory cache over Tor
//! (`tor_dirclient`) and managing the local copy on disk (`tor_dirmgr`). The
//! taxonomy has to tell "the relay sent us rubbish" apart from "our own cache
//! is corrupt", because only the second one calls for discarding local state.

use std::sync::Arc;

use tor_dirclient::Error as ClientError;
use tor_dirmgr::Error as MgrError;
use tor_error::{ErrorKind, HasKind};

/// An `io::Error` to stand in for a real failure.
fn ioerr() -> Arc<std::io::Error> {
    Arc::new(std::io::Error::new(
        std::io::ErrorKind::UnexpectedEof,
        "synthetic",
    ))
}

/// A `FromUtf8Error` produced from bytes that are not UTF-8.
fn bad_utf8() -> std::string::FromUtf8Error {
    String::from_utf8(vec![0x80, 0xff]).expect_err("0x80 0xff is not UTF-8")
}

#[test]
fn dirkind__a_slow_directory_cache_is_a_network_timeout() {
    assert_eq!(ClientError::DirTimeout.kind(), ErrorKind::TorNetworkTimeout);
    assert_eq!(ClientError::DirTimeout.to_string(), "directory timed out");
}

#[test]
fn dirkind__a_malformed_reply_is_a_protocol_violation() {
    assert_eq!(
        ClientError::TruncatedHeaders.kind(),
        ErrorKind::TorProtocolViolation
    );
    assert_eq!(
        ClientError::ResponseTooLong(1_048_576).kind(),
        ErrorKind::TorProtocolViolation
    );
    assert_eq!(
        ClientError::Utf8Encoding(bad_utf8()).kind(),
        ErrorKind::TorProtocolViolation
    );
    assert_eq!(
        ClientError::ContentEncoding("br".into()).kind(),
        ErrorKind::TorProtocolViolation
    );
    assert_eq!(
        ClientError::ResponseTooLong(42).to_string(),
        "response too long; gave up after 42 bytes"
    );
}

#[test]
fn dirkind__stream_trouble_is_a_directory_error_and_delegates_when_nested() {
    assert_eq!(
        ClientError::IoError(ioerr()).kind(),
        ErrorKind::TorDirectoryError
    );
    // Errors that came from a lower layer keep that layer's verdict.
    assert_eq!(
        ClientError::Proto(tor_proto::Error::CircuitClosed).kind(),
        ErrorKind::CircuitCollapse
    );
    assert_eq!(
        ClientError::CircMgr(tor_circmgr::Error::CircTimeout).kind(),
        ErrorKind::TorNetworkTimeout
    );
}

#[test]
fn dirkind__any_directory_failure_retires_the_circuit() {
    // A directory circuit that produced an error is not reused, whatever the
    // error was.
    for e in [
        ClientError::DirTimeout,
        ClientError::TruncatedHeaders,
        ClientError::IoError(ioerr()),
        ClientError::ContentEncoding("x".into()),
    ] {
        assert!(e.should_retire_circ(), "{:?} should retire the circuit", e);
    }
}

#[test]
fn dirkind__a_damaged_local_cache_is_reported_as_corruption() {
    assert_eq!(
        MgrError::CacheCorruption("missing consensus row").kind(),
        ErrorKind::CacheCorrupted
    );
    assert_eq!(MgrError::UnrecognizedSchema.kind(), ErrorKind::CacheCorrupted);
    let utf8_err = std::str::from_utf8(&[0x80, 0xff]).expect_err("not UTF-8");
    assert_eq!(
        MgrError::BadUtf8InCache(utf8_err).kind(),
        ErrorKind::CacheCorrupted
    );
    // Not being able to *reach* the cache is a different problem from the
    // cache being wrong.
    assert_eq!(
        MgrError::StorageError("disk full".into()).kind(),
        ErrorKind::CacheAccessFailed
    );
    assert_eq!(MgrError::IOError(ioerr()).kind(), ErrorKind::CacheAccessFailed);
}

#[test]
fn dirkind__manager_lifecycle_and_configuration_faults() {
    assert_eq!(MgrError::ManagerDropped.kind(), ErrorKind::ArtiShuttingDown);
    assert_eq!(
        MgrError::BadNetworkConfig("no authorities").kind(),
        ErrorKind::InvalidConfig
    );
    assert_eq!(
        MgrError::DirectoryNotPresent.kind(),
        ErrorKind::DirectoryExpired
    );
    assert_eq!(MgrError::NoDownloadSupport.kind(), ErrorKind::NotImplemented);
    assert_eq!(MgrError::OfflineMode.kind(), ErrorKind::BadApiUsage);
    assert_eq!(MgrError::CantAdvanceState.kind(), ErrorKind::TorAccessFailed);
    assert_eq!(
        MgrError::Unwanted("a document we never asked for").kind(),
        ErrorKind::TorProtocolViolation
    );
    assert_eq!(
        MgrError::UnrecognizedAuthorities.kind(),
        ErrorKind::TorProtocolViolation
    );
}

#[test]
fn dirkind__the_manager_relays_the_clients_verdict() {
    assert_eq!(
        MgrError::DirClientError(ClientError::DirTimeout).kind(),
        ErrorKind::TorNetworkTimeout
    );
    assert_eq!(
        MgrError::DirClientError(ClientError::IoError(ioerr())).kind(),
        ErrorKind::TorDirectoryError
    );
    assert_eq!(
        MgrError::DirClientError(ClientError::DirTimeout).to_string(),
        "dirclient error: directory timed out"
    );
    assert_eq!(
        MgrError::DirectoryNotPresent.to_string(),
        "directory not present or not up-to-date"
    );
    assert_eq!(
        MgrError::CacheCorruption("bad row").to_string(),
        "corrupt cache: bad row"
    );
}
