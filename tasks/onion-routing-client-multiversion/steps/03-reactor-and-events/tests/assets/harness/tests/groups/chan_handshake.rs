//! Group `chan_handshake` (inherited) - the outbound channel handshake.
//!
//! instruction.md section 1 requires the `tor-proto` reactor refactor to be
//! made without breaking what `tor-proto` already did: the channel handshake
//! is the entry point every other `tor-proto` capability depends on, and it
//! survived the refactor unchanged.
//!
//! Graded items: `tor_proto::channel::ChannelBuilder::{new, launch}`,
//! `OutboundClientHandshake::connect`, and the errors it reports.
//!
//! The relay side is a fixed byte script built by `common` from the cell
//! framing the Tor specification defines (see SPEC_NOTES.md): a 2-byte CircID
//! before version negotiation, 4 bytes after it; commands VERSIONS=7,
//! NETINFO=8, VPADDING=128, CERTS=129. Nothing here reads a socket or a clock.

use crate::common;

/// The exact bytes a conforming client sends first: CircID 0 on two bytes,
/// command VERSIONS, a 2-byte length of 2, and the single version 4.
const CLIENT_VERSIONS: &[u8] = &[0x00, 0x00, 0x07, 0x00, 0x02, 0x00, 0x04];

/// A scripted relay that speaks link protocol 4.
fn relay_v4(rest: &[&[u8]]) -> Vec<u8> {
    let mut s = common::versions_cell(&[4]);
    for part in rest {
        s.extend_from_slice(part);
    }
    s
}

#[test]
fn chan_handshake__client_opens_with_a_versions_cell() {
    // The peer says nothing at all.
    let out = common::run_handshake(Vec::new());
    assert_eq!(
        out.written, CLIENT_VERSIONS,
        "the client's first write must be the VERSIONS cell for link protocol 4"
    );
    assert_eq!(
        out.err(),
        "channel protocol violation: Doesn't seem to be a tor relay"
    );
}

#[test]
fn chan_handshake__first_cell_must_be_versions() {
    // A CERTS cell where the VERSIONS cell belongs.
    let script = common::var_cell_v4(0, common::CMD_CERTS, &common::certs_body_empty());
    let out = common::run_handshake(script);
    assert_eq!(
        out.err(),
        "channel protocol violation: Doesn't seem to be a tor relay"
    );
}

#[test]
fn chan_handshake__no_shared_link_protocol_is_refused() {
    // The relay offers only obsolete link protocols.
    let out = common::run_handshake(common::versions_cell(&[1, 2, 3]));
    assert_eq!(
        out.err(),
        "channel protocol violation: No shared link protocols"
    );
    assert_eq!(out.written, CLIENT_VERSIONS);
}

#[test]
fn chan_handshake__certs_cell_is_mandatory() {
    // Versions negotiate, but the relay goes straight to NETINFO.
    let script = relay_v4(&[&common::fixed_cell_v4(
        0,
        common::CMD_NETINFO,
        &common::netinfo_body_minimal(),
    )]);
    let out = common::run_handshake(script);
    assert_eq!(out.err(), "channel protocol violation: Missing certs cell");
}

#[test]
fn chan_handshake__netinfo_cell_is_mandatory() {
    // The relay sends CERTS and then closes without a NETINFO cell.
    let script = relay_v4(&[&common::var_cell_v4(
        0,
        common::CMD_CERTS,
        &common::certs_body_empty(),
    )]);
    let out = common::run_handshake(script);
    assert_eq!(
        out.err(),
        "channel protocol violation: Missing netinfo or closed stream"
    );
}

#[test]
fn chan_handshake__duplicate_certs_cell_is_refused() {
    let certs = common::var_cell_v4(0, common::CMD_CERTS, &common::certs_body_empty());
    let script = relay_v4(&[&certs, &certs]);
    let out = common::run_handshake(script);
    assert_eq!(
        out.err(),
        "channel protocol violation: Duplicate certs cell"
    );
}

#[test]
fn chan_handshake__certs_then_netinfo_completes_negotiation() {
    // Padding cells are permitted anywhere in the handshake and are ignored.
    let script = relay_v4(&[
        &common::var_cell_v4(0, common::CMD_VPADDING, &[0xff; 4]),
        &common::var_cell_v4(0, common::CMD_CERTS, &common::certs_body_empty()),
        &common::fixed_cell_v4(0, common::CMD_PADDING, &[]),
        &common::fixed_cell_v4(0, common::CMD_NETINFO, &common::netinfo_body_minimal()),
    ]);
    let out = common::run_handshake(script);
    assert!(
        out.result.is_ok(),
        "handshake should have completed, got {:?}",
        out.result
    );
    // The client writes its VERSIONS cell and nothing else during negotiation.
    assert_eq!(out.written, CLIENT_VERSIONS);
}
