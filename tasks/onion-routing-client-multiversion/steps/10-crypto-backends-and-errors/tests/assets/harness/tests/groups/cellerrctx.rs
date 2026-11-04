//! `tor_cell::Error` carries context: what was being parsed, and which
//! `tor_bytes` error explains it. Its `HasKind` mapping is graded too.

#![allow(non_snake_case)]
use bytes::BytesMut;
use tor_cell::chancell::{ChanCmd, CELL_DATA_LEN};
use tor_cell::Error;
use tor_error::{ErrorKind, HasKind};

/// Build a complete fixed-length channel cell with the given command and the
/// given first payload bytes; the rest is zero padding.
fn fixed_cell(circid: u32, cmd: ChanCmd, head: &[u8]) -> BytesMut {
    let mut raw = circid.to_be_bytes().to_vec();
    raw.push(u8::from(cmd));
    let mut payload = head.to_vec();
    payload.resize(CELL_DATA_LEN, 0);
    raw.extend_from_slice(&payload);
    BytesMut::from(&raw[..])
}

/// Build a complete variable-length channel cell.
fn var_cell(circid: u32, cmd: ChanCmd, body: &[u8]) -> BytesMut {
    let mut raw = circid.to_be_bytes().to_vec();
    raw.push(u8::from(cmd));
    raw.extend_from_slice(&(body.len() as u16).to_be_bytes());
    raw.extend_from_slice(body);
    BytesMut::from(&raw[..])
}

fn decode_err(mut src: BytesMut) -> Error {
    let mut codec = tor_cell::chancell::codec::ChannelCodec::new(4);
    match codec.decode_cell(&mut src) {
        Err(e) => e,
        Ok(other) => panic!("expected a decoding failure, got {:?}", other.is_some()),
    }
}

#[test]
fn cellerrctx__bytes_err_names_what_was_being_parsed() {
    // A VERSIONS body of odd length: the last u16 is incomplete.
    let err = decode_err(var_cell(0, ChanCmd::VERSIONS, &[0x00, 0x04, 0x00]));
    assert_eq!(
        crate::common::shown(&err),
        "Error while parsing channel cell",
        "the message must say what failed to parse"
    );
}

#[test]
fn cellerrctx__bytes_err_keeps_the_underlying_bytes_error() {
    let err = decode_err(var_cell(0, ChanCmd::VERSIONS, &[0x00, 0x04, 0x00]));
    match err {
        Error::BytesErr { err, parsed } => {
            assert_eq!(parsed, "channel cell");
            assert_eq!(err, tor_bytes::Error::Truncated, "got {:?}", err);
        }
        other => panic!("expected BytesErr, got {:?}", other),
    }
}

#[test]
fn cellerrctx__truncated_input_is_an_internal_error() {
    let err = decode_err(var_cell(0, ChanCmd::VERSIONS, &[0x00, 0x04, 0x00]));
    assert_eq!(
        err.kind(),
        ErrorKind::Internal,
        "a short read the codec already length-checked is our own bug"
    );
}

#[test]
fn cellerrctx__malformed_content_is_a_protocol_violation() {
    // PADDING_NEGOTIATE version byte must be 0.
    let err = decode_err(fixed_cell(0, ChanCmd::PADDING_NEGOTIATE, &[0x01, 0x00]));
    match &err {
        Error::BytesErr { err, parsed } => {
            assert_eq!(*parsed, "channel cell");
            assert_eq!(
                *err,
                tor_bytes::Error::BadMessage("Unrecognized padding negotiation version")
            );
        }
        other => panic!("expected BytesErr, got {:?}", other),
    }
    assert_eq!(err.kind(), ErrorKind::TorProtocolViolation);
}

#[test]
fn cellerrctx__chan_proto_reports_the_offending_cell() {
    // PADDING is only legal on circuit 0.
    let err = decode_err(fixed_cell(1, ChanCmd::PADDING, &[]));
    match &err {
        Error::ChanProto(msg) => assert!(
            msg.contains("PADDING"),
            "the message should name the command; got {:?}",
            msg
        ),
        other => panic!("expected ChanProto, got {:?}", other),
    }
    assert!(crate::common::shown(&err).starts_with("Channel protocol violation: "));
    assert_eq!(err.kind(), ErrorKind::TorProtocolViolation);
}

#[test]
fn cellerrctx__encode_error_converts_and_is_bad_api_usage() {
    let err: Error = tor_bytes::EncodeError::BadLengthValue.into();
    assert!(matches!(err, Error::EncodeErr(_)), "got {:?}", err);
    assert_eq!(crate::common::shown(&err), "Error while encoding message");
    assert_eq!(err.kind(), ErrorKind::BadApiUsage);
}

#[test]
fn cellerrctx__cant_encode_is_internal() {
    let err = Error::CantEncode("Too many versions");
    assert_eq!(err.kind(), ErrorKind::Internal);
}

#[test]
fn cellerrctx__bad_stream_address_is_bad_api_usage() {
    assert_eq!(Error::BadStreamAddress.kind(), ErrorKind::BadApiUsage);
}

#[test]
fn cellerrctx__source_chain_reaches_the_bytes_error() {
    use std::error::Error as _;
    let err = decode_err(var_cell(0, ChanCmd::VERSIONS, &[0x00, 0x04, 0x00]));
    let src = err.source().expect("BytesErr must expose its #[source]");
    assert_eq!(src.to_string(), "Object truncated (or not fully present)");
}
