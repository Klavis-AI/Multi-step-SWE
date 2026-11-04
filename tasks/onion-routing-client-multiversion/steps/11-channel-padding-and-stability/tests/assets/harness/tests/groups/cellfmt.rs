//! Group `cellfmt`: channel-cell framing and command numbering (tor-cell).
//!
//! INHERITED. Command numbers, the fixed-length cell payload size, the
//! variable-length rule and the circuit-ID rules all predate this milestone and
//! are fixed by tor-spec.txt section 3. They are graded here because this
//! milestone adds a *new* channel cell to the same numbering space -- the
//! padding negotiation cell, command 12 -- and the natural way to get that
//! wrong is to renumber or re-classify the cells around it.
#![allow(non_snake_case)]

use tor_bytes::Reader;
use tor_cell::chancell::msg::{Body, ChanMsg, Netinfo, Padding, Unrecognized, Versions};
use tor_cell::chancell::{ChanCell, ChanCmd, CircId, CELL_DATA_LEN};

use crate::common::encode_body;

/// The command byte of each cell type is fixed by the wire protocol.
#[test]
fn cellfmt__command_numbers_are_fixed() {
    for (cmd, want) in [
        (ChanCmd::PADDING, 0u8),
        (ChanCmd::CREATE, 1),
        (ChanCmd::CREATED, 2),
        (ChanCmd::RELAY, 3),
        (ChanCmd::DESTROY, 4),
        (ChanCmd::CREATE_FAST, 5),
        (ChanCmd::CREATED_FAST, 6),
        (ChanCmd::VERSIONS, 7),
        (ChanCmd::NETINFO, 8),
        (ChanCmd::RELAY_EARLY, 9),
        (ChanCmd::CREATE2, 10),
        (ChanCmd::CREATED2, 11),
        (ChanCmd::PADDING_NEGOTIATE, 12),
        (ChanCmd::VPADDING, 128),
        (ChanCmd::CERTS, 129),
        (ChanCmd::AUTH_CHALLENGE, 130),
        (ChanCmd::AUTHENTICATE, 131),
    ] {
        assert_eq!(u8::from(cmd), want, "wrong number for {cmd}");
    }
    assert_eq!(CELL_DATA_LEN, 509, "fixed-length cell payload");
}

/// Variable-length cells are exactly VERSIONS and the commands from 128 up.
#[test]
fn cellfmt__var_cells_are_identified_by_command() {
    assert!(ChanCmd::VERSIONS.is_var_cell(), "VERSIONS is the numbering exception");
    assert!(ChanCmd::VPADDING.is_var_cell());
    assert!(ChanCmd::CERTS.is_var_cell());
    assert!(ChanCmd::from(200u8).is_var_cell(), "unknown commands >= 128");

    assert!(!ChanCmd::PADDING.is_var_cell());
    assert!(!ChanCmd::RELAY.is_var_cell());
    assert!(
        !ChanCmd::PADDING_NEGOTIATE.is_var_cell(),
        "command 12 is a fixed-length cell despite being the newest one"
    );
    assert!(!ChanCmd::from(13u8).is_var_cell());
}

/// Channel-wide cells take circuit ID zero; circuit-scoped cells do not.
#[test]
fn cellfmt__circid_rules_depend_on_the_command() {
    let zero = CircId::from(0);
    let some = CircId::from(0x1234_5678);
    assert!(zero.is_zero());
    assert!(!some.is_zero());

    for cmd in [
        ChanCmd::PADDING,
        ChanCmd::NETINFO,
        ChanCmd::VERSIONS,
        ChanCmd::PADDING_NEGOTIATE,
        ChanCmd::CERTS,
    ] {
        assert!(cmd.accepts_circid_val(zero), "{cmd} is a channel-wide cell");
        assert!(!cmd.accepts_circid_val(some), "{cmd} must not name a circuit");
    }
    for cmd in [ChanCmd::RELAY, ChanCmd::CREATE2, ChanCmd::DESTROY] {
        assert!(!cmd.accepts_circid_val(zero), "{cmd} must name a circuit");
        assert!(cmd.accepts_circid_val(some));
    }
    let unknown = ChanCmd::from(250u8);
    assert!(unknown.accepts_circid_val(zero), "unknown commands are unconstrained");
    assert!(unknown.accepts_circid_val(some));
}

/// A `ChanCell` is a circuit ID plus a message, and both come back out.
#[test]
fn cellfmt__chan_cell_carries_circid_and_message() {
    let cell = ChanCell::new(CircId::from(7), Padding::new().into_message());
    assert_eq!(u32::from(cell.circid()), 7);
    assert_eq!(cell.msg().cmd(), ChanCmd::PADDING);

    let (id, msg) = cell.into_circid_and_msg();
    assert_eq!(u32::from(id), 7);
    assert_eq!(msg.cmd(), ChanCmd::PADDING);
    assert!(
        encode_body(msg).is_empty(),
        "a PADDING body carries no content of its own"
    );
}

/// A VERSIONS body is a bare sequence of big-endian u16s.
#[test]
fn cellfmt__versions_body_is_a_list_of_u16() {
    let want: &[u8] = include_bytes!("../../fixtures/versions_3_4_5.bin");
    let v = Versions::new([3u16, 4, 5]).expect("three versions is not too many");
    assert_eq!(encode_body(v.into_message()), want);

    let mut r = Reader::from_slice(want);
    let decoded = match ChanMsg::take(&mut r, ChanCmd::VERSIONS).expect("a VERSIONS body") {
        ChanMsg::Versions(v) => v,
        other => panic!("expected VERSIONS, got {:?}", other.cmd()),
    };
    assert_eq!(
        decoded.best_shared_link_protocol(&[4, 5, 100]),
        Some(5),
        "the highest link protocol both sides name"
    );
    assert_eq!(decoded.best_shared_link_protocol(&[1, 2]), None);
}

/// The VERSIONS cell is framed with a two-byte circuit ID, because versions
/// are negotiated before the four-byte format is in force.
#[test]
fn cellfmt__versions_handshake_framing_uses_a_two_byte_circid() {
    let v = Versions::new([3u16, 4, 5]).expect("three versions is not too many");
    let framed = v.encode_for_handshake().expect("encoding a VERSIONS cell");
    assert_eq!(
        framed,
        [0x00, 0x00, 0x07, 0x00, 0x06, 0x00, 0x03, 0x00, 0x04, 0x00, 0x05],
        "circid(2) | cmd(1) | len(2) | body, got {framed:02x?}"
    );
}

/// A client's NETINFO reports no timestamp and no addresses of its own.
#[test]
fn cellfmt__netinfo_client_body_is_timestampless() {
    let want: &[u8] = include_bytes!("../../fixtures/netinfo_client_unspec.bin");
    let n = Netinfo::for_client(None);
    assert_eq!(
        encode_body(n.into_message()),
        want,
        "timestamp(4) | their_addr(type,len,0.0.0.0) | n_my_addrs = 0"
    );

    let mut r = Reader::from_slice(want);
    let decoded = match ChanMsg::take(&mut r, ChanCmd::NETINFO).expect("a NETINFO body") {
        ChanMsg::Netinfo(n) => n,
        other => panic!("expected NETINFO, got {:?}", other.cmd()),
    };
    assert!(
        decoded.timestamp().is_none(),
        "a zero timestamp means 'not reported', not 1970"
    );
}

/// An unknown command is preserved rather than discarded, so a cell from a
/// future protocol version can be recognised and ignored rather than
/// mis-parsed.
#[test]
fn cellfmt__unrecognized_commands_are_preserved() {
    let cmd = ChanCmd::from(200u8);
    let body = [0xde_u8, 0xad, 0xbe, 0xef];

    let mut r = Reader::from_slice(&body);
    let msg = ChanMsg::take(&mut r, cmd).expect("an unknown command is not a parse failure");
    assert_eq!(msg.cmd(), cmd, "the command is remembered");
    assert_eq!(encode_body(msg), body, "and so is the body, byte for byte");

    let built = Unrecognized::new(cmd, &body[..]).into_message();
    assert_eq!(built.cmd(), cmd);
    assert_eq!(encode_body(built), body);
}
