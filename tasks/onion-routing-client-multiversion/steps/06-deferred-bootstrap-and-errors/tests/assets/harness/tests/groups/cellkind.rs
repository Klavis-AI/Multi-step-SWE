//! Group `cellkind`: stable kinds for the cell layer, and the END reason
//! taxonomy underneath them.
//!
//! instruction.md: "a structured error taxonomy". `tor_cell::Error` says
//! whether a bad cell was the relay's fault or ours; `EndReason` carries the
//! exit's own explanation for closing a stream and maps it onto the same
//! taxonomy. END reason code points are fixed by tor-spec (SPEC_NOTES.md,
//! tor-spec.txt S6.3).

use tor_cell::relaycell::msg::EndReason;
use tor_cell::Error;
use tor_error::{internal, ErrorKind, HasKind};

#[test]
fn cellkind__unparsable_cells_are_protocol_violations() {
    assert_eq!(
        Error::BytesErr(tor_bytes::Error::ExtraneousBytes).kind(),
        ErrorKind::TorProtocolViolation
    );
    assert_eq!(
        Error::BytesErr(tor_bytes::Error::BadMessage("impossible length")).kind(),
        ErrorKind::TorProtocolViolation
    );
    assert_eq!(
        Error::ChanProto("unexpected cell type".into()).kind(),
        ErrorKind::TorProtocolViolation
    );
}

#[test]
fn cellkind__running_out_of_bytes_inside_a_cell_is_our_bug() {
    // A cell is a fixed-size buffer we already hold, so "truncated" here means
    // the decoder was asked for more than the cell can hold: that is an
    // internal fault, not a bad peer.
    assert_eq!(
        Error::BytesErr(tor_bytes::Error::Truncated).kind(),
        ErrorKind::Internal
    );
}

#[test]
fn cellkind__encoding_faults_are_internal_and_misuse_is_bad_api_usage() {
    assert_eq!(Error::CantEncode.kind(), ErrorKind::Internal);
    assert_eq!(
        Error::Internal(internal!("unreachable")).kind(),
        ErrorKind::Internal
    );
    assert_eq!(Error::BadStreamAddress.kind(), ErrorKind::BadApiUsage);
}

#[test]
fn cellkind__display_states_what_went_wrong() {
    assert_eq!(
        Error::BytesErr(tor_bytes::Error::ExtraneousBytes).to_string(),
        "parsing error: extra bytes at end of object"
    );
    assert_eq!(
        Error::ChanProto("nope".into()).to_string(),
        "channel protocol violation: nope"
    );
    assert_eq!(
        Error::BadStreamAddress.to_string(),
        "invalid stream target address"
    );
    assert_eq!(
        Error::CantEncode.to_string(),
        "Message can't be represented in a Tor cell."
    );
}

#[test]
fn cellkind__end_reasons_use_the_wire_code_points() {
    // tor-spec S6.3 fixes these numbers; they travel in RELAY_END cells.
    let table: &[(EndReason, u8)] = &[
        (EndReason::MISC, 1),
        (EndReason::RESOLVEFAILED, 2),
        (EndReason::CONNECTREFUSED, 3),
        (EndReason::EXITPOLICY, 4),
        (EndReason::DESTROY, 5),
        (EndReason::DONE, 6),
        (EndReason::TIMEOUT, 7),
        (EndReason::NOROUTE, 8),
        (EndReason::HIBERNATING, 9),
        (EndReason::INTERNAL, 10),
        (EndReason::RESOURCELIMIT, 11),
        (EndReason::CONNRESET, 12),
        (EndReason::TORPROTOCOL, 13),
        (EndReason::NOTDIRECTORY, 14),
    ];
    for &(reason, code) in table {
        assert_eq!(u8::from(reason), code, "wrong code for {:?}", reason);
        assert_eq!(EndReason::from(code), reason, "code {} decoded wrongly", code);
    }
}

#[test]
fn cellkind__end_reasons_about_the_far_end_of_the_stream() {
    let table: &[(EndReason, ErrorKind)] = &[
        (EndReason::MISC, ErrorKind::RemoteStreamError),
        (EndReason::RESOLVEFAILED, ErrorKind::RemoteHostNotFound),
        (EndReason::CONNECTREFUSED, ErrorKind::RemoteConnectionRefused),
        (EndReason::EXITPOLICY, ErrorKind::ExitPolicyRejected),
        (EndReason::DONE, ErrorKind::RemoteStreamClosed),
        (EndReason::TIMEOUT, ErrorKind::ExitTimeout),
        (EndReason::NOROUTE, ErrorKind::RemoteNetworkFailed),
        (EndReason::CONNRESET, ErrorKind::RemoteStreamReset),
    ];
    for &(reason, want) in table {
        assert_eq!(reason.kind(), want, "wrong kind for {:?}", reason);
    }
}

#[test]
fn cellkind__end_reasons_about_the_relay_or_the_circuit() {
    let table: &[(EndReason, ErrorKind)] = &[
        (EndReason::DESTROY, ErrorKind::CircuitCollapse),
        (EndReason::RESOURCELIMIT, ErrorKind::RelayTooBusy),
        (EndReason::HIBERNATING, ErrorKind::RelayTooBusy),
        (EndReason::INTERNAL, ErrorKind::TorProtocolViolation),
        (EndReason::TORPROTOCOL, ErrorKind::TorProtocolViolation),
        (EndReason::NOTDIRECTORY, ErrorKind::TorProtocolViolation),
    ];
    for &(reason, want) in table {
        assert_eq!(reason.kind(), want, "wrong kind for {:?}", reason);
    }
}

#[test]
fn cellkind__an_unknown_end_reason_is_still_usable() {
    // The code point space is open: an unrecognised reason must not panic and
    // must fall back to a generic remote stream failure.
    let unknown = EndReason::from(200_u8);
    assert_eq!(u8::from(unknown), 200);
    assert_eq!(unknown.kind(), ErrorKind::RemoteStreamError);
    assert_eq!(unknown.to_string(), "200");
    // Recognised reasons render by name.
    assert_eq!(EndReason::EXITPOLICY.to_string(), "EXITPOLICY");
    assert_eq!(format!("{:?}", EndReason::DONE), "EndReason(DONE)");
}
