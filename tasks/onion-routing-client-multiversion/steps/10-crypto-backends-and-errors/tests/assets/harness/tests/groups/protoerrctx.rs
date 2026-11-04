//! `tor_proto::Error` after the context rebuild: struct variants that name the
//! object under manipulation, a standalone `ChannelClosed`, `ResolveError`,
//! and the `HasKind` mapping.

#![allow(non_snake_case)]
use tor_error::{ErrorKind, HasKind};
use tor_proto::{Error, ResolveError};

#[test]
fn protoerrctx__bytes_err_names_the_object_being_parsed() {
    let e = Error::BytesErr {
        object: "channel handshake",
        err: tor_bytes::Error::Truncated,
    };
    assert_eq!(crate::common::shown(&e), "Unable to parse channel handshake");
    assert_eq!(e.kind(), ErrorKind::TorProtocolViolation);
}

#[test]
fn protoerrctx__cell_encode_err_names_the_object_and_delegates_its_kind() {
    let e = Error::CellEncodeErr {
        object: "RELAY cell",
        err: tor_cell::Error::CantEncode("too long"),
    };
    assert_eq!(
        crate::common::shown(&e),
        "Unable to generate or encode RELAY cell"
    );
    assert_eq!(
        e.kind(),
        ErrorKind::Internal,
        "CellEncodeErr forwards the tor_cell kind"
    );
}

#[test]
fn protoerrctx__cell_decode_err_names_the_object_and_delegates_its_kind() {
    let e = Error::CellDecodeErr {
        object: "channel cell",
        err: tor_cell::Error::BytesErr {
            err: tor_bytes::Error::BadMessage("nope"),
            parsed: "channel cell",
        },
    };
    assert_eq!(crate::common::shown(&e), "Error while parsing channel cell");
    assert_eq!(e.kind(), ErrorKind::TorProtocolViolation);
}

#[test]
fn protoerrctx__encode_err_is_bad_api_usage() {
    let e = Error::EncodeErr {
        object: "CREATE2 handshake",
        err: tor_bytes::EncodeError::BadLengthValue,
    };
    assert_eq!(
        crate::common::shown(&e),
        "Problem while encoding CREATE2 handshake"
    );
    assert_eq!(e.kind(), ErrorKind::BadApiUsage);
}

#[test]
fn protoerrctx__handshake_cert_err_wraps_a_cert_error() {
    let e = Error::HandshakeCertErr(tor_cert::CertError::BadSignature);
    assert_eq!(
        crate::common::shown(&e),
        "Problem with certificate on handshake"
    );
    assert_eq!(e.kind(), ErrorKind::TorProtocolViolation);
    use std::error::Error as _;
    let src = e.source().expect("the CertError is the #[source]");
    assert_eq!(src.to_string(), "Signature on certificate was invalid");
}

#[test]
fn protoerrctx__closure_and_refusal_map_to_circuit_kinds() {
    assert_eq!(Error::CircuitClosed.kind(), ErrorKind::CircuitCollapse);
    assert_eq!(crate::common::shown(&Error::CircuitClosed), "Circuit closed");
    let refused = Error::CircRefused("no such relay");
    assert_eq!(refused.kind(), ErrorKind::CircuitRefused);
    assert_eq!(
        crate::common::shown(&refused),
        "Circuit extension refused: no such relay"
    );
    assert_eq!(
        Error::ChanMismatch("wrong identity".into()).kind(),
        ErrorKind::RelayIdMismatch
    );
}

#[test]
fn protoerrctx__resolve_error_distinguishes_transient_from_permanent() {
    let permanent = Error::ResolveError(ResolveError::Nontransient);
    let transient = Error::ResolveError(ResolveError::Transient);
    let garbled = Error::ResolveError(ResolveError::Unrecognized);
    assert_eq!(permanent.kind(), ErrorKind::RemoteHostNotFound);
    assert_eq!(transient.kind(), ErrorKind::RemoteHostResolutionFailed);
    assert_eq!(garbled.kind(), ErrorKind::RemoteHostResolutionFailed);
    assert_eq!(crate::common::shown(&permanent), "Remote resolve failed");
    assert_eq!(
        crate::common::shown(&ResolveError::Transient),
        "Received retriable transient error"
    );
}

#[test]
fn protoerrctx__source_chain_reaches_the_inner_bytes_error() {
    use std::error::Error as _;
    let e = Error::BytesErr {
        object: "NETINFO cell",
        err: tor_bytes::Error::ExtraneousBytes,
    };
    let src = e.source().expect("BytesErr exposes its #[source]");
    assert_eq!(src.to_string(), "Extra bytes at end of object");
}

#[test]
fn protoerrctx__converts_into_an_io_error_preserving_the_message() {
    let e = Error::CellDecodeErr {
        object: "RELAY cell",
        err: tor_cell::Error::BadStreamAddress,
    };
    let io: std::io::Error = e.into();
    assert_eq!(io.kind(), std::io::ErrorKind::InvalidData);
    assert!(
        io.to_string().contains("RELAY cell"),
        "the io error keeps the context; got {:?}",
        io.to_string()
    );
}

#[test]
fn protoerrctx__handshake_certs_expired_is_clock_skew() {
    let e = Error::HandshakeCertsExpired {
        expired_by: std::time::Duration::from_secs(3600),
    };
    assert_eq!(e.kind(), ErrorKind::ClockSkew);
    assert_eq!(
        crate::common::shown(&e),
        "Handshake failed due to expired certificates (possible clock skew)"
    );
}
